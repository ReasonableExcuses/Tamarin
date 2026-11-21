// ============================================================================
// cli.cpp - CLI implementation for tamarin-lsp Language Server
// ============================================================================

#include "lsp/cli.h"
#include "lsp/server.h"
#include "lsp/transport.h"

#include <iostream>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace tamarin::lsp {

static void printVersion() {
    std::cout << "tamarin-lsp 0.18.0 (Tamarin Language Server Protocol Engine)\n";
}

static void printHelp() {
    std::cout << "Usage: tamarin-lsp [options]\n\n"
              << "Options:\n"
              << "  --stdio        Run Language Server over standard I/O (default)\n"
              << "  --version, -v  Display version information\n"
              << "  --help, -h     Display this help message\n";
}

int cliMain(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            printVersion();
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        }
    }

#ifdef _WIN32
    // Ensure stdio is in binary mode to prevent \r\n translation from corrupting Content-Length framing
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // Fast I/O synchronization
    std::cin.tie(nullptr);
    std::ios_base::sync_with_stdio(false);

    LspTransport transport(std::cin, std::cout);
    LspServer server(transport);
    return server.run();
}

} // namespace tamarin::lsp
