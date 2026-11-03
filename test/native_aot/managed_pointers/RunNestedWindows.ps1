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
$work = (Join-Path ([IO.Path]::GetTempPath()) ('chibi-nested-pointers-' + [Guid]::NewGuid().ToString('N'))).Replace('\', '/')
New-Item -ItemType Directory -Path $work | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$completed = $false
try {
    & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" --package . `
        --source "$fixtures/SourceProbe.chibi" --entry Main --output "$work/SourceProbe.exe" `
        --llvm $llvmPath --target x86_64-pc-windows-msvc --jobs 1
    if ($LASTEXITCODE) { throw 'could not compile the Windows source audit' }
    $cases = @(
        @{ name = 'Cells'; source = 'Cells'; entry = 'Main' },
        @{ name = 'ScopedAliases'; source = 'ScopedAliases'; entry = 'Main' },
        @{ name = 'NestedPointers'; source = 'NestedPointers'; entry = 'Main' },
        @{ name = 'NestedPointerFailure'; source = 'NestedPointers'; entry = 'Failure' }
    )
    foreach ($case in $cases) {
        & $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$fixtures/source/$($case.source).chibi" --entry $case.entry --emit-llvm `
            --target x86_64-pc-windows-msvc --output "$work/$($case.name).ll"
        if ($LASTEXITCODE) { throw "could not emit $($case.name)" }
    }
    # Same narrow inventory admitted on Linux: 24 audited executions. Failure
    # cases still require the precise AssertionFailed category and status134.
    $audits = @(
        @{ name = 'normal'; flags = @(); modules = @('Cells', 'ScopedAliases', 'NestedPointers'); count = 12 },
        @{ name = 'assertion'; flags = @('--assertion'); modules = @('NestedPointerFailure'); count = 4 },
        @{ name = 'callbacks'; flags = @('--callbacks'); modules = @('NestedPointers'); count = 4 },
        @{ name = 'callback-assertion'; flags = @('--callbacks', '--assertion'); modules = @('NestedPointerFailure'); count = 4 }
    )
    foreach ($audit in $audits) {
        $directory = "$work/$($audit.name)"
        New-Item -ItemType Directory -Path $directory | Out-Null
        $arguments = @($audit.flags) + @($directory, $llvmPath, "$fixtures/SourceAudit.ll")
        $arguments += @($audit.modules | ForEach-Object { "$work/$_.ll" })
        $reported = @(& "$work/SourceProbe.exe" @arguments 2>&1 | ForEach-Object { "$_" })
        $status = $LASTEXITCODE
        [IO.File]::WriteAllLines("$directory/runner.log", $reported, $utf8)
        $reported | Write-Host
        $passes = @($reported | Where-Object { $_ -match '^PASS managed source .+ force=(false|true) O[02] poisoned balance$' }).Count
        $balanced = @($reported | Where-Object { $_ -match '^managed source live 0 peak [0-9]+ total [0-9]+$' }).Count
        if ($status -ne 0 -or $passes -ne $audit.count -or $balanced -ne $audit.count) {
            throw "$($audit.name) differs from its exact audited inventory"
        }
    }
    foreach ($name in 'PointerArraySchema', 'NestedPointerSchema') {
        $output = "$work/rejected-$name.ll"
        [IO.File]::WriteAllText($output, 'preserved', $utf8)
        $reported = @(& $compilerPath --compiler "$root/tools/compiler" --core "$root/packages/core" `
            --source "$fixtures/source/rejected/$name.chibi" --entry Main --emit-llvm `
            --target x86_64-pc-windows-msvc --output $output 2>&1 | ForEach-Object { "$_" })
        $status = $LASTEXITCODE
        [IO.File]::WriteAllLines("$work/rejected-$name.log", $reported, $utf8)
        $reported | Write-Host
        if ($status -ne 1 -or ($reported -join "`n") -notmatch 'native: rejected Keep:' -or
            ($reported -join "`n") -notmatch 'scalar proof requires number, bool, string, record or union parameters and one number, bool, string, record or union result' -or [IO.File]::ReadAllText($output) -ne 'preserved') {
            throw "$name did not preserve the schema rejection and prior output"
        }
        "PASS Windows rejected $name preserves prior output"
    }
    'PASS Windows nested pointers exact24 source audits and two schema rejections'
    $completed = $true
} finally {
    if ($completed) { Remove-Item -Recurse -Force $work }
    else { Write-Host "Windows nested-pointer evidence retained in $work" }
}
# Expected negative controls must not become the surrounding CI step status.
exit 0
