// ============================================================================
// alias_analysis.cpp - Implementation of Memory Disjointness Analysis
// ============================================================================
#include "tamarin/ir/opt/alias_analysis.h"

namespace tamarin::ir::opt {

bool AliasAnalysis::runOnFunction(IRFunction& fn, const sema::TypeContext* /*typeCtx*/) {
    provenanceMap_.clear();
    noaliasValues_.clear();
    noaliasParams_.clear();

    // 1. Analyze function parameters
    for (const auto& param : fn.params) {
        if (!param.type) continue;
        if (param.type->isReference() || param.type->isArrayView()) {
            PointerProvenance prov;
            prov.origin = StorageOrigin::ParamRef;
            prov.baseOriginId = param.valueId;
            provenanceMap_[param.valueId] = prov;
            noaliasParams_.insert(param.valueId);
            noaliasValues_.insert(param.valueId);
        }
    }

    // 2. Scan instructions for allocations and pointer derivations
    for (const auto& blk : fn.blocks) {
        for (const auto& inst : blk->instructions) {
            if (inst.result == InvalidValue) continue;

            switch (inst.opcode) {
                case Opcode::Alloca: {
                    PointerProvenance prov;
                    prov.origin = StorageOrigin::DistinctAlloca;
                    prov.baseOriginId = inst.result;
                    provenanceMap_[inst.result] = prov;
                    noaliasValues_.insert(inst.result);
                    break;
                }
                case Opcode::ArrayAlloc:
                case Opcode::SoAArrayAlloc: {
                    PointerProvenance prov;
                    prov.origin = StorageOrigin::DistinctAlloca;
                    prov.baseOriginId = inst.result;
                    provenanceMap_[inst.result] = prov;
                    noaliasValues_.insert(inst.result);
                    break;
                }
                case Opcode::RegionAlloc: {
                    PointerProvenance prov;
                    prov.origin = StorageOrigin::DistinctRegion;
                    prov.baseOriginId = inst.result;
                    provenanceMap_[inst.result] = prov;
                    noaliasValues_.insert(inst.result);
                    break;
                }
                case Opcode::MemAlloc: {
                    PointerProvenance prov;
                    prov.origin = StorageOrigin::DynamicHeap;
                    prov.baseOriginId = inst.result;
                    provenanceMap_[inst.result] = prov;
                    noaliasValues_.insert(inst.result);
                    break;
                }
                case Opcode::GetArrayElemPtr:
                case Opcode::GetFieldPtr:
                case Opcode::SoAGetFieldPtr: {
                    auto it = provenanceMap_.find(inst.op1);
                    if (it != provenanceMap_.end()) {
                        provenanceMap_[inst.result] = it->second;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    return false; // Analysis pass only; does not modify IR structure
}

bool AliasAnalysis::areDisjoint(ValueId ptrA, ValueId ptrB) const {
    if (ptrA == ptrB) return false;

    auto itA = provenanceMap_.find(ptrA);
    auto itB = provenanceMap_.find(ptrB);

    if (itA == provenanceMap_.end() || itB == provenanceMap_.end()) {
        return false;
    }

    const auto& provA = itA->second;
    const auto& provB = itB->second;

    // Distinct stack allocas never overlap
    if (provA.origin == StorageOrigin::DistinctAlloca && provB.origin == StorageOrigin::DistinctAlloca) {
        return provA.baseOriginId != provB.baseOriginId;
    }

    // Stack alloca vs distinct region or heap never overlap
    if (provA.origin == StorageOrigin::DistinctAlloca &&
        (provB.origin == StorageOrigin::DistinctRegion || provB.origin == StorageOrigin::DynamicHeap)) {
        return true;
    }
    if (provB.origin == StorageOrigin::DistinctAlloca &&
        (provA.origin == StorageOrigin::DistinctRegion || provA.origin == StorageOrigin::DynamicHeap)) {
        return true;
    }

    // Distinct region allocations never overlap
    if (provA.origin == StorageOrigin::DistinctRegion && provB.origin == StorageOrigin::DistinctRegion) {
        return provA.baseOriginId != provB.baseOriginId;
    }

    // Distinct param references or views do not alias in Tamarin's memory model
    if (provA.origin == StorageOrigin::ParamRef && provB.origin == StorageOrigin::ParamRef) {
        return provA.baseOriginId != provB.baseOriginId;
    }

    return false;
}

bool AliasAnalysis::isParamNoAlias(ValueId paramVal) const {
    return noaliasParams_.find(paramVal) != noaliasParams_.end();
}

} // namespace tamarin::ir::opt
