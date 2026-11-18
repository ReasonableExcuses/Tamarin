// ============================================================================
// codegen_test.cpp - Unit tests for Tamarin IR and LLVM code generation
// ============================================================================

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
#include "tamarin/target/target_triple.h"
#include "tamarin/backend/c_header_generator.h"
#include "tamarin/runtime/page_provider.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <sstream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace tamarin;

TAMARIN_TEST(CodeGen, EmitsValidIR) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    return 42;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "test");
    EXPECT_TRUE(mod != nullptr);
    EXPECT_EQ(mod->functions.size(), 1);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    EXPECT_TRUE(irStr.find("define i32 @main") != std::string::npos);
    EXPECT_TRUE(irStr.find("ret i32") != std::string::npos);

    return true;
}

TAMARIN_TEST(CodeGen, EmitsSumExampleIR) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int sum(int values[]) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < len(values); i++) {\n"
        "        total += values[i];\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    int numbers[3] = {10, 20, 30};\n"
        "    return sum(numbers);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "sum_test");
    EXPECT_TRUE(mod != nullptr);
    EXPECT_EQ(mod->functions.size(), 2);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    EXPECT_TRUE(irStr.find("define i32 @sum") != std::string::npos);
    EXPECT_TRUE(irStr.find("define i32 @main") != std::string::npos);
    EXPECT_TRUE(irStr.find("@__tamarin_bounds_error") != std::string::npos);
    EXPECT_TRUE(irStr.find("icmp slt i32") != std::string::npos);
    EXPECT_TRUE(irStr.find("icmp uge i64") != std::string::npos);

    return true;
}

TAMARIN_TEST(CodeGen, BoundsCheckInserted) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int get(int arr[]) {\n"
        "    return arr[5];\n"
        "}\n"
        "int main() {\n"
        "    int v[2] = {1, 2};\n"
        "    return get(v);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in BoundsCheckInserted: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "bounds_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    EXPECT_TRUE(irStr.find("bchk_fail_") != std::string::npos);
    EXPECT_TRUE(irStr.find("call void @__tamarin_bounds_error()") != std::string::npos);

    return true;
}

TAMARIN_TEST(CodeGen, ControlFlow_BothBranchesReturningNoMergeBlock) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int branch_func(int x) {\n"
        "    if (x > 0) {\n"
        "        return 1;\n"
        "    } else {\n"
        "        return 2;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return branch_func(5);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "cf_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    // In v0.2, when both branches return, no merge block is created!
    EXPECT_TRUE(irStr.find("merge_") == std::string::npos);
    EXPECT_TRUE(irStr.find("then_") != std::string::npos);
    EXPECT_TRUE(irStr.find("else_") != std::string::npos);

    // Verify generated IR is valid LLVM
    auto vRes = backend::LLVMVerifier::instance().verifyIR(irStr);
    if (!vRes.ok) {
        std::cerr << "Verification error: " << vRes.errorMessage << "\n";
        std::cerr << "Generated IR:\n" << irStr << "\n";
    }
    EXPECT_TRUE(vRes.ok);

    return true;
}

TAMARIN_TEST(CodeGen, ControlFlow_OneBranchReturning) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int early_ret(int x) {\n"
        "    if (x > 0) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 2;\n"
        "}\n"
        "int main() {\n"
        "    return early_ret(5);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "early_ret_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    auto vRes = backend::LLVMVerifier::instance().verifyIR(irStr);
    if (!vRes.ok) {
        std::cerr << "Verification error: " << vRes.errorMessage << "\n";
        std::cerr << "Generated IR:\n" << irStr << "\n";
    }
    EXPECT_TRUE(vRes.ok);

    return true;
}

TAMARIN_TEST(CodeGen, ControlFlow_UnreachableDeadCodeEliminated) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int dead_code() {\n"
        "    return 42;\n"
        "    int x = 100;\n"
        "    return x;\n"
        "}\n"
        "int main() {\n"
        "    return dead_code();\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "dead_code_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    auto vRes = backend::LLVMVerifier::instance().verifyIR(irStr);
    if (!vRes.ok) {
        std::cerr << "Verification error: " << vRes.errorMessage << "\n";
        std::cerr << "Generated IR:\n" << irStr << "\n";
    }
    EXPECT_TRUE(vRes.ok);

    return true;
}

TAMARIN_TEST(CodeGen, LLVMVerifier_ValidModule) {
    std::string validIR =
        "; ModuleID = 'test'\n"
        "target datalayout = \"e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n"
        "target triple = \"x86_64-pc-windows-msvc\"\n\n"
        "define i32 @main() {\n"
        "entry:\n"
        "  ret i32 0\n"
        "}\n";

    auto vRes = backend::LLVMVerifier::instance().verifyIR(validIR);
    EXPECT_TRUE(vRes.ok);
    EXPECT_TRUE(vRes.errorMessage.empty());

    return true;
}

TAMARIN_TEST(CodeGen, LLVMVerifier_CatchesInvalidIR) {
    std::string invalidIR =
        "; ModuleID = 'invalid_test'\n"
        "target datalayout = \"e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n"
        "target triple = \"x86_64-pc-windows-msvc\"\n\n"
        "define i32 @broken() {\n"
        "entry:\n"
        "  %x = add i32 1, 2\n"
        "}\n";

    auto vRes = backend::LLVMVerifier::instance().verifyIR(invalidIR);
    EXPECT_FALSE(vRes.ok);
    EXPECT_FALSE(vRes.errorMessage.empty());

    return true;
}

TAMARIN_TEST(CodeGen, ArraySafety_NegativeIndexAndWidthReconciliation) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int access_elem(int arr[], int idx) {\n"
        "    return arr[idx];\n"
        "}\n"
        "int main() {\n"
        "    int v[3] = {10, 20, 30};\n"
        "    return access_elem(v, 1);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "array_safety_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    // Negative signed index check
    EXPECT_TRUE(irStr.find("icmp slt i32") != std::string::npos);
    // Width reconciliation: 32-bit index sign- or zero-extended to 64-bit
    EXPECT_TRUE(irStr.find("sext i32") != std::string::npos || irStr.find("zext i32") != std::string::npos);
    EXPECT_TRUE(irStr.find("to i64") != std::string::npos);
    // 64-bit comparison against length
    EXPECT_TRUE(irStr.find("icmp uge i64") != std::string::npos);
    // Disjunction
    EXPECT_TRUE(irStr.find("or i1") != std::string::npos);

    auto vRes = backend::LLVMVerifier::instance().verifyIR(irStr);
    if (!vRes.ok) {
        std::cerr << "Verification error: " << vRes.errorMessage << "\n";
        std::cerr << "Generated IR:\n" << irStr << "\n";
    }
    EXPECT_TRUE(vRes.ok);

    return true;
}

TAMARIN_TEST(CodeGen, ArraySafety_EmptyArrayAccess) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int get_elem(int arr[], int idx) {\n"
        "    return arr[idx];\n"
        "}\n"
        "int main() {\n"
        "    int empty[0];\n"
        "    return get_elem(empty, 0);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in ArraySafety_EmptyArrayAccess: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "empty_arr_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    // Runtime bounds check against length from view (which is 0)
    EXPECT_TRUE(irStr.find("icmp slt i32") != std::string::npos);
    EXPECT_TRUE(irStr.find("icmp uge i64") != std::string::npos);
    EXPECT_TRUE(irStr.find("or i1") != std::string::npos);

    auto vRes = backend::LLVMVerifier::instance().verifyIR(irStr);
    if (!vRes.ok) {
        std::cerr << "Verification error in EmptyArrayAccess: " << vRes.errorMessage << "\n";
        std::cerr << "Generated IR:\n" << irStr << "\n";
    }
    EXPECT_TRUE(vRes.ok);

    return true;
}

TAMARIN_TEST(CodeGen, Optimization_O0vsO2Equivalence) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int compute() {\n"
        "    int total = 0;\n"
        "    for (int i = 1; i <= 10; i++) {\n"
        "        total += i;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    return compute();\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "equiv_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);

    auto tempDir = std::filesystem::temp_directory_path();
    auto unoptLL = tempDir / "equiv_unopt.ll";
    auto optLL   = tempDir / "equiv_opt.ll";
    auto exeO0   = tempDir / "equiv_O0.exe";
    auto exeO2   = tempDir / "equiv_O2.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(unoptLL));
    EXPECT_TRUE(codegen.optimizeLLVMIR(unoptLL, optLL, 2));

    EXPECT_TRUE(codegen.compileToExecutable(unoptLL, exeO0, 0));
    EXPECT_TRUE(codegen.compileToExecutable(optLL, exeO2, 2));

    std::string cmdO0 = "\"" + exeO0.string() + "\"";
    std::string cmdO2 = "\"" + exeO2.string() + "\"";

    int exitO0 = std::system(cmdO0.c_str());
    int exitO2 = std::system(cmdO2.c_str());

    EXPECT_EQ(exitO0, 55);
    EXPECT_EQ(exitO2, 55);
    EXPECT_EQ(exitO0, exitO2);

    std::error_code ec;
    std::filesystem::remove(unoptLL, ec);
    std::filesystem::remove(optLL, ec);
    std::filesystem::remove(exeO0, ec);
    std::filesystem::remove(exeO2, ec);

    return true;
}

TAMARIN_TEST(CodeGen, Scope_ScalarShadowingArray) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x[3] = {1, 2, 3};\n"
        "    {\n"
        "        int x = 42;\n"
        "        if (x != 42) { return 1; }\n"
        "    }\n"
        "    return x[0] + x[2];\n" // 1 + 3 = 4
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "shadow_scalar_arr");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "shadow_scalar_arr.ll";
    auto exePath = tempDir / "shadow_scalar_arr.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 4);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, Scope_ArrayShadowingScalar) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x = 42;\n"
        "    {\n"
        "        int x[2] = {10, 20};\n"
        "        if (x[1] != 20) { return 1; }\n"
        "    }\n"
        "    return x;\n" // 42
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ArrayShadowingScalar: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "shadow_arr_scalar");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "shadow_arr_scalar.ll";
    auto exePath = tempDir / "shadow_arr_scalar.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 42);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, Scope_InnerArraySizesAndLengthRestoration) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int arr[2] = {10, 20};\n"
        "    {\n"
        "        int arr[5] = {1, 2, 3, 4, 5};\n"
        "        if (len(arr) != 5) { return 1; }\n"
        "    }\n"
        "    if (len(arr) != 2) { return 2; }\n"
        "    {\n"
        "        int arr[1] = {99};\n"
        "        if (len(arr) != 1) { return 3; }\n"
        "    }\n"
        "    return len(arr);\n" // 2
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_InnerArraySizesAndLengthRestoration: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "arr_sizes_restore");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "arr_sizes_restore.ll";
    auto exePath = tempDir / "arr_sizes_restore.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 2);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, Scope_ThreeLevelsOfShadowing) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x = 1;\n"
        "    {\n"
        "        int x = 2;\n"
        "        {\n"
        "            int x = 3;\n"
        "            if (x != 3) { return 10; }\n"
        "        }\n"
        "        if (x != 2) { return 20; }\n"
        "    }\n"
        "    return x;\n" // 1
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ThreeLevelsOfShadowing: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "three_levels");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "three_levels.ll";
    auto exePath = tempDir / "three_levels.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 1);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, Scope_NameReuseInDifferentFunctions) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int f1() {\n"
        "    int a = 10;\n"
        "    return a;\n"
        "}\n"
        "int f2() {\n"
        "    int a[2] = {20, 30};\n"
        "    return a[0];\n"
        "}\n"
        "int main() {\n"
        "    return f1() + f2();\n" // 10 + 20 = 30
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "name_reuse");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "name_reuse.ll";
    auto exePath = tempDir / "name_reuse.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 30);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, TypedArray_Operations) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    i8 bytes[2] = {1, 2};\n"
        "    i16 shorts[2] = {10, 20};\n"
        "    i32 ints[2] = {100, 200};\n"
        "    i64 longs[2] = {1000, 2000};\n"
        "    bytes[0] = 5;\n"
        "    shorts[0] = 50;\n"
        "    ints[0] = 500;\n"
        "    longs[0] = 5000;\n"
        "    return bytes[0];\n" // 5
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "typed_arrays");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    EXPECT_TRUE(irStr.find("alloca [2 x i8]") != std::string::npos);
    EXPECT_TRUE(irStr.find("alloca [2 x i16]") != std::string::npos);
    EXPECT_TRUE(irStr.find("alloca [2 x i32]") != std::string::npos);
    EXPECT_TRUE(irStr.find("alloca [2 x i64]") != std::string::npos);
    EXPECT_TRUE(irStr.find("getelementptr inbounds i8") != std::string::npos);
    EXPECT_TRUE(irStr.find("getelementptr inbounds i16") != std::string::npos);
    EXPECT_TRUE(irStr.find("getelementptr inbounds i64") != std::string::npos);

    auto vRes = backend::LLVMVerifier::instance().verifyIR(irStr);
    if (!vRes.ok) {
        std::cerr << "Verification error in EmptyArrayAccess: " << vRes.errorMessage << "\n";
        std::cerr << "Generated IR:\n" << irStr << "\n";
    }
    EXPECT_TRUE(vRes.ok);

    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "typed_arrays.ll";
    auto exePath = tempDir / "typed_arrays.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 5);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, ControlFlow_ShortCircuitAnd) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int trap_func() {\n"
        "    return 100;\n"
        "}\n"
        "int main() {\n"
        "    int flag = 0;\n"
        "    int result = 0;\n"
        "    if (flag != 0 && trap_func() == 100) {\n"
        "        result = 99;\n"
        "    } else {\n"
        "        result = 42;\n"
        "    }\n"
        "    return result;\n" // 42
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "sc_and");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "sc_and.ll";
    auto exePath = tempDir / "sc_and.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 42);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, ControlFlow_ShortCircuitOr) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int trap_func() {\n"
        "    return 100;\n"
        "}\n"
        "int main() {\n"
        "    int flag = 1;\n"
        "    int result = 0;\n"
        "    if (flag == 1 || trap_func() == 100) {\n"
        "        result = 42;\n"
        "    } else {\n"
        "        result = 99;\n"
        "    }\n"
        "    return result;\n" // 42
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "sc_or");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "sc_or.ll";
    auto exePath = tempDir / "sc_or.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 42);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, ControlFlow_BreakAndContinue) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int sum = 0;\n"
        "    for (int i = 0; i < 10; i++) {\n"
        "        if (i == 5) { break; }\n"
        "        if (i == 2) { continue; }\n"
        "        sum += i;\n" // 0 + 1 + 3 + 4 = 8
        "    }\n"
        "    return sum;\n" // 8
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in ControlFlow_BreakAndContinue: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "break_cont");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "break_cont.ll";
    auto exePath = tempDir / "break_cont.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 8);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, Array_ZeroInitialization) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int arr[5] = {10, 20};\n"
        "    if (arr[2] != 0) { return 1; }\n"
        "    if (arr[3] != 0) { return 2; }\n"
        "    if (arr[4] != 0) { return 3; }\n"
        "    return 42;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Array_ZeroInitialization: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "zero_init");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "zero_init.ll";
    auto exePath = tempDir / "zero_init.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 42);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, Numeric_FloatsAndUnsigned) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    u32 u = 20;\n"
        "    u32 v = 6;\n"
        "    u32 q = u / v;\n"  // 3
        "    u32 r = u % v;\n"  // 2
        "    double f1 = 10.5;\n"
        "    double f2 = 2.5;\n"
        "    double fsum = f1 + f2;\n" // 13.0
        "    if (fsum < 12.0) { return 1; }\n"
        "    return q + r;\n" // 3 + 2 = 5
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "Sema error in Scope_ScalarShadowingArray: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "numeric_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    EXPECT_TRUE(irStr.find("udiv i32") != std::string::npos);
    EXPECT_TRUE(irStr.find("urem i32") != std::string::npos);
    EXPECT_TRUE(irStr.find("fadd double") != std::string::npos);
    EXPECT_TRUE(irStr.find("fcmp olt double") != std::string::npos);

    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "numeric_test.ll";
    auto exePath = tempDir / "numeric_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 5);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, StructFieldAccessAndMutation) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Vec2 {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "int main() {\n"
        "    Vec2 v = { 10.0f, 20.0f };\n"
        "    v.x += 5.0f;\n"
        "    v.y = 30.0f;\n"
        "    return (int)(v.x + v.y);\n" // 15 + 30 = 45
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "NestedStructs sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "struct_mutate");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "struct_mutate.ll";
    auto exePath = tempDir / "struct_mutate.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 45);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, StructPassByValueAndReturn) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "Point movePoint(Point p, int dx, int dy) {\n"
        "    Point res = { .x = p.x + dx, .y = p.y + dy };\n"
        "    return res;\n"
        "}\n"
        "int main() {\n"
        "    Point pt = { .x = 10, .y = 20 };\n"
        "    Point moved = movePoint(pt, 5, 12);\n"
        "    return moved.x + moved.y;\n" // 15 + 32 = 47
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "NestedStructs sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "struct_pass_ret");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "struct_pass_ret.ll";
    auto exePath = tempDir / "struct_pass_ret.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 47);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, NestedStructs) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "struct Rect {\n"
        "    Point topLeft;\n"
        "    Point bottomRight;\n"
        "};\n"
        "int main() {\n"
        "    Rect r = { { 3, 5 }, { 13, 20 } };\n"
        "    int width = r.bottomRight.x - r.topLeft.x;\n" // 13 - 3 = 10
        "    int height = r.bottomRight.y - r.topLeft.y;\n" // 20 - 5 = 15
        "    return width + height;\n" // 25
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "NestedStructs sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "nested_structs");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "nested_structs.ll";
    auto exePath = tempDir / "nested_structs.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 25);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, StructOptimizationEquivalence) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "@layout(point)\n"
        "struct Vec2 {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "float dot(Vec2 a, Vec2 b) {\n"
        "    return a.x * b.x + a.y * b.y;\n"
        "}\n"
        "int main() {\n"
        "    const Vec2 a = { 2.0f, 3.0f };\n"
        "    Vec2 b = { .x = 4.0f, .y = 5.0f };\n"
        "    b.y = 6.0f;\n"
        "    float d = dot(a, b);\n"
        "    return (int)d;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "NestedStructs sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "struct_opt_equiv");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llO0   = tempDir / "struct_opt_O0.ll";
    auto exeO0  = tempDir / "struct_opt_O0.exe";
    auto llO2   = tempDir / "struct_opt_O2.ll";
    auto exeO2  = tempDir / "struct_opt_O2.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llO0));
    EXPECT_TRUE(codegen.compileToExecutable(llO0, exeO0, 0));
    EXPECT_TRUE(codegen.optimizeLLVMIR(llO0, llO2, 2));
    EXPECT_TRUE(codegen.compileToExecutable(llO2, exeO2, 2));

    std::string cmd0 = "\"" + exeO0.string() + "\"";
    int ret0 = std::system(cmd0.c_str());

    std::string cmd2 = "\"" + exeO2.string() + "\"";
    int ret2 = std::system(cmd2.c_str());

    EXPECT_EQ(ret0, 26);
    EXPECT_EQ(ret2, 26);
    EXPECT_EQ(ret0, ret2);

    std::error_code ec;
    std::filesystem::remove(llO0, ec);
    std::filesystem::remove(exeO0, ec);
    std::filesystem::remove(llO2, ec);
    std::filesystem::remove(exeO2, ec);
    return true;
}

TAMARIN_TEST(CodeGen, StructMethods) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Vector2 {\n"
        "    float x;\n"
        "    float y;\n"
        "\n"
        "    float lengthSquared() const {\n"
        "        return x * x + y * y;\n"
        "    }\n"
        "\n"
        "    void translate(float dx, float dy) {\n"
        "        x += dx;\n"
        "        y += dy;\n"
        "    }\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    Vector2 v = { .x = 1.0f, .y = 2.0f };\n"
        "    v.translate(2.0f, 2.0f);\n" // x = 3, y = 4
        "    return (int)v.lengthSquared();\n" // 9 + 16 = 25
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "StructMethods sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "struct_methods_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "struct_methods.ll";
    auto exePath = tempDir / "struct_methods.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 25);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, ClassConstructorsAndMethods) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Player {\n"
        "private:\n"
        "    int health;\n"
        "    int maxHealth;\n"
        "\n"
        "public:\n"
        "    Player(int initialHealth) {\n"
        "        health = initialHealth;\n"
        "        maxHealth = initialHealth;\n"
        "    }\n"
        "\n"
        "    void takeDamage(int amount) {\n"
        "        health -= amount;\n"
        "        if (health < 0) {\n"
        "            health = 0;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    void heal(int amount) {\n"
        "        health += amount;\n"
        "        if (health > maxHealth) {\n"
        "            health = maxHealth;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    int getHealth() const {\n"
        "        return health;\n"
        "    }\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    Player player(100);\n"
        "    player.takeDamage(30);\n"
        "    player.heal(10);\n"
        "    return player.getHealth();\n" // 80
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "ClassConstructorsAndMethods sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "class_methods_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "class_methods.ll";
    auto exePath = tempDir / "class_methods.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 80);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, SingleInheritanceAndMethodOverride) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Animal {\n"
        "protected:\n"
        "    int age;\n"
        "\n"
        "public:\n"
        "    Animal(int a) {\n"
        "        age = a;\n"
        "    }\n"
        "\n"
        "    virtual int speak() {\n"
        "        return 10;\n"
        "    }\n"
        "\n"
        "    int getAge() const {\n"
        "        return age;\n"
        "    }\n"
        "};\n"
        "\n"
        "class Dog : public Animal {\n"
        "public:\n"
        "    Dog(int a) {\n"
        "        age = a;\n"
        "    }\n"
        "\n"
        "    int speak() override {\n"
        "        return 42;\n"
        "    }\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    Dog dog(5);\n"
        "    return dog.speak() + dog.getAge();\n" // 42 + 5 = 47
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "SingleInheritanceAndMethodOverride sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "inheritance_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "inheritance.ll";
    auto exePath = tempDir / "inheritance.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 47);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, VirtualMethodDynamicDispatch) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Shape {\n"
        "public:\n"
        "    virtual int area() {\n"
        "        return 0;\n"
        "    }\n"
        "};\n"
        "\n"
        "class Rectangle : public Shape {\n"
        "    int w;\n"
        "    int h;\n"
        "public:\n"
        "    Rectangle(int width, int height) {\n"
        "        w = width;\n"
        "        h = height;\n"
        "    }\n"
        "\n"
        "    int area() override {\n"
        "        return w * h;\n"
        "    }\n"
        "};\n"
        "\n"
        "int calculateArea(Shape& s) {\n"
        "    return s.area();\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    Rectangle rect(6, 7);\n"
        "    return calculateArea(rect);\n" // 42 through dynamic vtable dispatch!
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "VirtualMethodDynamicDispatch sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "vtable_dispatch_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "vtable_dispatch.ll";
    auto exePath = tempDir / "vtable_dispatch.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 42);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, ReferencesParamAndMutation) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "\n"
        "void moveBy(Point& p, int dx, int dy) {\n"
        "    p.x += dx;\n"
        "    p.y += dy;\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    Point pt = { 10, 20 };\n"
        "    moveBy(pt, 5, 15);\n"
        "    return pt.x + pt.y;\n" // 15 + 35 = 50
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "ReferencesParamAndMutation sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "ref_mutation_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "ref_mutation.ll";
    auto exePath = tempDir / "ref_mutation.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 50);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, StructureOfArraysExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "@layout(scan)\n"
        "struct Particle {\n"
        "    float x;\n"
        "    float y;\n"
        "    float vx;\n"
        "    float vy;\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    Particle particles[10];\n"
        "    particles[0].x = 10.0f;\n"
        "    particles[0].y = 20.0f;\n"
        "    particles[1].x = 30.0f;\n"
        "    particles[1].y = 40.0f;\n"
        "\n"
        "    particles[0].x += 5.0f;\n"  // 15.0
        "    particles[1].y += 5.0f;\n"  // 45.0
        "\n"
        "    float sum = particles[0].x + particles[1].y;\n" // 60.0
        "    return (int)sum;\n" // 60
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "StructureOfArraysExecution sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "soa_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llPath  = tempDir / "soa_exec.ll";
    auto exePath = tempDir / "soa_exec.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 60);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, RAIIDestructorsDeterministicOrder) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Resource {\n"
        "    int id;\n"
        "public:\n"
        "    Resource(int id) {\n"
        "        this.id = id;\n"
        "    }\n"
        "    ~Resource() {\n"
        "        this.id = 0;\n"
        "    }\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    Resource r1(1);\n"
        "    {\n"
        "        Resource r2(2);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cerr << "RAIIDestructorsDeterministicOrder sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "raii_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    std::ostringstream llvmIR;
    EXPECT_TRUE(codegen.emitLLVMIR(llvmIR));

    auto irStr = llvmIR.str();
    EXPECT_TRUE(irStr.find("define void @Resource_destructor(ptr %v") != std::string::npos);
    EXPECT_TRUE(irStr.find("call void @Resource_destructor") != std::string::npos);

    return true;
}

TAMARIN_TEST(CodeGen, OOPOptimizationEquivalence) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Counter {\n"
        "private:\n"
        "    int count;\n"
        "public:\n"
        "    Counter(int c) {\n"
        "        count = c;\n"
        "    }\n"
        "    void add(int delta) {\n"
        "        count += delta;\n"
        "    }\n"
        "    int get() const {\n"
        "        return count;\n"
        "    }\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    Counter c(10);\n"
        "    c.add(25);\n"
        "    return c.get();\n" // 35
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "oop_opt_equiv");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    auto llO0   = tempDir / "oop_opt_O0.ll";
    auto exeO0  = tempDir / "oop_opt_O0.exe";
    auto llO2   = tempDir / "oop_opt_O2.ll";
    auto exeO2  = tempDir / "oop_opt_O2.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llO0));
    EXPECT_TRUE(codegen.compileToExecutable(llO0, exeO0, 0));
    EXPECT_TRUE(codegen.optimizeLLVMIR(llO0, llO2, 2));
    EXPECT_TRUE(codegen.compileToExecutable(llO2, exeO2, 2));

    std::string cmd0 = "\"" + exeO0.string() + "\"";
    int ret0 = std::system(cmd0.c_str());

    std::string cmd2 = "\"" + exeO2.string() + "\"";
    int ret2 = std::system(cmd2.c_str());

    EXPECT_EQ(ret0, 35);
    EXPECT_EQ(ret2, 35);
    EXPECT_EQ(ret0, ret2);

    std::error_code ec;
    std::filesystem::remove(llO0, ec);
    std::filesystem::remove(exeO0, ec);
    std::filesystem::remove(llO2, ec);
    std::filesystem::remove(exeO2, ec);
    return true;
}

// ── v0.3.8 Adversarial Test Suite & Helpers ─────────────────────────────────

static bool compileAndRunProgram(const std::string& src, int optLevel, int& exitCode, std::string& stdoutStr, const std::string& stdinStr = "", bool enableMemoryStats = false) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", src);
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error: " << errs.str() << "\n";
        return false;
    }

    sema::Sema sema(diag);
    if (!sema.analyze(prog)) {
        std::cerr << "Sema error: " << errs.str() << "\n";
        return false;
    }

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "main");
    if (!mod) return false;

    if (optLevel > 0) {
        auto tirPM = ir::opt::PassManager::createDefaultPipeline(optLevel);
        tirPM.run(*mod, &sema.checker().context());
    }

    backend::LLVMCodeGen codegen(*mod, diag);
    codegen.setEnableMemoryStats(enableMemoryStats);
    auto tempDir = std::filesystem::temp_directory_path();
    static int runId = 0;
    auto llPath = tempDir / ("tamarin_v038_run_" + std::to_string(++runId) + ".ll");
    auto exePath = tempDir / ("tamarin_v038_run_" + std::to_string(runId) + ".exe");
    auto inPath = tempDir / ("tamarin_v038_run_" + std::to_string(runId) + ".in");
    auto outPath = tempDir / ("tamarin_v038_run_" + std::to_string(runId) + ".out");

    if (!codegen.emitLLVMIRToFile(llPath)) {
        std::cerr << "emitLLVMIR error: " << errs.str() << "\n";
        return false;
    }

    if (optLevel > 0) {
        auto optLL = tempDir / ("tamarin_v038_run_" + std::to_string(runId) + "_opt.ll");
        if (!codegen.optimizeLLVMIR(llPath, optLL, optLevel)) {
            std::cerr << "optimize error: " << errs.str() << "\n";
            return false;
        }
        if (!codegen.compileToExecutable(optLL, exePath, optLevel)) {
            std::cerr << "compile error: " << errs.str() << "\n";
            return false;
        }
        std::error_code ec;
        std::filesystem::remove(optLL, ec);
    } else {
        if (!codegen.compileToExecutable(llPath, exePath, 0)) {
            std::cerr << "compile error: " << errs.str() << "\n";
            return false;
        }
    }

    if (!stdinStr.empty()) {
        std::ofstream in(inPath, std::ios::binary);
        in.write(stdinStr.data(), stdinStr.size());
        in.close();
    }

    std::string cmd = "\"" + exePath.string() + "\"";
    if (!stdinStr.empty()) {
        cmd += " < \"" + inPath.string() + "\"";
    }
    cmd += " > \"" + outPath.string() + "\" 2>&1";

    std::string fullCmd = "\"" + cmd + "\"";
    exitCode = std::system(fullCmd.c_str());

    std::ifstream out(outPath, std::ios::binary);
    if (out) {
        std::ostringstream ss;
        ss << out.rdbuf();
        stdoutStr = ss.str();
    }
    out.close();

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    std::filesystem::remove(inPath, ec);
    std::filesystem::remove(outPath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, DestructorInheritanceDerivedWithoutDestructor) {
    std::string src =
        "class Base {\n"
        "public:\n"
        "    ~Base() {\n"
        "        print(\"Base;\");\n"
        "    }\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    int x;\n"
        "};\n"
        "int main() {\n"
        "    Derived d;\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "Base;");
    return true;
}

TAMARIN_TEST(CodeGen, DestructorThreeLevelInheritance) {
    std::string src =
        "class Root {\n"
        "public:\n"
        "    ~Root() {\n"
        "        print(\"Root;\");\n"
        "    }\n"
        "};\n"
        "class Intermediate : public Root {\n"
        "public:\n"
        "    int a;\n"
        "};\n"
        "class Derived : public Intermediate {\n"
        "public:\n"
        "    int b;\n"
        "};\n"
        "int main() {\n"
        "    Derived d;\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "Root;");
    return true;
}

TAMARIN_TEST(CodeGen, DestructorDerivedAndBaseExecutionOrder) {
    std::string src =
        "class Base {\n"
        "public:\n"
        "    ~Base() {\n"
        "        print(\"Base;\");\n"
        "    }\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    ~Derived() {\n"
        "        print(\"Derived;\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Derived d;\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "Derived;Base;");
    return true;
}

TAMARIN_TEST(CodeGen, DestructorMultipleMembersReverseOrder) {
    std::string src =
        "class MemberA {\n"
        "public:\n"
        "    ~MemberA() {\n"
        "        print(\"A;\");\n"
        "    }\n"
        "};\n"
        "class MemberB {\n"
        "public:\n"
        "    ~MemberB() {\n"
        "        print(\"B;\");\n"
        "    }\n"
        "};\n"
        "class Owner {\n"
        "public:\n"
        "    MemberA a;\n"
        "    MemberB b;\n"
        "    ~Owner() {\n"
        "        print(\"Owner;\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Owner o;\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "Owner;B;A;");
    return true;
}

TAMARIN_TEST(CodeGen, DestructorNestedAggregateTransitive) {
    std::string src =
        "class Inner {\n"
        "public:\n"
        "    ~Inner() {\n"
        "        print(\"Inner;\");\n"
        "    }\n"
        "};\n"
        "struct Mid {\n"
        "    Inner in;\n"
        "};\n"
        "class Outer {\n"
        "public:\n"
        "    Mid m;\n"
        "};\n"
        "int main() {\n"
        "    Outer o;\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "Inner;");
    return true;
}

TAMARIN_TEST(CodeGen, DestructorLoopBreakAndContinue) {
    std::string src =
        "class Res {\n"
        "public:\n"
        "    int id;\n"
        "    Res(int i) {\n"
        "        id = i;\n"
        "    }\n"
        "    ~Res() {\n"
        "        print(\"D:\");\n"
        "        print(id);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    for (int i = 0; i < 3; i++) {\n"
        "        Res r(i);\n"
        "        if (i == 0) {\n"
        "            continue;\n"
        "        }\n"
        "        if (i == 1) {\n"
        "            break;\n"
        "        }\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "D:0;D:1;");
    return true;
}

TAMARIN_TEST(CodeGen, DestructorEarlyReturnScope) {
    std::string src =
        "class Res {\n"
        "public:\n"
        "    int id;\n"
        "    Res(int i) {\n"
        "        id = i;\n"
        "    }\n"
        "    ~Res() {\n"
        "        print(\"D:\");\n"
        "        print(id);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int test(int x) {\n"
        "    Res r1(1);\n"
        "    if (x > 0) {\n"
        "        Res r2(2);\n"
        "        return 42;\n"
        "    }\n"
        "    return 0;\n"
        "}\n"
        "int main() {\n"
        "    int v = test(5);\n"
        "    return v;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 42);
    EXPECT_EQ(out, "D:2;D:1;");
    return true;
}

TAMARIN_TEST(CodeGen, DestructorExactlyOnceCount) {
    std::string src =
        "class Res {\n"
        "public:\n"
        "    ~Res() {\n"
        "        print(\"X\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    {\n"
        "        Res r1;\n"
        "    }\n"
        "    {\n"
        "        Res r2;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "XX");
    return true;
}

TAMARIN_TEST(CodeGen, CrtPathEmbeddedNullRejection) {
    std::string src =
        "int main() {\n"
        "    char badPath[4];\n"
        "    badPath[0] = 97;\n"
        "    badPath[1] = 0;\n"
        "    badPath[2] = 99;\n"
        "    badPath[3] = 0;\n"
        "    int h = openFile(badPath, \"r\");\n"
        "    if (h == 0) {\n"
        "        print(\"REJECTED\");\n"
        "    } else {\n"
        "        print(\"FAILED\");\n"
        "        closeFile(h);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    if (exitCode != 0 || out != "REJECTED") {
        std::cerr << "CrtPathEmbeddedNullRejection: exitCode=" << exitCode << ", out='" << out << "'\n";
    }
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "REJECTED");
    return true;
}

TAMARIN_TEST(CodeGen, CrtPathEmptyAndLongPaths) {
    std::string src =
        "int main() {\n"
        "    int h = openFile(\"\", \"r\");\n"
        "    if (h == 0) {\n"
        "        print(\"EMPTY_OK;\");\n"
        "    }\n"
        "    bool exists = fileExists(\"\");\n"
        "    if (!exists) {\n"
        "        print(\"EXISTS_OK;\");\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "EMPTY_OK;EXISTS_OK;");
    return true;
}

TAMARIN_TEST(CodeGen, InputTruncationAndSubsequentReadLine) {
    std::string src =
        "int main() {\n"
        "    char buf[10];\n"
        "    int n1 = getInput(buf, 10);\n"
        "    print(\"N1:\");\n"
        "    print(n1);\n"
        "    print(\";B1:\");\n"
        "    print((int)buf[0]);\n"
        "    print(\"-\");\n"
        "    print((int)buf[8]);\n"
        "    int n2 = getInput(buf, 10);\n"
        "    print(\";N2:\");\n"
        "    print(n2);\n"
        "    print(\";B2:\");\n"
        "    print((int)buf[0]);\n"
        "    print(\"-\");\n"
        "    print((int)buf[7]);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    std::string in = "0123456789ABCDEF\nNextLine\n";
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out, in));
    if (exitCode != 0 || out != "N1:-2;B1:48-56;N2:8;B2:78-101") {
        std::cerr << "InputTruncation: exitCode=" << exitCode << ", out='" << out << "'\n";
    }
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "N1:-2;B1:48-56;N2:8;B2:78-101");
    return true;
}

TAMARIN_TEST(CodeGen, InputCapacityZeroAndOne) {
    std::string src =
        "int main() {\n"
        "    char buf[4];\n"
        "    int n0 = getInput(buf, 0);\n"
        "    print(\"N0:\");\n"
        "    print(n0);\n"
        "    int n1 = getInput(buf, 1);\n"
        "    print(\";N1:\");\n"
        "    print(n1);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    std::string in = "hello\n";
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out, in));
    if (exitCode != 0 || out != "N0:0;N1:-2") {
        std::cerr << "InputCapacityZeroAndOne: exitCode=" << exitCode << ", out='" << out << "'\n";
    }
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "N0:0;N1:-2");
    return true;
}

TAMARIN_TEST(CodeGen, OutputUnifiedSequencingExactOrder) {
    std::string src =
        "int main() {\n"
        "    print(123);\n"
        "    print(\"ABC\");\n"
        "    print(true);\n"
        "    print(456);\n"
        "    println();\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("123ABCtrue456") == 0);
    return true;
}

TAMARIN_TEST(CodeGen, OutputFormatStringVerbatim) {
    std::string src =
        "int main() {\n"
        "    print(\"%s %n %p %x %d %%\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "%s %n %p %x %d %%");
    return true;
}

TAMARIN_TEST(CodeGen, OptimizationLevelsO0ThroughO3) {
    std::string src =
        "int compute(int n) {\n"
        "    int s = 0;\n"
        "    for (int i = 1; i <= n; i++) {\n"
        "        s += i;\n"
        "    }\n"
        "    return s;\n"
        "}\n"
        "int main() {\n"
        "    int res = compute(10);\n" // 55
        "    print(\"SUM:\");\n"
        "    print(res);\n"
        "    return res;\n"
        "}\n";
    for (int opt = 0; opt <= 3; opt++) {
        int exitCode = -1;
        std::string out;
        EXPECT_TRUE(compileAndRunProgram(src, opt, exitCode, out));
        EXPECT_EQ(exitCode, 55);
        EXPECT_EQ(out, "SUM:55");
    }
    return true;
}

TAMARIN_TEST(CodeGen, DestructorDeepNestingExceeds64Levels) {
    std::ostringstream ss;
    ss << "class Res {\n"
       << "public:\n"
       << "    ~Res() {\n"
       << "        print(\"DESTROYED_DEEP_RES;\");\n"
       << "    }\n"
       << "};\n";
    ss << "struct Level70 { Res r; };\n";
    for (int i = 69; i >= 1; --i) {
        ss << "struct Level" << i << " { Level" << (i + 1) << " child; };\n";
    }
    ss << "int main() {\n"
       << "    {\n"
       << "        Level1 root;\n"
       << "        print(\"ENTER;\");\n"
       << "    }\n"
       << "    print(\"EXIT;\");\n"
       << "    return 0;\n"
       << "}\n";
    std::string src = ss.str();
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "ENTER;DESTROYED_DEEP_RES;EXIT;");
    return true;
}

TAMARIN_TEST(CodeGen, PrintAbiArgumentPromotion) {
    std::string src =
        "int main() {\n"
        "    long big = 9876543210123;\n"
        "    char c = 65;\n"
        "    float f = 2.5f;\n"
        "    print(\"BIG:\");\n"
        "    print(big);\n"
        "    print(\";CHAR:\");\n"
        "    print(c);\n"
        "    print(\";FLOAT:\");\n"
        "    print(f);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    if (out != "BIG:9876543210123;CHAR:65;FLOAT:2.5") {
        std::cerr << "PrintAbiArgumentPromotion: out='" << out << "'\n";
    }
    EXPECT_EQ(out, "BIG:9876543210123;CHAR:65;FLOAT:2.5");
    return true;
}

// ── v0.4.1 Benchmark and Backend Revalidation Tests ─────────────────────────

TAMARIN_TEST(CodeGen, SignedAndUnsignedArithmetic64BitWrap) {
    std::string src =
        "int main() {\n"
        "    u64 h = 1469598103934665603;\n"
        "    for (int i = 0; i < 1000; i++) {\n"
        "        h = (h * 1099511628211 + (u64)i) ^ (h >> 17);\n"
        "    }\n"
        "    print(\"HASH:\");\n"
        "    print(h & 2147483647);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("HASH:") == 0);
    return true;
}

TAMARIN_TEST(CodeGen, LoopLocalArrayAllocationHoistingAndZeroInit) {
    // Tests that arrays allocated inside loops are hoisted to the entry block
    // but correctly re-zero-initialized on each iteration of the loop!
    std::string src =
        "int main() {\n"
        "    int sum = 0;\n"
        "    for (int iter = 0; iter < 5; iter++) {\n"
        "        int buf[4];\n"
        "        sum += buf[0];\n" // must be 0 on every iteration!
        "        buf[0] = 42;\n"   // dirty the buffer for next iteration
        "    }\n"
        "    print(\"SUM:\");\n"
        "    print(sum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "SUM:0");
    return true;
}

TAMARIN_TEST(CodeGen, NoinlineFunctionEmission) {
    std::string src =
        "@noinline int helper(int x) {\n"
        "    return x + 5;\n"
        "}\n"
        "int main() {\n"
        "    int v = helper(37);\n"
        "    print(\"V:\");\n"
        "    print(v);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "V:42");
    return true;
}

TAMARIN_TEST(CodeGen, BenchmarkTimerIntrinsicsExecution) {
    std::string src =
        "int main() {\n"
        "    __benchmark_start();\n"
        "    long sum = 0;\n"
        "    for (int i = 0; i < 10000; i++) {\n"
        "        sum += (long)i;\n"
        "    }\n"
        "    double t = __benchmark_elapsed_ms();\n"
        "    if (t >= 0.0) {\n"
        "        print(\"TIMER_OK\");\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "TIMER_OK");
    return true;
}

TAMARIN_TEST(CodeGen, FloatToBitsBitcastExecution) {
    std::string src =
        "int main() {\n"
        "    u64 m = __f64_to_bits(3136788.0);\n"
        "    u64 mat = __f64_to_bits(628824.0);\n"
        "    print(\"MANDEL:\");\n"
        "    print(m);\n"
        "    print(\";MATMUL:\");\n"
        "    print(mat);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "MANDEL:4703990612284669952;MATMUL:4693648769122566144");
    return true;
}

TAMARIN_TEST(CodeGen, DeepTrivialAggregateCompilation) {
    std::ostringstream ss;
    ss << "struct Level256 { int val; };\n";
    for (int i = 255; i >= 1; --i) {
        ss << "struct Level" << i << " { Level" << (i + 1) << " child; int dummy; };\n";
    }
    ss << "int main() {\n"
       << "    Level1 root;\n"
       << "    root.dummy = 42;\n"
       << "    print(\"DEEP_OK:\");\n"
       << "    print(root.dummy);\n"
       << "    return 0;\n"
       << "}\n";
    std::string src = ss.str();
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "DEEP_OK:42");
    return true;
}

TAMARIN_TEST(CodeGen, RegionBasicExecution) {
    std::string src =
        "int main() {\n"
        "    int sum = 0;\n"
        "    region scratch(65536) {\n"
        "        int values[100];\n"
        "        for (int i = 0; i < 100; i++) {\n"
        "            values[i] = i * 2;\n"
        "        }\n"
        "        for (int j = 0; j < 100; j++) {\n"
        "            sum += values[j];\n"
        "        }\n"
        "    }\n"
        "    print(\"SUM:\");\n"
        "    print(sum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "SUM:9900");
    return true;
}

TAMARIN_TEST(CodeGen, RegionStructExecution) {
    std::string src =
        "struct Particle {\n"
        "    int x;\n"
        "    int y;\n"
        "    int vx;\n"
        "    int vy;\n"
        "};\n"
        "int main() {\n"
        "    int totalX = 0;\n"
        "    region scratch(65536) {\n"
        "        Particle p = (Particle){ 10, 20, 2, 4 };\n"
        "        p.x += p.vx;\n"
        "        p.y += p.vy;\n"
        "        totalX = p.x;\n"
        "    }\n"
        "    print(\"PARTICLE_X:\");\n"
        "    print(totalX);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "PARTICLE_X:12");
    return true;
}

TAMARIN_TEST(CodeGen, RegionMultipleSequentialAllocations) {
    std::string src =
        "struct Vec3 { int x; int y; int z; };\n"
        "int main() {\n"
        "    int checkSum = 0;\n"
        "    region mem {\n"
        "        int a[50];\n"
        "        Vec3 v;\n"
        "        int b[50];\n"
        "        v.x = 100; v.y = 200; v.z = 300;\n"
        "        for (int i = 0; i < 50; i++) {\n"
        "            a[i] = i;\n"
        "            b[i] = i * 10;\n"
        "        }\n"
        "        checkSum = a[25] + b[25] + v.x + v.y + v.z;\n"
        "    }\n"
        "    print(\"MULTI_OK:\");\n"
        "    print(checkSum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    // 25 + 250 + 100 + 200 + 300 = 875
    EXPECT_EQ(out, "MULTI_OK:875");
    return true;
}

TAMARIN_TEST(CodeGen, RegionChunkGrowthExecution) {
    std::string src =
        "int main() {\n"
        "    int sum = 0;\n"
        "    // Initial capacity 256 bytes is small, allocations will exceed it and trigger growth\n"
        "    region scratch(256) {\n"
        "        int first[50]; // 200 bytes\n"
        "        int second[100]; // 400 bytes, forces chunk growth\n"
        "        int third[100]; // 400 bytes, forces another chunk or fits grown chunk\n"
        "        for (int i = 0; i < 50; i++) { first[i] = 1; }\n"
        "        for (int j = 0; j < 100; j++) { second[j] = 2; }\n"
        "        for (int k = 0; k < 100; k++) { third[k] = 3; }\n"
        "        sum = first[25] + second[50] + third[75];\n"
        "    }\n"
        "    print(\"GROWTH_OK:\");\n"
        "    print(sum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    // 1 + 2 + 3 = 6
    EXPECT_EQ(out, "GROWTH_OK:6");
    return true;
}

TAMARIN_TEST(CodeGen, RegionNestedExecution) {
    std::string src =
        "int main() {\n"
        "    int result = 0;\n"
        "    region outer(4096) {\n"
        "        int a[10];\n"
        "        for (int i = 0; i < 10; i++) { a[i] = i * 5; }\n"
        "        region inner(1024) {\n"
        "            int b[10];\n"
        "            for (int j = 0; j < 10; j++) { b[j] = j * 10; }\n"
        "            result += b[9]; // 90\n"
        "        }\n"
        "        // Inner destroyed; outer remains valid\n"
        "        result += a[9]; // 45\n"
        "    }\n"
        "    print(\"NESTED_OK:\");\n"
        "    print(result);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    // 90 + 45 = 135
    EXPECT_EQ(out, "NESTED_OK:135");
    return true;
}

TAMARIN_TEST(CodeGen, RegionLoopReclamation) {
    std::string src =
        "int main() {\n"
        "    int sum = 0;\n"
        "    // Reclaims 1000 times in loop\n"
        "    for (int i = 0; i < 100; i++) {\n"
        "        region iter_scratch(512) {\n"
        "            int buf[20];\n"
        "            buf[10] = i;\n"
        "            sum += buf[10];\n"
        "        }\n"
        "    }\n"
        "    print(\"RECLAIM_OK:\");\n"
        "    print(sum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    // 0 + 1 + ... + 99 = 4950
    EXPECT_EQ(out, "RECLAIM_OK:4950");
    return true;
}

TAMARIN_TEST(CodeGen, RegionEarlyReturnCleanup) {
    std::string src =
        "int compute(int flag) {\n"
        "    region scratch(1024) {\n"
        "        int arr[10];\n"
        "        arr[0] = 42;\n"
        "        if (flag > 0) {\n"
        "            return arr[0];\n"
        "        }\n"
        "        arr[1] = 99;\n"
        "        return arr[1];\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    int r1 = compute(1);\n"
        "    int r2 = compute(0);\n"
        "    print(\"EARLY_RET:\");\n"
        "    print(r1 + r2);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    // 42 + 99 = 141
    EXPECT_EQ(out, "EARLY_RET:141");
    return true;
}

TAMARIN_TEST(CodeGen, RegionBreakContinueCleanup) {
    std::string src =
        "int main() {\n"
        "    int sum = 0;\n"
        "    for (int i = 0; i < 10; i++) {\n"
        "        region r {\n"
        "            int buf[10];\n"
        "            buf[0] = i;\n"
        "            if (i == 3) {\n"
        "                continue;\n"
        "            }\n"
        "            if (i == 7) {\n"
        "                break;\n"
        "            }\n"
        "            sum += buf[0];\n"
        "        }\n"
        "    }\n"
        "    print(\"BREAK_CONT:\");\n"
        "    print(sum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    // i in 0, 1, 2, (skip 3), 4, 5, 6, (break 7) -> 0+1+2+4+5+6 = 18
    EXPECT_EQ(out, "BREAK_CONT:18");
    return true;
}

TAMARIN_TEST(CodeGen, RegionZeroInitialization) {
    std::string src =
        "struct Item {\n"
        "    int id;\n"
        "    long value;\n"
        "};\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int numbers[20];\n"
        "        Item item;\n"
        "        int sum = 0;\n"
        "        for (int i = 0; i < 20; i++) {\n"
        "            sum += numbers[i];\n"
        "        }\n"
        "        sum += item.id;\n"
        "        sum += (int)item.value;\n"
        "        print(\"ZERO_INIT:\");\n"
        "        print(sum);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "ZERO_INIT:0");
    return true;
}

TAMARIN_TEST(CodeGen, RegionAddressStability) {
    std::string src =
        "int main() {\n"
        "    region scratch(128) {\n"
        "        int first[10];\n"
        "        for (int i = 0; i < 10; i++) {\n"
        "            first[i] = 100 + i;\n"
        "        }\n"
        "        // Allocate huge array forcing multiple new chunks\n"
        "        int huge[1000];\n"
        "        for (int j = 0; j < 1000; j++) {\n"
        "            huge[j] = j;\n"
        "        }\n"
        "        // Verify first array's elements were not corrupted or moved\n"
        "        int ok = 1;\n"
        "        for (int k = 0; k < 10; k++) {\n"
        "            if (first[k] != 100 + k) {\n"
        "                ok = 0;\n"
        "            }\n"
        "        }\n"
        "        print(\"STABLE:\");\n"
        "        print(ok);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "STABLE:1");
    return true;
}

TAMARIN_TEST(CodeGen, RegionMultipleSequential) {
    std::string src =
        "int main() {\n"
        "    int acc = 0;\n"
        "    region r1(512) {\n"
        "        int a[10];\n"
        "        a[5] = 50;\n"
        "        acc += a[5];\n"
        "    }\n"
        "    region r2(512) {\n"
        "        int b[10];\n"
        "        b[5] = 75;\n"
        "        acc += b[5];\n"
        "    }\n"
        "    print(\"SEQ:\");\n"
        "    print(acc);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "SEQ:125");
    return true;
}

TAMARIN_TEST(CodeGen, RegionAoSArrayElementMutationAndBounds) {
    std::string src =
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "int main() {\n"
        "    int res = 0;\n"
        "    region r(4096) {\n"
        "        Point pts[10];\n"
        "        for (int i = 0; i < 10; i++) {\n"
        "            pts[i].x = i * 10;\n"
        "            pts[i].y = i * 20;\n"
        "        }\n"
        "        pts[9].x += 5;\n"
        "        pts[9].y += 7;\n"
        "        res = pts[9].x + pts[9].y;\n"
        "    }\n"
        "    print(\"AOS:\");\n"
        "    print(res);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "AOS:282");
    return true;
}

TAMARIN_TEST(CodeGen, RegionConstantCapacityExpressionExecution) {
    std::string src =
        "int main() {\n"
        "    int sum = 0;\n"
        "    region r(1024 * 64) {\n"
        "        int buf[100];\n"
        "        for (int i = 0; i < 100; i++) {\n"
        "            buf[i] = i;\n"
        "            sum += buf[i];\n"
        "        }\n"
        "    }\n"
        "    print(\"SUM:\");\n"
        "    print(sum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "SUM:4950");
    return true;
}

TAMARIN_TEST(CodeGen, RegionNestedReferencesAndValidBorrowingExecution) {
    std::string src =
        "int sumView(int v[]) {\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < len(v); i++) {\n"
        "        s += v[i];\n"
        "    }\n"
        "    return s;\n"
        "}\n"
        "int main() {\n"
        "    int total = 0;\n"
        "    region outer_r(16384) {\n"
        "        int outer_buf[10];\n"
        "        for (int i = 0; i < 10; i++) {\n"
        "            outer_buf[i] = i * 2;\n"
        "        }\n"
        "        region inner_r(8192) {\n"
        "            total += sumView(outer_buf);\n"
        "        }\n"
        "    }\n"
        "    print(\"BORROW:\");\n"
        "    print(total);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "BORROW:90");
    return true;
}

TAMARIN_TEST(CodeGen, RegionLoopBreakContinueEarlyReturnCleanup) {
    std::string src =
        "int testRegions(int mode) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < 10; i++) {\n"
        "        region r(4096) {\n"
        "            int buf[20];\n"
        "            buf[0] = i * 10;\n"
        "            if (i == 2 && mode == 1) { continue; }\n"
        "            if (i == 5 && mode == 2) { break; }\n"
        "            if (i == 3 && mode == 3) { return total + buf[0]; }\n"
        "            total += buf[0];\n"
        "        }\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    print(\"M1:\");\n"
        "    print(testRegions(1));\n"
        "    print(\";M2:\");\n"
        "    print(testRegions(2));\n"
        "    print(\";M3:\");\n"
        "    print(testRegions(3));\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "M1:430;M2:100;M3:60");
    return true;
}

TAMARIN_TEST(CodeGen, RegionConstantExpressionArrayBoundsExecution) {
    std::string src =
        "int main() {\n"
        "    region scratch(65536) {\n"
        "        int values[4 * 1024];\n"
        "        values[0] = 11;\n"
        "        values[4095] = 99;\n"
        "        int l = len(values);\n"
        "        print(\"LEN:\");\n"
        "        print(l);\n"
        "        print(\";FIRST:\");\n"
        "        print(values[0]);\n"
        "        print(\";LAST:\");\n"
        "        print(values[4095]);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "LEN:4096;FIRST:11;LAST:99");
    return true;
}

TAMARIN_TEST(CodeGen, RegionConstantShiftExpressionArrayBoundsExecution) {
    std::string src =
        "int main() {\n"
        "    region scratch(131072) {\n"
        "        int values[1 << 12];\n"
        "        values[4095] = 777;\n"
        "        print(\"VAL:\");\n"
        "        print(values[4095]);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "VAL:777");
    return true;
}

TAMARIN_TEST(CodeGen, OrdinaryStackArrayConstantExpressionBoundsExecution) {
    std::string src =
        "int main() {\n"
        "    int values[2 * 256];\n"
        "    values[511] = 4321;\n"
        "    print(\"STACK_LEN:\");\n"
        "    print(len(values));\n"
        "    print(\";STACK_LAST:\");\n"
        "    print(values[511]);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "STACK_LEN:512;STACK_LAST:4321");
    return true;
}

TAMARIN_TEST(CodeGen, RegionMultiLevelHelperBorrowingAndAggregateExecution) {
    std::string src =
        "struct Holder {\n"
        "    int view[];\n"
        "};\n"
        "Holder makeHolder(int data[]) {\n"
        "    Holder h;\n"
        "    h.view = data;\n"
        "    return h;\n"
        "}\n"
        "int readHolder(Holder h, int idx) {\n"
        "    return h.view[idx];\n"
        "}\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int data[10];\n"
        "        data[7] = 888;\n"
        "        Holder h = makeHolder(data);\n"
        "        print(\"READ:\");\n"
        "        print(readHolder(h, 7));\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "READ:888");
    return true;
}

// ── v0.5.3 Adaptive Memory Engine & Sparse RAII Tests ───────────────────────

TAMARIN_TEST(PageProvider, WindowsAllocationAndGranularity) {
    using namespace tamarin::runtime;
    WindowsPageProvider provider;
    EXPECT_EQ(provider.allocationGranularity(), 65536ULL);
    EXPECT_EQ(provider.pageSize(), 4096ULL);

    void* base = provider.reserve(65536);
    EXPECT_TRUE(base != nullptr);

    bool commit1 = provider.commit(base, 0, 4096);
    EXPECT_TRUE(commit1);

    auto* p = static_cast<unsigned char*>(base);
    for (size_t i = 0; i < 4096; ++i) {
        EXPECT_EQ(p[i], 0);
    }

    p[0] = 42;
    p[4095] = 99;
    EXPECT_EQ(p[0], 42);
    EXPECT_EQ(p[4095], 99);

    bool commit2 = provider.commit(base, 4096, 4096);
    EXPECT_TRUE(commit2);

    auto* p2 = static_cast<unsigned char*>(base) + 4096;
    for (size_t i = 0; i < 4096; ++i) {
        EXPECT_EQ(p2[i], 0);
    }

    provider.decommit(base, 0, 8192);
    EXPECT_TRUE(provider.release(base));
    return true;
}

TAMARIN_TEST(PageProvider, FallbackProviderAllocation) {
    using namespace tamarin::runtime;
    FallbackPageProvider provider;
    EXPECT_TRUE(provider.pageSize() >= 4096ULL);
    void* base = provider.reserve(8192);
    EXPECT_TRUE(base != nullptr);
    EXPECT_TRUE(provider.commit(base, 0, 8192));
    EXPECT_TRUE(provider.release(base));
    return true;
}

TAMARIN_TEST(CodeGen, RegionEmptyZeroAllocations) {
    std::string src =
        "int main() {\n"
        "    print(\"START;\");\n"
        "    region r {\n"
        "        // zero allocations\n"
        "    }\n"
        "    print(\"END\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out, "", true));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("START;") != std::string::npos);
    EXPECT_TRUE(out.find("END") != std::string::npos);
    EXPECT_TRUE(out.find("Allocations:                0") != std::string::npos);
    EXPECT_TRUE(out.find("Virtual reserved:           0 bytes") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, RegionSingleDestructorExecution) {
    std::string src =
        "class Resource {\n"
        "public:\n"
        "    int id;\n"
        "    Resource(int val) {\n"
        "        this.id = val;\n"
        "        print(\"C:\");\n"
        "        print(this.id);\n"
        "        print(\";\");\n"
        "    }\n"
        "    ~Resource() {\n"
        "        print(\"D:\");\n"
        "        print(this.id);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Resource res(101);\n"
        "        print(\"BODY;\");\n"
        "    }\n"
        "    print(\"DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "C:101;BODY;D:101;DONE");
    return true;
}

TAMARIN_TEST(CodeGen, RegionMultipleDestructorsStrictLIFO) {
    std::string src =
        "class Node {\n"
        "public:\n"
        "    int id;\n"
        "    Node(int val) {\n"
        "        this.id = val;\n"
        "        print(\"C\");\n"
        "        print(this.id);\n"
        "    }\n"
        "    ~Node() {\n"
        "        print(\"D\");\n"
        "        print(this.id);\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Node a(1);\n"
        "        Node b(2);\n"
        "        Node c(3);\n"
        "        print(\"|\");\n"
        "    }\n"
        "    print(\"!\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "C1C2C3|D3D2D1!");
    return true;
}

TAMARIN_TEST(CodeGen, RegionArrayOfResourceOwnersDestruction) {
    std::string src =
        "class Token {\n"
        "public:\n"
        "    int id;\n"
        "    Token() {\n"
        "        this.id = 77;\n"
        "    }\n"
        "    ~Token() {\n"
        "        print(\"T\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Token tokens[4];\n"
        "        print(\"ALLOC;\");\n"
        "    }\n"
        "    print(\"DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "ALLOC;TTTTDONE");
    return true;
}

TAMARIN_TEST(CodeGen, RegionAggregateMemberDestructors) {
    std::string src =
        "class Member {\n"
        "public:\n"
        "    int val;\n"
        "    Member() { this.val = 12;\n }\n"
        "    ~Member() { print(\"MEMBER_DTOR;\"); }\n"
        "};\n"
        "struct Composite {\n"
        "    Member m;\n"
        "    int extra;\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Composite c;\n"
        "        print(\"INSIDE;\");\n"
        "    }\n"
        "    print(\"FINISH\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "INSIDE;MEMBER_DTOR;FINISH");
    return true;
}

TAMARIN_TEST(CodeGen, RegionInterleavedTrivialAndNonTrivial) {
    std::string src =
        "struct Point { int x; int y; };\n"
        "class Resource {\n"
        "public:\n"
        "    int num;\n"
        "    Resource(int t) { this.num = t; }\n"
        "    ~Resource() {\n"
        "        print(\"R\");\n"
        "        print(this.num);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Point p1;\n"
        "        p1.x = 100;\n"
        "        Resource r1(1);\n"
        "        int buf[64];\n"
        "        buf[10] = 55;\n"
        "        Resource r2(2);\n"
        "        print(\"MID:\");\n"
        "        print(p1.x + buf[10]);\n"
        "        print(\";\");\n"
        "    }\n"
        "    print(\"EXIT\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "MID:155;R2;R1;EXIT");
    return true;
}

TAMARIN_TEST(CodeGen, RegionDestructorsEarlyReturn) {
    std::string src =
        "class Guard {\n"
        "public:\n"
        "    int code;\n"
        "    Guard(int c) { this.code = c; }\n"
        "    ~Guard() {\n"
        "        print(\"G\");\n"
        "        print(this.code);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int testHelper(int flag) {\n"
        "    region r {\n"
        "        Guard g1(10);\n"
        "        if (flag == 1) {\n"
        "            Guard g2(20);\n"
        "            print(\"EARLY;\");\n"
        "            return 99;\n"
        "        }\n"
        "    }\n"
        "    return 0;\n"
        "}\n"
        "int main() {\n"
        "    int res = testHelper(1);\n"
        "    print(\"RES:\");\n"
        "    print(res);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "EARLY;G20;G10;RES:99");
    return true;
}

TAMARIN_TEST(CodeGen, RegionDestructorsInLoop) {
    std::string src =
        "class Probe {\n"
        "public:\n"
        "    int step;\n"
        "    Probe(int s) { this.step = s; }\n"
        "    ~Probe() {\n"
        "        print(\"P\");\n"
        "        print(this.step);\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    int i = 1;\n"
        "    while (i <= 3) {\n"
        "        region r {\n"
        "            Probe p(i);\n"
        "        }\n"
        "        i = i + 1;\n"
        "    }\n"
        "    print(\";DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "P1P2P3;DONE");
    return true;
}

TAMARIN_TEST(CodeGen, RegionMultiPageCrossingZeroInit) {
    std::string src =
        "int main() {\n"
        "    region r {\n"
        "        int big[4096];\n"
        "        int nonZero = 0;\n"
        "        int i = 0;\n"
        "        while (i < 4096) {\n"
        "            if (big[i] != 0) { nonZero = nonZero + 1; }\n"
        "            i = i + 1;\n"
        "        }\n"
        "        big[0] = 111;\n"
        "        big[1024] = 222;\n"
        "        big[2048] = 333;\n"
        "        big[4095] = 444;\n"
        "        print(\"NZ:\");\n"
        "        print(nonZero);\n"
        "        print(\";SUM:\");\n"
        "        print(big[0] + big[1024] + big[2048] + big[4095]);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "NZ:0;SUM:1110");
    return true;
}

TAMARIN_TEST(CodeGen, RegionChunkGrowthExceeding64K) {
    std::string src =
        "int main() {\n"
        "    region r {\n"
        "        int first[20000];\n"
        "        first[0] = 10;\n"
        "        first[19999] = 20;\n"
        "        int second[10000];\n"
        "        second[0] = 30;\n"
        "        second[9999] = 40;\n"
        "        print(\"TOTAL:\");\n"
        "        print(first[0] + first[19999] + second[0] + second[9999]);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out, "", true));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("TOTAL:100") != std::string::npos);
    EXPECT_TRUE(out.find("Chunks:") != std::string::npos);
    EXPECT_TRUE(out.find("Allocations:") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, RegionMemoryTelemetryOutput) {
    std::string src =
        "int main() {\n"
        "    region scratch {\n"
        "        int buf[256];\n"
        "        buf[0] = 7;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out, "", true));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("[TAMARIN MEMORY STATS] Region destroyed:") != std::string::npos);
    EXPECT_TRUE(out.find("Initial hint:") != std::string::npos);
    EXPECT_TRUE(out.find("Virtual reserved:") != std::string::npos);
    EXPECT_TRUE(out.find("Committed bytes / commit charge:") != std::string::npos);
    EXPECT_TRUE(out.find("Payload handed out:") != std::string::npos);
    EXPECT_TRUE(out.find("Alignment padding:") != std::string::npos);
    EXPECT_TRUE(out.find("Unused committed bytes:") != std::string::npos);
    EXPECT_TRUE(out.find("Reservation count:") != std::string::npos);
    EXPECT_TRUE(out.find("Commit operation count:") != std::string::npos);
    return true;
}

TAMARIN_TEST(Driver, StaticMemoryPlanningReport) {
    std::string src =
        "int globalVal;\n"
        "class Resource {\n"
        "    int id;\n"
        "    ~Resource() {}\n"
        "};\n"
        "int main() {\n"
        "    int localStack = 10;\n"
        "    region scratch {\n"
        "        int regionBuf[100];\n"
        "        Resource regionRes;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn", src);
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));

    std::ostringstream reportStream;
    sema.checker().printMemoryReport(reportStream);
    std::string rep = reportStream.str();

    EXPECT_TRUE(rep.find("=== TAMARIN Static Memory Planning Report ===") != std::string::npos);
    EXPECT_TRUE(rep.find("globalVal") != std::string::npos);
    EXPECT_TRUE(rep.find("localStack") != std::string::npos);
    EXPECT_TRUE(rep.find("regionBuf") != std::string::npos);
    EXPECT_TRUE(rep.find("regionRes") != std::string::npos);
    EXPECT_TRUE(rep.find("Sparse Sidecar") != std::string::npos);
    EXPECT_TRUE(rep.find("Total Region Payload:") != std::string::npos);
    return true;
}

TAMARIN_TEST(PageProvider, DynamicQueryAndInvalidRanges) {
    using namespace tamarin::runtime;
    auto& provider = getPlatformPageProvider();
    EXPECT_TRUE(provider.pageSize() >= 4096ULL);
    EXPECT_TRUE(provider.allocationGranularity() >= 4096ULL);

    // Invalid parameters
    EXPECT_TRUE(provider.reserve(0) == nullptr);
    EXPECT_FALSE(provider.commit(nullptr, 0, 4096));
    EXPECT_FALSE(provider.commit(nullptr, 0, 0));
    EXPECT_FALSE(provider.decommit(nullptr, 0, 4096));
    EXPECT_FALSE(provider.decommit(nullptr, 0, 0));
    EXPECT_FALSE(provider.release(nullptr));

    void* base = provider.reserve(provider.allocationGranularity());
    EXPECT_TRUE(base != nullptr);
    EXPECT_FALSE(provider.commit(base, 0, 0));
    EXPECT_FALSE(provider.decommit(base, 0, 0));
    EXPECT_TRUE(provider.commit(base, 0, provider.pageSize()));
    EXPECT_TRUE(provider.decommit(base, 0, provider.pageSize()));
    EXPECT_TRUE(provider.release(base));
    return true;
}

TAMARIN_TEST(PageProvider, FallbackSemanticsDetailed) {
    using namespace tamarin::runtime;
    FallbackPageProvider provider;
    EXPECT_EQ(provider.pageSize(), 4096ULL);
    EXPECT_EQ(provider.allocationGranularity(), 65536ULL);

    // Invalid inputs
    EXPECT_TRUE(provider.reserve(0) == nullptr);
    EXPECT_FALSE(provider.commit(nullptr, 0, 4096));
    EXPECT_FALSE(provider.commit(nullptr, 0, 0));
    EXPECT_FALSE(provider.decommit(nullptr, 0, 4096));
    EXPECT_FALSE(provider.decommit(nullptr, 0, 0));
    EXPECT_FALSE(provider.release(nullptr));

    // Valid lifecycle
    void* base = provider.reserve(8192);
    EXPECT_TRUE(base != nullptr);
    auto* bytes = static_cast<unsigned char*>(base);
    for (size_t i = 0; i < 8192; ++i) {
        EXPECT_EQ(bytes[i], 0);
    }
    bytes[0] = 0xAA;
    bytes[4095] = 0xBB;
    EXPECT_TRUE(provider.commit(base, 0, 4096));
    EXPECT_EQ(bytes[0], 0);
    EXPECT_EQ(bytes[4095], 0);

    EXPECT_TRUE(provider.decommit(base, 0, 4096));
    EXPECT_TRUE(provider.release(base));
    return true;
}

TAMARIN_TEST(CodeGen, ClassArrayDefaultConstructionAndDestructionOrder) {
    std::string src =
        "class Token {\n"
        "public:\n"
        "    int id;\n"
        "    Token() {\n"
        "        this.id = 77;\n"
        "        print(\"C;\");\n"
        "    }\n"
        "    ~Token() {\n"
        "        print(\"D:\");\n"
        "        print(this.id);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Token tokens[4];\n"
        "        print(\"MID;\");\n"
        "    }\n"
        "    print(\"DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "C;C;C;C;MID;D:77;D:77;D:77;D:77;DONE");
    return true;
}

TAMARIN_TEST(CodeGen, ClassArrayExplicitConstructorsOrder) {
    std::string src =
        "class Item {\n"
        "public:\n"
        "    int val;\n"
        "    Item(int v) {\n"
        "        this.val = v;\n"
        "        print(\"I:\");\n"
        "        print(this.val);\n"
        "        print(\";\");\n"
        "    }\n"
        "    ~Item() {\n"
        "        print(\"~I:\");\n"
        "        print(this.val);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Item items[3] = {\n"
        "            Item(10),\n"
        "            Item(20),\n"
        "            Item(30)\n"
        "        };\n"
        "        print(\"MID;\");\n"
        "    }\n"
        "    print(\"DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "I:10;I:20;I:30;MID;~I:30;~I:20;~I:10;DONE");
    return true;
}

TAMARIN_TEST(CodeGen, AggregateNonTrivialMemberConstructorOrder) {
    std::string src =
        "class Member1 {\n"
        "public:\n"
        "    int seq;\n"
        "    Member1() {\n"
        "        this.seq = 1;\n"
        "        print(\"MC1;\");\n"
        "    }\n"
        "    ~Member1() {\n"
        "        print(\"MD1:\");\n"
        "        print(this.seq);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "class Member2 {\n"
        "public:\n"
        "    int seq;\n"
        "    Member2() {\n"
        "        this.seq = 2;\n"
        "        print(\"MC2;\");\n"
        "    }\n"
        "    ~Member2() {\n"
        "        print(\"MD2:\");\n"
        "        print(this.seq);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "struct Composite {\n"
        "    Member1 first;\n"
        "    int padding;\n"
        "    Member2 second;\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Composite c;\n"
        "        print(\"MID;\");\n"
        "    }\n"
        "    print(\"DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "MC1;MC2;MID;MD2:2;MD1:1;DONE");
    return true;
}

TAMARIN_TEST(CodeGen, InheritanceBaseAndDerivedConstructorDestructorOrder) {
    std::string src =
        "class Base {\n"
        "public:\n"
        "    Base() { print(\"BaseC;\"); }\n"
        "    ~Base() { print(\"BaseD;\"); }\n"
        "};\n"
        "class Derived : Base {\n"
        "public:\n"
        "    Derived() { print(\"DerivedC;\"); }\n"
        "    ~Derived() { print(\"DerivedD;\"); }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Derived d;\n"
        "        print(\"MID;\");\n"
        "    }\n"
        "    print(\"DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "BaseC;DerivedC;MID;DerivedD;BaseD;DONE");
    return true;
}

TAMARIN_TEST(CodeGen, RegionSparseSidecarAdaptiveGrowth) {
    std::string src =
        "class Tracer {\n"
        "public:\n"
        "    int id;\n"
        "    Tracer(int val) { this.id = val; }\n"
        "    ~Tracer() {\n"
        "        print(\"T\");\n"
        "        print(this.id);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        Tracer t0(0);\n"
        "        Tracer t1(1);\n"
        "        Tracer t2(2);\n"
        "        Tracer t3(3);\n"
        "        Tracer t4(4);\n"
        "        Tracer t5(5);\n"
        "        Tracer t6(6);\n"
        "        Tracer t7(7);\n"
        "        Tracer t8(8);\n"
        "        Tracer t9(9);\n"
        "        print(\"MID;\");\n"
        "    }\n"
        "    print(\"DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "MID;T9;T8;T7;T6;T5;T4;T3;T2;T1;T0;DONE");
    return true;
}

TAMARIN_TEST(CodeGen, StackArrayDefaultConstructionAndDestructionOrder) {
    std::string src =
        "class Box {\n"
        "public:\n"
        "    int num;\n"
        "    Box() {\n"
        "        this.num = 88;\n"
        "        print(\"B;\");\n"
        "    }\n"
        "    ~Box() {\n"
        "        print(\"~B:\");\n"
        "        print(this.num);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    {\n"
        "        Box boxes[3];\n"
        "        print(\"STACK_MID;\");\n"
        "    }\n"
        "    print(\"STACK_DONE\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "B;B;B;STACK_MID;~B:88;~B:88;~B:88;STACK_DONE");
    return true;
}

TAMARIN_TEST(CodeGen, ImplicitBaseConstructorNonTrivialMemberOrder) {
    std::string src =
        "class Resource {\n"
        "public:\n"
        "    int id;\n"
        "    Resource() {\n"
        "        this.id = 99;\n"
        "        print(\"R_CTOR;\");\n"
        "    }\n"
        "    ~Resource() {\n"
        "        print(\"~R:\");\n"
        "        print(this.id);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "class BaseNoCtor {\n"
        "public:\n"
        "    Resource baseRes;\n"
        "};\n"
        "class DerivedWithCtor : BaseNoCtor {\n"
        "public:\n"
        "    int derivedVal;\n"
        "    DerivedWithCtor(int v) {\n"
        "        this.derivedVal = v;\n"
        "        print(\"D_CTOR:\");\n"
        "        print(this.baseRes.id);\n"
        "        print(\";\");\n"
        "    }\n"
        "    ~DerivedWithCtor() {\n"
        "        print(\"~D:\");\n"
        "        print(this.derivedVal);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region r {\n"
        "        DerivedWithCtor d(42);\n"
        "        print(\"BODY;\");\n"
        "    }\n"
        "    print(\"EXIT\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_EQ(out, "R_CTOR;D_CTOR:99;BODY;~D:42;~R:99;EXIT");
    return true;
}

TAMARIN_TEST(CodeGen, GeneratedAllocatorUsesProviderReportedValues) {
    _putenv("TAMARIN_ALLOC_GRANULARITY=131072");
    _putenv("TAMARIN_PAGE_SIZE=8192");
    std::string src =
        "int main() {\n"
        "    region r {\n"
        "        int buf[10];\n"
        "        buf[0] = 77;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out, "", true));
    EXPECT_EQ(exitCode, 0);
    _putenv("TAMARIN_ALLOC_GRANULARITY=");
    _putenv("TAMARIN_PAGE_SIZE=");
    EXPECT_TRUE(out.find("Virtual reserved:      131072 bytes") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, MemoryTelemetryProcessWorkingSet) {
    std::string src =
        "int main() {\n"
        "    region r {\n"
        "        int x = 123;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out, "", true));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Committed bytes / commit charge:") != std::string::npos);
    EXPECT_TRUE(out.find("Process working set (resident RAM):") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, ReservationCacheDirectMultithreadedIsolation) {
    constexpr int NUM_THREADS = 8;
    constexpr int ITERS = 100;
    std::vector<std::thread> threads;
    std::mutex mu;
    std::unordered_set<void*> globallyActiveReservations;
    std::atomic<bool> noDoubleUse{true};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&mu, &globallyActiveReservations, &noDoubleUse]() {
            for (int i = 0; i < ITERS; ++i) {
                void* ptr = __tamarin_runtime_rcache_acquire(65536);
                if (!ptr) {
                    ptr = __tamarin_runtime_page_reserve(65536);
                }
                if (!ptr) { noDoubleUse = false; break; }

                {
                    std::lock_guard<std::mutex> lock(mu);
                    if (globallyActiveReservations.count(ptr) > 0) {
                        noDoubleUse = false;
                    }
                    globallyActiveReservations.insert(ptr);
                }

                void* committed = __tamarin_runtime_page_commit(ptr, 0, 4096);
                if (committed) {
                    std::memset(committed, 0xAB, 4096);
                }

                {
                    std::lock_guard<std::mutex> lock(mu);
                    globallyActiveReservations.erase(ptr);
                }

                int putResult = __tamarin_runtime_rcache_put(ptr, 65536, 4096);
                if (!putResult) {
                    __tamarin_runtime_page_release(ptr);
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    EXPECT_TRUE(noDoubleUse.load());
    return true;
}

#ifdef _WIN32
TAMARIN_TEST(CodeGen, ReservationCacheMultithreadedStress) {
    std::string src =
        "int runRegionBatch(int seed) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < 50; i++) {\n"
        "        region r(4096) {\n"
        "            int buf[100];\n"
        "            for (int j = 0; j < 100; j++) {\n"
        "                buf[j] = seed + i * 100 + j;\n"
        "                total += buf[j];\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n";
    SourceManager sm;
    auto fileId = sm.addSource("test_mt.tmn", src);
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    EXPECT_TRUE(!diag.hasErrors());
    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "test_mt");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int mtRunId = 0;
    auto llPath = tempDir / ("tamarin_mt_" + std::to_string(++mtRunId) + ".ll");
    auto dllPath = tempDir / ("tamarin_mt_" + std::to_string(mtRunId) + ".dll");
    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));

    std::string clang = codegen.getClangPath();
    
    std::string rtLib;
    for (const auto& libPath : {"build/Release/tamarin_runtime.lib", "build/Debug/tamarin_runtime.lib", "Release/tamarin_runtime.lib", "Debug/tamarin_runtime.lib"}) {
        if (std::filesystem::exists(libPath)) { rtLib = std::filesystem::absolute(libPath).string(); break; }
    }
    std::string cmd = "\"\"" + clang + "\" -O2 -shared -Wl,/export:runRegionBatch --target=x86_64-pc-windows-msvc -Wno-override-module -o \"" +
                      dllPath.string() + "\" \"" + llPath.string() + "\" \"" + rtLib + "\"\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 0);

    HMODULE hMod = LoadLibraryA(dllPath.string().c_str());
    EXPECT_TRUE(hMod != nullptr);

    typedef int (*FnType)(int);
    FnType fn = (FnType)GetProcAddress(hMod, "runRegionBatch");
    EXPECT_TRUE(fn != nullptr);

    constexpr int NUM_THREADS = 8;
    constexpr int ITERS_PER_THREAD = 20;
    std::vector<std::thread> threads;
    std::atomic<bool> allCorrect{true};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([fn, t, &allCorrect]() {
            for (int iter = 0; iter < ITERS_PER_THREAD; ++iter) {
                int seed = t * 1000 + iter;
                int res = fn(seed);
                int expected = 5000 * seed + 12497500;
                if (res != expected) {
                    allCorrect = false;
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    FreeLibrary(hMod);
    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(dllPath, ec);
    EXPECT_TRUE(allCorrect.load());
    return true;
}

TAMARIN_TEST(CodeGen, SmallRegionPoolSteadyStateZeroVirtualMemoryOps) {
    std::string src =
        "public int runHighChurn(int iters) {\n"
        "    int sum = 0;\n"
        "    for (int iter = 0; iter < iters; iter++) {\n"
        "        region r(4096) {\n"
        "            int buf[16];\n"
        "            for (int i = 0; i < 16; i++) {\n"
        "                buf[i] = iter * 16 + i;\n"
        "                sum += buf[i];\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    return sum;\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n";
    SourceManager sm;
    auto fileId = sm.addSource("test_zero_sys.tmn", src);
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    EXPECT_TRUE(!diag.hasErrors());
    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "test_zero_sys");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int zrRunId = 0;
    auto llPath = tempDir / ("tamarin_zero_sys_" + std::to_string(++zrRunId) + ".ll");
    auto dllPath = tempDir / ("tamarin_zero_sys_" + std::to_string(zrRunId) + ".dll");
    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));

    std::string clang = codegen.getClangPath();
    std::string rtLib;
    for (const auto& libPath : {"build/Release/tamarin_runtime.lib", "build/Debug/tamarin_runtime.lib", "Release/tamarin_runtime.lib", "Debug/tamarin_runtime.lib"}) {
        if (std::filesystem::exists(libPath)) { rtLib = std::filesystem::absolute(libPath).string(); break; }
    }
    std::string cmd = "\"\"" + clang + "\" -O2 -shared -Wl,/export:runHighChurn --target=x86_64-pc-windows-msvc -Wno-override-module -o \"" +
                      dllPath.string() + "\" \"" + llPath.string() + "\" \"" + rtLib + "\"\"";
    int ret = std::system(cmd.c_str());
    EXPECT_EQ(ret, 0);

    HMODULE hMod = LoadLibraryA(dllPath.string().c_str());
    EXPECT_TRUE(hMod != nullptr);

    typedef int (*FnType)(int);
    FnType fn = (FnType)GetProcAddress(hMod, "runHighChurn");
    EXPECT_TRUE(fn != nullptr);

    // Warmup iteration: warms up the thread-local pool
    __tamarin_runtime_rcache_set_policy(1, 131072);
    fn(1);

    // Reset counters to prove zero steady-state calls
    __tamarin_runtime_reset_counters();

    // Run 10,000 steady-state iterations
    fn(10000);

    // Assert zero OS virtual-memory operations occurred during the steady-state loop
    EXPECT_EQ(__tamarin_runtime_get_reserve_count(), 0ULL);
    EXPECT_EQ(__tamarin_runtime_get_commit_count(), 0ULL);
    EXPECT_EQ(__tamarin_runtime_get_decommit_count(), 0ULL);
    EXPECT_EQ(__tamarin_runtime_get_release_count(), 0ULL);

    FreeLibrary(hMod);
    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(dllPath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, SmallRegionPoolZeroInitializationGuaranteed) {
    std::string src =
        "int main() {\n"
        "    region r1(4096) {\n"
        "        long dirty[16];\n"
        "        for (int i = 0; i < 16; i++) {\n"
        "            dirty[i] = 987654321;\n"
        "        }\n"
        "    }\n"
        "    region r2(4096) {\n"
        "        long clean[16];\n"
        "        long sum = 0;\n"
        "        for (int i = 0; i < 16; i++) {\n"
        "            sum += clean[i];\n"
        "        }\n"
        "        print(\"CLEAN_SUM:\");\n"
        "        print(sum);\n"
        "        print(\";\");\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("CLEAN_SUM:0;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SmallRegionPoolAggregateMemoryCapEnforced) {
    __tamarin_runtime_rcache_clear();
    __tamarin_runtime_rcache_set_policy(1, 65536); // 64 KB cap

    void* b1 = __tamarin_runtime_page_reserve(65536);
    __tamarin_runtime_page_commit(b1, 0, 65536);

    void* b2 = __tamarin_runtime_page_reserve(65536);
    __tamarin_runtime_page_commit(b2, 0, 65536);

    // Put b1: should be retained with 65536 committed
    int res1 = __tamarin_runtime_rcache_put(b1, 65536, 65536, 1024);
    EXPECT_EQ(res1, 1);
    EXPECT_EQ(__tamarin_runtime_rcache_get_aggregate_committed(), 65536ULL);

    // Put b2: cap is 65536, so aggregate would exceed cap. b2 must be decommitted down to 0
    int res2 = __tamarin_runtime_rcache_put(b2, 65536, 65536, 1024);
    EXPECT_EQ(res2, 1);
    // Aggregate committed remains capped at 65536
    EXPECT_EQ(__tamarin_runtime_rcache_get_aggregate_committed(), 65536ULL);

    __tamarin_runtime_rcache_clear();
    EXPECT_EQ(__tamarin_runtime_rcache_get_aggregate_committed(), 0ULL);
    __tamarin_runtime_rcache_set_policy(1, 131072); // restore default
    return true;
}

TAMARIN_TEST(CodeGen, SmallRegionPoolPolicy0DecommitBehavior) {
    __tamarin_runtime_rcache_clear();
    __tamarin_runtime_rcache_set_policy(0, 131072);
    __tamarin_runtime_reset_counters();

    void* b = __tamarin_runtime_page_reserve(65536);
    __tamarin_runtime_page_commit(b, 0, 4096);

    // Put b under policy 0: must decommit
    int res = __tamarin_runtime_rcache_put(b, 65536, 4096, 100);
    EXPECT_EQ(res, 1);
    EXPECT_TRUE(__tamarin_runtime_get_decommit_count() > 0ULL);
    EXPECT_EQ(__tamarin_runtime_rcache_get_aggregate_committed(), 0ULL);

    __tamarin_runtime_rcache_clear();
    __tamarin_runtime_rcache_set_policy(1, 131072);
    return true;
}

TAMARIN_TEST(CodeGen, LargeRegionBypassesSmallRegionPool) {
    __tamarin_runtime_rcache_clear();
    __tamarin_runtime_rcache_set_policy(1, 131072);
    __tamarin_runtime_reset_counters();

    // 128 KB reservation > 64 KB allocation granularity
    void* big = __tamarin_runtime_page_reserve(131072);
    __tamarin_runtime_page_commit(big, 0, 4096);

    // Large region put: must bypass pool and release immediately to OS
    int res = __tamarin_runtime_rcache_put(big, 131072, 4096, 4096);
    EXPECT_EQ(res, 0); // Not cached in pool
    EXPECT_TRUE(__tamarin_runtime_get_release_count() > 0ULL);

    return true;
}

TAMARIN_TEST(CodeGen, SlabBasicInsertGetAndRemove) {
    std::string src =
        "int main() {\n"
        "    Slab<int> s(16);\n"
        "    Handle<int> h1 = s.insert(10);\n"
        "    Handle<int> h2 = s.insert(20);\n"
        "    Handle<int> h3 = s.insert(30);\n"
        "    if (s.count() != 3) { return 1; }\n"
        "    if (s[h1] != 10) { return 2; }\n"
        "    if (s[h2] != 20) { return 3; }\n"
        "    if (s[h3] != 30) { return 4; }\n"
        "    s[h2] = 25;\n"
        "    if (s[h2] != 25) { return 5; }\n"
        "    bool rmOk = s.remove(h2);\n"
        "    if (!rmOk) { return 6; }\n"
        "    if (s.count() != 2) { return 7; }\n"
        "    if (s.is_valid(h2)) { return 8; }\n"
        "    int val = 0;\n"
        "    if (s.try_get(h2, val)) { return 9; }\n"
        "    print(\"SLAB_BASIC_OK\\n\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("SLAB_BASIC_OK") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SlabSlotRecyclingAndGenerationalParity) {
    std::string src =
        "int main() {\n"
        "    Slab<int> s(8);\n"
        "    Handle<int> h1 = s.insert(100);\n"
        "    int slot1 = h1.slot();\n"
        "    int gen1 = h1.generation();\n"
        "    if (gen1 != 1) { return 1; }\n" // First issued gen is 1 (odd)
        "    s.remove(h1);\n"
        "    Handle<int> h2 = s.insert(200);\n"
        "    int slot2 = h2.slot();\n"
        "    int gen2 = h2.generation();\n"
        "    // Reused slot 0 with incremented odd generation 3\n"
        "    if (slot1 != slot2) { return 2; }\n"
        "    if (gen2 != 3) { return 3; }\n"
        "    if (s.is_valid(h1)) { return 4; }\n" // Stale handle must be invalid
        "    if (!s.is_valid(h2)) { return 5; }\n"
        "    if (s[h2] != 200) { return 6; }\n"
        "    print(\"RECYCLED_OK\\n\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("RECYCLED_OK") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SlabDoubleFreeRejection) {
    std::string src =
        "int main() {\n"
        "    Slab<int> s(8);\n"
        "    Handle<int> h = s.insert(42);\n"
        "    bool rm1 = s.remove(h);\n"
        "    if (!rm1) { return 1; }\n"
        "    bool rm2 = s.remove(h);\n"
        "    if (rm2) { return 2; }\n" // Double remove must safely return false
        "    if (s.count() != 0) { return 3; }\n"
        "    print(\"DOUBLE_FREE_SAFE\\n\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("DOUBLE_FREE_SAFE") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SlabDynamicGrowthAndPageProvider) {
    std::string src =
        "int main() {\n"
        "    Slab<int> s(16);\n"
        "    Handle<int> handles[64];\n"
        "    for (int i = 0; i < 64; i++) {\n"
        "        handles[i] = s.insert(i * 10);\n"
        "    }\n"
        "    if (s.count() != 64) { return 1; }\n"
        "    if (s.capacity() < 64) { return 2; }\n"
        "    int checkSum = 0;\n"
        "    for (int j = 0; j < 64; j++) {\n"
        "        Handle<int> h = handles[j];\n"
        "        if (!s.is_valid(h)) { return 3; }\n"
        "        checkSum += s[h];\n"
        "    }\n"
        "    print(\"SUM:\");\n"
        "    print(checkSum);\n"
        "    print(\"\\n\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("SUM:20160") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SlabNonTrivialDestructorLifecycle) {
    std::string src =
        "class Resource {\n"
        "public:\n"
        "    int id;\n"
        "    Resource(int val) { this.id = val; }\n"
        "    ~Resource() {\n"
        "        print(\"D\");\n"
        "        print(this.id);\n"
        "        print(\";\");\n"
        "    }\n"
        "};\n"
        "void runScope() {\n"
        "    Slab<Resource> s(8);\n"
        "    Resource r1(1);\n"
        "    Resource r2(2);\n"
        "    Handle<Resource> h1 = s.insert(r1);\n"
        "    Handle<Resource> h2 = s.insert(r2);\n"
        "    s.remove(h1);\n"
        "}\n"
        "int main() {\n"
        "    runScope();\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    // h1 removed explicitly -> D1; then scope ends -> remaining active h2 (D2) and local r2/r1 destructors
    EXPECT_TRUE(out.find("D1;") != std::string::npos);
    EXPECT_TRUE(out.find("D2;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, PatternMatchingEnumUnitVariants) {
    std::string src =
        "enum Color {\n"
        "    Red,\n"
        "    Green,\n"
        "    Blue\n"
        "};\n"
        "int colorCode(Color c) {\n"
        "    return match c {\n"
        "        Color::Red => 10,\n"
        "        Color::Green => 20,\n"
        "        Color::Blue => 30\n"
        "    };\n"
        "}\n"
        "int main() {\n"
        "    Color c1 = Color::Red;\n"
        "    Color c2 = Color::Green;\n"
        "    Color c3 = Color::Blue;\n"
        "    int sum = colorCode(c1) + colorCode(c2) + colorCode(c3);\n"
        "    print(\"Sum=\");\n"
        "    print(sum);\n"
        "    print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Sum=60;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, PatternMatchingAlgebraicDataTypes) {
    std::string src =
        "enum Shape {\n"
        "    Point,\n"
        "    Circle(int),\n"
        "    Rect(int, int)\n"
        "};\n"
        "int area(Shape s) {\n"
        "    return match s {\n"
        "        Shape::Point => 0,\n"
        "        Shape::Circle(r) => 3 * r * r,\n"
        "        Shape::Rect(w, h) => w * h\n"
        "    };\n"
        "}\n"
        "int main() {\n"
        "    Shape p = Shape::Point;\n"
        "    Shape c = Shape::Circle(5);\n"
        "    Shape r = Shape::Rect(4, 6);\n"
        "    print(\"P=\"); print(area(p)); print(\";\");\n"
        "    print(\"C=\"); print(area(c)); print(\";\");\n"
        "    print(\"R=\"); print(area(r)); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("P=0;") != std::string::npos);
    EXPECT_TRUE(out.find("C=75;") != std::string::npos);
    EXPECT_TRUE(out.find("R=24;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, PatternMatchingOptionAndResult) {
    std::string src =
        "int unwrapOrZero(Option<int> opt) {\n"
        "    return match opt {\n"
        "        Option::Some(x) => x,\n"
        "        Option::None => 0\n"
        "    };\n"
        "}\n"
        "int checkResult(Result<int, int> res) {\n"
        "    return match res {\n"
        "        Result::Ok(val) => val,\n"
        "        Result::Err(code) => -code\n"
        "    };\n"
        "}\n"
        "int main() {\n"
        "    Option<int> o1 = Option::Some(42);\n"
        "    Option<int> o2 = Option::None;\n"
        "    Result<int, int> r1 = Result::Ok(100);\n"
        "    Result<int, int> r2 = Result::Err(404);\n"
        "    print(\"O1=\"); print(unwrapOrZero(o1)); print(\";\");\n"
        "    print(\"O2=\"); print(unwrapOrZero(o2)); print(\";\");\n"
        "    print(\"R1=\"); print(checkResult(r1)); print(\";\");\n"
        "    print(\"R2=\"); print(checkResult(r2)); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("O1=42;") != std::string::npos);
    EXPECT_TRUE(out.find("O2=0;") != std::string::npos);
    EXPECT_TRUE(out.find("R1=100;") != std::string::npos);
    EXPECT_TRUE(out.find("R2=-404;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SafeErrorHandlingTryOperator) {
    std::string src =
        "Result<int, int> stepOne(bool fail) {\n"
        "    if (fail) {\n"
        "        return Result::Err(500);\n"
        "    }\n"
        "    return Result::Ok(10);\n"
        "}\n"
        "Result<int, int> runPipeline(bool fail) {\n"
        "    int v = stepOne(fail)?;\n"
        "    return Result::Ok(v * 2);\n"
        "}\n"
        "int main() {\n"
        "    Result<int, int> okRes = runPipeline(false);\n"
        "    Result<int, int> errRes = runPipeline(true);\n"
        "    int v1 = match okRes {\n"
        "        Result::Ok(val) => val,\n"
        "        Result::Err(err) => 0\n"
        "    };\n"
        "    int v2 = match errRes {\n"
        "        Result::Ok(val) => 0,\n"
        "        Result::Err(err) => err\n"
        "    };\n"
        "    print(\"V1=\"); print(v1); print(\";\");\n"
        "    print(\"V2=\"); print(v2); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("V1=20;") != std::string::npos);
    EXPECT_TRUE(out.find("V2=500;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, NicheOptimizedOptionHandle) {
    std::string src =
        "struct Node {\n"
        "    int val;\n"
        "};\n"
        "int inspectHandle(Option<Handle<Node>> opt, Slab<Node>& s) {\n"
        "    return match opt {\n"
        "        Option::Some(h) => s[h].val,\n"
        "        Option::None => -1\n"
        "    };\n"
        "}\n"
        "int main() {\n"
        "    Slab<Node> s(4);\n"
        "    Node n;\n"
        "    n.val = 777;\n"
        "    Handle<Node> h = s.insert(n);\n"
        "    Option<Handle<Node>> someH = Option::Some(h);\n"
        "    Option<Handle<Node>> noneH = Option::None;\n"
        "    print(\"Some=\"); print(inspectHandle(someH, s)); print(\";\");\n"
        "    print(\"None=\"); print(inspectHandle(noneH, s)); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Some=777;") != std::string::npos);
    EXPECT_TRUE(out.find("None=-1;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, MonomorphizedGenericFunctionExecution) {
    std::string src =
        "T addVals<T>(T a, T b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main() {\n"
        "    int iRes = addVals<int>(15, 27);\n"
        "    print(\"IntAdd=\"); print(iRes); print(\";\");\n"
        "    int iInf = addVals(100, 250);\n"
        "    print(\"IntInf=\"); print(iInf); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("IntAdd=42;") != std::string::npos);
    EXPECT_TRUE(out.find("IntInf=350;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, MonomorphizedGenericStructExecution) {
    std::string src =
        "struct Box<T> {\n"
        "    T val;\n"
        "};\n"
        "int main() {\n"
        "    Box<int> b;\n"
        "    b.val = 999;\n"
        "    print(\"BoxInt=\"); print(b.val); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("BoxInt=999;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, TraitStaticDispatchExecution) {
    std::string src =
        "trait Measurable {\n"
        "    int measure();\n"
        "}\n"
        "struct Rect {\n"
        "    int width;\n"
        "    int height;\n"
        "};\n"
        "impl Measurable for Rect {\n"
        "    int measure() {\n"
        "        return this.width * this.height;\n"
        "    }\n"
        "}\n"
        "int computeArea<T>(T item) where T: Measurable {\n"
        "    return item.measure();\n"
        "}\n"
        "int main() {\n"
        "    Rect r;\n"
        "    r.width = 7;\n"
        "    r.height = 6;\n"
        "    int a = computeArea(r);\n"
        "    print(\"Area=\"); print(a); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Area=42;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, MultiTypeParamAndSpecialization) {
    std::string src =
        "struct Pair<A, B> {\n"
        "    A first;\n"
        "    B second;\n"
        "};\n"
        "Pair<B, A> swapPair<A, B>(Pair<A, B> p) {\n"
        "    Pair<B, A> res;\n"
        "    res.first = p.second;\n"
        "    res.second = p.first;\n"
        "    return res;\n"
        "}\n"
        "int main() {\n"
        "    Pair<int, int> p1;\n"
        "    p1.first = 10;\n"
        "    p1.second = 20;\n"
        "    Pair<int, int> s1 = swapPair(p1);\n"
        "    print(\"S1=\"); print(s1.first); print(\",\"); print(s1.second); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("S1=20,10;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, GenericVectorCollection) {
    std::string src =
        "struct Vec<T> {\n"
        "    T data[16];\n"
        "    int len;\n"
        "    int cap;\n"
        "    void init() {\n"
        "        this.len = 0;\n"
        "        this.cap = 16;\n"
        "    }\n"
        "    void push(T val) {\n"
        "        if (this.len < this.cap) {\n"
        "            this.data[this.len] = val;\n"
        "            this.len = this.len + 1;\n"
        "        }\n"
        "    }\n"
        "    T get(int idx) {\n"
        "        return this.data[idx];\n"
        "    }\n"
        "    int size() {\n"
        "        return this.len;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Vec<int> v;\n"
        "    v.init();\n"
        "    v.push(10);\n"
        "    v.push(20);\n"
        "    v.push(30);\n"
        "    print(\"Len=\"); print(v.size()); print(\";\");\n"
        "    print(\"V1=\"); print(v.get(1)); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Len=3;") != std::string::npos);
    EXPECT_TRUE(out.find("V1=20;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, TraitStaticDispatchWithMultipleImplementations) {
    std::string src =
        "trait Summary {\n"
        "    int summarize();\n"
        "}\n"
        "struct Order {\n"
        "    int count;\n"
        "    int unitPrice;\n"
        "    int summarize() {\n"
        "        return this.count * this.unitPrice;\n"
        "    }\n"
        "};\n"
        "impl Summary for Order {\n"
        "    int summarize() {\n"
        "        return this.count * this.unitPrice;\n"
        "    }\n"
        "}\n"
        "struct Account {\n"
        "    int balance;\n"
        "    int summarize() {\n"
        "        return this.balance;\n"
        "    }\n"
        "};\n"
        "impl Summary for Account {\n"
        "    int summarize() {\n"
        "        return this.balance;\n"
        "    }\n"
        "}\n"
        "int getSummary<T>(T item) where T: Summary {\n"
        "    return item.summarize();\n"
        "}\n"
        "int main() {\n"
        "    Order o;\n"
        "    o.count = 4;\n"
        "    o.unitPrice = 25;\n"
        "    Account a;\n"
        "    a.balance = 500;\n"
        "    int s1 = getSummary(o);\n"
        "    int s2 = getSummary(a);\n"
        "    print(\"S1=\"); print(s1); print(\";\");\n"
        "    print(\"S2=\"); print(s2); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("S1=100;") != std::string::npos);
    EXPECT_TRUE(out.find("S2=500;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, GenericStack) {
    std::string src =
        "struct Stack<T> {\n"
        "    T elements[32];\n"
        "    int top;\n"
        "    void init() {\n"
        "        this.top = 0;\n"
        "    }\n"
        "    void push(T val) {\n"
        "        if (this.top < 32) {\n"
        "            this.elements[this.top] = val;\n"
        "            this.top = this.top + 1;\n"
        "        }\n"
        "    }\n"
        "    T pop() {\n"
        "        this.top = this.top - 1;\n"
        "        return this.elements[this.top];\n"
        "    }\n"
        "    T peek() {\n"
        "        return this.elements[this.top - 1];\n"
        "    }\n"
        "    int size() {\n"
        "        return this.top;\n"
        "    }\n"
        "    bool isEmpty() {\n"
        "        return this.top == 0;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Stack<int> s;\n"
        "    s.init();\n"
        "    s.push(111);\n"
        "    s.push(222);\n"
        "    s.push(333);\n"
        "    print(\"Peek=\"); print(s.peek()); print(\";\");\n"
        "    int p1 = s.pop();\n"
        "    int p2 = s.pop();\n"
        "    print(\"Pop=\"); print(p1); print(\",\"); print(p2); print(\";\");\n"
        "    print(\"Size=\"); print(s.size()); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Peek=333;") != std::string::npos);
    EXPECT_TRUE(out.find("Pop=333,222;") != std::string::npos);
    EXPECT_TRUE(out.find("Size=1;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, DynamicMemoryPrimitivesExecution) {
    std::string src =
        "int main() {\n"
        "    int buf[] = __builtin_alloc<int>(4);\n"
        "    buf[0] = 10;\n"
        "    buf[1] = 20;\n"
        "    buf[2] = 30;\n"
        "    buf[3] = 40;\n"
        "    int grown[] = __builtin_realloc<int>(buf, 8);\n"
        "    grown[4] = 50;\n"
        "    print(\"G0=\"); print(grown[0]); print(\";\");\n"
        "    print(\"G3=\"); print(grown[3]); print(\";\");\n"
        "    print(\"G4=\"); print(grown[4]); print(\";\");\n"
        "    print(\"Len=\"); print(len(grown)); print(\";\");\n"
        "    __builtin_free(grown);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("G0=10;") != std::string::npos);
    EXPECT_TRUE(out.find("G3=40;") != std::string::npos);
    EXPECT_TRUE(out.find("G4=50;") != std::string::npos);
    EXPECT_TRUE(out.find("Len=8;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, CustomStructIndexOperatorGetSet) {
    std::string src =
        "struct Box {\n"
        "    int items[8];\n"
        "    int get(int idx) {\n"
        "        return this.items[idx];\n"
        "    }\n"
        "    void set(int idx, int val) {\n"
        "        this.items[idx] = val;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Box b;\n"
        "    b[0] = 77;\n"
        "    b[3] = 99;\n"
        "    print(\"B0=\"); print(b[0]); print(\";\");\n"
        "    print(\"B3=\"); print(b[3]); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("B0=77;") != std::string::npos);
    EXPECT_TRUE(out.find("B3=99;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, DynamicGenericVectorPushPop) {
    std::string src =
        "struct DynamicVec<T> {\n"
        "    T buffer[];\n"
        "    int length;\n"
        "    int capacity;\n"
        "    void init() {\n"
        "        this.capacity = 2;\n"
        "        this.length = 0;\n"
        "        this.buffer = __builtin_alloc<T>(2);\n"
        "    }\n"
        "    void push(T val) {\n"
        "        if (this.length >= this.capacity) {\n"
        "            int newCap = this.capacity * 2;\n"
        "            this.buffer = __builtin_realloc<T>(this.buffer, newCap);\n"
        "            this.capacity = newCap;\n"
        "        }\n"
        "        this.buffer[this.length] = val;\n"
        "        this.length = this.length + 1;\n"
        "    }\n"
        "    T pop() {\n"
        "        this.length = this.length - 1;\n"
        "        return this.buffer[this.length];\n"
        "    }\n"
        "    T get(int idx) {\n"
        "        return this.buffer[idx];\n"
        "    }\n"
        "    void set(int idx, T val) {\n"
        "        this.buffer[idx] = val;\n"
        "    }\n"
        "    int size() {\n"
        "        return this.length;\n"
        "    }\n"
        "    void release() {\n"
        "        if (this.capacity > 0) {\n"
        "            __builtin_free(this.buffer);\n"
        "            this.capacity = 0;\n"
        "            this.length = 0;\n"
        "        }\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    DynamicVec<int> v;\n"
        "    v.init();\n"
        "    v.push(100);\n"
        "    v.push(200);\n"
        "    v.push(300);\n"
        "    v.push(400);\n"
        "    v.push(500);\n"
        "    print(\"Size=\"); print(v.size()); print(\";\");\n"
        "    print(\"V0=\"); print(v[0]); print(\";\");\n"
        "    print(\"V4=\"); print(v[4]); print(\";\");\n"
        "    v[2] = 999;\n"
        "    print(\"V2=\"); print(v[2]); print(\";\");\n"
        "    int p1 = v.pop();\n"
        "    print(\"Pop=\"); print(p1); print(\";\");\n"
        "    print(\"NewSize=\"); print(v.size()); print(\";\");\n"
        "    v.release();\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Size=5;") != std::string::npos);
    EXPECT_TRUE(out.find("V0=100;") != std::string::npos);
    EXPECT_TRUE(out.find("V4=500;") != std::string::npos);
    EXPECT_TRUE(out.find("V2=999;") != std::string::npos);
    EXPECT_TRUE(out.find("Pop=500;") != std::string::npos);
    EXPECT_TRUE(out.find("NewSize=4;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, DynamicMemoryCopyAndSet) {
    std::string src =
        "int main() {\n"
        "    int src[] = __builtin_alloc<int>(4);\n"
        "    src[0] = 11;\n"
        "    src[1] = 22;\n"
        "    src[2] = 33;\n"
        "    src[3] = 44;\n"
        "    int dst[] = __builtin_alloc<int>(6);\n"
        "    __builtin_memset<int>(dst, 0, 0, 6);\n"
        "    __builtin_memcpy<int>(dst, 1, src, 0, 4);\n"
        "    print(\"D0=\"); print(dst[0]); print(\";\");\n"
        "    print(\"D1=\"); print(dst[1]); print(\";\");\n"
        "    print(\"D4=\"); print(dst[4]); print(\";\");\n"
        "    print(\"D5=\"); print(dst[5]); print(\";\");\n"
        "    __builtin_free(src);\n"
        "    __builtin_free(dst);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("D0=0;") != std::string::npos);
    EXPECT_TRUE(out.find("D1=11;") != std::string::npos);
    EXPECT_TRUE(out.find("D4=44;") != std::string::npos);
    EXPECT_TRUE(out.find("D5=0;") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, BuiltinSqrtAndSqrtfExecution) {
    const std::string src =
        "int main() {\n"
        "    float s1 = sqrtf(16.0f);\n"
        "    double s2 = sqrt(25.0);\n"
        "    float s3 = __builtin_sqrtf(100.0f);\n"
        "    print(\"S1=\"); print((int)s1); print(\";\");\n"
        "    print(\"S2=\"); print((int)s2); print(\";\");\n"
        "    print(\"S3=\"); print((int)s3); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 0, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("S1=4;") != std::string::npos);
    EXPECT_TRUE(out.find("S2=5;") != std::string::npos);
    EXPECT_TRUE(out.find("S3=10;") != std::string::npos);
    return true;
}

TAMARIN_TEST(BCE, PassEliminationVerification) {
    SourceManager sm;
    auto fileId = sm.addSource("test_bce.tmn",
        "int test_sum(int arr[]) {\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < len(arr); i++) {\n"
        "        s += arr[i];\n"
        "    }\n"
        "    return s;\n"
        "}\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "test_bce");
    EXPECT_TRUE(mod != nullptr);

    // Count bounds checks before BCE
    size_t checksBefore = 0;
    for (auto& fn : mod->functions) {
        for (auto& blk : fn->blocks) {
            for (auto& inst : blk->instructions) {
                if (inst.opcode == ir::Opcode::BoundsCheck) checksBefore++;
            }
        }
    }
    EXPECT_TRUE(checksBefore > 0);

    // Run BCE pass
    ir::opt::BoundsCheckEliminationPass bce;
    for (auto& fn : mod->functions) {
        bce.runOnFunction(*fn, &sema.checker().context());
    }
    EXPECT_TRUE(bce.getEliminatedCount() > 0);

    // Count bounds checks after BCE
    size_t checksAfter = 0;
    for (auto& fn : mod->functions) {
        for (auto& blk : fn->blocks) {
            for (auto& inst : blk->instructions) {
                if (inst.opcode == ir::Opcode::BoundsCheck) checksAfter++;
            }
        }
    }
    EXPECT_EQ(checksAfter, 0);
    return true;
}

TAMARIN_TEST(BCE, CanonicalLoopArrayViewExecution) {
    const std::string src =
        "int sum_slice(int arr[]) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < len(arr); i++) {\n"
        "        total += arr[i];\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    int data[5] = { 10, 20, 30, 40, 50 };\n"
        "    int res = sum_slice(data);\n"
        "    print(\"SUM=\"); print(res); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("SUM=150;") != std::string::npos);
    return true;
}

TAMARIN_TEST(AliasAnalysis, DisjointnessProof) {
    SourceManager sm;
    auto fileId = sm.addSource("test_aa.tmn",
        "int process(int a[], int b[]) {\n"
        "    int local1[4] = {1, 2, 3, 4};\n"
        "    int local2[4] = {5, 6, 7, 8};\n"
        "    return a[0] + b[0] + local1[0] + local2[0];\n"
        "}\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "test_aa");
    EXPECT_TRUE(mod != nullptr);

    ir::opt::AliasAnalysis aa;
    size_t totalNoAlias = 0;
    for (auto& fn : mod->functions) {
        aa.runOnFunction(*fn, &sema.checker().context());
        totalNoAlias += aa.getNoAliasValues().size();
    }
    EXPECT_TRUE(totalNoAlias > 0);
    return true;
}

TAMARIN_TEST(SIMD, Float4BasicArithmeticExecution) {
    const std::string src =
        "int main() {\n"
        "    float4 a = float4(1.0f, 2.0f, 3.0f, 4.0f);\n"
        "    float4 b = float4(10.0f, 20.0f, 30.0f, 40.0f);\n"
        "    float4 c = a + b;\n"
        "    float4 d = c * 2.0f;\n"
        "    print(\"X=\"); print((int)d.x); print(\";\");\n"
        "    print(\"Y=\"); print((int)d.y); print(\";\");\n"
        "    print(\"Z=\"); print((int)d.z); print(\";\");\n"
        "    print(\"W=\"); print((int)d.w); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("X=22;") != std::string::npos);
    EXPECT_TRUE(out.find("Y=44;") != std::string::npos);
    EXPECT_TRUE(out.find("Z=66;") != std::string::npos);
    EXPECT_TRUE(out.find("W=88;") != std::string::npos);
    return true;
}

TAMARIN_TEST(SIMD, Float4ReduceAddAndDotProductExecution) {
    const std::string src =
        "int main() {\n"
        "    float4 v1 = float4(1.0f, 2.0f, 3.0f, 4.0f);\n"
        "    float4 v2 = float4(2.0f, 2.0f, 2.0f, 2.0f);\n"
        "    float sum = reduce_add(v1);\n"
        "    float dot = dot_product(v1, v2);\n"
        "    print(\"SUM=\"); print((int)sum); print(\";\");\n"
        "    print(\"DOT=\"); print((int)dot); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("SUM=10;") != std::string::npos);
    EXPECT_TRUE(out.find("DOT=20;") != std::string::npos);
    return true;
}

TAMARIN_TEST(SIMD, Int4ArithmeticAndSubscript) {
    const std::string src =
        "int main() {\n"
        "    int4 a = int4(10, 20, 30, 40);\n"
        "    int4 b = int4(5, 5, 5, 5);\n"
        "    int4 c = a - b;\n"
        "    int sum = reduce_add(c);\n"
        "    print(\"C0=\"); print(c[0]); print(\";\");\n"
        "    print(\"C3=\"); print(c[3]); print(\";\");\n"
        "    print(\"SUM=\"); print(sum); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("C0=5;") != std::string::npos);
    EXPECT_TRUE(out.find("C3=35;") != std::string::npos);
    EXPECT_TRUE(out.find("SUM=80;") != std::string::npos);
    return true;
}

TAMARIN_TEST(SIMD, Float8AVX2Execution) {
    const std::string src =
        "int main() {\n"
        "    float8 v = float8(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);\n"
        "    float8 doubled = v * 2.0f;\n"
        "    float sum = reduce_add(doubled);\n"
        "    print(\"F8_SUM=\"); print((int)sum); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("F8_SUM=72;") != std::string::npos);
    return true;
}

TAMARIN_TEST(SIMD, Int8AVX2Execution) {
    const std::string src =
        "int main() {\n"
        "    int8 v = int8(1, 2, 3, 4, 5, 6, 7, 8);\n"
        "    int sum = reduce_add(v);\n"
        "    print(\"I8_SUM=\"); print(sum); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("I8_SUM=36;") != std::string::npos);
    return true;
}

TAMARIN_TEST(SIMD, Int16AVX512Execution) {
    const std::string src =
        "int main() {\n"
        "    int16 v = int16(10);\n"
        "    int sum = reduce_add(v);\n"
        "    print(\"I16_SUM=\"); print(sum); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("I16_SUM=160;") != std::string::npos);
    return true;
}

TAMARIN_TEST(Parallel, ParallelForArrayFillExecution) {
    const std::string src =
        "int main() {\n"
        "    int data[1000] = {0};\n"
        "    parallel for (int i = 0; i < len(data); i++) {\n"
        "        data[i] = i * 2;\n"
        "    }\n"
        "    print(\"D0=\"); print(data[0]); print(\";\");\n"
        "    print(\"D500=\"); print(data[500]); print(\";\");\n"
        "    print(\"D999=\"); print(data[999]); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("D0=0;") != std::string::npos);
    EXPECT_TRUE(out.find("D500=1000;") != std::string::npos);
    EXPECT_TRUE(out.find("D999=1998;") != std::string::npos);
    return true;
}

TAMARIN_TEST(Parallel, ParallelSIMDVectorExecution) {
    const std::string src =
        "int main() {\n"
        "    float res[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};\n"
        "    parallel for (int i = 0; i < 8; i++) {\n"
        "        float4 v1 = float4((float)i);\n"
        "        float4 v2 = float4(2.0f);\n"
        "        res[i] = dot_product(v1, v2);\n"
        "    }\n"
        "    print(\"R0=\"); print((int)res[0]); print(\";\");\n"
        "    print(\"R3=\"); print((int)res[3]); print(\";\");\n"
        "    print(\"R7=\"); print((int)res[7]); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("R0=0;") != std::string::npos);
    EXPECT_TRUE(out.find("R3=24;") != std::string::npos);
    EXPECT_TRUE(out.find("R7=56;") != std::string::npos);
    return true;
}

TAMARIN_TEST(Parallel, TaskScopeExecution) {
    const std::string src =
        "int main() {\n"
        "    int results[3] = {0, 0, 0};\n"
        "    task_scope {\n"
        "        spawn {\n"
        "            results[0] = 100;\n"
        "        }\n"
        "        spawn {\n"
        "            results[1] = 200;\n"
        "        }\n"
        "        spawn {\n"
        "            results[2] = 300;\n"
        "        }\n"
        "    }\n"
        "    print(\"T0=\"); print(results[0]); print(\";\");\n"
        "    print(\"T1=\"); print(results[1]); print(\";\");\n"
        "    print(\"T2=\"); print(results[2]); print(\";\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("T0=100;") != std::string::npos);
    EXPECT_TRUE(out.find("T1=200;") != std::string::npos);
    EXPECT_TRUE(out.find("T2=300;") != std::string::npos);
    return true;
}

TAMARIN_TEST(Parallel, SemaRejectOuterScalarMutation) {
    SourceManager sm;
    auto fileId = sm.addSource("test_race.tmn",
        "int main() {\n"
        "    int shared_sum = 0;\n"
        "    parallel for (int i = 0; i < 100; i++) {\n"
        "        shared_sum += i;\n"
        "    }\n"
        "    return shared_sum;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(errs.str().find("data-race safety violation") != std::string::npos);
    return true;
}

TAMARIN_TEST(Parallel, SemaRejectSpawnOutsideTaskScope) {
    SourceManager sm;
    auto fileId = sm.addSource("test_orphan.tmn",
        "int main() {\n"
        "    spawn {\n"
        "        int x = 42;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(errs.str().find("structured concurrency safety violation") != std::string::npos);
    return true;
}
TAMARIN_TEST(Parallel, ParallelReductionSumExecution) {
    const std::string src =
        "int main() {\n"
        "    int total = 0;\n"
        "    parallel (reduction(+:total)) for (int i = 0; i < 1000; i++) {\n"
        "        total += i;\n"
        "    }\n"
        "    print(\"SUM=\"); print(total);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("SUM=499500") != std::string::npos);
    return true;
}

TAMARIN_TEST(Parallel, ParallelReductionMinMaxExecution) {
    const std::string src =
        "int main() {\n"
        "    int min_val = 999999;\n"
        "    int max_val = -999999;\n"
        "    parallel (reduction(min:min_val), reduction(max:max_val)) for (int i = 1; i <= 500; i++) {\n"
        "        if (i < min_val) { min_val = i; }\n"
        "        if (i > max_val) { max_val = i; }\n"
        "    }\n"
        "    print(\"MIN=\"); print(min_val); print(\";MAX=\"); print(max_val);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("MIN=1;MAX=500") != std::string::npos);
    return true;
}

TAMARIN_TEST(Parallel, ParallelReductionBitwiseExecution) {
    const std::string src =
        "int main() {\n"
        "    int flags = 0;\n"
        "    parallel (reduction(|:flags)) for (int i = 0; i < 8; i++) {\n"
        "        flags = flags | (1 << i);\n"
        "    }\n"
        "    print(\"FLAGS=\"); print(flags);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("FLAGS=255") != std::string::npos);
    return true;
}
TAMARIN_TEST(Parallel, ParallelReductionMulExecution) {
    const std::string src =
        "int main() {\n"
        "    int prod = 1;\n"
        "    parallel (reduction(*:prod)) for (int i = 1; i <= 7; i++) {\n"
        "        prod = prod * i;\n"
        "    }\n"
        "    print(\"PROD=\"); print(prod);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("PROD=5040") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SingleStatementControlFlowExecution) {
    const std::string src =
        "int max(int a, int b) {\n"
        "    if (a > b) return a;\n"
        "    else return b;\n"
        "}\n"
        "int classify(int x) {\n"
        "    if (x < 0) return -1;\n"
        "    else if (x == 0) return 0;\n"
        "    else return 1;\n"
        "}\n"
        "int main() {\n"
        "    int m1 = max(42, 17);\n"
        "    int m2 = max(5, 99);\n"
        "    int c1 = classify(-10);\n"
        "    int c2 = classify(0);\n"
        "    int c3 = classify(25);\n"
        "    int sum = 0;\n"
        "    for (int i = 0; i < 10; i++) sum += i;\n"
        "    int count = 5;\n"
        "    while (count > 0) count--;\n"
        "    print(\"M1=\"); print(m1);\n"
        "    print(\";M2=\"); print(m2);\n"
        "    print(\";C1=\"); print(c1);\n"
        "    print(\";C2=\"); print(c2);\n"
        "    print(\";C3=\"); print(c3);\n"
        "    print(\";SUM=\"); print(sum);\n"
        "    print(\";COUNT=\"); print(count);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("M1=42;M2=99;C1=-1;C2=0;C3=1;SUM=45;COUNT=0") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, VectorizeLoopMetadataExecution) {
    const std::string src =
        "int main() {\n"
        "    int arr[128];\n"
        "    @vectorize(width: 8, interleave: 4)\n"
        "    for (int i = 0; i < 128; i++) {\n"
        "        arr[i] = i * 3;\n"
        "    }\n"
        "    int sum = 0;\n"
        "    @vectorize(width: 8)\n"
        "    for (int i = 0; i < 128; i++) {\n"
        "        sum += arr[i];\n"
        "    }\n"
        "    print(\"SUM=\"); print(sum);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("SUM=24384") != std::string::npos);
    return true;
}

TAMARIN_TEST(CodeGen, SelectStatementExecution) {
    const std::string src =
        "int main() {\n"
        "    int branch1 = 0;\n"
        "    int branch2 = 0;\n"
        "    int defaultBranch = 0;\n"
        "    bool condF = false;\n"
        "    bool condT = true;\n"
        "    select {\n"
        "        case condF:\n"
        "            branch1 = 100;\n"
        "        case condT:\n"
        "            branch2 = 200;\n"
        "        default:\n"
        "            defaultBranch = 300;\n"
        "    }\n"
        "    int defHit = 0;\n"
        "    select {\n"
        "        case condF:\n"
        "            defHit = 1;\n"
        "        default:\n"
        "            defHit = 42;\n"
        "    }\n"
        "    print(\"B1=\"); print(branch1);\n"
        "    print(\";B2=\"); print(branch2);\n"
        "    print(\";DEF=\"); print(defaultBranch);\n"
        "    print(\";DEFHIT=\"); print(defHit);\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("B1=0;B2=200;DEF=0;DEFHIT=42") != std::string::npos);
    return true;
}

TAMARIN_TEST(TargetTriple, ParsingAndAccessors) {
    // Windows x86_64
    auto win = target::TargetTriple::parse("x86_64-pc-windows-msvc");
    EXPECT_TRUE(win.arch() == target::Arch::X86_64);
    EXPECT_TRUE(win.vendor() == target::Vendor::PC);
    EXPECT_TRUE(win.os() == target::OS::Windows);
    EXPECT_TRUE(win.env() == target::Environment::MSVC);
    EXPECT_TRUE(win.isWindows());
    EXPECT_FALSE(win.isLinux());
    EXPECT_FALSE(win.isDarwin());
    EXPECT_TRUE(win.isX86_64());
    EXPECT_FALSE(win.isAArch64());
    EXPECT_EQ(win.executableExtension(), ".exe");
    EXPECT_EQ(win.objectExtension(), ".obj");
    EXPECT_EQ(win.staticLibraryExtension(), ".lib");
    EXPECT_EQ(win.sharedLibraryExtension(), ".dll");
    EXPECT_TRUE(win.llvmDataLayout().find("e-m:w-p270:32:32") != std::string::npos);

    // Linux x86_64
    auto linux64 = target::TargetTriple::parse("x86_64-unknown-linux-gnu");
    EXPECT_TRUE(linux64.arch() == target::Arch::X86_64);
    EXPECT_TRUE(linux64.isLinux());
    EXPECT_FALSE(linux64.isWindows());
    EXPECT_EQ(linux64.executableExtension(), "");
    EXPECT_EQ(linux64.objectExtension(), ".o");
    EXPECT_EQ(linux64.staticLibraryExtension(), ".a");
    EXPECT_EQ(linux64.sharedLibraryExtension(), ".so");
    EXPECT_TRUE(linux64.llvmDataLayout().find("e-m:e-p270:32:32") != std::string::npos);

    // Linux AArch64
    auto aarch64 = target::TargetTriple::parse("aarch64-unknown-linux-gnu");
    EXPECT_TRUE(aarch64.arch() == target::Arch::AArch64);
    EXPECT_TRUE(aarch64.isLinux());
    EXPECT_TRUE(aarch64.isAArch64());
    EXPECT_FALSE(aarch64.isX86_64());
    EXPECT_EQ(aarch64.executableExtension(), "");
    EXPECT_EQ(aarch64.objectExtension(), ".o");
    EXPECT_EQ(aarch64.staticLibraryExtension(), ".a");
    EXPECT_EQ(aarch64.sharedLibraryExtension(), ".so");
    EXPECT_TRUE(aarch64.llvmDataLayout().find("e-m:e-i8:8:32-i16:16:32-i64:64-i128:128") != std::string::npos);

    // macOS Apple Silicon (AArch64 Darwin)
    auto macArm = target::TargetTriple::parse("aarch64-apple-darwin");
    EXPECT_TRUE(macArm.arch() == target::Arch::AArch64);
    EXPECT_TRUE(macArm.isDarwin());
    EXPECT_EQ(macArm.executableExtension(), "");
    EXPECT_EQ(macArm.objectExtension(), ".o");
    EXPECT_EQ(macArm.staticLibraryExtension(), ".a");
    EXPECT_EQ(macArm.sharedLibraryExtension(), ".dylib");
    EXPECT_TRUE(macArm.llvmDataLayout().find("e-m:o-i64:64-i128:128") != std::string::npos);

    // Host
    auto host = target::TargetTriple::host();
    EXPECT_FALSE(host.str().empty());
#ifdef _WIN32
    EXPECT_TRUE(host.isWindows());
#endif
    return true;
}

TAMARIN_TEST(CHeaderGen, ExportedTypesAndFunctions) {
    SourceManager sm;
    auto fileId = sm.addSource("math_module.tmn",
        "@c_layout\n"
        "struct Vector3 {\n"
        "    float x;\n"
        "    float y;\n"
        "    float z;\n"
        "};\n\n"
        "@export\n"
        "int dot(Vector3 a, Vector3 b) {\n"
        "    return 0;\n"
        "}\n\n"
        "@export\n"
        "int parse_name(*u8 name) {\n"
        "    return 42;\n"
        "}\n\n"
        "int private_internal(int x) {\n"
        "    return x + 1;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    std::ostringstream headerOut;
    backend::CHeaderGenerator headerGen(diag);
    bool genOk = headerGen.generate(prog, sema.checker(), "math_module.h", headerOut);
    EXPECT_TRUE(genOk);

    auto h = headerOut.str();
    EXPECT_TRUE(h.find("#ifndef MATH_MODULE_H") != std::string::npos);
    EXPECT_TRUE(h.find("#define MATH_MODULE_H") != std::string::npos);
    EXPECT_TRUE(h.find("#include <stdint.h>") != std::string::npos);
    EXPECT_TRUE(h.find("typedef struct Vector3 {") != std::string::npos);
    EXPECT_TRUE(h.find("float x;") != std::string::npos);
    EXPECT_TRUE(h.find("float y;") != std::string::npos);
    EXPECT_TRUE(h.find("float z;") != std::string::npos);
    EXPECT_TRUE(h.find("int32_t dot(Vector3 a, Vector3 b);") != std::string::npos);
    EXPECT_TRUE(h.find("parse_name") != std::string::npos);
    EXPECT_TRUE(h.find("private_internal") == std::string::npos);
    EXPECT_TRUE(h.find("extern \"C\" {") != std::string::npos);
    EXPECT_TRUE(h.find("#endif /* MATH_MODULE_H */") != std::string::npos);

    return true;
}

TAMARIN_TEST(FFI, CallCStdlibAbs) {
    const std::string src =
        "extern \"C\" {\n"
        "    int abs(int n);\n"
        "}\n"
        "int main() {\n"
        "    int a = abs(-42);\n"
        "    int b = abs(100);\n"
        "    if (a == 42 && b == 100) {\n"
        "        print(\"C_ABS_OK\");\n"
        "        return 0;\n"
        "    }\n"
        "    return 1;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("C_ABS_OK") != std::string::npos);
    return true;
}

TAMARIN_TEST(FFI, CallCStdlibPuts) {
    const std::string src =
        "extern \"C\" {\n"
        "    int puts(*u8 str);\n"
        "}\n"
        "int main() {\n"
        "    puts(\"Tamarin C-FFI Success!\");\n"
        "    return 0;\n"
        "}\n";
    int exitCode = -1;
    std::string out;
    EXPECT_TRUE(compileAndRunProgram(src, 2, exitCode, out));
    EXPECT_EQ(exitCode, 0);
    EXPECT_TRUE(out.find("Tamarin C-FFI Success!") != std::string::npos);
    return true;
}

TAMARIN_TEST(BinaryFormats, EmitObjectAndStaticLibrary) {
    SourceManager sm;
    auto fileId = sm.addSource("export_test.tmn",
        "@export\n"
        "int exported_add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main() {\n"
        "    return exported_add(10, 20);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "export_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int binRunId = 0;
    auto llPath = tempDir / ("tamarin_bin_test_" + std::to_string(++binRunId) + ".ll");
    auto objPath = tempDir / ("tamarin_bin_test_" + std::to_string(binRunId) + codegen.getTargetTriple().objectExtension());
    auto libPath = tempDir / ("tamarin_bin_test_" + std::to_string(binRunId) + codegen.getTargetTriple().staticLibraryExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToObject(llPath, objPath, 2));
    EXPECT_TRUE(std::filesystem::exists(objPath));
    EXPECT_TRUE(std::filesystem::file_size(objPath) > 0);

    EXPECT_TRUE(codegen.compileToStaticLibrary(llPath, libPath, 2));
    EXPECT_TRUE(std::filesystem::exists(libPath));
    EXPECT_TRUE(std::filesystem::file_size(libPath) > 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(objPath, ec);
    std::filesystem::remove(libPath, ec);
    return true;
}

TAMARIN_TEST(BinaryFormats, EmitSharedLibrary) {
    SourceManager sm;
    auto fileId = sm.addSource("export_dll.tmn",
        "@export\n"
        "int shared_mul(int a, int b) {\n"
        "    return a * b;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "export_dll");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int dllRunId = 0;
    auto llPath = tempDir / ("tamarin_dll_test_" + std::to_string(++dllRunId) + ".ll");
    auto dllPath = tempDir / ("tamarin_dll_test_" + std::to_string(dllRunId) + codegen.getTargetTriple().sharedLibraryExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToSharedLibrary(llPath, dllPath, 2));
    EXPECT_TRUE(std::filesystem::exists(dllPath));
    EXPECT_TRUE(std::filesystem::file_size(dllPath) > 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(dllPath, ec);
    return true;
}

TAMARIN_TEST(CrossCompilation, TargetAArch64LinuxObject) {
    SourceManager sm;
    auto fileId = sm.addSource("cross_aarch64.tmn",
        "@export\n"
        "int aarch64_add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "cross_aarch64");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    codegen.setTargetTriple("aarch64-unknown-linux-gnu");

    auto tempDir = std::filesystem::temp_directory_path();
    static int crossRunId = 0;
    auto llPath = tempDir / ("tamarin_cross_aarch64_" + std::to_string(++crossRunId) + ".ll");
    auto oPath = tempDir / ("tamarin_cross_aarch64_" + std::to_string(crossRunId) + ".o");

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));

    // Verify LLVM IR contains AArch64 triple & data layout
    std::ifstream inLL(llPath);
    std::string llContent((std::istreambuf_iterator<char>(inLL)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(llContent.find("target triple = \"aarch64-unknown-linux-gnu\"") != std::string::npos);
    EXPECT_TRUE(llContent.find("target datalayout = \"e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128\"") != std::string::npos);

    // Cross-compile to ELF object file via clang
    EXPECT_TRUE(codegen.compileToObject(llPath, oPath, 2));
    EXPECT_TRUE(std::filesystem::exists(oPath));
    EXPECT_TRUE(std::filesystem::file_size(oPath) > 0);

    // Read header magic to verify ELF format (0x7F 'E' 'L' 'F')
    std::ifstream inObj(oPath, std::ios::binary);
    char magic[4] = {0};
    inObj.read(magic, 4);
    EXPECT_EQ(static_cast<unsigned char>(magic[0]), 0x7F);
    EXPECT_EQ(magic[1], 'E');
    EXPECT_EQ(magic[2], 'L');
    EXPECT_EQ(magic[3], 'F');

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(oPath, ec);
    return true;
}

TAMARIN_TEST(BinaryFormats, StaticAndDynamicExecutableEmission) {
    SourceManager sm;
    auto fileId = sm.addSource("linkage_test.tmn",
        "int main() {\n"
        "    print(\"LINK_OK\");\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "linkage_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int crtRunId = 0;
    auto llPath = tempDir / ("tamarin_crt_test_" + std::to_string(++crtRunId) + ".ll");
    auto exeDyn = tempDir / ("tamarin_crt_dyn_" + std::to_string(crtRunId) + codegen.getTargetTriple().executableExtension());
    auto exeStat = tempDir / ("tamarin_crt_stat_" + std::to_string(crtRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));

    // Dynamic CRT build
    codegen.setCrtLinkage(backend::CrtLinkage::Dynamic);
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exeDyn, 2));
    EXPECT_TRUE(std::filesystem::exists(exeDyn));
    auto dynSize = std::filesystem::file_size(exeDyn);
    EXPECT_TRUE(dynSize > 0);

    // Static CRT build
    codegen.setCrtLinkage(backend::CrtLinkage::Static);
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exeStat, 2));
    EXPECT_TRUE(std::filesystem::exists(exeStat));
    auto statSize = std::filesystem::file_size(exeStat);
    EXPECT_TRUE(statSize > 0);

    // Static CRT bundles runtime machinery so it should be larger than dynamic CRT
    EXPECT_TRUE(statSize > dynSize);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exeDyn, ec);
    std::filesystem::remove(exeStat, ec);
    return true;
}

TAMARIN_TEST(CodeGen, TernaryConditionalOperatorExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("ternary_test.tmn",
        "int sideEffect(int& count) {\n"
        "    count = count + 1;\n"
        "    return 100;\n"
        "}\n"
        "int max(int a, int b) {\n"
        "    return a > b ? a : b;\n"
        "}\n"
        "int min(int a, int b) {\n"
        "    return a < b ? a : b;\n"
        "}\n"
        "int sign(int x) {\n"
        "    return x > 0 ? 1 : (x < 0 ? -1 : 0);\n"
        "}\n"
        "int testShortCircuit(bool pickTrue, int& count) {\n"
        "    return pickTrue ? 42 : sideEffect(count);\n"
        "}\n"
        "int main() {\n"
        "    int m1 = max(10, 20);\n"
        "    int m2 = min(10, 20);\n"
        "    int s1 = sign(50);\n"
        "    int s2 = sign(-50);\n"
        "    int s3 = sign(0);\n"
        "    int count = 0;\n"
        "    int res = testShortCircuit(true, count);\n"
        "    if (m1 != 20 || m2 != 10) return 1;\n"
        "    if (s1 != 1 || s2 != -1 || s3 != 0) return 2;\n"
        "    if (res != 42 || count != 0) return 3;\n"
        "    int res2 = testShortCircuit(false, count);\n"
        "    if (res2 != 100 || count != 1) return 4;\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "ternary_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int ternRunId = 0;
    auto llPath = tempDir / ("tamarin_tern_" + std::to_string(++ternRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_tern_" + std::to_string(ternRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    if (exitCode != 0) {
        std::cout << "\n>>> OperatorOverloadingExecution FAILED with exit code: " << exitCode << " <<<\n\n";
    }
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, OperatorOverloadingExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("op_overload_test.tmn",
        "struct Vec2 {\n"
        "    int x;\n"
        "    int y;\n"
        "\n"
        "    Vec2 operator+(Vec2 other) {\n"
        "        return (Vec2){ .x = this.x + other.x, .y = this.y + other.y };\n"
        "    }\n"
        "    Vec2 operator-(Vec2 other) {\n"
        "        return (Vec2){ .x = this.x - other.x, .y = this.y - other.y };\n"
        "    }\n"
        "    Vec2 operator*(int s) {\n"
        "        return (Vec2){ .x = this.x * s, .y = this.y * s };\n"
        "    }\n"
        "    bool operator==(Vec2 other) {\n"
        "        return this.x == other.x && this.y == other.y;\n"
        "    }\n"
        "    bool operator!=(Vec2 other) {\n"
        "        return !(*this == other);\n"
        "    }\n"
        "    Vec2 operator-() {\n"
        "        return (Vec2){ .x = -this.x, .y = -this.y };\n"
        "    }\n"
        "    int operator[](int idx) {\n"
        "        if (idx == 0) return this.x;\n"
        "        return this.y;\n"
        "    }\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    Vec2 a;\n"
        "    a.x = 10;\n"
        "    a.y = 20;\n"
        "    Vec2 b;\n"
        "    b.x = 3;\n"
        "    b.y = 4;\n"
        "\n"
        "    Vec2 sum = a + b;\n"
        "    if (sum.x != 13 || sum.y != 24) return 1;\n"
        "\n"
        "    Vec2 diff = a - b;\n"
        "    if (diff.x != 7 || diff.y != 16) return 2;\n"
        "\n"
        "    Vec2 scaled = b * 5;\n"
        "    if (scaled.x != 15 || scaled.y != 20) return 3;\n"
        "\n"
        "    Vec2 neg = -b;\n"
        "    if (neg.x != -3 || neg.y != -4) return 4;\n"
        "\n"
        "    if (!(a == a)) return 5;\n"
        "    if (a == b) return 6;\n"
        "    if (!(a != b)) return 7;\n"
        "\n"
        "    if (a[0] != 10 || a[1] != 20) return 8;\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "op_overload_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int opRunId = 0;
    auto llPath = tempDir / ("tamarin_op_" + std::to_string(++opRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_op_" + std::to_string(opRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    if (exitCode != 0) {
        std::cout << "\n>>> OperatorOverloadingExecution FAILED with exit code: " << exitCode << " <<<\n\n";
    }
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, TypeInferenceAutoExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("auto_test.tmn",
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    auto a = 10;\n"
        "    auto b = 20;\n"
        "    auto sum = a + b;\n"
        "    if (sum != 30) return 1;\n"
        "\n"
        "    const auto factor = 3;\n"
        "    auto prod = sum * factor;\n"
        "    if (prod != 90) return 2;\n"
        "\n"
        "    auto& r = a;\n"
        "    r += 100;\n"
        "    if (a != 110) return 3;\n"
        "\n"
        "    const auto& cr = a;\n"
        "    if (cr != 110) return 4;\n"
        "\n"
        "    auto pt = (Point){ .x = 5, .y = 12 };\n"
        "    if (pt.x != 5 || pt.y != 12) return 5;\n"
        "\n"
        "    auto& rpt = pt;\n"
        "    rpt.x = 42;\n"
        "    if (pt.x != 42) return 6;\n"
        "\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "auto_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int autoRunId = 0;
    auto llPath = tempDir / ("tamarin_auto_" + std::to_string(++autoRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_auto_" + std::to_string(autoRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, DefaultParameterArgumentsExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("default_args_test.tmn",
        "int compute(int a, int b = 10, int c = 100) {\n"
        "    return a + b + c;\n"
        "}\n"
        "\n"
        "struct Service {\n"
        "    int base;\n"
        "    int request(int reqId, int retry = 3) {\n"
        "        return this.base + reqId * 10 + retry;\n"
        "    }\n"
        "};\n"
        "\n"
        "int main() {\n"
        "    int r1 = compute(1);\n"
        "    if (r1 != 111) return 1;\n"
        "\n"
        "    int r2 = compute(1, 20);\n"
        "    if (r2 != 121) return 2;\n"
        "\n"
        "    int r3 = compute(1, 20, 300);\n"
        "    if (r3 != 321) return 3;\n"
        "\n"
        "    Service s;\n"
        "    s.base = 1000;\n"
        "\n"
        "    int m1 = s.request(5);\n"
        "    if (m1 != 1053) return 4;\n"
        "\n"
        "    int m2 = s.request(5, 7);\n"
        "    if (m2 != 1057) return 5;\n"
        "\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "default_args_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int defRunId = 0;
    auto llPath = tempDir / ("tamarin_def_" + std::to_string(++defRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_def_" + std::to_string(defRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    if (exitCode != 0) {
        std::cout << "\n>>> DefaultParameterArgumentsExecution FAILED with exit code: " << exitCode << " <<<\n\n";
    }
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, TupleAndDestructuringExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("tuple_test.tmn",
        "(int, int) makePair(int x, int y) {\n"
        "    return (x, y);\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    auto pair = (10, 20);\n"
        "    int p0 = pair.0;\n"
        "    int p1 = pair.1;\n"
        "    if (p0 != 10) return 1;\n"
        "    if (p1 != 20) return 2;\n"
        "\n"
        "    auto retPair = makePair(100, 200);\n"
        "    if (retPair.0 != 100) return 3;\n"
        "    if (retPair.1 != 200) return 4;\n"
        "\n"
        "    auto (a, b) = retPair;\n"
        "    if (a != 100) return 5;\n"
        "    if (b != 200) return 6;\n"
        "\n"
        "    auto [c, d] = (300, 400);\n"
        "    if (c != 300) return 7;\n"
        "    if (d != 400) return 8;\n"
        "\n"
        "    auto triple = (42, 1000, 99);\n"
        "    auto (t0, t1, t2) = triple;\n"
        "    if (t0 != 42) return 9;\n"
        "    if (t1 != 1000) return 10;\n"
        "    if (t2 != 99) return 11;\n"
        "\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "tuple_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int tupleRunId = 0;
    auto llPath = tempDir / ("tamarin_tuple_" + std::to_string(++tupleRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_tuple_" + std::to_string(tupleRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    if (exitCode != 0) {
        std::cout << "\n>>> TupleAndDestructuringExecution FAILED with exit code: " << exitCode << " <<<\n\n";
    }
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, LambdaExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("lambda_test.tm",
        "int square(int x) {\n"
        "    return x * x;\n"
        "}\n"
        "\n"
        "int applyOp(fn(int, int) -> int op, int x, int y) {\n"
        "    return op(x, y);\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    // Lambda with auto inference\n"
        "    auto add = (int a, int b) => a + b;\n"
        "    int sum = add(15, 25);\n"
        "    if (sum != 40) return 1;\n"
        "\n"
        "    // Explicit function pointer variable\n"
        "    fn(int, int) -> int mul = (int a, int b) => a * b;\n"
        "    int prod = mul(6, 7);\n"
        "    if (prod != 42) return 2;\n"
        "\n"
        "    // Passing lambda to higher-order function\n"
        "    int resAdd = applyOp(add, 100, 20);\n"
        "    if (resAdd != 120) return 3;\n"
        "\n"
        "    int resSub = applyOp((int a, int b) => a - b, 50, 8);\n"
        "    if (resSub != 42) return 4;\n"
        "\n"
        "    // Lambda with block body\n"
        "    auto clamp = (int v, int lo, int hi) => {\n"
        "        if (v < lo) return lo;\n"
        "        if (v > hi) return hi;\n"
        "        return v;\n"
        "    };\n"
        "    if (clamp(5, 10, 20) != 10) return 5;\n"
        "    if (clamp(25, 10, 20) != 20) return 6;\n"
        "    if (clamp(15, 10, 20) != 15) return 7;\n"
        "\n"
        "    // Named function as first-class function pointer\n"
        "    fn(int) -> int f = square;\n"
        "    if (f(9) != 81) return 8;\n"
        "\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "lambda_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int lambdaRunId = 0;
    auto llPath = tempDir / ("tamarin_lambda_" + std::to_string(++lambdaRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_lambda_" + std::to_string(lambdaRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    if (exitCode != 0) {
        std::cout << "\n>>> LambdaExecution FAILED with exit code: " << exitCode << " <<<\n\n";
    }
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, RangeSlicingExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("range_test.tm",
        "int sumSlice(int[] slice) {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < len(slice); i++) {\n"
        "        total += slice[i];\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    int arr[6] = {10, 20, 30, 40, 50, 60};\n"
        "\n"
        "    // Full subslice arr[1..4] -> [20, 30, 40] (len = 3)\n"
        "    int[] s1 = arr[1..4];\n"
        "    if (len(s1) != 3) return 1;\n"
        "    if (s1[0] != 20) return 2;\n"
        "    if (s1[1] != 30) return 3;\n"
        "    if (s1[2] != 40) return 4;\n"
        "    if (sumSlice(s1) != 90) return 5;\n"
        "\n"
        "    // Open-ended slice arr[3..] -> [40, 50, 60] (len = 3)\n"
        "    int[] s2 = arr[3..];\n"
        "    if (len(s2) != 3) return 6;\n"
        "    if (s2[0] != 40) return 7;\n"
        "    if (s2[2] != 60) return 8;\n"
        "\n"
        "    // Prefix slice arr[..2] -> [10, 20] (len = 2)\n"
        "    int[] s3 = arr[..2];\n"
        "    if (len(s3) != 2) return 9;\n"
        "    if (s3[0] != 10) return 10;\n"
        "    if (s3[1] != 20) return 11;\n"
        "\n"
        "    // Sub-slice of view s1[1..3] -> [30, 40]\n"
        "    int[] s4 = s1[1..3];\n"
        "    if (len(s4) != 2) return 12;\n"
        "    if (s4[0] != 30) return 13;\n"
        "    if (s4[1] != 40) return 14;\n"
        "\n"
        "    // Mutation through subslice\n"
        "    s4[0] = 99;\n"
        "    if (arr[2] != 99) return 15;\n"
        "    if (s1[1] != 99) return 16;\n"
        "\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERRORS:\n" << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "range_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int rangeRunId = 0;
    auto llPath = tempDir / ("tamarin_range_" + std::to_string(++rangeRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_range_" + std::to_string(rangeRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    if (exitCode != 0) {
        std::cout << "\n>>> RangeSlicingExecution FAILED with exit code: " << exitCode << " <<<\n\n";
    }
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, AsyncFunctionExecution) {
    SourceManager sm;
    auto fileId = sm.addSource("async_test.tm",
        "async int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "async int square(int x) {\n"
        "    return x * x;\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    Task<int> t1 = add(20, 22);\n"
        "    int res1 = await t1;\n"
        "    if (res1 != 42) return 1;\n"
        "\n"
        "    Task<int> t2 = square(10);\n"
        "    Task<int> t3 = square(20);\n"
        "    int r2 = await t2;\n"
        "    int r3 = await t3;\n"
        "    if (r2 + r3 != 500) return 2;\n"
        "\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "async_exec_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int asyncRunId = 0;
    auto llPath = tempDir / ("tamarin_async_" + std::to_string(++asyncRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_async_" + std::to_string(asyncRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}

TAMARIN_TEST(CodeGen, AsyncRecursiveWorkStealing) {
    SourceManager sm;
    auto fileId = sm.addSource("async_rec_test.tm",
        "async int fib(int n) {\n"
        "    if (n <= 1) return n;\n"
        "    Task<int> a = fib(n - 1);\n"
        "    Task<int> b = fib(n - 2);\n"
        "    int ra = await a;\n"
        "    int rb = await b;\n"
        "    return ra + rb;\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    int ans = await fib(10);\n"
        "    if (ans != 55) return 1;\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    EXPECT_FALSE(diag.hasErrors());

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildModule(prog, "async_rec_test");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto tempDir = std::filesystem::temp_directory_path();
    static int asyncRecRunId = 0;
    auto llPath = tempDir / ("tamarin_async_rec_" + std::to_string(++asyncRecRunId) + ".ll");
    auto exePath = tempDir / ("tamarin_async_rec_" + std::to_string(asyncRecRunId) + codegen.getTargetTriple().executableExtension());

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));
    EXPECT_TRUE(std::filesystem::exists(exePath));

    int exitCode = std::system(exePath.string().c_str());
    EXPECT_EQ(exitCode, 0);

    std::error_code ec;
    std::filesystem::remove(llPath, ec);
    std::filesystem::remove(exePath, ec);
    return true;
}
#endif




