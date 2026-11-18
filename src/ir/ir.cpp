// ============================================================================
// ir.cpp - Tamarin IR printer and utility implementation
// ============================================================================

#include "tamarin/ir/ir.h"
#include <iostream>

namespace tamarin::ir {

static const char* opcodeToString(Opcode op) {
    switch (op) {
        case Opcode::Nop:           return "nop";
        case Opcode::IConst:      return "iconst";
        case Opcode::FConst:      return "fconst";
        case Opcode::BoolConst:   return "bconst";
        case Opcode::Add:         return "add";
        case Opcode::Sub:         return "sub";
        case Opcode::Mul:         return "mul";
        case Opcode::SDiv:        return "sdiv";
        case Opcode::UDiv:        return "udiv";
        case Opcode::SRem:        return "srem";
        case Opcode::URem:        return "urem";
        case Opcode::FAdd:        return "fadd";
        case Opcode::FSub:        return "fsub";
        case Opcode::FMul:        return "fmul";
        case Opcode::FDiv:        return "fdiv";
        case Opcode::And:         return "and";
        case Opcode::Or:          return "or";
        case Opcode::Xor:         return "xor";
        case Opcode::Shl:         return "shl";
        case Opcode::AShr:        return "ashr";
        case Opcode::LShr:        return "lshr";
        case Opcode::ICmpEq:      return "icmp.eq";
        case Opcode::ICmpNe:      return "icmp.ne";
        case Opcode::ICmpSLt:     return "icmp.slt";
        case Opcode::ICmpSLe:     return "icmp.sle";
        case Opcode::ICmpSGt:     return "icmp.sgt";
        case Opcode::ICmpSGe:     return "icmp.sge";
        case Opcode::ICmpULt:     return "icmp.ult";
        case Opcode::ICmpULe:     return "icmp.ule";
        case Opcode::ICmpUGt:     return "icmp.ugt";
        case Opcode::ICmpUGe:     return "icmp.uge";
        case Opcode::FCmpEq:      return "fcmp.eq";
        case Opcode::FCmpNe:      return "fcmp.ne";
        case Opcode::FCmpLt:      return "fcmp.lt";
        case Opcode::FCmpLe:      return "fcmp.le";
        case Opcode::FCmpGt:      return "fcmp.gt";
        case Opcode::FCmpGe:      return "fcmp.ge";
        case Opcode::Alloca:      return "alloca";
        case Opcode::Load:        return "load";
        case Opcode::Store:       return "store";
        case Opcode::Branch:      return "branch";
        case Opcode::Jump:        return "jump";
        case Opcode::Return:      return "ret";
        case Opcode::Call:        return "call";
        case Opcode::FunctionPtr: return "fn.ptr";
        case Opcode::GlobalPtr:   return "global.ptr";
        case Opcode::ArrayAlloc:  return "array.alloc";
        case Opcode::ArrayLoad:   return "array.load";
        case Opcode::ArrayStore:  return "array.store";
        case Opcode::BoundsCheck: return "bounds.check";
        case Opcode::GetLen:      return "get.len";
        case Opcode::MakeView:    return "make.view";
        case Opcode::GetFieldPtr:   return "getfieldptr";
        case Opcode::CallVirtual:   return "call.virtual";
        case Opcode::SoAArrayAlloc: return "soa.alloc";
        case Opcode::SoAGetFieldPtr:return "soa.getfieldptr";
        case Opcode::RegionInit:    return "region.init";
        case Opcode::RegionAlloc:   return "region.alloc";
        case Opcode::RegionRegisterDestructor: return "region.register_dtor";
        case Opcode::RegionDestroy: return "region.destroy";
        case Opcode::SlabInit:      return "slab.init";
        case Opcode::SlabInsert:    return "slab.insert";
        case Opcode::SlabGet:       return "slab.get";
        case Opcode::SlabIsValid:   return "slab.is_valid";
        case Opcode::SlabTryGet:    return "slab.try_get";
        case Opcode::SlabRemove:    return "slab.remove";
        case Opcode::SlabCount:     return "slab.count";
        case Opcode::SlabCapacity:  return "slab.capacity";
        case Opcode::SlabClear:     return "slab.clear";
        case Opcode::SlabDestroy:   return "slab.destroy";
        case Opcode::Cast:          return "cast";
        case Opcode::Phi:         return "phi";
        case Opcode::TagGet:        return "tag.get";
        case Opcode::TagSet:        return "tag.set";
        case Opcode::VariantPayloadPtr: return "variant.payload_ptr";
        case Opcode::Switch:        return "switch";
        case Opcode::MemAlloc:      return "mem.alloc";
        case Opcode::MemRealloc:    return "mem.realloc";
        case Opcode::MemFree:       return "mem.free";
        case Opcode::MemCopy:       return "mem.copy";
        case Opcode::MemMove:       return "mem.move";
        case Opcode::MemSet:        return "mem.set";
        case Opcode::VecSplat:      return "vec.splat";
        case Opcode::VecExtract:    return "vec.extract";
        case Opcode::VecInsert:     return "vec.insert";
        case Opcode::VecShuffle:    return "vec.shuffle";
        case Opcode::VecReduceAdd:  return "vec.reduce_add";
        case Opcode::VecDot:        return "vec.dot";
        case Opcode::ParallelFor:   return "parallel.for";
        case Opcode::TaskScopeBegin: return "task_scope.begin";
        case Opcode::TaskScopeEnd:   return "task_scope.end";
        case Opcode::TaskSpawn:      return "task.spawn";
        case Opcode::AtomicRMW:      return "atomic.rmw";
        case Opcode::AtomicLoad:     return "atomic.load";
        case Opcode::AtomicStore:    return "atomic.store";
        case Opcode::AtomicCmpXchg:  return "atomic.cmpxchg";
    }
    return "<unknown>";
}

void IRModule::dump(std::ostream& os) const {
    os << "; Tamarin Module: " << name << "\n\n";
    for (auto& fn : functions) {
        os << "fn @" << fn->name << "(";
        for (size_t i = 0; i < fn->params.size(); ++i) {
            if (i > 0) os << ", ";
            os << "%v" << fn->params[i].valueId << ": "
               << (fn->params[i].type ? fn->params[i].type->toString() : "?");
        }
        os << ") -> " << (fn->returnType ? fn->returnType->toString() : "void") << " {\n";

        for (auto& block : fn->blocks) {
            os << block->label << ":\n";
            for (auto& inst : block->instructions) {
                os << "  ";
                if (inst.result != InvalidValue) {
                    os << "%v" << inst.result << " = ";
                }
                os << opcodeToString(inst.opcode);

                switch (inst.opcode) {
                    case Opcode::IConst:
                        os << " " << inst.intImm;
                        break;
                    case Opcode::FConst:
                        os << " " << inst.floatImm;
                        break;
                    case Opcode::BoolConst:
                        os << " " << (inst.intImm ? "true" : "false");
                        break;
                    case Opcode::Jump:
                        os << " " << inst.label1;
                        break;
                    case Opcode::Branch:
                        os << " %v" << inst.op1 << ", " << inst.label1 << ", " << inst.label2;
                        break;
                    case Opcode::Return:
                        if (inst.op1 != InvalidValue) os << " %v" << inst.op1;
                        break;
                    case Opcode::Call:
                        os << " @" << inst.strImm << "(";
                        for (size_t i = 0; i < inst.callArgs.size(); ++i) {
                            if (i > 0) os << ", ";
                            os << "%v" << inst.callArgs[i];
                        }
                        os << ")";
                        break;
                    default:
                        if (inst.op1 != InvalidValue) os << " %v" << inst.op1;
                        if (inst.op2 != InvalidValue) os << ", %v" << inst.op2;
                        if (inst.op3 != InvalidValue) os << ", %v" << inst.op3;
                        break;
                }
                os << "\n";
            }
        }
        os << "}\n\n";
    }
}

} // namespace tamarin::ir

