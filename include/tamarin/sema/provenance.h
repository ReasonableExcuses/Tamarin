// ============================================================================
// provenance.h - Semantic provenance and escape analysis representation
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace tamarin::sema {

enum class ProvenanceKind {
    None,            // Untracked or primitive value (no region or borrow tracking)
    StaticStorage,   // Global or static storage (outlives all functions and regions)
    LocalRegion,     // Storage allocated in a lexical region with unique regionId
    ParameterBorrow, // Value borrowed from function parameter
    ThisBorrow,      // Value borrowed from method receiver 'this'
    LocalStack,      // Value pointing to local stack frame variable/storage
    Unknown          // Indeterminate origin (conservatively treated as unable to escape)
};

struct ProvenanceOrigin {
    ProvenanceKind kind = ProvenanceKind::None;
    uint64_t identity = 0; // regionId for LocalRegion, paramIndex for ParameterBorrow, 0 otherwise
    std::string detail;    // Parameter or variable name for source-located diagnostics

    bool operator==(const ProvenanceOrigin& other) const {
        return kind == other.kind && identity == other.identity;
    }
    bool operator!=(const ProvenanceOrigin& other) const {
        return !(*this == other);
    }

    [[nodiscard]] bool isRegion() const noexcept {
        return kind == ProvenanceKind::LocalRegion;
    }
    [[nodiscard]] bool isBorrow() const noexcept {
        return kind == ProvenanceKind::ParameterBorrow || kind == ProvenanceKind::ThisBorrow;
    }
    [[nodiscard]] bool isStack() const noexcept {
        return kind == ProvenanceKind::LocalStack;
    }
    [[nodiscard]] bool isStatic() const noexcept {
        return kind == ProvenanceKind::StaticStorage;
    }

    [[nodiscard]] std::string toString() const {
        switch (kind) {
            case ProvenanceKind::None: return "none";
            case ProvenanceKind::StaticStorage: return "static storage";
            case ProvenanceKind::LocalRegion: return "region #" + std::to_string(identity);
            case ProvenanceKind::ParameterBorrow:
                return "borrowed parameter" + (detail.empty() ? (" #" + std::to_string(identity)) : (" '" + detail + "'"));
            case ProvenanceKind::ThisBorrow: return "borrowed 'this' receiver";
            case ProvenanceKind::LocalStack:
                return "local stack variable" + (detail.empty() ? "" : (" '" + detail + "'"));
            case ProvenanceKind::Unknown: return "unknown provenance";
        }
        return "unknown";
    }
};

struct ProvenanceSet {
    std::vector<ProvenanceOrigin> origins;

    ProvenanceSet() = default;
    ProvenanceSet(ProvenanceKind kind, uint64_t id = 0, std::string detail = "") {
        if (kind != ProvenanceKind::None) {
            origins.push_back(ProvenanceOrigin{kind, id, std::move(detail)});
        }
    }
    ProvenanceSet(ProvenanceOrigin origin) {
        if (origin.kind != ProvenanceKind::None) {
            origins.push_back(std::move(origin));
        }
    }

    void add(const ProvenanceOrigin& origin) {
        if (origin.kind == ProvenanceKind::None) return;
        for (const auto& o : origins) {
            if (o == origin) return;
        }
        origins.push_back(origin);
    }

    void merge(const ProvenanceSet& other) {
        for (const auto& o : other.origins) {
            add(o);
        }
    }

    [[nodiscard]] bool empty() const noexcept {
        return origins.empty();
    }

    [[nodiscard]] bool hasRegion() const noexcept {
        for (const auto& o : origins) {
            if (o.isRegion()) return true;
        }
        return false;
    }

    [[nodiscard]] bool hasBorrow() const noexcept {
        for (const auto& o : origins) {
            if (o.isBorrow()) return true;
        }
        return false;
    }

    [[nodiscard]] bool hasParamBorrow() const noexcept {
        for (const auto& o : origins) {
            if (o.kind == ProvenanceKind::ParameterBorrow) return true;
        }
        return false;
    }

    [[nodiscard]] bool hasThisBorrow() const noexcept {
        for (const auto& o : origins) {
            if (o.kind == ProvenanceKind::ThisBorrow) return true;
        }
        return false;
    }

    [[nodiscard]] bool hasLocalStack() const noexcept {
        for (const auto& o : origins) {
            if (o.isStack()) return true;
        }
        return false;
    }

    [[nodiscard]] bool hasUnknown() const noexcept {
        for (const auto& o : origins) {
            if (o.kind == ProvenanceKind::Unknown) return true;
        }
        return false;
    }

    [[nodiscard]] bool hasStaticStorage() const noexcept {
        for (const auto& o : origins) {
            if (o.kind == ProvenanceKind::StaticStorage) return true;
        }
        return false;
    }

    [[nodiscard]] bool hasNonStaticBorrowOrRegion() const noexcept {
        for (const auto& o : origins) {
            if (o.isRegion() || o.isBorrow() || o.isStack() || o.kind == ProvenanceKind::Unknown) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] uint64_t primaryRegionId() const noexcept {
        for (const auto& o : origins) {
            if (o.isRegion()) return o.identity;
        }
        return 0;
    }

    [[nodiscard]] std::string describe() const {
        if (origins.empty()) return "untracked storage";
        std::string s;
        for (size_t i = 0; i < origins.size(); ++i) {
            if (i > 0) s += ", ";
            s += origins[i].toString();
        }
        return s;
    }
};

enum class BorrowEffect {
    NoEscape, // Parameter or receiver proven not to escape function or caller scope
    Escapes,  // Parameter or receiver may escape to static/global storage or outer lifetime
    Unknown   // Indeterminate escape contract (external or unanalyzed)
};

struct FunctionBorrowSummary {
    std::string functionName;
    std::vector<BorrowEffect> parameterEffects;
    BorrowEffect receiverEffect = BorrowEffect::NoEscape;
    std::vector<int> returnBorrowsFromParam; // indices of params returned by borrow
    bool returnBorrowsFromThis = false;
    bool hasBody = false;
    bool isExternal = false;
    bool isBuiltin = false;

    [[nodiscard]] bool paramEscapes(size_t i) const noexcept {
        if (i >= parameterEffects.size()) return false;
        return parameterEffects[i] == BorrowEffect::Escapes;
    }
    [[nodiscard]] bool isParamUnknown(size_t i) const noexcept {
        if (i >= parameterEffects.size()) return isExternal;
        return parameterEffects[i] == BorrowEffect::Unknown;
    }
    [[nodiscard]] bool thisEscapes() const noexcept {
        return receiverEffect == BorrowEffect::Escapes;
    }
    [[nodiscard]] bool isReceiverUnknown() const noexcept {
        return receiverEffect == BorrowEffect::Unknown;
    }
};

} // namespace tamarin::sema
