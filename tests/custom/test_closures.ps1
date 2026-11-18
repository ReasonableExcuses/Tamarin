# ==============================================================================
# test_closures.ps1 - Tamarin Stack-Allocated Borrow-Checked Closures Gauntlet
# ==============================================================================

$ErrorActionPreference = "Continue"

$Compiler = "E:\Projects\Tamarin\build\Release\tamarin.exe"
$LibDir = "E:\Projects\Tamarin\lib"
$TempDir = Join-Path $env:TEMP "tamarin_closures"

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
        $p = Start-Process -FilePath $Compiler -ArgumentList @("build", "-I", $LibDir, $tamFile, "-o", $exeFile) -NoNewWindow -Wait -PassThru -RedirectStandardError "$tamFile.build.err" -RedirectStandardOutput "$tamFile.build.out"
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
Write-Host " TAMARIN STACK-ALLOCATED BORROW-CHECKED CLOSURES GAUNTLET" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------------------
# P01: Capturing & mutating outer stack variables with Vec<int> and Result<Vec<int>, int>
# ------------------------------------------------------------------------------
Run-Probe `
    -Name "P01: Capturing and mutating outer stack variables passed to higher-order functions on Vec<int> and Result<Vec<int>, int>" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Sum: 6; Calls: 3; Inner: 60;" `
    -Code @"
import std.collections.vec as col;
import std.io as io;

void process_vec(col::Vec<int>& v, fn(int) -> void consumer) {
    for (int i = 0; i < v.size(); i = i + 1) {
        consumer(v.get(i));
    }
}

Result<col::Vec<int>, int> build_and_transform(fn(int) -> int transform) {
    col::Vec<int> v;
    v.init();
    v.push(transform(1));
    v.push(transform(2));
    v.push(transform(3));
    return Result::Ok(move(v));
}

int main() {
    int running_sum = 0;
    int call_count = 0;

    fn(int) -> int transform = (int x) => {
        call_count = call_count + 1;
        running_sum = running_sum + x;
        return x * 10;
    };

    auto res = build_and_transform(transform);
    match (res) {
        Result::Ok(vec) => {
            int inner_total = 0;
            process_vec(vec, (int item) => {
                inner_total = inner_total + item;
            });
            io::print("Sum: ");
            io::print(running_sum);
            io::print("; Calls: ");
            io::print(call_count);
            io::print("; Inner: ");
            io::print(inner_total);
            io::println(";");
        }
        Result::Err(e) => {
            io::println("Error");
        }
    }
    return 0;
}
"@

# ------------------------------------------------------------------------------
# P02: Coercing a stateless lambda and named C-style function into fn(int) -> int
# ------------------------------------------------------------------------------
Run-Probe `
    -Name "P02: Coercing stateless lambda and named C-style function into same fn(int) -> int parameter" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "R1: 11; R2: 30; R3: 20; R4: 6;" `
    -Code @"
import std.io as io;

int add_one(int x) {
    return x + 1;
}

int double_val(int x) {
    return x * 2;
}

int apply_op(fn(int) -> int op, int val) {
    return op(val);
}

int main() {
    // 1. Pass named C-style function directly
    int r1 = apply_op(add_one, 10);

    // 2. Pass inline stateless lambda
    int r2 = apply_op((int x) => x * 3, 10);

    // 3. Assign named function to fn(int) -> int variable
    fn(int) -> int f1 = double_val;
    int r3 = apply_op(f1, 10);

    // 4. Assign stateless lambda to fn(int) -> int variable
    fn(int) -> int f2 = (int x) => x - 4;
    int r4 = apply_op(f2, 10);

    io::print("R1: ");
    io::print(r1);
    io::print("; R2: ");
    io::print(r2);
    io::print("; R3: ");
    io::print(r3);
    io::print("; R4: ");
    io::print(r4);
    io::println(";");
    return 0;
}
"@

# ------------------------------------------------------------------------------
# N01: Reject returning a closure that captures a local stack variable
# ------------------------------------------------------------------------------
Run-Probe `
    -Name "N01: Reject returning closure that captures local stack variable" `
    -Category "Soundness" `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "cannot return closure capturing local variable 'local_val'" `
    -Code @"
fn(int) -> int escape_closure() {
    int local_val = 42;
    return (int x) => x + local_val;
}

int main() {
    auto f = escape_closure();
    return 0;
}
"@

# ------------------------------------------------------------------------------
# N02: Reject mutating a local variable while an active closure borrows it
# ------------------------------------------------------------------------------
Run-Probe `
    -Name "N02: Reject mutating local variable directly while active closure mutably captures it" `
    -Category "Soundness" `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "cannot mutate 'counter' while active pointer 'inc' borrows it" `
    -Code @"
int main() {
    int counter = 0;
    fn() -> void inc = () => {
        counter = counter + 1;
    };
    counter = 10;
    inc();
    return 0;
}
"@

# ------------------------------------------------------------------------------
# N03: Reject passing a closure with mutable captures into parallel for
# ------------------------------------------------------------------------------
Run-Probe `
    -Name "N03: Reject passing closure with mutable captures into parallel for loop" `
    -Category "Soundness" `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "data-race safety violation: closure with mutable captures cannot be used inside parallel loop" `
    -Code @"
int main() {
    int counter = 0;
    fn() -> void mut_closure = () => {
        counter = counter + 1;
    };
    parallel for (int i = 0; i < 100; i = i + 1) {
        mut_closure();
    }
    return 0;
}
"@

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " CLOSURES SOUNDNESS SCORE: $global:Passed / $global:Total PASSED" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

if ($global:Passed -eq $global:Total) {
    exit 0
} else {
    exit 1
}
