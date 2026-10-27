# Fixture actions only; test/tools/test_policy.c owns the assertions. The driver
# prepends the actual Run.ps1 helpers and supplies a copy of its corpus loop.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$output = $args[0]
$evidence = [ordered]@{ batch_members = @(); cases = @() }
$source = Join-Path $output 'transport.json'
$text = ' [ [ {"text":"☃\\n\\\"[]","value":18446744073709551615} ], [true,1e-30], [] ] '
[IO.File]::WriteAllText($source, $text, $utf8)
$result = [pscustomobject]@{ Name = 'fixture'; Exit = 0; Stdout = $source; Stderr = '' }
$parts = @(Split-Batch $result 'member' 3)
Write-Batch (Join-Path $output 'joined.json') @($parts | ForEach-Object { $_.Stdout })
$evidence.batch_members | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $output 'members.json') -Encoding utf8
$singleton = Join-Path $output 'singleton.json'
Write-Batch $singleton @($parts[0].Stdout)
$singleResult = [pscustomobject]@{ Name = 'singleton'; Exit = 0; Stdout = $singleton; Stderr = '' }
$single = @(Split-Batch $singleResult 'single' 1)
Write-Host ("singleton=" + $single.Count)
Write-Host ("singleton-identical=" + ((Digest $single[0].Stdout) -eq (Digest $parts[0].Stdout)))
foreach ($count in @(0, 2, 9)) {
    try { $null = Split-Batch $result 'bad' $count; Write-Host "count-$count accepted" }
    catch { Write-Host "count-$count rejected" }
}
try { Write-Batch (Join-Path $output 'empty.json') @(); Write-Host 'empty accepted' }
catch { Write-Host 'empty rejected' }
try { Write-Batch (Join-Path $output 'nine.json') @($source, $source, $source, $source, $source, $source, $source, $source, $source); Write-Host 'nine accepted' }
catch { Write-Host 'nine rejected' }
foreach ($bad in @('[] trailing', '[[0],]', '{"not":"array"}')) {
    [IO.File]::WriteAllText($source, $bad, $utf8)
    try { $null = Split-Batch $result 'bad' 1; Write-Host 'json accepted' }
    catch { Write-Host 'json rejected' }
}
[IO.File]::WriteAllBytes($source, [byte[]]@(91, 34, 255, 34, 93))
try { $null = Split-Batch $result 'bad' 1; Write-Host 'utf8 accepted' }
catch { Write-Host 'utf8 rejected' }
$failure = [pscustomobject]@{ Name = '16-sljit-produce-batch'; Exit = 1; Stdout = $source; Stderr = 'compiler batch item 2: compiler argument count mismatch' }
try { $null = Split-Batch $failure 'bad' 3; Write-Host 'host accepted' }
catch { Write-Host $_.Exception.Message }

# A large selected count must not hide even one other-backend/fallback entry.
foreach ($backend in @('bytecode', 'sljit')) {
    $good = if ($backend -eq 'bytecode') { 'bytecode=99 sljit=0' } else { 'bytecode=0 sljit=99' }
    $null = Backend-Entries "compiler-host $good wasm=0 fallback=0 overflow=0" $backend 'counter-fixture'
    Write-Host "$backend counters accepted"
    foreach ($bad in @('bytecode=99 sljit=1 wasm=0 fallback=0 overflow=0',
                       "$good wasm=0 fallback=1 overflow=0",
                       "$good wasm=0 fallback=0 overflow=1",
                       "$good wasm=1 fallback=0 overflow=0",
                       'bytecode=0 sljit=0 wasm=0 fallback=0 overflow=0')) {
        try { $null = Backend-Entries "compiler-host $bad" $backend 'counter-fixture'; Write-Host 'bad counters accepted' }
        catch { Write-Host "$backend counters rejected" }
    }
}

# Simulate two negative authored results in different backend batches. The
# transport must report the first in original case/producer/consumer order,
# not whichever batch was evaluated first, and publish no later raw output.
$script:published = @()
function Invoke-Host([string]$name, [string]$artifact, [string]$entry,
                     [string]$request, [string]$backend, [string]$raw = '',
                     [switch]$Compile, [switch]$Batch) {
    $path = Join-Path $output ($name + '.stdout')
    if ($name.EndsWith('-produce-batch')) {
        $values = (Read-Node $request).AsArray()
        $results = [System.Text.Json.Nodes.JsonArray]::new()
        foreach ($value in $values) {
            $row = [System.Text.Json.Nodes.JsonNode]::Parse('{"name":"Scalar.chibi"}')
            $row['text'] = Copy-Node $value[2]
            $rows = [System.Text.Json.Nodes.JsonArray]::new()
            $rows.Add($row)
            $rows.Add([System.Text.Json.Nodes.JsonNode]::Parse('{}'))
            $item = [System.Text.Json.Nodes.JsonArray]::new()
            $item.Add($rows)
            $results.Add($item)
        }
        Write-Node $path $results
    } elseif ($name.EndsWith('-check-batch')) {
        $results = [System.Text.Json.Nodes.JsonArray]::new()
        for ($i = 0; $i -lt 8; $i++) {
            $value = if ($name -eq '0-bytecode-bytecode-check-batch' -and $i -eq 3) { -4 }
                     elseif ($name -eq '0-sljit-bytecode-check-batch' -and $i -eq 1) { -5 }
                     else { 1 }
            $results.Add([System.Text.Json.Nodes.JsonNode]::Parse("[$value]"))
        }
        Write-Node $path $results
    } else {
        $script:published += $name
        [IO.File]::WriteAllText($raw, 'IR', $utf8)
        [IO.File]::WriteAllText($path, '[]', $utf8)
    }
    return [pscustomobject]@{ Name = $name; Exit = 0; Stdout = $path; Stderr = '' }
}
$sources = [System.Text.Json.Nodes.JsonArray]::new()
for ($i = 0; $i -lt 9; $i++) { $sources.Add((String-Node "source-$i")) }
$manifest = [System.Text.Json.Nodes.JsonNode]::Parse('{"target":[0,0,0,0,0,0]}')
$compiler = 'compiler'
$regression = 'regression'
try { . (Join-Path $output 'loop.ps1'); Write-Host 'classification accepted' }
catch { Write-Host $_.Exception.Message }
Write-Host ("published=" + ($published -join ','))
Write-Host ("crossings=" + $evidence.cases.Count)
Write-Host ("next-batch=" + (Test-Path -LiteralPath (Join-Path $output 'batch-8.json')))
