// ============================================================================
// protocol.h - LSP 3.17 Protocol types & JSON serialization
// ============================================================================
#pragma once

#include "lsp/json.h"

#include <string>
#include <vector>
#include <optional>

namespace tamarin::lsp {

// ── Basic Geometric & Textual Types ─────────────────────────────────────────

struct Position {
    int line = 0;      // 0-based
    int character = 0; // 0-based UTF-16 code unit offset

    bool operator==(const Position& other) const noexcept {
        return line == other.line && character == other.character;
    }
    bool operator<(const Position& other) const noexcept {
        if (line != other.line) return line < other.line;
        return character < other.character;
    }

    JsonValue toJson() const {
        return JsonValue{
            {"line", line},
            {"character", character}
        };
    }

    static Position fromJson(const JsonValue& j) {
        Position pos;
        pos.line = static_cast<int>(j.getOrDefault("line", 0).asInt());
        pos.character = static_cast<int>(j.getOrDefault("character", 0).asInt());
        return pos;
    }
};

struct Range {
    Position start;
    Position end;

    bool operator==(const Range& other) const noexcept {
        return start == other.start && end == other.end;
    }

    bool contains(const Position& pos) const noexcept {
        if (pos < start) return false;
        if (end < pos) return false;
        return true;
    }

    JsonValue toJson() const {
        return JsonValue{
            {"start", start.toJson()},
            {"end", end.toJson()}
        };
    }

    static Range fromJson(const JsonValue& j) {
        Range r;
        if (const auto* s = j.get("start")) r.start = Position::fromJson(*s);
        if (const auto* e = j.get("end")) r.end = Position::fromJson(*e);
        return r;
    }
};

struct Location {
    std::string uri;
    Range range;

    JsonValue toJson() const {
        return JsonValue{
            {"uri", uri},
            {"range", range.toJson()}
        };
    }

    static Location fromJson(const JsonValue& j) {
        Location loc;
        loc.uri = j.getOrDefault("uri", "").asString();
        if (const auto* r = j.get("range")) loc.range = Range::fromJson(*r);
        return loc;
    }
};

// ── Diagnostics ─────────────────────────────────────────────────────────────

enum class DiagnosticSeverity : int {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

struct Diagnostic {
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string source = "tamarin";
    std::string code;

    JsonValue toJson() const {
        JsonValue j = JsonValue::object();
        j["range"] = range.toJson();
        j["severity"] = static_cast<int>(severity);
        j["message"] = message;
        j["source"] = source;
        if (!code.empty()) j["code"] = code;
        return j;
    }
};

// ── Document Identifiers & Content Changes ──────────────────────────────────

struct TextDocumentIdentifier {
    std::string uri;

    static TextDocumentIdentifier fromJson(const JsonValue& j) {
        return TextDocumentIdentifier{j.getOrDefault("uri", "").asString()};
    }
};

struct VersionedTextDocumentIdentifier {
    std::string uri;
    int version = 0;

    static VersionedTextDocumentIdentifier fromJson(const JsonValue& j) {
        VersionedTextDocumentIdentifier v;
        v.uri = j.getOrDefault("uri", "").asString();
        v.version = static_cast<int>(j.getOrDefault("version", 0).asInt());
        return v;
    }
};

struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    int version = 0;
    std::string text;

    static TextDocumentItem fromJson(const JsonValue& j) {
        TextDocumentItem item;
        item.uri = j.getOrDefault("uri", "").asString();
        item.languageId = j.getOrDefault("languageId", "").asString();
        item.version = static_cast<int>(j.getOrDefault("version", 0).asInt());
        item.text = j.getOrDefault("text", "").asString();
        return item;
    }
};

struct TextDocumentContentChangeEvent {
    std::optional<Range> range;
    std::string text;

    static TextDocumentContentChangeEvent fromJson(const JsonValue& j) {
        TextDocumentContentChangeEvent ev;
        if (const auto* r = j.get("range")) {
            ev.range = Range::fromJson(*r);
        }
        ev.text = j.getOrDefault("text", "").asString();
        return ev;
    }
};

struct DidOpenTextDocumentParams {
    TextDocumentItem textDocument;

    static DidOpenTextDocumentParams fromJson(const JsonValue& j) {
        DidOpenTextDocumentParams p;
        if (const auto* td = j.get("textDocument")) {
            p.textDocument = TextDocumentItem::fromJson(*td);
        }
        return p;
    }
};

struct DidChangeTextDocumentParams {
    VersionedTextDocumentIdentifier textDocument;
    std::vector<TextDocumentContentChangeEvent> contentChanges;

    static DidChangeTextDocumentParams fromJson(const JsonValue& j) {
        DidChangeTextDocumentParams p;
        if (const auto* td = j.get("textDocument")) {
            p.textDocument = VersionedTextDocumentIdentifier::fromJson(*td);
        }
        if (const auto* cc = j.get("contentChanges")) {
            if (cc->isArray()) {
                for (const auto& item : cc->asArray()) {
                    p.contentChanges.push_back(TextDocumentContentChangeEvent::fromJson(item));
                }
            }
        }
        return p;
    }
};

struct DidCloseTextDocumentParams {
    TextDocumentIdentifier textDocument;

    static DidCloseTextDocumentParams fromJson(const JsonValue& j) {
        DidCloseTextDocumentParams p;
        if (const auto* td = j.get("textDocument")) {
            p.textDocument = TextDocumentIdentifier::fromJson(*td);
        }
        return p;
    }
};

// ── Navigation & Intelligence ───────────────────────────────────────────────

struct Hover {
    std::string contents; // Markdown content
    std::optional<Range> range;

    JsonValue toJson() const {
        JsonValue j = JsonValue::object();
        j["contents"] = JsonValue{
            {"kind", "markdown"},
            {"value", contents}
        };
        if (range) j["range"] = range->toJson();
        return j;
    }
};

enum class CompletionItemKind : int {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25
};

struct CompletionItem {
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Text;
    std::string detail;
    std::string documentation;
    std::string insertText;

    JsonValue toJson() const {
        JsonValue j = JsonValue::object();
        j["label"] = label;
        j["kind"] = static_cast<int>(kind);
        if (!detail.empty()) j["detail"] = detail;
        if (!documentation.empty()) {
            j["documentation"] = JsonValue{
                {"kind", "markdown"},
                {"value", documentation}
            };
        }
        if (!insertText.empty()) j["insertText"] = insertText;
        return j;
    }
};

enum class SymbolKind : int {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    String = 15,
    Number = 16,
    Boolean = 17,
    Array = 18,
    Object = 19,
    Key = 20,
    Null = 21,
    EnumMember = 22,
    Struct = 23,
    Event = 24,
    Operator = 25,
    TypeParameter = 26
};

struct DocumentSymbol {
    std::string name;
    std::string detail;
    SymbolKind kind = SymbolKind::Variable;
    Range range;
    Range selectionRange;
    std::vector<DocumentSymbol> children;

    JsonValue toJson() const {
        JsonValue j = JsonValue::object();
        j["name"] = name;
        if (!detail.empty()) j["detail"] = detail;
        j["kind"] = static_cast<int>(kind);
        j["range"] = range.toJson();
        j["selectionRange"] = selectionRange.toJson();
        if (!children.empty()) {
            JsonValue ch = JsonValue::array();
            for (const auto& c : children) ch.push_back(c.toJson());
            j["children"] = std::move(ch);
        }
        return j;
    }
};

// ── JSON-RPC 2.0 Request / Response / Notification ──────────────────────────

struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    JsonValue id; // Int or String
    std::string method;
    JsonValue params;

    bool isNotification() const noexcept {
        return id.isNull();
    }

    static std::optional<JsonRpcRequest> fromJson(const JsonValue& j) {
        if (!j.isObject()) return std::nullopt;
        const auto* m = j.get("method");
        if (!m || !m->isString()) return std::nullopt;

        JsonRpcRequest req;
        req.method = m->asString();
        if (const auto* pid = j.get("id")) {
            req.id = *pid;
        }
        if (const auto* p = j.get("params")) {
            req.params = *p;
        }
        return req;
    }
};

struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    JsonValue id;
    std::optional<JsonValue> result;
    std::optional<JsonValue> error;

    JsonValue toJson() const {
        JsonValue j = JsonValue::object();
        j["jsonrpc"] = jsonrpc;
        j["id"] = id;
        if (error) {
            j["error"] = *error;
        } else if (result) {
            j["result"] = *result;
        } else {
            j["result"] = nullptr;
        }
        return j;
    }

    static JsonRpcResponse makeResult(JsonValue id, JsonValue result) {
        JsonRpcResponse resp;
        resp.id = std::move(id);
        resp.result = std::move(result);
        return resp;
    }

    static JsonRpcResponse makeError(JsonValue id, int code, std::string message, JsonValue data = JsonValue()) {
        JsonRpcResponse resp;
        resp.id = std::move(id);
        JsonValue err = JsonValue::object();
        err["code"] = code;
        err["message"] = std::move(message);
        if (!data.isNull()) err["data"] = std::move(data);
        resp.error = std::move(err);
        return resp;
    }
};

struct JsonRpcNotification {
    std::string jsonrpc = "2.0";
    std::string method;
    JsonValue params;

    JsonValue toJson() const {
        JsonValue j = JsonValue::object();
        j["jsonrpc"] = jsonrpc;
        j["method"] = method;
        j["params"] = params;
        return j;
    }

    static JsonRpcNotification make(std::string method, JsonValue params) {
        JsonRpcNotification notif;
        notif.method = std::move(method);
        notif.params = std::move(params);
        return notif;
    }
};

// Standard JSON-RPC Error Codes
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;
inline constexpr int kServerNotInitialized = -32002;

} // namespace tamarin::lsp
