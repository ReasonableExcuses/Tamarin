import subprocess
import sys
import os

compiler = os.path.abspath("build/Release/tamarin.exe")

passed = 0
failed = 0

def test_code(name, code, expect_success=True, expected_exit=0):
    global passed, failed
    src_file = f"test_temp_{name}.tmn"
    exe_file = f"test_temp_{name}.exe"
    with open(src_file, "w") as f:
        f.write(code)
    try:
        build_res = subprocess.run([compiler, src_file, "-o", exe_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if not expect_success:
            if build_res.returncode != 0:
                print(f"[PASS] {name} (successfully rejected invalid code)")
                passed += 1
            else:
                print(f"[FAIL] {name} (expected compilation error, but succeeded)")
                failed += 1
            return

        if build_res.returncode != 0:
            print(f"[FAIL] {name} (compilation failed: {build_res.stderr})")
            failed += 1
            return

        run_res = subprocess.run([os.path.abspath(exe_file)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if run_res.returncode == expected_exit:
            print(f"[PASS] {name} (exit code: {run_res.returncode})")
            passed += 1
        else:
            print(f"[FAIL] {name} (exit code {run_res.returncode}, expected {expected_exit}: {run_res.stderr})")
            failed += 1
    finally:
        for f in [src_file, exe_file, f"test_temp_{name}.ll"]:
            if os.path.exists(f):
                try:
                    os.remove(f)
                except Exception:
                    pass

print("=== Running Tamarin Bug Fix Verification Suite ===")

# BUG-04: Compound pointer assignment (*p += 5)
test_code("bug04_compound_deref", """
int main() {
    int x = 10;
    int* p = &x;
    *p += 5;
    if (*p == 15) return 0;
    return 1;
}
""")

# BUG-05: Pointer array compound assignment (arr[0] += 1)
test_code("bug05_pointer_subscript_compound", """
int main() {
    int[3] arr = {10, 20, 30};
    int* p = &arr[0];
    p[1] += 5;
    if (p[1] == 25) return 0;
    return 1;
}
""")

# BUG-06 & BUG-07: Pointer safety rejection
test_code("bug06_reject_float_to_pointer", """
int main() {
    *void p = 3.14;
    return 0;
}
""", expect_success=False)

test_code("bug07_reject_pointer_multiplication", """
int main() {
    int x = 10;
    int* p = &x;
    auto q = p * 2;
    return 0;
}
""", expect_success=False)

# BUG-10: Ternary dereference starter token (*(&a))
test_code("bug10_ternary_deref", """
int main() {
    int a = 42;
    int b = 100;
    bool cond = true;
    int res = cond ? *(&a) : b;
    if (res == 42) return 0;
    return 1;
}
""")

# BUG-11: C-style cast with pointer starter token (*int)
test_code("bug11_c_style_pointer_cast", """
int main() {
    int x = 100;
    int res = 0;
    {
        *void vp = (*void)(&x);
    }
    {
        *int ip = (*int)(&x);
        res = *ip;
    }
    if (res == 100) return 0;
    return 1;
}
""")

# BUG-12: Comparison precedence (a < b == true)
test_code("bug12_comparison_precedence", """
int main() {
    int a = 5;
    int b = 10;
    bool res = (a < b == true);
    if (res) return 0;
    return 1;
}
""")

# BUG-13: Logical NOT in constant evaluation
test_code("bug13_const_logical_not", """
int main() {
    const bool x = !false;
    if (x) return 0;
    return 1;
}
""")

# BUG-26: Tuple index assignment (t.0 = 5, t.1 += 10)
test_code("bug26_tuple_index_assignment", """
int main() {
    auto t = (1, 2);
    t.0 = 5;
    t.1 += 10;
    if (t.0 + t.1 == 17) return 0;
    return 1;
}
""")

# BUG-27: Lambda block return type inference and sequential block eval
test_code("bug27_lambda_inference", """
int main() {
    auto f = () => {
        int x = 42;
        return x;
    };
    if (f() == 42) return 0;
    return 1;
}
""")

# BUG-03: Cyclic inheritance does not infinite loop / hang
test_code("bug03_cyclic_inheritance", """
class A : public B {};
class B : public A {};
int main() {
    return 0;
}
""", expect_success=False)

# BUG-30: Pointer compound assignment (p += 1, p -= 1)
test_code("bug30_pointer_compound_assignment", """
int main() {
    int[3] arr = {10, 20, 30};
    int* p = &arr[0];
    p += 1;
    if (*p == 20) return 0;
    return 1;
}
""")

# BUG-30: Reject struct invalid compound assignment
test_code("bug30_reject_struct_add_assign", """
struct Point { int x; int y; };
int main() {
    Point p1;
    Point p2;
    p1 += p2;
    return 0;
}
""", expect_success=False)

# BUG-30: Reject float shift compound assignment
test_code("bug30_reject_float_shift_assign", """
int main() {
    double f = 1.0;
    f <<= 1;
    return 0;
}
""", expect_success=False)

# BUG-31: Dynamic array length with <= does not eliminate bounds check (traps OOB)
test_code("bug31_dynamic_le_bounds_trap", """
int test(int[] arr) {
    int sum = 0;
    for (int i = 0; i <= len(arr); ++i) {
        sum += arr[i];
    }
    return sum;
}
int main() {
    int[5] arr = {1, 2, 3, 4, 5};
    return test(arr);
}
""", expected_exit=1)

# BUG-32: Character literal multi-byte UTF-8 codepoint decoding ('€' == 8364)
test_code("bug32_utf8_char_codepoint", """
int main() {
    int c = '€';
    if (c == 8364) return 0;
    return 1;
}
""")

# BUG-32: Reject empty character literal
test_code("bug32_reject_empty_char", """
int main() {
    int c = '';
    return 0;
}
""", expect_success=False)

# BUG-33: 64-bit binary literal with MSB set (0b1000...0000)
test_code("bug33_bin64_msb", """
int main() {
    u64 m = 0b1000000000000000000000000000000000000000000000000000000000000000;
    if (m == 0) return 1;
    return 0;
}
""")

# BUG-34: Reject void variable declaration
test_code("bug34_reject_void_var", """
int main() {
    void x;
    return 0;
}
""", expect_success=False)

# BUG-34: Reject void array declaration
test_code("bug34_reject_void_array", """
int main() {
    void[5] arr;
    return 0;
}
""", expect_success=False)

# BUG-34: Reject void function parameter
test_code("bug34_reject_void_param", """
int foo(void x) {
    return 0;
}
int main() {
    return 0;
}
""", expect_success=False)

# BUG-34: Reject void struct field
test_code("bug34_reject_void_field", """
struct Bad {
    void x;
};
int main() {
    return 0;
}
""", expect_success=False)

# BUG-18: Generic lookahead early abort on binary expression
test_code("bug18_generic_lookahead_operator", """
int main() {
    int a = 1;
    int b = 2;
    int c = 3;
    bool res = (a < b + c);
    if (res) return 0;
    return 1;
}
""")

# BUG-21: Temporary .ll files cleanup
def test_bug21_cleanup():
    global passed, failed
    src = "test_temp_bug21.tmn"
    exe = "test_temp_bug21.exe"
    ll = "test_temp_bug21.ll"
    with open(src, "w") as f:
        f.write("int main() { return 0; }\n")
    try:
        subprocess.run([compiler, src, "-o", exe], check=True)
        if not os.path.exists(ll):
            print("[PASS] bug21_cleanup_temps (temporary .ll cleaned up)")
            passed += 1
        else:
            print("[FAIL] bug21_cleanup_temps (temporary .ll was not cleaned up)")
            failed += 1
    except Exception as e:
        print(f"[FAIL] bug21_cleanup_temps ({e})")
        failed += 1
    finally:
        for f in [src, exe, ll]:
            if os.path.exists(f):
                os.remove(f)

test_bug21_cleanup()

# BUG-35: Reject binary operators on structs without overloaded operator
test_code("bug35_reject_struct_add", """
struct Point {
    int x;
    int y;
};
int main() {
    Point p1;
    Point p2;
    auto p3 = p1 + p2;
    return 0;
}
""", expect_success=False)

# BUG-35: Reject bitwise & mod operators on float/double
test_code("bug35_reject_float_bitwise", """
int main() {
    double a = 1.0;
    double b = 2.0;
    auto c = a & b;
    return 0;
}
""", expect_success=False)

test_code("bug35_reject_float_mod", """
int main() {
    double a = 1.0;
    double b = 2.0;
    auto c = a % b;
    return 0;
}
""", expect_success=False)

# BUG-36: Constant evaluation with comparisons and ternaries
test_code("bug36_const_comparison_ternary", """
int main() {
    int[(5 < 10) ? 5 : 10] arr;
    return len(arr) == 5 ? 0 : 1;
}
""")

test_code("bug36_const_logical_short_circuit", """
int main() {
    int[(true || false) ? 8 : 4] arr;
    return len(arr) == 8 ? 0 : 1;
}
""")

# BUG-37: Monomorphizer indirect call preserves calleeExpr
test_code("bug37_monomorphize_indirect_call", """
T apply<T>(fn(T) -> T f, T x) {
    return f(x);
}
int double_it(int x) { return x * 2; }
int main() {
    int res = apply<int>(double_it, 21);
    if (res == 42) return 0;
    return 1;
}
""")

# BUG-38: Float literals with suffixes attached to integer digits and hex range
test_code("bug38_int_float_suffix", """
int main() {
    float a = 10f32;
    double b = 10f64;
    double c = 10d;
    if (a == 10.0 && b == 10.0 && c == 10.0) return 0;
    return 1;
}
""")

test_code("bug38_hex_int_method_range", """
int main() {
    int x = 0x10;
    int count = 0;
    for (int i = 0x1; i < 0x5; ++i) {
        count += 1;
    }
    int[10] arr;
    auto s = arr[0x1..0x4];
    if (x == 16 && count == 4 && len(s) == 3) return 0;
    return 1;
}
""")

# BUG-39: Reject slice ArrayView (T[]) crossing C ABI
test_code("bug39_reject_slice_c_abi", """
@export
int process_items(int[] items) {
    return len(items);
}
int main() { return 0; }
""", expect_success=False)

test_bug21_cleanup()

print(f"=== Verification Summary: {passed} Passed, {failed} Failed ===")
if failed > 0:
    sys.exit(1)
