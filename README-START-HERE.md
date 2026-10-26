# Cambridge Battlecode — Local Bot Testing Kit (Windows)

This kit was prepared from your uploaded `battlebot.zip`. It contains **four actual bot folders**: `Ankv21`, `candidate-bot`, `v20`, and `v22`. The ZIP archives that were inside `bots/` are preserved under `bot-archives/`; source hashes show they are snapshots of those same four bots, not four additional unique bots.

No bot was compiled or match-tested during preparation. Maps are downloaded on your computer by the official CAMBC CLI so you get the map pool currently published by the competition.

## 1. Extract the kit

Extract `Battlecode_Local_Test_Kit.zip`. Open the extracted `bestbot` folder. Its root must contain:

- `cambc.toml`
- `bots\`
- `maps\`
- `scripts\`
- `replays\`
- `reports\`

**Run every command below from this `bestbot` root**, not from `bots\candidate-bot`.

## 2. Open PowerShell in the correct root

In File Explorer, open the extracted `bestbot` folder, click the address bar, type `powershell`, and press Enter. Verify:

```powershell
Get-Location
Get-ChildItem -Force
Get-Content .\cambc.toml
```

The root config sets `bots_dir = "bots"` and `maps_dir = "maps"`, so bot names resolve to folders under `bots\`.

## 3. Install/update CAMBC only if needed, pull maps, and build all bots

If `cambc --version` works already, you can run setup without upgrading:

```powershell
.\scripts\Setup-Workspace.ps1
```

If `cambc` is not installed or you want the current release, run:

```powershell

```

This script:
1. checks the workspace root and CLI;
2. lists the four bot folders;
3. runs `cambc maps pull` from the correct root;
4. checks for `.map26a` files;
5. runs `cambc build` for every bot and stops if any build fails.

If `maps pull` fails, read the full error. Do not create an HTML file. If the CLI says no map pool is published, verify the current competition release and download page.

## 4. Fast test: candidate vs every other bot on one map, both sides

```powershell
.\scripts\Run-Tournament.ps1 -Mode CandidateVsAll -Coverage Quick
```

This uses the first map alphabetically and runs `candidate-bot` against `Ankv21`, `v20`, and `v22`, in both team orders. It writes a match plan, CSV manifest, console log, and replay bundles. It does not upload anything or enter ranked matches.

## 5. Candidate vs all other bots on every downloaded map, both sides

```powershell
.\scripts\Run-Tournament.ps1 -Mode CandidateVsAll -Coverage AllMaps -Jobs 2
```

Use `-Jobs 1` if your computer struggles. This is a larger run. Each map is tested with candidate as team A and team B against each opponent.

## 6. Full round-robin: every bot vs every other bot

```powershell
.\scripts\Run-Tournament.ps1 -Mode AllPairs -Coverage AllMaps -Jobs 2
```

This is the run that compares all four bot folders against one another. It runs every pair on every map with both team orders. For four bots, there are six unique pairs; with two side orders and M maps, the batch contains `12 × M` matches.

## 7. Generate statistics JSON for a win-rate report

The batch runner is best for many parallel replays. For per-match final statistics, use the statistics runner. First do a quick test:

```powershell
.\scripts\Run-Stats-Tournament.ps1 -Mode CandidateVsAll -Coverage Quick
python .\scripts\summarize_stats.py
```

Then, if the first run works, run all maps:

```powershell
.\scripts\Run-Stats-Tournament.ps1 -Mode CandidateVsAll -Coverage AllMaps
python .\scripts\summarize_stats.py
```

For a full all-pairs stats tournament:

```powershell
.\scripts\Run-Stats-Tournament.ps1 -Mode AllPairs -Coverage AllMaps
python .\scripts\summarize_stats.py
```

The statistics summarizer only counts a winner if the actual JSON exposes a recognized winner field. If the CLI's statistics schema uses a different shape, it marks the result `UNKNOWN` rather than inventing a win/loss. In that case, open one `reports\stats\stats-*.json` and share it to adapt the parser. The detailed report is written to `reports\stats\summary-latest.csv`.

`-MaxMatches 4` can limit a stats run for a quick smoke check, e.g.:

```powershell
.\scripts\Run-Stats-Tournament.ps1 -Mode CandidateVsAll -Coverage AllMaps -MaxMatches 4
```

## 8. View a replay

Copy a replay path from the tournament log or manifest, then run:

```powershell
cambc view replays\<actual-replay-filename>.repl26a
```

Replace the placeholder with the real filename. Inspect repeated idle turns, failed paths, unfinished captures, lost gems, poor purchases, and combat choices.

## 9. One direct local match command

After maps are downloaded and builds pass:

```powershell
cambc run candidate-bot v22 --map maps\<actual-map-name>.map26a --view
```

Replace `v22` and the map placeholder as needed. This is a local match; it does not submit the bot.

## 10. What counts as evidence that a bot is better?

- Do not judge from a single match.
- Compare the same maps and both team orders.
- Review per-map results; a good average can hide a severe map regression.
- Treat a small win-rate difference as uncertain and run more games.
- Inspect failed matches and replays, not only winners.
- A local win rate is not the same as official ladder Elo.
- No script in this kit submits a bot. Submission must be a separate, deliberate action.

## Troubleshooting

- **`cambc` not recognized:** run `python -m pip install --upgrade cambc`, reopen PowerShell, and check `cambc --version`.
- **`cambc maps pull` cannot find the workspace:** verify PowerShell is in the directory containing the root `cambc.toml`, not inside `bots\candidate-bot`.
- **No maps found:** check `cambc maps pull` output and `Get-ChildItem .\maps -Recurse -File`.
- **Build fails for one bot:** save the complete compiler output; do not compare a candidate that has not built successfully.
- **Batch fails:** open the latest `reports\logs\*.log` and the generated `matches-*.txt`. Batch paths are relative to the match file.
- **Stats show UNKNOWN:** inspect one JSON file and provide it for a schema-specific parser update; UNKNOWN is not a loss.
