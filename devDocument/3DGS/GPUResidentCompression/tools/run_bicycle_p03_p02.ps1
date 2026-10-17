[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][string]$ShaderDirectory,
 [Parameter(Mandatory=$true)][string]$OutputRoot,
 [string]$Config=(Join-Path $PSScriptRoot 'comparison-config.json'),
 [ValidateRange(0,3)][int]$P03Degree=2,
 [ValidateRange(30,3600)][int]$TimeoutSeconds=600
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Save-Json($Value,[string]$Path) { $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $Path -Encoding UTF8 }
function Require([bool]$Condition,[string]$Message) { if(-not $Condition) { throw $Message } }

$exe = (Resolve-Path -LiteralPath $Executable).Path
$shaders = (Resolve-Path -LiteralPath $ShaderDirectory).Path
$base = Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
foreach($name in @('centerQuantization','conicQuantization')) {
 if($base.PSObject.Properties.Name -contains $name) { Require ([int]$base.$name -eq 0) "$name must remain disabled for this experiment" }
}
$root = [IO.Path]::GetFullPath($OutputRoot)
Require (-not (Test-Path -LiteralPath $root)) 'OutputRoot already exists; choose a new directory.'
New-Item -ItemType Directory -Path $root | Out-Null
$identity = @(@{path=$exe;sha256=(Hash $exe)})
foreach($file in (Get-ChildItem -LiteralPath $shaders -Filter '*.spv' -Recurse -File | Sort-Object FullName)) { $identity += @{path=$file.FullName;sha256=(Hash $file.FullName)} }
Require ($identity.Count -gt 1) 'No shader identity found.'
foreach($key in @('scene','sourcePLY','sequence')) { $path=(Resolve-Path -LiteralPath $base.$key).Path; $identity += @{path=$path;sha256=(Hash $path)} }

$manifest = [ordered]@{version=1;status='RUNNING';scope='Bicycle P03 degree/storage pair followed by isolated P02 projection-color pair';p03Degree=$P03Degree;qualityGate=[ordered]@{representation='normalized linear RGB RGBA8_UNORM';maeMaximum=(1.0/255);maximumAbsoluteError=(16.0/255);visualInspectionRequired=$true};identity=$identity;variants=@();errors=@()}
$manifestPath = Join-Path $root 'run-manifest.json'
$variants = @(
 [ordered]@{name='baseline';shStorageDegree=3;projectionColorCompression=$false},
 [ordered]@{name='p03';shStorageDegree=$P03Degree;projectionColorCompression=$false},
 [ordered]@{name='p02';shStorageDegree=$P03Degree;projectionColorCompression=$true}
)
$previousShader = $env:MONSTER_SPLAT_SHADER_DIR
$env:MONSTER_SPLAT_SHADER_DIR = $shaders
try {
 foreach($variant in $variants) {
  foreach($kind in @('timing','capture')) {
   $directory = Join-Path $root ($variant.name + '-' + $kind)
   New-Item -ItemType Directory -Path $directory | Out-Null
   $runtime = [ordered]@{}
   foreach($p in $base.PSObject.Properties) { $runtime[$p.Name] = $p.Value }
   $runtime['output']=$directory; $runtime['shStorageDegree']=$variant.shStorageDegree; $runtime['displaySHDegree']=$variant.shStorageDegree; $runtime['projectionColorCompression']=$variant.projectionColorCompression
   $runtime['captureOnly']=($kind -eq 'capture'); $runtime['validation']=($kind -eq 'capture'); $runtime['captureTimes']=@(); if($kind -eq 'capture') { $runtime['captureTimes']=@(0) }
   $configPath=Join-Path $directory 'runtime-config.json'; Save-Json $runtime $configPath
   $record=[ordered]@{variant=$variant.name;kind=$kind;directory=$directory;config=$runtime;command=@($exe,'--splat-config',$configPath);startedUTC=[DateTime]::UtcNow.ToString('o');exitCode=$null;rawHashes=@()}
   $manifest.variants += ,$record; Save-Json $manifest $manifestPath
   $process=$null
   try {
    $process=Start-Process -FilePath $exe -ArgumentList @('--splat-config',('"'+$configPath+'"')) -WorkingDirectory (Split-Path -Parent $exe) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $directory 'stdout.log') -RedirectStandardError (Join-Path $directory 'stderr.log') -PassThru
    if(-not $process.WaitForExit($TimeoutSeconds*1000)) { throw 'Renderer timeout; partial evidence is not a completed run.' }
    $process.WaitForExit(); $process.Refresh(); $record.exitCode=$process.ExitCode
    if($record.exitCode -ne 0) { throw ('Renderer exit '+$record.exitCode) }
   } finally {
    if($null -ne $process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $record['endedUTC']=[DateTime]::UtcNow.ToString('o')
    foreach($file in (Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName)) { $record.rawHashes += @{path=$file.FullName;sha256=(Hash $file.FullName)} }
    Save-Json $manifest $manifestPath
   }
  }
 }
 foreach($file in $identity) { Require ((Hash $file.path) -eq $file.sha256) ('Experiment input changed: '+$file.path) }
 $manifest.status='RECORDED'
} catch { $manifest.status='FAILED'; $manifest.errors += $_.Exception.Message } finally { $env:MONSTER_SPLAT_SHADER_DIR=$previousShader; Save-Json $manifest $manifestPath }
Write-Output $manifestPath
if($manifest.status -ne 'RECORDED') { throw ($manifest.errors -join '; ') }
