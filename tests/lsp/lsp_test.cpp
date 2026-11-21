// ============================================================================
// lsp_test.cpp - Unit tests for tamarin-lsp JSON, Protocol, Transport, Server
// ============================================================================

#include "test_harness.h"
#include "lsp/json.h"
#include "lsp/protocol.h"
#include "lsp/transport.h"
#include "lsp/server.h"
#include "lsp/vfs.h"
#include "tamarin/source/source_manager.h"
#include "tamarin/lexer/lexer.h"
#include "tamarin/parser/parser.h"
#include "tamarin/diagnostics/diagnostic_engine.h"

#include <sstream>

using namespace tamarin::lsp;

TAMARIN_TEST(Lsp, JsonPrimitives) {
    // Null
    auto jNull = JsonValue::parse("null");
    EXPECT_TRUE(jNull.has_value());
    EXPECT_TRUE(jNull->isNull());
    EXPECT_EQ(jNull->serialize(), "null");

    // Bool
    auto jTrue = JsonValue::parse("true");
    auto jFalse = JsonValue::parse("false");
    EXPECT_TRUE(jTrue.has_value() && jTrue->isBool() && jTrue->asBool());
    EXPECT_TRUE(jFalse.has_value() && jFalse->isBool() && !jFalse->asBool());

    // Numbers
    auto jInt = JsonValue::parse("12345");
    EXPECT_TRUE(jInt.has_value() && jInt->isInt());
    EXPECT_EQ(jInt->asInt(), 12345);

    auto jNeg = JsonValue::parse("-987");
    EXPECT_TRUE(jNeg.has_value() && jNeg->isInt());
    EXPECT_EQ(jNeg->asInt(), -987);

    auto jFloat = JsonValue::parse("3.14159");
    EXPECT_TRUE(jFloat.has_value() && jFloat->isFloat());
    EXPECT_TRUE(std::abs(jFloat->asFloat() - 3.14159) < 0.0001);

    // String with escapes
    auto jStr = JsonValue::parse("\"hello \\\"world\\\"\\n\\t\\\\\"");
    EXPECT_TRUE(jStr.has_value() && jStr->isString());
    EXPECT_EQ(jStr->asString(), "hello \"world\"\n\t\\");
    EXPECT_EQ(jStr->serialize(), "\"hello \\\"world\\\"\\n\\t\\\\\"");

    // Unicode escape \u0041 -> 'A'
    auto jUni = JsonValue::parse("\"\\u0041\\u0042\\u0043\"");
    EXPECT_TRUE(jUni.has_value() && jUni->isString());
    EXPECT_EQ(jUni->asString(), "ABC");

    return true;
}

TAMARIN_TEST(Lsp, JsonArraysAndObjects) {
    // Array
    auto jArr = JsonValue::parse("[1, 2, \"three\", true, null]");
    EXPECT_TRUE(jArr.has_value() && jArr->isArray());
    EXPECT_EQ(jArr->asArray().size(), 5);
    EXPECT_EQ((*jArr)[0].asInt(), 1);
    EXPECT_EQ((*jArr)[1].asInt(), 2);
    EXPECT_EQ((*jArr)[2].asString(), "three");
    EXPECT_TRUE((*jArr)[3].asBool());
    EXPECT_TRUE((*jArr)[4].isNull());

    // Object
    auto jObj = JsonValue::parse("{\"name\": \"tamarin\", \"version\": 18, \"enabled\": true}");
    EXPECT_TRUE(jObj.has_value() && jObj->isObject());
    EXPECT_TRUE(jObj->contains("name"));
    EXPECT_EQ((*jObj)["name"].asString(), "tamarin");
    EXPECT_EQ((*jObj)["version"].asInt(), 18);
    EXPECT_TRUE((*jObj)["enabled"].asBool());
    EXPECT_FALSE(jObj->contains("nonexistent"));

    // Builder API
    JsonValue built = JsonValue::object();
    built["key"] = "value";
    built["items"] = JsonValue::array();
    built["items"].push_back(10);
    built["items"].push_back(20);

    EXPECT_TRUE(built.isObject());
    EXPECT_EQ(built["key"].asString(), "value");
    EXPECT_EQ(built["items"].asArray().size(), 2);

    return true;
}

TAMARIN_TEST(Lsp, ProtocolSerialization) {
    Position pos{10, 5};
    JsonValue jPos = pos.toJson();
    Position restoredPos = Position::fromJson(jPos);
    EXPECT_EQ(pos, restoredPos);

    Range range{Position{1, 0}, Position{1, 10}};
    JsonValue jRange = range.toJson();
    Range restoredRange = Range::fromJson(jRange);
    EXPECT_EQ(range, restoredRange);
    EXPECT_TRUE(range.contains(Position{1, 5}));
    EXPECT_FALSE(range.contains(Position{2, 0}));

    Location loc{"file:///test.tam", range};
    JsonValue jLoc = loc.toJson();
    Location restoredLoc = Location::fromJson(jLoc);
    EXPECT_EQ(restoredLoc.uri, "file:///test.tam");
    EXPECT_EQ(restoredLoc.range, range);

    Diagnostic diag{range, DiagnosticSeverity::Error, "type mismatch", "tamarin", "E001"};
    JsonValue jDiag = diag.toJson();
    EXPECT_EQ(jDiag["severity"].asInt(), 1);
    EXPECT_EQ(jDiag["message"].asString(), "type mismatch");
    EXPECT_EQ(jDiag["code"].asString(), "E001");

    return true;
}

TAMARIN_TEST(Lsp, TransportFraming) {
    std::string body = "{\"jsonrpc\": \"2.0\"}";
    std::string framedData = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    std::istringstream in(framedData);
    std::ostringstream out;

    LspTransport transport(in, out);
    std::string err;
    auto msg = transport.readMessage(&err);
    EXPECT_TRUE(msg.has_value());
    EXPECT_EQ(*msg, body);

    // Writing
    std::string sendBody = "{\"result\": 42}";
    std::string expectedWritten = "Content-Length: " + std::to_string(sendBody.size()) + "\r\n\r\n" + sendBody;
    EXPECT_TRUE(transport.sendMessage(sendBody));
    std::string written = out.str();
    EXPECT_TRUE(written.find(expectedWritten) != std::string::npos);

    return true;
}

TAMARIN_TEST(Lsp, ServerLifecycle) {
    std::istringstream in("");
    std::ostringstream out;
    LspTransport transport(in, out);
    LspServer server(transport);

    EXPECT_EQ(server.state(), ServerState::Uninitialized);

    // 1. Request prior to initialize fails
    server.processMessage("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/hover\",\"params\":{}}");
    std::string out1 = out.str();
    EXPECT_TRUE(out1.find("\"error\":") != std::string::npos);
    EXPECT_TRUE(out1.find(std::to_string(kServerNotInitialized)) != std::string::npos);

    // 2. Initialize request succeeds and transitions to Initialized
    out.str("");
    server.processMessage("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"initialize\",\"params\":{}}");
    std::string out2 = out.str();
    EXPECT_EQ(server.state(), ServerState::Initialized);
    EXPECT_TRUE(out2.find("\"capabilities\":") != std::string::npos);
    EXPECT_TRUE(out2.find("\"tamarin-lsp\"") != std::string::npos);
    EXPECT_TRUE(out2.find("\"hoverProvider\":true") != std::string::npos);

    // 3. Duplicate initialize fails
    out.str("");
    server.processMessage("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"initialize\",\"params\":{}}");
    std::string out3 = out.str();
    EXPECT_TRUE(out3.find("\"error\":") != std::string::npos);

    // 4. Initialized notification
    server.processMessage("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");

    // 5. Shutdown request transitions to ShuttingDown
    out.str("");
    server.processMessage("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"shutdown\"}");
    std::string out5 = out.str();
    EXPECT_EQ(server.state(), ServerState::ShuttingDown);
    EXPECT_TRUE(out5.find("\"result\":null") != std::string::npos);

    // 6. Exit notification transitions to Exited
    server.processMessage("{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}");
    EXPECT_EQ(server.state(), ServerState::Exited);

    return true;
}

TAMARIN_TEST(Lsp, VfsDocumentLifecycle) {
    Vfs vfs;
    std::string uri = "file:///c:/project/main.tam";
    TextDocumentItem item{uri, "tamarin", 1, "int main() {\n    return 0;\n}\n"};

    EXPECT_FALSE(vfs.hasDocument(uri));
    vfs.openDocument(item);
    EXPECT_TRUE(vfs.hasDocument(uri));

    // Case-insensitivity & drive-letter normalization
    EXPECT_TRUE(vfs.hasDocument("file:///C:/project/main.tam"));

    auto doc = vfs.getDocument(uri);
    EXPECT_TRUE(doc.has_value());
    EXPECT_EQ(doc->version(), 1);
    EXPECT_EQ(doc->lineCount(), 4);
    EXPECT_EQ(doc->text(), item.text);

    // Update
    vfs.updateDocument(uri, 2, "int main() { return 42; }");
    doc = vfs.getDocument(uri);
    EXPECT_TRUE(doc.has_value());
    EXPECT_EQ(doc->version(), 2);
    EXPECT_EQ(doc->text(), "int main() { return 42; }");

    // Close
    vfs.closeDocument(uri);
    EXPECT_FALSE(vfs.hasDocument(uri));

    return true;
}

TAMARIN_TEST(Lsp, VfsUtf16PositionTranslation) {
    // UTF-8 string containing ASCII and an astral-plane emoji:
    // U+1F680 (🚀) is 4 bytes in UTF-8 (\xF0\x9F\x9A\x80) and 2 UTF-16 code units.
    std::string code = "let a = 10;\nlet s = \"hi \xF0\x9F\x9A\x80!\";\n";
    VfsDocument doc("file:///utf.tam", "tamarin", 1, code);

    // Line 0: "let a = 10;\n"
    EXPECT_EQ(doc.positionToOffset(Position{0, 0}), 0);
    EXPECT_EQ(doc.positionToOffset(Position{0, 4}), 4); // 'a'
    EXPECT_EQ(doc.offsetToPosition(4), (Position{0, 4}));

    // Line 1:
    // "let s = \"hi " has 12 ASCII chars (offsets 12..23, UTF-16 units 0..11)
    // 🚀 is at offset 24, UTF-16 units 12 and 13.
    // '!' is at offset 28 (24 + 4), UTF-16 unit 14.
    size_t line1Start = doc.positionToOffset(Position{1, 0});
    EXPECT_EQ(line1Start, 12);

    size_t bangOffset = doc.positionToOffset(Position{1, 14});
    EXPECT_EQ(bangOffset, 28);
    EXPECT_EQ(code[bangOffset], '!');

    Position bangPos = doc.offsetToPosition(bangOffset);
    EXPECT_EQ(bangPos.line, 1);
    EXPECT_EQ(bangPos.character, 14);

    return true;
}

TAMARIN_TEST(Lsp, VfsIncrementalChange) {
    Vfs vfs;
    std::string uri = "file:///change.tam";
    TextDocumentItem item{uri, "tamarin", 1, "int x = 10;\nint y = 20;\n"};
    vfs.openDocument(item);

    // Incremental change: replace "10" with "999"
    // "10" is on Line 0, character 8 to 10
    DidChangeTextDocumentParams changeParams;
    changeParams.textDocument.uri = uri;
    changeParams.textDocument.version = 2;

    TextDocumentContentChangeEvent ev;
    ev.range = Range{Position{0, 8}, Position{0, 10}};
    ev.text = "999";
    changeParams.contentChanges.push_back(ev);

    EXPECT_TRUE(vfs.applyChanges(changeParams));

    auto doc = vfs.getDocument(uri);
    EXPECT_TRUE(doc.has_value());
    EXPECT_EQ(doc->version(), 2);
    EXPECT_EQ(doc->text(), "int x = 999;\nint y = 20;\n");

    // Full document change (range is nullopt)
    DidChangeTextDocumentParams fullParams;
    fullParams.textDocument.uri = uri;
    fullParams.textDocument.version = 3;

    TextDocumentContentChangeEvent fullEv;
    fullEv.text = "int z = 100;\n";
    fullParams.contentChanges.push_back(fullEv);

    EXPECT_TRUE(vfs.applyChanges(fullParams));
    doc = vfs.getDocument(uri);
    EXPECT_TRUE(doc.has_value());
    EXPECT_EQ(doc->version(), 3);
    EXPECT_EQ(doc->text(), "int z = 100;\n");

    return true;
}

TAMARIN_TEST(Lsp, ParserFaultTolerance) {
    // A source file where the first function has an intentional syntax error,
    // but the second function is completely valid.
    std::string source =
        "int broken(int a {\n"
        "    return 1;\n"
        "}\n"
        "\n"
        "int valid_function() {\n"
        "    int b = 42;\n"
        "    return b;\n"
        "}\n";

    tamarin::SourceManager sm;
    auto fileId = sm.addSource("fault_test.tam", source);
    std::ostringstream errs;
    tamarin::DiagnosticEngine diag(sm, errs);
    tamarin::lexer::Lexer lex(sm, fileId, diag);
    tamarin::parser::Parser parser(lex, diag);

    auto prog = parser.parseProgram();

    // The parser should report errors on the broken function
    EXPECT_TRUE(diag.hasErrors());

    // The parser should have recovered and parsed valid_function
    bool foundValid = false;
    for (const auto& decl : prog.decls) {
        if (decl && decl->kind == tamarin::ast::DeclKind::Function) {
            auto& fn = static_cast<tamarin::ast::FunctionDecl&>(*decl);
            if (fn.name == "valid_function") {
                foundValid = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundValid);

    return true;
}

TAMARIN_TEST(Lsp, ServerDocumentSync) {
    std::istringstream in("");
    std::ostringstream out;
    LspTransport transport(in, out);
    LspServer server(transport);

    // Initialize server
    server.processMessage("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    server.processMessage("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");

    std::string uri = "file:///workspace/app.tam";
    EXPECT_FALSE(server.vfs().hasDocument(uri));

    // textDocument/didOpen
    std::string openMsg =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + uri + "\",\"languageId\":\"tamarin\",\"version\":1,\"text\":\"fn main() { return 0; }\"}"
        "}}";
    server.processMessage(openMsg);

    EXPECT_TRUE(server.vfs().hasDocument(uri));
    auto doc = server.vfs().getDocument(uri);
    EXPECT_TRUE(doc.has_value());
    EXPECT_EQ(doc->text(), "fn main() { return 0; }");

    // textDocument/didChange (full replacement)
    std::string changeMsg =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + uri + "\",\"version\":2},"
        "\"contentChanges\":[{\"text\":\"fn main() { return 100; }\"}]"
        "}}";
    server.processMessage(changeMsg);

    doc = server.vfs().getDocument(uri);
    EXPECT_TRUE(doc.has_value());
    EXPECT_EQ(doc->version(), 2);
    EXPECT_EQ(doc->text(), "fn main() { return 100; }");

    // textDocument/didClose
    std::string closeMsg =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + uri + "\"}"
        "}}";
    server.processMessage(closeMsg);
    EXPECT_FALSE(server.vfs().hasDocument(uri));

    return true;
}

TAMARIN_TEST(Lsp, DocumentAnalyzerCleanCode) {
    VfsDocument doc("file:///clean.tam", "tamarin", 1,
                    "int add(int a, int b) {\n"
                    "    return a + b;\n"
                    "}\n");
    DocumentAnalyzer analyzer;
    auto res = analyzer.analyze(doc);

    EXPECT_EQ(res.diagnostics.size(), 0);
    EXPECT_TRUE(res.sema != nullptr);
    EXPECT_EQ(res.program.decls.size(), 1);

    return true;
}

TAMARIN_TEST(Lsp, DocumentAnalyzerSyntaxError) {
    VfsDocument doc("file:///syntax_err.tam", "tamarin", 1,
                    "int broken( {\n"
                    "    return 1;\n"
                    "}\n");
    DocumentAnalyzer analyzer;
    auto res = analyzer.analyze(doc);

    EXPECT_GE(res.diagnostics.size(), 1);
    EXPECT_EQ(res.diagnostics[0].severity, DiagnosticSeverity::Error);
    EXPECT_TRUE(res.diagnostics[0].message.find("expected") != std::string::npos ||
                res.diagnostics[0].message.find("error") != std::string::npos);

    return true;
}

TAMARIN_TEST(Lsp, DocumentAnalyzerTypeMismatch) {
    VfsDocument doc("file:///type_err.tam", "tamarin", 1,
                    "int compute() {\n"
                    "    int x = true;\n"
                    "    return x;\n"
                    "}\n");
    DocumentAnalyzer analyzer;
    auto res = analyzer.analyze(doc);

    EXPECT_GE(res.diagnostics.size(), 1);
    bool foundTypeErr = false;
    for (const auto& d : res.diagnostics) {
        if (d.severity == DiagnosticSeverity::Error) {
            foundTypeErr = true;
            break;
        }
    }
    EXPECT_TRUE(foundTypeErr);

    return true;
}

TAMARIN_TEST(Lsp, DocumentAnalyzerBorrowCheck) {
    // Attempt simultaneous active mutable borrows
    VfsDocument doc("file:///borrow_err.tam", "tamarin", 1,
                    "int test_borrow() {\n"
                    "    int x = 10;\n"
                    "    *int p1 = &x;\n"
                    "    *int p2 = &x;\n"
                    "    *p1 = 20;\n"
                    "    *p2 = 30;\n"
                    "    return x;\n"
                    "}\n");
    DocumentAnalyzer analyzer;
    auto res = analyzer.analyze(doc);

    EXPECT_GE(res.diagnostics.size(), 1);
    bool foundBorrowErr = false;
    for (const auto& d : res.diagnostics) {
        if (d.severity == DiagnosticSeverity::Error &&
            (d.message.find("borrow") != std::string::npos ||
             d.message.find("alias") != std::string::npos ||
             d.message.find("mutable") != std::string::npos)) {
            foundBorrowErr = true;
            break;
        }
    }
    EXPECT_TRUE(foundBorrowErr);

    return true;
}

TAMARIN_TEST(Lsp, ServerPublishDiagnosticsFlow) {
    std::istringstream in("");
    std::ostringstream out;
    LspTransport transport(in, out);
    LspServer server(transport);

    // Initialize
    server.processMessage("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
    server.processMessage("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");

    out.str(""); // clear init responses

    // 1. Open document with type mismatch -> should emit textDocument/publishDiagnostics with error
    std::string uri = "file:///diag_test.tam";
    std::string openMsg =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + uri + "\",\"languageId\":\"tamarin\",\"version\":1,"
        "\"text\":\"int foo() { int x = true; return x; }\"}"
        "}}";
    server.processMessage(openMsg);

    std::string outOpen = out.str();
    EXPECT_TRUE(outOpen.find("textDocument/publishDiagnostics") != std::string::npos);
    EXPECT_TRUE(outOpen.find(uri) != std::string::npos);
    EXPECT_TRUE(outOpen.find("\"severity\":1") != std::string::npos); // Error

    // 2. Change document to valid code -> should emit publishDiagnostics with empty array
    out.str("");
    std::string fixMsg =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + uri + "\",\"version\":2},"
        "\"contentChanges\":[{\"text\":\"int foo() { int x = 42; return x; }\"}]"
        "}}";
    server.processMessage(fixMsg);

    std::string outFix = out.str();
    EXPECT_TRUE(outFix.find("textDocument/publishDiagnostics") != std::string::npos);
    EXPECT_TRUE(outFix.find("\"diagnostics\":[]") != std::string::npos); // Cleared squiggles!

    // 3. Close document -> emits empty diagnostics to clear
    out.str("");
    std::string closeMsg =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + uri + "\"}"
        "}}";
    server.processMessage(closeMsg);

    std::string outClose = out.str();
    EXPECT_TRUE(outClose.find("textDocument/publishDiagnostics") != std::string::npos);
    EXPECT_TRUE(outClose.find("\"diagnostics\":[]") != std::string::npos);

    return true;
}
