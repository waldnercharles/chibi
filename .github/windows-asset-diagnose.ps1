param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Llvm,
    [Parameter(Mandatory = $true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$Source = (Resolve-Path -LiteralPath $Source).Path.Replace('\', '/')
$Llvm = (Resolve-Path -LiteralPath $Llvm).Path
New-Item -ItemType Directory -Path $Output | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path
# Local mini dumps are collected before process termination, even if WER
# reporting is disabled. No debugger or compiler instrumentation is installed.
# https://learn.microsoft.com/windows/win32/wer/collecting-user-mode-dumps
$key = 'HKLM:/SOFTWARE/Microsoft/Windows/Windows Error Reporting/LocalDumps/chibi.exe'
if (Test-Path $key) { throw 'An existing compiler dump policy must not be replaced' }
New-Item -Path $key -Force | Out-Null
$process = $null
$started = $false
try {
    New-ItemProperty -Path $key -Name DumpFolder -Value $Output -PropertyType ExpandString | Out-Null
    New-ItemProperty -Path $key -Name DumpType -Value 1 -PropertyType DWord | Out-Null
    New-ItemProperty -Path $key -Name DumpCount -Value 1 -PropertyType DWord | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Compiler
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $Source
    # Crash dumps must not capture the runner's artifact/OIDC credentials.
    # Only OS, temporary-directory and tool search settings reach this child.
    $start.Environment.Clear()
    foreach ($name in @('SystemRoot', 'WINDIR', 'PATH', 'PATHEXT', 'ComSpec', 'TEMP', 'TMP',
            'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'ProgramFiles', 'ProgramFiles(x86)',
            'ProgramData', 'NUMBER_OF_PROCESSORS', 'PROCESSOR_ARCHITECTURE',
            'INCLUDE', 'LIB', 'LIBPATH', 'VCToolsInstallDir', 'WindowsSdkDir',
            'UniversalCRTSdkDir', 'UCRTVersion', 'WindowsSDKVersion')) {
        $value = [Environment]::GetEnvironmentVariable($name, 'Process')
        if ($null -ne $value) { $start.Environment[$name] = $value }
    }
    foreach ($argument in @('--compiler', "$Source/tools/compiler", '--core', "$Source/packages/core",
            '--source', "$Source/tools/compiler/asset_driver/Main.chibi", '--entry', 'Main',
            '--output', "$Output/asset.exe", '--llvm', $Llvm, '--target', 'x86_64-pc-windows-msvc', '--level', '0', '--jobs', '2')) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (!$process.Start()) { throw 'Cannot start the exact retained compiler reproduction' }
    $started = $true
    Get-CimInstance Win32_OperatingSystem | Select-Object TotalVisibleMemorySize, FreePhysicalMemory, TotalVirtualMemorySize | Format-List
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    $peakPrivate = [long]0
    $peakWorking = [long]0
    $nextReport = 0
    while (!$process.WaitForExit(500)) {
        try {
            $process.Refresh()
            $private = $process.PrivateMemorySize64
            $working = $process.WorkingSet64
            $peak = $process.PeakWorkingSet64
        } catch {
            if ($process.HasExited) { break }
            throw
        }
        if ($private -gt $peakPrivate) { $peakPrivate = $private }
        if ($peak -gt $peakWorking) { $peakWorking = $peak }
        if ($elapsed.Elapsed.TotalSeconds -ge $nextReport) {
            "asset compile {0:F1}s working={1} private={2} peakWorking={3}" -f $elapsed.Elapsed.TotalSeconds, $working, $private, $peak |
                Tee-Object -FilePath "$Output/memory.log" -Append
            $nextReport += 30
        }
    }
    "asset compile exit={0} elapsed={1:F2}s peakPrivate={2} peakWorking={3}" -f $process.ExitCode, $elapsed.Elapsed.TotalSeconds, $peakPrivate, $peakWorking |
        Tee-Object -FilePath "$Output/memory.log" -Append
    Get-ChildItem -LiteralPath $Output | Select-Object Name, Length | Format-Table
    if ($process.ExitCode) { throw "The retained compiler reproduction exited $($process.ExitCode)" }
} finally {
    if ($process) {
        if ($started -and !$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
    Remove-Item -LiteralPath $key -Recurse
}
