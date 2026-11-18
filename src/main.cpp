// ============================================================================
// main.cpp - Tamarin compiler CLI entry point
// ============================================================================

#include "tamarin/driver/driver.h"
#include "tamarin/config.h"
#include "troop/cli.h"

#include <iostream>
#include <string_view>

static void printVersion() {
    std::cout << "Tamarin compiler version " << TAMARIN_VERSION_STRING
              << " (TAMARIN v" << TAMARIN_VERSION_STRING << ")\n"
              << "Target: x86_64-pc-windows-msvc\n";
}

static void printHelp() {
    std::cout << "Usage: tamarin <command> [options] <source.tmn>\n\n"
              << "Commands:\n"
              << "  check <file>     Validate source syntax and types without code generation\n"
              << "  build <file>     Compile source file to a native executable\n"
              << "  run   <file>     Compile and immediately run the executable\n"
              << "  troop <args>     Invoke the Troop package manager\n\n"
              << "Options:\n"
              << "  -o <path>             Specify output executable path\n"
              << "  -I <path>             Add directory to module search path\n"
              << "  -O0, -O1, -O2, -O3    Optimization levels (-O0: debug, -O2: release)\n"
              << "  --fast-math           Enable aggressive floating-point reassociation and SIMD vectorization\n"
              << "  --emit-tir, --emit-ir Dump Tamarin Intermediate Representation (TIR)\n"
              << "  --emit-llvm           Dump raw unoptimized LLVM IR\n"
              << "  --emit-optimized-llvm Dump optimized LLVM IR\n"
              << "  --emit-asm            Dump native x86-64 assembly\n"
              << "  --memory-stats        Enable runtime memory telemetry for regions\n"
              << "  --emit-memory-report  Emit static memory planning report for declarations\n"
              << "  --target-cpu <cpu>    Target CPU architecture (e.g. native, x86-64, x86-64-v3)\n"
              << "  --target <triple>     Target platform triple (e.g. x86_64-pc-windows-msvc, x86_64-unknown-linux-gnu, aarch64-apple-darwin)\n"
              << "  --emit=<format>       Output format: exe, obj, lib, shared\n"
              << "  --lib                 Emit static library archive\n"
              << "  --shared              Emit dynamic/shared library\n"
              << "  --emit-header <path>  Emit C99/C++ header (.h) for exported symbols\n"
              << "  --static, --crt=static   Statically link C runtime (/MT on MSVC, -static on POSIX)\n"
              << "  --dynamic, --crt=dynamic Dynamically link C runtime (default: /MD on MSVC, dynamic on POSIX)\n"
              << "  --clang <path>        Specify path to the clang executable\n"
              << "  --verbose             Enable verbose compiler output\n"
              << "  --version             Display version information\n"
              << "  --help                Display this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp();
        return 1;
    }

    if (std::string_view(argv[1]) == "troop") {
        return troop::cliMain(argc - 1, argv + 1);
    }

    tamarin::driver::DriverOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--version" || arg == "-v") {
            printVersion();
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "--emit-ir" || arg == "--emit-tir") {
            opts.emitTIR = true;
            opts.emitIR = true;
        } else if (arg == "--emit-llvm") {
            opts.emitLLVM = true;
        } else if (arg == "--emit-optimized-llvm") {
            opts.emitOptimizedLLVM = true;
        } else if (arg == "--emit-asm") {
            opts.emitAsm = true;
        } else if (arg == "--fast-math" || arg == "-ffast-math") {
            opts.fastMath = true;
        } else if (arg == "--memory-stats") {
            opts.memoryStats = true;
        } else if (arg == "--emit-memory-report") {
            opts.emitMemoryReport = true;
        } else if (arg == "--target-cpu" && i + 1 < argc) {
            opts.targetCPU = argv[++i];
        } else if (arg.starts_with("--target-cpu=") && arg.size() > 13) {
            opts.targetCPU = std::string(arg.substr(13));
        } else if (arg == "--target" && i + 1 < argc) {
            opts.targetTriple = argv[++i];
        } else if (arg.starts_with("--target=") && arg.size() > 9) {
            opts.targetTriple = std::string(arg.substr(9));
        } else if (arg == "--emit=exe") {
            opts.outputKind = tamarin::driver::DriverOptions::OutputKind::Exe;
        } else if (arg == "--emit=obj" || arg == "--emit-obj") {
            opts.outputKind = tamarin::driver::DriverOptions::OutputKind::Obj;
        } else if (arg == "--emit=lib" || arg == "--emit-lib") {
            opts.outputKind = tamarin::driver::DriverOptions::OutputKind::StaticLib;
        } else if (arg == "--emit=shared" || arg == "--emit-shared") {
            opts.outputKind = tamarin::driver::DriverOptions::OutputKind::SharedLib;
        } else if (arg == "--lib") {
            opts.outputKind = tamarin::driver::DriverOptions::OutputKind::StaticLib;
        } else if (arg == "--shared") {
            opts.outputKind = tamarin::driver::DriverOptions::OutputKind::SharedLib;
        } else if (arg == "--emit-header") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                opts.headerPath = argv[++i];
            } else {
                opts.headerPath = "__AUTO__";
            }
        } else if (arg.starts_with("--emit-header=") && arg.size() > 14) {
            opts.headerPath = std::string(arg.substr(14));
        } else if (arg == "--static" || arg == "--crt=static") {
            opts.crtLinkage = tamarin::driver::DriverOptions::CrtLinkage::Static;
        } else if (arg == "--dynamic" || arg == "--crt=dynamic") {
            opts.crtLinkage = tamarin::driver::DriverOptions::CrtLinkage::Dynamic;
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "-O0") {
            opts.optLevel = 0;
        } else if (arg == "-O1") {
            opts.optLevel = 1;
        } else if (arg == "-O2") {
            opts.optLevel = 2;
        } else if (arg == "-O3") {
            opts.optLevel = 3;
        } else if (arg == "-o" && i + 1 < argc) {
            opts.outputFile = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            opts.includeDirs.push_back(argv[++i]);
        } else if (arg.starts_with("-I") && arg.size() > 2) {
            opts.includeDirs.push_back(std::string(arg.substr(2)));
        } else if (arg == "--clang" && i + 1 < argc) {
            opts.clangPath = argv[++i];
        } else if (arg == "check") {
            opts.command = tamarin::driver::DriverOptions::Command::Check;
        } else if (arg == "build") {
            opts.command = tamarin::driver::DriverOptions::Command::Build;
        } else if (arg == "run") {
            opts.command = tamarin::driver::DriverOptions::Command::Run;
        } else if (arg.starts_with("-")) {
            std::cerr << "tamarin: unknown option: " << arg << "\n";
            return 1;
        } else {
            opts.inputFile = arg;
        }
    }

    if (opts.inputFile.empty()) {
        std::cerr << "tamarin: error: no input file specified\n";
        return 1;
    }

    tamarin::driver::Driver driver(std::move(opts));
    return driver.run();
}

