param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).Replace('\', '/')
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path.Replace('\', '/')
$Llvm = (Resolve-Path -LiteralPath $Llvm).Path.Replace('\', '/')
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-runner-ü-' + [Guid]::NewGuid().ToString('N'))
# Restore both metadata times in the stamp, not merely LastWriteTime. This
# makes content invalidation deterministic instead of hoping two writes race.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class RunnerFileTimes {
    [StructLayout(LayoutKind.Sequential)]
    public struct Basic {
        public long Creation, Access, Write, Change;
        public uint Attributes;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetFileInformationByHandleEx(SafeFileHandle handle,
        int kind, out Basic value, uint size);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetFileInformationByHandle(SafeFileHandle handle,
        int kind, ref Basic value, uint size);
    public static Basic Read(string path) {
        using (var handle = CreateFileW(path, 0x80, 7, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            Basic value;
            if (handle.IsInvalid || !GetFileInformationByHandleEx(handle, 0, out value, 40))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return value;
        }
    }
    public static void Restore(string path, Basic value) {
        using (var handle = CreateFileW(path, 0x100, 7, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
            if (handle.IsInvalid || !SetFileInformationByHandle(handle, 0, ref value, 40))
                throw new Win32Exception(Marshal.GetLastWin32Error());
        }
    }
}
'@
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $driver = "$work/driver.exe"
    & $Compiler --compiler "$root/tools/compiler" --core "$root/packages/core" `
        --source "$root/tools/toolchain/Driver.chibi" --collection "graph=$root/tools/graph" `
        --entry Main --output $driver --llvm $Llvm --target x86_64-pc-windows-msvc --jobs 2
    if ($LASTEXITCODE) { throw 'Could not compile the native Windows build entry' }
    $checkout = "$work/checkout with spaces ü"
    New-Item -ItemType Directory -Path "$checkout/tools/toolchain", "$checkout/tools/graph", "$checkout/packages" | Out-Null
    New-Item -ItemType Junction -Path "$checkout/tools/compiler" -Target "$root/tools/compiler" | Out-Null
    New-Item -ItemType Junction -Path "$checkout/packages/core" -Target "$root/packages/core" | Out-Null
    $source = "$checkout/tools/toolchain/Graph.chibi"
    $text = @'
package graph
import os "core:os"
Libraries :: () -> int {
    arguments := os.Arguments()
    for (index in 0..<length(arguments) - 1) {
        if (arguments[index] == "--native-driver" && !os.IsFile(arguments[index + 1])) { return 2 }
    }
    if (os.WriteText("native-ran", "one")) { return 1 }
    return 0
}
'@
    [IO.File]::WriteAllText($source, $text)
    $savedPath = $env:PATH
    Push-Location $checkout
    try {
        # No shell, mktemp, rm, uname, getconf, env or ln may be needed.
        $env:PATH = ''
        function Run-Graph {
            & $driver build --release --target chibi-asset-compiler --native-driver $Compiler --llvm $Llvm
            if ($LASTEXITCODE) { throw "Native Windows graph exited $LASTEXITCODE" }
        }
        Run-Graph
        if ([IO.File]::ReadAllText("$checkout/native-ran") -ne 'one') { throw 'The cold native graph did not execute' }
        $before = @(Get-ChildItem "$checkout/.chibi/build-runner" -Directory).Name
        if ($before.Count -ne 1) { throw 'Expected one published graph program' }
        Remove-Item "$checkout/native-ran"
        Run-Graph
        $warm = @(Get-ChildItem "$checkout/.chibi/build-runner" -Directory).Name
        if (($before -join ',') -ne ($warm -join ',')) { throw 'An unchanged native graph did not reuse its cache' }
        $times = [RunnerFileTimes]::Read($source)
        [IO.File]::WriteAllText($source, $text.Replace('"one"', '"two"'))
        [RunnerFileTimes]::Restore($source, $times)
        $restored = [RunnerFileTimes]::Read($source)
        if ($restored.Write -ne $times.Write -or $restored.Change -ne $times.Change) { throw 'Could not reproduce a metadata collision' }
        Run-Graph
        if ([IO.File]::ReadAllText("$checkout/native-ran") -ne 'two') { throw 'A same-stamp content change reused a stale runner' }
        $changed = @(Get-ChildItem "$checkout/.chibi/build-runner" -Directory).Name
        if ($changed.Count -ne 2) { throw 'Changed content did not publish a new graph program' }
        [IO.File]::WriteAllText($source, "not a Chibi program")
        & $driver build --release --target chibi-asset-compiler --native-driver $Compiler --llvm $Llvm
        if (!$LASTEXITCODE) { throw 'A rejected graph source succeeded' }
        $remaining = @(Get-ChildItem "$checkout/.chibi/build-runner" -Directory).Name
        if (($remaining | Where-Object { $_ -notmatch '^[0-9a-f]{64}$' }).Count) { throw 'A failed compilation left a private directory' }
        if ($remaining.Count -ne 2) { throw 'A failed compilation published a runner' }
        [IO.File]::WriteAllText($source, $text)
        Run-Graph
        if ([IO.File]::ReadAllText("$checkout/native-ran") -ne 'one') { throw 'The repaired graph did not execute' }
        Write-Host 'PASS native Windows runner cold/warm, same-stamp invalidation, rejection and cleanup without shell tools'
    } finally {
        $env:PATH = $savedPath
        Pop-Location
    }
} finally {
    # Remove junctions themselves before removing our private fixture tree.
    foreach ($junction in "$work/checkout with spaces ü/tools/compiler", "$work/checkout with spaces ü/packages/core") {
        if (Test-Path -LiteralPath $junction) { [IO.Directory]::Delete($junction) }
    }
    Remove-Item -LiteralPath $work -Recurse -Force
}
