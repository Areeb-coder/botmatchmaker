param(
  [ValidateSet('CandidateVsAll','AllPairs')][string]$Mode = 'CandidateVsAll',
  [ValidateSet('Quick','AllMaps')][string]$Coverage = 'Quick',
  [string]$Candidate = 'candidate-bot',
  [int]$MaxMatches = 0
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
if (!(Test-Path 'cambc.toml')) { throw 'Root cambc.toml not found.' }
$Bots = @(Get-ChildItem 'bots' -Directory | Where-Object { (Test-Path (Join-Path $_.FullName 'src/main.cpp')) -and (Test-Path (Join-Path $_.FullName 'cambc.toml')) } | Sort-Object Name | ForEach-Object { $_.Name })
if ($Mode -eq 'CandidateVsAll' -and $Candidate -notin $Bots) { throw "Candidate '$Candidate' not found. Bots: $($Bots -join ', ')" }
$Maps = @(Get-ChildItem 'maps' -File -Recurse | Where-Object { $_.Name -like '*.map26a' } | Sort-Object FullName)
if ($Maps.Count -eq 0) { cambc maps pull; if ($LASTEXITCODE -ne 0) { throw 'maps pull failed.' }; $Maps = @(Get-ChildItem 'maps' -File -Recurse | Where-Object { $_.Name -like '*.map26a' } | Sort-Object FullName) }
if ($Maps.Count -eq 0) { throw 'No .map26a maps found.' }
if ($Coverage -eq 'Quick') { $Maps = @($Maps | Select-Object -First 1) }
New-Item -ItemType Directory -Force 'reports/stats','reports/logs' | Out-Null
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$ManifestPath = "reports/stats/manifest-$Mode-$Coverage-$Stamp.csv"
$Rows = [System.Collections.Generic.List[object]]::new()
$Pairs = [System.Collections.Generic.List[object]]::new()
if ($Mode -eq 'CandidateVsAll') {
  foreach ($Opponent in $Bots) { if ($Opponent -ne $Candidate) { $Pairs.Add([pscustomobject]@{a=$Candidate;b=$Opponent}); $Pairs.Add([pscustomobject]@{a=$Opponent;b=$Candidate}) } }
} else {
  for ($i=0; $i -lt $Bots.Count; $i++) { for ($j=$i+1; $j -lt $Bots.Count; $j++) { $Pairs.Add([pscustomobject]@{a=$Bots[$i];b=$Bots[$j]}); $Pairs.Add([pscustomobject]@{a=$Bots[$j];b=$Bots[$i]}) } }
}
$Total = $Pairs.Count * $Maps.Count
if ($MaxMatches -gt 0) { $Total = [Math]::Min($Total,$MaxMatches) }
$Run = 0
foreach ($Pair in $Pairs) {
  foreach ($Map in $Maps) {
    if ($MaxMatches -gt 0 -and $Run -ge $MaxMatches) { break }
    $Run++
    $id = '{0:D5}' -f $Run
    $safeMap = [IO.Path]::GetFileNameWithoutExtension($Map.Name) -replace '[^A-Za-z0-9_-]','_'
    $stats = "reports/stats/stats-$Mode-$Coverage-$id.json"
    $log = "reports/logs/stats-$Mode-$Coverage-$id.log"
    $mapRel = $Map.FullName.Substring($Root.TrimEnd('\').Length + 1).Replace('\','/')
    Write-Progress -Activity 'CAMBC statistics tournament' -Status "$Run / $Total : $($Pair.a) vs $($Pair.b) on $($Map.Name)" -PercentComplete (($Run/$Total)*100)
    Write-Host "[$Run/$Total] $($Pair.a) vs $($Pair.b) | $mapRel"
    & cambc run $Pair.a $Pair.b --map $mapRel --statistics $stats 2>&1 | Tee-Object -FilePath $log
    $exit = $LASTEXITCODE
    $Rows.Add([pscustomobject]@{id=$id;bot_a=$Pair.a;bot_b=$Pair.b;map=$mapRel;stats_json=$stats;log=$log;exit_code=$exit;status=$(if($exit -eq 0 -and (Test-Path $stats)){'completed'}else{'failed_or_no_stats'})})
    if ($exit -ne 0) { Write-Warning "Match failed; see $log" }
  }
  if ($MaxMatches -gt 0 -and $Run -ge $MaxMatches) { break }
}
Write-Progress -Activity 'CAMBC statistics tournament' -Completed
$Rows | Export-Csv -NoTypeInformation -Encoding utf8 $ManifestPath
Write-Host "Manifest saved: $ManifestPath" -ForegroundColor Green
Write-Host 'Now run: python .\scripts\summarize_stats.py' -ForegroundColor Cyan
