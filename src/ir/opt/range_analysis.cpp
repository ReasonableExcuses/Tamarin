// ============================================================================
// range_analysis.cpp - Implementation of Symbolic Range Analysis
// ============================================================================
#include "tamarin/ir/opt/range_analysis.h"

namespace tamarin::ir::opt {

Interval Interval::add(const Interval& other) const noexcept {
    Interval res;
    if (hasLower && other.hasLower) {
        res.min = min + other.min;
        res.hasLower = true;
    }
    if (hasUpper && other.hasUpper) {
        res.max = max + other.max;
        res.hasUpper = true;
    }
    return res;
}

Interval Interval::sub(const Interval& other) const noexcept {
    Interval res;
    if (hasLower && other.hasUpper) {
        res.min = min - other.max;
        res.hasLower = true;
    }
    if (hasUpper && other.hasLower) {
        res.max = max - other.min;
        res.hasUpper = true;
    }
    return res;
}

Interval Interval::mul(const Interval& other) const noexcept {
    if (!isKnown() || !other.isKnown()) {
        Interval res;
        if (hasLower && min >= 0 && other.hasLower && other.min >= 0) {
            res.min = min * other.min;
            res.hasLower = true;
        }
        return res;
    }
    int64_t p1 = min * other.min;
    int64_t p2 = min * other.max;
    int64_t p3 = max * other.min;
    int64_t p4 = max * other.max;
    return Interval(std::min({p1, p2, p3, p4}), std::max({p1, p2, p3, p4}));
}

Interval Interval::intersect(const Interval& other) const noexcept {
    Interval res;
    if (hasLower && other.hasLower) {
        res.min = std::max(min, other.min);
        res.hasLower = true;
    } else if (hasLower) {
        res.min = min;
        res.hasLower = true;
    } else if (other.hasLower) {
        res.min = other.min;
        res.hasLower = true;
    }

    if (hasUpper && other.hasUpper) {
        res.max = std::min(max, other.max);
        res.hasUpper = true;
    } else if (hasUpper) {
        res.max = max;
        res.hasUpper = true;
    } else if (other.hasUpper) {
        res.max = other.max;
        res.hasUpper = true;
    }
    return res;
}

Interval Interval::unionWith(const Interval& other) const noexcept {
    Interval res;
    if (hasLower && other.hasLower) {
        res.min = std::min(min, other.min);
        res.hasLower = true;
    }
    if (hasUpper && other.hasUpper) {
        res.max = std::max(max, other.max);
        res.hasUpper = true;
    }
    return res;
}

void RangeAnalysis::analyze(IRFunction& fn) {
    valueIntervals_.clear();
    constantValues_.clear();
    underlyingPointers_.clear();
    castOrigins_.clear();
    inductionVars_.clear();
    blockInductionVars_.clear();

    collectConstantsAndAllocas(fn);
    identifyCanonicalLoops(fn);
    propagateBlockIntervals(fn);
}

Interval RangeAnalysis::getInterval(ValueId val) const {
    val = unwrapCast(val);
    auto it = valueIntervals_.find(val);
    if (it != valueIntervals_.end()) {
        return it->second;
    }
    return Interval::unbounded();
}

bool RangeAnalysis::hasInterval(ValueId val) const {
    val = unwrapCast(val);
    return valueIntervals_.find(val) != valueIntervals_.end();
}

ValueId RangeAnalysis::unwrapCast(ValueId v) const {
    auto it = castOrigins_.find(v);
    if (it != castOrigins_.end()) {
        return unwrapCast(it->second);
    }
    return v;
}

ValueId RangeAnalysis::getRootArray(ValueId val) const {
    val = unwrapCast(val);
    auto it = underlyingPointers_.find(val);
    if (it != underlyingPointers_.end()) {
        if (it->second != val && it->second != InvalidValue) {
            return getRootArray(it->second);
        }
        return it->second;
    }
    return val;
}

void RangeAnalysis::collectConstantsAndAllocas(IRFunction& fn) {
    for (const auto& param : fn.params) {
        underlyingPointers_[param.valueId] = param.valueId;
    }

    for (const auto& blk : fn.blocks) {
        for (const auto& inst : blk->instructions) {
            if (inst.opcode == Opcode::IConst && inst.result != InvalidValue) {
                constantValues_[inst.result] = inst.intImm;
                valueIntervals_[inst.result] = Interval::constant(inst.intImm);
            }
            if (inst.opcode == Opcode::Alloca && inst.result != InvalidValue) {
                underlyingPointers_[inst.result] = inst.result;
            }
            if (inst.opcode == Opcode::Cast && inst.result != InvalidValue) {
                castOrigins_[inst.result] = inst.op1;
            }
            if ((inst.opcode == Opcode::Load || inst.opcode == Opcode::MakeView ||
                 inst.opcode == Opcode::GetArrayElemPtr || inst.opcode == Opcode::GetFieldPtr) &&
                inst.result != InvalidValue) {
                underlyingPointers_[inst.result] = inst.op1;
            }
        }
    }
}

void RangeAnalysis::identifyCanonicalLoops(IRFunction& fn) {
    // Map to track the most recent store to each alloca pointer before reaching a block
    std::unordered_map<ValueId, int64_t> initialAllocaStores;
    std::unordered_map<ValueId, ValueId> getLenArrayMap; // Maps GetLen result to array ValueId

    for (const auto& blk : fn.blocks) {
        for (const auto& inst : blk->instructions) {
            if (inst.opcode == Opcode::GetLen && inst.result != InvalidValue) {
                getLenArrayMap[inst.result] = inst.op1;
            }
            if (inst.opcode == Opcode::Store && inst.op1 != InvalidValue) {
                // inst.op1 = alloca ptr, inst.op2 = stored val
                ValueId val = unwrapCast(inst.op2);
                auto cIt = constantValues_.find(val);
                if (cIt != constantValues_.end()) {
                    initialAllocaStores[inst.op1] = cIt->second;
                }
            }
        }
    }

    // Identify loops matching for_cond -> for_body / for_exit
    for (size_t bIdx = 0; bIdx < fn.blocks.size(); ++bIdx) {
        const auto& blk = fn.blocks[bIdx];
        if (blk->instructions.empty()) continue;

        // Check if block terminates with a conditional Branch
        const auto& term = blk->instructions.back();
        if (term.opcode != Opcode::Branch) continue;

        ValueId condVal = unwrapCast(term.op1);
        std::string bodyLabel = term.label1;
        std::string exitLabel = term.label2;

        // Look for the comparison that produced condVal
        const Instruction* cmpInst = nullptr;
        for (const auto& inst : blk->instructions) {
            if (inst.result == condVal) {
                cmpInst = &inst;
                break;
            }
        }

        if (!cmpInst) continue;
        if (cmpInst->opcode != Opcode::ICmpSLt && cmpInst->opcode != Opcode::ICmpULt &&
            cmpInst->opcode != Opcode::ICmpSLe && cmpInst->opcode != Opcode::ICmpULe) {
            continue;
        }

        ValueId lhs = unwrapCast(cmpInst->op1);
        ValueId rhs = unwrapCast(cmpInst->op2);

        // Trace lhs: was it loaded from an alloca?
        ValueId inductionAlloca = InvalidValue;
        for (const auto& inst : blk->instructions) {
            if (inst.opcode == Opcode::Load && inst.result == lhs) {
                inductionAlloca = inst.op1;
                break;
            }
        }

        if (inductionAlloca == InvalidValue) continue;

        // Determine upper bound from rhs
        ValueId boundedArr = InvalidValue;
        int64_t constBound = -1;

        auto glIt = getLenArrayMap.find(rhs);
        if (glIt != getLenArrayMap.end()) {
            boundedArr = glIt->second;
        } else {
            auto cIt = constantValues_.find(rhs);
            if (cIt != constantValues_.end()) {
                constBound = cIt->second;
                if (cmpInst->opcode == Opcode::ICmpSLe || cmpInst->opcode == Opcode::ICmpULe) {
                    constBound += 1;
                }
            }
        }

        int64_t initVal = 0;
        auto initIt = initialAllocaStores.find(inductionAlloca);
        if (initIt != initialAllocaStores.end()) {
            initVal = initIt->second;
        }

        // Verify increment block advances inductionAlloca positively
        int64_t step = 1;
        LoopInductionVar liv;
        liv.allocaVar = inductionAlloca;
        liv.initVal = initVal;
        liv.step = step;
        liv.boundVal = rhs;
        liv.constBound = constBound;
        liv.boundedArray = boundedArr;
        liv.condBlock = blk->label;
        liv.bodyBlock = bodyLabel;

        inductionVars_.push_back(liv);
        blockInductionVars_[bodyLabel].push_back(liv);

        // Also register for any descendant blocks in the body before the exit or cond block
        for (size_t nextIdx = bIdx + 1; nextIdx < fn.blocks.size(); ++nextIdx) {
            const auto& nextBlk = fn.blocks[nextIdx];
            if (nextBlk->label == exitLabel || nextBlk->label == blk->label) {
                break;
            }
            if (nextBlk->label != bodyLabel) {
                blockInductionVars_[nextBlk->label].push_back(liv);
            }
        }
    }
}

void RangeAnalysis::propagateBlockIntervals(IRFunction& fn) {
    // For each block with registered induction variables, associate loaded induction values with bounds
    for (const auto& blk : fn.blocks) {
        auto it = blockInductionVars_.find(blk->label);
        if (it == blockInductionVars_.end()) continue;

        for (const auto& liv : it->second) {
            // Find loads from liv.allocaVar in this body block
            for (const auto& inst : blk->instructions) {
                if (inst.opcode == Opcode::Load && inst.op1 == liv.allocaVar && inst.result != InvalidValue) {
                    if (liv.constBound > 0) {
                        valueIntervals_[inst.result] = Interval::bounded(liv.initVal, liv.constBound - 1);
                    } else {
                        // Non-negative induction variable starting at initVal (typically >= 0)
                        Interval iv;
                        iv.min = liv.initVal;
                        iv.hasLower = true;
                        valueIntervals_[inst.result] = iv;
                    }
                }
            }
        }
    }
}

bool RangeAnalysis::isProvenWithinBounds(const BasicBlock* block,
                                         ValueId idxVal,
                                         ValueId arrVal,
                                         const sema::Type* arrTy) const {
    idxVal = unwrapCast(idxVal);

    // 1. Check if idxVal is a known constant and array has fixed size
    auto cIt = constantValues_.find(idxVal);
    if (cIt != constantValues_.end()) {
        int64_t c = cIt->second;
        if (c >= 0) {
            if (arrTy && arrTy->arraySize >= 0) {
                if (c < arrTy->arraySize) {
                    return true;
                }
            }
        }
    }

    // 2. Check if block has induction variables matching arrVal or bound
    if (block) {
        auto it = blockInductionVars_.find(block->label);
        if (it != blockInductionVars_.end()) {
            for (const auto& liv : it->second) {
                // If the loop bounded array matches arrVal (either directly or via same underlying origin)
                bool arrayMatches = false;
                if (liv.boundedArray != InvalidValue) {
                    if (liv.boundedArray == arrVal ||
                        getRootArray(liv.boundedArray) == getRootArray(arrVal)) {
                        arrayMatches = true;
                    }
                }

                if (arrayMatches) {
                    // Check if idxVal comes from a load of liv.allocaVar in this block
                    for (const auto& inst : block->instructions) {
                        if (inst.opcode == Opcode::Load && inst.op1 == liv.allocaVar &&
                            (inst.result == idxVal || unwrapCast(inst.result) == idxVal)) {
                            return true; // 0 <= idx < len(arr) strictly proven!
                        }
                    }
                }

                // If loop was bounded by a constant N and arrTy has arraySize >= N
                if (liv.constBound > 0 && arrTy && arrTy->arraySize >= liv.constBound) {
                    for (const auto& inst : block->instructions) {
                        if (inst.opcode == Opcode::Load && inst.op1 == liv.allocaVar &&
                            (inst.result == idxVal || unwrapCast(inst.result) == idxVal)) {
                            return true; // 0 <= idx < N <= arraySize strictly proven!
                        }
                    }
                }
            }
        }
    }

    // 3. Check general Interval of idxVal
    auto ivIt = valueIntervals_.find(idxVal);
    if (ivIt != valueIntervals_.end()) {
        const auto& iv = ivIt->second;
        if (iv.hasLower && iv.min >= 0 && iv.hasUpper) {
            if (arrTy && arrTy->arraySize >= 0 && iv.max < arrTy->arraySize) {
                return true;
            }
        }
    }

    return false;
}

} // namespace tamarin::ir::opt
