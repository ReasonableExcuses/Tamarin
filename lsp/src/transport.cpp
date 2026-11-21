// ============================================================================
// transport.cpp - LSP Stream Transport implementation
// ============================================================================

#include "lsp/transport.h"

#include <cctype>
#include <charconv>

namespace tamarin::lsp {

LspTransport::LspTransport(std::istream& in, std::ostream& out)
    : in_(in), out_(out) {}

std::optional<std::string> LspTransport::readMessage(std::string* error) {
    size_t contentLength = 0;
    bool hasContentLength = false;

    // 1. Read headers until empty line (\r\n\r\n or \n\n)
    while (true) {
        std::string line;
        while (true) {
            int c = in_.get();
            if (c == EOF) {
                if (hasContentLength) {
                    if (error) *error = "Unexpected EOF while reading headers";
                }
                return std::nullopt;
            }
            if (c == '\n') break;
            if (c != '\r') line.push_back(static_cast<char>(c));
        }

        if (line.empty()) {
            // End of headers
            break;
        }

        // Parse header
        constexpr std::string_view kContentLengthHeader = "Content-Length:";
        if (line.size() >= kContentLengthHeader.size() &&
            line.compare(0, kContentLengthHeader.size(), kContentLengthHeader) == 0) {
            size_t valStart = kContentLengthHeader.size();
            while (valStart < line.size() && std::isspace(static_cast<unsigned char>(line[valStart]))) {
                valStart++;
            }
            size_t parsedLen = 0;
            auto [ptr, ec] = std::from_chars(line.data() + valStart, line.data() + line.size(), parsedLen);
            if (ec == std::errc()) {
                contentLength = parsedLen;
                hasContentLength = true;
            }
        }
    }

    if (!hasContentLength) {
        if (error) *error = "Missing Content-Length header";
        return std::nullopt;
    }

    // 2. Read exactly contentLength bytes
    std::string payload;
    payload.resize(contentLength);
    in_.read(payload.data(), static_cast<std::streamsize>(contentLength));
    std::streamsize bytesRead = in_.gcount();
    if (static_cast<size_t>(bytesRead) < contentLength) {
        if (error) *error = "Premature EOF while reading message body";
        return std::nullopt;
    }

    return payload;
}

bool LspTransport::sendMessage(const std::string& payload) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    out_ << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    out_.flush();
    return out_.good();
}

} // namespace tamarin::lsp
