# Preserved legacy recovery proof; still needed by prepared/VM consumers.
$ErrorActionPreference = 'Stop'
$timer = [Diagnostics.Stopwatch]::StartNew()
$proof = Join-Path $env:RUNNER_TEMP 'compiler-recovery-proof'
$suffix = if ($IsWindows) { '.exe' } else { '' }
$hostPath = Join-Path $proof ('chibi-compiler-host' + $suffix)
$evidence = [ordered]@{
  revision = $env:GITHUB_SHA
  runner_os = $env:RUNNER_OS
  runner_arch = $env:RUNNER_ARCH
  platform = $env:CHIBI_CI_PLATFORM
  stages = @()
}
try {
  $runtimeManifest = Get-Content "$proof/tools/compiler/runtime.json" -Raw | ConvertFrom-Json
  $runtime = $runtimeManifest.platforms.($env:CHIBI_CI_PLATFORM)
  $runtimeDigest = (Get-FileHash $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($runtimeDigest -ne $runtime.sha256) { throw 'The isolated runtime differs from its pin' }
  $evidence['runtime_sha256'] = $runtimeDigest
  $evidence['runtime_source_revision'] = $runtime.source_revision
  $evidence['runtime_source_tree'] = $runtime.source_tree
  $evidence['runtime_release'] = "$($runtimeManifest.repository)/$($runtime.tag)"
  $evidence['input_files'] = @(Get-ChildItem $proof -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($proof, $_.FullName).Replace('\', '/') } | Sort-Object)
  function Recover-Stage($name, $bootstrap, $output) {
    $record = [ordered]@{
      stage = $name
      input_compiler_sha256 = (Get-FileHash (Join-Path $bootstrap 'compiler.chibip') -Algorithm SHA256).Hash.ToLowerInvariant()
      input_manifest_sha256 = (Get-FileHash (Join-Path $bootstrap 'bootstrap.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $evidence.stages += $record
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $hostPath
    $start.WorkingDirectory = $proof
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $arguments = @('--bootstrap', $bootstrap, '--compiler', 'tools/compiler', '--core', 'packages/core', '--recover', $output)
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $start.Environment.Clear()
    if ($IsWindows) { $start.Environment['SystemRoot'] = $env:SystemRoot }
    $start.Environment['PATH'] = ''
    $start.Environment['CC'] = 'unavailable-in-self-build'
    $start.Environment['CXX'] = 'unavailable-in-self-build'
    $start.Environment['TEMP'] = $proof
    $start.Environment['TMP'] = $proof
    $start.Environment['TMPDIR'] = $proof
    $record['arguments'] = $arguments
    $record['child_environment'] = [ordered]@{}
    foreach ($key in $start.Environment.Keys | Sort-Object) { $record.child_environment[$key] = $start.Environment[$key] }
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $out = $stdout.GetAwaiter().GetResult()
    $err = $stderr.GetAwaiter().GetResult()
    $out | Set-Content "$proof/$name.stdout.txt" -NoNewline
    $err | Set-Content "$proof/$name.stderr.txt" -NoNewline
    Write-Host $out
    if ($err) { Write-Host $err }
    $record['exit_code'] = $process.ExitCode
    $record['elapsed_total_seconds'] = $timer.Elapsed.TotalSeconds
    if ($process.ExitCode) {
      $bound = $runtime.max_artifact_bytes
      if ($err -match 'exceeds type, shape or storage limits|artifact bound') {
        throw "$name failed: a compiler closure passed the $($env:CHIBI_CI_PLATFORM) runtime's $bound-byte bound ($($runtime.tag)). Refresh this platform's runtime pin or shrink the closure."
      }
      throw "$name failed with exit code $($process.ExitCode)"
    }
    $artifact = Get-Item $output
    if (!$artifact.Length) { throw "$name produced an empty compiler" }
    $record['output_bytes'] = $artifact.Length
    $record['output_compiler_sha256'] = (Get-FileHash $output -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  New-Item -ItemType Directory -Path "$proof/stage1", "$proof/stage2" | Out-Null
  # The checked artifact is only a seed. It has to load on the pinned
  # runtime and compile these sources; the host then builds stage one
  # with it and walks on to the sources' fixed point, so a change to the
  # code the compiler generates for itself needs no new seed. Stage 1
  # is that fixed point.
  Recover-Stage 'stage1' "$proof/tools/compiler/bootstrap" "$proof/stage1/compiler.chibip"
  # Preserve every original source hash and target ABI value. Only the
  # closure's size/hash change so the next call loads the new compiler.
  # JsonNode retains integer precision in untouched native identities.
  $manifest = [System.Text.Json.Nodes.JsonNode]::Parse([IO.File]::ReadAllText("$proof/tools/compiler/bootstrap/bootstrap.json"))
  $closure = $manifest['closure'].AsObject()
  $closure['bytes'] = [System.Text.Json.Nodes.JsonNode]::Parse([string]$evidence.stages[0].output_bytes)
  $closure['sha256'] = [System.Text.Json.Nodes.JsonNode]::Parse('"' + $evidence.stages[0].output_compiler_sha256 + '"')
  [IO.File]::WriteAllText("$proof/stage1/bootstrap.json", $manifest.ToJsonString())
  # Stage 2 is built by stage 1 and must be stage 1's bytes.
  Recover-Stage 'stage2' "$proof/stage1" "$proof/stage2/compiler.chibip"
  $first = $evidence.stages[0]
  $second = $evidence.stages[1]
  if ($second.input_compiler_sha256 -ne $first.output_compiler_sha256 -or
      $second.output_bytes -ne $first.output_bytes -or
      $second.output_compiler_sha256 -ne $first.output_compiler_sha256) {
    throw 'The newly built compiler did not reproduce itself'
  }
  $evidence['compiler_bytes'] = $second.output_bytes
  $evidence['compiler_sha256'] = $second.output_compiler_sha256
  "$($first.output_compiler_sha256)  stage1/compiler.chibip`n$($second.output_compiler_sha256)  stage2/compiler.chibip" | Set-Content "$proof/compiler.sha256"
  "Both compiler stages reproduced $($second.output_bytes) bytes, SHA-256 $($second.output_compiler_sha256), in $($timer.Elapsed.TotalSeconds) seconds" >> $env:GITHUB_STEP_SUMMARY
} catch {
  $evidence['error'] = $_.Exception.Message
  throw
} finally {
  $timer.Stop()
  $evidence['proof_seconds'] = $timer.Elapsed.TotalSeconds
  $evidence | ConvertTo-Json -Depth 8 | Set-Content "$proof/evidence.json"
}
