param(
    [Parameter(Mandatory)][string]$Proof,
    [Parameter(Mandatory)][ValidateSet('linux-x64', 'macos-arm64', 'windows-x64')][string]$Platform
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$Proof = [IO.Path]::GetFullPath($Proof)
$output = Join-Path $Proof 'scalar'
if (Test-Path -LiteralPath $output) { throw 'Scalar evidence output already exists' }
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$timer = [Diagnostics.Stopwatch]::StartNew()
$evidence = [ordered]@{ format = 1; status = 'running'; platform = $Platform; compiler_sources = @(); sources = @(); runs = @(); batch_members = @(); cases = @() }

function Digest([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    try {
        return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()
    } finally { $stream.Dispose() }
}
function Read-Node([string]$path) {
    return ,([System.Text.Json.Nodes.JsonNode]::Parse([IO.File]::ReadAllText($path, $utf8)))
}
function String-Node([string]$value) {
    return ,([System.Text.Json.Nodes.JsonNode]::Parse((ConvertTo-Json -InputObject $value -Compress)))
}
function Copy-Node($value) {
    return ,([System.Text.Json.Nodes.JsonNode]::Parse($value.ToJsonString()))
}
function Write-Node([string]$path, $value) {
    [IO.File]::WriteAllText($path, $value.ToJsonString(), $utf8)
}
function Entry([string]$symbol) {
    # Canonical package-symbol FNV-1a, without lossy floating-point conversion.
    $hash = [System.Numerics.BigInteger]::Parse('14695981039346656037')
    $mask = [System.Numerics.BigInteger]::Parse('18446744073709551615')
    foreach ($item in $utf8.GetBytes('package-symbol:' + $symbol)) {
        $hash = (($hash -bxor $item) * 1099511628211) -band $mask
    }
    if ($hash.IsZero) { $hash = [System.Numerics.BigInteger]::One }
    return '0x' + ([uint64]$hash).ToString('x16')
}
# Reuse the pinned host's existing 64-item limit. Batch only pure producers/checks;
# retain every raw-byte CLI crossing. The 96-source matrix needs 406 children,
# not 466. Keep the same finite total and never give a child more than
# 120 seconds or what remains.
$batchSize = 64
function Host-Limit([double]$elapsed, [string]$name) {
    $total = 240000
    if ($elapsed -ge $total) {
        $crossings = if ($evidence.Contains('case_count')) {
            "$($evidence.cases.Count)/$($evidence.case_count * 4)"
        } else { "$($evidence.cases.Count)/not-yet-enumerated" }
        throw "Scalar regression exceeded its $($total / 1000)s total deadline after $($evidence.runs.Count) host invocations and $crossings VM crossings; next: $name"
    }
    return [Math]::Max(1, [Math]::Min(120000, [int]($total - $elapsed)))
}
# Each batch member calls compiler_host_invoke with the same host backend.
# sum_counts adds unsigned counters and flags overflow: a zero aggregate for
# other backends/fallback proves no member used them; selected counts cannot
# cancel or hide a fallback. Counts remain aggregate, not invented per item.
function Backend-Entries([string]$errorText, [string]$backend, [string]$name) {
    $matches = [regex]::Matches($errorText, '(?m)^compiler-host bytecode=(\d+) sljit=(\d+) wasm=(\d+) fallback=(\d+) overflow=(\d+)\r?$')
    if ($matches.Count -ne 1) { throw "$name has no unique execution counters" }
    $counts = @($matches[0].Groups | Select-Object -Skip 1 | ForEach-Object { [uint64]$_.Value })
    $selected = if ($backend -eq 'bytecode') { 0 } else { 1 }
    if ($counts[$selected] -eq 0 -or $counts[1 - $selected] -ne 0 -or
        $counts[2] -ne 0 -or $counts[3] -ne 0 -or $counts[4] -ne 0) {
        throw "$name did not execute exclusively on its requested backend"
    }
    return $counts[$selected]
}
function Invoke-Host([string]$name, [string]$artifact, [string]$entry,
                     [string]$request, [string]$backend, [string]$raw = '', [switch]$Compile, [switch]$Batch) {
    $startedAt = $timer.Elapsed.TotalSeconds
    $limit = Host-Limit $timer.Elapsed.TotalMilliseconds $name
    $stdoutPath = Join-Path $output ($name + '.stdout')
    $stderrPath = Join-Path $output ($name + '.stderr')
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $hostPath
    $start.WorkingDirectory = $Proof
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $arguments = @('--artifact', $artifact, '--entry', $entry, '--input', $request,
                   '--backend', $backend, '--budget', '2000000000', '--timeout-ms', [string]$limit)
    if ($Batch) { $arguments += '--batch' }
    if ($raw) { $arguments += @($(if ($Compile) { '--output-artifact' } else { '--output-bytes' }), $raw) }
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $start.Environment.Clear()
    if ($IsWindows) { $start.Environment['SystemRoot'] = $env:SystemRoot }
    $start.Environment['PATH'] = ''
    $start.Environment['CC'] = 'unavailable-in-scalar-regression'
    $start.Environment['CXX'] = 'unavailable-in-scalar-regression'
    foreach ($key in @('TEMP', 'TMP', 'TMPDIR')) { $start.Environment[$key] = $output }
    $process = $null
    $started = $false
    $stdoutFile = $null
    $stderrFile = $null
    try {
        # Copy streams as bytes. In particular, producer stdout is never decoded
        # and re-encoded before being passed as the consumer's sole argument.
        $stdoutFile = [IO.File]::Open($stdoutPath, [IO.FileMode]::CreateNew)
        $stderrFile = [IO.File]::Open($stderrPath, [IO.FileMode]::CreateNew)
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $start
        if (!$process.Start()) { throw 'Compiler host did not start' }
        $started = $true
        $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdoutFile)
        $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderrFile)
        if (!$process.WaitForExit($limit)) { throw "$name exceeded its process deadline" }
        [void]$stdoutTask.GetAwaiter().GetResult()
        [void]$stderrTask.GetAwaiter().GetResult()
        $exit = $process.ExitCode
    } finally {
        if ($process) {
            if ($started -and !$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
            $process.Dispose()
        }
        if ($stdoutFile) { $stdoutFile.Dispose() }
        if ($stderrFile) { $stderrFile.Dispose() }
    }
    $record = [ordered]@{ name = $name; backend = $backend; exit_code = $exit;
        input_sha256 = (Digest $request); artifact_sha256 = (Digest $artifact);
        stdout_sha256 = (Digest $stdoutPath); stderr_sha256 = (Digest $stderrPath);
        batch = $Batch.IsPresent; elapsed_seconds = ($timer.Elapsed.TotalSeconds - $startedAt) }
    $evidence.runs += $record
    $errorText = [IO.File]::ReadAllText($stderrPath, $utf8)
    if ($exit -eq 0) {
        $record['selected_entries'] = Backend-Entries $errorText $backend $name
    }
    return [pscustomobject]@{ Name = $name; Exit = $exit; Stdout = $stdoutPath; Stderr = $errorText }
}
function Require-Success($result) {
    if ($result.Exit -ne 0) { throw "Compiler invocation $($result.Name) failed: $($result.Stderr)" }
}
function Scalar-Result($result) {
    Require-Success $result
    $node = Read-Node $result.Stdout
    if ($node.AsArray().Count -ne 1) { throw 'Expected one authored scalar check result' }
    return $node[0].GetValue[int]()
}


# The existing host batch API reuses only the loaded artifact: each item gets
# its own activation, arena and instruction budget. Join/split transport bytes,
# never reserialize an owned producer envelope through a host JSON model.
function Write-Batch([string]$path, [string[]]$files) {
    if ($files.Count -lt 1 -or $files.Count -gt $batchSize) { throw 'Invalid scalar batch size' }
    $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew)
    try {
        $stream.WriteByte(91)
        for ($index = 0; $index -lt $files.Count; $index++) {
            if ($index) { $stream.WriteByte(44) }
            $bytes = [IO.File]::ReadAllBytes($files[$index])
            $stream.Write($bytes, 0, $bytes.Length)
        }
        $stream.WriteByte(93)
    } finally { $stream.Dispose() }
}
function Split-Batch($result, [string]$prefix, [int]$count) {
    Require-Success $result
    $bytes = [IO.File]::ReadAllBytes($result.Stdout)
    $batchHash = Digest $result.Stdout
    $document = [System.Text.Json.JsonDocument]::Parse($utf8.GetString($bytes))
    try {
        if ($count -lt 1 -or $count -gt $batchSize -or
            $document.RootElement.ValueKind -ne [System.Text.Json.JsonValueKind]::Array -or
            $document.RootElement.GetArrayLength() -ne $count) { throw 'Scalar batch result count differs' }
        $offset = 0
        while ($bytes[$offset] -in 9, 10, 13, 32) { $offset++ }
        if ($bytes[$offset] -ne 91) { throw 'Scalar batch has no opening bracket' }
        $offset++
        $parts = @()
        $index = 0
        foreach ($item in $document.RootElement.EnumerateArray()) {
            while ($bytes[$offset] -in 9, 10, 13, 32) { $offset++ }
            if ($index) {
                if ($bytes[$offset] -ne 44) { throw 'Scalar batch has no separator' }
                $offset++
                while ($bytes[$offset] -in 9, 10, 13, 32) { $offset++ }
            }
            # GetRawText identifies boundaries only. The forwarded file is copied
            # from the original byte buffer, preserving escapes and numeric text.
            $raw = $item.GetRawText()
            $length = $utf8.GetByteCount($raw)
            $member = [byte[]]::new($length)
            [Array]::Copy($bytes, $offset, $member, 0, $length)
            if (![string]::Equals($utf8.GetString($member), $raw, [StringComparison]::Ordinal)) {
                throw 'Scalar batch member differs from its original byte range'
            }
            $name = "$prefix-$index"
            $path = [IO.Path]::Combine($output, $name + '.stdout')
            [IO.File]::WriteAllBytes($path, $member)
            $evidence.batch_members += [ordered]@{ name = $name; batch = $result.Name; index = $index;
                batch_sha256 = $batchHash; offset = $offset; bytes = $length; sha256 = (Digest $path) }
            $parts += [pscustomobject]@{ Name = $name; Batch = $result.Name; Item = $index; Exit = 0; Stdout = $path; Stderr = '' }
            $offset += $length
            $index++
        }
        while ($bytes[$offset] -in 9, 10, 13, 32) { $offset++ }
        if ($bytes[$offset] -ne 93) { throw 'Scalar batch has no closing bracket' }
        $offset++
        while ($offset -lt $bytes.Length -and $bytes[$offset] -in 9, 10, 13, 32) { $offset++ }
        if ($offset -ne $bytes.Length) { throw 'Scalar batch has trailing bytes' }
        return $parts
    } finally { $document.Dispose() }
}

try {
    $suffix = if ($IsWindows) { '.exe' } else { '' }
    $hostPath = Join-Path $Proof ('chibi-compiler-host' + $suffix)
    $compiler = Join-Path $Proof 'stage2/compiler.chibip'
    $runtimeFile = Join-Path $repo 'tools/compiler/runtime.json'
    if ((Digest $runtimeFile) -ne (Digest (Join-Path $Proof 'tools/compiler/runtime.json'))) {
        throw 'Proof runtime manifest differs from the checked-out source'
    }
    $runtime = Read-Node $runtimeFile
    $pin = $runtime['platforms'][$Platform]
    $runtimeHash = Digest $hostPath
    if ($runtimeHash -ne $pin['sha256'].GetValue[string]() -or
        (Get-Item -LiteralPath $hostPath).Length -ne $pin['bytes'].GetValue[long]()) {
        throw 'The proof runtime differs from its exact platform pin'
    }
    $recovery = Read-Node (Join-Path $Proof 'evidence.json')
    $compilerHash = Digest $compiler
    if ($recovery['error'] -or $recovery['stages'].AsArray().Count -ne 2 -or
        $runtimeHash -ne $recovery['runtime_sha256'].GetValue[string]() -or
        $runtime['source_revision'].GetValue[string]() -ne $recovery['runtime_source_revision'].GetValue[string]() -or
        $runtime['source_tree'].GetValue[string]() -ne $recovery['runtime_source_tree'].GetValue[string]() -or
        $compilerHash -ne $recovery['compiler_sha256'].GetValue[string]() -or
        $compilerHash -ne $recovery['stages'][1]['output_compiler_sha256'].GetValue[string]() -or
        (Digest (Join-Path $Proof 'stage1/compiler.chibip')) -ne $compilerHash -or
        $recovery['stages'][1]['input_compiler_sha256'].GetValue[string]() -ne $compilerHash) {
        throw 'The scalar compiler is not the exact successfully recovered stage2'
    }
    $manifestFile = Join-Path $repo 'tools/compiler/bootstrap/bootstrap.json'
    if ((Digest $manifestFile) -ne (Digest (Join-Path $Proof 'tools/compiler/bootstrap/bootstrap.json'))) {
        throw 'Proof bootstrap manifest differs from the checked-out source'
    }
    $manifest = Read-Node $manifestFile
    # The checked artifact is only the seed: recovery started from it, and the
    # compiler under test is the fixed point that seed's successors reached.
    if ((Digest (Join-Path $repo 'tools/compiler/bootstrap/compiler.chibip')) -ne
            $recovery['stages'][0]['input_compiler_sha256'].GetValue[string]() -or
        $recovery['stages'][0]['input_manifest_sha256'].GetValue[string]() -ne (Digest $manifestFile)) {
        throw 'Recovery did not start from the checked seed'
    }
    $evidence['runtime_sha256'] = $runtimeHash
    $evidence['compiler_sha256'] = $compilerHash
    $evidence['bootstrap_manifest_sha256'] = Digest $manifestFile
    $evidence['recovery_evidence_sha256'] = Digest (Join-Path $Proof 'evidence.json')
    # Validate every listed compiler source and each complete included package,
    # not just a hand-selected whitelist of known files. The list, not the
    # seed's record of the sources it was built from, names the compiler.
    $compilerFiles = [Collections.Generic.Dictionary[string, bool]]::new([StringComparer]::Ordinal)
    $sourceList = 'tools/compiler/asset_compiler/CompilerSources.txt'
    if ((Digest (Join-Path $repo $sourceList)) -ne (Digest (Join-Path $Proof $sourceList))) {
        throw 'Proof compiler source list differs from the checked-out source'
    }
    foreach ($name in [IO.File]::ReadAllLines((Join-Path $repo $sourceList)) | Where-Object { $_.Trim() }) {
        $name = $name.Trim()
        $relative = if ($name.StartsWith('/sdk/core/')) { 'packages/core/' + $name.Substring(10) } else { 'tools/compiler/' + $name }
        $file = Join-Path $repo $relative
        if ($compilerFiles.ContainsKey($relative) -or (Digest $file) -ne (Digest (Join-Path $Proof $relative))) {
            throw "Compiler source identity differs: $relative"
        }
        $compilerFiles[$relative] = $true
        $evidence.compiler_sources += [ordered]@{ path = $relative; sha256 = (Digest $file); bytes = (Get-Item -LiteralPath $file).Length }
    }
    foreach ($folder in @($compilerFiles.Keys | ForEach-Object { Split-Path $_ -Parent } | Sort-Object -Unique)) {
        foreach ($root in @($repo, $Proof)) {
            foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Filter '*.chibi') {
                $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
                if (!$compilerFiles.ContainsKey($relative)) { throw "Unlisted compiler package member: $relative" }
            }
        }
    }
    $map = [ordered]@{
        'scalar_regression/Regression.chibi' = 'test/native_aot/scalar/Regression.chibi'
        'scalar_llvm_tests/Tests.chibi' = 'test/native_aot/scalar/Tests.chibi'
        'scalar_cases/Cases.chibi' = 'test/scalar_facts/cases/Cases.chibi'
        'scalar_facts/Facts.chibi' = 'tools/compiler/scalar_facts/Facts.chibi'
        'scalar_llvm/Emit.chibi' = 'tools/compiler/scalar_llvm/Emit.chibi'
        'llvm_native/Emit.chibi' = 'tools/compiler/llvm_native/Emit.chibi'
        'native_abi/Abi.chibi' = 'tools/compiler/native_abi/Abi.chibi'
        '/sdk/core/strings/Strings.chibi' = 'packages/core/strings/Strings.chibi'
    }
    foreach ($folder in @($map.Values | ForEach-Object { Split-Path $_ -Parent } | Sort-Object -Unique)) {
        $roots = @($repo)
        if ($folder.Replace('\', '/').StartsWith('tools/compiler/') -or
            $folder.Replace('\', '/').StartsWith('packages/core/')) { $roots += $Proof }
        foreach ($root in $roots) {
            foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Filter '*.chibi') {
                $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
                $listed = @($map.Values | Where-Object { [string]::Equals($_, $relative, [StringComparison]::Ordinal) })
                if ($listed.Count -ne 1) { throw "Unlisted regression package member: $relative" }
            }
        }
    }
    $paths = [System.Text.Json.Nodes.JsonArray]::new()
    $texts = [System.Text.Json.Nodes.JsonArray]::new()
    foreach ($name in $map.Keys) {
        $file = Join-Path $repo $map[$name]
        if (($map[$name].StartsWith('tools/compiler/') -or $map[$name].StartsWith('packages/core/')) -and
            (Digest $file) -ne (Digest (Join-Path $Proof $map[$name]))) {
            throw "Regression dependency differs from isolated proof: $name"
        }
        $paths.Add((String-Node $name))
        $texts.Add((String-Node ([IO.File]::ReadAllText($file, $utf8))))
        $evidence.sources += [ordered]@{ path = $map[$name]; virtual = $name; sha256 = (Digest $file); bytes = (Get-Item -LiteralPath $file).Length }
    }
    $arguments = [System.Text.Json.Nodes.JsonArray]::new()
    $arguments.Add((String-Node '/project'))
    $arguments.Add([System.Text.Json.Nodes.JsonNode]::Parse('["core"]'))
    $arguments.Add([System.Text.Json.Nodes.JsonNode]::Parse('["/sdk/core"]'))
    $arguments.Add($paths)
    $arguments.Add($texts)
    $arguments.Add([System.Text.Json.Nodes.JsonNode]::Parse('false'))
    foreach ($target in $manifest['target'].AsArray()) { $arguments.Add((Copy-Node $target)) }
    $compileInput = Join-Path $output 'compile.json'
    Write-Node $compileInput $arguments
    $empty = Join-Path $output 'empty.json'
    [IO.File]::WriteAllText($empty, '[]', $utf8)
    $regression = Join-Path $output 'regression.chibip'
    $alternate = Join-Path $output 'regression-bytecode.chibip'
    Require-Success (Invoke-Host 'compile-sljit' $compiler ($manifest['entry'].GetValue[string]()) $compileInput 'sljit' $regression -Compile)
    Require-Success (Invoke-Host 'compile-bytecode' $compiler ($manifest['entry'].GetValue[string]()) $compileInput 'bytecode' $alternate -Compile)
    if ((Digest $regression) -ne (Digest $alternate)) { throw 'Regression compilation differs between backends' }
    Remove-Item -LiteralPath $alternate
    $evidence['regression_sha256'] = Digest $regression
    $evidence['regression_bytes'] = (Get-Item -LiteralPath $regression).Length
    $evidence['compile_input_sha256'] = Digest $compileInput
    foreach ($backend in @('bytecode', 'sljit')) {
        $test = Invoke-Host "tests-$backend" $regression (Entry 'project:scalar_regression:1:Tests:callable') $empty $backend
        if ((Scalar-Result $test) -ne 0) { throw 'Authored consumer representation tests failed' }
    }
    $sourcesResult = Invoke-Host 'sources' $regression (Entry 'project:scalar_regression:1:Sources:callable') $empty 'sljit'
    Require-Success $sourcesResult
    $sourcesBytecode = Invoke-Host 'sources-bytecode' $regression (Entry 'project:scalar_regression:1:Sources:callable') $empty 'bytecode'
    Require-Success $sourcesBytecode
    if ((Digest $sourcesResult.Stdout) -ne (Digest $sourcesBytecode.Stdout)) { throw 'Authored source corpus differs between backends' }
    $sources = (Read-Node $sourcesResult.Stdout)[0].AsArray()
    if ($sources.Count -le 0 -or $sources.Count -gt 96) { throw 'Authored source corpus exceeds its runner bound' }
    $evidence['case_count'] = $sources.Count
    $priorChecked = @{}
    $collectEntry = Entry 'project:package_compiler:1:CollectScalar:callable'
    $checkEntry = Entry 'project:scalar_regression:1:Check:callable'
    $emitEntry = Entry 'project:scalar_regression:1:EmitBytes:callable'
    for ($first = 0; $first -lt $sources.Count; $first += $batchSize) {
        $count = [Math]::Min($batchSize, $sources.Count - $first)
        $inputs = @()
        for ($item = 0; $item -lt $count; $item++) {
            $index = $first + $item
            $request = [System.Text.Json.Nodes.JsonArray]::new()
            $request.Add((String-Node '/project'))
            $request.Add((String-Node 'Scalar.chibi'))
            $request.Add((String-Node ($sources[$index].GetValue[string]())))
            $request.Add((Copy-Node $manifest['target'][0]))
            foreach ($fact in 2..5) { $request.Add((Copy-Node $manifest['target'][$fact])) }
            $inputPath = [IO.Path]::Combine($output, "case-$index.json")
            Write-Node $inputPath $request
            $inputs += $inputPath
        }
        $batchInput = [IO.Path]::Combine($output, "batch-$first.json")
        Write-Batch $batchInput $inputs
        $produced = @{}
        $checked = @{}
        foreach ($producerBackend in @('bytecode', 'sljit')) {
            $prefix = "$first-$producerBackend"
            $producer = Invoke-Host "$prefix-produce-batch" $compiler $collectEntry $batchInput $producerBackend -Batch
            $produced[$producerBackend] = @(Split-Batch $producer "$prefix-produce" $count)
            for ($item = 0; $item -lt $count; $item++) {
                $returned = Read-Node $produced[$producerBackend][$item].Stdout
                if ($returned.AsArray().Count -ne 1 -or $returned[0].AsArray().Count -lt 2 -or
                    ![string]::Equals($returned[0][0]['text'].GetValue[string](), $sources[$first + $item].GetValue[string](), [StringComparison]::Ordinal) -or
                    ![string]::Equals($returned[0][0]['name'].GetValue[string](), 'Scalar.chibi', [StringComparison]::Ordinal)) {
                    throw "Producer result source identity differs at case $($first + $item) ($producerBackend)"
                }
            }
            foreach ($consumerBackend in @('bytecode', 'sljit')) {
                # Producer stdout already is an array of invocation argument lists.
                # Forward that entire buffer unchanged through the consumer batch.
                $crossing = "$producerBackend-$consumerBackend"
                $check = Invoke-Host "$first-$crossing-check-batch" $regression $checkEntry $producer.Stdout $consumerBackend -Batch
                $checked[$crossing] = @(Split-Batch $check "$first-$crossing-check" $count)
            }
        }
        # Preserve original case/producer/consumer classification and raw output
        # order. Up to 63 later pure cases may already have been computed, but
        # the first failing classification stops all later raw output/next batches.
        for ($item = 0; $item -lt $count; $item++) {
            $index = $first + $item
            $inputPath = $inputs[$item]
            foreach ($producerBackend in @('bytecode', 'sljit')) {
                $producer = $produced[$producerBackend][$item]
                $envelopeHash = Digest $producer.Stdout
                foreach ($consumerBackend in @('bytecode', 'sljit')) {
                    $name = "$index-$producerBackend-$consumerBackend"
                    $check = $checked["$producerBackend-$consumerBackend"][$item]
                    $classification = Scalar-Result $check
                    if ($classification -notin 0, 1) { throw "Authored source/consumer check failed at $name (batch item $item): $classification" }
                    $rawPath = [IO.Path]::Combine($output, $name + '.ll')
                    $raw = Invoke-Host "$name-raw" $regression $emitEntry $producer.Stdout $consumerBackend $rawPath
                    $row = [ordered]@{ index = $index; producer = $producerBackend; consumer = $consumerBackend;
                        input_sha256 = (Digest $inputPath); envelope_sha256 = $envelopeHash; accepted = ($classification -eq 1);
                        producer_batch = $producer.Batch; producer_item = $producer.Item;
                        check_batch = $check.Batch; check_item = $check.Item }
                    if ($classification -eq 1) {
                        Require-Success $raw
                        if ((Get-Item -LiteralPath $rawPath).Length -eq 0) { throw 'Accepted raw output is empty' }
                        $row['ir_sha256'] = Digest $rawPath
                    } else {
                        if ($raw.Exit -eq 0 -or $raw.Stderr.Trim() -ne 'the compiler entry returned no bytes' -or
                            (Test-Path -LiteralPath $rawPath)) { throw 'Rejected raw output was published' }
                        # This host failure path omits counters. Its matching authored
                        # Check above executes EmitBytes and proves empty bytes on the
                        # selected backend; no unavailable raw counters are invented.
                        $crossing = "$producerBackend-$consumerBackend"
                        if (!$priorChecked.ContainsKey($crossing)) {
                            [IO.File]::WriteAllText($rawPath, "prior complete IR`n", $utf8)
                            $priorHash = Digest $rawPath
                            $prior = Invoke-Host "$name-prior" $regression $emitEntry $producer.Stdout $consumerBackend $rawPath
                            if ($prior.Exit -eq 0 -or $prior.Stderr.Trim() -ne 'the compiler entry returned no bytes' -or
                                (Digest $rawPath) -ne $priorHash) { throw 'Rejected output damaged an existing file' }
                            Remove-Item -LiteralPath $rawPath
                            $priorChecked[$crossing] = $true
                        }
                    }
                    if ((Digest $producer.Stdout) -ne $envelopeHash) { throw 'Producer envelope changed during forwarding' }
                    $evidence.cases += [pscustomobject]$row
                }
            }
        }
    }
    if ($priorChecked.Count -ne 4) { throw 'The authored corpus did not cover rejected prior-output preservation' }
    foreach ($group in $evidence.cases | Group-Object index) {
        if ($group.Count -ne 4 -or @($group.Group.envelope_sha256 | Select-Object -Unique).Count -ne 1 -or
            @($group.Group.accepted | Select-Object -Unique).Count -ne 1) { throw 'Producer or classification differs by backend' }
        if ($group.Group[0].accepted -and @($group.Group.ir_sha256 | Select-Object -Unique).Count -ne 1) {
            throw 'Emitted IR differs by backend'
        }
    }
    if ((Digest $hostPath) -ne $runtimeHash -or (Digest $compiler) -ne $compilerHash) { throw 'Runtime/compiler changed during regression' }
    foreach ($source in $evidence.sources) {
        if ((Digest (Join-Path $repo $source.path)) -ne $source.sha256) { throw 'Regression source changed during execution' }
    }
    foreach ($source in $evidence.compiler_sources) {
        if ((Digest (Join-Path $repo $source.path)) -ne $source.sha256 -or
            (Digest (Join-Path $Proof $source.path)) -ne $source.sha256) { throw 'Compiler source changed during regression' }
    }
    if ((Digest $manifestFile) -ne $evidence.bootstrap_manifest_sha256) { throw 'Bootstrap manifest changed during regression' }
    $evidence['status'] = 'passed'
    Write-Host "Scalar regression passed $($sources.Count) authored cases across all four VM crossings"
} catch {
    $evidence['status'] = 'failed'
    $evidence['error'] = $_.Exception.Message
    throw
} finally {
    $timer.Stop()
    $evidence['elapsed_seconds'] = $timer.Elapsed.TotalSeconds
    $evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding utf8
}
