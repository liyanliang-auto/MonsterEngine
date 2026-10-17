[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][string]$ShaderDirectory,
 [Parameter(Mandatory=$true)][string]$OutputRoot,
 [string]$Config=(Join-Path $PSScriptRoot 'comparison-config.json'),
 [ValidateRange(60,3600)][int]$TimeoutSeconds=600
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Save($Value,[string]$Path) { $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding UTF8 }
$exe=(Resolve-Path -LiteralPath $Executable).Path
$shaders=(Resolve-Path -LiteralPath $ShaderDirectory).Path
$base=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$root=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $root) { throw 'OutputRoot already exists; preserve previous evidence and choose a new directory.' }
New-Item -ItemType Directory -Path $root | Out-Null
$identity=@(@{path=$exe;sha256=(Hash $exe)})
foreach($file in (Get-ChildItem -LiteralPath $shaders -Filter '*.spv' -Recurse -File | Sort-Object FullName)) { $identity+=@{path=$file.FullName;sha256=(Hash $file.FullName)} }
if($identity.Count -lt 2) { throw 'No shader identity found.' }
foreach($key in @('scene','sourcePLY','sequence')) { $path=(Resolve-Path -LiteralPath $base.$key).Path; $identity+=@{path=$path;sha256=(Hash $path)} }
$manifest=[ordered]@{version=1;status='RUNNING';scope='One fixed Bicycle SH32/SH16 timing pair; separate validation-enabled RGBA8 capture pair';qualityGate=@{representation='normalized linear RGB RGBA8_UNORM';maeMaximum=(1.0/255);psnrMinimumDB=40;maximumAbsoluteError=(16.0/255);visualInspectionRequired=$true};identity=$identity;runs=@();errors=@()}
$manifestPath=Join-Path $root 'run-manifest.json'
$previousShader=$env:MONSTER_SPLAT_SHADER_DIR
$env:MONSTER_SPLAT_SHADER_DIR=$shaders
try {
 foreach($kind in @('timing','capture')) {
  foreach($mode in @(0,1)) {
   $directory=Join-Path $root "$kind-mode$mode"
   New-Item -ItemType Directory -Path $directory | Out-Null
   $runtime=[ordered]@{}
   foreach($p in $base.PSObject.Properties) { $runtime[$p.Name]=$p.Value }
   $runtime['output']=$directory; $runtime['shStorageMode']=$mode
   $runtime['captureOnly']=($kind -eq 'capture'); $runtime['validation']=($kind -eq 'capture')
   $runtime['captureTimes']=@(); if($kind -eq 'capture') { $runtime['captureTimes']=@(0) }
   $configPath=Join-Path $directory 'runtime-config.json'; Save $runtime $configPath
   $record=[ordered]@{kind=$kind;mode=$mode;directory=$directory;config=$runtime;command=@($exe,'--splat-config',$configPath);startedUTC=[DateTime]::UtcNow.ToString('o');exitCode=$null;rawHashes=@()}
   $manifest.runs+=,$record; Save $manifest $manifestPath
   $process=$null
   try {
    $process=Start-Process -FilePath $exe -ArgumentList @('--splat-config',('"'+$configPath+'"')) -WorkingDirectory (Split-Path -Parent $exe) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $directory 'stdout.log') -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
    if(-not $process.WaitForExit($TimeoutSeconds*1000)) { throw 'Renderer timeout; partial evidence is not a completed run.' }
    $process.WaitForExit(); $process.Refresh(); $record.exitCode=$process.ExitCode
    if($record.exitCode -ne 0) { throw ('Renderer exit '+$record.exitCode) }
   } finally {
    if($null -ne $process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $record['endedUTC']=[DateTime]::UtcNow.ToString('o')
    foreach($file in (Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName)) { $record.rawHashes+=@{path=$file.FullName;sha256=(Hash $file.FullName)} }
    Save $manifest $manifestPath
   }
  }
 }
 foreach($file in $identity) { if((Hash $file.path) -ne $file.sha256) { throw ('Experiment input changed: '+$file.path) } }
 $manifest.status='RECORDED'
} catch { $manifest.status='FAILED'; $manifest.errors+=$_.Exception.Message } finally { $env:MONSTER_SPLAT_SHADER_DIR=$previousShader; Save $manifest $manifestPath }
Write-Output $manifestPath
if($manifest.status -ne 'RECORDED') { throw ($manifest.errors -join '; ') }
