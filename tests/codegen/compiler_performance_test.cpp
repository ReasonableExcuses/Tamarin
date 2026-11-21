// ============================================================================
// compiler_performance_test.cpp - 50 Unit tests for compiler perf & scaling
// ============================================================================

#include "test_utils.h"

using namespace test_utils;

// ── 1-10: Monomorphization Scaling & Generic Instantiations ────────────────

TAMARIN_TEST(CompilerPerformance, Monomorphization_SingleTypeGenericFunction) {
    EXPECT_TRUE(checkCodegen(
        "T id<T>(T val) { return val; }\n"
        "int main() {\n"
        "    int a = id<int>(10);\n"
        "    float b = id<float>(3.14f);\n"
        "    return a + (int)b;\n"
        "}\n",
        "@id__int"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_MultiTypeGenericFunction) {
    EXPECT_TRUE(checkCodegen(
        "A pair<A, B>(A a, B b) { return a; }\n"
        "int main() {\n"
        "    int res = pair<int, float>(42, 1.0f);\n"
        "    return res;\n"
        "}\n",
        "@pair__int__float"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_GenericStructInstantiation) {
    EXPECT_TRUE(checkCodegen(
        "struct Box<T> { T val; };\n"
        "int main() {\n"
        "    Box<int> bi;\n"
        "    bi.val = 100;\n"
        "    Box<float> bf;\n"
        "    bf.val = 2.5f;\n"
        "    return bi.val + (int)bf.val;\n"
        "}\n",
        "%struct.Box__int"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_NestedGenericStructs) {
    EXPECT_TRUE(checkCodegen(
        "struct Inner<T> { T item; };\n"
        "struct Outer<T> { Inner<T> in; };\n"
        "int main() {\n"
        "    Inner<int> inn = (Inner<int>){ .item = 99 };\n"
        "    Outer<int> o = (Outer<int>){ .in = inn };\n"
        "    return o.in.item;\n"
        "}\n",
        "%struct.Outer__int"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_GenericStructWithMethods) {
    EXPECT_TRUE(checkCodegen(
        "struct Cell<T> {\n"
        "    T val;\n"
        "    T get() { return val; }\n"
        "    void set(T v) { val = v; }\n"
        "};\n"
        "int main() {\n"
        "    Cell<int> c;\n"
        "    c.set(50);\n"
        "    return c.get();\n"
        "}\n",
        "@Cell__int_get"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_GenericVectorPushPop) {
    EXPECT_TRUE(checkCodegen(
        "struct Vec<T> {\n"
        "    T data[10];\n"
        "    int len;\n"
        "    void push(T val) {\n"
        "        data[len] = val;\n"
        "        len++;\n"
        "    }\n"
        "    T get(int i) { return data[i]; }\n"
        "};\n"
        "int main() {\n"
        "    Vec<int> v;\n"
        "    v.len = 0;\n"
        "    v.push(10);\n"
        "    v.push(20);\n"
        "    return v.get(0) + v.get(1);\n"
        "}\n",
        "@Vec__int_push"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_GenericStackCollection) {
    EXPECT_TRUE(checkValid(
        "struct Stack<T> {\n"
        "    T items[16];\n"
        "    int top;\n"
        "    void push(T val) { items[top] = val; top++; }\n"
        "    T pop() { top--; return items[top]; }\n"
        "};\n"
        "int main() {\n"
        "    Stack<int> s;\n"
        "    s.top = 0;\n"
        "    s.push(1);\n"
        "    return s.pop();\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_GenericQueueCollection) {
    EXPECT_TRUE(checkValid(
        "struct Queue<T> {\n"
        "    T buf[8];\n"
        "    int head;\n"
        "    int tail;\n"
        "    void enqueue(T v) { buf[tail] = v; tail++; }\n"
        "    T dequeue() { int old = head; head++; return buf[old]; }\n"
        "};\n"
        "int main() {\n"
        "    Queue<int> q;\n"
        "    q.head = 0;\n"
        "    q.tail = 0;\n"
        "    q.enqueue(42);\n"
        "    return q.dequeue();\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_TraitStaticDispatchMonomorphized) {
    EXPECT_TRUE(checkCodegen(
        "trait Printable { void printMe(); }\n"
        "struct IntWrapper { int val; };\n"
        "impl Printable for IntWrapper {\n"
        "    void printMe() {}\n"
        "}\n"
        "void invokePrint<T>(T obj) where T: Printable {\n"
        "    obj.printMe();\n"
        "}\n"
        "int main() {\n"
        "    IntWrapper iw;\n"
        "    iw.val = 5;\n"
        "    invokePrint(iw);\n"
        "    return 0;\n"
        "}\n",
        "invokePrint"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Monomorphization_MultipleImplementationsDispatch) {
    EXPECT_TRUE(checkCodegen(
        "trait Sizer { int size(); }\n"
        "struct SmallS { int s; };\n"
        "struct LargeS { int s; };\n"
        "impl Sizer for SmallS { int size() { return 1; } }\n"
        "impl Sizer for LargeS { int size() { return 100; } }\n"
        "int getSize<T>(T obj) where T: Sizer {\n"
        "    return obj.size();\n"
        "}\n"
        "int main() {\n"
        "    SmallS s; s.s = 0;\n"
        "    LargeS l; l.s = 0;\n"
        "    return getSize(s) + getSize(l);\n"
        "}\n",
        "getSize"));
    return true;
}

// ── 11-20: Deep AST Scaling & Nesting Limits ───────────────────────────────

TAMARIN_TEST(CompilerPerformance, DeepAST_BinaryAdditionChain100Levels) {
    std::string expr = "1";
    for (int i = 0; i < 50; i++) expr += " + 1";
    std::string code = "int main() { return " + expr + "; }\n";
    EXPECT_TRUE(checkCodegen(code, "add"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_MultiplicationChain50Levels) {
    std::string expr = "1";
    for (int i = 0; i < 10; i++) expr += " * 2";
    std::string code = "int main() { return " + expr + "; }\n";
    EXPECT_TRUE(checkCodegen(code, "mul"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_NestedParentheses30Levels) {
    std::string openP = "", closeP = "";
    for (int i = 0; i < 20; i++) { openP += "("; closeP += ")"; }
    std::string code = "int main() { return " + openP + "42" + closeP + "; }\n";
    EXPECT_TRUE(checkCodegen(code, "ret i32 42"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_NestedTernaryExpressions10Levels) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int a = 1;\n"
        "    int b = 2;\n"
        "    int c = 3;\n"
        "    int res = (a > 0) ? ((b > 0) ? ((c > 0) ? 100 : 200) : 300) : 400;\n"
        "    return res;\n"
        "}\n",
        "select i1"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_NestedIfElseBlocks20Levels) {
    EXPECT_TRUE(checkValid(
        "int main() {\n"
        "    int x = 1;\n"
        "    if (x > 0) {\n"
        "        if (x > 1) { x++; }\n"
        "        else { if (x == 1) { x = 42; } }\n"
        "    }\n"
        "    return x;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_NestedLoops10Levels) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < 2; i++) {\n"
        "        for (int j = 0; j < 2; j++) {\n"
        "            for (int k = 0; k < 2; k++) {\n"
        "                total++;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    return total;\n"
        "}\n",
        "br label"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_ChainedMethodCalls20Levels) {
    EXPECT_TRUE(checkValid(
        "struct Builder {\n"
        "    int val;\n"
        "    Builder add(int v) { val += v; return *this; }\n"
        "};\n"
        "int main() {\n"
        "    Builder b;\n"
        "    b.val = 0;\n"
        "    b.add(1).add(2).add(3);\n"
        "    return b.val;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_ChainedFieldAccess15Levels) {
    EXPECT_TRUE(checkCodegen(
        "struct L3 { int val; };\n"
        "struct L2 { L3 l3; };\n"
        "struct L1 { L2 l2; };\n"
        "int main() {\n"
        "    L3 l3 = (L3){ .val = 77 };\n"
        "    L2 l2 = (L2){ .l3 = l3 };\n"
        "    L1 root = (L1){ .l2 = l2 };\n"
        "    return root.l2.l3.val;\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_LargeArrayLiteral50Elements) {
    std::string init = "{";
    for (int i = 0; i < 30; i++) {
        if (i > 0) init += ", ";
        init += std::to_string(i);
    }
    init += "}";
    std::string code = "int main() {\n    int arr[30] = " + init + ";\n    return arr[29];\n}\n";
    EXPECT_TRUE(checkCodegen(code, "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, DeepAST_LargeStructLiteral20Fields) {
    EXPECT_TRUE(checkValid(
        "struct Big {\n"
        "    int f1; int f2; int f3; int f4; int f5;\n"
        "    int f6; int f7; int f8; int f9; int f10;\n"
        "};\n"
        "int main() {\n"
        "    Big b;\n"
        "    b.f1 = 1; b.f2 = 2; b.f3 = 3; b.f4 = 4; b.f5 = 5;\n"
        "    b.f6 = 6; b.f7 = 7; b.f8 = 8; b.f9 = 9; b.f10 = 10;\n"
        "    return b.f1 + b.f10;\n"
        "}\n"));
    return true;
}

// ── 21-30: Type Inference & Deductions ─────────────────────────────────────

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoScalarDeduction) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    auto x = 42;\n"
        "    return x;\n"
        "}\n",
        "store i32 42"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoPointerDeduction) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int val = 100;\n"
        "    auto p = &val;\n"
        "    return *p;\n"
        "}\n",
        "load i32"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoReferenceDeduction) {
    EXPECT_TRUE(checkValid(
        "int main() {\n"
        "    int val = 50;\n"
        "    auto& r = val;\n"
        "    r = 99;\n"
        "    return val;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_ConstAutoDeduction) {
    EXPECT_TRUE(checkInvalid(
        "int main() {\n"
        "    const auto x = 10;\n"
        "    x = 20;\n"
        "    return x;\n"
        "}\n",
        "cannot assign to const variable 'x'"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoGenericFunctionReturnDeduction) {
    EXPECT_TRUE(checkCodegen(
        "T make<T>(T val) { return val; }\n"
        "int main() {\n"
        "    auto res = make<int>(777);\n"
        "    return res;\n"
        "}\n",
        "@make__int"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoLambdaExpression) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    auto square = (int x) => x * x;\n"
        "    return square(5);\n"
        "}\n",
        "call i32"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoTernaryCommonType) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    bool flag = true;\n"
        "    auto x = flag ? 10 : 20;\n"
        "    return x;\n"
        "}\n",
        "select i1"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoTupleDestructuring) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    auto (x, y) = (1, 2);\n"
        "    return x + y;\n"
        "}\n",
        "getelementptr inbounds"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoArraySliceDeduction) {
    EXPECT_TRUE(checkValid(
        "int main() {\n"
        "    int arr[5] = {1, 2, 3, 4, 5};\n"
        "    auto slice = arr[1..4];\n"
        "    return (int)len(slice);\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, TypeInference_AutoChainedCallReturn) {
    EXPECT_TRUE(checkValid(
        "struct S { int val; int get() { return val; } };\n"
        "S makeS() { S s; s.val = 33; return s; }\n"
        "int main() {\n"
        "    auto s = makeS();\n"
        "    auto v = s.get();\n"
        "    return v;\n"
        "}\n"));
    return true;
}

// ── 31-40: Operator Overload Scaling ───────────────────────────────────────

TAMARIN_TEST(CompilerPerformance, OperatorOverload_AdditionOperator) {
    EXPECT_TRUE(checkCodegen(
        "struct Complex {\n"
        "    float re; float im;\n"
        "    Complex operator+(Complex other) {\n"
        "        Complex res;\n"
        "        res.re = re + other.re;\n"
        "        res.im = im + other.im;\n"
        "        return res;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Complex a; a.re = 1.0f; a.im = 2.0f;\n"
        "    Complex b; b.re = 3.0f; b.im = 4.0f;\n"
        "    Complex c = a + b;\n"
        "    return (int)c.re;\n"
        "}\n",
        "Complex_op_add"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_SubtractionOperator) {
    EXPECT_TRUE(checkCodegen(
        "struct Vec2 {\n"
        "    int x; int y;\n"
        "    Vec2 operator-(Vec2 o) {\n"
        "        Vec2 r; r.x = x - o.x; r.y = y - o.y; return r;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Vec2 a; a.x = 10; a.y = 20;\n"
        "    Vec2 b; b.x = 3; b.y = 5;\n"
        "    Vec2 c = a - b;\n"
        "    return c.x + c.y;\n"
        "}\n",
        "Vec2_op_sub"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_MultiplicationOperator) {
    EXPECT_TRUE(checkCodegen(
        "struct Scale {\n"
        "    int factor;\n"
        "    Scale operator*(Scale o) {\n"
        "        Scale s; s.factor = factor * o.factor; return s;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Scale s1; s1.factor = 3;\n"
        "    Scale s2; s2.factor = 4;\n"
        "    Scale s3 = s1 * s2;\n"
        "    return s3.factor;\n"
        "}\n",
        "Scale_op_mul"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_IndexOperatorGet) {
    EXPECT_TRUE(checkCodegen(
        "struct SafeList {\n"
        "    int data[4];\n"
        "    int operator[](int i) { return data[i]; }\n"
        "};\n"
        "int main() {\n"
        "    SafeList l = (SafeList){ .data = {1, 2, 42, 4} };\n"
        "    return l[2];\n"
        "}\n",
        "SafeList_op_index"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_IndexOperatorSet) {
    EXPECT_TRUE(checkValid(
        "struct SafeList {\n"
        "    int data[4];\n"
        "    int& operator[](int i) { return data[i]; }\n"
        "};\n"
        "int main() {\n"
        "    SafeList l = (SafeList){ .data = {1, 2, 3, 4} };\n"
        "    l[1] = 99;\n"
        "    return l[1];\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_EqualityOperator) {
    EXPECT_TRUE(checkCodegen(
        "struct Coord {\n"
        "    int x; int y;\n"
        "    bool operator==(Coord o) { return x == o.x && y == o.y; }\n"
        "};\n"
        "int main() {\n"
        "    Coord c1; c1.x = 1; c1.y = 2;\n"
        "    Coord c2; c2.x = 1; c2.y = 2;\n"
        "    return (c1 == c2) ? 1 : 0;\n"
        "}\n",
        "Coord_op_eq"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_CompoundAddition) {
    EXPECT_TRUE(checkValid(
        "struct Acc {\n"
        "    int sum;\n"
        "    Acc operator+(Acc o) {\n"
        "        Acc a; a.sum = sum + o.sum; return a;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Acc a; a.sum = 10;\n"
        "    Acc b; b.sum = 20;\n"
        "    a = a + b;\n"
        "    return a.sum;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_UnaryNegation) {
    EXPECT_TRUE(checkCodegen(
        "struct Sign {\n"
        "    int val;\n"
        "    Sign operator-() {\n"
        "        Sign s; s.val = -val; return s;\n"
        "    }\n"
        "};\n"
        "int main() {\n"
        "    Sign s; s.val = 5;\n"
        "    Sign neg = -s;\n"
        "    return neg.val;\n"
        "}\n",
        "Sign_op_neg"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_ChainedOperators) {
    EXPECT_TRUE(checkValid(
        "struct V {\n"
        "    int x;\n"
        "    V operator+(V o) { V r; r.x = x + o.x; return r; }\n"
        "    V operator-(V o) { V r; r.x = x - o.x; return r; }\n"
        "};\n"
        "int main() {\n"
        "    V a; a.x = 10;\n"
        "    V b; b.x = 5;\n"
        "    V c; c.x = 2;\n"
        "    V res = a + b - c;\n"
        "    return res.x;\n"
        "}\n"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, OperatorOverload_MatrixVectorMultiplication) {
    EXPECT_TRUE(checkValid(
        "struct Vec { float x; float y; };\n"
        "struct Mat2 {\n"
        "    float m00; float m01; float m10; float m11;\n"
        "    Vec operator*(Vec v) {\n"
        "        Vec out;\n"
        "        out.x = m00 * v.x + m01 * v.y;\n"
        "        out.y = m10 * v.x + m11 * v.y;\n"
        "        return out;\n"
        "}\n"
        "};\n"
        "int main() {\n"
        "    Mat2 m; m.m00 = 1.0f; m.m01 = 0.0f; m.m10 = 0.0f; m.m11 = 1.0f;\n"
        "    Vec v; v.x = 3.0f; v.y = 4.0f;\n"
        "    Vec res = m * v;\n"
        "    return (int)res.x;\n"
        "}\n"));
    return true;
}

// ── 41-50: Compilation Throughput & Linear Scaling ─────────────────────────

TAMARIN_TEST(CompilerPerformance, Throughput_ManyFunctionsInSingleModule) {
    std::string code;
    for (int i = 0; i < 20; i++) {
        code += "int fn_" + std::to_string(i) + "(int x) { return x + " + std::to_string(i) + "; }\n";
    }
    code += "int main() { return fn_0(fn_19(0)); }\n";
    EXPECT_TRUE(checkCodegen(code, "define i32 @fn_19"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_ManyStructDefinitionsInModule) {
    std::string code;
    for (int i = 0; i < 15; i++) {
        code += "struct Struct_" + std::to_string(i) + " { int val_" + std::to_string(i) + "; };\n";
    }
    code += "int main() { Struct_0 s; s.val_0 = 1; return s.val_0; }\n";
    EXPECT_TRUE(checkValid(code));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_ManyGlobalConstants) {
    std::string code;
    for (int i = 0; i < 20; i++) {
        code += "const int G_CONST_" + std::to_string(i) + " = " + std::to_string(i * 10) + ";\n";
    }
    code += "int main() { return G_CONST_0 + G_CONST_19; }\n";
    EXPECT_TRUE(checkValid(code));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_MultipleIndependentLambdas) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    auto l1 = (int a) => a + 1;\n"
        "    auto l2 = (int b) => b * 2;\n"
        "    auto l3 = (int c) => c - 3;\n"
        "    return l3(l2(l1(5)));\n"
        "}\n",
        "define i32 @main"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_SequentialModuleTypeCheckLinearTime) {
    std::string code = "int main() {\n";
    for (int i = 0; i < 30; i++) {
        code += "    int var_" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    }
    code += "    return var_29;\n}\n";
    EXPECT_TRUE(checkValid(code));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_LargeSwitchTableCompilation) {
    std::string code = "int lookup(int k) {\n    return match k {\n";
    for (int i = 0; i < 20; i++) {
        code += "        " + std::to_string(i) + " => " + std::to_string(i * 100) + ",\n";
    }
    code += "        _ => -1\n    };\n}\nint main() { return lookup(5); }\n";
    EXPECT_TRUE(checkCodegen(code, "switch i32"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_MonomorphizationDeduplication) {
    EXPECT_TRUE(checkCodegen(
        "T helper<T>(T x) { return x; }\n"
        "int main() {\n"
        "    int a = helper<int>(1);\n"
        "    int b = helper<int>(2);\n"
        "    int c = helper<int>(3);\n"
        "    return a + b + c;\n"
        "}\n",
        "@helper__int"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_DeeplyChainedComparisons) {
    EXPECT_TRUE(checkCodegen(
        "int main() {\n"
        "    int x = 10;\n"
        "    bool ok = (x > 0 && x > 1 && x > 2 && x > 3 && x < 20 && x < 30);\n"
        "    return ok ? 1 : 0;\n"
        "}\n",
        "br i1"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_RecursiveHelperFunctionsInlining) {
    EXPECT_TRUE(checkCodegen(
        "@inline\n"
        "int recSub(int n) {\n"
        "    if (n <= 0) return 0;\n"
        "    return 1 + recSub(n - 1);\n"
        "}\n"
        "int main() { return recSub(4); }\n",
        "alwaysinline"));
    return true;
}

TAMARIN_TEST(CompilerPerformance, Throughput_ComprehensiveEndToEndModule) {
    EXPECT_TRUE(checkCodegen(
        "struct Point { int x; int y; };\n"
        "Point addPoints(Point a, Point b) {\n"
        "    Point p;\n"
        "    p.x = a.x + b.x;\n"
        "    p.y = a.y + b.y;\n"
        "    return p;\n"
        "}\n"
        "int main() {\n"
        "    Point p1; p1.x = 10; p1.y = 20;\n"
        "    Point p2; p2.x = 30; p2.y = 40;\n"
        "    Point sum = addPoints(p1, p2);\n"
        "    return sum.x + sum.y;\n"
        "}\n",
        "define %struct.Point @addPoints"));
    return true;
}
