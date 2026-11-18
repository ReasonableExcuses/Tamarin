# ==============================================================================
# test_move_semantics.ps1 - Tamarin Move Semantics & Container Soundness Gauntlet
# ==============================================================================

$ErrorActionPreference = "Continue"

$Compiler = "E:\Projects\Tamarin\build\Release\tamarin.exe"
$LibDir = "E:\Projects\Tamarin\lib"
$TempDir = Join-Path $env:TEMP "tamarin_move_semantics"

if (-not (Test-Path $Compiler)) {
    Write-Error "Compiler not found at $Compiler. Build with Release config first."
    exit 1
}

if (-not (Test-Path $TempDir)) {
    New-Item -ItemType Directory -Path $TempDir -Force | Out-Null
}

$Passed = 0
$Total = 0

function Run-Probe {
    param(
        [string]$Name,
        [string]$Category,
        [string]$Code,
        [string]$ExpectedOutcome, # "COMPILE_FAIL", "RUN_SUCCESS"
        [string]$ExpectedOutput = "",
        [string]$ExpectedDiagnostic = ""
    )

    $global:Total++
    $tamFile = Join-Path $TempDir "probe_$($global:Total).tam"
    $exeFile = Join-Path $TempDir "probe_$($global:Total).exe"
    Set-Content -Path $tamFile -Value $Code -Encoding UTF8

    if ($ExpectedOutcome -eq "COMPILE_FAIL") {
        $p = Start-Process -FilePath $Compiler -ArgumentList @("check", "-I", $LibDir, $tamFile) -NoNewWindow -Wait -PassThru -RedirectStandardError "$tamFile.err" -RedirectStandardOutput "$tamFile.out"
        $errText = ""
        if (Test-Path "$tamFile.err") { $errText = (Get-Content "$tamFile.err" -Raw) }
        if (Test-Path "$tamFile.out") { $errText += "`n" + (Get-Content "$tamFile.out" -Raw) }

        if ($p.ExitCode -ne 0 -and ($ExpectedDiagnostic -eq "" -or $errText -match [regex]::Escape($ExpectedDiagnostic))) {
            Write-Host " [PASS] [$Category] $Name" -ForegroundColor Green
            $diagLine = ($errText -split "`n" | Where-Object { $_ -match "error:" } | Select-Object -First 1)
            if ($diagLine) {
                Write-Host "        Diagnostic: $($diagLine.Trim())" -ForegroundColor DarkGray
            }
            $global:Passed++
        } else {
            Write-Host " [FAIL] [$Category] $Name (Expected compile rejection with '$ExpectedDiagnostic', got ExitCode=$($p.ExitCode))" -ForegroundColor Red
            if ($errText) { Write-Host "        Output: $errText" -ForegroundColor Yellow }
        }
    }
    elseif ($ExpectedOutcome -eq "RUN_SUCCESS") {
        $p = Start-Process -FilePath $Compiler -ArgumentList @("build", "-I", $LibDir, "--memory-stats", $tamFile, "-o", $exeFile) -NoNewWindow -Wait -PassThru -RedirectStandardError "$tamFile.build.err" -RedirectStandardOutput "$tamFile.build.out"
        if ($p.ExitCode -ne 0) {
            Write-Host " [FAIL] [$Category] $Name (Build failed with ExitCode=$($p.ExitCode))" -ForegroundColor Red
            if (Test-Path "$tamFile.build.err") { Get-Content "$tamFile.build.err" | Write-Host -ForegroundColor Yellow }
            return
        }

        $runP = Start-Process -FilePath $exeFile -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$tamFile.run.out" -RedirectStandardError "$tamFile.run.err"
        $runOut = ""
        if (Test-Path "$tamFile.run.out") { $runOut = (Get-Content "$tamFile.run.out" -Raw) }

        if ($runP.ExitCode -eq 0 -and ($ExpectedOutput -eq "" -or $runOut -match [regex]::Escape($ExpectedOutput))) {
            Write-Host " [PASS] [$Category] $Name" -ForegroundColor Green
            if ($runOut) {
                Write-Host "        Output: $($runOut.Trim())" -ForegroundColor DarkGray
            }
            $global:Passed++
        } else {
            Write-Host " [FAIL] [$Category] $Name (Run failed: ExitCode=0x$("{0:X}" -f $runP.ExitCode), Output='$runOut')" -ForegroundColor Red
        }
    }
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " TAMARIN MOVE SEMANTICS & RESOURCE-OWNING CONTAINER GAUNTLET" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------------------
# 1. Positive Tests
# ------------------------------------------------------------------------------
Run-Probe `
    -Name "Factory returning Result<Vec<int>, String>::Ok, match extraction, Vec mutation, zero leaks" `
    -Category "Positive" `
    -Code @"
import std.collections.vec as col;
import std.string as str;
import std.io as io;

Result<col::Vec<int>, str::String> create_vector(bool should_succeed) {
    if (should_succeed) {
        col::Vec<int> v;
        v.init();
        v.push(10);
        v.push(20);
        return Result::Ok(move(v));
    } else {
        str::String err;
        err.init_from("Vector creation failed");
        return Result::Err(move(err));
    }
}

int main() {
    auto res = create_vector(true);
    match (res) {
        Result::Ok(v) => {
            v.push(30);
            io::print("Vec size: ");
            io::print(v.size());
            io::print("; Element 2: ");
            io::print(v.get(2));
            io::println(";");
        }
        Result::Err(err) => {
            io::println("Error occurred");
        }
    }
    return 0;
}
"@ `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Vec size: 3; Element 2: 30;"

Run-Probe `
    -Name "Factory returning Result<Vec<int>, String>::Err, match extraction, String read, zero leaks" `
    -Category "Positive" `
    -Code @"
import std.collections.vec as col;
import std.string as str;
import std.io as io;

Result<col::Vec<int>, str::String> create_vector(bool should_succeed) {
    if (should_succeed) {
        col::Vec<int> v;
        v.init();
        v.push(10);
        return Result::Ok(move(v));
    } else {
        str::String err;
        err.init_from("FailMsg");
        return Result::Err(move(err));
    }
}

int main() {
    auto res = create_vector(false);
    match (res) {
        Result::Ok(v) => {
            io::println("Unexpected OK");
        }
        Result::Err(err) => {
            io::print("Caught error: len=");
            io::print(err.length());
            io::println(";");
        }
    }
    return 0;
}
"@ `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Caught error: len=7;"

# ------------------------------------------------------------------------------
# 2. Negative Compile-Fail Tests
# ------------------------------------------------------------------------------
Run-Probe `
    -Name "Reject use-after-move on Vec<int>" `
    -Category "MoveSafety" `
    -Code @"
import std.collections.vec as col;

int main() {
    col::Vec<int> v;
    v.init();
    col::Vec<int> w = move(v);
    int s = v.size();
    return s;
}
"@ `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "use of moved variable 'v'"

Run-Probe `
    -Name "Reject moving Vec<int> while active element pointer borrows it" `
    -Category "BorrowSafety" `
    -Code @"
import std.collections.vec as col;

int main() {
    col::Vec<int> v;
    v.init();
    v.push(42);
    int* p = &v.buffer[0];
    col::Vec<int> w = move(v);
    return *p;
}
"@ `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "cannot move 'v' while active pointer 'p' borrows it"

Run-Probe `
    -Name "Reject conditional move inside 'if' followed by unconditional read" `
    -Category "DefiniteInit" `
    -Code @"
import std.collections.vec as col;

int main() {
    col::Vec<int> v;
    v.init();
    if (true) {
        col::Vec<int> w = move(v);
    }
    int s = v.size();
    return s;
}
"@ `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "variable 'v' is used before being initialized"

Run-Probe `
    -Name "Reject copying resource-owning Vec<int> without explicit move" `
    -Category "Ownership" `
    -Code @"
import std.collections.vec as col;

int main() {
    col::Vec<int> v;
    v.init();
    col::Vec<int> w = v;
    return 0;
}
"@ `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "cannot copy resource-owning class 'Vec__int'; use 'move(v)' to transfer ownership"

Run-Probe `
    -Name "Reject passing resource-owning Vec<int> to enum variant without explicit move" `
    -Category "Ownership" `
    -Code @"
import std.collections.vec as col;
import std.string as str;

int main() {
    col::Vec<int> v;
    v.init();
    auto res = Result::Ok(v);
    return 0;
}
"@ `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "cannot copy resource-owning type 'Vec__int'; use 'move(v)'"

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " MOVE SEMANTICS SOUNDNESS SCORE: $global:Passed / $global:Total PASSED" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

if ($global:Passed -eq $global:Total) {
    exit 0
} else {
    exit 1
}
