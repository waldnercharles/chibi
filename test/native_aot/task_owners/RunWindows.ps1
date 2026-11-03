param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm,
    [string]$Root = (Join-Path $PSScriptRoot '../../..')
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath($Root).Replace('\', '/')
$compilerPath = [IO.Path]::GetFullPath($Compiler).Replace('\', '/')
$llvmPath = [IO.Path]::GetFullPath($Llvm).Replace('\', '/')
$work = (Join-Path ([IO.Path]::GetTempPath()) ('chibi-typed-generation-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
New-Item -ItemType Directory -Path $work | Out-Null
$completed = $false

# No image entry is added. This fixture-only host checks the existing poison
# allocator immediately before the real FreeLibrary, then requires its success.
$originalClose = 'CloseGeneration :: (handle: rawptr) -> bool { return windows.CloseLibrary(handle); }'
$auditedClose = @'
CloseGeneration :: (handle: rawptr) -> bool {
    address := windows.LibrarySymbol(handle, "chibi_test_owner_close");
    assert(address != 0, "every fixture image exposes its close audit");
    audit: fn() -> i32 = address;
    audited := audit();
    assert(audited == 0);
    closed := windows.CloseLibrary(handle);
    assert(closed, "the real FreeLibrary succeeds after the audit");
    print_string("PASS native owned image close");
    return closed;
}
'@

function Invoke-TypedGeneration {
    param([string]$Mode, [int]$Level, [bool]$Swap = $false, [bool]$Bad = $false)
    $label = $Mode
    if ($Bad) { $label = 'bad-audit' }
    $run = "$work/$label-O$Level"
    New-Item -ItemType Directory -Path $run | Out-Null
    [IO.File]::WriteAllText("$run/$Mode-case", '')
    $image = "$work/typed-O$Level.so"
    if ($Bad) { $image = "$work/bad-O$Level.so" }
    [IO.File]::Copy($image, "$run/live-0.so")
    $process = $null
    $started = $false
    $stdout = $null
    $stderr = $null
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    try {
        $process = [Diagnostics.Process]::new()
        $process.StartInfo.FileName = $hostProgram
        $process.StartInfo.WorkingDirectory = $run
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $started = $process.Start()
        if (-not $started) { throw "could not start $label O$Level" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if ($Swap) {
            while (-not (Test-Path -LiteralPath "$run/owner-ready")) {
                if ($process.HasExited) { throw "$label O$Level exited before its old task was ready" }
                if ($deadline.ElapsedMilliseconds -ge 30000) { throw "$label O$Level did not publish readiness" }
                Start-Sleep -Milliseconds 10
            }
            [IO.File]::Copy("$work/edited-O$Level.so", "$run/pending.so")
            [IO.File]::Move("$run/pending.so", "$run/next.so")
        }
        $remaining = [Math]::Max(1, 30000 - $deadline.ElapsedMilliseconds)
        if (-not $process.WaitForExit([int]$remaining)) { throw "$label O$Level exceeded its deadline" }
        $status = $process.ExitCode
        $remaining = [Math]::Max(1, 30000 - $deadline.ElapsedMilliseconds)
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdout, $stderr), [int]$remaining)) {
            throw "$label O$Level did not close its output streams"
        }
    } finally {
        if ($null -ne $process) {
            try {
                if ($started -and -not $process.HasExited) { $process.Kill($true); [void]$process.WaitForExit(5000) }
                if ($null -ne $stdout -and $stdout.IsCompletedSuccessfully) { [IO.File]::WriteAllText("$run/stdout.txt", $stdout.GetAwaiter().GetResult()) }
                if ($null -ne $stderr -and $stderr.IsCompletedSuccessfully) { [IO.File]::WriteAllText("$run/stderr.txt", $stderr.GetAwaiter().GetResult()) }
            } finally { $process.Dispose() }
        }
    }
    $reported = @([IO.File]::ReadAllLines("$run/stdout.txt")) + @([IO.File]::ReadAllLines("$run/stderr.txt"))
    $expected = 134
    if ($Mode -eq 'positive') { $expected = 42 }
    # LLVM's Win64 trap is an illegal-instruction exception, distinct from the
    # typed failure's controlled return134. A generic assertion cannot pass it.
    if ($Bad) { $expected = -1073741795 }
    if ($status -ne $expected) {
        $reported | Write-Host
        throw "$label O$Level status$status differs from $expected"
    }
    $closed = @($reported | Where-Object { $_ -eq 'PASS native owned image close' }).Count
    if ($Bad) {
        if ($closed -ne 0 -or -not (Test-Path -LiteralPath "$run/after-unwrap") -or `
            -not (Test-Path -LiteralPath "$run/live-0.so")) { throw 'the phantom owner did not fail at the image-close boundary' }
        if ($reported -contains 'native failure: UnwrappedNil') { throw 'a typed failure hid the phantom owner control' }
        "PASS Windows image-close audit rejects a phantom allocation O$Level"
        return
    }
    $expectedClosed = 1
    if ($Swap) { $expectedClosed = 2 }
    if ($closed -ne $expectedClosed) { throw "$label O$Level did not audit and close every mapped image" }
    if (@(Get-ChildItem -LiteralPath $run -Filter '*.so').Count -ne 0) { throw "$label O$Level left a mapped generation file" }
    if ($Mode -eq 'positive') {
        if (-not (Test-Path -LiteralPath "$run/after-unwrap")) { throw 'the successful unwrap did not continue' }
    } else {
        if ($reported -notcontains 'native failure: UnwrappedNil') { $reported | Write-Host; throw "$label O$Level lost its typed failure category" }
        foreach ($marker in 'after-unwrap', 'after-await', 'after-spawn') {
            if (Test-Path -LiteralPath "$run/$marker") { throw "$label O$Level continued after its typed failure" }
        }
    }
    "PASS Windows shared typed failure $label O$Level, poisoned frees and actual image close"
}

try {
    $builder = "$work/builder.exe"
    & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" --package . `
        --source "$root/test/native_aot/task_owners/BuildWindows.chibi" --entry Main --output $builder `
        --llvm $llvmPath --target x86_64-pc-windows-msvc --level 2 --jobs 2
    if ($LASTEXITCODE) { throw 'could not compile the audited Windows generation builder' }
    $hostSource = [IO.File]::ReadAllText("$root/apps/host/Host.chibi")
    if (($hostSource.Split(@($originalClose), [StringSplitOptions]::None).Count - 1) -ne 1) { throw 'the production Windows close boundary changed' }
    New-Item -ItemType Directory -Path "$work/host-source" | Out-Null
    [IO.File]::WriteAllText("$work/host-source/Host.chibi", $hostSource.Replace($originalClose, $auditedClose), [Text.UTF8Encoding]::new($false))
    foreach ($level in 0, 2) {
        $hostProgram = "$work/host-O$level.exe"
        & $compilerPath --compiler "$work/host-source" --core "$root/packages/core" `
            --source "$work/host-source/Host.chibi" --entry Main --output $hostProgram `
            --llvm $llvmPath --target x86_64-pc-windows-msvc --level $level --jobs 2
        if ($LASTEXITCODE) { throw "could not compile the audited Windows host at O$level" }
        foreach ($name in 'typed', 'edited', 'bad') {
            $source = $name
            $audit = 'good'
            if ($name -eq 'bad') { $source = 'typed'; $audit = 'bad' }
            & $builder $root $llvmPath $source "$work/$name-O$level.so" "$level" $audit
            if ($LASTEXITCODE) { throw "could not build the $name Windows generation at O$level" }
        }
        foreach ($mode in 'positive', 'sync', 'frame', 'spawn-immediate') { Invoke-TypedGeneration $mode $level }
        foreach ($mode in 'unwrap', 'unwrap-await', 'unwrap-immediate') { Invoke-TypedGeneration $mode $level -Swap $true }
        Invoke-TypedGeneration 'positive' $level -Bad $true
    }
    $completed = $true
} finally {
    if ($completed) { Remove-Item -Recurse -Force $work }
    else { Write-Host "Windows typed failure evidence retained in $work" }
}
