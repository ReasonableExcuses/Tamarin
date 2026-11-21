// ============================================================================
// package_manager.h - Troop High-Level Operations (new, build, run, test, clean)
// ============================================================================
#pragma once

#include "troop/manifest.h"
#include "troop/dependency_resolver.h"

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace troop {

class PackageManager {
public:
    PackageManager() = default;

    /// Locate troop.toml by walking up directory tree starting from startDir
    static std::optional<std::filesystem::path> findManifest(const std::filesystem::path& startDir = std::filesystem::current_path());

    /// Scaffolds a new package in targetDir
    bool newPackage(const std::filesystem::path& targetDir, const std::string& name, bool isLib, std::string& errorMsg, const std::string& templateName = "");

    /// Initializes package in existing targetDir
    bool initPackage(const std::filesystem::path& targetDir, bool isLib, std::string& errorMsg, const std::string& templateName = "");

    /// Builds the package (resolves dependencies, invokes compiler, writes bin/ and troop.lock)
    int buildPackage(const std::filesystem::path& manifestPath, bool isRelease, const std::string& targetTriple = "", const std::vector<std::string>& extraArgs = {});

    /// Validates syntax and types of the package without code generation or linking
    int checkPackage(const std::filesystem::path& manifestPath);

    /// Builds and executes the package binary
    int runPackage(const std::filesystem::path& manifestPath, bool isRelease, const std::vector<std::string>& runArgs = {});

    /// Runs all tests in the package tests/ folder
    int testPackage(const std::filesystem::path& manifestPath, bool isRelease);

    /// Adds a dependency to troop.toml
    bool addDependency(const std::filesystem::path& manifestPath, const std::string& depName, const DependencySpec& spec, std::string& errorMsg, bool isDev = false);

    /// Removes a dependency from troop.toml
    bool removeDependency(const std::filesystem::path& manifestPath, const std::string& depName, std::string& errorMsg);

    /// Prints dependency tree visualization (like cargo tree)
    int printTree(const std::filesystem::path& manifestPath);

    /// Cleans build artifacts (bin/, build/, .troop/)
    bool clean(const std::filesystem::path& manifestPath, std::string& errorMsg);

    /// Get path to the Tamarin compiler executable
    static std::filesystem::path getCompilerExecutable();
};

} // namespace troop
