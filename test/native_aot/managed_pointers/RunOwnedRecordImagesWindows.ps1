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
$work = (Join-Path ([IO.Path]::GetTempPath()) ('chibi-owned-record-images-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
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
    if ($status -ne $Expected) { throw "$Label status $status differs from $Expected" }
}

try {
    # Eight image cases only. Semantic, assertion and counted-copy source
    # audits and the source rejection remain separate Windows acceptance work.
    foreach ($name in 'OwnedRecordImages', 'OwnedRecordImagesHost') {
        Invoke-Checked $compilerPath @('--compiler', "$root/tools/compiler", '--core', "$root/packages/core",
            '--package', '.', '--source', "$fixtures/$name.chibi", '--entry', 'Main', '--output', "$work/$name.exe",
            '--llvm', $llvmPath, '--target', 'x86_64-pc-windows-msvc', '--jobs', '1') "compile $name"
    }
    foreach ($mode in 'borrowed', 'owned') {
        $directory = "$work/$mode"
        New-Item -ItemType Directory -Path $directory | Out-Null
        if ($mode -eq 'borrowed') {
            $text = [IO.File]::ReadAllText("$fixtures/text_cells/Record.chibi")
            $marker = '// The image runner extracts the prefix above this marker.'
            if (($text.Split(@($marker), [StringSplitOptions]::None).Count - 1) -ne 1) { throw 'writer source boundary changed' }
            [IO.File]::WriteAllText("$directory/writer.chibi", $text.Substring(0, $text.IndexOf($marker)), $utf8)
        } else {
            Copy-Item "$fixtures/text_cells/RecordOwned.chibi" "$directory/writer.chibi"
        }
        Invoke-Checked "$work/OwnedRecordImages.exe" @($directory, $llvmPath, $mode, "$fixtures/text_cells/Record.chibi",
            "$directory/writer.chibi", "$fixtures/OwnedRecordOld.ll", "$fixtures/BoundaryTextAllocation.ll",
            "$root/packages/core") "build $mode images"
        foreach ($level in 0, 2) {
            Invoke-Checked "$work/OwnedRecordImagesHost.exe" @("$directory/owned-record-old-O$level.dll",
                "$directory/owned-record-writer-O$level.dll", $mode) "$mode O$level"
            if (@($script:reported | Where-Object { $_ -ne '' }).Count -ne 0) { throw 'unexpected image-host diagnostic' }
            $passed += 1
            "PASS owned record $mode O$level all Text owners and exact image leases"
            Invoke-Checked "$work/OwnedRecordImagesHost.exe" @("$directory/owned-record-old-O$level.dll",
                "$directory/owned-record-broken-O$level.dll", $mode) "$mode one-site bypass O$level" 33
            $lines = @($script:reported | Where-Object { $_ -ne '' })
            if ($lines.Count -ne 1 -or $lines[0] -ne 'owned record Text owner missing before unload') {
                throw 'missing-import control did not clean up safely before either unload'
            }
            $passed += 1
            "PASS owned record $mode one-site bypass O$level cleanup before either unload"
        }
    }
    if ($passed -ne 8) { throw "Windows owned-record image inventory differs: $passed, expected 8" }
    'PASS Windows owned-record images exact 8 cases'
    $completed = $true
} finally {
    if ($completed) { Remove-Item -Recurse -Force $work }
    else { Write-Host "Windows owned-record image evidence retained in $work" }
}
# Expected negative controls must not become the surrounding CI step status.
exit 0
