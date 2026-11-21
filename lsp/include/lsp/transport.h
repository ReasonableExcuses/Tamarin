// ============================================================================
// transport.h - LSP Stream Transport (JSON-RPC 2.0 over Content-Length framing)
// ============================================================================
#pragma once

#include <iostream>
#include <string>
#include <optional>
#include <mutex>

namespace tamarin::lsp {

class LspTransport {
public:
    explicit LspTransport(std::istream& in = std::cin, std::ostream& out = std::cout);

    /// Read next framed message from the input stream.
    /// Blocks until a complete message is received, EOF, or error.
    [[nodiscard]] std::optional<std::string> readMessage(std::string* error = nullptr);

    /// Write framed message to the output stream.
    /// Thread-safe via internal write lock.
    bool sendMessage(const std::string& payload);

private:
    std::istream& in_;
    std::ostream& out_;
    std::mutex writeMutex_;
};

} // namespace tamarin::lsp
