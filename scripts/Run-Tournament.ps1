param(
  [ValidateSet('CandidateVsAll','CandidateVsOpponent','AllPairs')][string]$Mode = 'CandidateVsAll',
  [ValidateSet('Quick','AllMaps')][string]$Coverage = 'Quick',
  [int]$Jobs = 2,
  [string]$Candidate = 'candidate-bot',
  [string]$Opponent = ''
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
if (!(Test-Path 'cambc.toml')) { throw 'Run from the extracted workspace kit; root cambc.toml not found.' }
$Bots = @(Get-ChildItem 'bots' -Directory | Where-Object { (Test-Path (Join-Path $_.FullName 'src/main.cpp')) -and (Test-Path (Join-Path $_.FullName 'cambc.toml')) } | Sort-Object Name | ForEach-Object { $_.Name })
if ($Bots.Count -lt 2) { throw 'Need at least two configured bots under bots/.' }
if ($Mode -in @('CandidateVsAll','CandidateVsOpponent') -and $Candidate -notin $Bots) { throw "Candidate '$Candidate' not found. Bots detected: $($Bots -join ', ')" }
if ($Mode -eq 'CandidateVsOpponent') {
  if ([string]::IsNullOrWhiteSpace($Opponent)) { throw "Mode CandidateVsOpponent requires -Opponent <bot-folder-name>." }
  if ($Opponent -notin $Bots) { throw "Opponent '$Opponent' not found. Bots detected: $($Bots -join ', ')" }
  if ($Opponent -eq $Candidate) { throw 'Candidate and opponent must be two different bots.' }
}
$Maps = @(Get-ChildItem 'maps' -File -Recurse | Where-Object { $_.Name -like '*.map26a' } | Sort-Object FullName)
if ($Maps.Count -eq 0) {
  Write-Host 'No maps found; running cambc maps pull...' -ForegroundColor Yellow
  cambc maps pull
  if ($LASTEXITCODE -ne 0) { throw 'cambc maps pull failed. Run Setup-Workspace.ps1 and inspect the complete error.' }
  $Maps = @(Get-ChildItem 'maps' -File -Recurse | Where-Object { $_.Name -like '*.map26a' } | Sort-Object FullName)
}
if ($Maps.Count -eq 0) { throw 'Still no .map26a maps in maps/. Cannot create match plan.' }
if ($Coverage -eq 'Quick') { $Maps = @($Maps | Select-Object -First 1) }
New-Item -ItemType Directory -Force 'replays', 'reports/logs' | Out-Null
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ($Mode -eq 'CandidateVsAll') { $MatchFile = "matches-candidate-vs-all-$Coverage-$Stamp.txt"; $LogFile = "reports/logs/candidate-vs-all-$Coverage-$Stamp.log" }
elseif ($Mode -eq 'CandidateVsOpponent') {
  $PairTag = "$Candidate-vs-$Opponent"
  $MatchFile = "matches-$PairTag-$Coverage-$Stamp.txt"
  $LogFile = "reports/logs/$PairTag-$Coverage-$Stamp.log"
}
else { $MatchFile = "matches-all-pairs-$Coverage-$Stamp.txt"; $LogFile = "reports/logs/all-pairs-$Coverage-$Stamp.log" }
$Lines = [System.Collections.Generic.List[string]]::new()
$Manifest = [System.Collections.Generic.List[object]]::new()
$Index = 0
function Add-MatchLine([string]$A,[string]$B,[System.IO.FileInfo]$Map,[string]$Tag) {
  $script:Index++
  $id = '{0:D5}' -f $script:Index
  $replay = "replays/$($Tag)-$id.repl26a"
  $mapRel = $Map.FullName.Substring($script:Root.TrimEnd('\').Length + 1).Replace('\','/')
  $line = "bots/$A bots/$B $mapRel $replay"
  $script:Lines.Add($line)
  $script:Manifest.Add([pscustomobject]@{ id=$id; bot_a=$A; bot_b=$B; map=$mapRel; replay=$replay; mode=$script:Mode; coverage=$script:Coverage })
}
if ($Mode -eq 'CandidateVsAll') {
  foreach ($OtherBot in $Bots) {
    if ($OtherBot -eq $Candidate) { continue }
    foreach ($Map in $Maps) {
      $safeMap = [IO.Path]::GetFileNameWithoutExtension($Map.Name) -replace '[^A-Za-z0-9_-]','_'
      Add-MatchLine $Candidate $OtherBot $Map "candA-$OtherBot-$safeMap"
      Add-MatchLine $OtherBot $Candidate $Map "candB-$OtherBot-$safeMap"
    }
  }
} elseif ($Mode -eq 'CandidateVsOpponent') {
  foreach ($Map in $Maps) {
    $safeMap = [IO.Path]::GetFileNameWithoutExtension($Map.Name) -replace '[^A-Za-z0-9_-]','_'
    Add-MatchLine $Candidate $Opponent $Map "pair-$Candidate-vs-$Opponent-A-$safeMap"
    Add-MatchLine $Opponent $Candidate $Map "pair-$Candidate-vs-$Opponent-B-$safeMap"
  }
} else {
  for ($i=0; $i -lt $Bots.Count; $i++) {
    for ($j=$i+1; $j -lt $Bots.Count; $j++) {
      foreach ($Map in $Maps) {
        $safeMap = [IO.Path]::GetFileNameWithoutExtension($Map.Name) -replace '[^A-Za-z0-9_-]','_'
        Add-MatchLine $Bots[$i] $Bots[$j] $Map "pair-$($Bots[$i])-$($Bots[$j])-$safeMap"
        Add-MatchLine $Bots[$j] $Bots[$i] $Map "pair-$($Bots[$j])-$($Bots[$i])-$safeMap"
      }
    }
  }
}
if ($Lines.Count -eq 0) { throw 'No matches generated.' }

# Interactive collision handling: protect existing replays and reports.
$PlannedReplays = @($Manifest | ForEach-Object { Join-Path $Root ($_.replay.Replace('/', '\')) })
$ExistingReplays = @($PlannedReplays | Where-Object { Test-Path $_ })
$FixedReports = @(
  'reports/tournament-match-results.csv', 'reports/tournament-standings.csv',
  'reports/candidate-head-to-head.csv', 'reports/candidate-by-map.csv', 'reports/tournament-report.xlsx'
)
$ExistingReports = @($FixedReports | ForEach-Object { Join-Path $Root ($_ -replace '/', '\') } | Where-Object { Test-Path $_ })
$OutputPolicy = 'New'
if ($ExistingReplays.Count -gt 0 -or $ExistingReports.Count -gt 0) {
  Write-Host "`nExisting tournament files were detected." -ForegroundColor Yellow
  Write-Host "Conflicting replay files: $($ExistingReplays.Count)"
  Write-Host "Existing report files: $($ExistingReports.Count)"
  Write-Host '1 = Rematch and REPLACE matching replays/reports'
  Write-Host '2 = Rematch and CREATE NEW timestamped reports/replays (preserve old files)'
  Write-Host '3 = Cancel without changes'
  $choice = Read-Host 'Choose 1, 2, or 3'
  if ($choice -eq '3' -or [string]::IsNullOrWhiteSpace($choice)) { Write-Host 'Cancelled; no files changed.' -ForegroundColor Yellow; return }
  if ($choice -notin @('1','2')) { throw 'Invalid choice. Run again and choose 1, 2, or 3.' }
  if ($choice -eq '1') { $OutputPolicy = 'Replace' } else { $OutputPolicy = 'New' }
}
if ($OutputPolicy -eq 'New') {
  # Fresh suffixes ensure no collision with old replay files or prior manifests.
  $nonce = Get-Date -Format 'yyyyMMdd-HHmmss'
  $Lines = [System.Collections.Generic.List[string]]::new()
  foreach ($row in $Manifest) {
    $row.replay = $row.replay -replace '\.repl26a$', "-$nonce.repl26a"
    $row.map = $row.map
    $Lines.Add("bots/$($row.bot_a) bots/$($row.bot_b) $($row.map) $($row.replay)")
  }
  $MatchFile = $MatchFile -replace '\.txt$', "-$nonce.txt"
  $ManifestFile = $MatchFile -replace '\.txt$', '.csv'
  $LogFile = $LogFile -replace '\.log$', "-$nonce.log"
} else {
  # Delete only the exact replay files scheduled for this confirmed replacement.
  foreach ($file in $ExistingReplays) { Remove-Item -LiteralPath $file -Force }
}
$Lines | Set-Content -Encoding utf8 $MatchFile
if (!$ManifestFile) { $ManifestFile = ($MatchFile -replace '\.txt$','.csv') }
$Manifest | Export-Csv -NoTypeInformation -Encoding utf8 $ManifestFile
if ($Mode -eq 'CandidateVsOpponent') {
  Write-Host "Selected pair: $Candidate vs $Opponent" -ForegroundColor Cyan
} else {
  Write-Host "Bots: $($Bots -join ', ')" -ForegroundColor Cyan
}
Write-Host "Maps in this run: $($Maps.Count)" -ForegroundColor Cyan
Write-Host "Matches planned: $($Lines.Count)" -ForegroundColor Cyan
Write-Host "Match plan: $MatchFile" -ForegroundColor Cyan
Write-Host "Manifest: $ManifestFile" -ForegroundColor Cyan
Write-Host "Replays will be saved under replays/." -ForegroundColor Cyan
Write-Host "Starting local tournament. No ranked submission is performed." -ForegroundColor Green
# Native CAMBC output can write progress/diagnostics to stderr. Keep PowerShell from
# treating that text as a terminating error; the process exit code is checked explicitly.
$PreviousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
  & cambc run-batch $MatchFile --jobs $Jobs 2>&1 | Tee-Object -FilePath $LogFile
  $Code = $LASTEXITCODE
} finally { $ErrorActionPreference = $PreviousPreference }
if ($Code -ne 0) { throw "run-batch exited with code $Code. See $LogFile and inspect the first error." }
Write-Host "`nBatch run finished. Log: $LogFile" -ForegroundColor Green
Write-Host 'Generating CSV/Excel results from the actual winner lines in the log...' -ForegroundColor Cyan
if ($OutputPolicy -eq 'New') { $ReportSuffix = [IO.Path]::GetFileNameWithoutExtension($ManifestFile) -replace '^matches-', '' } else { $ReportSuffix = '' }

# Ensure the optional Excel dependency is available. CSV generation still proceeds if
# package installation is unavailable, but the user receives an explicit warning.
Write-Host "Checking Excel dependency..." -ForegroundColor Cyan
if ($LASTEXITCODE -ne 0) {
  Write-Host 'openpyxl is missing; attempting to install it so the Excel workbook is generated automatically...' -ForegroundColor Yellow
  & python -m pip install openpyxl
  if ($LASTEXITCODE -ne 0) {
    Write-Warning 'Could not install openpyxl. CSV reports will still be generated, but the .xlsx workbook will be skipped. Run: python -m pip install openpyxl'
  }
}

# Build arguments conditionally: passing an empty value after --output-suffix makes
# argparse fail with "expected one argument". In Replace mode, omit that option entirely.
$SummaryArgs = @('.\scripts\summarize_tournament_log.py', '--manifest', $ManifestFile, '--log', $LogFile, '--candidate', $Candidate)
if (-not [string]::IsNullOrWhiteSpace($ReportSuffix)) {
  $SummaryArgs += @('--output-suffix', $ReportSuffix)
}
& python @SummaryArgs
$SummaryCode = $LASTEXITCODE
if ($SummaryCode -ne 0) { Write-Warning "Tournament completed, but the report parser flagged incomplete/unmatched results (exit $SummaryCode). Review the validation counts above and the log." }
Write-Host 'No ranked submission is performed.' -ForegroundColor Green
