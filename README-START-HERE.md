# Cambridge Battlecode 2026a — Complete Local Bot Matchmaker & Research Workspace

This is a **local-only** Windows workspace for comparing four C++ bot variants across the included CAMBC map pool. It does not submit bots to the competition and does not automatically promote or overwrite a bot.

## Included
- `bots/`: `candidate-bot`, `Ankv21`, `v20`, `v22`, source and per-bot configs.
- `maps/`: 70 `.map26a` maps (refresh from the current official CLI before new benchmarks).
- `replays/`: historical replay bundles.
- `matches-*.csv/.txt`: match manifests and plans.
- `reports/`: raw parsed outcomes, standings, head-to-head/per-map reports, XLSX workbook, and logs.
- `bot-archives/`: preserved bot ZIP snapshots.
- `scripts/`: setup, matchmaker, result parsing, plus new preflight, verification, and analysis utilities.
- `research/`: repository audit, baseline, pipeline design, and improvement backlog.

## Existing local baseline — not proof of current-runtime strength

The included historical run has 420 outcomes over 70 maps, three opponents, and both seat orders:
- candidate-bot vs Ankv21: 75–65 (53.57% candidate win rate)
- candidate-bot vs v20: 69–71 (49.29%; v20 narrowly wins)
- candidate-bot vs v22: 74–66 (52.86%)
- candidate-bot aggregate: 218/420 wins (51.90%) against this opponent pool

Do not conclude candidate-bot is the strongest bot from the pooled rate. The direct comparison against v20 is a narrow loss; map samples are only six games each. The CLI/SDK must be refreshed and verified before treating historic results as current-patch evidence. See `research/IMPROVEMENT_BACKLOG.md`.

## Quick start (PowerShell)
Open PowerShell in this extracted folder, containing root `cambc.toml`.

```powershell
# Read-only environment audit
python .\scripts\preflight_audit.py

# Install/update CLI, pull official maps, build all configured bots
.\scripts\Setup-Workspace.ps1 -InstallOrUpgradeCLI

# Test ONE specific bot against ONE specific bot on the first map, both seat orders
.\Run-Matchmaker.ps1 -Mode CandidateVsOpponent -Candidate candidate-bot -Opponent v20 -Coverage Quick

# Test the same selected pair on ALL maps, both seat orders
.\Run-Matchmaker.ps1 -Mode CandidateVsOpponent -Candidate candidate-bot -Opponent v20 -Coverage AllMaps -Jobs 2

# Quick smoke test against every other configured bot (not a single-pair test)
.\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage Quick -Candidate candidate-bot

# Full map-pool test
.\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage AllMaps -Jobs 2 -Candidate candidate-bot

# All-pairs round-robin
.\Run-Matchmaker.ps1 -Mode AllPairs -Coverage AllMaps -Jobs 2

# Connected preflight → setup/build → match → verify → analyze
.\Run-Research-Pipeline.ps1 -Mode CandidateVsAll -Coverage AllMaps -Jobs 2 -Candidate candidate-bot
```

If outputs collide, the existing runner asks whether to replace matching files, create timestamped outputs, or cancel. Use `-Jobs 1` if your machine struggles.

## Analyze the included historical run without replaying matches
```powershell
python .\scripts\verify_tournament.py --manifest .\matches-candidate-vs-all-AllMaps-20261026-162935.csv --results .\reports\tournament-match-results.csv --check-replays
python .\scripts\analyze_tournament.py --input .\reports\tournament-match-results.csv --candidate candidate-bot
```

The verifier checks manifest/outcome consistency and replay existence. The analyzer writes additional reports under `reports/research-analysis/`, preserving original results.

## Next improvements
1. Verify the official CLI version and supported SDK pin; CLI version and `sdk_version` are distinct.
2. Build all bots on the current toolchain.
3. Review weak map replays (`00packs`, `00monolith`, `01broken-curve`, `01elk-arena`, `01hooks`, `02famine`, `02treasure-box`, `adgato-gemcombat2`).
4. Inspect evidence for path stalls, unsafe camp engagement, carrier deaths, capture timing and missed tributes before editing strategy.
5. Change one strategy dimension at a time and benchmark on the same maps and both seats.

## Validation boundaries
- The included log reports 420 outcomes and 420 replay bundles saved with zero failed runs in that historical run.
- The packaging step does not run CAMBC or rebuild C++ source.
- External repo code with unverified licenses is not copied.
- All matches are local. No ranked upload or automatic promotion occurs.


## One-specific-bot-versus-one-specific-bot mode

`Run-Matchmaker.ps1` now accepts `-Mode CandidateVsOpponent -Candidate <bot-folder> -Opponent <bot-folder>`. It generates only the selected pair, on the chosen map coverage, with both seat orders. Example:

```powershell
# Candidate-bot vs v20 on the first map, both seat orders
.\Run-Matchmaker.ps1 -Mode CandidateVsOpponent -Candidate candidate-bot -Opponent v20 -Coverage Quick

# Candidate-bot vs v20 across every map, both seat orders
.\Run-Matchmaker.ps1 -Mode CandidateVsOpponent -Candidate candidate-bot -Opponent v20 -Coverage AllMaps -Jobs 2

# Compare two other bots directly
.\Run-Matchmaker.ps1 -Mode CandidateVsOpponent -Candidate Ankv21 -Opponent v22 -Coverage AllMaps -Jobs 2
```

`Quick` creates two matches (A/B seat order) on one map. `AllMaps` creates two matches per map. It validates that both bot names exist and rejects using the same bot as both sides. Match plans and replays are timestamped/preserved using the existing collision-handling behavior.
