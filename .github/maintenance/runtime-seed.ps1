# Called by runtime-seed.sh, whose process-group watchdog bounds this whole phase.
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
if ($IsLinux) { $platform = 'linux-x64'; $runnerOS = 'Linux'; $arch = 'X64' }
elseif ($IsMacOS) { $platform = 'macos-arm64'; $runnerOS = 'macOS'; $arch = 'ARM64' }
else { throw 'Runtime preparation supports only Linux x64 and macOS arm64; keep the Windows pin' }
if ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString().ToUpperInvariant() -ne $arch -or
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToUpperInvariant() -ne $arch) {
  throw 'The process or operating-system architecture differs from this runtime seed'
}
$revision = git rev-parse HEAD
if ($LASTEXITCODE) { throw 'Cannot identify the source revision' }
$tree = git rev-parse 'HEAD^{tree}'
if ($LASTEXITCODE) { throw 'Cannot identify the source tree' }
$dirty = git status --porcelain --untracked-files=normal
if ($LASTEXITCODE -or $dirty) { throw 'Use a clean checkout for runtime preparation' }
if (!$env:LIBCLANG_ROOT) { throw 'Set LIBCLANG_ROOT to the installed LLVM prefix' }
if (Test-Path $OutputDirectory) { throw 'OutputDirectory must not already exist' }
$proof = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path "$proof/tools", "$proof/packages" | Out-Null
sh tools/bootstrap/seed.sh
if ($LASTEXITCODE) { throw 'Fetching the pinned compiler seed failed' }
if ($env:CC) { $env:NATIVE_TOOLCHAIN_FLAGS = "-DCMAKE_C_COMPILER=$env:CC -DCMAKE_CXX_COMPILER=$env:CXX" }
$buildTimer = [Diagnostics.Stopwatch]::StartNew()
sh tools/build.sh build --release --target chibi-compiler-host
if ($LASTEXITCODE) { throw 'Building the native runtime failed' }
$buildTimer.Stop()
Copy-Item build-release/sdk/chibi-compiler-host "$proof/chibi-compiler-host"
Copy-Item tools/compiler "$proof/tools/compiler" -Recurse
Copy-Item packages/core "$proof/packages/core" -Recurse
$nativeSources = Get-ChildItem $proof -Recurse -File | Where-Object { $_.Extension -in '.c', '.h', '.cc', '.cpp', '.cxx', '.m', '.mm' }
if ($nativeSources) { throw 'The recovery input directory contains native sources' }
$inputs = @(Get-ChildItem $proof -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($proof, $_.FullName).Replace('\', '/') } | Sort-Object)
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = Join-Path $proof 'chibi-compiler-host'
$start.WorkingDirectory = $proof
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$arguments = @('--bootstrap', 'tools/compiler/bootstrap', '--compiler', 'tools/compiler', '--core', 'packages/core', '--recover', (Join-Path $proof 'compiler.chibip'))
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
$start.Environment.Clear()
$start.Environment['PATH'] = ''
$start.Environment['CC'] = 'unavailable-in-recovery-proof'
$start.Environment['CXX'] = 'unavailable-in-recovery-proof'
$start.Environment['TEMP'] = $proof
$start.Environment['TMP'] = $proof
$start.Environment['TMPDIR'] = $proof
$childEnvironment = [ordered]@{}
foreach ($key in $start.Environment.Keys | Sort-Object) { $childEnvironment[$key] = $start.Environment[$key] }
$timer = [Diagnostics.Stopwatch]::StartNew()
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$process.WaitForExit()
$timer.Stop()
$out = $stdout.GetAwaiter().GetResult()
$err = $stderr.GetAwaiter().GetResult()
$out | Set-Content "$proof/recovery.stdout.txt" -NoNewline
$err | Set-Content "$proof/recovery.stderr.txt" -NoNewline
Write-Host $out
if ($err) { Write-Host $err }
$evidence = [ordered]@{
  revision = $revision
  source_tree = $tree
  runner_os = $runnerOS
  runner_arch = $arch
  runtime_sha256 = (Get-FileHash "$proof/chibi-compiler-host" -Algorithm SHA256).Hash.ToLowerInvariant()
  native_build_seconds = $buildTimer.Elapsed.TotalSeconds
  proof_seconds = $timer.Elapsed.TotalSeconds
  proof_exit_code = $process.ExitCode
  command = "chibi-compiler-host $($arguments -join ' ')"
  child_environment = $childEnvironment
  input_files = $inputs
}
$evidence | ConvertTo-Json -Depth 4 | Set-Content "$proof/evidence.json"
if ($process.ExitCode) { throw "Compiler recovery failed with exit code $($process.ExitCode)" }
$artifact = Get-Item "$proof/compiler.chibip"
if (!$artifact.Length) { throw 'Compiler recovery produced an empty artifact' }
$sha256 = (Get-FileHash $artifact.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$evidence['compiler_bytes'] = $artifact.Length
$evidence['compiler_sha256'] = $sha256
$evidence | ConvertTo-Json -Depth 4 | Set-Content "$proof/evidence.json"
"$sha256  compiler.chibip" | Set-Content "$proof/compiler.chibip.sha256"
$asset = "chibi-compiler-host-$platform"
Copy-Item "$proof/chibi-compiler-host" "$proof/$asset"
"$($evidence.runtime_sha256)  $asset" | Set-Content "$proof/$asset.sha256"
if ($IsMacOS) { $imports = otool -L "$proof/$asset" }
else { $imports = readelf --program-headers --dynamic "$proof/$asset" }
if ($LASTEXITCODE) { throw 'Listing native imports failed' }
if ($IsLinux) { $imports = $imports | Select-String 'interpreter|NEEDED' }
$imports | Set-Content "$proof/native-imports.txt"
if ((git rev-parse HEAD) -ne $revision -or (git rev-parse 'HEAD^{tree}') -ne $tree) { throw 'The source changed during preparation' }
$dirty = git status --porcelain --untracked-files=normal
if ($LASTEXITCODE -or $dirty) { throw 'The source changed during preparation' }
Write-Host "Prepared $asset; recovered $($artifact.Length) compiler bytes, SHA-256 $sha256"
