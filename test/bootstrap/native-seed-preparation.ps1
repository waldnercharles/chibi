# Run the actual artifact preparation against disposable Git trees and GH data.
# On Windows, Compiler and Llvm build the small authored GitHub client fixture.
#requires -Version 7.0
param([string]$Compiler = '', [string]$Llvm = '')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$work = Join-Path ([IO.Path]::GetTempPath()) ('chibi-seed-preparation-Ω-' + [Guid]::NewGuid().ToString('N'))
$repo = "$work/repo"
$api = "$work/api"
$bin = "$work/bin"
$names = @('PATH', 'CHIBI_REPOSITORY', 'CHIBI_SEED_API_FIXTURE')
$saved = @{}
foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Check([bool]$condition, [string]$message) { if (!$condition) { throw $message } }
function Git([string[]]$arguments) {
    $output = & $git -C $repo @arguments
    if ($LASTEXITCODE) { throw 'Fixture Git command failed' }
    return $output
}
function Json([string]$file, $value) {
    $value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$api/$file" -Encoding utf8NoBOM
}
try {
    New-Item -ItemType Directory -Path "$repo/.github/maintenance", "$repo/tools/compiler", "$repo/packages/core", $api, $bin | Out-Null
    Copy-Item -LiteralPath "$root/.github/maintenance/native-seed.ps1" -Destination "$repo/.github/maintenance/native-seed.ps1"
    [IO.File]::WriteAllText("$repo/tools/compiler/Main.chibi", "package seed_fixture`n// Ω source round trip`n")
    [IO.File]::WriteAllText("$repo/packages/core/Core.chibi", "package core_fixture`n")
    $git = (Get-Command git -CommandType Application | Select-Object -First 1).Source
    Git @('init', '-q')
    Git @('config', 'user.name', 'Native seed fixture')
    Git @('config', 'user.email', 'seed@example.invalid')
    Git @('config', 'core.hooksPath', "$work/no-hooks")
    Git @('add', '.')
    Git @('commit', '-qm', 'fixture source')
    $revision = Git @('rev-parse', 'HEAD')
    if ($IsWindows) {
        if (!$Compiler -or !$Llvm) { throw 'Windows preparation fixture requires Compiler and Llvm' }
        & $Compiler --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$root/test/bootstrap/fixtures/NativeSeedGitHub.chibi" --entry Main --llvm $Llvm --target x86_64-pc-windows-msvc --output "$bin/gh.exe" --jobs 2
        if ($LASTEXITCODE) { throw 'Could not build the authored GitHub fixture' }
    } else {
        [IO.File]::WriteAllText("$bin/gh", @'
#!/bin/sh
set -eu
case "$2" in
    */jobs\?*) file=jobs.json ;;
    */artifacts\?*) file=artifacts.json ;;
    */zip) file=archive.zip ;;
    *) file=run.json ;;
esac
cat "$CHIBI_SEED_API_FIXTURE/$file"
'@)
        & chmod +x "$bin/gh"
        if ($LASTEXITCODE) { throw 'Cannot prepare the local GH boundary' }
    }
    $env:PATH = $bin + [IO.Path]::PathSeparator + $saved['PATH']
    $env:CHIBI_REPOSITORY = 'fixture/chibi'
    $env:CHIBI_SEED_API_FIXTURE = $api
    $variants = @('valid', 'transition', 'wrong-repository', 'wrong-source', 'wrong-workflow', 'incomplete-run', 'failed-windows', 'duplicate-artifact', 'expired-artifact', 'wrong-artifact-source', 'wrong-archive-digest', 'wrong-stage-hash', 'no-fixed-point', 'unexpected-generation', 'dirty-source')
    foreach ($variant in $variants) {
        $stages = "$work/stages"
        if (Test-Path -LiteralPath $stages) { Remove-Item -LiteralPath $stages -Recurse -Force }
        New-Item -ItemType Directory -Path "$stages/stage3", "$stages/stage4" | Out-Null
        [IO.File]::WriteAllText("$stages/stage3/chibi.exe", 'fixture seed A')
        $next = if ($variant -in 'transition', 'no-fixed-point') { 'fixture seed B' } else { 'fixture seed A' }
        [IO.File]::WriteAllText("$stages/stage4/chibi.exe", $next)
        if ($variant -in 'transition', 'unexpected-generation') {
            New-Item -ItemType Directory -Path "$stages/stage5" | Out-Null
            [IO.File]::WriteAllText("$stages/stage5/chibi.exe", $next)
        }
        $lines = @()
        foreach ($number in 3, 4, 5) {
            $path = "$stages/stage$number/chibi.exe"
            if (!(Test-Path -LiteralPath $path)) { continue }
            $digest = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($variant -eq 'wrong-stage-hash' -and $number -eq 4) { $digest = '0' * 64 }
            $lines += "stage $number sha256 $digest (1 s)"
        }
        $lines | Set-Content -LiteralPath "$stages/windows-fixed-point.log" -Encoding utf8NoBOM
        if (Test-Path -LiteralPath "$api/archive.zip") { Remove-Item -LiteralPath "$api/archive.zip" }
        [IO.Compression.ZipFile]::CreateFromDirectory($stages, "$api/archive.zip")
        $digest = (Get-FileHash -LiteralPath "$api/archive.zip" -Algorithm SHA256).Hash.ToLowerInvariant()
        $run = @{id = 77; repository = @{full_name = 'fixture/chibi'}; path = '.github/workflows/ci.yml'; head_sha = $revision; status = 'completed'; conclusion = 'success'; html_url = 'https://example.invalid/fixture/77'}
        $job = @{id = 88; name = 'Native self-host (Windows)'; conclusion = 'success'; steps = @(@{name = 'runtime fixture'; conclusion = 'success'})}
        $artifact = @{id = 99; name = 'native-windows-fixed-point'; expired = $false; digest = "sha256:$digest"; workflow_run = @{id = 77; head_sha = $revision}}
        switch ($variant) {
            'wrong-repository' { $run.repository.full_name = 'other/chibi' }
            'wrong-source' { $run.head_sha = '0' * 40 }
            'wrong-workflow' { $run.path = '.github/workflows/other.yml' }
            'incomplete-run' { $run.status = 'in_progress' }
            'failed-windows' { $job.conclusion = 'failure' }
            'expired-artifact' { $artifact.expired = $true }
            'wrong-artifact-source' { $artifact.workflow_run.head_sha = '0' * 40 }
            'wrong-archive-digest' { $artifact.digest = 'sha256:' + ('0' * 64) }
            'dirty-source' { [IO.File]::WriteAllText("$repo/dirty", 'changed') }
        }
        Json 'run.json' $run
        Json 'jobs.json' @{jobs = @($job)}
        $artifacts = @($artifact)
        if ($variant -eq 'duplicate-artifact') { $artifacts += $artifact }
        Json 'artifacts.json' @{artifacts = $artifacts}
        $output = "$work/output-$variant"
        $failed = $false
        try { & "$repo/.github/maintenance/native-seed.ps1" -RunId 77 -OutputDirectory $output }
        catch { $failed = $true; Write-Host "Rejected $variant`: $($_.Exception.Message)" }
        $valid = $variant -in 'valid', 'transition'
        Check ($failed -ne $valid) "Unexpected preparation result for $variant"
        Check ((Test-Path -LiteralPath "$output/native-seed.tsv") -eq $valid) 'A failed preparation produced a release pin proposal'
        if ($valid) {
            $evidence = Get-Content -LiteralPath "$output/evidence.json" -Raw | ConvertFrom-Json
            Check ($evidence.revision -eq $revision -and $evidence.restored_source_files -eq 2) 'Source identity/restoration evidence differs'
            $expected = if ($variant -eq 'valid') { '3' } else { '4' }
            Check ($evidence.fixed_stage -eq $expected) 'The bounded fixed-point stage differs'
        }
    }
    Write-Host 'PASS native Windows seed artifact identity, digests, fixed point and source restoration controls'
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
}
exit 0
