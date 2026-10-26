param([switch]$InstallOrUpgradeCLI)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
Write-Host "Workspace: $Root" -ForegroundColor Cyan
if (!(Test-Path (Join-Path $Root 'cambc.toml'))) { throw 'Root cambc.toml is missing. Run this script from the extracted kit, not from inside bots/.' }
if ($InstallOrUpgradeCLI) {
  Write-Host 'Installing/upgrading CAMBC CLI...' -ForegroundColor Yellow
  python -m pip install --upgrade cambc
  if ($LASTEXITCODE -ne 0) { throw 'CAMBC CLI install/upgrade failed.' }
}
Write-Host "`nCAMBC version:" -ForegroundColor Cyan
cambc --version
if ($LASTEXITCODE -ne 0) { throw 'CAMBC is not available. Run: python -m pip install --upgrade cambc' }
Write-Host "`nBots discovered:" -ForegroundColor Cyan
$Bots = Get-ChildItem (Join-Path $Root 'bots') -Directory | Where-Object { (Test-Path (Join-Path $_.FullName 'src/main.cpp')) -and (Test-Path (Join-Path $_.FullName 'cambc.toml')) } | Sort-Object Name
if (!$Bots) { throw 'No bot folders found. Expected bots/<bot-name>/src/main.cpp and bots/<bot-name>/cambc.toml.' }
$Bots | ForEach-Object { Write-Host " - $($_.Name)" }
Write-Host "`nPulling current official maps from the workspace root..." -ForegroundColor Cyan
cambc maps pull
if ($LASTEXITCODE -ne 0) { throw 'cambc maps pull failed. Copy the complete error message for troubleshooting.' }
$Maps = Get-ChildItem (Join-Path $Root 'maps') -File -Recurse | Where-Object { $_.Name -like '*.map26a' }
Write-Host "Found $($Maps.Count) .map26a map(s)." -ForegroundColor Green
if ($Maps.Count -eq 0) { throw 'No .map26a maps found after maps pull. Check CLI output and current map publication.' }
New-Item -ItemType Directory -Force (Join-Path $Root 'replays'), (Join-Path $Root 'reports/logs'), (Join-Path $Root 'reports/stats') | Out-Null
Write-Host "`nBuilding each bot..." -ForegroundColor Cyan
$Failed = @()
foreach ($Bot in $Bots) {
  Write-Host "BUILD: $($Bot.Name)" -ForegroundColor Yellow
  $PreviousPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { cambc build $Bot.Name; $BuildCode = $LASTEXITCODE } finally { $ErrorActionPreference = $PreviousPreference }
  if ($BuildCode -ne 0) { $Failed += $Bot.Name; Write-Host "BUILD FAILED: $($Bot.Name)" -ForegroundColor Red }
}
if ($Failed.Count -gt 0) { throw "Build failures: $($Failed -join ', '). Fix these before running tournaments." }
Write-Host "`nSetup complete. All discovered bots built successfully; this does not prove they are competitive." -ForegroundColor Green
Write-Host "Next: .\scripts\Run-Tournament.ps1 -Mode CandidateVsAll -Coverage Quick"
