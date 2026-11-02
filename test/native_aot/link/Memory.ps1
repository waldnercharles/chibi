param([Parameter(Mandatory)][string]$Compiler, [Parameter(Mandatory)][string]$Llvm)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-compiler-memory-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    & $Compiler --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$root/test/native_aot/link/Memory.chibi" --entry Main --llvm $Llvm --target x86_64-pc-windows-msvc --output "$work/builder.exe" --jobs 2
    if ($LASTEXITCODE) { throw 'Could not compile the Windows memory runtime probe' }
    & "$work/builder.exe" $Llvm $work "$root/test/native_aot/link/Memory.ll"
    if ($LASTEXITCODE) { throw 'The Windows compiler memory runtime probe failed' }
} finally { Remove-Item -LiteralPath $work -Recurse -Force }
