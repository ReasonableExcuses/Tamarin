// ============================================================================
// module_test.cpp - Unit and integration tests for Tamarin static module system
// ============================================================================

#include "test_harness.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"
#include "tamarin/sema/sema.h"
#include "tamarin/driver/module_graph.h"
#include "tamarin/driver/driver.h"
#include "tamarin/ir/ir_builder.h"
#include "tamarin/backend/llvm_codegen.h"
#include "tamarin/backend/llvm_verifier.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace tamarin;

static void writeTestFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

// ── 1. Lexer & Parser Tests ──────────────────────────────────────────────────

TAMARIN_TEST(Modules, ParseImportBasic) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "import math.vector;\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.imports.size(), 1);
    EXPECT_EQ(prog.imports[0].modulePath, "math.vector");
    EXPECT_EQ(prog.imports[0].alias, "vector");
    return true;
}

TAMARIN_TEST(Modules, ParseImportWithAlias) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "import math.vector as vec;\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.imports.size(), 1);
    EXPECT_EQ(prog.imports[0].modulePath, "math.vector");
    EXPECT_EQ(prog.imports[0].alias, "vec");
    return true;
}

TAMARIN_TEST(Modules, ParseMultipleImports) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "import math.vector as vec;\n"
        "import std.io;\n"
        "import data.tree as dt;\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(prog.imports.size(), 3);
    EXPECT_EQ(prog.imports[0].alias, "vec");
    EXPECT_EQ(prog.imports[1].alias, "io");
    EXPECT_EQ(prog.imports[2].alias, "dt");
    return true;
}

TAMARIN_TEST(Modules, RejectImportAfterDecl) {
    SourceManager sm;
    auto fileId = sm.addSource("test.tmn",
        "int helper() { return 1; }\n"
        "import math.vector;\n"
        "int main() { return 0; }\n");
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);
    lexer::Lexer lex(sm, fileId, diag);
    parser::Parser p(lex, diag);
    auto prog = p.parseProgram();

    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("import declarations must appear at the beginning of the file") != std::string::npos);
    return true;
}

// ── 2. Module Graph & Cycle Detection ────────────────────────────────────────

TAMARIN_TEST(Modules, GraphResolveModulePath) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_graph_resolve";
    std::filesystem::remove_all(tempDir);

    auto mathPath = tempDir / "math" / "vector.tmn";
    writeTestFile(mathPath, "public int add(int a, int b) { return a + b; }\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import math.vector;\n"
        "int main() { return vector::add(2, 3); }\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);
    EXPECT_FALSE(diag.hasErrors());

    const auto& topo = graph.getTopologicalOrder();
    EXPECT_EQ(topo.size(), 2);
    // Dependencies first: math.vector, then main
    EXPECT_EQ(topo[0]->id.logicalName, "math.vector");
    EXPECT_EQ(topo[1]->id.logicalName, "main");

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, GraphRejectTraversal) {
    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { "." });
    auto res1 = graph.resolveModulePath("math..vector", SourceLocation{});
    EXPECT_FALSE(res1.has_value());
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("invalid module path") != std::string::npos);

    auto res2 = graph.resolveModulePath("math/vector", SourceLocation{});
    EXPECT_FALSE(res2.has_value());
    return true;
}

TAMARIN_TEST(Modules, GraphCircularDependency) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_cycle";
    std::filesystem::remove_all(tempDir);

    auto aPath = tempDir / "a.tmn";
    auto bPath = tempDir / "b.tmn";
    auto mainPath = tempDir / "main.tmn";

    writeTestFile(aPath, "import b;\n public int fa() { return 1; }\n");
    writeTestFile(bPath, "import a;\n public int fb() { return 2; }\n");
    writeTestFile(mainPath, "import a;\n int main() { return a::fa(); }\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    EXPECT_FALSE(graph.buildGraph(mainPath.string()));
    EXPECT_TRUE(diag.hasErrors());

    auto err = errs.str();
    EXPECT_TRUE(err.find("circular module dependency detected") != std::string::npos);
    EXPECT_TRUE(err.find("imports") != std::string::npos);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, GraphDiamondDependency) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_diamond";
    std::filesystem::remove_all(tempDir);

    auto dPath = tempDir / "d.tmn";
    auto bPath = tempDir / "b.tmn";
    auto cPath = tempDir / "c.tmn";
    auto mainPath = tempDir / "main.tmn";

    writeTestFile(dPath, "public int get() { return 10; }\n");
    writeTestFile(bPath, "import d;\n public int getB() { return d::get() + 1; }\n");
    writeTestFile(cPath, "import d;\n public int getC() { return d::get() + 2; }\n");
    writeTestFile(mainPath, "import b;\n import c;\n int main() { return b::getB() + c::getC(); }\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);
    EXPECT_FALSE(diag.hasErrors());

    const auto& topo = graph.getTopologicalOrder();
    EXPECT_EQ(topo.size(), 4);
    // d must be first before b and c, and main last
    EXPECT_EQ(topo[0]->id.logicalName, "d");
    EXPECT_EQ(topo[3]->id.logicalName, "main");

    std::filesystem::remove_all(tempDir);
    return true;
}

// ── 3. Semantic Analysis Tests ───────────────────────────────────────────────

TAMARIN_TEST(Modules, SemaPublicFunctionAccessible) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_sema_pub";
    std::filesystem::remove_all(tempDir);

    auto mPath = tempDir / "calc.tmn";
    writeTestFile(mPath, "public int add(int a, int b) { return a + b; }\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import calc;\n"
        "int main() { return calc::add(3, 4); }\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyzeModules(graph.getTopologicalOrder()));
    EXPECT_FALSE(diag.hasErrors());

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, SemaPrivateFunctionInaccessible) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_sema_priv";
    std::filesystem::remove_all(tempDir);

    auto mPath = tempDir / "calc.tmn";
    // Not marked public -> private by default
    writeTestFile(mPath, "int add(int a, int b) { return a + b; }\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import calc;\n"
        "int main() { return calc::add(3, 4); }\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyzeModules(graph.getTopologicalOrder()));
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("is private to module") != std::string::npos);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, SemaConflictingAlias) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_sema_conflict";
    std::filesystem::remove_all(tempDir);

    auto aPath = tempDir / "mod_a.tmn";
    auto bPath = tempDir / "mod_b.tmn";
    auto mainPath = tempDir / "main.tmn";

    writeTestFile(aPath, "public int f() { return 1; }\n");
    writeTestFile(bPath, "public int g() { return 2; }\n");
    writeTestFile(mainPath,
        "import mod_a as x;\n"
        "import mod_b as x;\n"
        "int main() { return x::f(); }\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    EXPECT_FALSE(graph.buildGraph(mainPath.string()));
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("conflicting import alias") != std::string::npos);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, SemaUnknownAlias) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_sema_unk";
    std::filesystem::remove_all(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "int main() { return foo::bar(); }\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_FALSE(sema.analyzeModules(graph.getTopologicalOrder()));
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_TRUE(errs.str().find("unknown module alias or namespace 'foo'") != std::string::npos);

    std::filesystem::remove_all(tempDir);
    return true;
}

// ── 4. End-to-End Compilation & Execution Tests ──────────────────────────────

TAMARIN_TEST(Modules, CodeGenCrossModuleFunctionCall) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_cg_fn";
    std::filesystem::remove_all(tempDir);

    auto mathPath = tempDir / "math.tmn";
    writeTestFile(mathPath,
        "public int add(int a, int b) {\n"
        "    return a + b;\n"
        "}\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import math;\n"
        "int main() {\n"
        "    return math::add(40, 2);\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyzeModules(graph.getTopologicalOrder()));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "cg_fn.ll";
    auto exePath = tempDir / "cg_fn.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 42);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, CodeGenCrossModuleExplicitAlias) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_cg_alias";
    std::filesystem::remove_all(tempDir);

    auto mathPath = tempDir / "advanced" / "math.tmn";
    writeTestFile(mathPath,
        "public int multiply(int a, int b) {\n"
        "    return a * b;\n"
        "}\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import advanced.math as m;\n"
        "int main() {\n"
        "    return m::multiply(6, 7);\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyzeModules(graph.getTopologicalOrder()));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "cg_alias.ll";
    auto exePath = tempDir / "cg_alias.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 42);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, CodeGenCrossModuleStructMethod) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_cg_struct";
    std::filesystem::remove_all(tempDir);

    auto vecPath = tempDir / "math" / "vector.tmn";
    writeTestFile(vecPath,
        "public struct Vec2 {\n"
        "    int x;\n"
        "    int y;\n"
        "    int sum() const {\n"
        "        return x + y;\n"
        "    }\n"
        "    void scale(int factor) {\n"
        "        x = x * factor;\n"
        "        y = y * factor;\n"
        "    }\n"
        "};\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import math.vector as vec;\n"
        "int main() {\n"
        "    vec::Vec2 v = { 10, 20 };\n"
        "    v.scale(2);\n"
        "    return v.sum();\n" // (10*2) + (20*2) = 60
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyzeModules(graph.getTopologicalOrder()));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "cg_struct.ll";
    auto exePath = tempDir / "cg_struct.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 60);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, CodeGenCrossModuleInheritance) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_cg_inherit";
    std::filesystem::remove_all(tempDir);

    auto baseDir = tempDir / "entities" / "base.tmn";
    writeTestFile(baseDir,
        "public class BaseEntity {\n"
        "public:\n"
        "    int id;\n"
        "    virtual int score() {\n"
        "        return 10;\n"
        "    }\n"
        "};\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import entities.base;\n"
        "class Player : public base::BaseEntity {\n"
        "public:\n"
        "    int bonus;\n"
        "    int score() override {\n"
        "        return 50;\n"
        "    }\n"
        "};\n"
        "int getEntityScore(base::BaseEntity& e) {\n"
        "    return e.score();\n"
        "}\n"
        "int main() {\n"
        "    Player p;\n"
        "    return getEntityScore(p);\n" // Polymorphic call dispatches to Player::score -> 50
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyzeModules(graph.getTopologicalOrder()));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "cg_inherit.ll";
    auto exePath = tempDir / "cg_inherit.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 50);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, CodeGenCrossModuleReferenceMutation) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_cg_ref";
    std::filesystem::remove_all(tempDir);

    auto mPath = tempDir / "ops.tmn";
    writeTestFile(mPath,
        "public struct Counter {\n"
        "    int count;\n"
        "};\n"
        "public void increment(Counter& c, int delta) {\n"
        "    c.count += delta;\n"
        "}\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import ops;\n"
        "int main() {\n"
        "    ops::Counter c = { 10 };\n"
        "    ops::increment(c, 15);\n"
        "    return c.count;\n" // 10 + 15 = 25
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyzeModules(graph.getTopologicalOrder()));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "cg_ref.ll";
    auto exePath = tempDir / "cg_ref.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 25);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, CodeGenCrossModuleSoA) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_cg_soa";
    std::filesystem::remove_all(tempDir);

    auto pPath = tempDir / "particles.tmn";
    writeTestFile(pPath,
        "@layout(soa)\n"
        "public struct Particle {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n");

    auto mainPath = tempDir / "main.tmn";
    writeTestFile(mainPath,
        "import particles;\n"
        "int main() {\n"
        "    particles::Particle parts[4];\n"
        "    parts[2].x = 42;\n"
        "    parts[2].y = 58;\n"
        "    return parts[2].x + parts[2].y;\n" // 42 + 58 = 100
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    EXPECT_TRUE(sema.analyzeModules(graph.getTopologicalOrder()));

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "cg_soa.ll";
    auto exePath = tempDir / "cg_soa.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 100);

    std::filesystem::remove_all(tempDir);
    return true;
}

TAMARIN_TEST(Modules, DriverIncludeDirsAndBuild) {
    auto baseTemp = std::filesystem::temp_directory_path() / "tamarin_test_driver_inc";
    std::filesystem::remove_all(baseTemp);

    auto extDir = baseTemp / "external_libs";
    auto libFile = extDir / "math" / "util.tmn";
    writeTestFile(libFile, "public int answer() { return 42; }\n");

    auto projectDir = baseTemp / "my_project";
    auto mainFile = projectDir / "main.tmn";
    writeTestFile(mainFile,
        "import math.util;\n"
        "int main() { return util::answer(); }\n");

    driver::DriverOptions opts;
    opts.command = driver::DriverOptions::Command::Build;
    opts.inputFile = mainFile.string();
    opts.outputFile = (projectDir / "main.exe").string();
    opts.includeDirs.push_back(extDir.string()); // -I <extDir>

    driver::Driver drv(std::move(opts));
    int res = drv.build();
    EXPECT_EQ(res, 0);

    std::string cmd = "\"" + (projectDir / "main.exe").string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 42);

    std::filesystem::remove_all(baseTemp);
    return true;
}
