// ============================================================================
// parser_test.cpp - Unit tests for the Tamarin parser
// ============================================================================

#include "test_harness.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <sstream>

using namespace tamarin;
using namespace tamarin::ast;

TAMARIN_TEST(Parser, ParseFunction) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 1);

    auto& fn = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fn.name, "add");
    EXPECT_EQ(fn.params.size(), 2);
    EXPECT_EQ(fn.params[0].name, "a");
    EXPECT_EQ(fn.params[1].name, "b");

    return true;
}



TAMARIN_TEST(Parser, ParseSumExample) {
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
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 2);

    auto& fn1 = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fn1.name, "sum");
    EXPECT_TRUE(fn1.params[0].isArray);

    auto& fn2 = static_cast<FunctionDecl&>(*prog.decls[1]);
    EXPECT_EQ(fn2.name, "main");

    return true;
}



TAMARIN_TEST(Parser, ParseIfElse) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int max(int a, int b) {\n"
        "    if (a > b) {\n"
        "        return a;\n"
        "    } else {\n"
        "        return b;\n"
        "    }\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 1);

    return true;
}



TAMARIN_TEST(Parser, ParseWhileLoop) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int count() {\n"
        "    int x = 0;\n"
        "    while (x < 10) {\n"
        "        x++;\n"
        "    }\n"
        "    return x;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 1);

    return true;
}



TAMARIN_TEST(Parser, ParseStructWithLayout) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "@layout(point)\n"
        "struct Vec2 {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "@c_layout\n"
        "struct CCompat {\n"
        "    int id;\n"
        "    double val;\n"
        "};\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 2);

    auto& s1 = static_cast<StructDecl&>(*prog.decls[0]);
    EXPECT_EQ(s1.name, "Vec2");
    EXPECT_EQ(s1.fields.size(), 2);
    EXPECT_EQ(s1.layout, LayoutDirective::Point);

    auto& s2 = static_cast<StructDecl&>(*prog.decls[1]);
    EXPECT_EQ(s2.name, "CCompat");
    EXPECT_EQ(s2.fields.size(), 2);
    EXPECT_EQ(s2.layout, LayoutDirective::CLayout);

    return true;
}



TAMARIN_TEST(Parser, ParseCompoundLiteral) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Vec2 {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "int main() {\n"
        "    Vec2 a = (Vec2){ 1.0f, 2.0f };\n"
        "    Vec2 b = (Vec2){ .x = 3.0f, .y = 4.0f };\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 2);

    return true;
}



TAMARIN_TEST(Parser, ParseRegionSyntaxVariants) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int main() {\n"
        "    region scratch(65536) {\n"
        "        int a[10];\n"
        "    }\n"
        "    region frame {\n"
        "        int b[20];\n"
        "    }\n"
        "    region(1024) {\n"
        "        int c[30];\n"
        "    }\n"
        "    region {\n"
        "        int d[40];\n"
        "    }\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 1);

    auto& fn = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fn.name, "main");
    EXPECT_TRUE(fn.body != nullptr);
    EXPECT_EQ(fn.body->stmts.size(), 5);

    EXPECT_EQ(fn.body->stmts[0]->kind, StmtKind::Region);
    auto& r1 = static_cast<RegionStmt&>(*fn.body->stmts[0]);
    EXPECT_EQ(r1.name, "scratch");
    EXPECT_TRUE(r1.capacityExpr != nullptr);

    EXPECT_EQ(fn.body->stmts[1]->kind, StmtKind::Region);
    auto& r2 = static_cast<RegionStmt&>(*fn.body->stmts[1]);
    EXPECT_EQ(r2.name, "frame");
    EXPECT_TRUE(r2.capacityExpr == nullptr);

    EXPECT_EQ(fn.body->stmts[2]->kind, StmtKind::Region);
    auto& r3 = static_cast<RegionStmt&>(*fn.body->stmts[2]);
    EXPECT_TRUE(r3.name.empty());
    EXPECT_TRUE(r3.capacityExpr != nullptr);

    EXPECT_EQ(fn.body->stmts[3]->kind, StmtKind::Region);
    auto& r4 = static_cast<RegionStmt&>(*fn.body->stmts[3]);
    EXPECT_TRUE(r4.name.empty());
    EXPECT_TRUE(r4.capacityExpr == nullptr);

    return true;
}



TAMARIN_TEST(Parser, ParseHandleAndSlabSyntax) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "struct Particle { float x; float y; };\n"
        "int main() {\n"
        "    Slab<int> pool(128);\n"
        "    slab(Particle, 64) particles;\n"
        "    Handle<int> h1 = pool.insert(42);\n"
        "    Handle<Particle> h2;\n"
        "    return 0;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 2);

    auto& fn = static_cast<FunctionDecl&>(*prog.decls[1]);
    EXPECT_EQ(fn.name, "main");
    EXPECT_TRUE(fn.body != nullptr);
    EXPECT_EQ(fn.body->stmts.size(), 5);

    auto& s1 = static_cast<VarDeclStmt&>(*fn.body->stmts[0]);
    EXPECT_EQ(s1.name, "pool");
    EXPECT_TRUE(s1.type != nullptr);
    EXPECT_EQ(s1.type->kind, TypeNodeKind::Slab);

    auto& s2 = static_cast<VarDeclStmt&>(*fn.body->stmts[1]);
    EXPECT_EQ(s2.name, "particles");
    EXPECT_TRUE(s2.type != nullptr);
    EXPECT_EQ(s2.type->kind, TypeNodeKind::Slab);

    auto& s3 = static_cast<VarDeclStmt&>(*fn.body->stmts[2]);
    EXPECT_EQ(s3.name, "h1");
    EXPECT_TRUE(s3.type != nullptr);
    EXPECT_EQ(s3.type->kind, TypeNodeKind::Handle);

    auto& s4 = static_cast<VarDeclStmt&>(*fn.body->stmts[3]);
    EXPECT_EQ(s4.name, "h2");
    EXPECT_TRUE(s4.type != nullptr);
    EXPECT_EQ(s4.type->kind, TypeNodeKind::Handle);

    return true;
}



TAMARIN_TEST(Parser, ParseEnumAndPatternMatch) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "enum Shape {\n"
        "    Circle(float),\n"
        "    Rect(float, float),\n"
        "    Point\n"
        "};\n"
        "float getArea(Shape s) {\n"
        "    match (s) {\n"
        "        Shape::Circle(r) => {\n"
        "            return 3.14 * r * r;\n"
        "        }\n"
        "        Shape::Rect(w, h) => {\n"
        "            return w * h;\n"
        "        }\n"
        "        Shape::Point => {\n"
        "            return 0.0;\n"
        "        }\n"
        "        _ => {\n"
        "            return -1.0;\n"
        "        }\n"
        "    }\n"
        "    return 0.0;\n"
        "}\n"
        "int testMatchExpr(Option<int> opt) {\n"
        "    int val = match (opt) {\n"
        "        Option::Some(x) => x,\n"
        "        _ => 0,\n"
        "    };\n"
        "    int y = readVal()?;\n"
        "    return val + y;\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 3);

    // Decl 0: EnumDecl Shape
    EXPECT_EQ(prog.decls[0]->kind, DeclKind::Enum);
    auto& enumDecl = static_cast<EnumDecl&>(*prog.decls[0]);
    EXPECT_EQ(enumDecl.name, "Shape");
    EXPECT_EQ(enumDecl.variants.size(), 3);
    EXPECT_EQ(enumDecl.variants[0].name, "Circle");
    EXPECT_EQ(enumDecl.variants[0].payloadTypes.size(), 1);
    EXPECT_EQ(enumDecl.variants[1].name, "Rect");
    EXPECT_EQ(enumDecl.variants[1].payloadTypes.size(), 2);
    EXPECT_EQ(enumDecl.variants[2].name, "Point");
    EXPECT_EQ(enumDecl.variants[2].payloadTypes.size(), 0);

    // Decl 1: Function getArea with MatchStmt
    EXPECT_EQ(prog.decls[1]->kind, DeclKind::Function);
    auto& fn1 = static_cast<FunctionDecl&>(*prog.decls[1]);
    EXPECT_EQ(fn1.name, "getArea");
    EXPECT_EQ(fn1.body->stmts.size(), 2);
    EXPECT_EQ(fn1.body->stmts[0]->kind, StmtKind::Match);
    auto& matchStmt = static_cast<MatchStmt&>(*fn1.body->stmts[0]);
    EXPECT_EQ(matchStmt.cases.size(), 4);
    EXPECT_EQ(matchStmt.cases[0].pattern.kind, MatchPatternKind::Variant);
    EXPECT_EQ(matchStmt.cases[0].pattern.variantName, "Circle");
    EXPECT_EQ(matchStmt.cases[0].pattern.bindings.size(), 1);
    EXPECT_EQ(matchStmt.cases[0].pattern.bindings[0], "r");
    EXPECT_EQ(matchStmt.cases[3].pattern.kind, MatchPatternKind::Wildcard);

    // Decl 2: Function testMatchExpr with MatchExpr and TryExpr
    EXPECT_EQ(prog.decls[2]->kind, DeclKind::Function);
    auto& fn2 = static_cast<FunctionDecl&>(*prog.decls[2]);
    EXPECT_EQ(fn2.name, "testMatchExpr");
    EXPECT_EQ(fn2.body->stmts.size(), 3);
    auto& v1 = static_cast<VarDeclStmt&>(*fn2.body->stmts[0]);
    EXPECT_TRUE(v1.initializer != nullptr);
    EXPECT_EQ(v1.initializer->kind, ExprKind::Match);
    auto& v2 = static_cast<VarDeclStmt&>(*fn2.body->stmts[1]);
    EXPECT_TRUE(v2.initializer != nullptr);
    EXPECT_EQ(v2.initializer->kind, ExprKind::Try);

    return true;
}

TAMARIN_TEST(Parser, GenericFunctionAndStruct) {
    SourceManager sm;
    auto fileId = sm.addSource("generics.tmn",
        "T max<T>(T a, T b) where T: Comparable {\n"
        "    if (a > b) { return a; }\n"
        "    return b;\n"
        "}\n"
        "struct Pair<T, U> {\n"
        "    T first;\n"
        "    U second;\n"
        "};\n"
        "void test() {\n"
        "    Pair<int, double> p;\n"
        "    int m = max<int>(1, 2);\n"
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 3);

    // Decl 0: max<T>
    EXPECT_EQ(prog.decls[0]->kind, DeclKind::Function);
    auto& fn = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fn.name, "max");
    EXPECT_EQ(fn.typeParams.size(), 1);
    EXPECT_EQ(fn.typeParams[0], "T");
    EXPECT_EQ(fn.traitBounds.size(), 1);
    EXPECT_EQ(fn.traitBounds[0].first, "T");
    EXPECT_EQ(fn.traitBounds[0].second, "Comparable");

    // Decl 1: Pair<T, U>
    EXPECT_EQ(prog.decls[1]->kind, DeclKind::Struct);
    auto& sd = static_cast<StructDecl&>(*prog.decls[1]);
    EXPECT_EQ(sd.name, "Pair");
    EXPECT_EQ(sd.typeParams.size(), 2);
    EXPECT_EQ(sd.typeParams[0], "T");
    EXPECT_EQ(sd.typeParams[1], "U");
    EXPECT_EQ(sd.fields.size(), 2);
    EXPECT_EQ(sd.fields[0].name, "first");
    EXPECT_EQ(sd.fields[1].name, "second");

    // Decl 2: test()
    EXPECT_EQ(prog.decls[2]->kind, DeclKind::Function);
    auto& tfn = static_cast<FunctionDecl&>(*prog.decls[2]);
    EXPECT_EQ(tfn.body->stmts.size(), 2);

    auto& v1 = static_cast<VarDeclStmt&>(*tfn.body->stmts[0]);
    EXPECT_EQ(v1.type->kind, TypeNodeKind::Named);
    auto& nt1 = static_cast<NamedTypeNode&>(*v1.type);
    EXPECT_EQ(nt1.name, "Pair");
    EXPECT_EQ(nt1.typeArgs.size(), 2);

    auto& v2 = static_cast<VarDeclStmt&>(*tfn.body->stmts[1]);
    EXPECT_TRUE(v2.initializer != nullptr);
    EXPECT_EQ(v2.initializer->kind, ExprKind::Call);
    auto& call = static_cast<CallExpr&>(*v2.initializer);
    EXPECT_EQ(call.callee, "max");
    EXPECT_EQ(call.typeArgs.size(), 1);

    return true;
}

TAMARIN_TEST(Parser, TraitAndImpl) {
    SourceManager sm;
    auto fileId = sm.addSource("trait_impl.tmn",
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
        "}\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cout << "PARSER ERRORS: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 3);

    // Decl 0: TraitDecl Printable
    EXPECT_EQ(prog.decls[0]->kind, DeclKind::Trait);
    auto& td = static_cast<TraitDecl&>(*prog.decls[0]);
    EXPECT_EQ(td.name, "Printable");
    EXPECT_EQ(td.methods.size(), 1);
    EXPECT_EQ(td.methods[0].name, "print");

    // Decl 1: StructDecl Point
    EXPECT_EQ(prog.decls[1]->kind, DeclKind::Struct);

    // Decl 2: ImplDecl Printable for Point
    EXPECT_EQ(prog.decls[2]->kind, DeclKind::Impl);
    auto& id = static_cast<ImplDecl&>(*prog.decls[2]);
    EXPECT_EQ(id.traitName, "Printable");
    EXPECT_EQ(id.targetType->kind, TypeNodeKind::Named);
    EXPECT_EQ(static_cast<NamedTypeNode&>(*id.targetType).name, "Point");
    EXPECT_EQ(id.methods.size(), 1);
    EXPECT_EQ(id.methods[0].name, "print");

    return true;
}

TAMARIN_TEST(Parser, ParseSingleStatementControlFlow) {
    SourceManager sm;
    auto fileId = sm.addSource("single_stmt.tmn",
        "int max(int a, int b) {\n"
        "    if (a > b) return a;\n"
        "    else return b;\n"
        "}\n"
        "int testChained(int x) {\n"
        "    if (x == 1) return 10;\n"
        "    else if (x == 2) return 20;\n"
        "    else return 30;\n"
        "}\n"
        "int testLoops() {\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < 10; i++) s += i;\n"
        "    while (s > 0) s--;\n"
        "    return s;\n"
        "}\n"
        "int testParallel(int arr[]) {\n"
        "    int sum = 0;\n"
        "    parallel (reduction(+:sum)) for (int i = 0; i < 100; i++) sum += arr[i];\n"
        "    return sum;\n"
        "}\n"
    );
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 4);

    // Verify max() body has IfStmt with ReturnStmt in then and else
    auto& fn0 = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fn0.name, "max");
    EXPECT_EQ(fn0.body->stmts.size(), 1);
    EXPECT_EQ(fn0.body->stmts[0]->kind, StmtKind::If);
    auto& if0 = static_cast<IfStmt&>(*fn0.body->stmts[0]);
    EXPECT_EQ(if0.thenBlock->kind, StmtKind::Return);
    EXPECT_TRUE(if0.elseBlock != nullptr);
    EXPECT_EQ(if0.elseBlock->kind, StmtKind::Return);

    // Verify testLoops() body has ForStmt with ExprStmt, WhileStmt with ExprStmt
    auto& fn2 = static_cast<FunctionDecl&>(*prog.decls[2]);
    EXPECT_EQ(fn2.name, "testLoops");
    EXPECT_EQ(fn2.body->stmts.size(), 4);
    EXPECT_EQ(fn2.body->stmts[1]->kind, StmtKind::For);
    auto& forStmt = static_cast<ForStmt&>(*fn2.body->stmts[1]);
    EXPECT_EQ(forStmt.body->kind, StmtKind::Expr);
    EXPECT_EQ(fn2.body->stmts[2]->kind, StmtKind::While);
    auto& whileStmt = static_cast<WhileStmt&>(*fn2.body->stmts[2]);
    EXPECT_EQ(whileStmt.body->kind, StmtKind::Expr);

    // Verify testParallel() body has ParallelForStmt with ExprStmt
    auto& fn3 = static_cast<FunctionDecl&>(*prog.decls[3]);
    EXPECT_EQ(fn3.name, "testParallel");
    EXPECT_EQ(fn3.body->stmts.size(), 3);
    EXPECT_EQ(fn3.body->stmts[1]->kind, StmtKind::ParallelFor);
    auto& pforStmt = static_cast<ParallelForStmt&>(*fn3.body->stmts[1]);
    EXPECT_EQ(pforStmt.body->kind, StmtKind::Expr);

    return true;
}

TAMARIN_TEST(Parser, ParseVectorizeLoopAttributes) {
    SourceManager sm;
    auto fileId = sm.addSource("vec_attr.tmn",
        "void testVec(float a[], float b[], float c[], int n) {\n"
        "    @vectorize(width: 8, interleave: 4)\n"
        "    for (int i = 0; i < n; i++) {\n"
        "        c[i] = a[i] + b[i];\n"
        "    }\n"
        "    @vectorize(enable: false)\n"
        "    while (n > 0) {\n"
        "        n--;\n"
        "    }\n"
        "    @unroll(8) @vectorize(width: 16)\n"
        "    parallel for (int i = 0; i < 1000; i++) {\n"
        "        c[i] = a[i] * 2.0f;\n"
        "    }\n"
        "}\n"
    );
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 1);

    auto& fn = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fn.body->stmts.size(), 3);

    auto& s0 = static_cast<ForStmt&>(*fn.body->stmts[0]);
    EXPECT_EQ(s0.vectorizeEnable, 1);
    EXPECT_EQ(s0.vectorizeWidth, 8);
    EXPECT_EQ(s0.interleaveCount, 4);

    auto& s1 = static_cast<WhileStmt&>(*fn.body->stmts[1]);
    EXPECT_EQ(s1.vectorizeEnable, -1);

    auto& s2 = static_cast<ParallelForStmt&>(*fn.body->stmts[2]);
    EXPECT_EQ(s2.unrollCount, 8);
    EXPECT_EQ(s2.vectorizeEnable, 1);
    EXPECT_EQ(s2.vectorizeWidth, 16);

    return true;
}

TAMARIN_TEST(Parser, ParseSelectStatement) {
    SourceManager sm;
    auto fileId = sm.addSource("select_test.tmn",
        "void worker() {\n"
        "    int x = 0;\n"
        "    select {\n"
        "        case ch1.recv(x):\n"
        "            x = x + 1;\n"
        "        case ch2.send(10): {\n"
        "            x = 0;\n"
        "        }\n"
        "        default:\n"
        "            x = -1;\n"
        "    }\n"
        "}\n"
    );
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 1);

    auto& fn = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fn.body->stmts.size(), 2);

    auto& sel = static_cast<SelectStmt&>(*fn.body->stmts[1]);
    EXPECT_EQ(sel.cases.size(), 2);
    EXPECT_TRUE(sel.hasDefault);
    EXPECT_EQ(sel.defaultBody.size(), 1);

    return true;
}

TAMARIN_TEST(Parser, ParseExternCBlocksAndPrototypes) {
    SourceManager sm;
    auto fileId = sm.addSource("extern_test.tmn",
        "extern \"C\" {\n"
        "    int puts(char* s);\n"
        "    int abs(int x);\n"
        "}\n"
        "extern double sqrt(double x);\n"
        "@export int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
    );
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    if (diag.hasErrors()) {
        std::cerr << "Parser error output: " << errs.str() << "\n";
    }
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 4);

    // puts
    auto& fnPuts = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fnPuts.name, "puts");
    EXPECT_TRUE(fnPuts.isExtern);
    EXPECT_EQ(fnPuts.linkage, "C");
    EXPECT_TRUE(fnPuts.body == nullptr);

    // abs
    auto& fnAbs = static_cast<FunctionDecl&>(*prog.decls[1]);
    EXPECT_EQ(fnAbs.name, "abs");
    EXPECT_TRUE(fnAbs.isExtern);
    EXPECT_EQ(fnAbs.linkage, "C");
    EXPECT_TRUE(fnAbs.body == nullptr);

    // sqrt
    auto& fnSqrt = static_cast<FunctionDecl&>(*prog.decls[2]);
    EXPECT_EQ(fnSqrt.name, "sqrt");
    EXPECT_TRUE(fnSqrt.isExtern);
    EXPECT_EQ(fnSqrt.linkage, "C");
    EXPECT_TRUE(fnSqrt.body == nullptr);

    // add
    auto& fnAdd = static_cast<FunctionDecl&>(*prog.decls[3]);
    EXPECT_EQ(fnAdd.name, "add");
    EXPECT_FALSE(fnAdd.isExtern);
    EXPECT_TRUE(fnAdd.isExported);
    EXPECT_TRUE(fnAdd.body != nullptr);

    return true;
}

TAMARIN_TEST(Parser, ParsePointerTypesAndPrototypes) {
    SourceManager sm;
    auto fileId = sm.addSource("pointer_types.tmn",
        "extern \"C\" {\n"
        "    void* malloc(usize size);\n"
        "    void free(void* ptr);\n"
        "    int copyMem(*u8 dst, *u8 src, usize len);\n"
        "}\n"
    );
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);

    auto prog = p.parseProgram();
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.decls.size(), 3);

    auto& fnMalloc = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(fnMalloc.name, "malloc");
    EXPECT_TRUE(fnMalloc.returnType->kind == TypeNodeKind::Reference);

    auto& fnFree = static_cast<FunctionDecl&>(*prog.decls[1]);
    EXPECT_EQ(fnFree.name, "free");
    EXPECT_EQ(fnFree.params.size(), 1);
    EXPECT_TRUE(fnFree.params[0].type->kind == TypeNodeKind::Reference);

    auto& fnCopy = static_cast<FunctionDecl&>(*prog.decls[2]);
    EXPECT_EQ(fnCopy.name, "copyMem");
    EXPECT_EQ(fnCopy.params.size(), 3);
    EXPECT_TRUE(fnCopy.params[0].type->kind == TypeNodeKind::Reference);
    EXPECT_TRUE(fnCopy.params[1].type->kind == TypeNodeKind::Reference);

    return true;
}

TAMARIN_TEST(Parser, ParseCfgDirectives) {
    SourceManager sm;
    auto fileId = sm.addSource("cfg_test.tmn",
        "@cfg(target_os = \"windows\")\n"
        "import std.os.windows;\n"
        "\n"
        "@cfg(target_os = \"linux\")\n"
        "import std.os.linux;\n"
        "\n"
        "@cfg(target_os = \"windows\")\n"
        "int winOnly() { return 1; }\n"
        "\n"
        "@cfg(target_os = \"linux\")\n"
        "int linuxOnly() { return 2; }\n"
        "\n"
        "@cfg(any(target_os = \"windows\", target_os = \"linux\"))\n"
        "int winOrLinux() { return 3; }\n"
        "\n"
        "@cfg(all(target_os = \"linux\", target_arch = \"x86_64\"))\n"
        "int linuxX64Only() { return 4; }\n"
        "\n"
        "@cfg(not(target_os = \"windows\"))\n"
        "int notWindows() { return 5; }\n"
    );

    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    p.setTargetTriple(target::TargetTriple::parse("x86_64-pc-windows-msvc"));

    auto prog = p.parseProgram();
    EXPECT_FALSE(diag.hasErrors());

    // On Windows target:
    // imports: only std.os.windows
    EXPECT_EQ(prog.imports.size(), 1);
    EXPECT_EQ(prog.imports[0].modulePath, "std.os.windows");

    // decls: winOnly (1), winOrLinux (3) -> total 2 decls
    EXPECT_EQ(prog.decls.size(), 2);
    auto& f0 = static_cast<FunctionDecl&>(*prog.decls[0]);
    EXPECT_EQ(f0.name, "winOnly");
    auto& f1 = static_cast<FunctionDecl&>(*prog.decls[1]);
    EXPECT_EQ(f1.name, "winOrLinux");

    return true;
}



