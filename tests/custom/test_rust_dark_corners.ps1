# test_rust_dark_corners.ps1
# Advanced corner-case memory & concurrency soundness gauntlet for Tamarin.
# Probes subtle borrow-checker, lifetime, parallel-effect, and multi-dimensional bounds loopholes.

$ErrorActionPreference = "Continue"
$workDir = Join-Path $env:TEMP "tamarin_dark_corners"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

$passed = 0
$failed = 0

function Run-DarkCornerTest {
    param(
        [string]$Id,
        [string]$Category,
        [string]$Title,
        [ValidateSet("MustPass", "MustFailCompile", "MustTrapRuntime")]
        [string]$Mode,
        [string]$SourceCode
    )

    $srcPath = Join-Path $workDir "$Id.tam"
    $exePath = Join-Path $workDir "$Id.exe"
    Set-Content -Path $srcPath -Value $SourceCode -Encoding UTF8

    $compOut = tamarin build $srcPath -O3 --dynamic -o $exePath 2>&1
    $compExit = $LASTEXITCODE
    $firstErr = ($compOut | Where-Object { $_ -match "error:" } | Select-Object -First 1)

    if ($Mode -eq "MustPass") {
        if ($compExit -eq 0 -and (Test-Path $exePath)) {
            $runOut = & $exePath 2>&1
            $runExit = $LASTEXITCODE
            if ($runExit -eq 0) {
                Write-Host "[PASS] [$Category] $Title" -ForegroundColor Green
                $script:passed++
                return
            }
            Write-Host "[FAIL] [$Category] $Title -> Runtime failed (Exit=$runExit): $runOut" -ForegroundColor Red
            $script:failed++
            return
        }
        Write-Host "[FAIL] [$Category] $Title -> Positive control failed to compile: $firstErr" -ForegroundColor Red
        $script:failed++
        return
    }

    if ($Mode -eq "MustFailCompile") {
        if ($compExit -ne 0) {
            Write-Host "[PASS] [$Category] $Title" -ForegroundColor Green
            if ($firstErr) {
                Write-Host "       Diagnostic: $firstErr" -ForegroundColor DarkGray
            }
            $script:passed++
        } else {
            Write-Host "[FAIL] [$Category] $Title -> LOOPHOLE FOUND: UNSAFE CODE COMPILED!" -ForegroundColor Red
            $script:failed++
        }
        return
    }

    if ($Mode -eq "MustTrapRuntime") {
        if ($compExit -ne 0) {
            Write-Host "[PASS] [$Category] $Title (Rejected statically at compile time)" -ForegroundColor Green
            if ($firstErr) {
                Write-Host "       Diagnostic: $firstErr" -ForegroundColor DarkGray
            }
            $script:passed++
            return
        }

        $runOut = & $exePath 2>&1
        $runExit = $LASTEXITCODE
        $leaked = ($runOut -match "MEMORY_CORRUPTION_LEAK")
        if (($runExit -ne 0) -and (-not $leaked)) {
            Write-Host "[PASS] [$Category] $Title (Trapped safely at runtime, Exit=$runExit)" -ForegroundColor Green
            $script:passed++
        } else {
            Write-Host "[FAIL] [$Category] $Title -> LOOPHOLE FOUND: OOB NOT TRAPPED! ($runOut)" -ForegroundColor Red
            $script:failed++
        }
    }
}

Write-Host "`n======================================================================" -ForegroundColor Cyan
Write-Host " TAMARIN DARK-CORNERS SOUNDNESS GAUNTLET (14 SUBTLE LOOPHOLE PROBES)" -ForegroundColor Cyan
Write-Host "======================================================================`n" -ForegroundColor Cyan

# ----------------------------------------------------------------------------
# 0. POSITIVE CONTROL (Verify 2D array & sequential re-borrow syntax works)
# ----------------------------------------------------------------------------
Run-DarkCornerTest -Id "dc00_control_valid" -Category "Control" `
    -Title "Valid 2D array indexing and sequential non-overlapping pointer retargeting" `
    -Mode "MustPass" -SourceCode @'
int main() {
    int a = 10;
    int b = 20;
    int* p = &a;
    *p = 15;
    p = &b;
    *p = 25;

    int mat[4][4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            mat[r][c] = r * 10 + c;
        }
    }

    if (a == 15 && b == 25 && mat[3][3] == 33) {
        return 0;
    }
    return 1;
}
'@

# ----------------------------------------------------------------------------
# 1. MULTI-PATH & OUT-PARAMETER LIFETIME ESCAPES
# ----------------------------------------------------------------------------
Run-DarkCornerTest -Id "dc01_conditional_return_escape" -Category "Lifetime" `
    -Title "Reject stack pointer escape on secondary conditional return branch" `
    -Mode "MustFailCompile" -SourceCode @'
int* pick_ptr(int* valid_arg, int cond) {
    int local_temp = 55;
    if (cond == 1) {
        return valid_arg;
    }
    return &local_temp;
}

int main() {
    int x = 10;
    int* res = pick_ptr(&x, 0);
    print(*res);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc02_out_param_ptr_escape" -Category "Lifetime" `
    -Title "Reject storing local stack address into pointer-to-pointer out-parameter" `
    -Mode "MustFailCompile" -SourceCode @'
void write_out_ptr(int** out_slot) {
    int callee_local = 888;
    *out_slot = &callee_local;
}

int main() {
    int initial = 1;
    int* holder = &initial;
    write_out_ptr(&holder);
    print(*holder);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc03_loop_iteration_local_escape" -Category "Lifetime" `
    -Title "Reject pointer capturing loop-body variable across loop iterations" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int fallback = 0;
    int* p = &fallback;
    for (int i = 0; i < 4; i++) {
        int iter_local = i * 100;
        if (i == 2) {
            p = &iter_local;
        }
    }
    print(*p);
    return 0;
}
'@

# ----------------------------------------------------------------------------
# 2. CALL-SITE ALIASING, PROVENANCE COPYING & SELF-REFERENTIAL MOVES
# ----------------------------------------------------------------------------
Run-DarkCornerTest -Id "dc04_call_site_double_mut_alias" -Category "BorrowCheck" `
    -Title "Reject passing &x twice as two mutable pointer arguments to same call" `
    -Mode "MustFailCompile" -SourceCode @'
void clobber_both(int* a, int* b) {
    *a = 111;
    *b = 222;
}

int main() {
    int x = 10;
    clobber_both(&x, &x);
    print(x);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc05_pointer_copy_transitive_alias" -Category "BorrowCheck" `
    -Title "Reject mutating through both original pointer and copied pointer alias" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int target = 5;
    int* p1 = &target;
    int* p2 = p1;
    *p1 = 10;
    *p2 = 20;
    print(target);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc06_array_of_ptrs_scope_escape" -Category "Lifetime" `
    -Title "Reject storing inner-block pointer inside an outer array element" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int safe_val = 1;
    int* ptr_table[2];
    ptr_table[0] = &safe_val;
    ptr_table[1] = &safe_val;
    {
        int short_lived = 777;
        ptr_table[1] = &short_lived;
    }
    print(*ptr_table[1]);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc07_self_referential_struct_move" -Category "Lifetime" `
    -Title "Reject returning self-referential struct whose pointer points to old stack frame" `
    -Mode "MustFailCompile" -SourceCode @'
struct SelfNode {
    SelfNode* self_ptr;
    int value;
};

SelfNode create_self_ref() {
    SelfNode n;
    n.value = 42;
    n.self_ptr = &n;
    return n;
}

int main() {
    SelfNode moved = create_self_ref();
    print(moved.self_ptr->value);
    return 0;
}
'@

# ----------------------------------------------------------------------------
# 3. ZERO-TRIP LOOP DEFINITE INIT & PROVENANCE LAUNDERING
# ----------------------------------------------------------------------------
Run-DarkCornerTest -Id "dc08_zero_trip_loop_uninit" -Category "DefiniteInit" `
    -Title "Reject reading variable only initialized inside a loop that may run 0 times" `
    -Mode "MustFailCompile" -SourceCode @'
extern "C" {
    int rand();
}

int main() {
    int uninit_var;
    int trips = rand() & 1;
    for (int i = 0; i < trips; i++) {
        uninit_var = 99;
    }
    print(uninit_var);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc09_int_to_ptr_provenance_launder" -Category "TypeSafety" `
    -Title "Reject integer-to-pointer cast laundering a dead stack address" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int* p;
    {
        int secret = 12345;
        long raw_addr = (long)(&secret);
        p = (int*)raw_addr;
    }
    print(*p);
    return 0;
}
'@

# ----------------------------------------------------------------------------
# 4. SUBTLE PARALLEL DATA RACES (DIVISION, INDIRECT SCATTER, HELPER CALLS)
# ----------------------------------------------------------------------------
Run-DarkCornerTest -Id "dc10_parallel_division_index_race" -Category "Concurrency" `
    -Title "Reject non-injective integer division write index (arr[i / 2]) in parallel for" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int arr[32];
    for (int i = 0; i < 32; i++) {
        arr[i] = 0;
    }

    parallel for (int i = 0; i < 32; i++) {
        arr[i / 2] = i;
    }

    print(arr[0]);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc11_parallel_indirect_scatter_race" -Category "Concurrency" `
    -Title "Reject data-dependent scatter write (arr[idx[i]] = i) in parallel for" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int arr[16];
    int idx[16];
    for (int i = 0; i < 16; i++) {
        arr[i] = 0;
        idx[i] = 3;
    }

    parallel for (int i = 0; i < 16; i++) {
        arr[idx[i]] = i;
    }

    print(arr[3]);
    return 0;
}
'@

Run-DarkCornerTest -Id "dc12_parallel_helper_call_race" -Category "Concurrency" `
    -Title "Reject inter-procedural pointer mutation via helper call inside parallel for" `
    -Mode "MustFailCompile" -SourceCode @'
void mutate_target(int* dst, int v) {
    *dst = v;
}

int main() {
    int shared = 0;
    parallel for (int i = 0; i < 64; i++) {
        mutate_target(&shared, i);
    }
    print(shared);
    return 0;
}
'@

# ----------------------------------------------------------------------------
# 5. MULTI-DIMENSIONAL INNER BOUNDS CHECK (mat[1][5] on int mat[4][4])
# ----------------------------------------------------------------------------
Run-DarkCornerTest -Id "dc13_2d_inner_row_oob_trap" -Category "SpatialBounds" `
    -Title "Trap 2D inner-row out-of-bounds access (mat[1][5] in int[4][4]) under -O3" `
    -Mode "MustTrapRuntime" -SourceCode @'
extern "C" {
    int rand();
}

int main() {
    int mat[4][4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            mat[r][c] = r * 10 + c;
        }
    }

    // Flattened offset (1*4 + 5 = 9) is < 16, so only per-dimension bounds checks catch this!
    int bad_col = 5 + (rand() & 1);
    int leaked = mat[1][bad_col];

    print("MEMORY_CORRUPTION_LEAK:");
    print(leaked);
    return 0;
}
'@

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $workDir

$total = $passed + $failed
Write-Host "`n======================================================================" -ForegroundColor Cyan
Write-Host " DARK-CORNERS SOUNDNESS SCORE: $passed / $total PASSED" -ForegroundColor Yellow
Write-Host "======================================================================`n" -ForegroundColor Cyan