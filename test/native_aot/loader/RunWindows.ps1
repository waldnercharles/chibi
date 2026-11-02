param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).Replace('\', '/')
$compilerPath = [IO.Path]::GetFullPath($Compiler).Replace('\', '/')
$llvmPath = [IO.Path]::GetFullPath($Llvm).Replace('\', '/')
$work = (Join-Path ([IO.Path]::GetTempPath()) ('chibi-dll-loader-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
New-Item -ItemType Directory -Path $work | Out-Null
$completed = $false
try {
    $builder = "$work/builder.exe"
    & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
        --source "$root/test/native_aot/loader/Build.chibi" --entry Main --output $builder `
        --llvm $llvmPath --target x86_64-pc-windows-msvc --level 0 --jobs 2
    if ($LASTEXITCODE) { throw 'could not compile the native DLL fixture builder' }
    $images = "$work/images"
    & $builder $llvmPath $images
    if ($LASTEXITCODE) { throw 'could not link the plain and context Windows DLL fixtures' }
    foreach ($level in 0, 2) {
        $client = "$work/client-O$level.exe"
        & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$root/test/native_aot/loader/Windows.chibi" --entry Main --output $client `
            --llvm $llvmPath --target x86_64-pc-windows-msvc --level $level --jobs 2
        if ($LASTEXITCODE) { throw "could not compile the Windows DLL client at O$level" }
        & $client $images
        if ($LASTEXITCODE) { throw "Windows DLL isolation/argv/heap/release failed at O$level" }
        "PASS native Windows DLL isolation, Unicode paths, argv and heap callbacks O$level"
    }
    $completed = $true
} finally {
    if ($completed) { Remove-Item -Recurse -Force $work }
    else { Write-Host "Windows DLL evidence retained in $work" }
}
