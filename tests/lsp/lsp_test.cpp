// ============================================================================
// lsp_test.cpp - Unit tests for tamarin-lsp JSON, Protocol, Transport, Server
// ============================================================================

#include "test_harness.h"
#include "lsp/json.h"
#include "lsp/protocol.h"
#include "lsp/transport.h"
#include "lsp/server.h"

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
