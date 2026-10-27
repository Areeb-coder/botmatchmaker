# Research → build → benchmark → review pipeline

## Included foundation
- Four configured C++ bots (`candidate-bot`, `Ankv21`, `v20`, `v22`).
- 70 `.map26a` maps.
- Historical match manifests, result logs, CSV/XLSX reports, and replay bundles.
- Existing PowerShell setup and candidate-vs-all/all-pairs matchmaker.
- Existing result parser with planned-versus-parsed outcome checks.

## Added safeguards and analysis
- `scripts/preflight_audit.py`: read-only runtime, config, SDK-pin, bot and map checks.
- `scripts/verify_tournament.py`: fail-closed manifest/result/replay consistency checks.
- `scripts/analyze_tournament.py`: head-to-head, per-map, seat split and Wilson intervals; does not change raw results or bot sources.
- `Run-Research-Pipeline.ps1`: orchestrates preflight, setup/build, matchmaker, verification and analysis; never uploads or auto-promotes.
- `research/IMPROVEMENT_BACKLOG.md`: replay-first map triage and testable hypotheses.

## Windows commands
```powershell
python .\scripts\preflight_audit.py
.\scripts\Setup-Workspace.ps1 -InstallOrUpgradeCLI
.\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage Quick -Candidate candidate-bot
.\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage AllMaps -Jobs 2 -Candidate candidate-bot
.\Run-Matchmaker.ps1 -Mode AllPairs -Coverage AllMaps -Jobs 2
.\Run-Research-Pipeline.ps1 -Mode CandidateVsAll -Coverage AllMaps -Jobs 2 -Candidate candidate-bot
python .\scripts\verify_tournament.py --manifest .\matches-candidate-vs-all-AllMaps-20261026-162935.csv --results .\reports\tournament-match-results.csv --check-replays
python .\scripts\analyze_tournament.py --input .\reports\tournament-match-results.csv --candidate candidate-bot
```

## Safety
No external repository code is copied into the bots. The preflight does not mutate SDK pins. Historical results are not retroactively certified for the current runtime. Promotion and ranked submission remain manual.


## One selected bot versus one selected bot

```powershell
# First map, both seat orders
.\Run-Matchmaker.ps1 -Mode CandidateVsOpponent -Candidate candidate-bot -Opponent v20 -Coverage Quick

# Full map pool, both seat orders
.\Run-Matchmaker.ps1 -Mode CandidateVsOpponent -Candidate candidate-bot -Opponent v20 -Coverage AllMaps -Jobs 2
```
