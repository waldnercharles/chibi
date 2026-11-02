# The Windows checkout entry. The verified native compiler owns build caching.
#requires -Version 5.1
param([Parameter(ValueFromRemainingArguments)][AllowEmptyString()][AllowEmptyCollection()][string[]]$Arguments)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$previous = $PWD.Path
$status = 2

# core:sys/windows's command-line contract: quote each argument, doubling
# backslashes before a quote and before the closing quote. .NET Framework has
# no ArgumentList, so its Arguments string must preserve the same bytes.
function WindowsArgument([string]$text) {
    $quoted = [Text.StringBuilder]::new('"')
    $slashes = 0
    foreach ($character in $text.ToCharArray()) {
        if ($character -eq [char]92) { $slashes++; continue }
        $count = $slashes
        if ($character -eq [char]34) { $count = 2 * $slashes + 1 }
        $null = $quoted.Append([char]92, $count)
        $null = $quoted.Append($character)
        $slashes = 0
    }
    $null = $quoted.Append([char]92, 2 * $slashes)
    $null = $quoted.Append([char]34)
    return $quoted.ToString()
}
function RunCompiler([string]$compiler, [string[]]$arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $compiler
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    if ($start.PSObject.Properties['ArgumentList']) {
        foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    } else {
        $start.Arguments = ($arguments | ForEach-Object { WindowsArgument $_ }) -join ' '
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        while (!$process.WaitForExit(100)) { }
        return $process.ExitCode
    } finally {
        try {
            if ($started -and !$process.HasExited) {
                # Framework's Kill has no tree overload. The standard Windows
                # tool cancels this owned child tree when the pipeline stops.
                $stop = [Diagnostics.ProcessStartInfo]::new()
                $stop.FileName = Join-Path ([Environment]::GetFolderPath('Windows')) 'System32/taskkill.exe'
                $stop.Arguments = "/PID $($process.Id) /T /F"
                $stop.UseShellExecute = $false
                $stop.CreateNoWindow = $true
                try {
                    $killer = [Diagnostics.Process]::Start($stop)
                    try { $killer.WaitForExit() } finally { $killer.Dispose() }
                } finally {
                    if (!$process.HasExited) { $process.Kill() }
                    $process.WaitForExit()
                }
            }
        } finally { $process.Dispose() }
    }
}
try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or ![Environment]::Is64BitProcess -or
        $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'tools/build.ps1: the native Windows entry requires Windows x64 and PowerShell x64'
    }
    Set-Location -LiteralPath $root
    $forwarded = [Collections.Generic.List[string]]::new()
    if ($Arguments) { $forwarded.AddRange($Arguments) }
    if ($forwarded.Count -and $forwarded[0] -eq 'build') { $forwarded.RemoveAt(0) }
    $compiler = ''
    $llvm = ''
    for ($index = 0; $index -lt $forwarded.Count; $index++) {
        if ($forwarded[$index] -notin '--native-driver', '--llvm') { continue }
        $option = $forwarded[$index]
        if ($index + 1 -eq $forwarded.Count -or !$forwarded[$index + 1]) { throw "tools/build.ps1: $option requires a value" }
        $index++
        $value = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($forwarded[$index])
        $forwarded[$index] = $value
        if ($option -eq '--native-driver' -and !$compiler) { $compiler = $value }
        if ($option -eq '--llvm' -and !$llvm) { $llvm = $value }
    }
    if (!$llvm) {
        $llvm = $env:CHIBI_LLVM_BIN
        if (!$llvm) {
            $opt = Get-Command opt.exe, opt -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($opt) { $llvm = [IO.Path]::GetDirectoryName($opt.Source) }
        }
        if (!$llvm) { throw 'tools/build.ps1: pass --llvm DIR or set CHIBI_LLVM_BIN to the LLVM tool directory' }
        $llvm = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($llvm)
        $forwarded.Add('--llvm'); $forwarded.Add($llvm)
    }
    foreach ($tool in 'opt', 'llc', 'lld-link') {
        if (![IO.File]::Exists("$llvm/$tool.exe")) { throw "tools/build.ps1: native build entry needs $llvm/$tool.exe" }
    }
    if (!$compiler) {
        $compiler = "$root/.chibi/native-seed/windows-x64/chibi.exe"
        & "$root/tools/bootstrap/native-seed.ps1" -Platform windows-x64 -Output $compiler
        if ($LASTEXITCODE) { $status = $LASTEXITCODE; throw 'tools/build.ps1: cannot obtain the verified Windows native seed' }
    }
    if (![IO.File]::Exists($compiler)) { throw "tools/build.ps1: native build entry needs executable $compiler" }
    $status = RunCompiler $compiler (@('build') + $forwarded)
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
} finally {
    Set-Location -LiteralPath $previous
}
exit $status
