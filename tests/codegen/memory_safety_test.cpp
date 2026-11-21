// ============================================================================
// memory_safety_test.cpp - 50 Unit tests for Tamarin compiler memory safety
// ============================================================================

#include "test_utils.h"

using namespace test_utils;

// ── 1-4: Parameter Move & Ownership Transfer ───────────────────────────────

TAMARIN_TEST(MemorySafety, ParamMove_UseAfterMoveRejection) {
    EXPECT_TRUE(checkInvalid(
        "struct Res { int h; ~Res() {} };\n"
        "int consume(Res r) {\n"
        "    Res b = move(r);\n"
        "    return r.h;\n"
        "}\n",
        "use of moved variable 'r'"));
    return true;
}

TAMARIN_TEST(MemorySafety, ParamMove_DoubleMoveRejection) {
    EXPECT_TRUE(checkInvalid(
        "struct Res { int h; ~Res() {} };\n"
        "void helper(Res r) {}\n"
        "void test(Res r) {\n"
        "    helper(move(r));\n"
        "    helper(move(r));\n"
        "}\n",
        "use of moved variable 'r'"));
    return true;
}

TAMARIN_TEST(MemorySafety, ParamMove_ReassignedCanBeRead) {
    EXPECT_TRUE(checkValid(
        "struct Res { int h; ~Res() {} };\n"
        "int test(Res r) {\n"
        "    Res b = move(r);\n"
        "    Res fresh;\n"
        "    fresh.h = 100;\n"
        "    r = move(fresh);\n"
        "    return r.h;\n"
        "}\n"
        "int main() { return 0; }\n"));
    return true;
}

TAMARIN_TEST(MemorySafety, ParamMove_ConditionalBranchMove) {
    EXPECT_TRUE(checkInvalid(
        "struct Res { int h; ~Res() {} };\n"
        "void helper(Res r) {}\n"
        "int test(Res r, bool cond) {\n"
        "    if (cond) {\n"
        "        helper(move(r));\n"
        "    }\n"
        "    return r.h;\n"
        "}\n",
        "use of moved variable 'r'"));
    return true;
}

// ── 5-8: Local Variable Move Semantics ──────────────────────────────────────

TAMARIN_TEST(MemorySafety, LocalMove_UseAfterMoveRejection) {
    EXPECT_TRUE(checkInvalid(
        "struct Buffer { int id; ~Buffer() {} };\n"
        "int main() {\n"
        "    Buffer a;\n"
        "    a.id = 10;\n"
        "    Buffer b = move(a);\n"
        "    return a.id;\n"
        "}\n",
        "use of moved variable 'a'"));
    return true;
}

TAMARIN_TEST(MemorySafety, LocalMove_DoubleMoveRejection) {
    EXPECT_TRUE(checkInvalid(
        "struct Buffer { int id; ~Buffer() {} };\n"
        "int main() {\n"
        "    Buffer a;\n"
        "    Buffer b = move(a);\n"
        "    Buffer c = move(a);\n"
        "    return 0;\n"
        "}\n",
        "use of moved variable 'a'"));
    return true;
}

TAMARIN_TEST(MemorySafety, LocalMove_ReassignmentRestoresState) {
    EXPECT_TRUE(checkValid(
        "struct Buffer { int id; ~Buffer() {} };\n"
        "int main() {\n"
        "    Buffer a;\n"
        "    a.id = 1;\n"
        "    Buffer b = move(a);\n"
        "    Buffer c;\n"
        "    c.id = 2;\n"
        "    a = move(c);\n"
        "    return a.id;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(MemorySafety, LocalMove_LoopBodyMoveWithoutReassignment) {
    EXPECT_TRUE(checkInvalid(
        "struct Buffer { int id; ~Buffer() {} };\n"
        "void sink(Buffer b) {}\n"
        "int main() {\n"
        "    Buffer a;\n"
        "    a.id = 5;\n"
        "    for (int i = 0; i < 3; i++) {\n"
        "        sink(move(a));\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "use of moved variable 'a'"));
    return true;
}

// ── 9-17: Borrow Checker Invariants ────────────────────────────────────────

TAMARIN_TEST(MemorySafety, Borrow_ExclusiveMutableRefPreventsShared) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    int x = 42;\n"
        "    int& mutRef = x;\n"
        "    const int& sharedRef = x;\n"
        "    return 0;\n"
        "}\n",
        "cannot borrow 'x' as immutable while also borrowed as mutable"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_SharedRefPreventsMutable) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    int x = 42;\n"
        "    const int& r1 = x;\n"
        "    int& r2 = x;\n"
        "    return 0;\n"
        "}\n",
        "cannot borrow 'x' as mutable while also borrowed as immutable"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_MoveWhileBorrowedRejection) {
    EXPECT_TRUE(checkInvalid(
        "struct Node { int val; ~Node() {} };\n"
        "int main() {\n"
        "    Node n;\n"
        "    Node& ref = n;\n"
        "    Node moved = move(n);\n"
        "    return 0;\n"
        "}\n",
        "cannot move 'n' while active pointer"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_MutateWhileBorrowedRejection) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    int x = 10;\n"
        "    const int& r = x;\n"
        "    x = 20;\n"
        "    return r;\n"
        "}\n",
        "cannot mutate 'x' while active pointer"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_ReturnLocalRefRejected) {
    EXPECT_TRUE(checkInvalid(
        "int& getDangling() {\n"
        "    int localVal = 55;\n"
        "    return localVal;\n"
        "}\n",
        "cannot return reference to local variable 'localVal'"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_ReturnLocalStructFieldRefRejected) {
    EXPECT_TRUE(checkInvalid(
        "struct Box { int data; };\n"
        "int& getFieldRef() {\n"
        "    Box b;\n"
        "    b.data = 99;\n"
        "    return b.data;\n"
        "}\n",
        "cannot return reference to local variable 'b'"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_EscapeRefToOuterBlockRejected) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    int* p = null;\n"
        "    {\n"
        "        int inner = 123;\n"
        "        p = &inner;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "cannot"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_RefExpiryAtBlockEndAllowsMutation) {
    EXPECT_TRUE(checkValid(
        "int main() {\n"
        "    int x = 10;\n"
        "    {\n"
        "        const int& r = x;\n"
        "        int y = r;\n"
        "    }\n"
        "    x = 20;\n"
        "    return x;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(MemorySafety, Borrow_ReborrowChainValidation) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    int x = 100;\n"
        "    int& r1 = x;\n"
        "    int& r2 = r1;\n"
        "    r2 = 200;\n"
        "    return x;\n"
        "}\n",
        "cannot borrow 'x' as mutable more than once at a time; already borrowed by 'r1'"));
    return true;
}

// ── 18-28: RAII Destructors & Cleanups ──────────────────────────────────────

TAMARIN_TEST(MemorySafety, Destructor_LIFOExecutionOrderCodegen) {
    EXPECT_TRUE(checkCodegen(
        "struct A { int a; ~A() {} };\n"
        "struct B { int b; ~B() {} };\n"
        "int main() {\n"
        "    A x;\n"
        "    B y;\n"
        "    return 0;\n"
        "}\n",
        "call void @B_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_ReverseMemberOrderCodegen) {
    EXPECT_TRUE(checkCodegen(
        "struct Member1 { int x; ~Member1() {} };\n"
        "struct Member2 { int y; ~Member2() {} };\n"
        "struct Composite {\n"
        "    Member1 m1;\n"
        "    Member2 m2;\n"
        "    ~Composite() {}\n"
        "};\n"
        "int main() {\n"
        "    Composite c;\n"
        "    return 0;\n"
        "}\n",
        "Composite_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_DerivedBeforeBaseCallCodegen) {
    EXPECT_TRUE(checkCodegen(
        "class Base {\n"
        "    int b;\n"
        "    ~Base() {}\n"
        "};\n"
        "class Derived : Base {\n"
        "    int d;\n"
        "    ~Derived() {}\n"
        "};\n"
        "int main() {\n"
        "    Derived dev;\n"
        "    return 0;\n"
        "}\n",
        "Derived_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_BreakInLoopRunsScopeCleanups) {
    EXPECT_TRUE(checkCodegen(
        "struct ResourceSlot { int fd; ~ResourceSlot() {} };\n"
        "int main() {\n"
        "    for (int i = 0; i < 10; i++) {\n"
        "        ResourceSlot h;\n"
        "        if (i == 5) break;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "ResourceSlot_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_ContinueInLoopRunsScopeCleanups) {
    EXPECT_TRUE(checkCodegen(
        "struct ResourceSlot { int fd; ~ResourceSlot() {} };\n"
        "int main() {\n"
        "    for (int i = 0; i < 10; i++) {\n"
        "        ResourceSlot h;\n"
        "        if (i == 3) continue;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "ResourceSlot_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_EarlyReturnRunsAllScopeCleanups) {
    EXPECT_TRUE(checkCodegen(
        "struct R1 { int a; ~R1() {} };\n"
        "struct R2 { int b; ~R2() {} };\n"
        "int foo(bool c) {\n"
        "    R1 r1;\n"
        "    if (c) {\n"
        "        R2 r2;\n"
        "        return 1;\n"
        "    }\n"
        "    return 2;\n"
        "}\n"
        "int main() { return foo(true); }\n",
        "R2_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_MovedLocalDestructorSkipped) {
    EXPECT_TRUE(checkCodegen(
        "struct Resource { int id; ~Resource() {} };\n"
        "void sink(Resource r) {}\n"
        "int main() {\n"
        "    Resource a;\n"
        "    sink(move(a));\n"
        "    return 0;\n"
        "}\n",
        "Resource_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_ConditionalMoveDynamicDropFlag) {
    EXPECT_TRUE(checkCodegen(
        "struct Resource { int id; ~Resource() {} };\n"
        "void sink(Resource r) {}\n"
        "int main(bool flag) {\n"
        "    Resource a;\n"
        "    if (flag) {\n"
        "        sink(move(a));\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "Resource_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_FixedArrayOfResourceObjectsDestruction) {
    EXPECT_TRUE(checkCodegen(
        "struct Item { int val; ~Item() {} };\n"
        "int main() {\n"
        "    Item items[4];\n"
        "    return 0;\n"
        "}\n",
        "Item_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_ZeroInitDoesNotTriggerDestructor) {
    EXPECT_TRUE(checkValid(
        "struct Data { int x; int y; };\n"
        "int main() {\n"
        "    Data d;\n"
        "    d.x = 10;\n"
        "    d.y = 20;\n"
        "    return d.x + d.y;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(MemorySafety, Destructor_ReassignmentDropsOldResource) {
    EXPECT_TRUE(checkCodegen(
        "struct Res { int handle; ~Res() {} };\n"
        "int main() {\n"
        "    Res a;\n"
        "    Res b;\n"
        "    a = move(b);\n"
        "    return 0;\n"
        "}\n",
        "reassign_drop"));
    return true;
}

// ── 29-35: Region Memory Management & Safety ───────────────────────────────

TAMARIN_TEST(MemorySafety, Region_ScopeExitDestroysBackingMemory) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    region scratch(65536) {\n"
        "        int values[10];\n"
        "        values[0] = 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "__tamarin_region_destroy"));
    return true;
}

TAMARIN_TEST(MemorySafety, Region_NestedRegionsIndependentDestruction) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    region outer(65536) {\n"
        "        region inner(32768) {\n"
        "            int nums[5];\n"
        "            nums[0] = 42;\n"
        "        }\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "__tamarin_region_destroy"));
    return true;
}

TAMARIN_TEST(MemorySafety, Region_RegionAllocObjectWithDestructor) {
    EXPECT_TRUE(checkCodegen(
        "struct Widget { int id; ~Widget() {} };\n"
        "int main() {\n"
        "    region scratch(65536) {\n"
        "        Widget w;\n"
        "        w.id = 10;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "Widget_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Region_RegionAllocArrayWithDestructors) {
    EXPECT_TRUE(checkCodegen(
        "struct Node { int k; ~Node() {} };\n"
        "int main() {\n"
        "    region scratch(65536) {\n"
        "        Node nodes[4];\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "Node_destructor"));
    return true;
}

TAMARIN_TEST(MemorySafety, Region_CannotReturnRegionPointerToOuterScope) {
    EXPECT_TRUE(checkInvalid(
        "int[] escapeRegion() {\n"
        "    region scratch {\n"
        "        int a[10];\n"
        "        return a;\n"
        "    }\n"
        "}\n",
        "cannot return reference or view to local region-backed storage"));
    return true;
}

TAMARIN_TEST(MemorySafety, Region_CannotAssignRegionPointerToGlobal) {
    EXPECT_TRUE(checkInvalid(
        "int g_arr[5];\n"
        "int main() {\n"
        "    region scratch {\n"
        "        int a[5];\n"
        "        g_arr = a;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "cannot"));
    return true;
}

TAMARIN_TEST(MemorySafety, Region_CannotPassRegionViewToEscapingParameter) {
    EXPECT_TRUE(checkInvalid(
        "int[] escapeRef(int[] view) {\n"
        "    return view;\n"
        "}\n"
        "int[] test() {\n"
        "    region scratch {\n"
        "        int local[10];\n"
        "        return escapeRef(local);\n"
        "    }\n"
        "}\n",
        "escape"));
    return true;
}

// ── 36-40: Slab Generational Allocation & Safety ───────────────────────────

TAMARIN_TEST(MemorySafety, Slab_GenerationalHandleReuseCodegen) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    Slab<int> s(16);\n"
        "    Handle<int> h = s.insert(42);\n"
        "    s.remove(h);\n"
        "    return 0;\n"
        "}\n",
        "__tamarin_slab_insert"));
    return true;
}

TAMARIN_TEST(MemorySafety, Slab_RemoveRejectionOnInvalidHandle) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    Slab<int> s(16);\n"
        "    Handle<int> h = s.insert(42);\n"
        "    bool ok = s.remove(h);\n"
        "    return ok ? 1 : 0;\n"
        "}\n",
        "__tamarin_slab_remove"));
    return true;
}

TAMARIN_TEST(MemorySafety, Slab_DoubleRemoveRejection) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    Slab<int> s(16);\n"
        "    Handle<int> h = s.insert(10);\n"
        "    bool r1 = s.remove(h);\n"
        "    bool r2 = s.remove(h);\n"
        "    return (r1 && !r2) ? 0 : 1;\n"
        "}\n",
        "__tamarin_slab_remove"));
    return true;
}

TAMARIN_TEST(MemorySafety, Slab_ClearInvokesDestructorsCodegen) {
    EXPECT_TRUE(checkCodegen(
        "struct Droppable { int id; ~Droppable() {} };\n"
        "int main() {\n"
        "    Slab<Droppable> s(8);\n"
        "    s.clear();\n"
        "    return 0;\n"
        "}\n",
        "__tamarin_slab_clear"));
    return true;
}

TAMARIN_TEST(MemorySafety, Slab_DynamicGrowthPreservesHandles) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    Slab<int> s(4);\n"
        "    for (int i = 0; i < 50; i++) {\n"
        "        s.insert(i);\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "__tamarin_slab_insert"));
    return true;
}

// ── 41-47: Affine & Move Enforcement ───────────────────────────────────────

TAMARIN_TEST(MemorySafety, Affine_ResourceCannotBeCopiedWithoutMove) {
    EXPECT_TRUE(checkInvalid(
        "struct Lock { int fd; ~Lock() {} };\n"
        "int main() {\n"
        "    Lock l1;\n"
        "    Lock l2 = l1;\n"
        "    return 0;\n"
        "}\n",
        "cannot copy resource-owning class 'Lock'; use 'move(l1)'"));
    return true;
}

TAMARIN_TEST(MemorySafety, Affine_ResourceCannotBePassedByValueWithoutMove) {
    EXPECT_TRUE(checkInvalid(
        "struct FileHandle { int fd; ~FileHandle() {} };\n"
        "void consumeFile(FileHandle f) {}\n"
        "int main() {\n"
        "    FileHandle fh;\n"
        "    consumeFile(fh);\n"
        "    return 0;\n"
        "}\n",
        "cannot pass resource-owning type 'FileHandle' by value; use 'move(fh)'"));
    return true;
}

TAMARIN_TEST(MemorySafety, Affine_ResourceStructMemberCannotBeCopied) {
    EXPECT_TRUE(checkInvalid(
        "struct Res { int h; ~Res() {} };\n"
        "struct Container { Res r; };\n"
        "int main() {\n"
        "    Container c1;\n"
        "    Container c2 = c1;\n"
        "    return 0;\n"
        "}\n",
        "cannot copy resource-owning"));
    return true;
}

TAMARIN_TEST(MemorySafety, Affine_CompoundAssignmentToMovedVariableRejected) {
    EXPECT_TRUE(checkInvalid(
        "struct Num { int val; ~Num() {} };\n"
        "void consume(Num n) {}\n"
        "int main() {\n"
        "    Num x;\n"
        "    x.val = 10;\n"
        "    consume(move(x));\n"
        "    x.val += 5;\n"
        "    return 0;\n"
        "}\n",
        "use of moved variable 'x'"));
    return true;
}

TAMARIN_TEST(MemorySafety, Affine_IncrementOfMovedVariableRejected) {
    EXPECT_TRUE(checkInvalid(
        "struct Counter { int val; ~Counter() {} };\n"
        "void consume(Counter c) {}\n"
        "int main() {\n"
        "    Counter c;\n"
        "    c.val = 0;\n"
        "    consume(move(c));\n"
        "    c.val++;\n"
        "    return 0;\n"
        "}\n",
        "use of moved variable 'c'"));
    return true;
}

TAMARIN_TEST(MemorySafety, Affine_MatchOnResourceMovesValue) {
    EXPECT_TRUE(checkCodegen(
        "enum Shape { Circle(int), Empty };\n"
        "int main() {\n"
        "    Shape s = Shape::Circle(10);\n"
        "    int res = match (s) {\n"
        "        Shape::Circle(r) => r * 2,\n"
        "        Shape::Empty => 0\n"
        "    };\n"
        "    return res;\n"
        "}\n",
        "switch i32"));
    return true;
}

TAMARIN_TEST(MemorySafety, Affine_MatchArmsMustMoveUniformly) {
    EXPECT_TRUE(checkValid(
        "enum Opt { Val(int), None };\n"
        "int main() {\n"
        "    Opt o = Opt::Val(42);\n"
        "    int v = match (o) {\n"
        "        Opt::Val(x) => x,\n"
        "        Opt::None => 0\n"
        "    };\n"
        "    return v;\n"
        "}\n"));
    return true;
}

// ── 48-50: Lifetime & Self-Referential Guarantees ──────────────────────────

TAMARIN_TEST(MemorySafety, Lifetime_ReferenceParameterCanBeReturned) {
    EXPECT_TRUE(checkValid(
        "int& choose(int& a, int& b, bool cond) {\n"
        "    if (cond) return a;\n"
        "    return b;\n"
        "}\n"
        "int main() {\n"
        "    int x = 1;\n"
        "    int y = 2;\n"
        "    choose(x, y, true) = 10;\n"
        "    return x;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(MemorySafety, Lifetime_StructContainingReferenceLifetimeBound) {
    EXPECT_TRUE(checkValid(
        "struct RefHolder {\n"
        "    int& ref;\n"
        "};\n"
        "int main() {\n"
        "    int val = 42;\n"
        "    RefHolder rh;\n"
        "    rh.ref = val;\n"
        "    return rh.ref;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(MemorySafety, Lifetime_SelfReferentialStructDetection) {
    EXPECT_TRUE(checkInvalid(
        "struct SelfRef {\n"
        "    SelfRef inner;\n"
        "};\n",
        "cannot contain itself by value"));
    return true;
}
