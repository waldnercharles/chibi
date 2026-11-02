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
$work = (Join-Path ([IO.Path]::GetTempPath()) ('chibi-public-watch-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
New-Item -ItemType Directory -Path $work | Out-Null
$completed = $false
try {
    $controller = "$work/controller.exe"
    & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
        --source "$root/test/native_aot/loader/PublicWatchProof.chibi" --entry Main --output $controller `
        --llvm $llvmPath --target x86_64-pc-windows-msvc --level 0 --jobs 2
    if ($LASTEXITCODE) { throw 'could not compile the public watch acceptance controller' }
    foreach ($level in 0, 2) {
        & $controller $compilerPath $root $llvmPath "$work/O$level" x86_64-pc-windows-msvc "$level"
        if ($LASTEXITCODE) { throw "public native Windows watch failed at O$level" }
    }
    $completed = $true
} finally {
    if ($completed) { Remove-Item -LiteralPath $work -Recurse -Force }
    else { Write-Host "Public Windows watch evidence retained in $work" }
}
