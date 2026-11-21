// ============================================================================
// driver.cpp - Compiler orchestration pipeline implementation
// ============================================================================

#include "tamarin/driver/driver.h"
#include "tamarin/driver/module_graph.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"
#include "tamarin/sema/sema.h"
#include "tamarin/ir/ir_builder.h"
#include "tamarin/ir/opt/pass_manager.h"
#include "tamarin/backend/llvm_codegen.h"
#include "tamarin/backend/llvm_verifier.h"
#include "tamarin/backend/c_header_generator.h"
#include "tamarin/target/target_triple.h"
#include "tamarin/target/process_util.h"

#include <iostream>
#include <fstream>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace tamarin::driver {

static std::filesystem::path getSelfExeDir() {
#ifdef _WIN32
    wchar_t exeBuf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH) > 0) {
        return std::filesystem::path(exeBuf).parent_path();
    }
#elif defined(__APPLE__)
    char pathBuf[1024];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        std::error_code ec;
        return std::filesystem::canonical(pathBuf, ec).parent_path();
    }
#else
    char pathBuf[1024];
    ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len > 0) {
        pathBuf[len] = '\0';
        std::error_code ec;
        return std::filesystem::canonical(pathBuf, ec).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

static std::filesystem::path resolveOutputPath(const std::filesystem::path& inputPath,
                                              const std::string& explicitOutput,
                                              const std::string& outExt) {
    if (!explicitOutput.empty()) {
        return std::filesystem::path(explicitOutput);
    }
    if (inputPath.has_extension()) {
        auto p = inputPath;
        return p.replace_extension(outExt);
    }
    return std::filesystem::path(inputPath.string() + outExt);
}

static std::vector<std::string> gatherSearchRoots(const DriverOptions& options) {
    std::vector<std::string> searchRoots;
    std::filesystem::path inputPath(options.inputFile);
    if (inputPath.has_parent_path()) {
        searchRoots.push_back(std::filesystem::absolute(inputPath.parent_path()).string());
    } else {
        searchRoots.push_back(std::filesystem::current_path().string());
    }

    for (const auto& inc : options.includeDirs) {
        searchRoots.push_back(inc);
    }

    if (const char* envPath = std::getenv("TAMARIN_PATH")) {
        std::string envStr(envPath);
        std::string item;
        for (char c : envStr) {
            if (c == ';' || c == ':') {
                if (!item.empty()) {
                    searchRoots.push_back(item);
                    item.clear();
                }
            } else {
                item.push_back(c);
            }
        }
        if (!item.empty()) searchRoots.push_back(item);
    }

    // Standard library search roots
    std::vector<std::filesystem::path> stdlibCandidates;
    if (const char* envLib = std::getenv("TAMARIN_STDLIB_DIR")) {
        stdlibCandidates.push_back(envLib);
    }
    stdlibCandidates.push_back(std::filesystem::current_path() / "lib");
    if (inputPath.has_parent_path()) {
        stdlibCandidates.push_back(std::filesystem::absolute(inputPath.parent_path()) / "lib");
        stdlibCandidates.push_back(std::filesystem::absolute(inputPath.parent_path()).parent_path() / "lib");
    }

    auto exeDir = getSelfExeDir();
    stdlibCandidates.push_back(exeDir / "lib");
    stdlibCandidates.push_back(exeDir.parent_path() / "lib");
    stdlibCandidates.push_back(exeDir.parent_path().parent_path() / "lib");

    for (const auto& candidate : stdlibCandidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            auto can = std::filesystem::canonical(candidate, ec);
            if (!ec) {
                std::string canStr = can.string();
                if (std::find(searchRoots.begin(), searchRoots.end(), canStr) == searchRoots.end()) {
                    searchRoots.push_back(canStr);
                }
            }
        }
    }
    return searchRoots;
}

Driver::Driver(DriverOptions options)
    : options_(std::move(options)), diag_(sm_, std::cerr) {}

int Driver::run() {
    switch (options_.command) {
        case DriverOptions::Command::Check:   return check();
        case DriverOptions::Command::Build:   return build();
        case DriverOptions::Command::Run:     return execute();
        case DriverOptions::Command::Help:
        case DriverOptions::Command::Version: return 0;
    }
    return 1;
}

int Driver::check() {
    auto triple = options_.targetTriple.empty()
        ? target::TargetTriple::getHost()
        : target::TargetTriple::parse(options_.targetTriple);
    auto searchRoots = gatherSearchRoots(options_);
    ModuleGraph graph(sm_, diag_, std::move(searchRoots));
    graph.setTargetTriple(triple);
    if (!graph.buildGraph(options_.inputFile) || diag_.hasErrors()) {
        std::cerr << "TAMARIN compilation failed.\n";
        return 1;
    }

    sema::Sema sema(diag_);
    sema.setTargetTriple(triple);
    if (!sema.analyzeModules(graph.getTopologicalOrder())) {
        std::cerr << "TAMARIN compilation failed.\n";
        return 1;
    }

    if (options_.emitMemoryReport) {
        sema.checker().printMemoryReport(std::cout);
    }

    std::cout << "TAMARIN: Check succeeded for " << options_.inputFile << "\n";
    return 0;
}

int Driver::build() {
    auto triple = options_.targetTriple.empty()
        ? target::TargetTriple::getHost()
        : target::TargetTriple::parse(options_.targetTriple);
    auto searchRoots = gatherSearchRoots(options_);
    ModuleGraph graph(sm_, diag_, std::move(searchRoots));
    graph.setTargetTriple(triple);
    if (!graph.buildGraph(options_.inputFile) || diag_.hasErrors()) {
        std::cerr << "TAMARIN compilation failed.\n";
        return 1;
    }

    sema::Sema sema(diag_);
    sema.setTargetTriple(triple);
    if (!sema.analyzeModules(graph.getTopologicalOrder())) {
        std::cerr << "TAMARIN compilation failed.\n";
        return 1;
    }

    if (options_.emitMemoryReport) {
        sema.checker().printMemoryReport(std::cout);
    }

    ir::IRBuilder irBuilder(sema.checker());
    std::filesystem::path inputPath(options_.inputFile);
    auto moduleName = inputPath.stem().string();
    auto irModule = irBuilder.buildProgram(graph.getTopologicalOrder(), moduleName);

    // 4. TIR Optimization Pipeline
    if (options_.optLevel > 0) {
        auto tirPM = ir::opt::PassManager::createDefaultPipeline(options_.optLevel);
        tirPM.run(*irModule, &sema.checker().context());
    }

    if (options_.emitIR || options_.emitTIR) {
        irModule->dump(std::cout);
    }

    if (!options_.clangPath.empty()) {
        backend::LLVMVerifier::instance().setClangPath(options_.clangPath);
    }

    backend::LLVMVerifier::instance().setTargetTriple(triple.str());

    // C Header generation
    if (!options_.headerPath.empty()) {
        std::string outHeader = options_.headerPath;
        if (outHeader == "__AUTO__") {
            outHeader = (std::filesystem::path(options_.inputFile).stem().string() + ".h");
        }
        backend::CHeaderGenerator headerGen(diag_);
        std::vector<const ast::Decl*> allDecls;
        for (const auto& m : graph.getTopologicalOrder()) {
            if (m && m->ast) {
                for (const auto& d : m->ast->decls) {
                    allDecls.push_back(d.get());
                }
            }
        }
        if (!headerGen.generateDeclsToFile(allDecls, sema.checker(), moduleName, outHeader)) {
            return 1;
        }
        std::cout << "TAMARIN: Successfully generated C header " << outHeader << "\n";
    }

    // 5. LLVM CodeGen
    backend::LLVMCodeGen codegen(*irModule, diag_, options_.clangPath);
    codegen.setTargetTriple(triple);
    codegen.setEnableMemoryStats(options_.memoryStats);
    if (options_.fastMath) {
        codegen.setFastMath(true);
    }
    if (!options_.targetCPU.empty()) {
        codegen.setTargetCPU(options_.targetCPU);
    }
    if (options_.crtLinkage == DriverOptions::CrtLinkage::Static) {
        codegen.setCrtLinkage(backend::CrtLinkage::Static);
    } else {
        codegen.setCrtLinkage(backend::CrtLinkage::Dynamic);
    }
    codegen.setLinkLibraries(options_.linkLibs);
    codegen.setLinkDirectories(options_.linkDirs);
    codegen.setLinkerFlags(options_.linkerFlags);

    auto llPath = inputPath;
    llPath.replace_extension(".ll");

    if (!codegen.emitLLVMIRToFile(llPath)) return 1;

    // Verify generated module before optimization
    if (!backend::LLVMVerifier::instance().verifyAndReport(llPath, diag_)) {
        return 1;
    }

    if (options_.emitLLVM) {
        std::ifstream llIn(llPath);
        if (llIn) {
            std::cout << llIn.rdbuf();
        }
    }

    std::filesystem::path finalLLPath = llPath;
    std::filesystem::path optLLPath;

    // 6. Optimization pipeline
    if (options_.optLevel > 0 || options_.emitOptimizedLLVM) {
        int effOpt = options_.optLevel > 0 ? options_.optLevel : 2;
        optLLPath = inputPath;
        optLLPath.replace_filename(inputPath.stem().string() + "_opt.ll");
        if (!codegen.optimizeLLVMIR(llPath, optLLPath, effOpt)) {
            return 1;
        }

        // Verify module after optimization
        if (!backend::LLVMVerifier::instance().verifyAndReport(optLLPath, diag_)) {
            return 1;
        }
        finalLLPath = optLLPath;

        if (options_.emitOptimizedLLVM) {
            std::ifstream optIn(optLLPath);
            if (optIn) {
                std::cout << optIn.rdbuf();
            }
        }
    }

    if (options_.emitAsm) {
        auto asmPath = inputPath;
        asmPath.replace_extension(".s");
        if (!codegen.compileToAssembly(finalLLPath, asmPath, options_.optLevel)) {
            return 1;
        }
        std::ifstream asmIn(asmPath);
        if (asmIn) {
            std::cout << asmIn.rdbuf();
        }
    }

    auto cleanupTemps = [&]() {
        std::error_code ec;
        if (!options_.emitLLVM) {
            std::filesystem::remove(llPath, ec);
        }
        if (!optLLPath.empty() && !options_.emitOptimizedLLVM) {
            std::filesystem::remove(optLLPath, ec);
        }
    };

    // 7. Output emission based on OutputKind
    int exitCode = 0;
    if (options_.outputKind == DriverOptions::OutputKind::Obj) {
        auto outExt = triple.objectExtension();
        auto objPath = resolveOutputPath(inputPath, options_.outputFile, outExt);
        if (!codegen.compileToObject(finalLLPath, objPath, options_.optLevel)) {
            exitCode = 1;
        } else {
            std::cout << "TAMARIN: Successfully compiled object file " << objPath.string() << "\n";
        }
    } else if (options_.outputKind == DriverOptions::OutputKind::StaticLib) {
        auto outExt = triple.staticLibraryExtension();
        auto libPath = resolveOutputPath(inputPath, options_.outputFile, outExt);
        if (!codegen.compileToStaticLibrary(finalLLPath, libPath, options_.optLevel)) {
            exitCode = 1;
        } else {
            std::cout << "TAMARIN: Successfully built static library " << libPath.string() << "\n";
        }
    } else if (options_.outputKind == DriverOptions::OutputKind::SharedLib) {
        auto outExt = triple.sharedLibraryExtension();
        auto dylibPath = resolveOutputPath(inputPath, options_.outputFile, outExt);
        if (!codegen.compileToSharedLibrary(finalLLPath, dylibPath, options_.optLevel)) {
            exitCode = 1;
        } else {
            std::cout << "TAMARIN: Successfully built shared library " << dylibPath.string() << "\n";
        }
    } else {
        auto outExt = triple.executableExtension();
        auto exePath = resolveOutputPath(inputPath, options_.outputFile, outExt);

        if (!codegen.compileToExecutable(finalLLPath, exePath, options_.optLevel)) {
            exitCode = 1;
        } else {
            std::cout << "TAMARIN: Successfully built " << exePath.string() << "\n";
        }
    }

    cleanupTemps();
    return exitCode;
}

int Driver::execute() {
    int buildResult = build();
    if (buildResult != 0) return buildResult;

    auto triple = options_.targetTriple.empty()
        ? target::TargetTriple::getHost()
        : target::TargetTriple::parse(options_.targetTriple);
    auto outExt = triple.executableExtension();

    std::filesystem::path inputPath(options_.inputFile);
    auto exePath = resolveOutputPath(inputPath, options_.outputFile, outExt);

    auto absExe = std::filesystem::absolute(exePath);
    return target::runProcess({ absExe.string() });
}

} // namespace tamarin::driver
