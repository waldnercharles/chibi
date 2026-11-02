# Preserved legacy recovery proof; still needed by prepared/VM consumers.
$ErrorActionPreference = 'Stop'
if ($env:RUNNER_ARCH -ne $env:CHIBI_CI_ARCH -or
    [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString().ToUpperInvariant() -ne $env:CHIBI_CI_ARCH -or
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToUpperInvariant() -ne $env:CHIBI_CI_ARCH) {
  throw 'The runner architecture differs from this runtime seed'
}
$manifest = Get-Content tools/compiler/runtime.json -Raw | ConvertFrom-Json
$runtime = $manifest.platforms.($env:CHIBI_CI_PLATFORM)
$suffix = if ($IsWindows) { '.exe' } else { '' }
$asset = "chibi-compiler-host-$($env:CHIBI_CI_PLATFORM)" + $suffix
if ($manifest.format -ne 2 -or !$manifest.repository -or !$runtime.tag -or
    $runtime.source_revision -notmatch '^[a-f0-9]{40}$' -or
    $runtime.source_tree -notmatch '^[a-f0-9]{40}$' -or
    $runtime.max_artifact_bytes -le 0 -or
    $runtime.asset -ne $asset -or $runtime.bytes -le 0 -or
    $runtime.sha256 -notmatch '^[a-f0-9]{64}$') {
  throw 'The native runtime manifest has no complete pin for this platform'
}
# A runtime reads compiler closures up to the bound it was built with;
# a seed past it cannot load, so say so instead of failing opaquely.
$seedBytes = (Get-Item tools/compiler/bootstrap/compiler.chibip).Length
if ($seedBytes -gt $runtime.max_artifact_bytes) {
  throw "The $($env:CHIBI_CI_PLATFORM) runtime ($($runtime.tag)) reads compiler closures up to $($runtime.max_artifact_bytes) bytes, but the seed is $seedBytes bytes. Refresh this platform's runtime pin or shrink the closure."
}
$download = Join-Path $env:RUNNER_TEMP 'compiler-runtime-download'
New-Item -ItemType Directory -Path $download | Out-Null
gh release download $runtime.tag --repo $manifest.repository --pattern $asset --dir $download
if ($LASTEXITCODE) { throw 'Downloading the native runtime seed failed' }
$file = Get-Item (Join-Path $download $asset)
$digest = (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($file.Length -ne $runtime.bytes -or $digest -ne $runtime.sha256) {
  throw 'The native runtime seed differs from its checked-in size or SHA-256'
}
$proof = Join-Path $env:RUNNER_TEMP 'compiler-recovery-proof'
New-Item -ItemType Directory -Path "$proof/tools", "$proof/packages" | Out-Null
$hostPath = Join-Path $proof ('chibi-compiler-host' + $suffix)
Copy-Item $file.FullName $hostPath
if (!$IsWindows) {
  $mode = [IO.UnixFileMode]::UserRead -bor [IO.UnixFileMode]::UserWrite -bor [IO.UnixFileMode]::UserExecute -bor
          [IO.UnixFileMode]::GroupRead -bor [IO.UnixFileMode]::GroupExecute -bor
          [IO.UnixFileMode]::OtherRead -bor [IO.UnixFileMode]::OtherExecute
  [IO.File]::SetUnixFileMode($hostPath, $mode)
}
Copy-Item tools/compiler "$proof/tools/compiler" -Recurse
Copy-Item packages/core "$proof/packages/core" -Recurse
$nativeSources = Get-ChildItem $proof -Recurse -File | Where-Object { $_.Extension -in '.c', '.h', '.cc', '.cpp', '.cxx', '.m', '.mm' }
if ($nativeSources) { throw 'The self-build input directory contains native sources' }
