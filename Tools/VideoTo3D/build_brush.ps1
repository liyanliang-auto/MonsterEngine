#Requires -Version 7.0
# Build the immutable upstream baseline. Local paths belong in the caller.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$CargoPath,
    [Parameter(Mandatory)][string]$GitPath,
    [Parameter(Mandatory)][string]$LinkerPath,
    [Parameter(Mandatory)][string]$CargoHome,
    [Parameter(Mandatory)][string]$RustupHome,
    [Parameter(Mandatory)][string]$TargetDirectory,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [string]$NvidiaSmiPath,
    [string]$LinkerObservationPath,
    [string]$Toolchain = '1.89.0',
    # Only for an already completed invocation with retained command/log evidence.
    [switch]$RecordExistingBuild,
    [string]$ExistingBuildLog,
    [string]$ExistingBuildExitCodePath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$pinnedRevision = '3edecbb2fe79d3e2c87eeab85b15e0b1dd10d486'

function Get-AbsolutePath([string]$Value) {
    if (-not [IO.Path]::IsPathFullyQualified($Value)) { throw "Expected absolute path: $Value" }
    return [IO.Path]::GetFullPath($Value)
}
function Get-FileIdentity([string]$Value) {
    $item = Get-Item -LiteralPath $Value -ErrorAction Stop
    if ($item.PSIsContainer) { throw "Expected file: $Value" }
    return [ordered]@{ path = $item.FullName; bytes = $item.Length;
        sha256 = (Get-FileHash -LiteralPath $Value -Algorithm SHA256).Hash.ToLowerInvariant();
        fileVersion = $item.VersionInfo.FileVersion }
}
function Invoke-Recorded([string]$Program, [string[]]$Arguments, [string]$Log) {
    & $Program @Arguments *> $Log
    $code = $LASTEXITCODE
    return [ordered]@{ program = $Program; arguments = $Arguments; exitCode = $code;
        log = Get-FileIdentity $Log }
}
function Get-GitText([string[]]$Arguments) {
    $value = & $GitPath -C $Source @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Git failed: $Arguments ($value)" }
    return ($value -join "`n").Trim()
}

$Source = Get-AbsolutePath $Source
$CargoPath = Get-AbsolutePath $CargoPath
$GitPath = Get-AbsolutePath $GitPath
$LinkerPath = Get-AbsolutePath $LinkerPath
$CargoHome = Get-AbsolutePath $CargoHome
$RustupHome = Get-AbsolutePath $RustupHome
$TargetDirectory = Get-AbsolutePath $TargetDirectory
$EvidenceDirectory = Get-AbsolutePath $EvidenceDirectory
$null = New-Item -ItemType Directory -Path $EvidenceDirectory -Force
$reportPath = Join-Path $EvidenceDirectory 'brush-build-identity.json'
# Never overwrite prior evidence or use a stale executable as success.
if (Test-Path -LiteralPath $reportPath) { throw "Evidence already exists: $reportPath" }
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')
$logDirectory = Join-Path $EvidenceDirectory "build-record-$stamp"
$null = New-Item -ItemType Directory -Path $logDirectory
$report = [ordered]@{ schemaVersion = 1; recordedAtUtc = [DateTime]::UtcNow.ToString('o');
    status = 'FAILED'; trainingStatus = 'NOT_VERIFIED'; selectedTrainingDevice = $null;
    source = $Source; expectedRevision = $pinnedRevision; toolchain = $Toolchain;
    executable = $null; failure = $null }
$savedEnvironment = @{}
$environmentKeys = @('CARGO_HOME','RUSTUP_HOME','CARGO_TARGET_DIR','RUSTUP_TOOLCHAIN','PATH')
foreach ($key in $environmentKeys) { $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
$oldLocation = Get-Location
try {
    $report.cargoLauncher = Get-FileIdentity $CargoPath
    $report.git = Get-FileIdentity $GitPath
    $report.linker = Get-FileIdentity $LinkerPath
    $report.linkerSelection = 'Supplied PATH candidate; selected linker invocation not traced by Cargo default verbosity'
    $sdkKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Microsoft SDKs\Windows\v10.0'
    $report.windowsSdkInstallation = if (Test-Path -LiteralPath $sdkKey) {
        Get-ItemProperty -LiteralPath $sdkKey -ErrorAction Stop |
            Select-Object InstallationFolder, ProductVersion, ProductName
    } else { $null }
    $revision = Get-GitText @('rev-parse','HEAD')
    $origin = Get-GitText @('remote','get-url','origin')
    $dirtyBefore = Get-GitText @('status','--porcelain=v1','--untracked-files=all')
    $report.sourceIdentity = [ordered]@{ revision = $revision; origin = $origin; dirtyBefore = $dirtyBefore }
    if ($revision -ne $pinnedRevision) { throw 'Source revision is not the fixed upstream baseline' }
    if ($origin -notmatch '^https://github\.com/ArthurBrussee/brush(?:\.git)?/?$') { throw 'Unexpected upstream origin' }
    if ($dirtyBefore) { throw 'Unmodified baseline requires a clean worktree, including untracked files' }
    $lockPath = Join-Path $Source 'Cargo.lock'
    $report.lockBefore = Get-FileIdentity $lockPath
    $report.workspaceManifest = Get-FileIdentity (Join-Path $Source 'Cargo.toml')
    $cargoConfig = Join-Path $Source '.cargo/config.toml'
    $report.cargoConfiguration = if (Test-Path -LiteralPath $cargoConfig) { Get-FileIdentity $cargoConfig } else { $null }
    $env:CARGO_HOME = $CargoHome
    $env:RUSTUP_HOME = $RustupHome
    $env:CARGO_TARGET_DIR = $TargetDirectory
    $env:RUSTUP_TOOLCHAIN = $Toolchain
    $env:PATH = "$(Split-Path -Parent $CargoPath);$(Split-Path -Parent $LinkerPath);$env:PATH"
    $report.environment = [ordered]@{ cargoHome = $CargoHome; rustupHome = $RustupHome;
        targetDirectory = $TargetDirectory; linkerDirectory = (Split-Path -Parent $LinkerPath);
        include = $env:INCLUDE; lib = $env:LIB; windowsSdkDir = $env:WindowsSdkDir;
        rustflags = $env:RUSTFLAGS; cargoBuildTarget = $env:CARGO_BUILD_TARGET;
        cargoEncodedRustflags = $env:CARGO_ENCODED_RUSTFLAGS }
    if ($env:RUSTFLAGS -or $env:CARGO_ENCODED_RUSTFLAGS -or $env:CARGO_BUILD_TARGET) {
        throw 'Baseline builder requires default Rust flags and native host target'
    }
    Set-Location -LiteralPath $Source
    $rustcPath = Join-Path (Split-Path -Parent $CargoPath) 'rustc.exe'
    $report.rustc = Invoke-Recorded $rustcPath @('-vV') (Join-Path $logDirectory 'rustc.log')
    $report.cargo = Invoke-Recorded $CargoPath @('-vV') (Join-Path $logDirectory 'cargo.log')
    if ($report.rustc.exitCode -ne 0 -or $report.cargo.exitCode -ne 0) { throw 'Rust version command failed' }
    $versionText = Get-Content -Raw -LiteralPath $report.rustc.log.path
    if ($versionText -notmatch '(?m)^host: x86_64-pc-windows-msvc\s*$' -or
        $versionText -notmatch "(?m)^release: $([regex]::Escape($Toolchain))\s*$") {
        throw 'Expected pinned Rust release on x86_64-pc-windows-msvc'
    }
    $report.rustcResolved = Invoke-Recorded (Join-Path (Split-Path -Parent $CargoPath) 'rustup.exe') @('which','rustc') (Join-Path $logDirectory 'rustc-path.log')
    if ($report.rustcResolved.exitCode -ne 0) { throw 'Cannot resolve actual rustc binary' }
    $report.rustcBinary = Get-FileIdentity ((Get-Content -Raw -LiteralPath $report.rustcResolved.log.path).Trim())
    $report.cargoResolved = Invoke-Recorded (Join-Path (Split-Path -Parent $CargoPath) 'rustup.exe') @('which','cargo') (Join-Path $logDirectory 'cargo-path.log')
    if ($report.cargoResolved.exitCode -ne 0) { throw 'Cannot resolve actual cargo binary' }
    $report.cargoBinary = Get-FileIdentity ((Get-Content -Raw -LiteralPath $report.cargoResolved.log.path).Trim())
    $report.configuration = [ordered]@{ profile = 'release'; binary = 'brush_app';
        features = 'default'; locked = $true; target = 'native host'; workingDirectory = $Source }
    $arguments = @('build','--locked','--release','--bin','brush_app')
    if ($RecordExistingBuild) {
        $ExistingBuildLog = Get-AbsolutePath $ExistingBuildLog
        $ExistingBuildExitCodePath = Get-AbsolutePath $ExistingBuildExitCodePath
        $exitText = (Get-Content -Raw -LiteralPath $ExistingBuildExitCodePath).Trim()
        if ($exitText -notmatch '^-?\d+$') { throw 'Invalid retained build exit code' }
        $report.build = [ordered]@{ program = $CargoPath; arguments = $arguments;
            exitCode = [int]$exitText; log = Get-FileIdentity $ExistingBuildLog;
            exitCodeEvidence = Get-FileIdentity $ExistingBuildExitCodePath;
            mode = 'record_existing'; provenance = 'Caller must retain the actual invocation and prebuild source audit' }
    } else {
        $report.build = Invoke-Recorded $CargoPath $arguments (Join-Path $logDirectory 'build.log')
        $report.build.mode = 'executed'
    }
    $report.lockAfter = Get-FileIdentity $lockPath
    $report.sourceIdentity.dirtyAfter = Get-GitText @('status','--porcelain=v1','--untracked-files=all')
    if ($report.lockAfter.sha256 -ne $report.lockBefore.sha256 -or $report.sourceIdentity.dirtyAfter) {
        throw 'Source or dependency lock changed during build'
    }
    if ($report.build.exitCode -ne 0) { throw "Cargo build failed: exit $($report.build.exitCode)" }
    $binaryPath = Join-Path $TargetDirectory 'release/brush_app.exe'
    $report.executable = Get-FileIdentity $binaryPath
    if ($LinkerObservationPath) {
        $LinkerObservationPath = Get-AbsolutePath $LinkerObservationPath
        $report.linkerObservations = Get-FileIdentity $LinkerObservationPath
    }
    $dumpbinPath = Join-Path (Split-Path -Parent $LinkerPath) 'dumpbin.exe'
    if (Test-Path -LiteralPath $dumpbinPath) {
        $report.nativeImportsTool = Get-FileIdentity $dumpbinPath
        $report.nativeImports = Invoke-Recorded $dumpbinPath @('/nologo','/dependents',$binaryPath) (Join-Path $logDirectory 'native-imports.log')
        if ($report.nativeImports.exitCode -ne 0) { throw 'Native import identity capture failed' }
    }
    # Capture lock-resolved dependency/features separately; never edit or regenerate the lock.
    $report.dependencies = Invoke-Recorded $CargoPath @('tree','--locked','--offline','-p','brush-app','--target','x86_64-pc-windows-msvc','--edges','normal,build,features') (Join-Path $logDirectory 'dependency-features.log')
    if ($report.dependencies.exitCode -ne 0) { throw 'Dependency feature identity capture failed; build alone is insufficient identity evidence' }
    if ($NvidiaSmiPath) {
        $NvidiaSmiPath = Get-AbsolutePath $NvidiaSmiPath
        $report.gpuProbeTool = Get-FileIdentity $NvidiaSmiPath
        $report.availableGpus = Invoke-Recorded $NvidiaSmiPath @('--query-gpu=name,uuid,pci.bus_id,driver_version,memory.total','--format=csv,noheader') (Join-Path $logDirectory 'gpu.log')
        if ($report.availableGpus.exitCode -ne 0) { throw 'GPU inventory failed' }
    }
    $report.status = 'BUILT_UNMODIFIED'
} catch {
    $report.failure = $_.Exception.Message
    Write-Warning $report.failure
} finally {
    Set-Location $oldLocation
    foreach ($key in $environmentKeys) { [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], 'Process') }
    $json = $report | ConvertTo-Json -Depth 20
    $temporary = "$reportPath.$stamp.tmp"
    [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary, $reportPath)
}
if ($report.status -ne 'BUILT_UNMODIFIED') { exit 1 }
Write-Output $reportPath
