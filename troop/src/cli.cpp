// ============================================================================
// cli.cpp - Troop CLI Command Parser & Dispatcher
// ============================================================================
#include "troop/cli.h"
#include "troop/version.h"
#include "troop/package_manager.h"

#include <iostream>
#include <string_view>
#include <vector>

namespace troop {

static void printVersion() {
    std::cout << "troop " << TROOP_VERSION_STRING << " (Tamarin Package Manager)\n";
}

static void printHelp() {
    std::cout << "Troop - The Official Tamarin Package Manager (v" << TROOP_VERSION_STRING << ")\n\n"
              << "USAGE:\n"
              << "    troop <COMMAND> [OPTIONS]\n\n"
              << "COMMANDS:\n"
              << "    new <path>       Create a new Tamarin package in <path>\n"
              << "    init             Initialize a new Tamarin package in the current directory\n"
              << "    build            Compile the package and its dependencies\n"
              << "    run              Build and execute the package binary\n"
              << "    test             Execute all package test suites in tests/\n"
              << "    add <dep>        Add a dependency to troop.toml\n"
              << "    clean            Remove all build artifacts (bin/, build/, .troop/)\n"
              << "    version          Print version information\n"
              << "    help             Display this help message\n\n"
              << "OPTIONS:\n"
              << "    --release        Build with release optimizations (-O3)\n"
              << "    --lib            Create or configure as a library package\n"
              << "    --bin            Create or configure as a binary package (default)\n"
              << "    --target <trip>  Target architecture triple\n"
              << "    --path <dir>     Path to local dependency (for 'troop add')\n"
              << "    -v, --version    Print version info\n"
              << "    -h, --help       Print help message\n";
}

int cliMain(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp();
        return 1;
    }

    std::string_view command = argv[1];

    if (command == "--help" || command == "-h" || command == "help") {
        printHelp();
        return 0;
    }

    if (command == "--version" || command == "-v" || command == "version") {
        printVersion();
        return 0;
    }

    PackageManager pm;

    if (command == "new") {
        if (argc < 3) {
            std::cerr << "troop: error: missing path argument for 'troop new <path>'\n";
            return 1;
        }
        std::string targetPath = argv[2];
        bool isLib = false;
        for (int i = 3; i < argc; ++i) {
            std::string_view opt = argv[i];
            if (opt == "--lib") isLib = true;
            else if (opt == "--bin") isLib = false;
        }

        std::filesystem::path p(targetPath);
        std::string name = p.filename().string();
        std::string err;
        if (!pm.newPackage(p, name, isLib, err)) {
            std::cerr << "troop: error: " << err << "\n";
            return 1;
        }
        std::cout << "     Created " << (isLib ? "library" : "binary (application)")
                  << " `" << name << "` package in " << p.string() << "\n";
        return 0;
    }

    if (command == "init") {
        bool isLib = false;
        for (int i = 2; i < argc; ++i) {
            std::string_view opt = argv[i];
            if (opt == "--lib") isLib = true;
            else if (opt == "--bin") isLib = false;
        }
        std::string err;
        if (!pm.initPackage(std::filesystem::current_path(), isLib, err)) {
            std::cerr << "troop: error: " << err << "\n";
            return 1;
        }
        std::cout << "     Initialized " << (isLib ? "library" : "binary (application)")
                  << " package in " << std::filesystem::current_path().string() << "\n";
        return 0;
    }

    // For build, run, test, add, clean: locate troop.toml
    auto manifestPath = PackageManager::findManifest();
    if (!manifestPath) {
        std::cerr << "troop: error: could not find `troop.toml` in `"
                  << std::filesystem::current_path().string() << "` or any parent directory\n";
        return 1;
    }

    if (command == "build") {
        bool isRelease = false;
        std::string targetTriple;
        std::vector<std::string> extraArgs;

        for (int i = 2; i < argc; ++i) {
            std::string_view opt = argv[i];
            if (opt == "--release") {
                isRelease = true;
            } else if (opt == "--target" && i + 1 < argc) {
                targetTriple = argv[++i];
            } else {
                extraArgs.push_back(std::string(opt));
            }
        }

        return pm.buildPackage(*manifestPath, isRelease, targetTriple, extraArgs);
    }

    if (command == "run") {
        bool isRelease = false;
        std::vector<std::string> runArgs;
        bool inRunArgs = false;

        for (int i = 2; i < argc; ++i) {
            std::string_view opt = argv[i];
            if (inRunArgs) {
                runArgs.push_back(std::string(opt));
            } else if (opt == "--") {
                inRunArgs = true;
            } else if (opt == "--release") {
                isRelease = true;
            } else {
                runArgs.push_back(std::string(opt));
            }
        }

        return pm.runPackage(*manifestPath, isRelease, runArgs);
    }

    if (command == "test") {
        bool isRelease = false;
        for (int i = 2; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--release") {
                isRelease = true;
            }
        }
        return pm.testPackage(*manifestPath, isRelease);
    }

    if (command == "add") {
        if (argc < 3) {
            std::cerr << "troop: error: missing dependency name for 'troop add <name>'\n";
            return 1;
        }
        std::string depName = argv[2];
        DependencySpec spec;
        spec.name = depName;

        for (int i = 3; i < argc; ++i) {
            std::string_view opt = argv[i];
            if (opt == "--path" && i + 1 < argc) {
                spec.sourceKind = DependencySourceKind::Path;
                spec.path = argv[++i];
            } else if (opt == "--version" && i + 1 < argc) {
                auto req = VersionReq::parse(argv[++i]);
                if (req) spec.versionReq = *req;
            } else if (opt == "--git" && i + 1 < argc) {
                spec.sourceKind = DependencySourceKind::Git;
                spec.gitUrl = argv[++i];
            }
        }

        std::string err;
        if (!pm.addDependency(*manifestPath, depName, spec, err)) {
            std::cerr << "troop: error: " << err << "\n";
            return 1;
        }
        std::cout << "      Added `" << depName << "` to `troop.toml`\n";
        return 0;
    }

    if (command == "clean") {
        std::string err;
        if (!pm.clean(*manifestPath, err)) {
            std::cerr << "troop: error: " << err << "\n";
            return 1;
        }
        std::cout << "     Removed build artifacts\n";
        return 0;
    }

    std::cerr << "troop: error: unknown command `" << command << "`\n"
              << "       Run `troop --help` for available commands.\n";
    return 1;
}

} // namespace troop
