// ============================================================================
// dependency_resolver.h - Dependency Graph & Minimal Version Selection
// ============================================================================
#pragma once

#include "troop/manifest.h"
#include "troop/lockfile.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

namespace troop {

struct ResolvedDependency {
    std::string name;
    SemVer version;
    std::filesystem::path rootDir;
    std::filesystem::path srcDir;
    std::filesystem::path manifestPath;
    std::string source;
    std::string checksum;
    std::vector<std::string> directDeps;
    Manifest manifest;
};

struct ResolutionResult {
    bool success = false;
    std::string error;
    std::vector<ResolvedDependency> buildOrder; // Topological order (leaves first)
    std::map<std::string, ResolvedDependency> packages;
    std::vector<std::filesystem::path> includePaths;
    Lockfile lockfile;
};

class DependencyResolver {
public:
    DependencyResolver() = default;

    /// Resolves all dependencies starting from root manifest
    ResolutionResult resolve(const Manifest& rootManifest);

private:
    bool resolveRecursive(const Manifest& current,
                          const std::filesystem::path& baseDir,
                          std::vector<std::string>& activeChain,
                          std::set<std::string>& visited,
                          ResolutionResult& result);
};

} // namespace troop
