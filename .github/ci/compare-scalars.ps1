# Preserved legacy recovery proof; still needed by prepared/VM consumers.
$ErrorActionPreference = 'Stop'
$portable = @()
$expected = @{
  'macos-15' = @('macos-arm64', 'macOS')
  'ubuntu-24.04' = @('linux-x64', 'Linux')
  'windows-2022' = @('windows-x64', 'Windows')
}
foreach ($platform in @('macos-15', 'ubuntu-24.04', 'windows-2022')) {
  $root = "recovered/compiler-self-build-$platform"
  $recovery = Get-Content "$root/evidence.json" -Raw | ConvertFrom-Json
  $scalar = Get-Content "$root/scalar/evidence.json" -Raw | ConvertFrom-Json
  $artifact = Get-Item "$root/scalar/regression.chibip"
  $digest = (Get-FileHash $artifact.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  $recoveryDigest = (Get-FileHash "$root/evidence.json" -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($scalar.status -ne 'passed' -or $scalar.format -ne 1 -or
      ![string]::Equals($scalar.platform, $expected[$platform][0], [StringComparison]::Ordinal) -or
      ![string]::Equals($recovery.platform, $expected[$platform][0], [StringComparison]::Ordinal) -or
      ![string]::Equals($recovery.runner_os, $expected[$platform][1], [StringComparison]::Ordinal) -or
      $scalar.runtime_sha256 -ne $recovery.runtime_sha256 -or
      $scalar.compiler_sha256 -ne $recovery.compiler_sha256 -or
      $scalar.bootstrap_manifest_sha256 -ne $recovery.stages[0].input_manifest_sha256 -or
      $scalar.recovery_evidence_sha256 -ne $recoveryDigest -or
      $digest -ne $scalar.regression_sha256 -or $artifact.Length -ne $scalar.regression_bytes -or
      $scalar.case_count -le 0 -or $scalar.cases.Count -ne 4 * $scalar.case_count) {
    throw "$platform scalar proof differs from its runtime, recovered compiler or artifact"
  }
  # Only portable authored artifact/source/IR identities are compared.
  # Runtime executables and native object bytes are platform-specific.
  $portable += ([ordered]@{
    regression_sha256 = $digest
    compile_input_sha256 = $scalar.compile_input_sha256
    compiler_sources = $scalar.compiler_sources
    sources = $scalar.sources
    cases = @($scalar.cases | ForEach-Object {
      [ordered]@{ index = $_.index; producer = $_.producer; consumer = $_.consumer;
        accepted = $_.accepted; ir_sha256 = $_.ir_sha256 }
    })
  } | ConvertTo-Json -Depth 10 -Compress)
  "$platform scalar regression: $digest, $($scalar.case_count) authored cases" >> $env:GITHUB_STEP_SUMMARY
}
if (@($portable | Select-Object -Unique).Count -ne 1) {
  throw 'Portable scalar regression evidence differs across platforms'
}
