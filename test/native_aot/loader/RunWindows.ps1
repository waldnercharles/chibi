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
    $generationBuilder = "$work/generation-builder.exe"
    & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
        --package . --source "$root/test/native_aot/loader/GenerationBuild.chibi" --entry Main --output $generationBuilder `
        --llvm $llvmPath --target x86_64-pc-windows-msvc --level 2 --jobs 2
    if ($LASTEXITCODE) { throw 'could not compile the source-generation DLL builder' }
    $watchProof = "$work/watch-proof.exe"
    & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
        --source "$root/test/native_aot/loader/WatchProof.chibi" --entry Main --output $watchProof `
        --llvm $llvmPath --target x86_64-pc-windows-msvc --level 0 --jobs 2
    if ($LASTEXITCODE) { throw 'could not compile the generation watch acceptance controller' }
    foreach ($level in 0, 2) {
        $client = "$work/client-O$level.exe"
        & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$root/test/native_aot/loader/Windows.chibi" --entry Main --output $client `
            --llvm $llvmPath --target x86_64-pc-windows-msvc --level $level --jobs 2
        if ($LASTEXITCODE) { throw "could not compile the Windows DLL client at O$level" }
        & $client $images
        if ($LASTEXITCODE) { throw "Windows DLL isolation/argv/heap/release failed at O$level" }
        "PASS native Windows DLL isolation, Unicode paths, argv and heap callbacks O$level"
        $hostProgram = "$work/host-O$level.exe"
        & $compilerPath --compiler "$root/apps/host" --core "$root/packages/core" `
            --source "$root/apps/host/Host.chibi" --entry Main --output $hostProgram `
            --llvm $llvmPath --target x86_64-pc-windows-msvc --level $level --jobs 2
        if ($LASTEXITCODE) { throw "could not compile the Windows generation host at O$level" }
        $running = "$work/shared-O$level"
        & $watchProof $generationBuilder $root $llvmPath $hostProgram $running x86_64-pc-windows-msvc "$level"
        if ($LASTEXITCODE) { throw "Windows generation watch acceptance failed at O$level" }
        "PASS native Windows rejected source/entry/schema, accepted swaps, retained state, original task generation, cancellation, restart and DLL cleanup O$level"
        $formatRun = "$work/format-O$level"
        New-Item -ItemType Directory -Path $formatRun | Out-Null
        & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$root/test/native_aot/text_format/Family.chibi" --shared --output "$formatRun/live-0.so" `
            --llvm $llvmPath --target x86_64-pc-windows-msvc --level $level --jobs 2
        if ($LASTEXITCODE) { throw "could not compile Windows family formatting at O$level" }
        $formatProcess = $null
        try {
            $formatProcess = Start-Process -FilePath $hostProgram -WorkingDirectory $formatRun `
                -RedirectStandardOutput "$formatRun/stdout.txt" -RedirectStandardError "$formatRun/stderr.txt" -PassThru
            if (-not $formatProcess.WaitForExit(30000)) { throw "Windows family formatting exceeded its deadline at O$level" }
            if ($formatProcess.ExitCode -ne 42) { throw "Windows family formatting failed at O$level with exit $($formatProcess.ExitCode)" }
        } finally {
            if ($null -ne $formatProcess) {
                if (-not $formatProcess.HasExited) { $formatProcess.Kill($true); [void]$formatProcess.WaitForExit(5000) }
                $formatProcess.Dispose()
            }
        }
        "PASS native Windows family formatting and owned summaries O$level"
    }
    $completed = $true
} finally {
    if ($completed) { Remove-Item -Recurse -Force $work }
    else { Write-Host "Windows DLL evidence retained in $work" }
}
