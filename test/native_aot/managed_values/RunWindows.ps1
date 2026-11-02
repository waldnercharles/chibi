param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).Replace('\', '/')
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path.Replace('\', '/')
$Llvm = (Resolve-Path -LiteralPath $Llvm).Path.Replace('\', '/')
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-foreign-values-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    # The same cases use Win64's UCRT DLL exports instead of POSIX system:c.
    $source = [IO.File]::ReadAllText("$root/test/native_aot/managed_values/Foreign.chibi")
    $windows = $source.Replace('foreign import libc "system:c"', 'foreign import libc "system:ucrtbase"')
    if ($source -eq $windows) { throw 'The foreign-buffer fixture has no expected C library import' }
    [IO.File]::WriteAllText("$work/Foreign.chibi", $windows)
    foreach ($level in 0, 2) {
        $output = "$work/foreign-O$level.exe"
        & $Compiler --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$work/Foreign.chibi" --entry Main --output $output --llvm $Llvm `
            --target x86_64-pc-windows-msvc --level $level --jobs 2
        if ($LASTEXITCODE) { throw "Windows foreign-buffer compilation failed at O$level" }
        & $output
        if ($LASTEXITCODE) { throw "Windows foreign-buffer value semantics failed at O$level" }
        Write-Host "PASS Windows foreign-buffer value semantics O$level"
    }
    # Poisoned frees and allocation counts remain covered by the POSIX audit.
    # This runner does not claim a Windows allocator audit.
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}
