#Requires -Version 7.0
<##
Run formal Windows CTest with identity-checked production shaders. This does not
build targets: callers must first produce matching binaries in BuildDirectory.
OutputDirectory must be new; historical evidence is never overwritten.
##>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$ShaderDirectory,
    [string]$Configuration = 'Release',
    [string]$CTestPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$environmentName = 'MONSTER_SPLAT_SHADER_DIR'
$inherited = [Environment]::GetEnvironmentVariable($environmentName, 'Process')
$hadInherited = Test-Path "Env:$environmentName"
$reportStream = $null
$stdoutStream = $null
$stderrStream = $null
$exitCode = 1
$report = [ordered]@{
    schemaVersion = 1; status = 'refused'; startedUTC = [DateTime]::UtcNow.ToString('o')
    endedUTC = $null; failure = $null; buildDirectory = $null; configuration = $Configuration
    shaderSelection = $null; shaderDirectory = $null; sourceFiles = @(); shaders = @()
    inheritedEnvironment = @{ present = $hadInherited; MONSTER_SPLAT_SHADER_DIR = $inherited }
    childEnvironment = $null; environmentRestored = $false; command = $null
    binaryFreshness = 'caller_must_supply_fresh_matching_build'; exitCode = $null
}

function Get-Identity([string]$Path) {
    $file = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($file.PSIsContainer) { throw "Expected file: $Path" }
    return [ordered]@{ path = $file.FullName; bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}

function Get-ExistingDirectory([string]$Path) {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if (!$item.PSIsContainer) { throw "Expected directory: $Path" }
    return $item.FullName
}

function Assert-SamePath([string]$First, [string]$Second, [string]$Description) {
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    $left = [IO.Path]::GetFullPath($First).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $right = [IO.Path]::GetFullPath($Second).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    if (!$left.Equals($right, $comparison)) { throw "$Description does not match: $left versus $right" }
}

try {
    $output = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $output) { throw "Output directory already exists: $output" }
    [IO.Directory]::CreateDirectory($output) | Out-Null
    $reportStream = [IO.File]::Open((Join-Path $output 'windows-checks.json'), 'CreateNew', 'Write', 'None')
    $stdoutStream = [IO.File]::Open((Join-Path $output 'ctest.stdout.log'), 'CreateNew', 'Write', 'None')
    $stderrStream = [IO.File]::Open((Join-Path $output 'ctest.stderr.log'), 'CreateNew', 'Write', 'None')
    if ([string]::IsNullOrWhiteSpace($Configuration)) { throw 'Configuration must not be empty' }
    $build = Get-ExistingDirectory $BuildDirectory
    $report.buildDirectory = $build
    $cachePath = Join-Path $build 'CMakeCache.txt'
    $report.sourceFiles += Get-Identity $cachePath
    $cache = @{}
    foreach ($line in [IO.File]::ReadLines($cachePath)) {
        if ($line -match '^([^#/:][^:]*):[^=]+=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
    }
    foreach ($key in @('CMAKE_HOME_DIRECTORY', 'CMAKE_CACHEFILE_DIR', 'SPLAT_GENERATED_SHADER_DIR')) {
        if (!$cache.ContainsKey($key) -or [string]::IsNullOrWhiteSpace($cache[$key])) { throw "Missing CMake cache entry: $key" }
    }
    Assert-SamePath $cache.CMAKE_HOME_DIRECTORY $repo 'CMake source tree'
    Assert-SamePath $cache.CMAKE_CACHEFILE_DIR $build 'CMake binary tree'
    if ($PSBoundParameters.ContainsKey('ShaderDirectory')) {
        $report.shaderSelection = 'explicit'
        if ([string]::IsNullOrWhiteSpace($ShaderDirectory)) { throw 'Explicit ShaderDirectory is empty' }
        $selected = $ShaderDirectory
    } elseif ($hadInherited) {
        $report.shaderSelection = 'inherited'
        if ([string]::IsNullOrWhiteSpace($inherited)) { throw 'Inherited MONSTER_SPLAT_SHADER_DIR is empty' }
        $selected = $inherited
    } else {
        $report.shaderSelection = 'matching_cmake_cache'
        $selected = $cache.SPLAT_GENERATED_SHADER_DIR
    }
    $shaderRoot = Get-ExistingDirectory $selected
    $report.shaderDirectory = $shaderRoot

    # The configured input graph and the current production CMake lists must
    # agree. An arbitrary valid SPIR-V file or a test-only directory is not enough.
    $inputsPath = Join-Path $build 'splat-shader-inputs.json'
    $cmakePath = Join-Path $repo 'cmake/SplatLODStreaming.cmake'
    $manifestPath = Join-Path $shaderRoot 'shader-manifest.json'
    foreach ($path in @($inputsPath, $cmakePath, $manifestPath)) { $report.sourceFiles += Get-Identity $path }
    $inputs = @(Get-Content -LiteralPath $inputsPath -Raw | ConvertFrom-Json)
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or $manifest.targetEnvironment -ne 'vulkan1.1') { throw 'Unsupported production shader manifest' }
    $cmake = Get-Content -LiteralPath $cmakePath -Raw
    $expected = @()
    foreach ($list in @('SPLAT_COMPUTE_SHADERS', 'SPLAT_PRESENT_SHADERS')) {
        $match = [regex]::Match($cmake, ('(?s)set\(' + $list + '\s+([^)]*)\)'))
        if (!$match.Success) { throw "Cannot resolve production CMake list: $list" }
        foreach ($shader in ($match.Groups[1].Value.Trim() -split '\s+')) {
            $name = [IO.Path]::GetFileName($shader)
            if ($list -eq 'SPLAT_COMPUTE_SHADERS') { $name = [IO.Path]::GetFileNameWithoutExtension($name) }
            $expected += "$name.spv"
        }
    }
    $inputNames = @($inputs | ForEach-Object { [string]$_.output })
    $manifestNames = @($manifest.shaders | ForEach-Object { [string]$_.path })
    foreach ($collection in @(@{ names = $inputNames }, @{ names = $manifestNames })) {
        $actual = @($collection.names)
        if ($actual.Count -ne $expected.Count -or @($actual | Sort-Object -Unique).Count -ne $expected.Count -or
            @(Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)).Count) { throw 'Production shader collection mismatch' }
    }
    foreach ($input in $inputs) {
        $name = [string]$input.output
        if ([IO.Path]::GetFileName($name) -ne $name) { throw "Non-flat shader output: $name" }
        $entry = @($manifest.shaders | Where-Object { $_.path -ceq $name })
        if ($entry.Count -ne 1) { throw "Missing or duplicate manifest shader: $name" }
        $identity = Get-Identity (Join-Path $shaderRoot $name)
        $report.shaders += $identity
        if ($identity.bytes -lt 20 -or $identity.bytes % 4 -ne 0 -or $identity.bytes -ne $entry[0].bytes -or
            $identity.sha256 -cne $entry[0].sha256) { throw "SPIR-V identity/size mismatch: $name" }
        $words = [IO.File]::ReadAllBytes($identity.path)
        if ([BitConverter]::ToUInt32($words, 0) -ne 0x07230203) { throw "SPIR-V magic mismatch: $name" }
        $position = 20
        while ($position -lt $words.Length) {
            $instructionBytes = ([BitConverter]::ToUInt32($words, $position) -shr 16) * 4
            if ($instructionBytes -eq 0 -or $position + $instructionBytes -gt $words.Length) { throw "Malformed SPIR-V instruction: $name" }
            $position += $instructionBytes
        }
        $sourceNames = @($input.sources | ForEach-Object { [string]$_.logical })
        $recordedNames = @($entry[0].sources | ForEach-Object { [string]$_.path })
        if (!$sourceNames.Count -or $sourceNames.Count -ne $recordedNames.Count -or
            @(Compare-Object ($sourceNames | Sort-Object) ($recordedNames | Sort-Object)).Count) { throw "Shader dependency collection mismatch: $name" }
        foreach ($source in $input.sources) {
            $sourcePath = Join-Path $repo $source.logical
            Assert-SamePath $source.file $sourcePath 'Shader input source'
            $sourceIdentity = Get-Identity $sourcePath
            $recorded = @($entry[0].sources | Where-Object { $_.path -ceq $source.logical })
            if ($recorded.Count -ne 1 -or $sourceIdentity.sha256 -cne $recorded[0].sha256) { throw "Shader source identity mismatch: $($source.logical)" }
            $report.sourceFiles += $sourceIdentity
        }
    }
    if ($PSBoundParameters.ContainsKey('CTestPath')) {
        if ([string]::IsNullOrWhiteSpace($CTestPath)) { throw 'Explicit CTestPath is empty' }
    } elseif ($cache.ContainsKey('CMAKE_CTEST_COMMAND')) {
        $CTestPath = $cache.CMAKE_CTEST_COMMAND
    } elseif ($cache.ContainsKey('CMAKE_COMMAND')) {
        $CTestPath = Join-Path ([IO.Path]::GetDirectoryName($cache.CMAKE_COMMAND)) $(if ($IsWindows) { 'ctest.exe' } else { 'ctest' })
    } else { throw 'Cannot resolve CTest from matching CMake cache; supply CTestPath' }
    $ctestIdentity = Get-Identity $CTestPath
    $arguments = @('--test-dir', $build, '-C', $Configuration, '--output-on-failure', '--no-tests=error')
    $report.command = [ordered]@{ executable = $ctestIdentity; arguments = $arguments; workingDirectory = $repo
        startedUTC = $null; endedUTC = $null; exitCode = $null; childStarted = $false
        stdout = (Join-Path $output 'ctest.stdout.log'); stderr = (Join-Path $output 'ctest.stderr.log') }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $ctestIdentity.path
    $start.WorkingDirectory = $repo
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $start.Environment[$environmentName] = $shaderRoot
    $report.childEnvironment = @{ MONSTER_SPLAT_SHADER_DIR = $start.Environment[$environmentName] }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        $report.command.startedUTC = [DateTime]::UtcNow.ToString('o')
        if (!$process.Start()) { throw 'CTest process did not start' }
        $report.command.childStarted = $true
        $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdoutStream)
        $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderrStream)
        $process.WaitForExit()
        $stdoutTask.GetAwaiter().GetResult()
        $stderrTask.GetAwaiter().GetResult()
        $exitCode = $process.ExitCode
        $report.command.exitCode = $exitCode
        $report.status = if ($exitCode -eq 0) { 'passed' } else { 'failed' }
    } finally {
        $report.command.endedUTC = [DateTime]::UtcNow.ToString('o')
        $process.Dispose()
    }
} catch {
    $report.failure = $_.Exception.Message
    if ($report.command -and $report.command.childStarted) { $report.status = 'failed' }
    $exitCode = 1
    [Console]::Error.WriteLine($report.failure)
} finally {
    # Only ProcessStartInfo.Environment is changed. In particular, setting a
    # process variable to null can create an empty variable on this runtime.
    # Leave the caller's absent/empty/value state intact and verify it exactly.
    $report.environmentRestored = ((Test-Path "Env:$environmentName") -eq $hadInherited -and
        [Environment]::GetEnvironmentVariable($environmentName, 'Process') -ceq $inherited)
    $report.exitCode = $exitCode
    $report.endedUTC = [DateTime]::UtcNow.ToString('o')
    if ($stdoutStream) { $stdoutStream.Dispose() }
    if ($stderrStream) { $stderrStream.Dispose() }
    if ($reportStream) {
        try {
            $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($report | ConvertTo-Json -Depth 30) + "`n")
            $reportStream.Write($bytes, 0, $bytes.Length)
        } finally { $reportStream.Dispose() }
    }
}
exit $exitCode
