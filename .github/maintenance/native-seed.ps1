# Prepare a Windows native seed from the exact source's successful Windows CI
# proof. Downloads and restores files only; never executes or publishes a seed.
#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$RunId,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$deadline = [DateTime]::UtcNow.AddMinutes(180)

# One deadline covers every owned child, including download and archive work.
# Binary stdout goes directly to a private file without text transcoding.
function Run([string]$program, [string[]]$arguments, [string]$output = '') {
    $remaining = [int][Math]::Max(0, ($deadline - [DateTime]::UtcNow).TotalMilliseconds)
    if (!$remaining) { throw 'Native seed preparation exceeded its 180-minute budget' }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $program
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $file = $null
    $started = $false
    try {
        $started = $process.Start()
        $errorText = $process.StandardError.ReadToEndAsync()
        if ($output) {
            $file = [IO.File]::Open($output, [IO.FileMode]::CreateNew)
            $written = $process.StandardOutput.BaseStream.CopyToAsync($file)
        } else { $text = $process.StandardOutput.ReadToEndAsync() }
        if (!$process.WaitForExit($remaining)) { throw 'Native seed preparation exceeded its 180-minute budget' }
        $errors = $errorText.GetAwaiter().GetResult()
        if ($output) { $null = $written.GetAwaiter().GetResult() }
        else { $result = $text.GetAwaiter().GetResult() }
        if ($process.ExitCode) { throw "$program exited $($process.ExitCode): $errors" }
        if (!$output -and $result.Length) { return $result.Trim() }
    } finally {
        if ($started -and !$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        if ($file) { $file.Dispose() }
        $process.Dispose()
    }
}
function Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }

if ($RunId -cnotmatch '^[1-9][0-9]*$') { throw 'RunId must be a numeric GitHub Actions run ID' }
$git = (Get-Command git -CommandType Application | Select-Object -First 1).Source
$gh = (Get-Command gh -CommandType Application | Select-Object -First 1).Source
$revision = Run $git @('rev-parse', 'HEAD')
$tree = Run $git @('rev-parse', 'HEAD^{tree}')
if (Run $git @('status', '--porcelain', '--untracked-files=normal')) { throw 'Use a clean checkout for native seed preparation' }
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$relative = [IO.Path]::GetRelativePath($root, $output).Replace('\', '/')
if ($relative -eq '.' -or (!$relative.StartsWith('../') -and ![IO.Path]::IsPathRooted($relative))) {
    throw 'OutputDirectory must be outside the source checkout'
}
if (Test-Path -LiteralPath $output) { throw 'OutputDirectory must not already exist' }
$repository = if ($env:CHIBI_REPOSITORY) { $env:CHIBI_REPOSITORY } else { 'waldnercharles/chibi' }
$run = Run $gh @('api', "repos/$repository/actions/runs/$RunId") | ConvertFrom-Json
if ($run.repository.full_name -cne $repository -or $run.path -cne '.github/workflows/ci.yml' -or
    $run.head_sha -cne $revision -or $run.status -cne 'completed') {
    throw 'The CI producer does not match this repository, workflow, completed run and source revision'
}
$jobs = Run $gh @('api', "repos/$repository/actions/runs/$RunId/jobs?per_page=100") | ConvertFrom-Json
$windows = @($jobs.jobs | Where-Object { $_.name -ceq 'Native self-host (Windows)' -and $_.conclusion -ceq 'success' })
if ($windows.Count -ne 1) { throw 'The exact source needs a successful Windows fixed-point and runtime job' }
$listed = Run $gh @('api', "repos/$repository/actions/runs/$RunId/artifacts?per_page=100") | ConvertFrom-Json
$artifacts = @($listed.artifacts | Where-Object { $_.name -ceq 'native-windows-fixed-point' -and !$_.expired })
if ($artifacts.Count -ne 1 -or $artifacts[0].workflow_run.head_sha -cne $revision -or
    $artifacts[0].workflow_run.id -ne $run.id) { throw 'The run has no unique unexpired fixed-point artifact from this source' }
$artifact = $artifacts[0]
if ($artifact.digest -cnotmatch '^sha256:[0-9a-f]{64}$') { throw 'The artifact has no SHA-256 digest' }
New-Item -ItemType Directory -Path $output | Out-Null
$archive = Join-Path $output 'native-windows-fixed-point.zip'
Run $gh @('api', "repos/$repository/actions/artifacts/$($artifact.id)/zip") $archive
$archiveDigest = Hash $archive
if ("sha256:$archiveDigest" -cne $artifact.digest) { throw 'The downloaded artifact differs from its GitHub SHA-256 digest' }

# Restore only these fixed relative names. Other compiler intermediates stay in
# the retained archive; duplicate selected members are rejected by CreateNew.
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName -cnotmatch '^(stage[345]/(chibi\.exe|compiler\.log)|windows-fixed-point\.log)$') { continue }
        $path = Join-Path $output $entry.FullName
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
        $source = $entry.Open()
        try {
            $destination = [IO.File]::Open($path, [IO.FileMode]::CreateNew)
            try { $source.CopyTo($destination) } finally { $destination.Dispose() }
        } finally { $source.Dispose() }
    }
} finally { $zip.Dispose() }
$reported = @{}
foreach ($line in [IO.File]::ReadAllLines("$output/windows-fixed-point.log")) {
    if ($line -cmatch '^stage ([345]) sha256 ([0-9a-f]{64}) \([0-9]+ s\)$') {
        if ($reported.ContainsKey($matches[1])) { throw 'The fixed-point log repeats a stage hash' }
        $reported[$matches[1]] = $matches[2]
    }
}
$hashes = @{}
foreach ($number in '3', '4', '5') {
    $path = "$output/stage$number/chibi.exe"
    if ($number -eq '5' -and ![IO.File]::Exists($path) -and !$reported.ContainsKey($number)) { continue }
    $digest = Hash $path
    if ($digest -cne $reported[$number]) { throw "Stage $number bytes differ from the producer's recorded hash" }
    $hashes[$number] = $digest
}
$fixed = '3'
if ($hashes['3'] -cne $hashes['4']) {
    if (!$hashes.ContainsKey('5') -or $hashes['4'] -cne $hashes['5']) { throw 'Windows stages have no bounded fixed point' }
    $fixed = '4'
} elseif ($hashes.ContainsKey('5')) { throw 'The artifact contains an unexpected extra generation' }

$asset = 'chibi-native-windows-x64.exe'
Copy-Item -LiteralPath "$output/stage$fixed/chibi.exe" -Destination "$output/$asset"
$digest = Hash "$output/$asset"
# Retain and actually restore the compiler's authored closure from this commit.
Run $git @('archive', '--format=zip', "--output=$output/source.zip", 'HEAD', 'tools/compiler', 'packages/core')
[IO.Compression.ZipFile]::ExtractToDirectory("$output/source.zip", "$output/restored-source")
$inputs = @(Run $git @('ls-files', 'tools/compiler', 'packages/core')) -split "`n"
foreach ($path in $inputs) {
    if ((Hash "$root/$path") -cne (Hash "$output/restored-source/$path")) { throw "Restored source differs: $path" }
}
if ((Run $git @('rev-parse', 'HEAD')) -cne $revision -or (Run $git @('rev-parse', 'HEAD^{tree}')) -cne $tree -or
    (Run $git @('status', '--porcelain', '--untracked-files=normal'))) { throw 'The source changed during preparation' }
$evidence = [ordered]@{
    revision = $revision; source_tree = $tree; platform = 'windows-x64'
    run_id = $run.id; run_url = $run.html_url; run_conclusion = $run.conclusion
    job_id = $windows[0].id; job_steps = @($windows[0].steps | Select-Object name, conclusion)
    artifact_id = $artifact.id; artifact_sha256 = $archiveDigest
    stage_hashes = $hashes; fixed_stage = $fixed; compiler_sha256 = $digest
    source_archive_sha256 = Hash "$output/source.zip"; restored_source_files = $inputs.Count
}
$evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$output/evidence.json" -Encoding utf8NoBOM
"windows-x64`tnative-seed-$revision`t$asset`t$digest" | Set-Content -LiteralPath "$output/native-seed.tsv" -Encoding utf8NoBOM
@($asset, 'source.zip', 'native-windows-fixed-point.zip', 'native-seed.tsv', 'evidence.json') | ForEach-Object {
    "$(Hash "$output/$_")  $_"
} | Set-Content -LiteralPath "$output/SHA256SUMS" -Encoding utf8NoBOM
Write-Host "Prepared $asset from Windows stage $fixed, SHA-256 $digest; no release or pin was changed"
