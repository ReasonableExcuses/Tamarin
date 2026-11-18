// ============================================================================
// ir.h - Tamarin Intermediate Representation (TIR)
// ============================================================================
#pragma once

#include "tamarin/sema/types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tamarin::ir {

// ── Value identifiers ───────────────────────────────────────────────────────
using ValueId = uint32_t;
constexpr ValueId InvalidValue = 0xFFFFFFFF;

// ── IR Opcode ───────────────────────────────────────────────────────────────
enum class Opcode {
    // Special
    Nop,

    // Constants
    IConst,
    FConst,
    BoolConst,
    StringConst,

    // Arithmetic
    Add, Sub, Mul, SDiv, UDiv, SRem, URem,
    FAdd, FSub, FMul, FDiv,

    // Bitwise
    And, Or, Xor, Shl, AShr, LShr,

    // Comparison
    ICmpEq, ICmpNe, ICmpSLt, ICmpSLe, ICmpSGt, ICmpSGe,
    ICmpULt, ICmpULe, ICmpUGt, ICmpUGe,
    FCmpEq, FCmpNe, FCmpLt, FCmpLe, FCmpGt, FCmpGe,

    // Memory
    Alloca,
    Load,
    Store,

    // Control flow
    Branch,
    Jump,
    Return,
    Call,
    FunctionPtr,   // Pointer to function or lambda: strImm = fn_name, type = fn_type, result = ptr
    GlobalPtr,     // Pointer to global variable: strImm = global_name, type = ptr, result = ptr

    // Tamarin-specific: safety and arrays
    ArrayAlloc,    // Allocate local array with length: (elementType, length) -> {ptr, len}
    ArrayLoad,     // Load from array with bounds check info: (array, index) -> val
    ArrayStore,    // Store to array with bounds check info: (array, index, val)
    BoundsCheck,   // Explicit bounds check: (index, length) -> abort if out of bounds
    GetLen,        // Extract length from array/view: (array) -> i64
    MakeView,      // Construct array view from (ptr, len) -> {ptr, len}
    GetFieldPtr,   // GEP to struct field: op1 = struct_ptr, intImm = field_index, type = field_type, strImm = struct_name
    GetArrayElemPtr,// GEP to array element: op1 = array_val, op2 = index_val, type = elem_type
    CallVirtual,   // Dynamic dispatch via vtable: op1 = this_ptr, intImm = vtable_slot, strImm = method_name
    GetVTablePtr,  // Pointer to class vtable: strImm = class_name
    SoAArrayAlloc, // Allocate SoA bundle { [N x F0], [N x F1], ... }
    SoAGetFieldPtr,// GEP to field array in SoA: op1 = bundle_ptr, op2 = index, intImm = field_index, type = field_type

    // Regions & Arena Allocation
    RegionInit,    // Initialize region: op1 = region_ptr, intImm = initial_cap
    RegionAlloc,   // Allocate from region: op1 = region_ptr, intImm = size, strImm = align, result = ptr
    RegionReserveDestructor,  // Reserve destructor slot in sidecar before construction: op1 = region_ptr, result = slot_ptr
    RegionActivateDestructor, // Activate destructor slot after construction: op1 = region_ptr, op2 = slot_ptr, op3 = obj_ptr, strImm = dtor_fn, intImm = count, intImm2 = stride
    RegionRegisterDestructor, // Register destructor in region sidecar (combined reserve+activate): op1 = region_ptr, op2 = obj_ptr, strImm = dtor_fn, intImm = count, intImm2 = stride
    RegionDestroy, // Destroy region and reclaim backing memory: op1 = region_ptr

    // Slabs & Recyclable Generational Memory
    SlabInit,      // Initialize slab: op1 = slab_ptr, intImm = initial_cap, intImm2 = elem_size, strImm = elem_align
    SlabInsert,    // Insert into slab: op1 = slab_ptr, op2 = val_ptr, result = handle (i64)
    SlabGet,       // Safe dereference: op1 = slab_ptr, op2 = handle, result = elem_ptr
    SlabIsValid,   // Check handle: op1 = slab_ptr, op2 = handle, result = bool
    SlabTryGet,    // Fallible get: op1 = slab_ptr, op2 = handle, op3 = out_ptr, result = bool
    SlabRemove,    // Remove handle: op1 = slab_ptr, op2 = handle, strImm = dtor_fn, result = bool
    SlabCount,     // Active count: op1 = slab_ptr, result = u64
    SlabCapacity,  // Capacity: op1 = slab_ptr, result = u64
    SlabClear,     // Clear all: op1 = slab_ptr, strImm = dtor_fn
    SlabDestroy,   // Destroy slab: op1 = slab_ptr, strImm = dtor_fn

    // Casts
    Cast,
    Phi,

    // Algebraic Data Types & Pattern Matching
    TagGet,            // Extract discriminant tag: op1 = union_ptr, result = tag (i32)
    TagSet,            // Set discriminant tag: op1 = union_ptr, intImm = tag (i32)
    VariantPayloadPtr, // Pointer to payload field: op1 = union_ptr, intImm = field_index, type = field_type, result = ptr
    Switch,            // Multi-way branch: op1 = switch_val, label1 = default_label, switchCases = { (val, label) }

    // Dynamic Heap & Memory Primitives
    MemAlloc,          // Allocate dynamic buffer: op1 = count, type = ArrayView<T>, result = {ptr, count}
    MemRealloc,        // Reallocate dynamic buffer: op1 = slice, op2 = new_count, type = ArrayView<T>, result = {new_ptr, new_count}
    MemFree,           // Free dynamic buffer: op1 = slice
    MemCopy,           // Fast memory copy: callArgs = {dst, dstOff, src, srcOff, count}, type = elem_type
    MemMove,           // Fast memory move: callArgs = {dst, dstOff, src, srcOff, count}, type = elem_type
    MemSet,            // Fast memory set: callArgs = {dst, dstOff, byteVal, count}, type = elem_type

    // Native SIMD Vector Operations
    VecSplat,          // Broadcast scalar to vector: op1 = scalar, type = SIMDVector
    VecExtract,        // Extract lane at index: op1 = vector, intImm = lane, type = elem_type
    VecInsert,         // Insert scalar into vector lane: op1 = vector, op2 = scalar, intImm = lane, type = SIMDVector
    VecShuffle,        // Shuffle vector lanes: op1 = v1, op2 = v2, strImm = mask
    VecReduceAdd,      // Horizontal add reduction: op1 = vector, type = elem_type
    VecDot,            // Vector dot product: op1 = v1, op2 = v2, type = elem_type

    // Structured Concurrency & Parallel Execution
    ParallelFor,       // Parallel for loop dispatch: op1 = start, op2 = end, op3 = grain, strImm = worker_fn
    TaskScopeBegin,    // Begin task scope: result = scope_handle
    TaskScopeEnd,      // End task scope: op1 = scope_handle
    TaskSpawn,         // Spawn task: op1 = scope_handle, strImm = task_fn

    // Asynchronous Tasks, Coroutines & Fibers
    AsyncTaskCreate,   // Create async task: op1 = ctx_ptr, op2 = ctx_size, op3 = result_size, strImm = worker_fn -> result = task_handle
    AsyncTaskAwait,    // Await async task: op1 = task_handle -> result = result_ptr
    AsyncTaskIsReady,  // Check if async task ready: op1 = task_handle -> result = i32
    AsyncTaskFree,     // Free async task handle: op1 = task_handle

    // Hardware Atomics & Reductions
    AtomicRMW,         // op1 = ptr, op2 = val, intImm = AtomicOp, type = val_type
    AtomicLoad,        // op1 = ptr, type = val_type, result = val
    AtomicStore,       // op1 = ptr, op2 = val, type = val_type
    AtomicCmpXchg,     // op1 = ptr, op2 = expected, op3 = desired, type = val_type, result = bool
};

// ── Instruction ─────────────────────────────────────────────────────────────
struct Instruction {
    Opcode opcode;
    ValueId result = InvalidValue;
    const sema::Type* type = nullptr;

    // Operands
    ValueId op1 = InvalidValue;
    ValueId op2 = InvalidValue;
    ValueId op3 = InvalidValue;

    // Immediates
    int64_t intImm = 0;
    int64_t intImm2 = 0;
    double  floatImm = 0.0;
    std::string strImm;

    // Control flow targets
    std::string label1;
    std::string label2;

    // Loop optimization hints
    int32_t unrollCount = 0;
    int32_t vectorizeWidth = 0;
    int32_t interleaveCount = 0;
    int32_t vectorizeEnable = 0;

    // Arguments for calls
    std::vector<ValueId> callArgs;

    // Multi-way branch cases for Switch: (case_val, target_label)
    std::vector<std::pair<int64_t, std::string>> switchCases;
};

// ── Basic Block ─────────────────────────────────────────────────────────────
struct BasicBlock {
    std::string label;
    std::vector<Instruction> instructions;

    explicit BasicBlock(std::string lbl) : label(std::move(lbl)) {}

    bool hasTerminator() const {
        if (instructions.empty()) return false;
        auto op = instructions.back().opcode;
        return op == Opcode::Return || op == Opcode::Jump || op == Opcode::Branch || op == Opcode::Switch;
    }
};

// ── IR Function ─────────────────────────────────────────────────────────────
struct IRParam {
    std::string name;
    const sema::Type* type;
    ValueId valueId;
};

struct IRFunction {
    std::string name;
    const sema::Type* returnType;
    std::vector<IRParam> params;
    std::vector<std::unique_ptr<BasicBlock>> blocks;
    bool isNoInline = false;
    bool isAlwaysInline = false;
    bool isPublic = false;
    bool isLinkOnceODR = false;
    bool isExtern = false;
    std::string linkage = "";
    bool isExported = false;

    BasicBlock* createBlock(std::string label) {
        blocks.push_back(std::make_unique<BasicBlock>(std::move(label)));
        return blocks.back().get();
    }
};

// ── IR Global Variable ──────────────────────────────────────────────────────
struct IRGlobal {
    std::string name;
    const sema::Type* type = nullptr;
    int64_t initialValue = 0;
    bool isConst = false;
    bool isArray = false;
    int64_t arraySize = 0;
};

// ── IR Module ───────────────────────────────────────────────────────────────
struct IRModule {
    std::string name;
    std::vector<std::unique_ptr<IRFunction>> functions;
    std::vector<IRGlobal> globals;
    std::vector<const sema::Type*> recordTypes;
    std::vector<const sema::Type*> enumTypes;
    std::vector<std::string> stringLiterals;

    IRFunction* createFunction(std::string fnName, const sema::Type* retTy) {
        auto fn = std::make_unique<IRFunction>();
        fn->name = std::move(fnName);
        fn->returnType = retTy;
        functions.push_back(std::move(fn));
        return functions.back().get();
    }

    void dump(std::ostream& os) const;
};

} // namespace tamarin::ir

