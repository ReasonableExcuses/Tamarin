// ============================================================================
// lockfile.h - troop.lock Lockfile Data Structures & Serializer
// ============================================================================
#pragma once

#include "troop/semver.h"

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace troop {

struct LockedPackage {
    std::string name;
    SemVer version;
    std::string source;
    std::string checksum;
    std::vector<std::string> dependencies;
};

class Lockfile {
public:
    Lockfile() = default;

    int version = 1;
    std::vector<LockedPackage> packages;

    static std::optional<Lockfile> loadFromFile(const std::filesystem::path& path, std::string& errorMsg);
    static std::optional<Lockfile> parseString(std::string_view content, std::string& errorMsg);

    bool saveToFile(const std::filesystem::path& path, std::string& errorMsg) const;
    std::string toToml() const;

    const LockedPackage* findPackage(const std::string& name) const;
    void addOrUpdatePackage(const LockedPackage& pkg);

    static std::string computeChecksum(const std::filesystem::path& path);
};

} // namespace troop
