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
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-select-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$completed = $false
try {
    $probe = Join-Path $work 'probe.exe'
    & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
        --source "$root/test/native_aot/math_builtin/Select.chibi" --entry Main --output $probe `
        --llvm $llvmPath --target x86_64-pc-windows-msvc --level 0 --jobs 2
    if ($LASTEXITCODE) { throw 'could not compile the scalar selection probe' }
    & $probe $llvmPath $work
    if ($LASTEXITCODE) { throw 'native scalar selection failed' }
    $completed = $true
} finally {
    if ($completed) { Remove-Item -LiteralPath $work -Recurse -Force }
    else { Write-Host "Scalar selection evidence retained in $work" }
}
