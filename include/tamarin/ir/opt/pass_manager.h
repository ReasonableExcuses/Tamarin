// ============================================================================
// pass_manager.h - TIR Pass Pipeline Orchestration & Execution
// ============================================================================
#pragma once

#include "tamarin/ir/opt/pass.h"

#include <vector>
#include <memory>

namespace tamarin::ir::opt {

class PassManager {
public:
    PassManager() = default;

    void addFunctionPass(std::unique_ptr<TIRFunctionPass> pass) {
        functionPasses_.push_back(std::move(pass));
    }

    void addModulePass(std::unique_ptr<TIRModulePass> pass) {
        modulePasses_.push_back(std::move(pass));
    }

    bool run(IRModule& module, const sema::TypeContext* typeCtx = nullptr);

    /// Constructs standard optimization pipeline for requested optimization level (0, 1, 2, 3)
    static PassManager createDefaultPipeline(int optLevel);

private:
    std::vector<std::unique_ptr<TIRFunctionPass>> functionPasses_;
    std::vector<std::unique_ptr<TIRModulePass>> modulePasses_;
};

} // namespace tamarin::ir::opt
