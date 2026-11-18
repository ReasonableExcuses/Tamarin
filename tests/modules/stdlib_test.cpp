// ============================================================================
// stdlib_test.cpp - Tests for Tamarin Standard Library, I/O, Strings & Files
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

static void writeStdlibTestFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

static void safeRemoveAll(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
}

static std::string getStdlibDir() {
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
    return (std::filesystem::current_path() / "lib").string();
}

// ── 1. String Literals & Len ────────────────────────────────────────────────

TAMARIN_TEST(Stdlib, StringLiteralsAndLen) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_str_lit";
    safeRemoveAll(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    char greeting[] = \"Hello, TAMARIN!\";\n"
        "    int length = (int)len(greeting);\n"
        "    int firstChar = (int)greeting[0];\n"
        "    return length + firstChar;\n" // 15 + 72 ('H') = 87
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "str_test.ll";
    auto exePath = tempDir / "str_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 87);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, BuiltinPrintlnAndPrint) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_print_exec";
    safeRemoveAll(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    print(\"Test print: \");\n"
        "    println(42);\n"
        "    println(3.14);\n"
        "    println(true);\n"
        "    println();\n"
        "    println(\"Hello, TAMARIN console output!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "print_test.ll";
    auto exePath = tempDir / "print_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ProceduralFileHandling) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_file_proc";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto filePath = tempDir / "sample.txt";
    std::string filePathStr = filePath.string();
    for (char& c : filePathStr) {
        if (c == '\\') c = '/';
    }

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    char path[] = \"" + filePathStr + "\";\n"
        "    int written = writeFile(path, \"Hello Tamarin Files!\");\n"
        "    if (written <= 0) { return 1; }\n"
        "    if (!fileExists(path)) { return 2; }\n"
        "    char buffer[64];\n"
        "    int readBytes = readFile(path, buffer, 64);\n"
        "    if (readBytes != written) { return 3; }\n"
        "    // buffer[0] should be 'H' (72)\n"
        "    if ((int)buffer[0] != 72) { return 4; }\n"
        "    removeFile(path);\n"
        "    if (fileExists(path)) { return 5; }\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "file_proc.ll";
    auto exePath = tempDir / "file_proc.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ImportStdIo) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_import_io";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto filePath = tempDir / "io_out.txt";
    std::string filePathStr = filePath.string();
    for (char& c : filePathStr) {
        if (c == '\\') c = '/';
    }

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.io;\n"
        "import std.file as file;\n"
        "int main() {\n"
        "    io::println(\"Hello through std.io!\");\n"
        "    char p[] = \"" + filePathStr + "\";\n"
        "    file::File f;\n"
        "    if (f.open(p, \"w\")) {\n"
        "        f.writeLine(\"Line written with file::File\");\n"
        "        f.close();\n"
        "    }\n"
        "    if (!io::fileExists(p)) { return 1; }\n"
        "    io::removeFile(p);\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "import_io.ll";
    auto exePath = tempDir / "import_io.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ImportStdMath) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_import_math";
    safeRemoveAll(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.math as math;\n"
        "int main() {\n"
        "    int a = math::abs(-15);\n"
        "    int b = math::min(10, 25);\n"
        "    int c = math::max(10, 25);\n"
        "    int p = math::ipow(2, 4);\n"
        "    // a=15, b=10, c=25, p=16 -> sum = 66\n"
        "    return a + b + c + p;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) {
        std::cerr << "buildGraph error: " << errs.str() << "\n";
    }
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "import_math.ll";
    auto exePath = tempDir / "import_math.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 66);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, GetInput) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_get_input";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto inPath = tempDir / "input.txt";
    writeStdlibTestFile(inPath, "TamarinInput\n");

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    char input[] = getInput();\n"
        "    int length = (int)len(input);\n"
        "    println(\"Read input line: \");\n"
        "    println(input);\n"
        "    return length;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "input_test.ll";
    auto exePath = tempDir / "input_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\" < \"" + inPath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 12); // "TamarinInput" length is 12

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, DedicatedFileModule) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_file_mod";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto filePath = tempDir / "mod_file.txt";
    std::string filePathStr = filePath.string();
    for (char& c : filePathStr) {
        if (c == '\\') c = '/';
    }

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.file as file;\n"
        "int main() {\n"
        "    char p[] = \"" + filePathStr + "\";\n"
        "    int w = file::write(p, \"Hello std.file module!\");\n"
        "    if (w <= 0) { return 1; }\n"
        "    if (!file::exists(p)) { return 2; }\n"
        "    file::remove(p);\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "file_mod.ll";
    auto exePath = tempDir / "file_mod.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, StringLiteralImmutabilityAndEscapes) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_str_escapes";
    safeRemoveAll(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    char empty[] = \"\";\n"
        "    if (len(empty) != 0) { return 1; }\n"
        "    char escapes[] = \"Line1\\nLine2\\tQuote:\\\"Slash:\\\\\";\n"
        "    int l = (int)len(escapes);\n"
        "    if (l <= 0) { return 2; }\n"
        "    char embNull[] = \"abc\\0def\";\n"
        "    if (len(embNull) != 7) { return 3; }\n"
        "    if ((int)embNull[0] != 97) { return 4; }\n" // 'a'
        "    if ((int)embNull[3] != 0) { return 5; }\n"  // '\0'
        "    if ((int)embNull[4] != 100) { return 6; }\n" // 'd'
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    EXPECT_TRUE(graph.buildGraph(mainPath.string()));

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "str_esc.ll";
    auto exePath = tempDir / "str_esc.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ConsoleOutputFormatStringSafety) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_fmt_safety";
    safeRemoveAll(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    println(\"%s %n %x %d %p\");\n"
        "    print(\"Literal: %s%d%n\");\n"
        "    println();\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    EXPECT_TRUE(graph.buildGraph(mainPath.string()));

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "fmt_safety.ll";
    auto exePath = tempDir / "fmt_safety.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, CallerOwnedGetInputMultipleCalls) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_multi_input";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto inPath = tempDir / "input.txt";
    writeStdlibTestFile(inPath, "FirstLine\r\nSecondLine\n");

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    char b1[64];\n"
        "    char b2[64];\n"
        "    int n1 = getInput(b1, 64);\n"
        "    int n2 = getInput(b2, 64);\n"
        "    if (n1 != 9) { return 1; }\n"
        "    if (n2 != 10) { return 2; }\n"
        "    // Verify both buffers are still valid and uncorrupted\n"
        "    if ((int)b1[0] != 70) { return 3; }\n" // 'F'
        "    if ((int)b2[0] != 83) { return 4; }\n" // 'S'
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    EXPECT_TRUE(graph.buildGraph(mainPath.string()));

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "multi_input.ll";
    auto exePath = tempDir / "multi_input.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\" < \"" + inPath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, FileRAIIDoubleCloseAndResourceSafety) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_file_raii_safety";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto filePath = tempDir / "raii_safe.txt";
    std::string filePathStr = filePath.string();
    for (char& c : filePathStr) {
        if (c == '\\') c = '/';
    }

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.file as file;\n"
        "int testScope(const char p[]) {\n"
        "    file::File f;\n"
        "    if (!f.open(p, \"w\")) { return 1; }\n"
        "    f.writeLine(\"Resource safe line\");\n"
        "    // Early return triggers destructor cleanup\n"
        "    return 0;\n"
        "}\n"
        "int main() {\n"
        "    char p[] = \"" + filePathStr + "\";\n"
        "    int r = testScope(p);\n"
        "    if (r != 0) { return 1; }\n"
        "    file::File f2;\n"
        "    // Repeated close on un-opened file must be safe\n"
        "    f2.close();\n"
        "    f2.close();\n"
        "    if (f2.open(p, \"r\")) {\n"
        "        char buf[64];\n"
        "        int n = f2.read(buf, 64);\n"
        "        if (n <= 0) { return 2; }\n"
        "        // Explicit close followed by scope destruction is safe\n"
        "        f2.close();\n"
        "        f2.close();\n"
        "    }\n"
        "    file::remove(p);\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    EXPECT_TRUE(graph.buildGraph(mainPath.string()));

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "raii_safe.ll";
    auto exePath = tempDir / "raii_safe.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, FileBufferBoundsSafety) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_file_bounds";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto filePath = tempDir / "bounds.txt";
    std::string filePathStr = filePath.string();
    for (char& c : filePathStr) {
        if (c == '\\') c = '/';
    }

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "int main() {\n"
        "    char p[] = \"" + filePathStr + "\";\n"
        "    writeFile(p, \"1234567890\");\n"
        "    char buf[16];\n"
        "    // Read with capacity 0: should read 0 bytes safely\n"
        "    int n0 = readFile(p, buf, 0);\n"
        "    if (n0 != 0) { return 1; }\n"
        "    // Read with capacity 4: should read at most 4 bytes safely\n"
        "    int n4 = readFile(p, buf, 4);\n"
        "    if (n4 != 4) { return 2; }\n"
        "    // Reading non-existent file must return -1 safely\n"
        "    int missing = readFile(\"non_existent_file.xyz\", buf, 16);\n"
        "    if (missing != -1) { return 3; }\n"
        "    removeFile(p);\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    EXPECT_TRUE(graph.buildGraph(mainPath.string()));

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "file_bounds.ll";
    auto exePath = tempDir / "file_bounds.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, StdMathEdgeCases) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_math_edge";
    std::error_code ec;
    safeRemoveAll(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.math as math;\n"
        "int main() {\n"
        "    // 1. abs(INT_MIN) saturation to INT_MAX\n"
        "    int absMin = math::abs(-2147483647 - 1);\n"
        "    if (absMin != 2147483647) { return 1; }\n"
        "    // 2. ipow zero exponent\n"
        "    if (math::ipow(7, 0) != 1) { return 2; }\n"
        "    // 3. ipow negative exponent\n"
        "    if (math::ipow(7, -2) != 0) { return 3; }\n"
        "    // 4. clamp with inverted bounds (low > high)\n"
        "    int cl = math::clamp(100, 50, 20);\n"
        "    if (cl != 50) { return 4; }\n"
        "    // 5. min and max\n"
        "    if (math::min(-50, -10) != -50) { return 5; }\n"
        "    if (math::max(-50, -10) != -10) { return 6; }\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    EXPECT_TRUE(graph.buildGraph(mainPath.string()));

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "math_edge.ll";
    auto exePath = tempDir / "math_edge.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "FileBufferBoundsSafety failed with ret=" << ret << "\n";
    }
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}



TAMARIN_TEST(Stdlib, ImportStdMem) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_import_mem";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.mem as mem;\n"
        "import std.io as io;\n"
        "int main() {\n"
        "    int s[] = mem::alloc<int>(4);\n"
        "    s[0] = 111;\n"
        "    s[1] = 222;\n"
        "    s[2] = 333;\n"
        "    s[3] = 444;\n"
        "    int d[] = mem::alloc<int>(4);\n"
        "    mem::copy<int>(d, 0, s, 0, 4);\n"
        "    if (d[0] != 111 || d[1] != 222 || d[2] != 333 || d[3] != 444) { return 1; }\n"
        "    mem::free<int>(s);\n"
        "    mem::free<int>(d);\n"
        "    io::println(\"std.mem success!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) std::cerr << "buildGraph error: " << errs.str() << "\n";
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "mem_test.ll";
    auto exePath = tempDir / "mem_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ImportStdCollectionsVec) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_import_vec";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.collections.vec as col;\n"
        "import std.io as io;\n"
        "int main() {\n"
        "    col::Vec<int> v;\n"
        "    v.init();\n"
        "    v.push(10);\n"
        "    v.push(20);\n"
        "    v.push(30);\n"
        "    v.push(40);\n"
        "    v.push(50);\n"
        "    if (v.size() != 5) { return 1; }\n"
        "    if (v[0] != 10) { return 2; }\n"
        "    if (v[4] != 50) { return 3; }\n"
        "    v[2] = 999;\n"
        "    if (v[2] != 999) { return 4; }\n"
        "    int p = v.pop();\n"
        "    if (p != 50) { return 5; }\n"
        "    if (v.size() != 4) { return 6; }\n"
        "    v.release();\n"
        "    io::println(\"std.collections.vec success!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) std::cerr << "buildGraph error: " << errs.str() << "\n";
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "vec_test.ll";
    auto exePath = tempDir / "vec_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ImportStdString) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_import_string";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.string as str;\n"
        "import std.io as io;\n"
        "int main() {\n"
        "    str::String s;\n"
        "    s.init_from(\"Tamarin\");\n"
        "    s.append(\" Language\");\n"
        "    if (s.length() != 16) { return 1; }\n"
        "    if (!s.starts_with(\"Tamarin\")) { return 2; }\n"
        "    if (!s.ends_with(\"Language\")) { return 3; }\n"
        "    str::String num;\n"
        "    num.init_from(\"12345\");\n"
        "    if (num.to_int() != 12345) { return 4; }\n"
        "    s.release();\n"
        "    num.release();\n"
        "    io::println(\"std.string success!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) std::cerr << "buildGraph error: " << errs.str() << "\n";
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "str_test.ll";
    auto exePath = tempDir / "str_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ImportStdAlgorithms) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_import_algo";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.algorithms as algo;\n"
        "import std.io as io;\n"
        "int main() {\n"
        "    int data[8];\n"
        "    data[0] = 50;\n"
        "    data[1] = 20;\n"
        "    data[2] = 80;\n"
        "    data[3] = 10;\n"
        "    data[4] = 90;\n"
        "    data[5] = 30;\n"
        "    data[6] = 70;\n"
        "    data[7] = 40;\n"
        "    algo::sort<int>(data);\n"
        "    if (data[0] != 10) { return 1; }\n"
        "    if (data[1] != 20) { return 2; }\n"
        "    if (data[2] != 30) { return 3; }\n"
        "    if (data[3] != 40) { return 4; }\n"
        "    if (data[4] != 50) { return 5; }\n"
        "    if (data[5] != 70) { return 6; }\n"
        "    if (data[6] != 80) { return 7; }\n"
        "    if (data[7] != 90) { return 8; }\n"
        "    int idx = algo::binary_search<int>(data, 70);\n"
        "    if (idx != 5) { return 9; }\n"
        "    int missing = algo::binary_search<int>(data, 999);\n"
        "    if (missing != -1) { return 10; }\n"
        "    io::println(\"std.algorithms success!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) std::cerr << "buildGraph error: " << errs.str() << "\n";
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "algo_test.ll";
    auto exePath = tempDir / "algo_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, ImportStdSync) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_import_sync";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.sync as sync;\n"
        "import std.io as io;\n"
        "int main() {\n"
        "    sync::Atomic<int> atom;\n"
        "    atom.init(100);\n"
        "    int v1 = atom.load();\n"
        "    if (v1 != 100) { return 1; }\n"
        "    atom.store(200);\n"
        "    if (atom.load() != 200) { return 2; }\n"
        "    int old_add = atom.fetch_add(50);\n"
        "    if (old_add != 200) { return 3; }\n"
        "    if (atom.load() != 250) { return 4; }\n"
        "    int old_sub = atom.fetch_sub(30);\n"
        "    if (old_sub != 250) { return 5; }\n"
        "    if (atom.load() != 220) { return 6; }\n"
        "    bool swapped = atom.compare_exchange(220, 999);\n"
        "    if (!swapped) { return 7; }\n"
        "    if (atom.load() != 999) { return 8; }\n"
        "    bool failed_swap = atom.compare_exchange(123, 456);\n"
        "    if (failed_swap) { return 9; }\n"
        "    if (atom.load() != 999) { return 10; }\n"
        "    sync::SpinLock sl;\n"
        "    sl.init();\n"
        "    sl.lock();\n"
        "    if (sl.state != 1) { return 11; }\n"
        "    sl.unlock();\n"
        "    if (sl.state != 0) { return 12; }\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(8);\n"
        "    ch.send(42);\n"
        "    ch.send(108);\n"
        "    int r1 = ch.recv();\n"
        "    int r2 = ch.recv();\n"
        "    if (r1 != 42 || r2 != 108) { return 13; }\n"
        "    ch.release();\n"
        "    io::println(\"std.sync success!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) std::cerr << "buildGraph error: " << errs.str() << "\n";
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "sync_test.ll";
    auto exePath = tempDir / "sync_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 0));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}


TAMARIN_TEST(Stdlib, LockFreeMPMCChannelConcurrency) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_mpmc_chan";
    safeRemoveAll(tempDir);
    std::filesystem::create_directories(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.sync as sync;\n"
        "import std.io as io;\n"
        "int main() {\n"
        "    sync::Channel<int> ch;\n"
        "    ch.init(16);\n"
        "    if (!ch.is_empty()) return 1;\n"
        "    if (ch.len() != 0) return 2;\n"
        "    int outVal = 0;\n"
        "    if (ch.try_recv(outVal)) return 3;\n"
        "    for (int i = 0; i < 16; i++) {\n"
        "        if (!ch.try_send(i * 10)) return 4;\n"
        "    }\n"
        "    if (!ch.is_full()) return 5;\n"
        "    if (ch.len() != 16) return 6;\n"
        "    if (ch.try_send(999)) return 7;\n"
        "    for (int i = 0; i < 16; i++) {\n"
        "        int v = 0;\n"
        "        if (!ch.try_recv(v)) return 8;\n"
        "        if (v != i * 10) return 9;\n"
        "    }\n"
        "    if (!ch.is_empty()) return 10;\n"
        "    if (ch.len() != 0) return 11;\n"
        "    sync::Channel<int> stream;\n"
        "    stream.init(32);\n"
        "    int totalReceived = 0;\n"
        "    long sumReceived = 0;\n"
        "    task_scope {\n"
        "        spawn {\n"
        "            for (int i = 0; i < 100; i++) stream.send(1);\n"
        "        }\n"
        "        spawn {\n"
        "            for (int i = 0; i < 100; i++) stream.send(2);\n"
        "        }\n"
        "        spawn {\n"
        "            for (int i = 0; i < 100; i++) stream.send(3);\n"
        "        }\n"
        "        spawn {\n"
        "            for (int i = 0; i < 100; i++) stream.send(4);\n"
        "        }\n"
        "        while (totalReceived < 400) {\n"
        "            int item = 0;\n"
        "            if (stream.try_recv(item)) {\n"
        "                sumReceived += (long)item;\n"
        "                totalReceived++;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    stream.release();\n"
        "    ch.release();\n"
        "    if (totalReceived != 400) return 12;\n"
        "    if (sumReceived != 1000) return 13;\n"
        "    io::println(\"MPMC channel verified!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) std::cerr << "buildGraph error: " << errs.str() << "\n";
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "mpmc_test.ll";
    auto exePath = tempDir / "mpmc_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}

TAMARIN_TEST(Stdlib, SelectMultiplexChannels) {
    auto tempDir = std::filesystem::temp_directory_path() / "tamarin_test_select_mux";
    std::filesystem::create_directories(tempDir);

    auto mainPath = tempDir / "main.tmn";
    writeStdlibTestFile(mainPath,
        "import std.sync as sync;\n"
        "import std.io as io;\n"
        "int main() {\n"
        "    sync::Channel<int> ch1;\n"
        "    sync::Channel<int> ch2;\n"
        "    ch1.init(16);\n"
        "    ch2.init(16);\n"
        "    int count1 = 0;\n"
        "    int count2 = 0;\n"
        "    task_scope {\n"
        "        spawn {\n"
        "            for (int i = 0; i < 50; i++) ch1.send(1);\n"
        "        }\n"
        "        spawn {\n"
        "            for (int i = 0; i < 50; i++) ch2.send(2);\n"
        "        }\n"
        "        while (count1 + count2 < 100) {\n"
        "            int v = 0;\n"
        "            select {\n"
        "                case ch1.try_recv(v):\n"
        "                    count1++;\n"
        "                case ch2.try_recv(v):\n"
        "                    count2++;\n"
        "                default:\n"
        "                    ;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    ch1.release();\n"
        "    ch2.release();\n"
        "    if (count1 != 50) return 1;\n"
        "    if (count2 != 50) return 2;\n"
        "    io::println(\"Select multiplex verified!\");\n"
        "    return 0;\n"
        "}\n");

    SourceManager sm;
    std::ostringstream errs;
    DiagnosticEngine diag(sm, errs);

    driver::ModuleGraph graph(sm, diag, { tempDir.string(), getStdlibDir() });
    bool bgOk = graph.buildGraph(mainPath.string());
    if (!bgOk) std::cerr << "buildGraph error: " << errs.str() << "\n";
    EXPECT_TRUE(bgOk);

    sema::Sema sema(diag);
    bool semaOk = sema.analyzeModules(graph.getTopologicalOrder());
    if (!semaOk) std::cerr << "sema error: " << errs.str() << "\n";
    EXPECT_TRUE(semaOk);

    ir::IRBuilder irBuilder(sema.checker());
    auto mod = irBuilder.buildProgram(graph.getTopologicalOrder(), "main");
    EXPECT_TRUE(mod != nullptr);

    backend::LLVMCodeGen codegen(*mod, diag);
    auto llPath  = tempDir / "select_test.ll";
    auto exePath = tempDir / "select_test.exe";

    EXPECT_TRUE(codegen.emitLLVMIRToFile(llPath));
    EXPECT_TRUE(codegen.compileToExecutable(llPath, exePath, 2));

    std::string cmd = "\"" + exePath.string() + "\"";
    int ret = runCommand(cmd);
    EXPECT_EQ(ret, 0);

    safeRemoveAll(tempDir);
    return true;
}
