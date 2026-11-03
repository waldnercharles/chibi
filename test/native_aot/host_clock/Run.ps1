param(
    [Parameter(Mandatory=$true)][string]$Compiler,
    [Parameter(Mandatory=$true)][string]$LLVM
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = (Get-Item (Join-Path $PSScriptRoot '../../..')).FullName
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-clock-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -ErrorAction Stop | Out-Null
$complete = $false
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with $LASTEXITCODE" }
}
try {
    $core = Join-Path $root 'packages/core'
    $project = Join-Path $root 'tools/compiler'
    $target = 'x86_64-pc-windows-msvc'
    $factory = Join-Path $work 'factory.exe'
    Invoke-Checked $Compiler @('--compiler', $project, '--core', $core,
        '--source', (Join-Path $PSScriptRoot 'FactoryProbe.chibi'), '--entry', 'Main',
        '--output', $factory, '--llvm', $LLVM, '--target', $target, '--level', '2')
    Invoke-Checked $factory @($work, $LLVM, (Join-Path $PSScriptRoot 'Factory.ll'))
    $provider = Join-Path $work 'provider.lib'
    $object = Join-Path $work 'provider.o'
    Invoke-Checked (Join-Path $LLVM 'llc.exe') @('-mtriple=x86_64-pc-windows-msvc',
        '-filetype=obj', (Join-Path $PSScriptRoot 'Provider.ll'), '-o', $object)
    Invoke-Checked (Join-Path $LLVM 'llvm-lib.exe') @("/out:$provider", $object)
    $source = Join-Path $work 'Host.chibi'
    $template = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'HostProbe.chibi'))
    [IO.File]::WriteAllText($source, $template.Replace('@PROVIDER@', $provider.Replace('\', '/')),
        [Text.UTF8Encoding]::new($false))
    $images = Join-Path $work 'clock images'
    New-Item -ItemType Directory -Path $images -ErrorAction Stop | Out-Null
    foreach ($level in @('0', '2')) {
        $first = Join-Path $images "first-$level.dll"
        $second = Join-Path $images "second-$level.dll"
        Invoke-Checked $Compiler @('--compiler', $project, '--core', $core,
            '--source', (Join-Path $PSScriptRoot 'Guest.chibi'), '--collection', "host=$root/packages/host",
            '--shared', '--output', $first, '--llvm', $LLVM, '--target', $target, '--level', $level, '--jobs', '2')
        Copy-Item -LiteralPath $first -Destination $second
        $hostProgram = Join-Path $work 'host.exe'
        Invoke-Checked $Compiler @('--compiler', $project, '--core', $core,
            '--source', $source, '--entry', 'Main', '--output', $hostProgram,
            '--llvm', $LLVM, '--target', $target, '--level', $level, '--jobs', '2')
        foreach ($mode in @('normal', 'negative', 'invalid')) {
            & $hostProgram $first $second $mode
            $result = $LASTEXITCODE
            $expected = 0
            if ($mode -ne 'normal') { $expected = 134 }
            if ($result -ne $expected) { throw "clock $mode O$level returned $result, expected $expected" }
            Write-Host "PASS Windows clock images $mode O$level"
        }
    }
    $complete = $true
} finally {
    if ($complete) { Remove-Item -LiteralPath $work -Recurse -Force }
    else { Write-Host "Clock failure artifacts retained at $work" }
}
exit 0
