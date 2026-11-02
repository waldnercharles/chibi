# Actual Windows native checkout delegation, with a private minimal build program.
#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$Llvm
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$Llvm = (Resolve-Path -LiteralPath $Llvm).Path
$omega = [char]0x3a9
$wide = 'h' + [char]0xe9 + 'llo ' + [char]0x20ac
$work = Join-Path ([IO.Path]::GetTempPath()) ("chibi-windows-entry-$omega-" + [Guid]::NewGuid().ToString('N'))
$checkout = "$work/checkout with spaces"
$entry = "$checkout/tools/build.ps1"
$result = "$work/arguments.txt"
$release = $Compiler
$downloads = [Collections.Generic.List[string]]::new()
$previous = $PWD.Path
$caller = Join-Path $work 'caller'
$names = @('CHIBI_ENTRY_RESULT', 'CHIBI_ENTRY_EXIT', 'CHIBI_LLVM_BIN')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Check([bool]$condition, [string]$message) { if (!$condition) { throw $message } }
function gh {
    param([Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    $downloads.Add(($Arguments -join ' '))
    $destination = $Arguments[[Array]::IndexOf($Arguments, '--dir') + 1]
    $asset = $Arguments[[Array]::IndexOf($Arguments, '--pattern') + 1]
    Copy-Item -LiteralPath $release -Destination (Join-Path $destination $asset)
    $global:LASTEXITCODE = 0
}
function Entry([int]$expected, [string[]]$arguments) {
    & $entry @arguments
    Check ($LASTEXITCODE -eq $expected) "Expected entry status $expected, got $LASTEXITCODE"
    Check ($PWD.Path -eq $caller) 'The entry did not restore its caller directory'
}
try {
    New-Item -ItemType Directory -Path "$checkout/tools/bootstrap", "$checkout/tools/toolchain", "$checkout/tools/graph", "$checkout/packages", "$work/caller" | Out-Null
    Copy-Item -LiteralPath "$root/tools/build.ps1" -Destination $entry
    Copy-Item -LiteralPath "$root/tools/bootstrap/native-seed.ps1" -Destination "$checkout/tools/bootstrap/native-seed.ps1"
    Copy-Item -LiteralPath "$root/packages/core" -Destination "$checkout/packages/core" -Recurse
    [IO.File]::WriteAllText("$checkout/tools/toolchain/Driver.chibi", @'
package entry_probe
import os "core:os"
import strings "core:strings"
Main :: () -> int {
    arguments := [os.Root()];
    arguments.extend(os.Arguments());
    written := os.WriteText(os.Getenv("CHIBI_ENTRY_RESULT"), strings.Join(arguments, "\n"));
    if (written != nil) { return 74; }
    if (os.Getenv("CHIBI_ENTRY_EXIT") == "17") { return 17; }
    return 0;
}
'@)
    $env:CHIBI_ENTRY_RESULT = $result
    $env:CHIBI_LLVM_BIN = $Llvm
    Set-Location -LiteralPath "$work/caller"
    Entry 0 @('build', 'census', '--native-driver', $Compiler, '--build', "output with spaces $omega", '--fixture', '', 'a"b', 'space slash\', 'a\"b', $wide)
    $received = [IO.File]::ReadAllLines($result)
    Check ($received[0] -eq $checkout.Replace('\', '/')) 'The build program did not run in its checkout'
    Check ($received[1] -eq 'build' -and $received[2] -eq 'census') 'Build command forwarding differs'
    $at = [Array]::IndexOf($received, '--fixture')
    Check ($at -ge 0) 'The argument marker is missing'
    $expected = @('', 'a"b', 'space slash\', 'a\"b', $wide)
    for ($index = 0; $index -lt $expected.Count; $index++) {
        Check ($received[$at + 1 + $index] -eq $expected[$index]) 'Empty, quoted, backslash or Unicode arguments changed'
    }
    Check ($received[[Array]::IndexOf($received, '--build') + 1] -eq "output with spaces $omega") 'The output path split'
    $env:CHIBI_ENTRY_EXIT = '17'
    Entry 17 @('census', '--native-driver', $Compiler, '--llvm', $Llvm)
    $env:CHIBI_ENTRY_EXIT = ''
    Entry 2 @('census', '--native-driver')
    [IO.File]::WriteAllText("$checkout/tools/bootstrap/native-seed.tsv", "# no Windows pin`n")
    Entry 3 @('census')
    $hash = (Get-FileHash -LiteralPath $Compiler -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$checkout/tools/bootstrap/native-seed.tsv", "windows-x64`tfixture-tag`tchibi-native-windows-x64.exe`t$hash`n")
    Entry 0 @('census')
    Entry 0 @('census')
    Check ($downloads.Count -eq 1) 'Verified default selection downloaded twice'
    $seed = "$checkout/.chibi/native-seed/windows-x64/chibi.exe"
    [IO.File]::WriteAllText($seed, 'corrupted')
    Entry 0 @('census')
    Check ($downloads.Count -eq 2) 'Corrupted default seed was trusted'
    [IO.File]::WriteAllText($seed, 'preserve damaged bytes')
    $release = "$work/wrong release.exe"
    [IO.File]::WriteAllText($release, 'not the pinned compiler')
    $last = [IO.File]::ReadAllText($result)
    Entry 1 @('census')
    Check ([IO.File]::ReadAllText($result) -eq $last) 'A rejected seed was executed'
    Check ([IO.File]::ReadAllText($seed) -eq 'preserve damaged bytes') 'A rejected release replaced the cache'
    Write-Host 'PASS Windows native entry candidate/default, arguments, status, cwd and checksum failures'
} finally {
    Set-Location -LiteralPath $previous
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}
exit 0
