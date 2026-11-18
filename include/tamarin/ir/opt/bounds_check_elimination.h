// ============================================================================
// bounds_check_elimination.h - TIR Bounds-Check Elimination (BCE) Pass
// ============================================================================
#pragma once

#include "tamarin/ir/opt/pass.h"
#include "tamarin/ir/opt/range_analysis.h"

namespace tamarin::ir::opt {

class BoundsCheckEliminationPass : public TIRFunctionPass {
public:
    BoundsCheckEliminationPass() = default;

    std::string_view name() const override { return "bounds-check-elimination"; }

    bool runOnFunction(IRFunction& fn, const sema::TypeContext* typeCtx = nullptr) override;

    [[nodiscard]] size_t getEliminatedCount() const noexcept { return eliminatedCount_; }
    void resetStats() noexcept { eliminatedCount_ = 0; }

private:
    size_t eliminatedCount_ = 0;
};

} // namespace tamarin::ir::opt
