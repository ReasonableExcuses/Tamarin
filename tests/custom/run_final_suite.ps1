# ==============================================================================
# run_final_suite.ps1 - Tamarin End-to-End Mega Suite & ABI Interop Gauntlet
# ==============================================================================

$ErrorActionPreference = "Continue"
$passed = 0
$failed = 0

# Resolve compiler binary
$TamarinExe = (Get-Command tamarin -ErrorAction SilentlyContinue).Source
if (-not $TamarinExe) {
    $Candidate = Join-Path $PSScriptRoot "..\..\build\Release\tamarin.exe"
    if (Test-Path $Candidate) {
        $TamarinExe = (Resolve-Path $Candidate).Path
    } else {
        $TamarinExe = "tamarin"
    }
}

function Record-Result($name, $ok, $detail) {
    if ($ok) {
        Write-Host "[PASS] $name - $detail" -ForegroundColor Green
        $script:passed++
    } else {
        Write-Host "[FAIL] $name - $detail" -ForegroundColor Red
        $script:failed++
    }
}

try {
    Write-Host "`n=== 1. FULL FEATURE INTEGRATION (DYNAMIC + STATIC) ===" -ForegroundColor Cyan
    & $TamarinExe test_tamarin_mega.tam -O3 --fast-math --target-cpu native --dynamic -o mega_dyn.exe 2>$null | Out-Null
    if (Test-Path ".\mega_dyn.exe") {
        .\mega_dyn.exe | Out-Null
        Record-Result "Mega Suite (Dynamic + Fast-Math)" ($LASTEXITCODE -eq 0) "ExitCode=$LASTEXITCODE"
    } else {
        Record-Result "Mega Suite (Dynamic + Fast-Math)" $false "Compilation failed"
    }

    & $TamarinExe test_tamarin_mega.tam -O3 --target-cpu native --static -o mega_stat.exe 2>$null | Out-Null
    if (Test-Path ".\mega_stat.exe") {
        .\mega_stat.exe | Out-Null
        Record-Result "Mega Suite (Static + Strict IEEE-754)" ($LASTEXITCODE -eq 0) "ExitCode=$LASTEXITCODE"
    } else {
        Record-Result "Mega Suite (Static + Strict IEEE-754)" $false "Compilation failed"
    }

    Write-Host "`n=== 2. CROSS-COMPILER C ABI ROUND-TRIP ===" -ForegroundColor Cyan
    & $TamarinExe test_tamarin_mega.tam -O3 --emit-header --shared -o tamarin_mega.dll 2>$null | Out-Null
    if (Test-Path "test_tamarin_mega.h") {
        clang verify_mega_c_abi.c tamarin_mega.lib -O2 -o verify_mega_c_abi.exe 2>$null
        if ($LASTEXITCODE -eq 0 -and (Test-Path ".\verify_mega_c_abi.exe")) {
            .\verify_mega_c_abi.exe | Out-Null
            Record-Result "Clang C ABI Header & Struct Interop" ($LASTEXITCODE -eq 0) "ExitCode=$LASTEXITCODE"
        } else {
            Record-Result "Clang C ABI Header & Struct Interop" $false "Clang failed to compile/link generated .h"
        }
    } else {
        Record-Result "Clang C ABI Header & Struct Interop" $false "Header file test_tamarin_mega.h not emitted (check CLI flag)"
    }

    Write-Host "`n=== 3. ADVERSARIAL SOUNDNESS GAUNTLET (MUST REJECT) ===" -ForegroundColor Cyan
    foreach ($t in @("fail_1_data_race.tam", "fail_2_dangling_ref.tam", "fail_3_uninit_read.tam")) {
        & $TamarinExe $t -O3 -o bad_out.exe 2>$null | Out-Null
        Record-Result "Compile-Fail Check ($t)" ($LASTEXITCODE -ne 0) "CompilerExitCode=$LASTEXITCODE (non-zero expected)"
    }

    & $TamarinExe trap_4_runtime_oob.tam -O3 --dynamic -o trap_oob.exe 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path ".\trap_oob.exe")) {
        $oob_out = .\trap_oob.exe 2>&1
        $trapped = ($LASTEXITCODE -ne 0) -and ($oob_out -notmatch "LEAKED_OOB")
        Record-Result "Runtime Bounds Trap Under -O3 (trap_4_runtime_oob.tam)" $trapped "RuntimeExitCode=$LASTEXITCODE"
    } else {
        Record-Result "Runtime Bounds Trap Under -O3 (trap_4_runtime_oob.tam)" $true "Rejected at compile time"
    }
}
finally {
    # Clean up transient test artifacts
    Remove-Item -Force -ErrorAction SilentlyContinue mega_dyn.exe, mega_stat.exe, mega_dyn.lib, mega_stat.lib, tamarin_mega.dll, tamarin_mega.lib, tamarin_mega.exp, test_tamarin_mega.h, verify_mega_c_abi.exe, bad_out.exe, trap_oob.exe, *.ll, *.obj, *.lib, *.exp
}

Write-Host "`n==========================================" -ForegroundColor Cyan
Write-Host "FINAL VERIFICATION SCORE: $passed PASSED / $failed FAILED" -ForegroundColor Yellow
if ($failed -gt 0) { exit 1 } else { exit 0 }
