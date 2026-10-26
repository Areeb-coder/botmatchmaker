param(
  [ValidateSet('CandidateVsAll','AllPairs')][string]$Mode = 'CandidateVsAll',
  [ValidateSet('Quick','AllMaps')][string]$Coverage = 'Quick',
  [int]$Jobs = 2,
  [string]$Candidate = 'candidate-bot'
)
$ErrorActionPreference = 'Stop'
$Runner = Join-Path $PSScriptRoot 'scripts\Run-Tournament.ps1'
if (!(Test-Path $Runner)) { throw "Matchmaker script missing: $Runner" }
& $Runner -Mode $Mode -Coverage $Coverage -Jobs $Jobs -Candidate $Candidate
exit $LASTEXITCODE
