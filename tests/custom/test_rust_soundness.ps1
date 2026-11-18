# test_rust_soundness.ps1
# Single-file, self-contained Rust-level memory & concurrency soundness suite for Tamarin.
# Embeds 14 isolated test units (2 positive controls + 10 compile-fail + 2 runtime traps).

$ErrorActionPreference = "Continue"
$workDir = Join-Path $env:TEMP "tamarin_rust_soundness"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

$passed = 0
$failed = 0

function Run-TestCase {
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
            Write-Host "[FAIL] [$Category] $Title -> UNSAFE CODE COMPILED CLEANLY!" -ForegroundColor Red
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
            Write-Host "[FAIL] [$Category] $Title -> OUT-OF-BOUNDS ACCESS NOT TRAPPED! Output: $runOut" -ForegroundColor Red
            $script:failed++
        }
    }
}

Write-Host "`n======================================================================" -ForegroundColor Cyan
Write-Host " TAMARIN RUST-LEVEL MEMORY & CONCURRENCY SOUNDNESS GAUNTLET (14 TESTS)" -ForegroundColor Cyan
Write-Host "======================================================================`n" -ForegroundColor Cyan

# ----------------------------------------------------------------------------
# PART 1: POSITIVE CONTROLS (Verify syntax & valid Rust-style patterns work)
# ----------------------------------------------------------------------------
Run-TestCase -Id "c01_valid_scoped_borrow" -Category "Control" `
    -Title "Valid non-overlapping scoped borrows & disjoint parallel reduction" `
    -Mode "MustPass" -SourceCode @'
int main() {
    int value = 10;
    {
        int* scoped_ref = &value;
        *scoped_ref = 25;
    }
    // scoped_ref has ended; mutating value directly is now valid
    value = value + 5;

    int buf[64];
    parallel for (int i = 0; i < 64; i++) {
        buf[i] = i + 1;
    }

    int sum = 0;
    parallel (reduction(+:sum))
    for (int i = 0; i < 64; i++) {
        sum = sum + buf[i];
    }

    if (value == 30 && sum == 2080) {
        return 0;
    }
    return 1;
}
'@

Run-TestCase -Id "c02_valid_soa_safe_access" -Category "Control" `
    -Title "Valid @layout(scan) SoA field writes and bounds-checked reads" `
    -Mode "MustPass" -SourceCode @'
@layout(scan)
struct Node {
    float x;
    float y;
    int id;
};

int main() {
    Node nodes[16];
    for (int i = 0; i < 16; i++) {
        nodes[i].x = (float)i * 1.5f;
        nodes[i].y = (float)i * 2.5f;
        nodes[i].id = i;
    }
    if (nodes[15].id == 15) {
        return 0;
    }
    return 1;
}
'@

# ----------------------------------------------------------------------------
# PART 2: TEMPORAL & LIFETIME SAFETY (Must Fail Compile)
# ----------------------------------------------------------------------------
Run-TestCase -Id "s01_return_stack_ptr" -Category "Lifetime" `
    -Title "Reject returning pointer to local stack variable" `
    -Mode "MustFailCompile" -SourceCode @'
int* leak_local() {
    int secret = 42;
    return &secret;
}

int main() {
    int* p = leak_local();
    print(*p);
    return 0;
}
'@

Run-TestCase -Id "s02_struct_field_escape" -Category "Lifetime" `
    -Title "Reject returning struct containing pointer to local stack variable" `
    -Mode "MustFailCompile" -SourceCode @'
struct RefBox {
    int* ptr;
};

RefBox make_dangling_box() {
    int local_val = 77;
    RefBox box;
    box.ptr = &local_val;
    return box;
}

int main() {
    RefBox b = make_dangling_box();
    print(*b.ptr);
    return 0;
}
'@

Run-TestCase -Id "s03_inner_scope_escape" -Category "Lifetime" `
    -Title "Reject pointer outliving inner lexical block scope" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int outside = 10;
    int* p = &outside;
    {
        int inner_temp = 999;
        p = &inner_temp;
    }
    print(*p);
    return 0;
}
'@

Run-TestCase -Id "s04_branch_partial_init" -Category "DefiniteInit" `
    -Title "Reject reading variable initialized on only one branch" `
    -Mode "MustFailCompile" -SourceCode @'
extern "C" {
    int rand();
}

int main() {
    int maybe_init;
    if ((rand() & 1) == 1) {
        maybe_init = 42;
    }
    print(maybe_init);
    return 0;
}
'@

# ----------------------------------------------------------------------------
# PART 3: ALIASING XOR MUTABILITY (Must Fail Compile)
# ----------------------------------------------------------------------------
Run-TestCase -Id "s05_double_mut_alias" -Category "BorrowCheck" `
    -Title "Reject two simultaneous active mutable pointers to same variable" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int target = 10;
    int* mut_a = &target;
    int* mut_b = &target;
    *mut_a = 20;
    *mut_b = 30;
    print(target);
    return 0;
}
'@

Run-TestCase -Id "s06_borrow_invalidated_by_write" -Category "BorrowCheck" `
    -Title "Reject mutating array element while active pointer borrows it" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int arr[8];
    for (int i = 0; i < 8; i++) {
        arr[i] = i * 10;
    }

    int* elem_ref = &arr[2];
    arr[2] = 999;
    print(*elem_ref);
    return 0;
}
'@

Run-TestCase -Id "s07_soa_field_alias" -Category "BorrowCheck" `
    -Title "Reject overlapping mutable borrows on @layout(scan) SoA fields" `
    -Mode "MustFailCompile" -SourceCode @'
@layout(scan)
struct Vec2 {
    float x;
    float y;
};

int main() {
    Vec2 items[4];
    for (int i = 0; i < 4; i++) {
        items[i].x = 1.0f;
        items[i].y = 2.0f;
    }

    float* px1 = &items[0].x;
    float* px2 = &items[0].x;
    *px1 = 5.0f;
    *px2 = 9.0f;
    print(items[0].x);
    return 0;
}
'@

# ----------------------------------------------------------------------------
# PART 4: CONCURRENCY & DATA-RACE FREEDOM (Must Fail Compile)
# ----------------------------------------------------------------------------
Run-TestCase -Id "s08_parallel_outer_write" -Category "Concurrency" `
    -Title "Reject unsynchronized outer variable write inside parallel for" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int shared_counter = 0;
    parallel for (int i = 0; i < 1024; i++) {
        shared_counter = shared_counter + 1;
    }
    print(shared_counter);
    return 0;
}
'@

Run-TestCase -Id "s09_parallel_overlapping_index" -Category "Concurrency" `
    -Title "Reject non-disjoint array index writes (arr[i & 3]) in parallel for" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int arr[64];
    for (int i = 0; i < 64; i++) {
        arr[i] = 0;
    }

    parallel for (int i = 0; i < 64; i++) {
        arr[i & 3] = i;
    }

    print(arr[0]);
    return 0;
}
'@

Run-TestCase -Id "s10_parallel_loop_carried_race" -Category "Concurrency" `
    -Title "Reject loop-carried read/write race (arr[i] = arr[i - 1]) in parallel for" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int arr[64];
    for (int i = 0; i < 64; i++) {
        arr[i] = 1;
    }

    parallel for (int i = 1; i < 64; i++) {
        arr[i] = arr[i - 1] + 1;
    }

    print(arr[63]);
    return 0;
}
'@

Run-TestCase -Id "s11_parallel_ptr_capture_race" -Category "Concurrency" `
    -Title "Reject pointer-aliased outer mutation inside parallel for" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int shared = 0;
    int* alias = &shared;

    parallel for (int i = 0; i < 128; i++) {
        *alias = i;
    }

    print(shared);
    return 0;
}
'@

# ----------------------------------------------------------------------------
# PART 5: SPATIAL BOUNDS & FFI ISOLATION (Runtime Trap + Compile-Fail)
# ----------------------------------------------------------------------------
Run-TestCase -Id "s12_negative_index_underflow" -Category "SpatialBounds" `
    -Title "Trap negative index underflow (-1) under -O3" `
    -Mode "MustTrapRuntime" -SourceCode @'
extern "C" {
    int rand();
}

int main() {
    int guard = 123456;
    int buf[16];
    for (int i = 0; i < 16; i++) {
        buf[i] = i;
    }

    int neg_idx = -1 - (rand() & 1);
    int leaked = buf[neg_idx];

    print("MEMORY_CORRUPTION_LEAK:");
    print(leaked);
    print(guard);
    return 0;
}
'@

Run-TestCase -Id "s13_oob_write_trap" -Category "SpatialBounds" `
    -Title "Trap out-of-bounds array write under -O3" `
    -Mode "MustTrapRuntime" -SourceCode @'
extern "C" {
    int rand();
}

int main() {
    int buf[16];
    for (int i = 0; i < 16; i++) {
        buf[i] = i;
    }

    int bad_idx = 16 + (rand() & 1);
    buf[bad_idx] = 9999;

    print("MEMORY_CORRUPTION_LEAK\n");
    return 0;
}
'@

Run-TestCase -Id "s14_unchecked_ffi_ptr" -Category "FFISafety" `
    -Title "Reject passing safe stack pointer to unchecked extern C function" `
    -Mode "MustFailCompile" -SourceCode @'
extern "C" {
    void free(int* ptr);
}

int main() {
    int stack_arr[8];
    stack_arr[0] = 1;
    free(stack_arr);
    print(stack_arr[0]);
    return 0;
}
'@

# Cleanup temporary directory
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $workDir

$total = $passed + $failed
Write-Host "`n======================================================================" -ForegroundColor Cyan
Write-Host " FINAL RUST-LEVEL SOUNDNESS SCORE: $passed / $total PASSED" -ForegroundColor Yellow
Write-Host "======================================================================`n" -ForegroundColor Cyan