param(
  [ValidateSet('CandidateVsAll','CandidateVsOpponent','AllPairs')][string]$Mode = 'CandidateVsAll',
  [ValidateSet('Quick','AllMaps')][string]$Coverage = 'AllMaps',
  [int]$Jobs = 2,
  [string]$Candidate = 'candidate-bot',
  [string]$Opponent = '',
  [switch]$InstallOrUpgradeCLI,
  [switch]$SkipSetup
)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
Set-Location $Root
Write-Host 'CAMBC research pipeline — local only; no ranked upload or automatic promotion.' -ForegroundColor Cyan
$Setup = Join-Path $Root 'scripts\Setup-Workspace.ps1'
# If CLI installation was explicitly requested, perform it before preflight; otherwise
# a missing CLI would block the very setup action intended to install it.
if ($InstallOrUpgradeCLI) {
  & $Setup -InstallOrUpgradeCLI
  if ($LASTEXITCODE -ne 0) { throw 'CLI install/update or workspace build failed. No tournament was started.' }
  $SkipSetup = $true
} elseif (!$SkipSetup) {
  & $Setup
  if ($LASTEXITCODE -ne 0) { throw 'Workspace setup/build failed. No tournament was started.' }
  $SkipSetup = $true
}
& python (Join-Path $Root 'scripts\preflight_audit.py') --out 'reports/research-analysis/preflight-before.json'
$PreflightCode = $LASTEXITCODE
if ($PreflightCode -eq 2) { throw 'Preflight blocked the pipeline. Fix blockers in reports/research-analysis/preflight-before.json.' }
& (Join-Path $Root 'Run-Matchmaker.ps1') -Mode $Mode -Coverage $Coverage -Jobs $Jobs -Candidate $Candidate -Opponent $Opponent
if ($LASTEXITCODE -ne 0) { throw 'Matchmaker failed. Do not promote; inspect reports/logs.' }
$Manifest = Get-ChildItem $Root -File -Filter 'matches-*.csv' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$Results = Get-ChildItem (Join-Path $Root 'reports') -File -Filter 'tournament-match-results*.csv' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$Manifest -or !$Results) { throw 'No fresh manifest/results found; refusing to report success.' }
& python (Join-Path $Root 'scripts\verify_tournament.py') --manifest $Manifest.FullName --results $Results.FullName --check-replays --out 'reports/research-analysis/last-verification.json'
if ($LASTEXITCODE -ne 0) { throw 'Tournament verification failed. Results are not eligible for comparison.' }
& python (Join-Path $Root 'scripts\analyze_tournament.py') --input $Results.FullName --candidate $Candidate --out-dir 'reports/research-analysis/latest'
if ($LASTEXITCODE -ne 0) { throw 'Analysis failed. Inspect the error; raw results remain preserved.' }
Write-Host 'Pipeline complete. Reports verified and analyzed. No automatic promotion or ranked submission occurred.' -ForegroundColor Green
