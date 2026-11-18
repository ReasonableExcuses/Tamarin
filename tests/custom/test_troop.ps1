# ==============================================================================
# test_troop.ps1 - End-to-End Validation of the Troop Package Manager
# ==============================================================================

$ErrorActionPreference = "Stop"

$Troop = "E:\Projects\Tamarin\build\Release\troop.exe"
$Tamarin = "E:\Projects\Tamarin\build\Release\tamarin.exe"
$TempDir = Join-Path $env:TEMP "troop_custom_test"

if (-not (Test-Path $Troop)) {
    Write-Error "troop.exe not found at $Troop"
    exit 1
}

if (Test-Path $TempDir) {
    Remove-Item -Recurse -Force $TempDir
}
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

$Passed = 0
$Total = 0

function Assert-Test {
    param([string]$Name, [bool]$Condition)
    $global:Total++
    if ($Condition) {
        Write-Host " [PASS] $Name" -ForegroundColor Green
        $global:Passed++
    } else {
        Write-Host " [FAIL] $Name" -ForegroundColor Red
        exit 1
    }
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " TROOP PACKAGE MANAGER END-TO-END VALIDATION SUITE" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

Set-Location $TempDir

# 1. Version output check
$verTroop = (& $Troop --version)
Assert-Test "Troop CLI --version reports 0.15.0" ($verTroop -match "troop 0.15.0")

$verTamTroop = (& $Tamarin troop --version)
Assert-Test "Tamarin CLI 'tamarin troop --version' alias works" ($verTamTroop -match "troop 0.15.0")

# 2. Scaffolding a library
& $Troop new core_math --lib | Out-Null
Assert-Test "Scaffold core_math library" ((Test-Path "core_math/troop.toml") -and (Test-Path "core_math/src/lib.tam"))

$libCode = @"
public int multiply_and_add(int a, int b, int c) {
    return a * b + c;
}
"@
Set-Content -Path "core_math/src/lib.tam" -Value $libCode -Encoding UTF8

# 3. Scaffolding an application
& $Troop new calc_app --bin | Out-Null
Assert-Test "Scaffold calc_app binary" ((Test-Path "calc_app/troop.toml") -and (Test-Path "calc_app/src/main.tam"))

# 4. Adding path dependency
Set-Location (Join-Path $TempDir "calc_app")
& $Troop add core_math --path "../core_math" | Out-Null

$tomlContent = Get-Content "troop.toml" -Raw
Assert-Test "troop.toml updated with path dependency" ($tomlContent -match 'core_math = \{ path = "\.\./core_math" \}')

# 5. Writing application code that consumes library
$appCode = @"
import core_math;

int main() {
    int res = core_math::multiply_and_add(6, 7, 10);
    print("Troop App Output: ");
    print(res);
    println("");
    if (res != 52) return 1;
    return 0;
}
"@
Set-Content -Path "src/main.tam" -Value $appCode -Encoding UTF8

# 6. Building with troop build --release
$buildOut = (& $Troop build --release | Out-String)
Assert-Test "troop build --release succeeds" (Test-Path "bin/release/calc_app.exe")
Assert-Test "troop.lock generated" (Test-Path "troop.lock")

# 7. Running with troop run --release
$runOut = (& $Troop run --release | Out-String)
Assert-Test "troop run --release executes and produces expected output" ($runOut -match "Troop App Output: 52")

# 8. Testing with troop test
$testOut = (& $Troop test | Out-String)
Assert-Test "troop test runs package tests successfully" ($testOut -match "Test Result: PASSED")

# 9. Cleaning with troop clean
& $Troop clean | Out-Null
Assert-Test "troop clean removes bin directory" (-not (Test-Path "bin"))

Set-Location "E:\Projects\Tamarin"
Remove-Item -Recurse -Force $TempDir

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " TROOP TEST SCORE: $global:Passed / $global:Total PASSED" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan
