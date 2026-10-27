# Evidence-driven improvement backlog

**Updated:** 26 October 2026. Treat proposed causes as hypotheses until replay evidence confirms them.

## Existing 420-match local baseline

The included manifest, log, result CSV, and replay files represent a 420-match candidate-vs-all run across 70 maps, three opponents, and both seat orders. The included log ends with `Finished: 420 replay bundles saved, 0 failed`.

| Direct matchup | Candidate-bot record | Candidate-bot win rate |
|---|---:|---:|
| vs Ankv21 | 75–65 | 53.57% |
| vs v20 | 69–71 | 49.29% |
| vs v22 | 74–66 | 52.86% |

Candidate-bot's pooled result is 218/420 (51.90%), but its direct matchup against v20 is a narrow loss. **Do not rank bots by aggregate rates when their appearance counts differ.** Candidate seat results are 104/210 as A (49.52%) and 114/210 as B (54.29%); investigate a possible seat/order effect, but do not assume the cause. Six games per map is a small sample.

## P0 — Current runtime compatibility

1. Run `scripts/preflight_audit.py`.
2. Confirm the installed CAMBC CLI against the current official patch. The supplied Patch 2.0.0 research notes identify CLI 4.0.0.
3. Confirm the `sdk_version` in each bot config is supported; it is not the same field as CLI version. Do not rewrite pins blindly.
4. Run `scripts/Setup-Workspace.ps1` to refresh official maps and build all bots.
5. Rerun all maps and both seats, then verify every planned outcome and replay.
6. Do not treat historic results as current-runtime validation.

## P1 — Replay-review weak maps

| Map | Candidate-bot wins | Initial hypotheses to test, not confirmed causes |
|---|---:|---|
| `00packs` | 0/6 | Route/camp choice, unsafe approach, delayed objective switching |
| `00monolith` | 1/6 | High-value camp risk, carrier route, survival/tribute timing |
| `01broken-curve` | 1/6 | Narrow-path navigation, failed moves, stale target |
| `01elk-arena` | 1/6 | Camp choice, combat/economy timing |
| `01hooks` | 1/6 | Path geometry, repeated target selection |
| `02famine` | 1/6 | Resource scarcity and risk-adjusted reward |
| `02treasure-box` | 1/6 | Objective timing and contested camps |
| `adgato-gemcombat2` | 1/6 | Combat exposure and carrier survival |

For each, inspect all six replays. Record failed moves, position progress, target switches, camp clear/capture duration, gem-carrier deaths, missed tribute opportunities, invalid actions, and fuel issues. Only then select one causal hypothesis.

## P2 — Target expiry / stuck detection

The public Bird Business repository describes target expiry after non-progress. This is a design lead only; license was not verified. The existing bot already has nontrivial pathing, so first measure the actual stuck cases. If implemented, the watchdog should track position and target identity over a bounded window, exclude intentional stationary actions (combat/channeling), recheck reachability/danger, and emit telemetry. Compare a single isolated candidate against the incumbent and reject if it harms strong maps.

## P3 — Risk-adjusted camp value

The current project already caches camp metadata, uses distance fields, selects objectives by reward, and coordinates through shared memory. Avoid duplicating these mechanisms. Test a model such as expected net value divided by travel + clear + capture/channel + return-to-tribute time, adjusted for expected damage, contest risk, carrier safety, early-game gold value, and teammate target claims. Tune on training maps and validate on held-out maps.

## P4 — Patch 2.0.0 shop/emblem candidate

`bots/candidate-bot/PATCH_2.0.0_UPGRADE_NOTES.md` documents a focused candidate with distinct emblem families, capture-focused builds for designated carriers, corrected stage accounting, optional altar-lock spending after affordable shards, and corrected Golem/cluster estimates. This packaging step did not compile it. Rebuild on the official current runtime before treating its performance as current-patch evidence.

## P5 — Draft and role strategy

The dpark C++ repo is the best direct external research lead found, but its SDK differs and a root license was not verified. Do not copy its source. Independently test ideas such as map-aware draft scoring, role coverage, explicit capturer selection, opponent style buckets, and per-map results. Keep draft-only changes isolated from combat/economy changes.

## Promotion rule

No automatic source overwrite or ranked submission. A candidate must build, complete the planned benchmark, show no faults/invalid actions/fuel violations, improve paired performance under a predeclared test plan, avoid unacceptable map regressions, and preserve logs/manifests/replays/source hashes and the prior incumbent.
