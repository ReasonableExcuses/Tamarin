# ==============================================================================
# test_async_coroutines.ps1 - Tamarin Async/Await, Coroutines & Zero-Alloc Fibers Gauntlet
# ==============================================================================

$ErrorActionPreference = "Continue"

$Compiler = "E:\Projects\Tamarin\build\Release\tamarin.exe"
$LibDir = "E:\Projects\Tamarin\lib"
$TempDir = Join-Path $env:TEMP "tamarin_async_coroutines"

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

        $run = Start-Process -FilePath $exeFile -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$tamFile.run.out" -RedirectStandardError "$tamFile.run.err"
        $outText = ""
        if (Test-Path "$tamFile.run.out") { $outText = (Get-Content "$tamFile.run.out" -Raw) }

        if ($run.ExitCode -eq 0 -and ($ExpectedOutput -eq "" -or $outText -match [regex]::Escape($ExpectedOutput))) {
            Write-Host " [PASS] [$Category] $Name" -ForegroundColor Green
            if ($outText.Trim()) {
                Write-Host "        Output: $($outText.Trim())" -ForegroundColor DarkGray
            }
            $global:Passed++
        } else {
            Write-Host " [FAIL] [$Category] $Name (Run failed with ExitCode=$($run.ExitCode), expected '$ExpectedOutput')" -ForegroundColor Red
            if ($outText) { Write-Host "        Output: $outText" -ForegroundColor Yellow }
            if (Test-Path "$tamFile.run.err") { Get-Content "$tamFile.run.err" | Write-Host -ForegroundColor Yellow }
        }
    }
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " TAMARIN ASYNC/AWAIT, COROUTINES & WORK-STEALING FIBERS GAUNTLET" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------------------
# Positive Tests
# ------------------------------------------------------------------------------

# P01: Basic async function returning scalar
Run-Probe -Name "P01: Basic async function returning scalar and awaited" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Async Result: 42;" `
    -Code @'
async int add(int a, int b) {
    return a + b;
}

int main() {
    Task<int> t = add(20, 22);
    int res = await t;
    print("Async Result: ");
    print(res);
    println(";");
    if (res != 42) return 1;
    return 0;
}
'@

# P02: Concurrent fan-out tasks
Run-Probe -Name "P02: Concurrent fan-out tasks awaited concurrently" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Concurrent Sum: 500;" `
    -Code @'
async int square(int x) {
    return x * x;
}

int main() {
    Task<int> t1 = square(10);
    Task<int> t2 = square(20);
    int r1 = await t1;
    int r2 = await t2;
    int sum = r1 + r2;
    print("Concurrent Sum: ");
    print(sum);
    println(";");
    if (sum != 500) return 1;
    return 0;
}
'@

# P03: Recursive work-stealing await (Fibonacci)
Run-Probe -Name "P03: Recursive async work-stealing await (Fibonacci 10 -> 55)" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Fib 10: 55;" `
    -Code @'
async int fib(int n) {
    if (n <= 1) return n;
    Task<int> a = fib(n - 1);
    Task<int> b = fib(n - 2);
    int ra = await a;
    int rb = await b;
    return ra + rb;
}

int main() {
    int ans = await fib(10);
    print("Fib 10: ");
    print(ans);
    println(";");
    if (ans != 55) return 1;
    return 0;
}
'@

# P04: Async task recycling across 1,000 tasks
Run-Probe -Name "P04: Steady-state task recycling across 1,000 tasks (ThreadTaskPool zero-heap)" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Recycled Sum: 499500;" `
    -Code @'
async int identity(int x) {
    return x;
}

int main() {
    int total = 0;
    for (int i = 0; i < 1000; i++) {
        Task<int> t = identity(i);
        total += await t;
    }
    // 0 + 1 + ... + 999 = 999 * 1000 / 2 = 499500
    print("Recycled Sum: ");
    print(total);
    println(";");
    if (total != 499500) return 1;
    return 0;
}
'@

# P05: Direct inline await inside expressions
Run-Probe -Name "P05: Direct inline await inside expressions" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Combined: 100;" `
    -Code @'
async int add(int a, int b) {
    return a + b;
}

int main() {
    int res = (await add(10, 20)) + (await add(30, 40));
    print("Combined: ");
    print(res);
    println(";");
    if (res != 100) return 1;
    return 0;
}
'@

# P06: Async task returning struct aggregate
Run-Probe -Name "P06: Async task returning struct aggregate" `
    -Category "Positive" `
    -ExpectedOutcome "RUN_SUCCESS" `
    -ExpectedOutput "Point: (15, 25);" `
    -Code @'
struct Point {
    int x;
    int y;
};

async Point makePoint(int px, int py) {
    Point p;
    p.x = px;
    p.y = py;
    return p;
}

int main() {
    Task<Point> t = makePoint(15, 25);
    Point pt = await t;
    print("Point: (");
    print(pt.x);
    print(", ");
    print(pt.y);
    println(");");
    if (pt.x != 15 || pt.y != 25) return 1;
    return 0;
}
'@

# ------------------------------------------------------------------------------
# Negative / Compile-Fail Soundness Checks
# ------------------------------------------------------------------------------

# N01: Reject await on non-task scalar type
Run-Probe -Name "N01: Reject await on non-task scalar type" `
    -Category "Soundness" `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "cannot await non-task type 'int'" `
    -Code @'
int main() {
    int x = 42;
    int y = await x;
    return y;
}
'@

# N02: Reject async function with @export
Run-Probe -Name "N02: Reject async function with @export" `
    -Category "Soundness" `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "async function 'foo' cannot have C linkage or be exported directly across C ABI boundary" `
    -Code @'
@export
async int foo() {
    return 1;
}
int main() { return 0; }
'@

# N03: Reject Task<T> parameter across @export C ABI boundary
Run-Probe -Name "N03: Reject Task<T> parameter across @export C ABI boundary" `
    -Category "Soundness" `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "task type 'Task<int>' cannot cross C ABI boundaries directly" `
    -Code @'
@export
int bar(Task<int> t) {
    return 0;
}
int main() { return 0; }
'@

# N04: Reject Task<T> inside @c_layout struct
Run-Probe -Name "N04: Reject Task<T> inside @c_layout struct" `
    -Category "Soundness" `
    -ExpectedOutcome "COMPILE_FAIL" `
    -ExpectedDiagnostic "task type 'Task<int>' cannot cross C ABI boundaries directly" `
    -Code @'
@c_layout
struct CStruct {
    int id;
    Task<int> task;
};
int main() { return 0; }
'@

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " ASYNC & COROUTINES SCORE: $global:Passed / $global:Total PASSED" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

if ($global:Passed -ne $global:Total) {
    exit 1
}
