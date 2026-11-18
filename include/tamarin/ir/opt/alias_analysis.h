// ============================================================================
// alias_analysis.h - Memory Disjointness & Alias Analysis for TIR
// ============================================================================
#pragma once

#include "tamarin/ir/opt/pass.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tamarin::ir::opt {

enum class StorageOrigin {
    DistinctAlloca,
    DistinctRegion,
    DistinctSliceBorrow,
    DynamicHeap,
    ParamRef,
    Unknown
};

struct PointerProvenance {
    StorageOrigin origin = StorageOrigin::Unknown;
    ValueId baseOriginId = InvalidValue;
    int64_t constantOffset = 0;
};

class AliasAnalysis : public TIRFunctionPass {
public:
    AliasAnalysis() = default;

    std::string_view name() const override { return "alias-analysis"; }

    bool runOnFunction(IRFunction& fn, const sema::TypeContext* typeCtx = nullptr) override;

    [[nodiscard]] bool areDisjoint(ValueId ptrA, ValueId ptrB) const;

    [[nodiscard]] bool isParamNoAlias(ValueId paramVal) const;

    [[nodiscard]] const std::unordered_set<ValueId>& getNoAliasValues() const {
        return noaliasValues_;
    }

private:
    std::unordered_map<ValueId, PointerProvenance> provenanceMap_;
    std::unordered_set<ValueId> noaliasValues_;
    std::unordered_set<ValueId> noaliasParams_;
};

} // namespace tamarin::ir::opt
