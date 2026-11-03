# Fetch one SHA-256-pinned native compiler. No pin exits 3, invalid manifest 2,
# filesystem/checksum failure 1; gh failures keep their status. Existing output
# survives every failure.
#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$Platform,
    [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = $null
try {
    $pins = [Collections.Generic.Dictionary[string, string[]]]::new([StringComparer]::Ordinal)
    $line = 0
    foreach ($text in [IO.File]::ReadAllLines("$root/tools/bootstrap/native-seed.tsv")) {
        $line++
        if ($text -eq '' -or $text.StartsWith('#')) { continue }
        $fields = $text.Split("`t")
        if ($fields.Count -ne 4 -or $fields[0] -cnotmatch '^(linux-x64|macos-arm64|windows-x64)$' -or
            $fields[1] -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or
            $fields[2] -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$' -or
            $fields[3] -cnotmatch '^[0-9a-f]{64}$' -or $pins.ContainsKey($fields[0])) {
            throw [ArgumentException]::new("native-seed: tools/bootstrap/native-seed.tsv:$line has an invalid or duplicate pin")
        }
        $pins[$fields[0]] = $fields
    }
    if (!$pins.ContainsKey($Platform)) {
        [Console]::Error.WriteLine("native-seed: tools/bootstrap/native-seed.tsv pins no compiler for $Platform")
        exit 3
    }
    $pin = $pins[$Platform]
    $destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
    if ([IO.File]::Exists($destination) -and
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -eq $pin[3]) { exit 0 }

    $parent = [IO.Path]::GetDirectoryName($destination)
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    $work = Join-Path $parent ('.native-seed-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    $repository = if ($env:CHIBI_REPOSITORY) { $env:CHIBI_REPOSITORY } else { 'waldnercharles/chibi' }
    & gh release download $pin[1] --repo $repository --pattern $pin[2] --dir $work
    if ($LASTEXITCODE) {
        [Console]::Error.WriteLine("native-seed: release download exited $LASTEXITCODE")
        exit $LASTEXITCODE
    }
    $download = Join-Path $work $pin[2]
    $actual = (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $pin[3]) { throw "native-seed: $($pin[2]) has SHA-256 $actual, not the pinned $($pin[3])" }
    # The private download is on the destination volume; replace only verified bytes.
    if ([IO.File]::Exists($destination)) { [IO.File]::Replace($download, $destination, [NullString]::Value) }
    else { [IO.File]::Move($download, $destination) }
} catch [ArgumentException] {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 2
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
} finally {
    if ($work -and [IO.Directory]::Exists($work)) { Remove-Item -LiteralPath $work -Recurse -Force }
}
exit 0
