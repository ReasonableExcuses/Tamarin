// ============================================================================
// package_manager.cpp - Package Manager Operations Implementation
// ============================================================================
#include "troop/package_manager.h"
#include "troop/version.h"

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace troop {

static std::filesystem::path getSelfExeDir() {
#if defined(_WIN32)
    wchar_t buf[1024];
    DWORD len = GetModuleFileNameW(nullptr, buf, 1024);
    if (len > 0) {
        return std::filesystem::path(buf).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

static int runCommand(const std::string& cmdStr) {
#if defined(_WIN32)
    std::string wrapped = "\"" + cmdStr + "\"";
    return std::system(wrapped.c_str());
#else
    return std::system(cmdStr.c_str());
#endif
}

std::filesystem::path PackageManager::getCompilerExecutable() {
    const char* envTam = std::getenv("TAMARIN_EXE");
    if (envTam && std::filesystem::exists(envTam)) {
        return std::filesystem::path(envTam);
    }

    auto selfDir = getSelfExeDir();
    auto cand1 = selfDir / "tamarin.exe";
    if (std::filesystem::exists(cand1)) return cand1;
    auto cand2 = selfDir / "tamarin";
    if (std::filesystem::exists(cand2)) return cand2;

    auto cand3 = selfDir.parent_path() / "Release" / "tamarin.exe";
    if (std::filesystem::exists(cand3)) return cand3;

    return "tamarin.exe";
}

std::optional<std::filesystem::path> PackageManager::findManifest(const std::filesystem::path& startDir) {
    auto curr = std::filesystem::weakly_canonical(startDir);
    while (true) {
        auto manifestFile = curr / "troop.toml";
        if (std::filesystem::exists(manifestFile)) {
            return manifestFile;
        }
        if (!curr.has_parent_path() || curr.parent_path() == curr) {
            break;
        }
        curr = curr.parent_path();
    }
    return std::nullopt;
}

bool PackageManager::newPackage(const std::filesystem::path& targetDir, const std::string& name, bool isLib, std::string& errorMsg) {
    if (std::filesystem::exists(targetDir) && !std::filesystem::is_empty(targetDir)) {
        errorMsg = "target directory already exists and is not empty: " + targetDir.string();
        return false;
    }

    std::filesystem::create_directories(targetDir / "src");
    std::filesystem::create_directories(targetDir / "tests");

    Manifest m = Manifest::createDefault(name, isLib);
    auto manifestPath = targetDir / "troop.toml";
    if (!m.saveToFile(manifestPath, errorMsg)) {
        return false;
    }

    // Write .gitignore
    {
        std::ofstream gi(targetDir / ".gitignore");
        if (gi.is_open()) {
            gi << "/bin/\n/build/\n/.troop/\ntroop.lock\n";
        }
    }

    // Write source file
    if (isLib) {
        std::ofstream libSrc(targetDir / "src" / "lib.tam");
        if (libSrc.is_open()) {
            libSrc << "// " << name << " - Tamarin Library\n\n"
                   << "public int add(int a, int b) {\n"
                   << "    return a + b;\n"
                   << "}\n";
        }
    } else {
        std::ofstream binSrc(targetDir / "src" / "main.tam");
        if (binSrc.is_open()) {
            binSrc << "// " << name << " - Tamarin Application\n\n"
                   << "int main() {\n"
                   << "    println(\"Hello from " << name << "!\");\n"
                   << "    return 0;\n"
                   << "}\n";
        }
    }

    // Write default basic test
    {
        std::ofstream testSrc(targetDir / "tests" / "test_basic.tam");
        if (testSrc.is_open()) {
            testSrc << "// Basic test for " << name << "\n\n"
                    << "int main() {\n"
                    << "    println(\"Test passed for " << name << "!\");\n"
                    << "    return 0;\n"
                    << "}\n";
        }
    }

    return true;
}

bool PackageManager::initPackage(const std::filesystem::path& targetDir, bool isLib, std::string& errorMsg) {
    auto manifestPath = targetDir / "troop.toml";
    if (std::filesystem::exists(manifestPath)) {
        errorMsg = "troop.toml already exists in: " + targetDir.string();
        return false;
    }

    std::string name = targetDir.filename().string();
    if (name.empty() || name == ".") {
        name = "app";
    }

    Manifest m = Manifest::createDefault(name, isLib);
    if (!m.saveToFile(manifestPath, errorMsg)) {
        return false;
    }

    std::filesystem::create_directories(targetDir / "src");
    std::filesystem::create_directories(targetDir / "tests");

    auto entryFile = targetDir / (isLib ? "src/lib.tam" : "src/main.tam");
    if (!std::filesystem::exists(entryFile)) {
        std::ofstream src(entryFile);
        if (isLib) {
            src << "public int add(int a, int b) {\n    return a + b;\n}\n";
        } else {
            src << "int main() {\n    println(\"Hello from " << name << "!\");\n    return 0;\n}\n";
        }
    }

    return true;
}

int PackageManager::buildPackage(const std::filesystem::path& manifestPath, bool isRelease, const std::string& targetTriple, const std::vector<std::string>& extraArgs) {
    std::string loadErr;
    auto manifest = Manifest::loadFromFile(manifestPath, loadErr);
    if (!manifest) {
        std::cerr << "troop: error: " << loadErr << "\n";
        return 1;
    }

    DependencyResolver resolver;
    auto res = resolver.resolve(*manifest);
    if (!res.success) {
        std::cerr << "troop: error: " << res.error << "\n";
        return 1;
    }

    // Save/update troop.lock
    auto rootDir = manifest->getPackageRootDir();
    auto lockPath = rootDir / "troop.lock";
    std::string lockErr;
    if (!res.lockfile.saveToFile(lockPath, lockErr)) {
        std::cerr << "troop: warning: failed to write lockfile: " << lockErr << "\n";
    }

    // Determine output binary directory and path
    auto profile = isRelease ? manifest->profileRelease : manifest->profileDev;
    auto binDir = rootDir / "bin" / (isRelease ? "release" : "debug");
    std::filesystem::create_directories(binDir);

    auto entryPath = manifest->getEntryPointPath();
    if (!std::filesystem::exists(entryPath)) {
        std::cerr << "troop: error: entry point not found: " << entryPath.string() << "\n";
        return 1;
    }

    std::string ext = (manifest->package.kind == "lib") ? ".lib" : ".exe";
    auto outBinary = binDir / (manifest->package.name + ext);

    auto compiler = getCompilerExecutable();
    std::ostringstream cmd;
    cmd << "\"" << compiler.string() << "\" build \"" << entryPath.string() << "\"";
    cmd << " -o \"" << outBinary.string() << "\"";

    if (profile.optLevel > 0) {
        cmd << " -O" << profile.optLevel;
    } else {
        cmd << " -O0";
    }

    if (profile.fastMath) {
        cmd << " --fast-math";
    }

    if (profile.crt == "static") {
        cmd << " --static";
    } else {
        cmd << " --dynamic";
    }

    if (manifest->package.kind == "lib") {
        cmd << " --lib";
    }

    if (!targetTriple.empty()) {
        cmd << " --target " << targetTriple;
    }

    // Pass all resolved include paths (-I)
    for (const auto& inc : res.includePaths) {
        cmd << " -I \"" << inc.string() << "\"";
    }

    for (const auto& extra : extraArgs) {
        cmd << " " << extra;
    }

    std::cout << "  Compiling " << manifest->package.name << " v" << manifest->package.version.toString()
              << " (" << (isRelease ? "release" : "debug") << ")...\n";

    int exitCode = runCommand(cmd.str());
    if (exitCode == 0) {
        std::cout << "   Finished " << (isRelease ? "release [optimized]" : "dev [unoptimized]")
                  << " target(s) in " << outBinary.string() << "\n";
    } else {
        std::cerr << "troop: compilation failed with exit code " << exitCode << "\n";
    }

    return exitCode;
}

int PackageManager::runPackage(const std::filesystem::path& manifestPath, bool isRelease, const std::vector<std::string>& runArgs) {
    int buildExit = buildPackage(manifestPath, isRelease);
    if (buildExit != 0) return buildExit;

    std::string loadErr;
    auto manifest = Manifest::loadFromFile(manifestPath, loadErr);
    if (!manifest) return 1;

    if (manifest->package.kind == "lib") {
        std::cerr << "troop: cannot run a library package; use 'troop test' instead.\n";
        return 1;
    }

    auto rootDir = manifest->getPackageRootDir();
    auto binDir = rootDir / "bin" / (isRelease ? "release" : "debug");
    auto exePath = binDir / (manifest->package.name + ".exe");

    if (!std::filesystem::exists(exePath)) {
        std::cerr << "troop: executable not found at: " << exePath.string() << "\n";
        return 1;
    }

    std::cout << "    Running `" << exePath.filename().string() << "`...\n";

    std::ostringstream runCmd;
    runCmd << "\"" << exePath.string() << "\"";
    for (const auto& arg : runArgs) {
        runCmd << " \"" << arg << "\"";
    }

    return runCommand(runCmd.str());
}

int PackageManager::testPackage(const std::filesystem::path& manifestPath, bool isRelease) {
    std::string loadErr;
    auto manifest = Manifest::loadFromFile(manifestPath, loadErr);
    if (!manifest) return 1;

    auto rootDir = manifest->getPackageRootDir();
    auto testsDir = rootDir / "tests";
    if (!std::filesystem::exists(testsDir) || std::filesystem::is_empty(testsDir)) {
        std::cout << "troop: no tests found in: " << testsDir.string() << "\n";
        return 0;
    }

    DependencyResolver resolver;
    auto res = resolver.resolve(*manifest);
    if (!res.success) {
        std::cerr << "troop: error: " << res.error << "\n";
        return 1;
    }

    auto binTestsDir = rootDir / "bin" / "tests";
    std::filesystem::create_directories(binTestsDir);

    auto compiler = getCompilerExecutable();
    int passed = 0;
    int failed = 0;

    for (const auto& entry : std::filesystem::directory_iterator(testsDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".tam") {
            auto testName = entry.path().stem().string();
            auto outTestExe = binTestsDir / (testName + ".exe");

            std::ostringstream cmd;
            cmd << "\"" << compiler.string() << "\" build \"" << entry.path().string() << "\"";
            cmd << " -o \"" << outTestExe.string() << "\"";
            cmd << (isRelease ? " -O3" : " -O0");
            for (const auto& inc : res.includePaths) {
                cmd << " -I \"" << inc.string() << "\"";
            }

            int compileRes = runCommand(cmd.str());
            if (compileRes != 0) {
                std::cout << " [FAIL] " << testName << " (compile failed)\n";
                failed++;
                continue;
            }

            int runRes = runCommand("\"" + outTestExe.string() + "\"");
            if (runRes == 0) {
                std::cout << " [PASS] " << testName << "\n";
                passed++;
            } else {
                std::cout << " [FAIL] " << testName << " (exit code " << runRes << ")\n";
                failed++;
            }
        }
    }

    std::cout << "\nTest Result: " << (failed == 0 ? "PASSED" : "FAILED")
              << " (" << passed << " passed, " << failed << " failed)\n";
    return failed == 0 ? 0 : 1;
}

bool PackageManager::addDependency(const std::filesystem::path& manifestPath, const std::string& depName, const DependencySpec& spec, std::string& errorMsg) {
    auto manifest = Manifest::loadFromFile(manifestPath, errorMsg);
    if (!manifest) return false;

    manifest->dependencies[depName] = spec;
    if (!manifest->saveToFile(manifestPath, errorMsg)) return false;

    // Re-resolve and update lockfile
    DependencyResolver resolver;
    auto res = resolver.resolve(*manifest);
    if (res.success) {
        auto rootDir = manifest->getPackageRootDir();
        res.lockfile.saveToFile(rootDir / "troop.lock", errorMsg);
    }

    return true;
}

bool PackageManager::clean(const std::filesystem::path& manifestPath, std::string& errorMsg) {
    auto manifest = Manifest::loadFromFile(manifestPath, errorMsg);
    if (!manifest) return false;

    auto rootDir = manifest->getPackageRootDir();
    std::error_code ec;
    std::filesystem::remove_all(rootDir / "bin", ec);
    std::filesystem::remove_all(rootDir / "build", ec);
    std::filesystem::remove_all(rootDir / ".troop", ec);
    return true;
}

} // namespace troop
