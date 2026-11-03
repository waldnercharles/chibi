param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm,
    [string]$Root = (Join-Path $PSScriptRoot '../../..')
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath($Root).Replace('\', '/')
$compilerPath = [IO.Path]::GetFullPath($Compiler)
$llvmPath = [IO.Path]::GetFullPath($Llvm)
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-builtin-math-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
function Invoke-MathTests([string]$Project, [string]$Level) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $compilerPath
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment['CHIBI_CORE'] = "$root/packages/core"
    foreach ($argument in @('test', '--llvm', $llvmPath, "-O$Level", '--root', $Project, $Project)) {
        $start.ArgumentList.Add($argument)
    }
    $child = $null
    try {
        $child = [Diagnostics.Process]::Start($start)
        $output = $child.StandardOutput.ReadToEndAsync()
        $errors = $child.StandardError.ReadToEndAsync()
        $child.WaitForExit()
        return [pscustomobject]@{ Code = $child.ExitCode; Output = $output.GetAwaiter().GetResult(); Errors = $errors.GetAwaiter().GetResult() }
    } finally {
        if ($null -ne $child) {
            if (-not $child.HasExited) { $child.Kill($true); $child.WaitForExit() }
            $child.Dispose()
        }
    }
}
$completed = $false
try {
    foreach ($level in 0, 2) {
        foreach ($source in 'Deterministic', 'Nested', 'Edges') {
            $program = Join-Path $work "$source-$level.exe"
            & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
                --source "$root/test/native_aot/math_builtin/$source.chibi" --entry Main --output $program `
                --llvm $llvmPath --target x86_64-pc-windows-msvc --level $level --jobs 2
            if ($LASTEXITCODE) { throw "could not compile built-in math $source at O$level" }
            & $program
            if ($LASTEXITCODE) { throw "built-in math $source failed at O$level" }
        }
        # Scope core selection to each test child; the caller's environment is unchanged.
        $samples = Invoke-MathTests "$root/test/compiler/samples/math" "$level"
        Write-Host $samples.Output
        Write-Host $samples.Errors
        if ($samples.Code) { throw "original math sample inventory failed at O$level with $($samples.Code)" }
        $failures = Invoke-MathTests "$root/test/native_aot/math_builtin/failures" "$level"
        [IO.File]::WriteAllText((Join-Path $work "failures-$level.log"), $failures.Output + $failures.Errors)
        if ($failures.Code -ne 1) { throw "logical-place failure controls returned $($failures.Code)" }
        [string[]]$actual = @($failures.Output -split '\r?\n' | Where-Object { $_ -match '^(PASS|FAIL) facts\.' })
        [Array]::Sort($actual, [StringComparer]::Ordinal)
        [string[]]$expected = Get-Content -LiteralPath "$root/test/native_aot/math_builtin/failures/expected.txt"
        $report = $failures.Output + $failures.Errors
        if (($actual -join "`n") -ne ($expected -join "`n") -or $report -notmatch '16 tests, 15 failed' -or
            $report -notmatch 'Referenced array element is unavailable' -or
            [regex]::Matches($report, 'but it failed with another failure').Count -ne 15) {
            Write-Host $report
            throw "logical-place failure categories differ at O$level"
        }
        Write-Host "PASS deterministic trigonometry and original math sample inventory O$level"
    }
    foreach ($case in @(
        @{ Source = 'RejectedView'; Message = 'record fields require a scalar, pointer, record, union or array of those types' },
        @{ Source = 'RejectedRecursive'; Message = 'record has a recursive inline or array value layout; use a pointer for recursive links' }
    )) {
        $preserved = Join-Path $work 'rejected.exe'
        [IO.File]::WriteAllText($preserved, "preserved`n")
        $diagnostic = & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$root/test/native_aot/math_builtin/$($case.Source).chibi" --entry Main --output $preserved `
            --llvm $llvmPath --target x86_64-pc-windows-msvc --level 0 --jobs 2 2>&1 | Out-String
        if ($LASTEXITCODE -ne 1 -or -not $diagnostic.Contains($case.Message) -or
            [IO.File]::ReadAllText($preserved) -cne "preserved`n") {
            Write-Host $diagnostic
            throw "math field rejection or output preservation failed for $($case.Source)"
        }
    }
    $completed = $true
} finally {
    if ($completed) { Remove-Item -LiteralPath $work -Recurse -Force }
    else { Write-Host "Built-in math evidence retained in $work" }
}
