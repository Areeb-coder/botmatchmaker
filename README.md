# Cambridge Battlecode 2026 — Local Bot Matchmaker (Windows)

This workspace runs local CAMBC matches to compare the bot folders under `bots/` against maps under `maps/`. It does **not** submit bots to the competition.

## Quick start

1. Extract the complete workspace ZIP into a folder of your choice.
2. Open PowerShell in the extracted `bestbot-workspace` folder — the folder that contains `cambc.toml` and `Run-Matchmaker.ps1`.
3. If PowerShell blocks a script because of the execution policy, run this first in the same PowerShell window. This allows scripts to run in that window only:

   ```powershell
   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
   ```

4. Run a quick smoke test:

   ```powershell
   .\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage Quick
   ```

5. Run the full map pool:

   ```powershell
   .\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage AllMaps -Jobs 2
   ```

To compare every bot against every other bot on every map, with both team sides:

```powershell
.\Run-Matchmaker.ps1 -Mode AllPairs -Coverage AllMaps -Jobs 2
```

`Quick` uses the first map alphabetically. `AllMaps` uses every `.map26a` file discovered under `maps/`. `Jobs` controls CAMBC batch parallelism; use `-Jobs 1` if your computer struggles.

## Existing files: rematch, replace, or preserve

Before starting, the tournament runner checks for replay files that would conflict with the planned run and for the standard report files. If existing outputs are found, it prompts you to choose:

- **1 — Rematch and REPLACE matching outputs.** Removes only the exact replay files that conflict with this planned run after you confirm this option. The standard CSV/Excel reports are regenerated and replace their previous versions after the tournament finishes.
- **2 — Rematch and CREATE NEW files.** Adds a timestamp to replay names and creates a separate timestamped match plan, manifest, log, CSV reports, and Excel workbook. Previous outputs are preserved.
- **3 — Cancel.** Exits before starting the tournament; existing outputs are left alone.

Choose option 2 when you want to keep the previous results for comparison. Do not manually delete your old replay or report files just to start a new run.

If no conflicting replay or standard report files are detected, the runner proceeds with a new run automatically.

## First-time setup

Check that CAMBC is available:

```powershell
cambc --version
```

If it is not recognized, install or update the CLI:

```powershell
python -m pip install --upgrade cambc
```

Then run the workspace setup script:

```powershell
.\scripts\Setup-Workspace.ps1
```

To explicitly install or upgrade CAMBC during setup:

```powershell
.\scripts\Setup-Workspace.ps1 -InstallOrUpgradeCLI
```

The setup script may build/check configured bots and refresh maps. If a bot build fails, fix that bot before treating its tournament results as meaningful.

## Bots and maps

The matchmaker automatically discovers bot folders under `bots/` that contain both:

- `cambc.toml`
- `src/main.cpp`

It discovers maps with the `.map26a` extension under `maps/` (including subfolders).

To add a bot, create or copy a properly configured CAMBC bot folder under `bots/`, then build it from the workspace root:

```powershell
cambc build my-new-bot
```

To compare it against all other discovered bots:

```powershell
.\Run-Matchmaker.ps1 -Mode CandidateVsAll -Candidate my-new-bot -Coverage AllMaps -Jobs 2
```

To include it in the full round-robin:

```powershell
.\Run-Matchmaker.ps1 -Mode AllPairs -Coverage AllMaps -Jobs 2
```

## What the matchmaker does

The automated runner:

- discovers configured bots and available maps;
- creates a match-plan `.txt` and CSV manifest for each run;
- schedules both team-side orders for each required pairing/map;
- runs `cambc run-batch` locally and captures a log;
- saves replays under `replays/`;
- parses actual winner records from the log, with UTF-8/UTF-16 log decoding support;
- validates parsed outcomes against the manifest and warns when results are incomplete or unmatched;
- creates CSV reports and, when `openpyxl` is installed, an Excel workbook.

The root launcher is `Run-Matchmaker.ps1`; the underlying tournament runner is `scripts/Run-Tournament.ps1`.

## Reports

Reports are written under `reports/`:

- `tournament-standings.csv` — matches, wins, losses, win percentage, and wins by seat;
- `tournament-match-results.csv` — parsed outcome for each match;
- `candidate-head-to-head.csv` — candidate results against each opponent;
- `candidate-by-map.csv` — candidate results by map;
- `tournament-report.xlsx` — workbook with Standings, Head-to-head, By-map, and Match results sheets, if Python's `openpyxl` package is installed.

When you choose **option 2**, these reports receive a run-specific suffix so the previous reports remain available. CSV files are the basic report format; the Excel workbook is optional.

If Excel output is missing, install `openpyxl`:

```powershell
python -m pip install openpyxl
```

If the parser reports partial or unmatched results, inspect the validation counts and log before relying on the win percentages. Missing match outcomes are not automatically counted as losses.

## Re-create reports without replaying matches

Use the manifest and log from the run you want to analyze:

```powershell
python .\scripts\summarize_tournament_log.py --manifest .\matches-candidate-vs-all-AllMaps-YYYYMMDD-HHMMSS.csv --log .\reports\logs\candidate-vs-all-AllMaps-YYYYMMDD-HHMMSS.log
```

Replace the example filenames with the actual files. To let the script choose the newest compatible manifest and log:

```powershell
python .\scripts\summarize_tournament_log.py
```

To create a separate set of reports while preserving the standard report filenames, pass an output suffix:

```powershell
python .\scripts\summarize_tournament_log.py --manifest .\matches-<run>.csv --log .\reports\logs\<run>.log --output-suffix manual-analysis
```

## Inspect a replay

Use an actual replay path from the manifest or log:

```powershell
cambc view replays\<actual-replay-filename>.repl26a
```

## Troubleshooting

**`root cambc.toml not found`**

You are likely running the script from the wrong folder or using an incomplete extraction. Change to the folder that contains `cambc.toml` and `Run-Matchmaker.ps1`, then check:

```powershell
Test-Path .\cambc.toml
Test-Path .\Run-Matchmaker.ps1
Test-Path .\scripts\Run-Tournament.ps1
```

All three should return `True`.

**`output already exists`**

The runner found a replay-name collision. Run the matchmaker again and choose option 1 to replace the conflicting planned replay files, option 2 to create new timestamped outputs, or option 3 to cancel.

**A bot is not discovered**

Confirm its folder is under `bots/` and contains both `cambc.toml` and `src/main.cpp`.

**`run-batch` fails**

Read the log path printed by the runner and inspect the first CAMBC error. A failed batch must not be treated as a completed tournament.

## Important notes

- Compare bots on the same maps and both team sides; one match is not enough to judge a bot.
- Local win percentage is not official ladder Elo and can be affected by map selection, seeds, and match conditions.
- This is a local testing tool only. It does not upload or submit bots.
