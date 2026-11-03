$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$producer = '72598f131af1e008db34da1270ed1f35c26c0d17'
$run = '41589954388'
$artifactId = '13216372581'
$archiveHash = '01f7718b0eb7a5689ce87b213602e610b8cb519054dc49ca3592ac3c4d09c04d'
$root = $PWD.Path
$work = Join-Path $env:RUNNER_TEMP ('chibi-facade-replay-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
Write-Host "Diagnostic evidence: $work"

# The step process owns this anonymous job for its whole lifetime. Windows
# closes its raw handle on process exit, killing descendants even when their
# original consumer leader has already exited. The Actions runner is outside it.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class ReplayJob {
    private static IntPtr job;
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr CreateJobObjectW(IntPtr security, string name);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool SetInformationJobObject(IntPtr handle, int kind, IntPtr data, uint size);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool AssignProcessToJobObject(IntPtr handle, IntPtr process);
    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    public static void Own() {
        if (IntPtr.Size != 8) throw new InvalidOperationException("The replay requires Win64");
        IntPtr handle = CreateJobObjectW(IntPtr.Zero, null);
        if (handle == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        IntPtr limits = Marshal.AllocHGlobal(144);
        try {
            for (int at=0; at<144; at+=4) Marshal.WriteInt32(limits, at, 0);
            Marshal.WriteInt32(limits, 16, 0x2000); // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if (!SetInformationJobObject(handle, 9, limits, 144) ||
                !AssignProcessToJobObject(handle, GetCurrentProcess()))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            job = handle;
            handle = IntPtr.Zero;
        } finally {
            Marshal.FreeHGlobal(limits);
            if (handle != IntPtr.Zero) CloseHandle(handle);
        }
        GC.KeepAlive(job);
    }
}
'@
[ReplayJob]::Own()

function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Check-Hash([string]$Path, [string]$Expected) {
    if ((Hash $Path) -ne $Expected) { throw "Checksum mismatch: $Path" }
}
$headers = @{ Authorization = "Bearer $env:GH_TOKEN"; Accept = 'application/vnd.github+json' }
$api = 'https://api.github.com/repos/waldnercharles/chibi'
$metadata = Invoke-RestMethod -Headers $headers -Uri "$api/actions/artifacts/$artifactId"
$origin = Invoke-RestMethod -Headers $headers -Uri "$api/actions/runs/$run"
if ($metadata.name -ne 'native-windows-asset-facade-failure' -or $metadata.expired -or
    $metadata.workflow_run.id.ToString() -ne $run -or $metadata.workflow_run.head_sha -ne $producer -or
    $origin.head_sha -ne $producer -or $origin.path -ne '.github/workflows/ci.yml' -or
    $origin.repository.full_name -ne 'waldnercharles/chibi' -or $origin.status -ne 'completed') {
    throw 'The retained facade artifact identity does not match the frozen producer'
}
$zip = Join-Path $work 'original.zip'
Invoke-WebRequest -Headers $headers -Uri "$api/actions/artifacts/$artifactId/zip" -OutFile $zip
Check-Hash $zip $archiveHash
$unpacked = Join-Path $work 'original'
Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
$folders = @(Get-ChildItem -LiteralPath $unpacked -Directory)
if ($folders.Count -ne 1) { throw 'Expected exactly one retained facade workspace' }
$saved = $folders[0].FullName
$sdk = Join-Path $saved 'relocated SDK Ω'
$library = Join-Path $saved 'chibi_asset_compiler_host.lib'
$consumer = Join-Path $sdk 'bin/asset-consumer.exe'
$asset = Join-Path $sdk 'libexec/chibi/chibi-asset-compiler.exe'
Check-Hash $library 'fc51ec6c5ad26be44e429a0082f8974606ee9a8aad5d699b674f9ad5990bfb76'
Check-Hash $consumer '0f32b0f1df6fa877247600507e447df4068522d49d58765a842f212d6c0cb012'
Check-Hash $asset '5810203a681923b2087b8ef5e89f6bd4418ecd84a81fa1c2b817810d451e0d2e'
Check-Hash (Join-Path $saved 'simple.chibip') '08b7ec6ca8d209820e44ddbda3151b5f8b866334fd45cc7f06124794279f3dc9'
& git fetch --no-tags --depth=1 origin $producer
if ($LASTEXITCODE) { throw 'Cannot fetch the exact original consumer source' }
$fetched = & git rev-parse FETCH_HEAD
if ($LASTEXITCODE -or $fetched -ne $producer) { throw 'The fetched producer source identity differs' }
$source = & git show "${producer}:test/native_aot/asset_compiler/Bridge.c"
if ($LASTEXITCODE) { throw 'Cannot read the exact original C consumer source' }
$text = $source -join "`n"
$marker = '    options.program = NULL;'
if (($text.Split(@($marker), [StringSplitOptions]::None)).Count -ne 2) { throw 'The original discovery gate changed' }
$instrumented = "void sdk_probe_enable(void);`n" + $text.Replace($marker, "    sdk_probe_enable();`n$marker") + "`n"
$bridge = Join-Path $work 'Bridge.c'
[IO.File]::WriteAllText($bridge, $instrumented, [Text.UTF8Encoding]::new($false))

$clang = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
if ($clang) { $clang = $clang.Source }
else { $clang = Join-Path $env:ProgramFiles 'LLVM/bin/clang-cl.exe' }
if (!(Test-Path -LiteralPath $clang -PathType Leaf)) { throw 'The C consumer regression requires clang-cl.exe' }
$llvm = Split-Path $clang
$ar = Join-Path $llvm 'llvm-ar.exe'
$objcopy = Join-Path $llvm 'llvm-objcopy.exe'
if (!(Test-Path -LiteralPath $ar) -or !(Test-Path -LiteralPath $objcopy)) { throw 'The LLVM COFF inspection tools are missing' }
$members = @(& $ar t $library)
if ($LASTEXITCODE -or $members.Count -ne 1) { throw 'Expected one authored archive object' }
$object = Join-Path $work 'compiler.obj'
$extract = [Diagnostics.ProcessStartInfo]::new()
$extract.FileName = $ar
$extract.UseShellExecute = $false
$extract.RedirectStandardOutput = $true
foreach ($arg in @('p', $library)) { $extract.ArgumentList.Add($arg) }
$process = [Diagnostics.Process]::Start($extract)
try {
    $stream = [IO.File]::Create($object)
    try { $process.StandardOutput.BaseStream.CopyTo($stream) } finally { $stream.Dispose() }
    $process.WaitForExit()
    if ($process.ExitCode) { throw 'Cannot extract the retained COFF object' }
} finally { $process.Dispose() }
$renames = @(
    '--redefine-sym=__imp_GetModuleFileNameW=probe_imp_GetModuleFileNameW',
    '--redefine-sym=CreateFileW=probe_CreateFileW',
    '--redefine-sym=GetFinalPathNameByHandleW=probe_GetFinalPathNameByHandleW',
    '--redefine-sym=GetFileAttributesW=probe_GetFileAttributesW',
    '--redefine-sym=WideCharToMultiByte=probe_WideCharToMultiByte',
    '--redefine-sym=CompareStringOrdinal=probe_CompareStringOrdinal'
)
& $objcopy @renames $object
if ($LASTEXITCODE) { throw 'Cannot redirect the test-only Win32 boundary symbols' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -or !$installation) { throw 'The Visual C SDK is unavailable' }
$devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$environment = & $env:ComSpec /d /s /c "call `"$devcmd`" -no_logo -arch=amd64 -host_arch=amd64 >nul && set"
if ($LASTEXITCODE) { throw 'Cannot prepare the test C compiler environment' }
$output = Join-Path $sdk 'bin/probed-consumer.exe'
$compile = [Diagnostics.ProcessStartInfo]::new()
$compile.FileName = $clang
$compile.UseShellExecute = $false
$compile.WorkingDirectory = $work
$compile.Environment.Clear()
foreach ($line in $environment) {
    $equals = $line.IndexOf('=')
    if ($equals -gt 0) { $compile.Environment[$line.Substring(0, $equals)] = $line.Substring($equals + 1) }
}
foreach ($arg in @('/nologo', '/TC', '/std:c11', '/MT', '/W4', '/WX', '/D_CRT_SECURE_NO_WARNINGS',
    '/D_CRT_NONSTDC_NO_DEPRECATE', "/I$root/src", "/Fe$output", $bridge,
    "$root/.github/windows-facade-replay.c", $object, '/link', '/MANIFEST:EMBED',
    "/MANIFESTINPUT:$root/src/host/windows/chibi.manifest")) { $compile.ArgumentList.Add($arg) }
$process = [Diagnostics.Process]::Start($compile)
try { $process.WaitForExit(); if ($process.ExitCode) { throw 'The instrumented C consumer did not build' } }
finally { $process.Dispose() }

function Save-Receipt($Results) {
    @{ producer = $producer; producer_run = $run; producer_artifact = $artifactId; artifact_sha256 = $archiveHash;
       diagnostic_commit = (& git rev-parse HEAD); original_source_git_blob = (& git rev-parse "${producer}:test/native_aot/asset_compiler/Bridge.c");
       wrapper_sha256 = (Hash "$root/.github/windows-facade-replay.c"); rewritten_object_sha256 = (Hash $object); results = $Results;
       scope = 'Diagnostic replay only; individual exit statuses retain all original null-program assertions. No product acceptance or native seed publication.'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$work/receipt.json" -Encoding utf8
}
$results = @()
Save-Receipt $results
foreach ($name in @('original', 'probed')) {
    foreach ($extended in @($false, $true)) {
        $binary = $consumer
        if ($name -eq 'probed') { $binary = $output }
        $spelling = $binary
        if ($extended) { $spelling = '\\?\' + $binary.Replace('/', '\') }
        $label = "$name-$extended"
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $spelling
        $start.UseShellExecute = $false
        $start.WorkingDirectory = $root
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($key in @($start.Environment.Keys)) {
            if ($key -match 'TOKEN|SECRET|PASSWORD|CREDENTIAL|AUTHORIZATION') { $null = $start.Environment.Remove($key) }
        }
        $start.Environment['PATH'] = ''
        $start.Environment['CC'] = 'Z:/no-c-compiler.exe'
        $start.Environment['CXX'] = 'Z:/no-cxx-compiler.exe'
        $null = $start.Environment.Remove('CHIBI_ASSET_COMPILER')
        $temp = Join-Path $work "temp-$label"
        $cache = Join-Path $work "cache-$label"
        New-Item -ItemType Directory -Path $temp, $cache | Out-Null
        $start.Environment['TEMP'] = $temp
        $start.Environment['TMP'] = $temp
        foreach ($arg in @($asset, "$saved/simple.json", "$saved/source-error.json", $cache,
            "$saved/crash.exe", "$saved/simple.chibip")) { $start.ArgumentList.Add($arg.Replace('\', '/')) }
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $start
        $began = $false
        $timer = [Diagnostics.Stopwatch]::StartNew()
        try {
            $began = $process.Start()
            if (!$began) { throw "Cannot launch $label" }
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
            # Reserve five seconds for cleanup inside the same 120-second
            # deadline. A completed leader does not imply that pipes are closed.
            $remaining = [Math]::Max(0, 115000 - [int]$timer.ElapsedMilliseconds)
            if (!$process.WaitForExit($remaining)) {
                $results += @{ name = $name; extended = $extended; error = 'leader deadline'; seconds = $timer.Elapsed.TotalSeconds }
                Save-Receipt $results
                throw "The owned $label consumer exceeded its diagnostic deadline"
            }
            $status = $process.ExitCode
            $draining = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($stdout, $stderr))
            $remaining = [Math]::Max(0, 115000 - [int]$timer.ElapsedMilliseconds)
            if (!$draining.Wait($remaining)) {
                $results += @{ name = $name; extended = $extended; status = $status; error = 'stream deadline'; seconds = $timer.Elapsed.TotalSeconds }
                Save-Receipt $results
                throw "The owned $label descendants did not close their output streams"
            }
            $stdoutText = $stdout.GetAwaiter().GetResult()
            $stderrText = $stderr.GetAwaiter().GetResult()
            [IO.File]::WriteAllText("$work/$label.stdout", $stdoutText)
            [IO.File]::WriteAllText("$work/$label.stderr", $stderrText)
            Write-Host "REPLAY $label exit=$status elapsed=$($timer.Elapsed.TotalSeconds)"
            Write-Host $stdoutText
            Write-Host $stderrText
            $results += @{ name = $name; extended = $extended; status = $status; seconds = $timer.Elapsed.TotalSeconds;
                executable_sha256 = (Hash $binary); stdout_sha256 = (Hash "$work/$label.stdout"); stderr_sha256 = (Hash "$work/$label.stderr") }
            Save-Receipt $results
        } finally {
            if ($began -and !$process.HasExited) {
                $process.Kill($true)
                $remaining = [Math]::Max(0, 120000 - [int]$timer.ElapsedMilliseconds)
                $null = $process.WaitForExit($remaining)
            }
            $process.Dispose()
        }
    }
}
Write-Host 'Diagnostic replay complete. Per-consumer results are recorded independently of this script status.'
exit 0
