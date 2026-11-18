# test_boss_corners.ps1
# Final 6 "boss-level" probes: pointer-cast type punning, inter-procedural reborrows,
# partial struct/array initialization, and signed overflow spatial bounds safety.

$ErrorActionPreference = "Continue"
$workDir = Join-Path $env:TEMP "tamarin_boss_corners"
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

$passed = 0
$failed = 0

function Run-BossTest {
    param(
        [string]$Id,
        [string]$Category,
        [string]$Title,
        [ValidateSet("MustFailCompile", "MustTrapRuntime")]
        [string]$Mode,
        [string]$SourceCode
    )

    $srcPath = Join-Path $workDir "$Id.tam"
    $exePath = Join-Path $workDir "$Id.exe"
    Set-Content -Path $srcPath -Value $SourceCode -Encoding UTF8

    $compOut = tamarin build $srcPath -O3 --dynamic -o $exePath 2>&1
    $compExit = $LASTEXITCODE
    $firstErr = ($compOut | Where-Object { $_ -match "error:" } | Select-Object -First 1)

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
            Write-Host "[FAIL] [$Category] $Title -> LOOPHOLE FOUND: NOT TRAPPED! ($runOut)" -ForegroundColor Red
            $script:failed++
        }
    }
}

Write-Host "`n======================================================================" -ForegroundColor Cyan
Write-Host " TAMARIN BOSS-LEVEL MEMORY SAFETY PROBES (6 FINAL EDGE CASES)" -ForegroundColor Cyan
Write-Host "======================================================================`n" -ForegroundColor Cyan

# 1. Pointer-to-pointer cast type punning (forging a pointer from a long)
Run-BossTest -Id "b01_ptr_cast_type_punning" -Category "TypeSafety" `
    -Title "Reject forging arbitrary pointer via (int**)(&long_val) cast" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    long fake_addr = 3735928559;
    int** forged_slot = (int**)(&fake_addr);
    int* evil = *forged_slot;
    print(*evil);
    return 0;
}
'@

# 2. Inter-procedural reborrow tracking across function return
Run-BossTest -Id "b02_interproc_reborrow_alias" -Category "BorrowCheck" `
    -Title "Reject aliasing &x while returned reborrow from helper function is live" `
    -Mode "MustFailCompile" -SourceCode @'
int* identity_ptr(int* p) {
    return p;
}

int main() {
    int x = 10;
    int* q = identity_ptr(&x);
    int* r = &x;
    *q = 20;
    *r = 30;
    print(x);
    return 0;
}
'@

# 3. Overwriting whole struct while one of its fields is borrowed
Run-BossTest -Id "b03_struct_overwrite_while_field_borrowed" -Category "BorrowCheck" `
    -Title "Reject overwriting whole struct while active pointer borrows a field" `
    -Mode "MustFailCompile" -SourceCode @'
struct Pair {
    int a;
    int b;
};

int main() {
    Pair s1;
    s1.a = 10;
    s1.b = 20;

    Pair s2;
    s2.a = 99;
    s2.b = 88;

    int* field_ref = &s1.a;
    s1 = s2;
    print(*field_ref);
    return 0;
}
'@

# 4. Partial array initialization (reading uninitialized element)
Run-BossTest -Id "b04_partial_array_uninit_read" -Category "DefiniteInit" `
    -Title "Reject reading uninitialized element from partially initialized array" `
    -Mode "MustFailCompile" -SourceCode @'
int main() {
    int arr[4];
    arr[0] = 100;
    print(arr[2]);
    return 0;
}
'@

# 5. Cyclic/mutual pointer references between stack structs
Run-BossTest -Id "b05_cyclic_stack_struct_refs" -Category "Lifetime" `
    -Title "Reject mutually referencing stack structs with asymmetric drop order" `
    -Mode "MustFailCompile" -SourceCode @'
struct Link {
    Link* peer;
    int val;
};

int main() {
    Link first;
    first.val = 1;
    first.peer = &first;
    {
        Link second;
        second.val = 2;
        second.peer = &first;
        first.peer = &second;
    }
    print(first.peer->val);
    return 0;
}
'@

# 6. Signed integer overflow wrapping into an out-of-bounds array index under -O3
Run-BossTest -Id "b06_signed_overflow_oob_trap" -Category "SpatialBounds" `
    -Title "Trap signed integer overflow (INT_MAX + 1) used as array index under -O3" `
    -Mode "MustTrapRuntime" -SourceCode @'
extern "C" {
    int rand();
}

int main() {
    int buf[16];
    for (int i = 0; i < 16; i++) {
        buf[i] = i + 10;
    }

    int base = 2147483647 - (rand() & 1);
    int wrapped_idx = base + 2;
    int leaked = buf[wrapped_idx];

    print("MEMORY_CORRUPTION_LEAK:");
    print(leaked);
    return 0;
}
'@

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $workDir

$total = $passed + $failed
Write-Host "`n======================================================================" -ForegroundColor Cyan
Write-Host " BOSS-LEVEL SOUNDNESS SCORE: $passed / $total PASSED" -ForegroundColor Yellow
Write-Host "======================================================================`n" -ForegroundColor Cyan