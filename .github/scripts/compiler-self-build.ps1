[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Prepare', 'Prove')][string]$Mode,
    [Parameter(Mandatory)][string]$Proof,
    [string]$Repository = $PWD.Path,
    [string]$Runtime,
    [string]$Platform,
    [switch]$PinnedRuntime,
    [switch]$QueryTarget
)

$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$Proof = [IO.Path]::GetFullPath($Proof)
$Repository = [IO.Path]::GetFullPath($Repository)
$hostPath = Join-Path $Proof ('chibi-compiler-host' + $(if ($IsWindows) { '.exe' } else { '' }))
$bootstrap = Join-Path $Proof 'tools/compiler/bootstrap'

function Read-Node([string]$Path) {
    return ,([System.Text.Json.Nodes.JsonNode]::Parse([IO.File]::ReadAllText($Path, $utf8)))
}
function Write-Node([string]$Path, $Node) {
    [IO.File]::WriteAllText($Path, $Node.ToJsonString(), $utf8)
}
function Json-Text([string]$Text) {
    return [System.Text.Json.Nodes.JsonNode]::Parse((ConvertTo-Json -InputObject $Text -Compress))
}
function Digest([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Source-File([string]$Root, [string]$Name, [string]$Core) {
    $relative = $Name
    $folder = Join-Path $Root 'tools/compiler'
    if ($Name.StartsWith("$Core/", [StringComparison]::Ordinal)) {
        $relative = $Name.Substring($Core.Length + 1)
        $folder = Join-Path $Root 'packages/core'
    }
    if ($relative -notmatch '^([A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+\.chibi$' -or
        @($relative.Split('/') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count) {
        throw "Unsafe compiler source path: $Name"
    }
    return Join-Path $folder $relative
}
function Check-Inventory($Manifest, [string]$Root) {
    if ([int]$Manifest['format'].ToJsonString() -ne 1 -or
        [int]$Manifest['minimum'].ToJsonString() -lt 1 -or
        [int]$Manifest['version'].ToJsonString() -lt [int]$Manifest['minimum'].ToJsonString() -or
        $Manifest['project'].GetValue[string]() -ne '/project' -or
        $Manifest['core'].GetValue[string]() -ne '/sdk/core' -or
        $Manifest['entry'].GetValue[string]() -ne '0x140a9546d9e8c296' -or
        $Manifest['target'].Count -ne 6 -or !$Manifest['sources'].Count) {
        throw 'The compiler manifest does not describe this self-build contract'
    }
    $recorded = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $folders = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($source in $Manifest['sources']) {
        $name = $source['path'].GetValue[string]()
        $path = Source-File $Root $name '/sdk/core'
        if (!$recorded.Add([IO.Path]::GetFullPath($path)) -or !(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing or repeated compiler source: $name"
        }
        $item = Get-Item -LiteralPath $path
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked compiler source: $name" }
        $directory = $item.Directory
        while ($directory.FullName.Length -gt $Root.Length) {
            if ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked compiler source directory: $name" }
            $directory = $directory.Parent
        }
        $null = $folders.Add($item.DirectoryName)
    }
    foreach ($folder in $folders) {
        foreach ($file in Get-ChildItem -LiteralPath $folder -File -Filter '*.chibi') {
            if (!$recorded.Contains($file.FullName)) { throw "Compiler package has an unrecorded source: $($file.FullName)" }
        }
    }
}
function Check-Closure($Manifest) {
    $file = Join-Path $bootstrap 'compiler.chibip'
    if ($Manifest['closure']['file'].GetValue[string]() -ne 'compiler.chibip' -or
        (Get-Item -LiteralPath $file).Length -ne [long]$Manifest['closure']['bytes'].ToJsonString() -or
        (Digest $file) -ne $Manifest['closure']['sha256'].GetValue[string]()) {
        throw 'The seed compiler differs from the checked-in size or SHA-256'
    }
}
function Runtime-Provenance {
    $record = [ordered]@{ runtime_sha256 = Digest $hostPath; platform = $Platform }
    if ($PinnedRuntime) {
        $manifest = Get-Content -LiteralPath (Join-Path $Proof 'tools/compiler/runtime.json') -Raw | ConvertFrom-Json
        $pin = $manifest.platforms.$Platform
        if ($manifest.format -ne 1 -or !$pin -or $pin.bytes -ne (Get-Item $hostPath).Length -or
            $pin.sha256 -ne $record.runtime_sha256) { throw 'The isolated runtime differs from its checked-in pin' }
        $record['runtime_source_revision'] = $manifest.source_revision
        $record['runtime_source_tree'] = $manifest.source_tree
        $record['runtime_release'] = "$($manifest.repository)/$($manifest.tag)"
    } else {
        $record['runtime_source_revision'] = $env:GITHUB_SHA
    }
    return $record
}

if ($Mode -eq 'Prepare') {
    if (!$Runtime -or (Test-Path -LiteralPath $Proof)) { throw 'Preparation requires a runtime and a new isolated directory' }
    $manifest = Read-Node (Join-Path $Repository 'tools/compiler/bootstrap/bootstrap.json')
    Check-Inventory $manifest $Repository
    New-Item -ItemType Directory -Path $bootstrap | Out-Null
    Copy-Item -LiteralPath $Runtime -Destination $hostPath
    foreach ($name in @('bootstrap.json', 'compiler.chibip')) {
        Copy-Item -LiteralPath (Join-Path $Repository "tools/compiler/bootstrap/$name") -Destination (Join-Path $bootstrap $name)
    }
    Copy-Item -LiteralPath (Join-Path $Repository 'tools/compiler/runtime.json') -Destination (Join-Path $Proof 'tools/compiler/runtime.json')
    Check-Closure $manifest
    foreach ($source in $manifest['sources']) {
        $name = $source['path'].GetValue[string]()
        $destination = Source-File $Proof $name '/sdk/core'
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath (Source-File $Repository $name '/sdk/core') -Destination $destination
    }
    if (!$IsWindows) {
        $modeBits = [IO.UnixFileMode]::UserRead -bor [IO.UnixFileMode]::UserWrite -bor [IO.UnixFileMode]::UserExecute -bor
                    [IO.UnixFileMode]::GroupRead -bor [IO.UnixFileMode]::GroupExecute -bor
                    [IO.UnixFileMode]::OtherRead -bor [IO.UnixFileMode]::OtherExecute
        [IO.File]::SetUnixFileMode($hostPath, $modeBits)
    }
    $null = Runtime-Provenance
    if (Get-ChildItem $Proof -Recurse -File | Where-Object { $_.Extension -in '.c', '.h', '.cc', '.cpp', '.cxx', '.m', '.mm' }) {
        throw 'The isolated compiler inputs contain native source'
    }
    return
}

$timer = [Diagnostics.Stopwatch]::StartNew()
$evidence = [ordered]@{
    revision = $env:GITHUB_SHA; runner_os = $env:RUNNER_OS; runner_arch = $env:RUNNER_ARCH
    stages = @(); invocations = @(); source_files = @()
    native_configure_seconds = $env:CONFIGURE_SECONDS; native_build_seconds = $env:BUILD_SECONDS
}
function Invoke-Runtime([string]$Name, [string[]]$Arguments) {
    $record = [ordered]@{ name = $Name; arguments = $Arguments }
    $evidence.invocations += $record
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $hostPath
    $start.WorkingDirectory = $Proof
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $start.Environment.Clear()
    if ($IsWindows) { $start.Environment['SystemRoot'] = $env:SystemRoot }
    $start.Environment['PATH'] = ''
    $start.Environment['CC'] = 'unavailable-in-self-build'
    $start.Environment['CXX'] = 'unavailable-in-self-build'
    $start.Environment['TEMP'] = $Proof
    $start.Environment['TMP'] = $Proof
    $start.Environment['TMPDIR'] = $Proof
    $record['child_environment'] = [ordered]@{}
    foreach ($key in $start.Environment.Keys | Sort-Object) { $record.child_environment[$key] = $start.Environment[$key] }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $out = $stdout.GetAwaiter().GetResult()
        $err = $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $Proof "$Name.stdout.txt"), $out, $utf8)
        [IO.File]::WriteAllText((Join-Path $Proof "$Name.stderr.txt"), $err, $utf8)
        Write-Host $out
        if ($err) { Write-Host $err }
        $record['exit_code'] = $process.ExitCode
        $record['elapsed_total_seconds'] = $timer.Elapsed.TotalSeconds
        if ($process.ExitCode) { throw "$Name failed with exit code $($process.ExitCode)" }
    } finally {
        if (!$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
    return $out
}
try {
    $provenance = Runtime-Provenance
    foreach ($key in $provenance.Keys) { $evidence[$key] = $provenance[$key] }
    $manifest = Read-Node (Join-Path $bootstrap 'bootstrap.json')
    Check-Inventory $manifest $Proof
    Check-Closure $manifest
    $evidence['seed_manifest_sha256'] = Digest (Join-Path $bootstrap 'bootstrap.json')
    $evidence['input_files'] = @(Get-ChildItem $Proof -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($Proof, $_.FullName).Replace('\', '/') } | Sort-Object)
    if ($QueryTarget) {
        $targetPath = Join-Path $Proof 'runtime-target.json'
        $null = Invoke-Runtime 'target' @('--target-json', $targetPath)
        $target = Read-Node $targetPath
        if (![System.Text.Json.Nodes.JsonNode]::DeepEquals($target, $manifest['target'])) {
            throw 'The runtime target differs from the complete checked-in bootstrap target'
        }
        $evidence['target_validation'] = 'Independent runtime target query matched all six manifest arguments'
    } else {
        $evidence['target_validation'] = 'Pinned runtime has no independent target-query interface; manifest target is used and output-artifact validates actual prepared ABI/native catalog'
    }
    $request = [System.Text.Json.Nodes.JsonArray]::new()
    $request.Add($manifest['project'].DeepClone())
    $request.Add([System.Text.Json.Nodes.JsonNode]::Parse('["core"]'))
    $collections = [System.Text.Json.Nodes.JsonArray]::new()
    $collections.Add($manifest['core'].DeepClone())
    $request.Add($collections)
    $paths = [System.Text.Json.Nodes.JsonArray]::new()
    $texts = [System.Text.Json.Nodes.JsonArray]::new()
    foreach ($source in $manifest['sources']) {
        $name = $source['path'].GetValue[string]()
        $file = Source-File $Proof $name '/sdk/core'
        $bytes = [IO.File]::ReadAllBytes($file)
        $paths.Add((Json-Text $name))
        $texts.Add((Json-Text ($utf8.GetString($bytes))))
        $evidence.source_files += [ordered]@{ path = $name; bytes = $bytes.Length; sha256 = Digest $file; seed_sha256 = $source['sha256'].GetValue[string]() }
    }
    $request.Add($paths)
    $request.Add($texts)
    $request.Add([System.Text.Json.Nodes.JsonNode]::Parse('false'))
    foreach ($part in $manifest['target']) { $request.Add($part.DeepClone()) }
    $requestPath = Join-Path $Proof 'compile.json'
    Write-Node $requestPath $request
    if ((Get-Item $requestPath).Length -gt 67108864) { throw 'Compiler request exceeds the runtime input limit' }
    $evidence['request_sha256'] = Digest $requestPath
    $inputCompiler = Join-Path $bootstrap 'compiler.chibip'
    $inputManifest = Join-Path $bootstrap 'bootstrap.json'
    $inputVersion = [int]$manifest['version'].ToJsonString()
    $empty = Join-Path $Proof 'contract.json'
    [IO.File]::WriteAllText($empty, '[]', $utf8)
    foreach ($stage in 1, 2) {
        $name = "stage$stage"
        $directory = Join-Path $Proof $name
        New-Item -ItemType Directory -Path $directory | Out-Null
        $output = Join-Path $directory 'compiler.chibip'
        $record = [ordered]@{
            stage = $name; input_compiler_sha256 = Digest $inputCompiler
            input_manifest_sha256 = Digest $inputManifest
        }
        $evidence.stages += $record
        $null = Invoke-Runtime $name @('--artifact', $inputCompiler, '--entry', $manifest['entry'].GetValue[string](),
            '--input', $requestPath, '--backend', 'sljit', '--budget', '2000000000', '--output-artifact', $output)
        $record['output_bytes'] = (Get-Item $output).Length
        if (!$record.output_bytes) { throw "$name produced an empty compiler" }
        $record['output_compiler_sha256'] = Digest $output
        # package-symbol:project:package_compiler:1:BootstrapContract:callable.
        # Only compiled outputs need this entry; the older seed need not have it.
        $contractText = Invoke-Runtime "$name-contract" @('--artifact', $output, '--entry', '0x3e6062160494dc34',
            '--input', $empty, '--backend', 'bytecode', '--budget', '10000')
        $contract = [System.Text.Json.Nodes.JsonNode]::Parse($contractText)
        if ($contract.Count -ne 1 -or $contract[0].Count -ne 2) { throw "$name returned an invalid bootstrap contract" }
        $version = [int]$contract[0][0].ToJsonString()
        $minimum = [int]$contract[0][1].ToJsonString()
        if ($minimum -lt 1 -or $version -lt $minimum -or $inputVersion -lt $minimum) {
            throw "$name requires a newer seed compiler"
        }
        $record['version'] = $version
        $record['minimum'] = $minimum
        $promoted = $manifest.DeepClone()
        $promoted['version'] = [System.Text.Json.Nodes.JsonNode]::Parse("$version")
        $promoted['minimum'] = [System.Text.Json.Nodes.JsonNode]::Parse("$minimum")
        $promoted['closure']['bytes'] = [System.Text.Json.Nodes.JsonNode]::Parse("$($record.output_bytes)")
        $promoted['closure']['sha256'] = Json-Text $record.output_compiler_sha256
        for ($i = 0; $i -lt $evidence.source_files.Count; $i++) {
            $promoted['sources'][$i]['bytes'] = [System.Text.Json.Nodes.JsonNode]::Parse("$($evidence.source_files[$i].bytes)")
            $promoted['sources'][$i]['sha256'] = Json-Text $evidence.source_files[$i].sha256
        }
        if ($env:GITHUB_SHA) { $promoted['revision'] = Json-Text $env:GITHUB_SHA }
        $inputCompiler = $output
        $inputManifest = Join-Path $directory 'bootstrap.json'
        $inputVersion = $version
        Write-Node $inputManifest $promoted
    }
    $first = $evidence.stages[0]
    $second = $evidence.stages[1]
    if ($second.input_compiler_sha256 -ne $first.output_compiler_sha256 -or
        $second.output_bytes -ne $first.output_bytes -or $second.output_compiler_sha256 -ne $first.output_compiler_sha256) {
        throw 'The newly built compiler did not reproduce itself'
    }
    $evidence['compiler_bytes'] = $second.output_bytes
    $evidence['compiler_sha256'] = $second.output_compiler_sha256
    Copy-Item -LiteralPath (Join-Path $Proof 'stage2/compiler.chibip') -Destination (Join-Path $Proof 'compiler.chibip')
    [IO.File]::WriteAllText((Join-Path $Proof 'compiler.sha256'), "$($first.output_compiler_sha256)  stage1/compiler.chibip`n$($second.output_compiler_sha256)  stage2/compiler.chibip`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $Proof 'compiler.chibip.sha256'), "$($second.output_compiler_sha256)  compiler.chibip`n", $utf8)
    $summary = "Both compiler stages reproduced $($second.output_bytes) bytes, SHA-256 $($second.output_compiler_sha256), in $($timer.Elapsed.TotalSeconds) seconds"
    Write-Host $summary
    if ($env:GITHUB_STEP_SUMMARY) { $summary >> $env:GITHUB_STEP_SUMMARY }
} catch {
    $evidence['error'] = $_.Exception.Message
    throw
} finally {
    $timer.Stop()
    $evidence['proof_seconds'] = $timer.Elapsed.TotalSeconds
    $evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $Proof 'evidence.json')
}
