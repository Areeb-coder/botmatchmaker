param(
  [ValidateSet('CandidateVsAll','AllPairs')][string]$Mode = 'CandidateVsAll',
  [ValidateSet('Quick','AllMaps')][string]$Coverage = 'Quick',
  [int]$Jobs = 2,
  [string]$Candidate = 'candidate-bot'
)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
if (!(Test-Path (Join-Path $Root 'cambc.toml'))) { throw "Workspace configuration missing: $(Join-Path $Root 'cambc.toml'). Extract the complete workspace ZIP and run this script from that folder." }
$Runner = Join-Path $Root 'scripts\Run-Tournament.ps1'
if (!(Test-Path $Runner)) { throw "Matchmaker script missing: $Runner" }
& $Runner -Mode $Mode -Coverage $Coverage -Jobs $Jobs -Candidate $Candidate
if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
