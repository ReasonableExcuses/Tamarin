// ============================================================================
// server.cpp - Tamarin LSP Server implementation
// ============================================================================

#include "lsp/server.h"

#include <iostream>

namespace tamarin::lsp {

LspServer::LspServer(LspTransport& transport)
    : transport_(transport) {}

int LspServer::run() {
    while (state_ != ServerState::Exited) {
        std::string err;
        auto payload = transport_.readMessage(&err);
        if (!payload) {
            // EOF or unrecoverable transport error
            break;
        }
        processMessage(*payload);
    }
    return exitCode_;
}

bool LspServer::processMessage(std::string_view payload) {
    std::string err;
    auto jsonOpt = JsonValue::parse(payload, &err);
    if (!jsonOpt) {
        // Parse error
        JsonRpcResponse resp = JsonRpcResponse::makeError(JsonValue(), kParseError, "Parse error: " + err);
        return sendResponse(resp);
    }

    auto reqOpt = JsonRpcRequest::fromJson(*jsonOpt);
    if (!reqOpt) {
        // Invalid request
        JsonRpcResponse resp = JsonRpcResponse::makeError(JsonValue(), kInvalidRequest, "Invalid JSON-RPC 2.0 Request");
        return sendResponse(resp);
    }

    const auto& req = *reqOpt;
    if (req.isNotification()) {
        handleNotification(req);
        return true;
    } else {
        auto resp = handleRequest(req);
        if (resp) {
            return sendResponse(*resp);
        }
        return true;
    }
}

std::optional<JsonRpcResponse> LspServer::handleRequest(const JsonRpcRequest& req) {
    if (state_ == ServerState::Uninitialized) {
        if (req.method == "initialize") {
            return handleInitialize(req);
        }
        return JsonRpcResponse::makeError(req.id, kServerNotInitialized, "Server is not initialized yet");
    }

    if (state_ == ServerState::ShuttingDown) {
        return JsonRpcResponse::makeError(req.id, kInvalidRequest, "Server is shutting down");
    }

    if (req.method == "initialize") {
        return JsonRpcResponse::makeError(req.id, kInvalidRequest, "Server is already initialized");
    }
    if (req.method == "shutdown") {
        return handleShutdown(req);
    }
    if (req.method == "textDocument/hover") {
        return handleHover(req);
    }
    if (req.method == "textDocument/definition") {
        return handleDefinition(req);
    }
    if (req.method == "textDocument/references") {
        return handleReferences(req);
    }
    if (req.method == "textDocument/completion") {
        return handleCompletion(req);
    }
    if (req.method == "textDocument/documentSymbol") {
        return handleDocumentSymbol(req);
    }

    return JsonRpcResponse::makeError(req.id, kMethodNotFound, "Method '" + req.method + "' not found");
}

void LspServer::handleNotification(const JsonRpcRequest& notif) {
    if (notif.method == "exit") {
        handleExit(notif);
        return;
    }
    if (state_ == ServerState::Uninitialized) {
        // Ignore notifications prior to initialization
        return;
    }

    if (notif.method == "initialized") {
        handleInitialized(notif);
    } else if (notif.method == "textDocument/didOpen") {
        handleDidOpen(notif);
    } else if (notif.method == "textDocument/didChange") {
        handleDidChange(notif);
    } else if (notif.method == "textDocument/didClose") {
        handleDidClose(notif);
    } else if (notif.method == "textDocument/didSave") {
        handleDidSave(notif);
    }
}

// ── Specific Handlers ───────────────────────────────────────────────────────

JsonRpcResponse LspServer::handleInitialize(const JsonRpcRequest& req) {
    state_ = ServerState::Initialized;

    JsonValue capabilities = JsonValue::object();
    // TextDocumentSyncKind: 1 = Full
    capabilities["textDocumentSync"] = 1;
    capabilities["hoverProvider"] = true;
    capabilities["definitionProvider"] = true;
    capabilities["referencesProvider"] = true;
    capabilities["documentSymbolProvider"] = true;

    JsonValue completionProvider = JsonValue::object();
    completionProvider["resolveProvider"] = false;
    JsonValue triggerChars = JsonValue::array();
    triggerChars.push_back(".");
    triggerChars.push_back(":");
    completionProvider["triggerCharacters"] = std::move(triggerChars);
    capabilities["completionProvider"] = std::move(completionProvider);

    JsonValue serverInfo = JsonValue::object();
    serverInfo["name"] = "tamarin-lsp";
    serverInfo["version"] = "0.18.0";

    JsonValue result = JsonValue::object();
    result["capabilities"] = std::move(capabilities);
    result["serverInfo"] = std::move(serverInfo);

    return JsonRpcResponse::makeResult(req.id, std::move(result));
}

JsonRpcResponse LspServer::handleShutdown(const JsonRpcRequest& req) {
    state_ = ServerState::ShuttingDown;
    return JsonRpcResponse::makeResult(req.id, nullptr);
}

void LspServer::handleInitialized(const JsonRpcRequest& /*notif*/) {
    // Client acknowledged server capabilities
}

void LspServer::handleExit(const JsonRpcRequest& /*notif*/) {
    if (state_ == ServerState::ShuttingDown) {
        exitCode_ = 0;
    } else {
        exitCode_ = 1;
    }
    state_ = ServerState::Exited;
}

void LspServer::handleDidOpen(const JsonRpcRequest& notif) {
    if (notif.params.isNull()) return;
    auto params = DidOpenTextDocumentParams::fromJson(notif.params);
    vfs_.openDocument(params.textDocument);
    analyzeAndPublish(params.textDocument.uri);
}

void LspServer::handleDidChange(const JsonRpcRequest& notif) {
    if (notif.params.isNull()) return;
    auto params = DidChangeTextDocumentParams::fromJson(notif.params);
    if (vfs_.applyChanges(params)) {
        analyzeAndPublish(params.textDocument.uri);
    }
}

void LspServer::handleDidClose(const JsonRpcRequest& notif) {
    if (notif.params.isNull()) return;
    auto params = DidCloseTextDocumentParams::fromJson(notif.params);
    vfs_.closeDocument(params.textDocument.uri);
    analyzer_.evictCache(params.textDocument.uri);
    publishDiagnostics(params.textDocument.uri, {});
}

void LspServer::handleDidSave(const JsonRpcRequest& /*notif*/) {
    // Optional save handler
}

void LspServer::analyzeAndPublish(const std::string& uri) {
    auto docOpt = vfs_.getDocument(uri);
    if (!docOpt.has_value()) return;

    auto result = analyzer_.analyze(*docOpt);
    publishDiagnostics(uri, result.diagnostics);
    analyzer_.updateCache(uri, std::move(result));
}

JsonRpcResponse LspServer::handleHover(const JsonRpcRequest& req) {
    // Default placeholder in Phase 1
    return JsonRpcResponse::makeResult(req.id, nullptr);
}

JsonRpcResponse LspServer::handleDefinition(const JsonRpcRequest& req) {
    // Default placeholder in Phase 1
    return JsonRpcResponse::makeResult(req.id, nullptr);
}

JsonRpcResponse LspServer::handleReferences(const JsonRpcRequest& req) {
    // Default placeholder in Phase 1
    return JsonRpcResponse::makeResult(req.id, JsonValue::array());
}

JsonRpcResponse LspServer::handleCompletion(const JsonRpcRequest& req) {
    // Default placeholder in Phase 1
    return JsonRpcResponse::makeResult(req.id, JsonValue::array());
}

JsonRpcResponse LspServer::handleDocumentSymbol(const JsonRpcRequest& req) {
    // Default placeholder in Phase 1
    return JsonRpcResponse::makeResult(req.id, JsonValue::array());
}

bool LspServer::publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics) {
    JsonValue diagsArr = JsonValue::array();
    for (const auto& d : diagnostics) {
        diagsArr.push_back(d.toJson());
    }

    JsonValue params = JsonValue::object();
    params["uri"] = uri;
    params["diagnostics"] = std::move(diagsArr);

    return sendNotification(JsonRpcNotification::make("textDocument/publishDiagnostics", std::move(params)));
}

bool LspServer::sendNotification(const JsonRpcNotification& notif) {
    return transport_.sendMessage(notif.toJson().serialize());
}

bool LspServer::sendResponse(const JsonRpcResponse& resp) {
    return transport_.sendMessage(resp.toJson().serialize());
}

} // namespace tamarin::lsp
