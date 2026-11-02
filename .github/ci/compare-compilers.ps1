# Preserved legacy recovery proof; still needed by prepared/VM consumers.
$ErrorActionPreference = 'Stop'
$digests = @()
foreach ($platform in @('macos-15', 'ubuntu-24.04', 'windows-2022')) {
  $root = "recovered/compiler-self-build-$platform"
  $evidence = Get-Content "$root/evidence.json" -Raw | ConvertFrom-Json
  if ($evidence.error -or $evidence.stages.Count -ne 2 -or
      $evidence.stages[1].input_compiler_sha256 -ne $evidence.stages[0].output_compiler_sha256) {
    throw "$platform did not execute the compiler it built"
  }
  foreach ($stage in 1, 2) {
    $artifact = Get-Item "$root/stage$stage/compiler.chibip"
    $digest = (Get-FileHash $artifact.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($digest -ne $evidence.compiler_sha256 -or
        $digest -ne $evidence.stages[$stage - 1].output_compiler_sha256 -or
        $artifact.Length -ne $evidence.stages[$stage - 1].output_bytes) {
      throw "$platform stage$stage differs from its recorded output"
    }
    $digests += $digest
    "$platform stage$stage`: $digest" >> $env:GITHUB_STEP_SUMMARY
  }
}
if (@($digests | Select-Object -Unique).Count -ne 1) {
  throw 'The compiler stages differ across platforms with the validated bootstrap ABI'
}
