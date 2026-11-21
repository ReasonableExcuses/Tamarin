// ============================================================================
// bounds_check_elimination.cpp - Implementation of Bounds-Check Elimination
// ============================================================================
#include "tamarin/ir/opt/bounds_check_elimination.h"

#include <unordered_map>

namespace tamarin::ir::opt {

bool BoundsCheckEliminationPass::runOnFunction(IRFunction& fn, const sema::TypeContext* /*typeCtx*/) {
    // Collect types of all values in function
    std::unordered_map<ValueId, const sema::Type*> valueTypes;
    for (const auto& param : fn.params) {
        valueTypes[param.valueId] = param.type;
    }
    for (const auto& blk : fn.blocks) {
        for (const auto& inst : blk->instructions) {
            if (inst.result != InvalidValue && inst.type != nullptr) {
                valueTypes[inst.result] = inst.type;
            }
        }
    }

    RangeAnalysis ra;
    ra.analyze(fn);

    bool changed = false;

    for (auto& blk : fn.blocks) {
        for (auto& inst : blk->instructions) {
            if (inst.opcode == Opcode::BoundsCheck) {
                ValueId idxVal = inst.op1;
                ValueId arrVal = inst.op2;
                const sema::Type* arrTy = nullptr;

                auto it = valueTypes.find(arrVal);
                if (it != valueTypes.end()) {
                    arrTy = it->second;
                    if (arrTy && arrTy->isReference()) {
                        arrTy = arrTy->elementType;
                    }
                }

                if (ra.isProvenWithinBounds(blk.get(), idxVal, arrVal, arrTy)) {
                    // Turn redundant bounds check into Nop
                    inst.opcode = Opcode::Nop;
                    inst.op1 = InvalidValue;
                    inst.op2 = InvalidValue;
                    inst.op3 = InvalidValue;
                    eliminatedCount_++;
                    changed = true;
                }
            }
        }
    }

    return changed;
}

} // namespace tamarin::ir::opt
