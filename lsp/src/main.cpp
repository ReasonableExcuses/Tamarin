// ============================================================================
// main.cpp - Entry point for tamarin-lsp Language Server
// ============================================================================

#include "lsp/cli.h"

int main(int argc, char* argv[]) {
    return tamarin::lsp::cliMain(argc, argv);
}
