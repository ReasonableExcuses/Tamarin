// ============================================================================
// sema_test.cpp - Unit tests for Tamarin semantic analysis
// ============================================================================

#include "test_harness.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"
#include "tamarin/sema/sema.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <sstream>

using namespace tamarin;

TAMARIN_TEST(Sema, ValidProgramPasses) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x = 42;\n"
        "    return x;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, UndeclaredVariableFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    return y;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, ReturnTypeMismatchFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "void foo() {\n"
        "    return 42;\n"
        "}\n"
        "int main() {\n"
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
    EXPECT_TRUE(diag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, MissingMainFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int foo() {\n"
        "    return 42;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, FullSumProgramPasses) {
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
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, AssignToConstFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    const int x = 42;\n"
        "    x = 100;\n"
        "    return x;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RedefinitionInSameScopeFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int a = 1;\n"
        "    int a = 2;\n"
        "    return a;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, FunctionArgCountMismatchFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int add(int a, int b) { return a + b; }\n"
        "int main() {\n"
        "    return add(1);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, FixedArrayOutOfBoundsFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int arr[3] = {1, 2, 3};\n"
        "    return arr[10];\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, NonIntegerIndexFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int arr[3] = {1, 2, 3};\n"
        "    return arr[true];\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, NonVoidFunctionMissingReturnFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int missing_return(int x) {\n"
        "    int y = x + 1;\n"
        "}\n"
        "int main() {\n"
        "    return missing_return(5);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("control reaches end of non-void function") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, NonVoidFunctionOneBranchMissingReturnFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int branch_missing(int x) {\n"
        "    if (x > 0) {\n"
        "        return 1;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return branch_missing(5);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("control reaches end of non-void function") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, NonVoidFunctionBothBranchesReturnPasses) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int both_return(int x) {\n"
        "    if (x > 0) {\n"
        "        return 1;\n"
        "    } else {\n"
        "        return 0;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return both_return(5);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}


TAMARIN_TEST(Sema, VoidFunctionWithoutReturnPasses) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "void do_work() {\n"
        "    int a = 10;\n"
        "    int b = 20;\n"
        "}\n"
        "int main() {\n"
        "    do_work();\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}


TAMARIN_TEST(Sema, VoidFunctionWithBareReturnPasses) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "void do_nothing(int x) {\n"
        "    if (x > 0) {\n"
        "        return;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    do_nothing(5);\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}


TAMARIN_TEST(Sema, NestedEarlyReturnsPasses) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int classify(int x, int y) {\n"
        "    if (x > 0) {\n"
        "        if (y > 0) {\n"
        "            return 1;\n"
        "        } else {\n"
        "            return 2;\n"
        "        }\n"
        "    } else {\n"
        "        return 3;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return classify(1, 2);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}


// ── v0.2.1 Hotfix Regression Tests ──────────────────────────────────────────

TAMARIN_TEST(Sema, RejectVariableLengthArray) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int n = 5;\n"
        "    int vla[n];\n"
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
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("variable-length arrays are not supported") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectNegativeArraySize) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int bad[-5];\n"
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
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("array size cannot be negative") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectUnsizedArrayWithoutInitializer) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int arr[];\n"
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
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("unsized array declaration must have an initializer") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectExcessArrayInitializerElements) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int arr[2] = {1, 2, 3, 4};\n"
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
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("excess elements in array initializer") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectUninitializedScalarRead) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x;\n"
        "    return x;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("used before being initialized") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectConditionallyInitializedRead) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x;\n"
        "    int cond = 1;\n"
        "    if (cond) {\n"
        "        x = 42;\n"
        "    }\n"
        "    return x;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("used before being initialized") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, AcceptDefiniteInitializationBothBranches) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x;\n"
        "    int cond = 1;\n"
        "    if (cond) {\n"
        "        x = 42;\n"
        "    } else {\n"
        "        x = 100;\n"
        "    }\n"
        "    return x;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}


TAMARIN_TEST(Sema, RejectCompoundAssignUninitialized) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x;\n"
        "    x += 5;\n"
        "    return x;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("used before being initialized") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectBreakOutsideLoop) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    break;\n"
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
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("'break' statement not within a loop") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectContinueOutsideLoop) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    continue;\n"
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
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("'continue' statement not within a loop") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectSwitchUnsupported) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int x = 1;\n"
        "    switch (x) {\n"
        "        case 1: return 10;\n"
        "        default: return 0;\n"
        "    }\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("'switch' statements are not yet supported") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectInt128Unsupported) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    i128 huge_val = 0;\n"
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
    EXPECT_TRUE(diag.hasErrors());
    std::string msg = errs.str();
    EXPECT_TRUE(msg.find("128-bit integers are not yet supported") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, StructFieldAccess) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Vec2 {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "int main() {\n"
        "    Vec2 v = { .x = 1.0f, .y = 2.0f };\n"
        "    v.x = 10.0f;\n"
        "    v.y += 5.0f;\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}


TAMARIN_TEST(Sema, StructAssignToConstFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Vec2 {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "int main() {\n"
        "    const Vec2 v = { .x = 1.0f, .y = 2.0f };\n"
        "    v.x = 10.0f;\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot assign to field 'x' of const struct 'v'") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, StructRecursiveDefinitionFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Node {\n"
        "    int val;\n"
        "    Node next;\n"
        "};\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("recursive struct definition") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, StructUndeclaredFieldFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Vec2 {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "int main() {\n"
        "    Vec2 v = { .x = 1.0f, .y = 2.0f };\n"
        "    v.z = 3.0f;\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("no member named 'z'") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, StructABILayoutAndPadding) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Aligned {\n"
        "    i8 a;\n"
        "    int b;\n"
        "    i8 c;\n"
        "};\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);

    auto structTy = sema.context().findStruct("Aligned");
    EXPECT_TRUE(structTy != nullptr);
    EXPECT_EQ(structTy->structFields.size(), 3);
    EXPECT_EQ(structTy->structFields[0].offset, 0); // a: i8 at 0
    EXPECT_EQ(structTy->structFields[1].offset, 4); // b: int aligned to 4 (padding 3 bytes)
    EXPECT_EQ(structTy->structFields[2].offset, 8); // c: i8 at 8
    EXPECT_EQ(structTy->structAlignment, 4);        // max align = 4
    EXPECT_EQ(structTy->structSize, 12);            // size padded to 12

    return true;
}

TAMARIN_TEST(Sema, ClassPrivateAccessFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Player {\n"
        "private:\n"
        "    int health;\n"
        "public:\n"
        "    int getHealth() const { return health; }\n"
        "};\n"
        "int main() {\n"
        "    Player p;\n"
        "    p.health = 50;\n"
        "    return p.getHealth();\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot access private member 'health'") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, ClassProtectedAccess) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Base {\n"
        "protected:\n"
        "    int secret;\n"
        "};\n"
        "class Derived : public Base {\n"
        "public:\n"
        "    void setSecret(int s) { secret = s; }\n"
        "    int getSecret() const { return secret; }\n"
        "};\n"
        "int main() {\n"
        "    Derived d;\n"
        "    d.setSecret(42);\n"
        "    d.secret = 10;\n" // Error: secret is protected
        "    return d.getSecret();\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot access protected member 'secret'") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, SingleInheritanceCycleFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class A : public B {};\n"
        "class B : public A {};\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("inheritance cycle detected") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, OverrideValidationFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Animal {\n"
        "public:\n"
        "    void speak() {}\n"
        "};\n"
        "class Dog : public Animal {\n"
        "public:\n"
        "    void speak() override {}\n" // Error: Animal::speak is not virtual
        "};\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("marked 'override' does not override any virtual") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, AbstractClassInstantiationFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Shape {\n"
        "public:\n"
        "    virtual float area() = 0;\n"
        "};\n"
        "int main() {\n"
        "    Shape s;\n" // Error: cannot instantiate abstract class
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot instantiate abstract class") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, ReferenceNonConstRValueFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int& r = 42;\n" // Error: cannot bind non-const reference to rvalue
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot bind non-const reference") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, ReferenceUninitializedFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int& r;\n" // Error: reference must be initialized
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("reference variable 'r' must be initialized") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, ReturnLocalReferenceFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int& getLocal() {\n"
        "    int x = 10;\n"
        "    return x;\n" // Error: returning reference to local variable
        "}\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot return reference to local variable") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, SoAAddrOfElementFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "@layout(scan)\n"
        "struct Particle {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "int main() {\n"
        "    Particle particles[10];\n"
        "    Particle& p = particles[0];\n" // Error: cannot take address of SoA element
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot take address of element in Structure-of-Arrays") != std::string::npos);
    return true;
}

// ── v0.3.7 Safety Audit Tests ───────────────────────────────────────────────

TAMARIN_TEST(Sema, RejectStringLiteralModification) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    \"hello\"[0] = 'x';\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot modify string literal") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectConstArrayModification) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    const char s[] = \"immutable\";\n"
        "    s[0] = 'X';\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectFileObjectCopy) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Resource {\n"
        "public:\n"
        "    int handle;\n"
        "    Resource() { this.handle = 1; }\n"
        "    ~Resource() { this.handle = 0; }\n"
        "};\n"
        "int main() {\n"
        "    Resource r1;\n"
        "    Resource r2 = r1;\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot copy resource-owning class") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectFileObjectAssign) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Resource {\n"
        "public:\n"
        "    int handle;\n"
        "    Resource() { this.handle = 1; }\n"
        "    ~Resource() { this.handle = 0; }\n"
        "};\n"
        "int main() {\n"
        "    Resource r1;\n"
        "    Resource r2;\n"
        "    r2 = r1;\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot copy or assign resource-owning class") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectResourceClassPassByValue) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Resource {\n"
        "public:\n"
        "    int handle;\n"
        "    Resource() { this.handle = 1; }\n"
        "    ~Resource() { this.handle = 0; }\n"
        "};\n"
        "void takeResource(Resource r) {\n"
        "}\n"
        "int main() {\n"
        "    Resource r1;\n"
        "    takeResource(r1);\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot pass resource-owning class") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectResourceReturnByValue) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Resource {\n"
        "public:\n"
        "    int handle;\n"
        "    Resource() { this.handle = 1; }\n"
        "    ~Resource() { this.handle = 0; }\n"
        "};\n"
        "Resource makeResource() {\n"
        "    Resource r;\n"
        "    return r;\n"
        "}\n"
        "int main() {\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot return resource-owning") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectDerivedResourcePassAndReturnByValue) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Base {\n"
        "public:\n"
        "    ~Base() {}\n"
        "};\n"
        "class Derived : Base {\n"
        "public:\n"
        "    int x;\n"
        "};\n"
        "void takeDerived(Derived d) {}\n"
        "Derived makeDerived() { Derived d; return d; }\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot pass resource-owning") != std::string::npos ||
                errs.str().find("cannot return resource-owning") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectNestedResourcePassAndReturnByValue) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Res {\n"
        "public:\n"
        "    ~Res() {}\n"
        "};\n"
        "struct Container {\n"
        "    Res r;\n"
        "};\n"
        "void takeContainer(Container c) {}\n"
        "Container makeContainer() { Container c; return c; }\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RejectResourceArrayCopyInit) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Res {\n"
        "public:\n"
        "    ~Res() {}\n"
        "};\n"
        "int main() {\n"
        "    Res r1;\n"
        "    Res arr[2] = { r1, r1 };\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("into array element") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectPrintStructOrClass) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Foo { int a; };\n"
        "int main() {\n"
        "    Foo f;\n"
        "    println(f);\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("unsupported argument type for println") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectOpenFileInvalidArgs) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int h1 = openFile(\"test.txt\");\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("openFile() takes exactly 2 arguments") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectReadFileConstBuffer) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    const char buf[10] = \"123456789\";\n"
        "    readFile(\"test.txt\", buf, 10);\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("readFile() buffer cannot be const") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RejectFileReadInvalidHandle) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    char buf[10];\n"
        "    fileRead(\"not_a_handle\", buf, 10);\n"
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
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("fileRead() handle must be an integer") != std::string::npos);
    return true;
}

// ── v0.4.1 Benchmark & Arithmetic Integrity Tests ───────────────────────────

TAMARIN_TEST(Sema, SignedUnsignedArithmeticPromotion) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int i = 10;\n"
        "    u32 u = 20u32;\n"
        "    u32 res1 = i + u;\n"        // int + u32 -> u32
        "    u32 res2 = u + i;\n"        // u32 + int -> u32 (symmetric)
        "    long l = 100;\n"
        "    long res3 = l + u;\n"       // long (i64) + u32 -> long (i64)
        "    u64 ul = 500u64;\n"
        "    u64 res4 = i + ul;\n"       // int (i32) + u64 -> u64
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
        std::cerr << "SignedUnsignedArithmeticPromotion sema error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, LargeIntegerLiterals) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    long big = 9876543210123;\n"
        "    long hash = 1469598103934665603;\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}


TAMARIN_TEST(Sema, DestructorTraversalCycleSafety) {
    sema::TypeContext tc;
    // Construct a type with cyclic field reference to verify cycle detection doesn't loop or throw
    std::vector<sema::StructFieldType> fields;
    sema::StructFieldType f;
    f.name = "selfRef";
    f.type = nullptr; // placeholder
    fields.push_back(f);
    auto structTy = const_cast<sema::Type*>(tc.getStruct("CyclicNode", fields));
    structTy->structFields[0].type = structTy; // cycle!
    // hasNonTrivialDestructor must return true conservatively without hanging
    EXPECT_TRUE(structTy->hasNonTrivialDestructor());
    return true;
}

TAMARIN_TEST(Sema, DestructorIterativeGraphDeepDepths) {
    sema::TypeContext tc;

    // 1. Deep trivial hierarchy at depths 70, 129, 256, 512
    // Must return false without false positives or stack overflow.
    const sema::Type* currTrivial = tc.getI32();
    for (int d = 1; d <= 512; ++d) {
        std::vector<sema::StructFieldType> fields;
        fields.push_back({"val", currTrivial});
        currTrivial = tc.getStruct("TrivialChain_" + std::to_string(d), fields);
        if (d == 70 || d == 129 || d == 256 || d == 512) {
            EXPECT_FALSE(currTrivial->hasNonTrivialDestructor());
        }
    }

    // 2. Deep non-trivial hierarchy at depths 70, 129, 256, 512
    // Leaf has a destructor -> all enclosing parents must return true.
    auto leafWithDtor = const_cast<sema::Type*>(tc.getStruct("ResourceLeaf", {{"val", tc.getI32()}}));
    leafWithDtor->destructor = std::make_unique<sema::DestructorType>();
    EXPECT_TRUE(leafWithDtor->hasNonTrivialDestructor());

    const sema::Type* currResource = leafWithDtor;
    for (int d = 1; d <= 512; ++d) {
        std::vector<sema::StructFieldType> fields;
        fields.push_back({"child", currResource});
        currResource = tc.getStruct("ResourceChain_" + std::to_string(d), fields);
        if (d == 70 || d == 129 || d == 256 || d == 512) {
            EXPECT_TRUE(currResource->hasNonTrivialDestructor());
        }
    }

    return true;
}

TAMARIN_TEST(Sema, ArithmeticPromotionAllCombinations) {
    sema::TypeContext tc;

    std::vector<const sema::Type*> types = {
        tc.getI8(), tc.getI16(), tc.getI32(), tc.getI64(), tc.getIsize(),
        tc.getU8(), tc.getU16(), tc.getU32(), tc.getU64(), tc.getUsize(),
        tc.getF32(), tc.getF64()
    };

    // Verify perfect symmetry for all pairs: getCommonArithmeticType(A, B) == getCommonArithmeticType(B, A)
    for (auto* t1 : types) {
        for (auto* t2 : types) {
            auto* r1 = tc.getCommonArithmeticType(t1, t2);
            auto* r2 = tc.getCommonArithmeticType(t2, t1);
            EXPECT_TRUE(r1 != nullptr);
            EXPECT_TRUE(r2 != nullptr);
            EXPECT_EQ(r1, r2);
        }
    }

    // Specific expected promotion rules:
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getI8(), tc.getU8()), tc.getU8());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getI16(), tc.getU8()), tc.getI16());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getI32(), tc.getU32()), tc.getU32());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getI64(), tc.getU32()), tc.getI64());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getI64(), tc.getU64()), tc.getU64());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getIsize(), tc.getUsize()), tc.getUsize());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getF32(), tc.getI32()), tc.getF32());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getF32(), tc.getI64()), tc.getF64());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getF64(), tc.getI32()), tc.getF64());
    EXPECT_EQ(tc.getCommonArithmeticType(tc.getF64(), tc.getF32()), tc.getF64());

    return true;
}

TAMARIN_TEST(Sema, FloatToBitsIntrinsicTypeCheck) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    u64 b1 = __f64_to_bits(1.0);\n"
        "    u64 b2 = __double_to_bits(2.5);\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());

    // Bad argument count
    auto badFile = sm.addSource("bad.tmn",
        "int main() {\n"
        "    u64 b = __f64_to_bits(1.0, 2.0);\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream badErrs;
    DiagnosticEngine badDiag(sm, badErrs);
    lexer::Lexer badLex(sm, badFile, badDiag);
    parser::Parser badParser(badLex, badDiag);
    auto badProg = badParser.parseProgram();
    sema::Sema badSema(badDiag);
    EXPECT_FALSE(badSema.analyze(badProg));
    EXPECT_TRUE(badDiag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, NoinlineAttributeParsing) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "@noinline int compute(int a) {\n"
        "    return a * 2;\n"
        "}\n"
        "int main() {\n"
        "    return compute(21);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionValidPasses) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "int main() {\n"
        "    region scratch(65536) {\n"
        "        int values[100];\n"
        "        values[0] = 42;\n"
        "        Point pt = (Point){ 1, 2 };\n"
        "        Point pts[10];\n"
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
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectNegativeOrZeroCapacity) {
    SourceManager sm;
    auto fileId = sm.addSource("test_neg.tmn",
        "int main() {\n"
        "    region r(-1024) {\n"
        "        int a[10];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();
    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());

    auto fileIdZero = sm.addSource("test_zero.tmn",
        "int main() {\n"
        "    region r(0) {\n"
        "        int a[10];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errsZero;
    DiagnosticEngine diagZero(sm, errsZero);
    lexer::Lexer lexZero(sm, fileIdZero, diagZero);
    parser::Parser pZero(lexZero, diagZero);
    auto progZero = pZero.parseProgram();
    sema::Sema semaZero(diagZero);
    EXPECT_FALSE(semaZero.analyze(progZero));
    EXPECT_TRUE(diagZero.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectNonConstantCapacity) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int cap = 1024;\n"
        "    region r(cap) {\n"
        "        int a[10];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptNonTrivialDestructor) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Resource {\n"
        "    int handle;\n"
        "    ~Resource() {\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region scratch {\n"
        "        Resource r;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectEscapeReturningView) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int[] escapeRegion() {\n"
        "    region scratch {\n"
        "        int a[10];\n"
        "        return a;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectAssignToOuterLifetime) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int& outerRef;\n"
        "    region scratch {\n"
        "        int a[10];\n"
        "        outerRef = a[0];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectGlobalEscape) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int& g_ref;\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int a[10];\n"
        "        g_ref = a[0];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptNestedClassWithDestructor) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Resource {\n"
        "    int handle;\n"
        "    ~Resource() {\n"
        "    }\n"
        "};\n"
        "struct Container {\n"
        "    Resource res;\n"
        "};\n"
        "int main() {\n"
        "    region scratch {\n"
        "        Container c;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectGlobalStorageThroughParamBorrow) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int saved[100];\n"
        "void retain(int values[]) {\n"
        "    saved = values;\n"
        "}\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectMethodReceiverEscapeToGlobal) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Node {\n"
        "    int id;\n"
        "    void escape();\n"
        "};\n"
        "Node& g_node;\n"
        "void Node::escape() {\n"
        "    g_node = this;\n"
        "}\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectMethodReturningRegionFieldEscapingCaller) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Buffer {\n"
        "    int data[];\n"
        "    int[] getData() {\n"
        "        return this.data;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    int outerSlice[];\n"
        "    region scratch {\n"
        "        int localArr[10];\n"
        "        Buffer b = (Buffer){ localArr };\n"
        "        outerSlice = b.getData();\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectCompoundInitEscapingRegion) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct SliceRef {\n"
        "    int slice[];\n"
        "};\n"
        "int main() {\n"
        "    SliceRef outerRef;\n"
        "    region scratch {\n"
        "        int a[10];\n"
        "        outerRef = (SliceRef){ a };\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectNestedAggregateViewEscapingRegion) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Inner {\n"
        "    int view[];\n"
        "};\n"
        "struct Outer {\n"
        "    Inner inner;\n"
        "};\n"
        "int main() {\n"
        "    int escaped[];\n"
        "    region scratch {\n"
        "        int buf[20];\n"
        "        Outer o = (Outer){ (Inner){ buf } };\n"
        "        escaped = o.inner.view;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectFieldAssignToOuterObject) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Holder {\n"
        "    int view[];\n"
        "};\n"
        "int main() {\n"
        "    Holder h;\n"
        "    region scratch {\n"
        "        int buf[10];\n"
        "        h.view = buf;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectAliasEscapingToOuterScope) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    int outerView[];\n"
        "    region scratch {\n"
        "        int a[10];\n"
        "        int aliasView[] = a;\n"
        "        outerView = aliasView;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectExternalCallWithEscapingView) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "void externalRetain(int view[]);\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int a[10];\n"
        "        externalRetain(a);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptSafeHelperParamBorrowing) {
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
        "    int res = 0;\n"
        "    region scratch {\n"
        "        int a[5];\n"
        "        res = sum(a);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptMethodReturningReceiverViewWithinRegion) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Slice {\n"
        "    int data[];\n"
        "    int[] getView() {\n"
        "        return this.data;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int buf[10];\n"
        "        Slice s = (Slice){ buf };\n"
        "        int localView[] = s.getView();\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectSoAArrayInRegion) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "@layout(scan)\n"
        "struct Particle {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "int main() {\n"
        "    region scratch {\n"
        "        Particle particles[100];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("@layout(scan)") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RegionRejectArraySizeMultiplicationOverflow) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    region scratch {\n"
        "        int huge[1000000000000000000];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("overflow") != std::string::npos ||
                 errs.str().find("exceeds") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, RegionRejectInvalidCapacityExpressions) {
    // Non-constant
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "int main() {\n"
            "    int dyn = 100;\n"
            "    region r(dyn) {}\n"
            "    return 0;\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();
        sema::Sema sema(diag);
        EXPECT_FALSE(sema.analyze(prog));
        EXPECT_TRUE(diag.hasErrors());
    }
    // Negative capacity
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "int main() {\n"
            "    region r(-1024) {}\n"
            "    return 0;\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();
        sema::Sema sema(diag);
        EXPECT_FALSE(sema.analyze(prog));
        EXPECT_TRUE(diag.hasErrors());
    }
    // Zero capacity
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "int main() {\n"
            "    region r(0) {}\n"
            "    return 0;\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();
        sema::Sema sema(diag);
        EXPECT_FALSE(sema.analyze(prog));
        EXPECT_TRUE(diag.hasErrors());
    }
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptConstantCapacityExpressions) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    region r(1024 * 1024) {\n"
        "        int buf[10];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectParamBorrowForwardedToUnknownExternal) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "void externalRetain(int values[]);\n"
        "void forward(int values[]) {\n"
        "    externalRetain(values);\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectMethodReceiverForwardedToUnknownExternal) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Node {\n"
        "public:\n"
        "    int val;\n"
        "    void forward() {\n"
        "        externalStore(*this);\n"
        "    }\n"
        "};\n"
        "void externalStore(Node& node);\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectTransitiveExternalForwarding) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "void externalRetain(int values[]);\n"
        "void forward(int values[]) {\n"
        "    externalRetain(values);\n"
        "}\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int data[100];\n"
        "        forward(data);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptSafeHelperParamBorrowingChain) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int step3(int v[]) {\n"
        "    return len(v);\n"
        "}\n"
        "int step2(int v[]) {\n"
        "    return step3(v);\n"
        "}\n"
        "int step1(int v[]) {\n"
        "    return step2(v);\n"
        "}\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int buf[50];\n"
        "        int l = step1(buf);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptMethodReceiverForwardedThroughHelper) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Item {\n"
        "public:\n"
        "    int id;\n"
        "    int getId() {\n"
        "        return id;\n"
        "    }\n"
        "    int compute() {\n"
        "        return getId();\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    region scratch {\n"
        "        Item item;\n"
        "        int res = item.compute();\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAcceptRecursiveHelperBorrowing) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int sumRange(int values[], int idx, int count) {\n"
        "    if (idx >= count) {\n"
        "        return 0;\n"
        "    }\n"
        "    return values[idx] + sumRange(values, idx + 1, count);\n"
        "}\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int nums[5];\n"
        "        int s = sumRange(nums, 0, 5);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionShadowingRestoresOuterLocalRegionProvenance) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int[] leak(int& p) {\n"
        "    region scratch {\n"
        "        int values[10];\n"
        "        {\n"
        "            int& values = p;\n"
        "        }\n"
        "        return values;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionShadowingRestoresOuterParamBorrowProvenance) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int saved[100];\n"
        "void test(int values[]) {\n"
        "    {\n"
        "        int values = 42;\n"
        "    }\n"
        "    saved = values;\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionNestedScopeShadowingMultiLevel) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int[] multiLevel(int& a, int& b) {\n"
        "    region scratch {\n"
        "        int values[10];\n"
        "        {\n"
        "            int& values = a;\n"
        "            {\n"
        "                int& values = b;\n"
        "            }\n"
        "        }\n"
        "        return values;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionAggregateFieldBorrowReturnPropagation) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Holder {\n"
        "    int view[];\n"
        "};\n"
        "Holder wrap(int values[]) {\n"
        "    Holder h;\n"
        "    h.view = values;\n"
        "    return h;\n"
        "}\n"
        "Holder g_h;\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int data[10];\n"
        "        g_h = wrap(data);\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionRejectReturningAggregateWithLocalRegionField) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Holder {\n"
        "    int view[];\n"
        "};\n"
        "Holder leak() {\n"
        "    region scratch {\n"
        "        int data[10];\n"
        "        Holder h;\n"
        "        h.view = data;\n"
        "        return h;\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionMethodReturningAggregateWithReceiverView) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Holder {\n"
        "    int view[];\n"
        "};\n"
        "class Owner {\n"
        "public:\n"
        "    int data[50];\n"
        "    Holder getHolder() {\n"
        "        Holder h;\n"
        "        h.view = data;\n"
        "        return h;\n"
        "    }\n"
        "};\n"
        "Holder g_holder;\n"
        "int main() {\n"
        "    region scratch {\n"
        "        Owner o;\n"
        "        g_holder = o.getHolder();\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionFixedSizeArrayFieldViewDecayProvenance) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "class Buffer {\n"
        "public:\n"
        "    int values[100];\n"
        "    int[] view() {\n"
        "        return values;\n"
        "    }\n"
        "};\n"
        "int g_saved[100];\n"
        "int main() {\n"
        "    region scratch {\n"
        "        Buffer b;\n"
        "        g_saved = b.view();\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionNestedFieldPathAssignmentEscapeRejection) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Inner {\n"
        "    int view[];\n"
        "};\n"
        "struct Outer {\n"
        "    Inner inner;\n"
        "};\n"
        "Outer g_outer;\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int data[20];\n"
        "        Outer local_outer;\n"
        "        local_outer.inner.view = data;\n"
        "        g_outer = local_outer;\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionCrossModuleGlobalHolderEscapeRejection) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Holder {\n"
        "    int view[];\n"
        "};\n"
        "Holder globalHolder;\n"
        "void save(int values[]) {\n"
        "    globalHolder.view = values;\n"
        "}\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyze(prog));
    EXPECT_TRUE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, RegionDeepCanContainBorrowCycleSafety) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Node {\n"
        "    int val;\n"
        "    Node& next;\n"
        "};\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    return true;
}

TAMARIN_TEST(Sema, RegionConstantExpressionArrayBoundsSema) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    region scratch {\n"
        "        int a[4 * 1024];\n"
        "        int b[1 << 14];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, DiamondTypeGraphSharedBorrowDetection) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct SharedBorrow {\n"
        "    int& ref;\n"
        "};\n"
        "struct Left {\n"
        "    SharedBorrow s;\n"
        "};\n"
        "struct Right {\n"
        "    SharedBorrow s;\n"
        "};\n"
        "struct Diamond {\n"
        "    Left l;\n"
        "    Right r;\n"
        "};\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    auto diamondTy = sema.context().findRecord("Diamond");
    EXPECT_TRUE(diamondTy != nullptr);
    EXPECT_TRUE(diamondTy->canContainBorrow());
    return true;
}

TAMARIN_TEST(Sema, DiamondTypeGraphSharedTrivialBorrowFree) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct SharedTrivial {\n"
        "    int x;\n"
        "    float y;\n"
        "};\n"
        "struct Left {\n"
        "    SharedTrivial s;\n"
        "};\n"
        "struct Right {\n"
        "    SharedTrivial s;\n"
        "};\n"
        "struct Diamond {\n"
        "    Left l;\n"
        "    Right r;\n"
        "};\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    auto diamondTy = sema.context().findRecord("Diamond");
    EXPECT_TRUE(diamondTy != nullptr);
    EXPECT_FALSE(diamondTy->canContainBorrow());
    return true;
}

TAMARIN_TEST(Sema, DeepNestedTypeGraphBorrowDetection) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct L0 { int& ref; };\n"
        "struct L1 { L0 n; };\n"
        "struct L2 { L1 n; };\n"
        "struct L3 { L2 n; };\n"
        "struct L4 { L3 n; };\n"
        "struct L5 { L4 n; };\n"
        "struct L6 { L5 n; };\n"
        "struct L7 { L6 n; };\n"
        "struct L8 { L7 n; };\n"
        "struct L9 { L8 n; };\n"
        "struct L10 { L9 n; };\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    auto topTy = sema.context().findRecord("L10");
    EXPECT_TRUE(topTy != nullptr);
    EXPECT_TRUE(topTy->canContainBorrow());
    return true;
}

TAMARIN_TEST(Sema, DeepNestedTypeGraphTrivialBorrowFree) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct L0 { int x; };\n"
        "struct L1 { L0 n; };\n"
        "struct L2 { L1 n; };\n"
        "struct L3 { L2 n; };\n"
        "struct L4 { L3 n; };\n"
        "struct L5 { L4 n; };\n"
        "struct L6 { L5 n; };\n"
        "struct L7 { L6 n; };\n"
        "struct L8 { L7 n; };\n"
        "struct L9 { L8 n; };\n"
        "struct L10 { L9 n; };\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyze(prog));
    auto topTy = sema.context().findRecord("L10");
    EXPECT_TRUE(topTy != nullptr);
    EXPECT_FALSE(topTy->canContainBorrow());
    return true;
}

TAMARIN_TEST(Sema, SlabValidOperationsAndMethods) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    Slab<int> s(64);\n"
        "    Handle<int> h = s.insert(10);\n"
        "    bool v = s.is_valid(h);\n"
        "    bool hv = h.is_valid();\n"
        "    int slot = h.slot();\n"
        "    int gen = h.generation();\n"
        "    long count = s.count();\n"
        "    long cap = s.capacity();\n"
        "    int val = s[h];\n"
        "    s[h] = 20;\n"
        "    int outVal = 0;\n"
        "    bool ok = s.try_get(h, outVal);\n"
        "    bool rmOk = s.remove(h);\n"
        "    s.clear();\n"
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
        std::cerr << "Sema errors: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);
    return true;
}

TAMARIN_TEST(Sema, SlabTypeMismatchRejections) {
    // 1. Insert wrong type into slab
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "int main() {\n"
            "    Slab<int> s(64);\n"
            "    s.insert(3.14);\n"
            "    return 0;\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();

        sema::Sema sema(diag);
        EXPECT_FALSE(sema.analyze(prog));
        EXPECT_TRUE(errs.str().find("cannot insert element of type") != std::string::npos);
    }

    // 2. Mismatched handle lookup
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "struct Point { int x; int y; };\n"
            "int main() {\n"
            "    Slab<int> s1(64);\n"
            "    Slab<Point> s2(64);\n"
            "    Point pt = (Point){ 1, 2 };\n"
            "    Handle<Point> hp = s2.insert(pt);\n"
            "    int val = s1.get(hp);\n"
            "    return 0;\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();

        sema::Sema sema(diag);
        EXPECT_FALSE(sema.analyze(prog));
        EXPECT_TRUE(errs.str().find("handle type mismatch") != std::string::npos);
    }

    return true;
}

TAMARIN_TEST(Sema, EnumAndPatternMatchExhaustiveness) {
    // 1. Valid exhaustive match passes
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "enum Color {\n"
            "    Red,\n"
            "    Green,\n"
            "    Blue\n"
            "};\n"
            "int toCode(Color c) {\n"
            "    match (c) {\n"
            "        Color::Red => { return 1; }\n"
            "        Color::Green => { return 2; }\n"
            "        Color::Blue => { return 3; }\n"
            "    }\n"
            "    return 0;\n"
            "}\n"
            "int main() {\n"
            "    Color c = Color::Red;\n"
            "    return toCode(c);\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();

        sema::Sema sema(diag);
        bool ok = sema.analyze(prog);
        if (!ok) {
            std::cout << "SEMA ERROR: " << errs.str() << "\n";
        }
        EXPECT_TRUE(ok);
        EXPECT_FALSE(diag.hasErrors());
    }

    // 2. Non-exhaustive match without catch-all fails
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "enum Color {\n"
            "    Red,\n"
            "    Green,\n"
            "    Blue\n"
            "};\n"
            "int toCode(Color c) {\n"
            "    match (c) {\n"
            "        Color::Red => { return 1; }\n"
            "        Color::Green => { return 2; }\n"
            "    }\n"
            "    return 0;\n"
            "}\n"
            "int main() {\n"
            "    return 0;\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();

        sema::Sema sema(diag);
        EXPECT_FALSE(sema.analyze(prog));
        EXPECT_TRUE(diag.hasErrors());
        EXPECT_TRUE(errs.str().find("non-exhaustive patterns in match: missing variant 'Blue'") != std::string::npos);
    }

    // 3. Option with Some / None and ? operator
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "Option<int> maybeVal(bool flag) {\n"
            "    if (flag) {\n"
            "        return Option::Some(100);\n"
            "    }\n"
            "    return Option::None;\n"
            "}\n"
            "int main() {\n"
            "    Option<int> opt = maybeVal(true);\n"
            "    int res = match (opt) {\n"
            "        Option::Some(x) => x,\n"
            "        _ => 0,\n"
            "    };\n"
            "    return res;\n"
            "}\n");
        std::ostringstream errs;
        DiagnosticEngine diag(sm, errs);
        lexer::Lexer lex(sm, fileId, diag);
        parser::Parser p(lex, diag);
        auto prog = p.parseProgram();

        sema::Sema sema(diag);
        bool ok = sema.analyze(prog);
        if (!ok) {
            std::cout << "SEMA ERROR: " << errs.str() << "\n";
        }
        EXPECT_TRUE(ok);
        EXPECT_FALSE(diag.hasErrors());
    }

    return true;
}

TAMARIN_TEST(Sema, GenericStructInstantiation) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Pair<T, U> {\n"
        "    T first;\n"
        "    U second;\n"
        "};\n"
        "int main() {\n"
        "    Pair<int, double> p;\n"
        "    p.first = 42;\n"
        "    p.second = 3.14;\n"
        "    return p.first;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERROR: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, GenericFunctionExplicitAndInferred) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "T identity<T>(T val) {\n"
        "    return val;\n"
        "}\n"
        "int main() {\n"
        "    int a = identity<int>(10);\n"
        "    int b = identity(20);\n"
        "    return a + b;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) {
        std::cout << "SEMA ERROR: " << errs.str() << "\n";
    }
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());

    return true;
}

TAMARIN_TEST(Sema, TraitBoundValidation) {
    // 1. Valid implementation satisfies bound
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "trait Printable {\n"
            "    void print();\n"
            "}\n"
            "struct Point {\n"
            "    int x;\n"
            "    int y;\n"
            "};\n"
            "impl Printable for Point {\n"
            "    void print() {\n"
            "        println(this.x);\n"
            "    }\n"
            "}\n"
            "void display<T>(T item) where T: Printable {\n"
            "    item.print();\n"
            "}\n"
            "int main() {\n"
            "    Point pt;\n"
            "    pt.x = 10;\n"
            "    pt.y = 20;\n"
            "    display(pt);\n"
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
            std::cout << "SEMA ERROR: " << errs.str() << "\n";
        }
        EXPECT_TRUE(ok);
        EXPECT_FALSE(diag.hasErrors());
    }

    // 2. Missing implementation rejected by trait bound
    {
        SourceManager sm;
        auto fileId = sm.addSource("test.tmn",
            "trait Printable {\n"
            "    void print();\n"
            "}\n"
            "struct NotPrintable {\n"
            "    int x;\n"
            "};\n"
            "void display<T>(T item) where T: Printable {\n"
            "    item.print();\n"
            "}\n"
            "int main() {\n"
            "    NotPrintable np;\n"
            "    display(np);\n"
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
        EXPECT_TRUE(diag.hasErrors());
        EXPECT_TRUE(errs.str().find("does not satisfy trait bound 'Printable'") != std::string::npos);
    }

    return true;
}

TAMARIN_TEST(Sema, SelectStatementTypeChecking) {
    SourceManager sm;
    auto fileId = sm.addSource("select_sema.tmn",
        "int main() {\n"
        "    int res = 0;\n"
        "    bool ready = true;\n"
        "    select {\n"
        "        case ready:\n"
        "            res = 42;\n"
        "        default:\n"
        "            res = 0;\n"
        "    }\n"
        "    return res;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    if (!ok) std::cerr << "Sema error: " << errs.str() << "\n";
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, AsyncRejectAwaitNonTask) {
    SourceManager sm;
    auto fileId = sm.addSource("test_await_non_task.tm",
        "int main() {\n"
        "    int x = 42;\n"
        "    int y = await x;\n"
        "    return y;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("cannot await non-task type 'int'") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, AsyncRejectCAbiExport) {
    SourceManager sm;
    auto fileId = sm.addSource("test_async_export.tm",
        "@export\n"
        "async int foo() {\n"
        "    return 1;\n"
        "}\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("async function 'foo' cannot have C linkage or be exported directly across C ABI boundary") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, AsyncRejectTaskCAbiExport) {
    SourceManager sm;
    auto fileId = sm.addSource("test_task_export.tm",
        "@export\n"
        "int bar(Task<int> t) {\n"
        "    return 0;\n"
        "}\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("task type 'Task<int>' cannot cross C ABI boundaries directly") != std::string::npos);
    return true;
}

TAMARIN_TEST(Sema, CfgPruningValidProgram) {
    SourceManager sm;
    auto fileId = sm.addSource("test_cfg_valid.tam",
        "@cfg(target_os = \"windows\")\n"
        "int getCode() { return 100; }\n"
        "\n"
        "@cfg(target_os = \"linux\")\n"
        "int getCode() { return 200; }\n"
        "\n"
        "int main() {\n"
        "    return getCode() - 100;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    p.setTargetTriple(target::TargetTriple::parse("x86_64-pc-windows-msvc"));
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    sema.setTargetTriple(target::TargetTriple::parse("x86_64-pc-windows-msvc"));
    bool ok = sema.analyze(prog);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(diag.hasErrors());
    return true;
}

TAMARIN_TEST(Sema, CfgCallingInactiveFunctionFails) {
    SourceManager sm;
    auto fileId = sm.addSource("test_cfg_inactive.tam",
        "@cfg(target_os = \"linux\")\n"
        "int linuxOnly() { return 42; }\n"
        "\n"
        "int main() {\n"
        "    return linuxOnly();\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    p.setTargetTriple(target::TargetTriple::parse("x86_64-pc-windows-msvc"));
    auto prog = p.parseProgram();

    sema::Sema sema(diag);
    sema.setTargetTriple(target::TargetTriple::parse("x86_64-pc-windows-msvc"));
    bool ok = sema.analyze(prog);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("undeclared function 'linuxOnly'") != std::string::npos);
    return true;
}





