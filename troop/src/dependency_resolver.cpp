// ============================================================================
// dependency_resolver.cpp - Dependency Resolution & DAG Ordering
// ============================================================================
#include "troop/dependency_resolver.h"

#include <algorithm>
#include <sstream>

namespace troop {

ResolutionResult DependencyResolver::resolve(const Manifest& rootManifest) {
    ResolutionResult result;
    std::vector<std::string> activeChain;
    std::set<std::string> visited;

    auto rootDir = rootManifest.getPackageRootDir();
    ResolvedDependency rootDep;
    rootDep.name = rootManifest.package.name;
    rootDep.version = rootManifest.package.version;
    rootDep.rootDir = rootDir;
    rootDep.srcDir = rootDir / "src";
    rootDep.manifestPath = rootManifest.manifestPath;
    rootDep.source = "root";
    rootDep.checksum = Lockfile::computeChecksum(rootDir / "src");
    for (const auto& [name, _] : rootManifest.dependencies) {
        rootDep.directDeps.push_back(name);
    }
    rootDep.manifest = rootManifest;

    result.packages[rootDep.name] = rootDep;
    activeChain.push_back(rootDep.name);

    if (!resolveRecursive(rootManifest, rootDir, activeChain, visited, result)) {
        result.success = false;
        return result;
    }

    // Include the root's own src directory
    if (std::filesystem::exists(rootDep.srcDir)) {
        result.includePaths.push_back(rootDep.srcDir);
    }
    result.includePaths.push_back(rootDir);

    // Build the lockfile
    result.lockfile.version = 1;
    for (const auto& dep : result.buildOrder) {
        LockedPackage lp;
        lp.name = dep.name;
        lp.version = dep.version;
        lp.source = dep.source;
        lp.checksum = dep.checksum;
        lp.dependencies = dep.directDeps;
        result.lockfile.addOrUpdatePackage(lp);
    }

    // Add root to lockfile
    LockedPackage rootLp;
    rootLp.name = rootDep.name;
    rootLp.version = rootDep.version;
    rootLp.source = "root";
    rootLp.checksum = rootDep.checksum;
    rootLp.dependencies = rootDep.directDeps;
    result.lockfile.addOrUpdatePackage(rootLp);

    result.success = true;
    return result;
}

bool DependencyResolver::resolveRecursive(const Manifest& current,
                                          const std::filesystem::path& baseDir,
                                          std::vector<std::string>& activeChain,
                                          std::set<std::string>& visited,
                                          ResolutionResult& result) {
    for (const auto& [depName, spec] : current.dependencies) {
        // Cycle check
        auto itChain = std::find(activeChain.begin(), activeChain.end(), depName);
        if (itChain != activeChain.end()) {
            std::ostringstream ss;
            ss << "cyclic dependency detected: ";
            for (auto it = itChain; it != activeChain.end(); ++it) {
                ss << *it << " -> ";
            }
            ss << depName;
            result.error = ss.str();
            return false;
        }

        if (visited.count(depName)) {
            // Already visited and resolved, verify version compatibility
            const auto& existing = result.packages[depName];
            if (!spec.versionReq.matches(existing.version)) {
                result.error = "version mismatch for dependency '" + depName + "': requires " +
                               spec.versionReq.toString() + " but found " + existing.version.toString();
                return false;
            }
            continue;
        }

        if (spec.sourceKind == DependencySourceKind::Path) {
            std::filesystem::path childDir = std::filesystem::weakly_canonical(baseDir / spec.path);
            std::filesystem::path childManifestPath = childDir / "troop.toml";

            std::string loadErr;
            auto childManifest = Manifest::loadFromFile(childManifestPath, loadErr);
            if (!childManifest) {
                result.error = "failed to load path dependency '" + depName + "' at " + childManifestPath.string() + ": " + loadErr;
                return false;
            }

            if (childManifest->package.name != depName) {
                result.error = "path dependency at " + childDir.string() + " declares package name '" +
                               childManifest->package.name + "', expected '" + depName + "'";
                return false;
            }

            if (!spec.versionReq.matches(childManifest->package.version)) {
                result.error = "dependency '" + depName + "' version " + childManifest->package.version.toString() +
                               " does not satisfy requirement " + spec.versionReq.toString();
                return false;
            }

            ResolvedDependency resolved;
            resolved.name = depName;
            resolved.version = childManifest->package.version;
            resolved.rootDir = childDir;
            resolved.srcDir = childDir / "src";
            resolved.manifestPath = childManifestPath;
            resolved.source = "path+" + spec.path;
            resolved.checksum = Lockfile::computeChecksum(resolved.srcDir);
            for (const auto& [subName, _] : childManifest->dependencies) {
                resolved.directDeps.push_back(subName);
            }
            resolved.manifest = *childManifest;

            result.packages[depName] = resolved;

            activeChain.push_back(depName);
            if (!resolveRecursive(*childManifest, childDir, activeChain, visited, result)) {
                return false;
            }
            activeChain.pop_back();

            visited.insert(depName);
            result.buildOrder.push_back(resolved);

            if (std::filesystem::exists(resolved.srcDir)) {
                result.includePaths.push_back(resolved.srcDir);
            }
            result.includePaths.push_back(resolved.rootDir);
        } else {
            // Registry or Git: For initial version, path dependencies are fully wired;
            // mock or register placeholder for offline resolution
            ResolvedDependency resolved;
            resolved.name = depName;
            resolved.version = SemVer{1, 0, 0, "", ""};
            resolved.source = spec.sourceKind == DependencySourceKind::Git ? ("git+" + spec.gitUrl) : "registry";
            resolved.checksum = "0000000000000000";
            result.packages[depName] = resolved;
            visited.insert(depName);
            result.buildOrder.push_back(resolved);
        }
    }

    return true;
}

} // namespace troop
