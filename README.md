# Cambridge Battlecode 2026 — Local Bot Matchmaker (Windows)

This is a local-only matchmaker for comparing bot folders against the maps in `maps/`. It does **not** submit bots to the competition. It includes the bot sources currently in `bots/`, the map pool, CAMBC workspace configuration/SDK files, previous match plans/logs/replays, and the fixed automation/reporting scripts.

## Quick start

Open PowerShell in this extracted folder (the folder containing `cambc.toml`). If CAMBC is already installed and maps/builds are ready, run:

```powershell
.\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage Quick
```

For a full map-pool run:

```powershell
.\Run-Matchmaker.ps1 -Mode CandidateVsAll -Coverage AllMaps -Jobs 2
```

To compare **every bot against every other bot** on all maps, with both team sides:

```powershell
.\Run-Matchmaker.ps1 -Mode AllPairs -Coverage AllMaps -Jobs 2
```

`Quick` tests the first map alphabetically. `AllMaps` tests every `.map26a` file under `maps/`. `Jobs` controls CAMBC batch parallelism; use `1` if your computer struggles.

## First-time setup

If `cambc --version` is not recognized, install the CLI:

```powershell
python -m pip install --upgrade cambc
```

Then build/check all bots and refresh the map pool:

```powershell
.\scripts\Setup-Workspace.ps1
```

If you explicitly want to upgrade CAMBC as part of setup:

```powershell
.\scripts\Setup-Workspace.ps1 -InstallOrUpgradeCLI
```

## Add another bot later

1. Create or copy a bot folder under `bots/`, for example `bots\my-new-bot\`.
2. Make sure it has the expected CAMBC bot structure: `bots\my-new-bot\cambc.toml` and `bots\my-new-bot\src\main.cpp` (plus its other source/header files).
3. Build it from this workspace root:

   ```powershell
   cambc build my-new-bot
   ```

4. It is discovered automatically on the next run; no script editing is required. To test the new bot against all other discovered bots, set it as the candidate:

   ```powershell
   .\Run-Matchmaker.ps1 -Mode CandidateVsAll -Candidate my-new-bot -Coverage AllMaps -Jobs 2
   ```

   To include it in the complete round-robin, use `-Mode AllPairs`.

Only folders that contain both `src/main.cpp` and `cambc.toml` are treated as bots. Keep map files under `maps/` with the `.map26a` extension.

## What happens automatically

The matchmaker:

- discovers bot folders automatically;
- discovers `.map26a` maps automatically;
- creates a unique match-plan `.txt` and manifest `.csv` for each run;
- runs local matches and saves the console log and replay bundles;
- parses actual winner lines from the log and creates result reports;
- detects UTF-16 and UTF-8 log encodings (the Windows PowerShell/CAMBC log encoding issue is fixed);
- validates that planned matches and parsed results line up, rather than silently treating missing outcomes as losses.

After a successful tournament, these reports are refreshed under `reports/`:

- `tournament-standings.csv` — wins/losses and win percentage per bot;
- `tournament-match-results.csv` — each parsed match outcome;
- `candidate-head-to-head.csv` — candidate results versus each opponent;
- `candidate-by-map.csv` — candidate results by map;
- `tournament-report.xlsx` — Excel workbook with Standings, Head-to-head, By-map, and Match results sheets (when Python's `openpyxl` is installed).

The CSV files are the primary report format. If the parser says results are partial or unmatched, inspect the reported validation counts and the log before using the win rate.

## Re-run analysis without replaying matches

To re-create reports from a particular existing manifest and log, use:

```powershell
python .\scripts\summarize_tournament_log.py --manifest .\matches-candidate-vs-all-AllMaps-YYYYMMDD-HHMMSS.csv --log .\reports\logs\candidate-vs-all-AllMaps-YYYYMMDD-HHMMSS.log
```

Replace the placeholders with the real filenames. To let the parser choose the newest compatible manifest and log:

```powershell
python .\scripts\summarize_tournament_log.py
```

For a statistics-JSON-based run (separate from the batch matchmaker), use:

```powershell
.\scripts\Run-Stats-Tournament.ps1 -Mode CandidateVsAll -Coverage Quick
python .\scripts\summarize_stats.py
```

The statistics runner depends on the installed CAMBC CLI's `--statistics` output format. Its summarizer marks unrecognized winner schemas as `UNKNOWN`, never as a loss. The regular matchmaker's log-based report is the tested path for the included batch results.

## Inspect a replay

Use the replay path from a match manifest/log, for example:

```powershell
cambc view replays\<actual-replay-filename>.repl26a
```

## Important notes

- Compare bots on the same maps and both team sides; do not judge a bot from one match.
- A local win rate is not official ladder Elo and can be affected by map selection, seeds, and match conditions.
- Matchmaker scripts only run local tests. They do not upload or submit anything.
- If a build fails, fix that bot before trusting the comparison.
