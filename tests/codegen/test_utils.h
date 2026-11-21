#pragma once

#include "test_harness.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"
#include "tamarin/sema/sema.h"
#include "tamarin/ir/ir_builder.h"
#include "tamarin/ir/opt/pass_manager.h"
#include "tamarin/ir/opt/bounds_check_elimination.h"
#include "tamarin/ir/opt/range_analysis.h"
#include "tamarin/ir/opt/alias_analysis.h"
#include "tamarin/backend/llvm_codegen.h"
#include "tamarin/backend/llvm_verifier.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"
#include "tamarin/driver/module_graph.h"

#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace test_utils {

inline std::string getStdlibDir() {
    if (const char* env = std::getenv("TAMARIN_STDLIB_DIR")) {
        return env;
    }
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "lib",
        std::filesystem::current_path().parent_path() / "lib",
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "lib"
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec) && std::filesystem::is_directory(c, ec)) {
            return std::filesystem::canonical(c, ec).string();
        }
    }
    return "";
}

inline bool checkValid(const std::string& code) {
    std::string fullCode = code;
    if (fullCode.find("main") == std::string::npos) {
        fullCode += "\nint main() { return 0; }\n";
    }
    if (fullCode.find("import ") != std::string::npos) {
        static int runId = 0;
        auto tempDir = std::filesystem::temp_directory_path() / ("tamarin_val_" + std::to_string(++runId));
        std::filesystem::create_directories(tempDir);
        auto mainPath = tempDir / "main.tmn";
        {
            std::ofstream out(mainPath);
            out << fullCode;
        }
        tamarin::SourceManager sm;
        std::ostringstream errs;
        tamarin::DiagnosticEngine diag(sm, errs);
        tamarin::driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
        bool bgOk = graph.buildGraph(mainPath.string());
        if (!bgOk || diag.hasErrors()) {
            std::error_code ec;
            std::filesystem::remove_all(tempDir, ec);
            return false;
        }
        tamarin::sema::Sema sema(diag);
        bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
        return semaOk && !diag.hasErrors();
    }
    tamarin::SourceManager sm;
    auto fileId = sm.addSource("test.tmn", fullCode);
    std::ostringstream errs;
    tamarin::DiagnosticEngine diag(sm, errs);
    tamarin::lexer::Lexer lex(sm, fileId, diag);
    tamarin::parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    tamarin::sema::Sema sema(diag);
    return sema.analyze(prog) && !diag.hasErrors();
}

inline bool checkInvalid(const std::string& code, const std::string& expectedErr = "") {
    std::string fullCode = code;
    if (fullCode.find("main") == std::string::npos) {
        fullCode += "\nint main() { return 0; }\n";
    }
    tamarin::SourceManager sm;
    auto fileId = sm.addSource("test.tmn", fullCode);
    std::ostringstream errs;
    tamarin::DiagnosticEngine diag(sm, errs);
    tamarin::lexer::Lexer lex(sm, fileId, diag);
    tamarin::parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    tamarin::sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (ok && !diag.hasErrors()) return false;
    if (!expectedErr.empty()) {
        if (errs.str().find(expectedErr) == std::string::npos) {
            std::cerr << "Expected error substring '" << expectedErr << "' not found in: " << errs.str() << "\n";
            return false;
        }
    }
    return true;
}

inline bool checkCodegen(const std::string& code, const std::string& expectedIR = "") {
    std::string fullCode = code;
    if (fullCode.find("main") == std::string::npos) {
        fullCode += "\nint main() { return 0; }\n";
    }
    if (fullCode.find("import ") != std::string::npos) {
        static int runId = 0;
        auto tempDir = std::filesystem::temp_directory_path() / ("tamarin_cg_" + std::to_string(++runId));
        std::filesystem::create_directories(tempDir);
        auto mainPath = tempDir / "main.tmn";
        {
            std::ofstream out(mainPath);
            out << fullCode;
        }
        tamarin::SourceManager sm;
        std::ostringstream errs;
        tamarin::DiagnosticEngine diag(sm, errs);
        tamarin::driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
        bool bgOk = graph.buildGraph(mainPath.string());
        if (!bgOk || diag.hasErrors()) {
            std::error_code ec;
            std::filesystem::remove_all(tempDir, ec);
            return false;
        }
        tamarin::sema::Sema sema(diag);
        if (!sema.analyzeModules(graph.getTopologicalOrder()) || diag.hasErrors()) {
            std::error_code ec;
            std::filesystem::remove_all(tempDir, ec);
            return false;
        }
        tamarin::ir::IRBuilder irBuilder(sema.checker());
        auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
        if (!mod) {
            std::error_code ec;
            std::filesystem::remove_all(tempDir, ec);
            return false;
        }
        tamarin::backend::LLVMCodeGen codegen(*mod, diag);
        std::ostringstream llvmIR;
        bool emitted = codegen.emitLLVMIR(llvmIR);
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
        if (!emitted) return false;
        if (!expectedIR.empty() && llvmIR.str().find(expectedIR) == std::string::npos) {
            std::cerr << "Expected IR substring '" << expectedIR << "' not found in:\n" << llvmIR.str() << "\n";
            return false;
        }
        return true;
    }
    tamarin::SourceManager sm;
    auto fileId = sm.addSource("test.tmn", fullCode);
    std::ostringstream errs;
    tamarin::DiagnosticEngine diag(sm, errs);
    tamarin::lexer::Lexer lex(sm, fileId, diag);
    tamarin::parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    tamarin::sema::Sema sema(diag);
    if (!sema.analyze(prog) || diag.hasErrors()) {
        std::cerr << "Sema failed unexpectedly: " << errs.str() << "\n";
        return false;
    }
    tamarin::ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "test");
    if (!mod) return false;
    tamarin::backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    if (!codegen.emitLLVMIR(llvmIR)) return false;
    if (!expectedIR.empty() && llvmIR.str().find(expectedIR) == std::string::npos) {
        std::cerr << "Expected IR substring '" << expectedIR << "' not found in:\n" << llvmIR.str() << "\n";
        return false;
    }
    return true;
}

inline bool checkCodegenFails(const std::string& code) {
    tamarin::SourceManager sm;
    auto fileId = sm.addSource("test.tmn", code);
    std::ostringstream errs;
    tamarin::DiagnosticEngine diag(sm, errs);
    tamarin::lexer::Lexer lex(sm, fileId, diag);
    tamarin::parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    tamarin::sema::Sema sema(diag);
    if (!sema.analyze(prog) || diag.hasErrors()) return true;
    tamarin::ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "test");
    if (!mod) return true;
    tamarin::backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    return !codegen.emitLLVMIR(llvmIR) || diag.hasErrors();
}

} // namespace test_utils
