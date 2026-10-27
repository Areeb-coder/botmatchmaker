# Bot inventory from uploaded archive

Four actual C++ bot directories were found and included in the tournament. ZIP files originally stored inside `bots/` were moved to `bot-archives/` so the CLI does not mistake them for bot directories. Their source files match the corresponding folder snapshots as follows (SHA-256 prefixes of `src/bot.cpp`):

| Bot name | `src/bot.cpp` SHA-256 prefix | Archive correspondence |
|---|---|---|
| `Ankv21` | `88c5f51cb8` | `archive-submission-v21.zip` |
| `candidate-bot` | `b18d2e5de6` | `archive-candidate-patch-2.0.0.zip` |
| `v20` | `b95e1871f3` | `archive-submission-fixed.zip` |
| `v22` | `b5e2843bbc` | `archive-patch-updated.zip` |

All four are distinct in `src/bot.cpp`; some shared headers/entry-point files have identical hashes. The archive correspondence was verified by comparing source-file hashes, not by running matches. The `Ankv21` and archive `submission-v21` snapshots were missing per-bot `cambc.toml`, so matching SDK metadata (`sdk_version = "2.1.6"`, languages C++/Python) was added for local testing.

**No bot was compiled or match-tested while preparing this kit.** The kit provides scripts to do that on your Windows machine with the current official CLI and downloaded maps.


## Historical local match evidence (26 October 2026)

The full matchmaker package contains the manifest `matches-candidate-vs-all-AllMaps-20261026-162935.csv`, the corresponding log, `reports/tournament-match-results.csv`, and replay bundles. These artifacts contain 420 matched outcomes across 70 maps, three opponents, and both seat orders. The included log ends with `Finished: 420 replay bundles saved, 0 failed`.

| Direct matchup | Candidate-bot record | Candidate-bot win rate |
|---|---:|---:|
| vs Ankv21 | 75–65 | 53.57% |
| vs v20 | 69–71 | 49.29% |
| vs v22 | 74–66 | 52.86% |

The direct records are more useful than comparing aggregate rates, because the bots played different numbers of matches. Candidate-bot's result is mixed; v20 narrowly wins the direct series. These are historic local results, not official Elo and not fresh current-runtime verification.
