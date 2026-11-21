// ============================================================================
// cli.cpp - CLI implementation for tamarin-bindgen
// ============================================================================

#include "bindgen/cli.h"
#include "bindgen/c_parser.h"
#include "bindgen/tamarin_emitter.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string_view>

namespace tamarin::bindgen {

static void printHelp() {
    std::cout << "Usage: tamarin-bindgen [options] <header.h>\n"
              << "       tamarin bindgen [options] <header.h>\n\n"
              << "Automatic C/C++ FFI binding generator for the Tamarin programming language.\n\n"
              << "Options:\n"
              << "  -o, --output <file>      Output Tamarin file (default: <header>.tmn)\n"
              << "  -I, --include <dir>      Add directory to C include search path\n"
              << "  -D, --define <macro>     Predefine preprocessor macro\n"
              << "  --module <name>          Emit 'module <name>;' declaration\n"
              << "  --prefix <prefix>        Only bind symbols starting with <prefix>\n"
              << "  --strip-prefix <prefix>  Strip <prefix> from generated symbol names\n"
              << "  --public                 Emit 'public' visibility modifier on all declarations\n"
              << "  --all-headers            Include declarations from all headers, not just target\n"
              << "  -v, --version            Display version information\n"
              << "  -h, --help               Display this help message\n";
}

int cliMain(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp();
        return 1;
    }

    ParserOptions parseOpts;
    EmitterOptions emitOpts;
    std::string outputPath;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "--version" || arg == "-v") {
            std::cout << "tamarin-bindgen version 0.18.0\n";
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) outputPath = argv[++i];
        } else if (arg.starts_with("--output=") && arg.size() > 9) {
            outputPath = std::string(arg.substr(9));
        } else if (arg == "-I" || arg == "--include") {
            if (i + 1 < argc) parseOpts.includeDirs.push_back(argv[++i]);
        } else if (arg.starts_with("-I") && arg.size() > 2) {
            parseOpts.includeDirs.push_back(std::string(arg.substr(2)));
        } else if (arg == "-D" || arg == "--define") {
            if (i + 1 < argc) parseOpts.defines.push_back(argv[++i]);
        } else if (arg.starts_with("-D") && arg.size() > 2) {
            parseOpts.defines.push_back(std::string(arg.substr(2)));
        } else if (arg == "--module" && i + 1 < argc) {
            emitOpts.moduleName = argv[++i];
        } else if (arg.starts_with("--module=") && arg.size() > 9) {
            emitOpts.moduleName = std::string(arg.substr(9));
        } else if (arg == "--prefix" && i + 1 < argc) {
            parseOpts.prefixFilter = argv[++i];
        } else if (arg.starts_with("--prefix=") && arg.size() > 9) {
            parseOpts.prefixFilter = std::string(arg.substr(9));
        } else if (arg == "--strip-prefix" && i + 1 < argc) {
            emitOpts.stripPrefix = argv[++i];
        } else if (arg.starts_with("--strip-prefix=") && arg.size() > 15) {
            emitOpts.stripPrefix = std::string(arg.substr(15));
        } else if (arg == "--public") {
            emitOpts.emitPublic = true;
        } else if (arg == "--all-headers") {
            parseOpts.includeSystemHeaders = true;
        } else if (!arg.starts_with("-")) {
            parseOpts.headerPath = std::string(arg);
        } else {
            // Pass through unknown options to Clang (e.g. -Wno-everything)
            parseOpts.extraClangArgs.push_back(std::string(arg));
        }
    }

    if (parseOpts.headerPath.empty()) {
        std::cerr << "tamarin-bindgen: error: no input header file specified\n";
        return 1;
    }

    if (!std::filesystem::exists(parseOpts.headerPath)) {
        std::cerr << "tamarin-bindgen: error: input file not found: " << parseOpts.headerPath << "\n";
        return 1;
    }

    if (outputPath.empty()) {
        std::filesystem::path p(parseOpts.headerPath);
        outputPath = p.stem().string() + ".tmn";
    }

    if (emitOpts.moduleName.empty()) {
        std::filesystem::path p(parseOpts.headerPath);
        emitOpts.moduleName = p.stem().string();
    }

    CParser parser(parseOpts);
    CHeaderModel model;
    std::string error;

    if (!parser.parse(model, error)) {
        std::cerr << "tamarin-bindgen: error parsing header:\n" << error << "\n";
        return 1;
    }

    TamarinEmitter emitter(emitOpts);

    if (outputPath == "-") {
        emitter.emit(model, std::cout);
    } else {
        std::ofstream outFile(outputPath);
        if (!outFile) {
            std::cerr << "tamarin-bindgen: error: cannot open output file for writing: " << outputPath << "\n";
            return 1;
        }
        emitter.emit(model, outFile);
        std::cout << "tamarin-bindgen: successfully generated " << outputPath << " ("
                  << model.structs.size() << " structs, "
                  << model.enums.size() << " enums, "
                  << model.functions.size() << " functions, "
                  << model.constants.size() << " constants)\n";
    }

    return 0;
}

} // namespace tamarin::bindgen
