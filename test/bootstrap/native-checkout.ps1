param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).Replace('\', '/')
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path.Replace('\', '/')
$Llvm = (Resolve-Path -LiteralPath $Llvm).Path.Replace('\', '/')
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-checkout-Ω-' + [Guid]::NewGuid().ToString('N'))
$previous = $PWD.Path
$names = @('PATH', 'CC', 'CXX', 'CHIBI_CORE', 'CHIBI_BUILD_HOST', 'CHIBI_COMPILER_HOST')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
New-Item -ItemType Directory -Path "$work/current directory Ω" | Out-Null
try {
    $env:PATH = ''
    $env:CC = 'Z:/no-c-compiler.exe'
    $env:CXX = 'Z:/no-cxx-compiler.exe'
    $env:CHIBI_BUILD_HOST = 'Z:/no-build-host.exe'
    $env:CHIBI_COMPILER_HOST = 'Z:/no-compiler-host.exe'
    [Environment]::SetEnvironmentVariable('CHIBI_CORE', [NullString]::Value, 'Process')
    $fixtures = @(
        @{ name = 'Directories'; arguments = @("$work/current directory Ω") },
        @{ name = 'Checkout'; arguments = @($Compiler, $Llvm, $work) }
    )
    foreach ($fixture in $fixtures) {
        $output = "$work/$($fixture.name).exe"
        & $Compiler --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$root/test/native_cli/$($fixture.name).chibi" --entry Main --output $output `
            --llvm $Llvm --target x86_64-pc-windows-msvc --jobs 2
        if ($LASTEXITCODE) { throw "Could not compile Windows $($fixture.name)" }
        $arguments = $fixture.arguments
        & $output @arguments
        if ($LASTEXITCODE) { throw "Windows $($fixture.name) failed" }
    }

    # The compiler must find the checkout above the caller, bind its core and
    # graph explicitly, and construct both default products without a shell.
    Set-Location -LiteralPath "$root/packages/core"
    & $Compiler build --llvm $Llvm --build "$work/products" --jobs 2
    if ($LASTEXITCODE) { throw 'Native checkout build failed without host tools' }
    foreach ($name in 'chibi', 'chibi-asset-compiler') {
        if (!(Test-Path -LiteralPath "$work/products/sdk/$name.exe" -PathType Leaf)) {
            throw "Native checkout build omitted $name.exe"
        }
    }
    [IO.File]::WriteAllText("$work/Main.chibi", "package smoke`nMain :: () -> int { assert(6 * 7 == 42); return 0; }`n")
    & "$work/products/sdk/chibi.exe" run --llvm $Llvm "$work/Main.chibi"
    if ($LASTEXITCODE) { throw 'The built native CLI cannot find its packaged core or run a program' }
    Write-Host 'PASS native Windows checkout build, default products and packaged CLI without host tools'
} finally {
    Set-Location -LiteralPath $previous
    foreach ($name in $names) {
        $restored = $saved[$name]
        if ($null -eq $restored) { $restored = [NullString]::Value }
        [Environment]::SetEnvironmentVariable($name, $restored, 'Process')
    }
    Remove-Item -LiteralPath $work -Recurse -Force
}
