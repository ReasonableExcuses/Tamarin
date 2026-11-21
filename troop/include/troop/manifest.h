// ============================================================================
// manifest.h - troop.toml Manifest Data Structures & Parser
// ============================================================================
#pragma once

#include "troop/semver.h"

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>

namespace troop {

struct PackageInfo {
    std::string name;
    SemVer version;
    std::string edition = "2026";
    std::vector<std::string> authors;
    std::string description;
    std::string license;
    std::string kind = "bin"; // "bin" or "lib"
    std::string entryPoint;   // defaults to "src/main.tam" or "src/lib.tam"
    std::vector<std::string> links;   // Native libraries to link against, e.g. ["raylib"]
    std::vector<std::string> libDirs; // Native library search paths, e.g. ["lib/native"]
};

enum class DependencySourceKind {
    Registry,
    Path,
    Git
};

struct DependencySpec {
    std::string name;
    DependencySourceKind sourceKind = DependencySourceKind::Registry;
    VersionReq versionReq;
    std::string path;         // for local path dependencies
    std::string gitUrl;       // for git dependencies
    std::string gitTag;
    std::string gitBranch;
    std::vector<std::string> features;
    bool optional = false;

    std::string toTomlString() const;
};

struct BuildProfile {
    int optLevel = 0;
    bool fastMath = false;
    std::string crt = "dynamic"; // "dynamic" or "static"
    bool lto = false;
};

struct WorkspaceConfig {
    std::vector<std::string> members;
    bool isWorkspace() const { return !members.empty(); }
};

class Manifest {
public:
    Manifest() = default;

    PackageInfo package;
    std::map<std::string, DependencySpec> dependencies;
    std::map<std::string, DependencySpec> devDependencies;
    BuildProfile profileDev{0, false, "dynamic", false};
    BuildProfile profileRelease{3, false, "dynamic", false};
    WorkspaceConfig workspace;
    std::filesystem::path manifestPath;

    static std::optional<Manifest> loadFromFile(const std::filesystem::path& path, std::string& errorMsg);
    static std::optional<Manifest> parseString(std::string_view content, const std::filesystem::path& manifestPath, std::string& errorMsg);
    static Manifest createDefault(const std::string& name, bool isLib);

    std::string toToml() const;
    bool saveToFile(const std::filesystem::path& path, std::string& errorMsg) const;

    /// Returns the resolved absolute entry point file path (.tam)
    std::filesystem::path getEntryPointPath() const;

    /// Returns the package root directory (parent of troop.toml)
    std::filesystem::path getPackageRootDir() const;
};

} // namespace troop
