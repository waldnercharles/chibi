param([Parameter(Mandatory)][string]$Compiler, [Parameter(Mandatory)][string]$Llvm)
$ErrorActionPreference = 'Stop'
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$Llvm = (Resolve-Path -LiteralPath $Llvm).Path
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).Replace('\', '/')
$work = (Join-Path ([IO.Path]::GetTempPath()) ('chibi-directory-runner-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
New-Item -ItemType Directory -Path $work | Out-Null
try {
    & $Compiler --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$root/test/native_aot/directories/Run.chibi" --entry Run --collection "graph=$root/tools/graph" --output "$work/runner.exe" --llvm $Llvm --target x86_64-pc-windows-msvc --jobs 2
    if ($LASTEXITCODE) { throw 'Could not compile the directory enumeration runner' }
    Push-Location -LiteralPath $root
    try {
        & "$work/runner.exe" $Compiler $Llvm
        if ($LASTEXITCODE) { throw 'Directory enumeration regression failed' }
    } finally { Pop-Location }
} finally { Remove-Item -LiteralPath $work -Recurse -Force }
