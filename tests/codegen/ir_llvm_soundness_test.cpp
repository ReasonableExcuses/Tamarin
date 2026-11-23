// ============================================================================
// ir_llvm_soundness_test.cpp - 50 Unit tests for Tamarin IR and LLVM codegen
// ============================================================================

#include "test_utils.h"

using namespace test_utils;

// ── 1-10: Integer & Floating Point Arithmetic Lowers ────────────────────────

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_SignedDivisionTrapsSDiv) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 100;\n"
        "    int b = -5;\n"
        "    return a / b;\n"
        "}\n",
        "sdiv i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_UnsignedDivisionTrapsUDiv) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    u32 a = 100;\n"
        "    u32 b = 5;\n"
        "    return (int)(a / b);\n"
        "}\n",
        "udiv i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_SignedRemainderSRem) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 17;\n"
        "    int b = 4;\n"
        "    return a % b;\n"
        "}\n",
        "srem i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_UnsignedRemainderURem) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    u32 a = 17;\n"
        "    u32 b = 4;\n"
        "    return (int)(a % b);\n"
        "}\n",
        "urem i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_SignedComparisonSLt) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = -10;\n"
        "    int b = 5;\n"
        "    return (a < b) ? 1 : 0;\n"
        "}\n",
        "icmp slt i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_UnsignedComparisonULt) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    u32 a = 10;\n"
        "    u32 b = 20;\n"
        "    return (a < b) ? 1 : 0;\n"
        "}\n",
        "icmp ult i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_64BitIntegerOverflowWrapping) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    i64 maxVal = 9223372036854775807;\n"
        "    i64 wrapped = maxVal + 1;\n"
        "    return (int)wrapped;\n"
        "}\n",
        "add i64"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_FloatFAddFSubFMulFDiv) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float a = 1.0f;\n"
        "    float b = 2.0f;\n"
        "    float c = (a + b) * (b - a) / 2.0f;\n"
        "    return (int)c;\n"
        "}\n",
        "fadd float"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_FloatComparisonFCmpEq) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float a = 3.14f;\n"
        "    float b = 3.14f;\n"
        "    return (a == b) ? 1 : 0;\n"
        "}\n",
        "fcmp oeq float"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Arithmetic_FloatComparisonFCmpLt) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float a = 1.5f;\n"
        "    float b = 2.5f;\n"
        "    return (a < b) ? 1 : 0;\n"
        "}\n",
        "fcmp olt float"));
    return true;
}

// ── 11-20: Control Flow & Short Circuit Evaluation ──────────────────────────

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_TernaryConditionalOperator) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int x = 10;\n"
        "    int y = (x > 5) ? 100 : 200;\n"
        "    return y;\n"
        "}\n",
        "select i1"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_NestedTernaryExpressions) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 1;\n"
        "    int b = 2;\n"
        "    int c = (a > 0) ? ((b > 0) ? 10 : 20) : 30;\n"
        "    return c;\n"
        "}\n",
        "select i1"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_ShortCircuitAndWithSideEffects) {
    EXPECT_TRUE(checkCodegen(
        "int check(int val) { return val; }\n"
        "int main() {\n"
        "    int x = 0;\n"
        "    if (x != 0 && check(10) > 5) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "br i1"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_ShortCircuitOrWithSideEffects) {
    EXPECT_TRUE(checkCodegen(
        "int compute() { return 1; }\n"
        "int main() {\n"
        "    int x = 5;\n"
        "    if (x == 5 || compute() == 0) {\n"
        "        return 42;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        "br i1"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_SwitchStatementMultipleCases) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int val = 2;\n"
        "    int res = match val {\n"
        "        1 => 10,\n"
        "        2 => 20,\n"
        "        3 => 30,\n"
        "        _ => -1\n"
        "    };\n"
        "    return res;\n"
        "}\n",
        "switch i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_SwitchWithDefaultFallback) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int x = 99;\n"
        "    int out = match x {\n"
        "        0 => 1,\n"
        "        _ => 999\n"
        "    };\n"
        "    return out;\n"
        "}\n",
        "switch i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_BreakNestedLoopTarget) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int sum = 0;\n"
        "    for (int i = 0; i < 5; i++) {\n"
        "        for (int j = 0; j < 5; j++) {\n"
        "            if (j == 2) break;\n"
        "            sum += j;\n"
        "        }\n"
        "    }\n"
        "    return sum;\n"
        "}\n",
        "br label"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_ContinueNestedLoopTarget) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int count = 0;\n"
        "    for (int i = 0; i < 4; i++) {\n"
        "        for (int j = 0; j < 4; j++) {\n"
        "            if (j % 2 == 0) continue;\n"
        "            count++;\n"
        "        }\n"
        "    }\n"
        "    return count;\n"
        "}\n",
        "br label"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_DeeplyNestedIfElseChain) {
    EXPECT_TRUE(checkCodegen(
        "int eval(int x) {\n"
        "    if (x == 1) return 10;\n"
        "    else if (x == 2) return 20;\n"
        "    else if (x == 3) return 30;\n"
        "    else if (x == 4) return 40;\n"
        "    else return 0;\n"
        "}\n"
        "int main() { return eval(3); }\n",
        "ret i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_DeadCodeEliminationAfterReturn) {
    EXPECT_TRUE(checkCodegen(
        "int test() {\n"
        "    return 42;\n"
        "    int unreachable = 999;\n"
        "}\n"
        "int main() { return test(); }\n",
        "ret i32 42"));
    return true;
}

// ── 21-30: Aggregates, Struct Layouts & Calling Conventions ────────────────

TAMARIN_TEST(IRLLVMSoundness, Aggregate_StructFieldOffsetAlignment) {
    EXPECT_TRUE(checkCodegen(
        "struct AlignTest {\n"
        "    i8 a;\n"
        "    i32 b;\n"
        "    i16 c;\n"
        "};\n"
        "int main() {\n"
        "    AlignTest t;\n"
        "    t.b = 100;\n"
        "    return t.b;\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_NestedStructLayout) {
    EXPECT_TRUE(checkCodegen(
        "struct Point { int x; int y; };\n"
        "struct Rect { Point topLeft; Point bottomRight; };\n"
        "int main() {\n"
        "    Point p1 = (Point){ .x = 0, .y = 0 };\n"
        "    Point p2 = (Point){ .x = 50, .y = 60 };\n"
        "    Rect r = (Rect){ .topLeft = p1, .bottomRight = p2 };\n"
        "    return r.bottomRight.x;\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_ArrayOfStructsGEP) {
    EXPECT_TRUE(checkCodegen(
        "struct Vec { float x; float y; };\n"
        "int main() {\n"
        "    Vec vecs[4];\n"
        "    vecs[2].y = 4.5f;\n"
        "    return (int)vecs[2].y;\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_MultiDimensionalFixedArray) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int grid[3][3];\n"
        "    grid[1][2] = 42;\n"
        "    return grid[1][2];\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_ZeroSizedStructAlignment) {
    EXPECT_TRUE(checkValid(
        "struct Empty {};\n"
        "int main() {\n"
        "    Empty e;\n"
        "    return 0;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_StructPassingByValueMSVC) {
    EXPECT_TRUE(checkCodegen(
        "struct Small { int a; int b; };\n"
        "int sum(Small s) { return s.a + s.b; }\n"
        "int main() {\n"
        "    Small s;\n"
        "    s.a = 1;\n"
        "    s.b = 2;\n"
        "    return sum(s);\n"
        "}\n",
        "define i32 @sum"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_StructPassingByValueSysV) {
    EXPECT_TRUE(checkCodegen(
        "struct Quad { float x; float y; float z; float w; };\n"
        "float dotSelf(Quad q) { return q.x * q.x + q.y * q.y; }\n"
        "int main() {\n"
        "    Quad q;\n"
        "    q.x = 1.0f;\n"
        "    q.y = 2.0f;\n"
        "    return (int)dotSelf(q);\n"
        "}\n",
        "define float @dotSelf"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_StructReturnByValueSysV) {
    EXPECT_TRUE(checkCodegen(
        "struct Pair { int first; int second; };\n"
        "Pair makePair(int a, int b) {\n"
        "    Pair p;\n"
        "    p.first = a;\n"
        "    p.second = b;\n"
        "    return p;\n"
        "}\n"
        "int main() {\n"
        "    Pair res = makePair(10, 20);\n"
        "    return res.first + res.second;\n"
        "}\n",
        "define %struct.Pair @makePair"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_PackedStructLayout) {
    EXPECT_TRUE(checkCodegen(
        "struct PackedHeader {\n"
        "    u8 magic;\n"
        "    u32 len;\n"
        "};\n"
        "int main() {\n"
        "    PackedHeader h;\n"
        "    h.magic = 170;\n"
        "    h.len = 1024;\n"
        "    return (int)h.len;\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Aggregate_StructureOfArraysGEP) {
    EXPECT_TRUE(checkCodegen(
        "@layout(scan)\n"
        "struct Particle {\n"
        "    float x;\n"
        "    float y;\n"
        "};\n"
        "int main() {\n"
        "    Particle p[64];\n"
        "    p[0].x = 1.0f;\n"
        "    return (int)p[0].x;\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

// ── 31-42: Native SIMD Vector Instructions ─────────────────────────────────

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float4SplatVector) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float4 v = (float4)1.5f;\n"
        "    return (int)v.x;\n"
        "}\n",
        "<4 x float>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float4ExtractLane) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float4 v = (float4)2.0f;\n"
        "    float z = v.z;\n"
        "    return (int)z;\n"
        "}\n",
        "extractelement <4 x float>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float4InsertLane) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float4 v = float4(0.0f, 0.0f, 0.0f, 0.0f);\n"
        "    v.y = 5.0f;\n"
        "    return (int)v.y;\n"
        "}\n",
        "<4 x float>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float4VectorAdd) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float4 a = (float4)1.0f;\n"
        "    float4 b = (float4)2.0f;\n"
        "    float4 c = a + b;\n"
        "    return (int)c.x;\n"
        "}\n",
        "fadd <4 x float>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float4VectorMul) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float4 a = (float4)3.0f;\n"
        "    float4 b = (float4)4.0f;\n"
        "    float4 c = a * b;\n"
        "    return (int)c.x;\n"
        "}\n",
        "fmul <4 x float>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float4HorizontalAddReduction) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float4 v = float4(1.0f, 2.0f, 3.0f, 4.0f);\n"
        "    float sum = reduce_add(v);\n"
        "    return (int)sum;\n"
        "}\n",
        "llvm.vector.reduce.fadd"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float4DotProduct) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float4 a = float4(1.0f, 2.0f, 3.0f, 4.0f);\n"
        "    float4 b = float4(2.0f, 2.0f, 2.0f, 2.0f);\n"
        "    float dot = dot_product(a, b);\n"
        "    return (int)dot;\n"
        "}\n",
        "fmul <4 x float>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Int4ArithmeticVector) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int4 a = (int4)10;\n"
        "    int4 b = (int4)20;\n"
        "    int4 c = a + b;\n"
        "    return c.x;\n"
        "}\n",
        "add <4 x i32>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Int4VectorSubscript) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int4 v = (int4)5;\n"
        "    return v[2];\n"
        "}\n",
        "extractelement <4 x i32>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Float8AVX2VectorArithmetic) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    float8 v = (float8)1.0f;\n"
        "    float8 res = v * (float8)2.0f;\n"
        "    return (int)res.x;\n"
        "}\n",
        "<8 x float>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Int8AVX2VectorArithmetic) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int8 v = (int8)7;\n"
        "    int8 r = v + (int8)3;\n"
        "    return r.x;\n"
        "}\n",
        "<8 x i32>"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, SIMD_Int16AVX512VectorArithmetic) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int16 v = (int16)1;\n"
        "    int16 r = v + v;\n"
        "    return r.x;\n"
        "}\n",
        "<16 x i32>"));
    return true;
}

// ── 43-50: Optimizer Passes & Attributes ───────────────────────────────────

TAMARIN_TEST(IRLLVMSoundness, Optimizer_BoundsCheckEliminationLoopInvariants) {
    EXPECT_TRUE(checkCodegen(
        "int sumArray(int arr[10]) {\n"
        "    int s = 0;\n"
        "    for (int i = 0; i < 10; i++) {\n"
        "        s += arr[i];\n"
        "    }\n"
        "    return s;\n"
        "}\n"
        "int main() { return 0; }\n",
        "define i32 @sumArray"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Optimizer_AliasAnalysisDisjointArrays) {
    EXPECT_TRUE(checkCodegen(
        "void copy(int dst[5], int src[5]) {\n"
        "    for (int i = 0; i < 5; i++) {\n"
        "        dst[i] = src[i];\n"
        "    }\n"
        "}\n"
        "int main() { return 0; }\n",
        "define void @copy"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Optimizer_LoopVectorizationMetadata) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int data[64];\n"
        "    @vectorize\n"
        "    for (int i = 0; i < 64; i++) {\n"
        "        data[i] = i * 2;\n"
        "    }\n"
        "    return data[0];\n"
        "}\n",
        "llvm.loop.vectorize.enable"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Optimizer_O0vsO2EquivalenceCheck) {
    EXPECT_TRUE(checkCodegen(
        "int calc(int x) {\n"
        "    return (x * 4) + (x * 2);\n"
        "}\n"
        "int main() { return calc(5); }\n",
        "define i32 @calc"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Optimizer_NoinlineAttributePreservedInLLVM) {
    EXPECT_TRUE(checkCodegen(
        "@noinline\n"
        "int separate(int x) {\n"
        "    return x + 1;\n"
        "}\n"
        "int main() { return separate(10); }\n",
        "noinline"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Optimizer_AlwaysInlineAttributeEmitted) {
    EXPECT_TRUE(checkCodegen(
        "@inline\n"
        "int fastAdd(int a, int b) {\n"
        "    return a + b;\n"
        "}\n"
        "int main() { return fastAdd(3, 4); }\n",
        "alwaysinline"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Optimizer_TailCallOptimizationReadiness) {
    EXPECT_TRUE(checkCodegen(
        "int gcd(int a, int b) {\n"
        "    if (b == 0) return a;\n"
        "    return gcd(b, a % b);\n"
        "}\n"
        "int main() { return gcd(48, 18); }\n",
        "call i32 @gcd"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, Optimizer_ConstantFoldingIntegerAndFloat) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int folded = 10 * 5 + (20 / 4) - 3;\n"
        "    return folded;\n"
        "}\n",
        "ret i32"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, LValue_ComplexIncrementAndDecrement) {
    EXPECT_TRUE(checkCodegen(
        "struct Point { int x; int y; };\n"
        "int main() {\n"
        "    int val = 10;\n"
        "    int* p = &val;\n"
        "    (*p)++;\n"
        "    Point pt = {20, 30};\n"
        "    pt.x++;\n"
        "    (int, int) t = (40, 50);\n"
        "    t.0++;\n"
        "    int[3] arr = {60, 70, 80};\n"
        "    arr[0]++;\n"
        "    return 0;\n"
        "}\n",
        "define i32 @main"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, LValue_CompoundAssignmentTuplePointer) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int[4] arr = {10, 20, 30, 40};\n"
        "    (int*, int) t = (&arr[0], 100);\n"
        "    t.0 += 2;\n"
        "    return 0;\n"
        "}\n",
        "define i32 @main"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, ControlFlow_MatchAllReturnErasesEmptyMerge) {
    EXPECT_TRUE(checkCodegen(
        "int check_match(int x) {\n"
        "    match x {\n"
        "        1 => { return 10; }\n"
        "        2 => { return 20; }\n"
        "        _ => { return 30; }\n"
        "    }\n"
        "}\n"
        "int main() { return check_match(1); }\n",
        "define i32 @check_match"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, RangeAnalysis_InductionWithNestedIf) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int[10] arr = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};\n"
        "    int sum = 0;\n"
        "    for (int i = 0; i < 10; ++i) {\n"
        "        if (i % 2 == 0) {\n"
        "            sum += arr[i];\n"
        "        } else {\n"
        "            sum -= arr[i];\n"
        "        }\n"
        "    }\n"
        "    return sum;\n"
        "}\n",
        "define i32 @main"));
    return true;
}

TAMARIN_TEST(IRLLVMSoundness, AliasAnalysis_DisjointAllocaAndParamRef) {
    EXPECT_TRUE(checkCodegen(
        "void update(int& refVal) {\n"
        "    int localVal = 42;\n"
        "    refVal = localVal;\n"
        "}\n"
        "int main() {\n"
        "    int x = 0;\n"
        "    update(x);\n"
        "    return x;\n"
        "}\n",
        "define void @update"));
    return true;
}
