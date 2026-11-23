// ============================================================================
// range_analysis.cpp - Implementation of Symbolic Range Analysis
// ============================================================================
#include "tamarin/ir/opt/range_analysis.h"
#include <unordered_set>

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
    if (res.hasLower && res.hasUpper && res.min > res.max) {
        res.hasLower = false;
        res.hasUpper = false;
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

    std::unordered_map<std::string, const BasicBlock*> blockMap;
    std::unordered_map<std::string, std::vector<const BasicBlock*>> predecessors;
    for (const auto& blk : fn.blocks) {
        blockMap[blk->label] = blk.get();
    }
    for (const auto& blk : fn.blocks) {
        if (blk->instructions.empty()) continue;
        const auto& term = blk->instructions.back();
        if (term.opcode == Opcode::Jump) {
            predecessors[term.label1].push_back(blk.get());
        } else if (term.opcode == Opcode::Branch) {
            predecessors[term.label1].push_back(blk.get());
            predecessors[term.label2].push_back(blk.get());
        } else if (term.opcode == Opcode::Switch) {
            predecessors[term.label1].push_back(blk.get());
            for (const auto& sc : term.switchCases) {
                predecessors[sc.second].push_back(blk.get());
            }
        }
    }

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
            if (cmpInst->opcode == Opcode::ICmpSLt || cmpInst->opcode == Opcode::ICmpULt) {
                boundedArr = glIt->second;
            }
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
        bool hasInitStore = false;

        auto predIt = predecessors.find(blk->label);
        if (predIt != predecessors.end()) {
            for (const auto* pred : predIt->second) {
                for (auto rIt = pred->instructions.rbegin(); rIt != pred->instructions.rend(); ++rIt) {
                    if (rIt->opcode == Opcode::Store && rIt->op1 == inductionAlloca) {
                        ValueId val = unwrapCast(rIt->op2);
                        auto cIt = constantValues_.find(val);
                        if (cIt != constantValues_.end()) {
                            initVal = cIt->second;
                            hasInitStore = true;
                            break;
                        }
                    }
                }
                if (hasInitStore) break;
            }
        }
        if (!hasInitStore) {
            auto initIt = initialAllocaStores.find(inductionAlloca);
            if (initIt != initialAllocaStores.end()) {
                initVal = initIt->second;
                hasInitStore = true;
            }
        }
        if (!hasInitStore) {
            continue;
        }

        // Collect all blocks belonging to the loop body via CFG reachability
        std::unordered_set<std::string> loopBlocks;
        std::vector<std::string> worklist;
        worklist.push_back(bodyLabel);
        loopBlocks.insert(bodyLabel);

        while (!worklist.empty()) {
            std::string curr = worklist.back();
            worklist.pop_back();

            auto bIt = blockMap.find(curr);
            if (bIt == blockMap.end()) continue;
            const auto* currBlk = bIt->second;
            if (currBlk->instructions.empty()) continue;

            const auto& t = currBlk->instructions.back();
            auto addSucc = [&](const std::string& succ) {
                if (succ.empty()) return;
                if (succ == exitLabel || succ == blk->label) return;
                if (loopBlocks.insert(succ).second) {
                    worklist.push_back(succ);
                }
            };

            if (t.opcode == Opcode::Jump) {
                addSucc(t.label1);
            } else if (t.opcode == Opcode::Branch) {
                addSucc(t.label1);
                addSucc(t.label2);
            } else if (t.opcode == Opcode::Switch) {
                addSucc(t.label1);
                for (const auto& sc : t.switchCases) {
                    addSucc(sc.second);
                }
            }
        }

        // Verify that inductionAlloca is modified only once in the loop by a step-1 increment,
        // and that boundedArr is not modified in the loop body.
        bool validIncrement = false;
        int storeCount = 0;
        bool arrayModified = false;
        ValueId rootBoundedArr = (boundedArr != InvalidValue) ? getRootArray(boundedArr) : InvalidValue;

        for (const auto& lbl : loopBlocks) {
            auto bIt = blockMap.find(lbl);
            if (bIt == blockMap.end()) continue;
            const auto* lBlk = bIt->second;

            for (const auto& inst : lBlk->instructions) {
                if (boundedArr != InvalidValue && inst.opcode == Opcode::Store) {
                    if (inst.op1 == boundedArr || inst.op1 == rootBoundedArr) {
                        arrayModified = true;
                    }
                }
                if (inst.opcode == Opcode::Store && inst.op1 == inductionAlloca) {
                    storeCount++;
                    ValueId rhsVal = inst.op2;
                    for (const auto& prevInst : lBlk->instructions) {
                        if (prevInst.result == rhsVal && prevInst.opcode == Opcode::Add) {
                            ValueId addOp1 = prevInst.op1;
                            ValueId addOp2 = prevInst.op2;
                            auto c1 = constantValues_.find(addOp1);
                            auto c2 = constantValues_.find(addOp2);
                            bool hasOne = (c1 != constantValues_.end() && c1->second == 1) ||
                                          (c2 != constantValues_.end() && c2->second == 1);
                            ValueId loadCandidate = (c1 != constantValues_.end() && c1->second == 1) ? addOp2 : addOp1;
                            for (const auto& loadInst : lBlk->instructions) {
                                if (loadInst.result == loadCandidate && loadInst.opcode == Opcode::Load && loadInst.op1 == inductionAlloca) {
                                    if (hasOne) {
                                        validIncrement = true;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (arrayModified) {
            boundedArr = InvalidValue;
        }

        if (boundedArr == InvalidValue && constBound <= 0) {
            continue;
        }

        if (storeCount != 1 || !validIncrement) {
            continue; // Non-monotonic or modified in body; cannot eliminate bounds checks
        }

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
        for (const auto& lbl : loopBlocks) {
            blockInductionVars_[lbl].push_back(liv);
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
    if (arrTy && arrTy->isReference()) arrTy = arrTy->elementType;
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

                if (arrayMatches && liv.initVal >= 0) {
                    // Check if idxVal comes from a load of liv.allocaVar in this block
                    for (const auto& inst : block->instructions) {
                        if (inst.opcode == Opcode::Load && inst.op1 == liv.allocaVar &&
                            (inst.result == idxVal || unwrapCast(inst.result) == idxVal)) {
                            return true; // 0 <= idx < len(arr) strictly proven!
                        }
                    }
                }

                // If loop was bounded by a constant N and arrTy has arraySize >= N
                if (liv.constBound > 0 && arrTy && arrTy->arraySize >= liv.constBound && liv.initVal >= 0) {
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
