param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm,
    [string]$CCompiler = "",
    [string]$FacadeArchive = "",
    [string]$Work = ""
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).Replace('\', '/')
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path.Replace('\', '/')
$Llvm = (Resolve-Path -LiteralPath $Llvm).Path.Replace('\', '/')
if (!$Work) {
    # The SDK manifest must preserve UTF-8 paths through the C consumer too.
    $Work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-native-asset-Ω-' + [Guid]::NewGuid().ToString('N'))
}
$Work = [IO.Path]::GetFullPath($Work).Replace('\', '/')
if (Test-Path -LiteralPath $Work) { throw "Scratch directory already exists: $Work" }
New-Item -ItemType Directory -Path $Work | Out-Null

function Build-Native([string]$Source, [string]$Output, [string[]]$Extra = @()) {
    & $Compiler --compiler "$root/tools/compiler" --core "$root/packages/core" `
        --source $Source --entry Main --output $Output --llvm $Llvm `
        --target x86_64-pc-windows-msvc --level 0 --jobs 2 @Extra
    if ($LASTEXITCODE) { throw "Native compilation failed: $Source ($LASTEXITCODE)" }
}

$passed = $false
try {
    if (!$CCompiler) {
        $clang = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
        if ($clang) { $CCompiler = $clang.Source }
        if (!$CCompiler) {
            $installed = Join-Path $env:ProgramFiles 'LLVM/bin/clang-cl.exe'
            if (Test-Path -LiteralPath $installed -PathType Leaf) { $CCompiler = $installed }
        }
    }
    if (!$CCompiler) { throw 'The C consumer regression requires clang-cl.exe' }
    $CCompiler = (Resolve-Path -LiteralPath $CCompiler).Path
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Visual Studio locator is unavailable' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -or !$installation) { throw 'The C consumer regression requires the MSVC/Windows SDK tools' }
    $devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
    # Capture the batch initializer's environment without printing it or changing
    # this process. Only the C compiler receives these SDK include/lib paths.
    $environment = & $env:ComSpec /d /s /c "call `"$devcmd`" -no_logo -arch=amd64 -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE) { throw 'Could not prepare the MSVC/Windows SDK environment' }

    $sdk = "$Work/SDK"
    New-Item -ItemType Directory -Path "$sdk/bin", "$sdk/libexec/chibi", "$Work/objects", "$Work/cache", "$Work/temp" | Out-Null
    $asset = "$sdk/libexec/chibi/chibi-asset-compiler.exe"
    Write-Host 'Build the native Windows asset compiler'
    Build-Native "$root/tools/compiler/asset_driver/Main.chibi" $asset
    Write-Host 'Build the authored verifier and native failure fixture'
    Build-Native "$root/test/native_aot/asset_compiler/Verify.chibi" "$Work/verify.exe"
    Build-Native "$root/test/native_aot/asset_compiler/Crash.chibi" "$Work/crash.exe"

    if ($FacadeArchive) {
        $FacadeArchive = (Resolve-Path -LiteralPath $FacadeArchive).Path.Replace('\', '/')
    } else {
        Write-Host 'Build the authored facade archive and LLVM call boundary'
        Build-Native "$root/tools/graph/asset_native/Build.chibi" "$Work/asset-native-build.exe" @('--collection', "graph=$root/tools/graph")
        $FacadeArchive = "$Work/chibi_asset_compiler_host.lib"
        & "$Work/asset-native-build.exe" $root $Compiler $Llvm x86_64-pc-windows-msvc $FacadeArchive
        if ($LASTEXITCODE) { throw "The authored facade archive failed to build ($LASTEXITCODE)" }
    }

    $consumer = "$sdk/bin/asset-consumer.exe"
    $compile = [Diagnostics.ProcessStartInfo]::new()
    $compile.FileName = $CCompiler
    $compile.UseShellExecute = $false
    $compile.WorkingDirectory = $Work
    $compile.Environment.Clear()
    foreach ($line in $environment) {
        $equals = $line.IndexOf('=')
        if ($equals -gt 0) { $compile.Environment[$line.Substring(0, $equals)] = $line.Substring($equals + 1) }
    }
    $arguments = @('/nologo', '/TC', '/std:c11', '/MT', '/W4', '/WX', '/D_CRT_SECURE_NO_WARNINGS',
        '/D_CRT_NONSTDC_NO_DEPRECATE', "/I$root/src", "/Fo$Work/objects/", "/Fe$consumer",
        "$root/test/native_aot/asset_compiler/Bridge.c", $FacadeArchive, '/link', '/MANIFEST:EMBED',
        "/MANIFESTINPUT:$root/src/host/windows/chibi.manifest")
    foreach ($argument in $arguments) { $compile.ArgumentList.Add($argument) }
    Write-Host 'Build the C consumer without compiler-host or VM objects'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $compile
    if (!$process.Start()) { throw 'Could not start the C compiler' }
    $process.WaitForExit()
    $status = $process.ExitCode
    $process.Dispose()
    if ($status) { throw "The C consumer failed to build ($status)" }

    $relocated = "$Work/relocated SDK Ω"
    Move-Item -LiteralPath $sdk -Destination $relocated
    $asset = "$relocated/libexec/chibi/chibi-asset-compiler.exe"
    $consumer = "$relocated/bin/asset-consumer.exe"
    $names = @('PATH', 'CC', 'CXX', 'CHIBI_ASSET_COMPILER', 'TEMP', 'TMP')
    $saved = @{}
    foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
    try {
        $env:PATH = ''
        $env:CC = 'Z:/no-c-compiler.exe'
        $env:CXX = 'Z:/no-cxx-compiler.exe'
        [Environment]::SetEnvironmentVariable('CHIBI_ASSET_COMPILER', $null, 'Process')
        $env:TEMP = "$Work/temp"
        $env:TMP = "$Work/temp"
        Write-Host 'Run the relocated native service and C consumer without runtime build tools'
        & "$Work/verify.exe" $asset "$root/test/native_aot/asset_compiler/catalog.json" $Work
        if ($LASTEXITCODE) { throw "The native service verifier failed ($LASTEXITCODE)" }
        # Preserve an extended-length loader spelling, which GetModuleFileNameW
        # may return even for a short path. SDK discovery must canonicalize it
        # before using parent segments, while all UTF-8 argv stay unchanged.
        $nativeConsumer = $consumer.Replace('/', '\')
        if ($nativeConsumer.StartsWith('\\')) { $nativeConsumer = '\\?\UNC\' + $nativeConsumer.Substring(2) }
        else { $nativeConsumer = '\\?\' + $nativeConsumer }
        $invoke = [Diagnostics.ProcessStartInfo]::new()
        $invoke.FileName = $nativeConsumer
        $invoke.UseShellExecute = $false
        foreach ($argument in @($asset, "$Work/simple.json", "$Work/source-error.json", "$Work/cache", "$Work/crash.exe", "$Work/simple.chibip")) {
            $invoke.ArgumentList.Add($argument)
        }
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $invoke
        $started = $false
        try {
            $started = $process.Start()
            if (!$started) { throw 'Could not start the C asset consumer' }
            $process.WaitForExit()
            if ($process.ExitCode) { throw "The C asset consumer failed ($($process.ExitCode))" }
        } finally {
            if ($started -and !$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
            $process.Dispose()
        }
        if (@(Get-ChildItem -LiteralPath "$Work/temp" -Force).Count) { throw 'The native compiler left temporary files' }
    } finally {
        foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    }
    Write-Host 'Windows native asset quota, cancellation, diagnostics and UTF-8 SDK relocation passed'
    $passed = $true
} finally {
    if ($passed) { Remove-Item -LiteralPath $Work -Recurse -Force }
    else { Write-Host "Failed native facade evidence retained at $Work" }
}
