// ============================================================================
// pass.h - Tamarin Intermediate Representation (TIR) Optimization Passes
// ============================================================================
#pragma once

#include "tamarin/ir/ir.h"
#include "tamarin/sema/types.h"

#include <string_view>
#include <memory>

namespace tamarin::ir::opt {

struct PassResult {
    bool changed = false;
};

class TIRFunctionPass {
public:
    virtual ~TIRFunctionPass() = default;
    virtual std::string_view name() const = 0;
    virtual bool runOnFunction(IRFunction& fn, const sema::TypeContext* typeCtx = nullptr) = 0;
};

class TIRModulePass {
public:
    virtual ~TIRModulePass() = default;
    virtual std::string_view name() const = 0;
    virtual bool runOnModule(IRModule& module, const sema::TypeContext* typeCtx = nullptr) = 0;
};

} // namespace tamarin::ir::opt
