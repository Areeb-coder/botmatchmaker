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
