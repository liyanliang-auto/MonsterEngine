param(
    [Parameter(Mandatory=$true)][string]$Cli,
    [Parameter(Mandatory=$true)][string]$NativeTests,
    [Parameter(Mandatory=$true)][string]$FixtureRoot
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $FixtureRoot) { Remove-Item -LiteralPath $FixtureRoot -Recurse -Force }
New-Item -ItemType Directory -Path $FixtureRoot -Force | Out-Null
$project = Join-Path $FixtureRoot '项目 space'
& $NativeTests --create-project $project
if ($LASTEXITCODE -ne 0) { throw 'Cannot create CLI test project' }
$projectHash = (Get-FileHash -LiteralPath (Join-Path $project 'project.mstudio') -Algorithm SHA256).Hash
$count = 0
function Run-Cli([string[]]$Arguments, [int]$ExpectedCode, [string]$ExpectedStatus) {
    $script:count++
    $stdout = Join-Path $FixtureRoot "stdout-$script:count.json"
    $stderr = Join-Path $FixtureRoot "stderr-$script:count.json"
    $quoted = @($Arguments | ForEach-Object { '"' + $_ + '"' })
    $process = Start-Process -FilePath $Cli -ArgumentList $quoted -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $exitCode = $process.ExitCode
    if ($exitCode -ne $ExpectedCode) { throw "CLI exit $exitCode, expected $ExpectedCode; $stderr" }
    $resultPath = if ($ExpectedCode -eq 0) { $stdout } else { $stderr }
    $result = Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($result.status -ne $ExpectedStatus) { throw "CLI status mismatch: $resultPath" }
    return $result
}
$absent = Run-Cli @('inspect','--project',$project) 10 'error'
if ($absent.code -ne 'NO_INPUT') { throw 'Missing input diagnostic' }
$video = Join-Path $FixtureRoot '视频 one.mp4'
[IO.File]::WriteAllBytes($video, [byte[]](0,1,2,3,254,255))
$videoHash = (Get-FileHash -LiteralPath $video -Algorithm SHA256).Hash.ToLowerInvariant()
$first = Run-Cli @('import-video','--project',$project,'--source',$video) 0 'ok'
if ($first.kind -ne 'video' -or $first.revision -ne 1 -or $first.files.Count -ne 1 -or
    $first.files[0].sha256 -ne $videoHash -or $first.files[0].byteCount -ne 6) { throw 'Video CLI identity mismatch' }
if ((Get-FileHash -LiteralPath (Join-Path $project 'project.mstudio') -Algorithm SHA256).Hash -ne $projectHash) { throw 'v1 project changed' }
Remove-Item -LiteralPath $video
$reopened = Run-Cli @('inspect','--project',$project) 0 'ok'
if ($reopened.inputId -ne $first.inputId -or $reopened.files[0].sha256 -ne $videoHash) { throw 'Video reopen identity mismatch' }
$photos = Join-Path $FixtureRoot '照片 space'
New-Item -ItemType Directory -Path $photos | Out-Null
[IO.File]::WriteAllBytes((Join-Path $photos '中文.png'), [byte[]](8,7,6))
[IO.File]::WriteAllBytes((Join-Path $photos 'second.JPG'), [byte[]](9,10))
$second = Run-Cli @('import-photos','--project',$project,'--source',$photos) 0 'ok'
if ($second.kind -ne 'photos' -or $second.files.Count -ne 2 -or $second.revision -ne 2 -or
    $second.inputId -eq $first.inputId) { throw 'Photo replacement identity mismatch' }
Remove-Item -LiteralPath $photos -Recurse
$reopened = Run-Cli @('inspect','--project',$project) 0 'ok'
if ($reopened.inputId -ne $second.inputId) { throw 'Photo reopen identity mismatch' }
$bad = Run-Cli @('import-video','--project',$project,'--source',(Join-Path $FixtureRoot 'missing.mp4')) 4 'error'
if ($bad.code -ne 'INVALID_SOURCE') { throw 'Invalid source diagnostic mismatch' }
$reopened = Run-Cli @('inspect','--project',$project) 0 'ok'
if ($reopened.inputId -ne $second.inputId) { throw 'Failed import replaced current input' }
$manifest = Join-Path $project 'input.mstudio'
$saved = [IO.File]::ReadAllBytes($manifest)
$corrupt = [byte[]]$saved.Clone(); $corrupt[8] = 2
[IO.File]::WriteAllBytes($manifest, $corrupt)
try {
    $failure = Run-Cli @('inspect','--project',$project) 7 'error'
    if ($failure.code -ne 'CORRUPT_MANIFEST') { throw 'Corrupt manifest diagnostic mismatch' }
} finally { [IO.File]::WriteAllBytes($manifest, $saved) }
if ((Get-FileHash -LiteralPath (Join-Path $project 'project.mstudio') -Algorithm SHA256).Hash -ne $projectHash) { throw 'v1 project changed after CLI test' }
Write-Output "CLI integration PASS: $count invocations; video/photos/reopen/replacement/failure/corruption"
