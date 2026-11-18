// ============================================================================
// semver.h - Semantic Versioning 2.0.0 and Version Requirements
// ============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <compare>

namespace troop {

struct SemVer {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    std::string preRelease;
    std::string buildMeta;

    static std::optional<SemVer> parse(std::string_view str);

    std::string toString() const;

    auto operator<=>(const SemVer& other) const {
        if (auto cmp = major <=> other.major; cmp != 0) return cmp;
        if (auto cmp = minor <=> other.minor; cmp != 0) return cmp;
        if (auto cmp = patch <=> other.patch; cmp != 0) return cmp;

        // Pre-release versions have lower precedence than normal versions
        if (preRelease.empty() && !other.preRelease.empty()) return std::strong_ordering::greater;
        if (!preRelease.empty() && other.preRelease.empty()) return std::strong_ordering::less;
        return preRelease <=> other.preRelease;
    }

    bool operator==(const SemVer& other) const {
        return major == other.major &&
               minor == other.minor &&
               patch == other.patch &&
               preRelease == other.preRelease;
    }
};

enum class VersionOp {
    Exact,               // "=1.2.3" or "1.2.3"
    Caret,               // "^1.2.3" (Rust/Cargo compatible)
    Tilde,               // "~1.2.3" (patch updates only)
    GreaterThan,         // ">1.2.3"
    GreaterThanOrEqual,  // ">=1.2.3"
    LessThan,            // "<1.2.3"
    LessThanOrEqual,     // "<=1.2.3"
    Wildcard             // "*" or "1.*"
};

struct VersionPredicate {
    VersionOp op = VersionOp::Exact;
    SemVer target;
    uint32_t wildcardDepth = 0; // 0 = "*", 1 = "1.*", 2 = "1.2.*"

    bool matches(const SemVer& ver) const;
};

class VersionReq {
public:
    VersionReq() = default;

    static std::optional<VersionReq> parse(std::string_view str);

    bool matches(const SemVer& ver) const;

    std::string toString() const;

    bool isWildcard() const { return predicates_.empty() || (predicates_.size() == 1 && predicates_[0].op == VersionOp::Wildcard && predicates_[0].wildcardDepth == 0); }

    const std::vector<VersionPredicate>& predicates() const { return predicates_; }

private:
    std::vector<VersionPredicate> predicates_;
    std::string rawString_;
};

} // namespace troop
