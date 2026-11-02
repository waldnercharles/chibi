# Exercise the actual verifier with a local release-download boundary.
#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-native-seed-' + [char]0x3a9 + '-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$work/checkout/tools/bootstrap" | Out-Null
$fetch = "$work/checkout/tools/bootstrap/native-seed.ps1"
$manifest = "$work/checkout/tools/bootstrap/native-seed.tsv"
$output = "$work/cache with spaces/chibi.exe"
$release = "$work/release"
$downloads = [Collections.Generic.List[string]]::new()
$downloadStatus = 0
function gh {
    param([Parameter(ValueFromRemainingArguments)][string[]]$Arguments)
    $downloads.Add(($Arguments -join ' '))
    $global:LASTEXITCODE = $downloadStatus
    if ($downloadStatus) { return }
    $destination = $Arguments[[Array]::IndexOf($Arguments, '--dir') + 1]
    $asset = $Arguments[[Array]::IndexOf($Arguments, '--pattern') + 1]
    Copy-Item -LiteralPath $release -Destination (Join-Path $destination $asset)
}
function Check([bool]$condition, [string]$message) { if (!$condition) { throw $message } }
function Fetch([int]$status) {
    & $fetch -Platform windows-x64 -Output $output
    Check ($LASTEXITCODE -eq $status) "Expected seed status $status, got $LASTEXITCODE"
}
try {
    Copy-Item -LiteralPath "$root/tools/bootstrap/native-seed.ps1" -Destination $fetch
    [IO.File]::WriteAllText($release, "fixture seed`n")
    $hash = (Get-FileHash -LiteralPath $release -Algorithm SHA256).Hash.ToLowerInvariant()
    $pin = "windows-x64`tfixture-tag`tchibi-native-windows-x64.exe`t$hash`n"
    [IO.File]::WriteAllText($manifest, "# no Windows pin`n")
    Fetch 3
    Check (!(Test-Path -LiteralPath $output)) 'A missing pin created a seed'
    [IO.File]::WriteAllText($manifest, $pin)
    Fetch 0
    Check ($downloads.Count -eq 1) 'Cold fetch did not download exactly once'
    Fetch 0
    Check ($downloads.Count -eq 1) 'Warm verified bytes downloaded again'

    # A same-size rewrite with restored metadata still fails the content check.
    $stamp = [IO.File]::GetLastWriteTimeUtc($output)
    [IO.File]::WriteAllText($output, "damaged seed`n")
    [IO.File]::SetLastWriteTimeUtc($output, $stamp)
    Fetch 0
    Check ($downloads.Count -eq 2) 'Corrupted cached bytes were trusted'
    Check ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant() -eq $hash) 'Repair bytes differ'

    foreach ($bad in @(
        ($pin + $pin),
        "windows-x64`tfixture-tag`t../escape`t$hash`n",
        "windows-x64`t--option`tasset`t$hash`n",
        "windows-x64`ttag`tasset`t$($hash.ToUpperInvariant())`n",
        "windows-x64`ttag`tasset`tshort`n",
        "Windows-x64`ttag`tasset`t$hash`n",
        "alien-x64`ttag`tasset`t$hash`n",
        "windows-x64`ttag`tasset`t$hash`textra`n"
    )) {
        [IO.File]::WriteAllText($manifest, $bad)
        Fetch 2
        Check ($downloads.Count -eq 2) 'An invalid manifest reached the download boundary'
        Check ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant() -eq $hash) 'An invalid manifest changed the output'
    }
    [IO.File]::WriteAllText($manifest, $pin.Replace("`n", "`r`n"))
    Fetch 0
    Check ($downloads.Count -eq 2) 'CRLF changed a valid manifest'
    [IO.File]::WriteAllText($output, 'preserve me')
    [IO.File]::WriteAllText($release, 'wrong release')
    Fetch 1
    Check ([IO.File]::ReadAllText($output) -eq 'preserve me') 'Checksum failure replaced the output'
    $downloadStatus = 7
    Fetch 7
    Check ([IO.File]::ReadAllText($output) -eq 'preserve me') 'Download failure replaced the output'
    Check (@(Get-ChildItem -LiteralPath ([IO.Path]::GetDirectoryName($output)) -Force | Where-Object Name -Like '.native-seed-*').Count -eq 0) 'Private download directories leaked'
    Write-Host 'PASS native seed missing, malformed, cold, warm, repaired, failed and preserved-output cases'
} finally { Remove-Item -LiteralPath $work -Recurse -Force }
exit 0
