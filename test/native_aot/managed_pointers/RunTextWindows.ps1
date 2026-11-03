param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$Llvm,
    [string]$Root = (Join-Path $PSScriptRoot '../../..')
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = [IO.Path]::GetFullPath($Root).Replace('\', '/')
$compilerPath = [IO.Path]::GetFullPath($Compiler).Replace('\', '/')
$llvmPath = [IO.Path]::GetFullPath($Llvm).Replace('\', '/')
$fixtures = "$root/test/native_aot/managed_pointers"
$work = (Join-Path ([IO.Path]::GetTempPath()) ('chibi-text-import-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
New-Item -ItemType Directory -Path $work | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$completed = $false
$passed = 0
$invocation = 0

function Invoke-Checked {
    param([string]$File, [string[]]$Arguments, [string]$Label, [int]$Expected = 0)
    $script:invocation += 1
    $script:reported = @(& $File @Arguments 2>&1 | ForEach-Object { "$_" })
    $status = $LASTEXITCODE
    [IO.File]::WriteAllLines("$work/command-$script:invocation.log", $script:reported, $utf8)
    $script:reported | Write-Host
    if ($status -ne $Expected) { throw "$Label status$status differs from $Expected" }
}
function Invoke-Audit {
    param([string]$Directory, [string]$Audit, [string]$Module, [bool]$Callbacks = $false)
    New-Item -ItemType Directory -Path $Directory | Out-Null
    $arguments = @($Directory, $llvmPath, $Audit, $Module)
    if ($Callbacks) { $arguments = @('--callbacks') + $arguments }
    Invoke-Checked "$work/SourceProbe.exe" $arguments "source audit $Module"
    $passes = @($script:reported | Where-Object { $_ -match '^PASS managed source .+ force=(false|true) O[02] poisoned balance$' })
    $balances = @($script:reported | Where-Object { $_ -match '^managed source live 0 peak [0-9]+ total [0-9]+$' })
    $unexpected = @($script:reported | Where-Object {
        $_ -ne '' -and $_ -notmatch '^PASS managed source .+ force=(false|true) O[02] poisoned balance$' -and
        $_ -notmatch '^managed source live 0 peak [0-9]+ total [0-9]+$'
    })
    if ($passes.Count -ne 4 -or $balances.Count -ne 4 -or $unexpected.Count -ne 0) {
        throw "source audit $Module did not report exactly four clean O0/O2 normal/forced executions"
    }
    $script:passed += 4
}

try {
    # Same exact 68-case inventory as text-import.sh, never a broad task suite.
    foreach ($name in 'SourceProbe', 'BoundaryTextImages', 'BoundaryTextImagesHost') {
        Invoke-Checked $compilerPath @('--compiler', "$root/tools/compiler", '--core', "$root/packages/core",
            '--package', '.', '--source', "$fixtures/$name.chibi", '--entry', 'Main', '--output', "$work/$name.exe",
            '--llvm', $llvmPath, '--target', 'x86_64-pc-windows-msvc', '--jobs', '1') "compile $name"
    }
    foreach ($shape in 'Labels', 'Leaf') {
        $directory = "$work/$shape"
        New-Item -ItemType Directory -Path "$directory/writer" | Out-Null
        $source = "$fixtures/text_import/$shape.chibi"
        $text = [IO.File]::ReadAllText($source)
        $marker = '// The image runner extracts the prefix above this marker.'
        if (($text.Split(@($marker), [StringSplitOptions]::None).Count - 1) -ne 1) { throw 'writer source boundary changed' }
        [IO.File]::WriteAllText("$directory/writer/Main.chibi", $text.Substring(0, $text.IndexOf($marker)), $utf8)
        Invoke-Checked $compilerPath @('--compiler', "$root/tools/compiler", '--core', "$root/packages/core",
            '--source', $source, '--entry', 'Main', '--emit-llvm', '--target', 'x86_64-pc-windows-msvc',
            '--output', "$directory/$shape.ll") "emit $shape"
        Invoke-Checked "$work/BoundaryTextImages.exe" @($directory, $llvmPath, $shape, $source,
            "$directory/writer/Main.chibi", "$fixtures/BoundaryTextOld.ll", "$fixtures/BoundaryTextAudit.ll",
            "$fixtures/BoundaryTextAllocation.ll", "$root/packages/core", "$fixtures/BoundaryTextLabels.ll") "build $shape images"
        $boundaries = @('view', 'replace', 'constructor', 'scoped_set', 'scoped_append', 'scoped_extend')
        if ($shape -eq 'Labels') {
            $boundaries += @('scoped_field', 'scoped_field_append', 'scoped_field_extend', 'scoped_text_set', 'scoped_rhs', 'aliases')
        }
        foreach ($level in 0, 2) {
            foreach ($boundary in $boundaries) {
                Invoke-Checked "$work/BoundaryTextImagesHost.exe" @("$directory/boundary-old-O$level.so",
                    "$directory/boundary-writer-O$level.so", $shape, $boundary) "$shape $boundary O$level"
                if (@($script:reported | Where-Object { $_ -ne '' }).Count -ne 0) { throw 'unexpected image-host diagnostic' }
                $passed += 1
                "PASS boundary Text $shape $boundary O$level all owners before writer unload"
            }
            Invoke-Checked "$work/BoundaryTextImagesHost.exe" @("$directory/boundary-old-O$level.so",
                "$directory/boundary-broken-O$level.so", $shape, 'view') "$shape one-site bypass O$level" 33
            $lines = @($script:reported | Where-Object { $_ -ne '' })
            if ($lines.Count -ne 1 -or $lines[0] -ne 'boundary Text owner missing before unload') {
                throw 'missing-import control did not fail safely before unload'
            }
            $passed += 1
            "PASS boundary Text $shape one-site bypass O$level fails safely before unload"
        }
        Invoke-Audit "$directory/normal" "$fixtures/SourceAudit.ll" "$directory/$shape.ll"
        Invoke-Audit "$directory/callbacks" "$fixtures/SourceAudit.ll" "$directory/$shape.ll" $true
    }
    Invoke-Checked $compilerPath @('--compiler', "$root/tools/compiler", '--core', "$root/packages/core",
        '--source', "$fixtures/text_import/Copies.chibi", '--entry', 'Main', '--emit-llvm', '--target', 'x86_64-pc-windows-msvc',
        '--output', "$work/Copies.ll") 'emit Copies'
    $audit = [IO.File]::ReadAllText("$fixtures/SourceAudit.ll").Replace("`r`n", "`n")
    $bound = '%bounded = icmp ule i64 %peak, 2048'
    if (($audit.Split(@($bound), [StringSplitOptions]::None).Count - 1) -ne 1) { throw 'allocation audit bound changed' }
    [IO.File]::WriteAllText("$work/copy-audit.ll", $audit.Replace($bound, '%bounded = icmp ule i64 %total, 64'), $utf8)
    Invoke-Audit "$work/copies" "$work/copy-audit.ll" "$work/Copies.ll"
    'PASS boundary Text ordinary counted copies stay within 64 allocations'

    # Same pre-allocation trap as POSIX: allocation 513 is never performed.
    # DAG and Deep each have 15 backed layers, the unchanged metadata limit.
    $needle = "  store i64 %allocated, ptr @managed_source.total`n"
    if (($audit.Split(@($needle), [StringSplitOptions]::None).Count - 1) -ne 1) { throw 'allocation audit entry changed' }
    $budget = @'
  %within.budget = icmp ule i64 %allocated, 512
  br i1 %within.budget, label %allocate, label %excessive
excessive:
  call void @llvm.trap()
  unreachable
allocate:
'@
    $graph = $audit.Replace($needle, $needle + $budget + "`n").Replace($bound, '%bounded = icmp ule i64 %total, 512')
    [IO.File]::WriteAllText("$work/graph-audit.ll", $graph, $utf8)
    foreach ($entry in 'DAG', 'Deep') {
        Invoke-Checked $compilerPath @('--compiler', "$root/tools/compiler", '--core', "$root/packages/core",
            '--source', "$fixtures/text_import/Graphs.chibi", '--entry', $entry, '--emit-llvm', '--target', 'x86_64-pc-windows-msvc',
            '--output', "$work/$entry.ll") "emit $entry"
        Invoke-Audit "$work/$entry" "$work/graph-audit.ll" "$work/$entry.ll"
        "PASS boundary Text $entry bounded to 512 allocations"
    }
    if ($passed -ne 68) { throw "Windows Text importer inventory differs: $passed, expected68" }
    'PASS Windows Text importer exact68 cases'
    $completed = $true
} finally {
    if ($completed) { Remove-Item -Recurse -Force $work }
    else { Write-Host "Windows Text importer evidence retained in $work" }
}
# Expected negative controls must not become the surrounding CI step status.
exit 0
