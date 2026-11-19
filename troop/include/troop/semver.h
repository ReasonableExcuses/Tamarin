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
#include <charconv>
#include <algorithm>

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
        if (preRelease.empty() && other.preRelease.empty()) return std::strong_ordering::equal;

        // SemVer 2.0.0 Section 11 segment comparison
        auto split = [](std::string_view s) {
            std::vector<std::string_view> parts;
            size_t start = 0;
            while (start < s.size()) {
                size_t dot = s.find('.', start);
                if (dot == std::string_view::npos) {
                    parts.push_back(s.substr(start));
                    break;
                }
                parts.push_back(s.substr(start, dot - start));
                start = dot + 1;
            }
            return parts;
        };

        auto aParts = split(preRelease);
        auto bParts = split(other.preRelease);
        size_t n = std::min(aParts.size(), bParts.size());
        for (size_t i = 0; i < n; ++i) {
            auto pA = aParts[i];
            auto pB = bParts[i];
            if (pA == pB) continue;
            bool aIsNum = !pA.empty() && std::all_of(pA.begin(), pA.end(), ::isdigit);
            bool bIsNum = !pB.empty() && std::all_of(pB.begin(), pB.end(), ::isdigit);
            if (aIsNum && bIsNum) {
                uint64_t vA = 0, vB = 0;
                std::from_chars(pA.data(), pA.data() + pA.size(), vA);
                std::from_chars(pB.data(), pB.data() + pB.size(), vB);
                if (auto cmp = vA <=> vB; cmp != 0) return cmp;
            } else if (aIsNum && !bIsNum) {
                return std::strong_ordering::less;
            } else if (!aIsNum && bIsNum) {
                return std::strong_ordering::greater;
            } else {
                if (auto cmp = pA <=> pB; cmp != 0) return cmp;
            }
        }
        return aParts.size() <=> bParts.size();
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
