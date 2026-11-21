// ============================================================================
// server.h - Tamarin LSP Server core orchestrator
// ============================================================================
#pragma once

#include "lsp/json.h"
#include "lsp/protocol.h"
#include "lsp/transport.h"
#include "lsp/vfs.h"
#include "lsp/analyzer.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>

namespace tamarin::lsp {

enum class ServerState {
    Uninitialized,
    Initialized,
    ShuttingDown,
    Exited
};

class LspServer {
public:
    explicit LspServer(LspTransport& transport);
    ~LspServer() = default;

    /// Enter the main JSON-RPC message processing loop.
    /// Returns the process exit code (0 on normal exit, 1 on abnormal exit).
    int run();

    /// Process a single incoming raw message payload.
    /// Useful for testing without blocking in run().
    bool processMessage(std::string_view payload);

    /// Server state query.
    [[nodiscard]] ServerState state() const noexcept { return state_; }

    /// Virtual File System access
    [[nodiscard]] const Vfs& vfs() const noexcept { return vfs_; }
    [[nodiscard]] Vfs& vfs() noexcept { return vfs_; }

    /// Document analyzer access
    [[nodiscard]] const DocumentAnalyzer& analyzer() const noexcept { return analyzer_; }
    [[nodiscard]] DocumentAnalyzer& analyzer() noexcept { return analyzer_; }

    /// Run analysis pipeline and publish diagnostic squiggles
    void analyzeAndPublish(const std::string& uri);

    /// Publish diagnostics for a document.
    bool publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);

    /// Send an arbitrary notification to the client.
    bool sendNotification(const JsonRpcNotification& notif);

    /// Send a response to the client.
    bool sendResponse(const JsonRpcResponse& resp);

private:
    LspTransport& transport_;
    Vfs vfs_;
    DocumentAnalyzer analyzer_;
    ServerState state_ = ServerState::Uninitialized;
    int exitCode_ = 0;

    // Dispatch router
    std::optional<JsonRpcResponse> handleRequest(const JsonRpcRequest& req);
    void handleNotification(const JsonRpcRequest& notif);

    // Request handlers
    JsonRpcResponse handleInitialize(const JsonRpcRequest& req);
    JsonRpcResponse handleShutdown(const JsonRpcRequest& req);
    JsonRpcResponse handleHover(const JsonRpcRequest& req);
    JsonRpcResponse handleDefinition(const JsonRpcRequest& req);
    JsonRpcResponse handleReferences(const JsonRpcRequest& req);
    JsonRpcResponse handleCompletion(const JsonRpcRequest& req);
    JsonRpcResponse handleDocumentSymbol(const JsonRpcRequest& req);

    // Notification handlers
    void handleInitialized(const JsonRpcRequest& notif);
    void handleExit(const JsonRpcRequest& notif);
    void handleDidOpen(const JsonRpcRequest& notif);
    void handleDidChange(const JsonRpcRequest& notif);
    void handleDidClose(const JsonRpcRequest& notif);
    void handleDidSave(const JsonRpcRequest& notif);
};

} // namespace tamarin::lsp
