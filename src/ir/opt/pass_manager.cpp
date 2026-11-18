// ============================================================================
// pass_manager.cpp - TIR Pass Pipeline Implementation
// ============================================================================
#include "tamarin/ir/opt/pass_manager.h"
#include "tamarin/ir/opt/bounds_check_elimination.h"
#include "tamarin/ir/opt/alias_analysis.h"

namespace tamarin::ir::opt {

bool PassManager::run(IRModule& module, const sema::TypeContext* typeCtx) {
    bool anyChanged = false;

    // Run function passes on every function
    for (const auto& fn : module.functions) {
        if (!fn) continue;
        for (const auto& pass : functionPasses_) {
            if (pass->runOnFunction(*fn, typeCtx)) {
                anyChanged = true;
            }
        }
    }

    // Run module passes
    for (const auto& pass : modulePasses_) {
        if (pass->runOnModule(module, typeCtx)) {
            anyChanged = true;
        }
    }

    return anyChanged;
}

PassManager PassManager::createDefaultPipeline(int optLevel) {
    PassManager pm;
    if (optLevel >= 1) {
        pm.addFunctionPass(std::make_unique<BoundsCheckEliminationPass>());
        pm.addFunctionPass(std::make_unique<AliasAnalysis>());
    }
    return pm;
}

} // namespace tamarin::ir::opt
