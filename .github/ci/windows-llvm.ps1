# The verified LLVM22 Windows tools shared by compiler CI jobs.
$ErrorActionPreference = 'Stop'
$name = 'clang+llvm-22.1.8-x86_64-pc-windows-msvc'
$archive = "$env:RUNNER_TEMP/llvm.tar.xz"
$phase = [Diagnostics.Stopwatch]::StartNew()
Write-Host 'LLVM download started'
curl.exe --fail --location --retry 2 --connect-timeout 30 --max-time 240 --speed-limit 1048576 --speed-time 60 "https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/$name.tar.xz" --output $archive
if ($LASTEXITCODE) { throw "could not download LLVM tools: curl exited $LASTEXITCODE" }
Write-Host ('LLVM download completed in {0:F2} s ({1} bytes)' -f $phase.Elapsed.TotalSeconds, (Get-Item $archive).Length)
$phase.Restart()
Write-Host 'LLVM checksum verification started'
$sha256 = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sha256 -ne 'd96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234') {
  throw 'LLVM archive SHA-256 differs from the pinned release'
}
Write-Host ('LLVM checksum verified in {0:F2} s' -f $phase.Elapsed.TotalSeconds)
$members = @("$name/bin/opt.exe", "$name/bin/llc.exe", "$name/bin/lld-link.exe",
             "$name/bin/llvm-link.exe", "$name/bin/llvm-lib.exe", "$name/bin/*.dll")
$sevenZip = (Get-Command 7z.exe, 7z -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (!$sevenZip -and $env:ProgramFiles) {
  $candidate = "$env:ProgramFiles/7-Zip/7z.exe"
  if (Test-Path $candidate -PathType Leaf) { $sevenZip = $candidate }
}
$phase.Restart()
if ($sevenZip) {
  # XZ contains one tar file. Decompress it once, then select the required
  # executables and their DLLs instead of writing every LLVM tool.
  $expanded = "$env:RUNNER_TEMP/llvm-unpack"
  New-Item -ItemType Directory -Force $expanded | Out-Null
  Write-Host "LLVM decompression started: $sevenZip"
  & $sevenZip x -txz -y "-o$expanded" $archive
  if ($LASTEXITCODE) { throw 'could not decompress the verified LLVM archive' }
  Write-Host ('LLVM decompression completed in {0:F2} s' -f $phase.Elapsed.TotalSeconds)
  $phase.Restart()
  Write-Host 'LLVM required-tool extraction started: 7-Zip'
  & $sevenZip x -ttar -y "-o$env:RUNNER_TEMP" "$expanded/llvm.tar" @members
  if ($LASTEXITCODE) { throw 'could not extract the required LLVM tools' }
  Remove-Item "$expanded/llvm.tar"
  Remove-Item $expanded
} else {
  Write-Host 'LLVM required-tool extraction started: tar'
  tar -xf $archive -C $env:RUNNER_TEMP @members
  if ($LASTEXITCODE) { throw 'could not extract the required LLVM tools' }
}
Write-Host ('LLVM required-tool extraction completed in {0:F2} s' -f $phase.Elapsed.TotalSeconds)
Remove-Item $archive
