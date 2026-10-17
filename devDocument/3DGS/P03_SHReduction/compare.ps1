param([string]$Baseline,[string]$Candidate,[string]$Output='p03-comparison.json')
if(!(Test-Path $Baseline) -or !(Test-Path $Candidate)){ throw 'baseline/candidate metadata required' }
$b=Get-Content -Raw $Baseline | ConvertFrom-Json; $c=Get-Content -Raw $Candidate | ConvertFrom-Json
[pscustomobject]@{scene='Bicycle';baseline=$b;candidate=$c;fpsDeltaPct=if($b.fps){100*($c.fps-$b.fps)/$b.fps}else{$null};capacityDeltaPct=if($b.sourceBackingBytes){100*($c.sourceBackingBytes-$b.sourceBackingBytes)/$b.sourceBackingBytes}else{$null};note='Native title-bar screenshots and image metrics must be supplied by the real window capture run.'} | ConvertTo-Json -Depth 8 | Set-Content $Output -Encoding UTF8
