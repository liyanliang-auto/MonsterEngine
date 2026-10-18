#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Profile','Release')][string]$Variant = 'Debug',
    [switch]$Bundle,
    [switch]$RequireSigning,
    [string]$OutputDirectory,
    [string]$InitScript,
    [int]$VersionCode = 1,
    [string]$VersionName = '0.1.0'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo 'android/build/reports/delivery' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$report = [ordered]@{ schemaVersion=1; variant=$Variant; bundle=[bool]$Bundle; startedUTC=[DateTime]::UtcNow.ToString('o'); commands=@(); artifacts=@(); status='running'; runtimeVerified=$false }
function Invoke-Checked([string]$Executable,[string[]]$Arguments) {
    $entry = [ordered]@{ executable=[IO.Path]::GetFileName($Executable); arguments=@(); exitCode=$null }
    for ($index=0; $index -lt $Arguments.Count; ++$index) {
        $argument=$Arguments[$index]
        if ($argument -match '^-P(SPLAT_GLSLC|SPLAT_SPIRV_VAL|Python3_EXECUTABLE)=(.+)$') { $argument="-P$($Matches[1])=$([IO.Path]::GetFileName($Matches[2]))" }
        elseif ($index -gt 0 -and $Arguments[$index-1] -eq '--init-script') { $argument=[IO.Path]::GetFileName($argument) }
        elseif ([IO.Path]::IsPathRooted($argument)) { $argument=[IO.Path]::GetFileName($argument) }
        $entry.arguments += $argument
    }
    $report.commands += $entry
    & $Executable @Arguments
    $entry.exitCode=$LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $([IO.Path]::GetFileName($Executable)), exit $LASTEXITCODE" }
}
function Resolve-HostTool([string]$Name,[string]$CommandName) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if (!$value) { $command = Get-Command $CommandName -ErrorAction SilentlyContinue; if ($command) { $value = $command.Source } }
    if (!$value -or !(Test-Path -LiteralPath $value -PathType Leaf)) { throw "Resolve $Name to an existing host executable" }
    return (Resolve-Path -LiteralPath $value).Path
}
try {
    $sdk = $env:ANDROID_HOME
    if (!$sdk) { $sdk = $env:ANDROID_SDK_ROOT }
    if (!$sdk -or !(Test-Path -LiteralPath $sdk -PathType Container)) { throw 'Set ANDROID_HOME to the SDK root' }
    if (!$env:JAVA_HOME -or !(Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin/java.exe')) -and !(Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin/java'))) { throw 'Set JAVA_HOME to JDK 21' }
    $java = Join-Path $env:JAVA_HOME $(if ($IsWindows) { 'bin/java.exe' } else { 'bin/java' })
    $versionOutput = (& $java -version 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch 'version "21[.\"]') { throw 'A JDK 21 runtime is required' }
    $report.javaVersion = $versionOutput.Trim()
    $report.toolchain = @{ gradle='8.13'; agp='8.13.2'; kotlin='2.3.10'; compileSDK=36; buildTools='36.1.0'; ndk='30.0.16248370'; cmake='3.22.1'; abis=@('arm64-v8a','x86_64') }
    foreach ($relative in @('platforms/android-36','build-tools/36.1.0','ndk/30.0.16248370','cmake/3.22.1')) {
        if (!(Test-Path -LiteralPath (Join-Path $sdk $relative))) { throw "Missing pinned SDK package: $relative" }
    }
    $glslc = Resolve-HostTool 'SPLAT_GLSLC' 'glslc'
    $spirv = Resolve-HostTool 'SPLAT_SPIRV_VAL' 'spirv-val'
    $python = Resolve-HostTool 'Python3_EXECUTABLE' 'python'
    $report.hostTools = @()
    foreach ($tool in @($glslc,$spirv,$python)) {
        $report.hostTools += @{ name=[IO.Path]::GetFileName($tool); sha256=(Get-FileHash -LiteralPath $tool -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $signingNames = @('MONSTER_KEYSTORE','MONSTER_KEYSTORE_PASSWORD','MONSTER_KEY_ALIAS','MONSTER_KEY_PASSWORD')
    $signed = @($signingNames | Where-Object { [Environment]::GetEnvironmentVariable($_) }).Count
    if ($signed -ne 0 -and $signed -ne 4) { throw 'Supply all four release signing environment variables or none' }
    if ($RequireSigning -and ($Variant -ne 'Release' -or $signed -ne 4)) { throw 'RequireSigning needs Release and all external signing variables' }
    if ($signed -eq 4 -and !(Test-Path -LiteralPath $env:MONSTER_KEYSTORE -PathType Leaf)) { throw 'External keystore is missing' }
    if ($VersionCode -le 0 -or $VersionName -notmatch '^[0-9A-Za-z.+_-]{1,64}$') { throw 'Invalid application version' }
    $report.signing = if ($Variant -eq 'Release' -and $signed -eq 4) { 'external_release' } elseif ($Variant -eq 'Release') { 'unsigned' } else { 'development' }
    $report.gitHead = (& git -C $repo rev-parse HEAD | Out-String).Trim()
    $report.workspaceDirty = [bool]((& git -C $repo status --porcelain | Out-String).Trim())
    $report.sourceInventory = @()
    $sourceFiles = @(& git -C $repo ls-files --cached --others --exclude-standard -- Include Source Shaders cmake Tools/Android android/app/src android/app/build.gradle.kts android/gradle android/build.gradle.kts android/settings.gradle.kts CMakeLists.txt)
    foreach ($relative in ($sourceFiles | Sort-Object -Unique)) {
        $source = Join-Path $repo $relative
        if (Test-Path -LiteralPath $source -PathType Leaf) { $report.sourceInventory += @{ path=$relative; sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() } }
    }
    $task = if ($Bundle) { ":app:bundle$Variant" } else { ":app:assemble$Variant" }
    $gradle = Join-Path $repo $(if ($IsWindows) { 'android/gradlew.bat' } else { 'android/gradlew' })
    $gradleArgs = @('--no-daemon','--console=plain','--stacktrace',$task,"-PmonsterVersionCode=$VersionCode","-PmonsterVersionName=$VersionName","-PSPLAT_GLSLC=$glslc","-PSPLAT_SPIRV_VAL=$spirv","-PPython3_EXECUTABLE=$python")
    if ($InitScript) { $gradleArgs += @('--init-script',(Resolve-Path -LiteralPath $InitScript).Path) }
    Push-Location (Join-Path $repo 'android')
    try { Invoke-Checked $gradle $gradleArgs } finally { Pop-Location }
    $kind = if ($Bundle) { 'bundle' } else { 'apk' }
    $extension = if ($Bundle) { '*.aab' } else { '*.apk' }
    $artifactRoot = Join-Path $repo "android/app/build/outputs/$kind/$($Variant.ToLowerInvariant())"
    $packages = @(Get-ChildItem -LiteralPath $artifactRoot -Filter $extension -File)
    if (!$packages.Count) { throw 'Build returned without expected package' }
    foreach ($package in $packages) {
        $report.artifacts += @{ path=[IO.Path]::GetRelativePath($repo,$package.FullName); bytes=$package.Length; sha256=(Get-FileHash -LiteralPath $package.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $inspectArgs = @((Join-Path $repo 'Tools/Android/inspect_native_package.py')) + @($packages.FullName) + @('--output',(Join-Path $output 'native-package.json'))
    Invoke-Checked $python $inspectArgs
    $report.status='passed_static_package_checks'
} catch {
    $report.status='failed'; $report.failure=$_.Exception.Message
    throw
} finally {
    $report.finishedUTC=[DateTime]::UtcNow.ToString('o')
    # No signing values or keystore contents enter delivery metadata.
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $output 'build-identity.json') -Encoding utf8
}
