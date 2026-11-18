// ============================================================================
// range_analysis.h - Symbolic Range & Loop Induction Variable Analysis
// ============================================================================
#pragma once

#include "tamarin/ir/ir.h"
#include "tamarin/sema/types.h"

#include <cstdint>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <optional>

namespace tamarin::ir::opt {

struct Interval {
    int64_t min = INT64_MIN;
    int64_t max = INT64_MAX;
    bool hasLower = false;
    bool hasUpper = false;

    Interval() = default;
    Interval(int64_t val) : min(val), max(val), hasLower(true), hasUpper(true) {}
    Interval(int64_t lo, int64_t hi) : min(lo), max(hi), hasLower(true), hasUpper(true) {}

    static Interval unbounded() { return Interval(); }
    static Interval constant(int64_t v) { return Interval(v, v); }
    static Interval nonNegative() {
        Interval i;
        i.min = 0;
        i.hasLower = true;
        return i;
    }
    static Interval bounded(int64_t lo, int64_t hi) { return Interval(lo, hi); }

    [[nodiscard]] bool isConstant() const noexcept { return hasLower && hasUpper && min == max; }
    [[nodiscard]] bool isKnown() const noexcept { return hasLower && hasUpper; }
    [[nodiscard]] bool isNonNegative() const noexcept { return hasLower && min >= 0; }

    [[nodiscard]] Interval add(const Interval& other) const noexcept;
    [[nodiscard]] Interval sub(const Interval& other) const noexcept;
    [[nodiscard]] Interval mul(const Interval& other) const noexcept;
    [[nodiscard]] Interval intersect(const Interval& other) const noexcept;
    [[nodiscard]] Interval unionWith(const Interval& other) const noexcept;
};

/// Symbolic loop induction variable descriptor
struct LoopInductionVar {
    ValueId allocaVar = InvalidValue;   // The alloca pointer holding the induction variable
    int64_t initVal = 0;               // Initial value (typically 0)
    int64_t step = 1;                  // Step increment (typically 1)
    ValueId boundVal = InvalidValue;   // The upper bound value (e.g. from GetLen)
    int64_t constBound = -1;           // Constant upper bound if known, else -1
    ValueId boundedArray = InvalidValue; // Array from which bound was derived, if any
    std::string condBlock;
    std::string bodyBlock;
    std::string incBlock;
};

class RangeAnalysis {
public:
    void analyze(IRFunction& fn);

    [[nodiscard]] Interval getInterval(ValueId val) const;
    [[nodiscard]] bool hasInterval(ValueId val) const;

    /// Checks whether an index value is provably within [0, len(arr) - 1]
    [[nodiscard]] bool isProvenWithinBounds(const BasicBlock* block,
                                            ValueId idxVal,
                                            ValueId arrVal,
                                            const sema::Type* arrTy) const;

    [[nodiscard]] const std::vector<LoopInductionVar>& getInductionVars() const {
        return inductionVars_;
    }

    [[nodiscard]] ValueId unwrapCast(ValueId v) const;
    [[nodiscard]] ValueId getRootArray(ValueId val) const;

private:
    std::unordered_map<ValueId, Interval> valueIntervals_;
    std::unordered_map<ValueId, int64_t> constantValues_;
    std::unordered_map<ValueId, ValueId> underlyingPointers_;
    std::unordered_map<ValueId, ValueId> castOrigins_;
    std::vector<LoopInductionVar> inductionVars_;
    // Map from body block label to active induction variables
    std::unordered_map<std::string, std::vector<LoopInductionVar>> blockInductionVars_;

    void collectConstantsAndAllocas(IRFunction& fn);
    void identifyCanonicalLoops(IRFunction& fn);
    void propagateBlockIntervals(IRFunction& fn);
};

} // namespace tamarin::ir::opt

