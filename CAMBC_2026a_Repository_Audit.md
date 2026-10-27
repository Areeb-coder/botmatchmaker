> **Scope note for this bundled workspace:** The original audit inspected `Cambridge-Battlecode-main(1).zip`, a source snapshot. It correctly notes that *that source snapshot* lacked maps, replay bundles, and result exports. This delivery is based on the separate `Battlecode_Final_Matchmaker_Complete.zip`, which includes a 70-map pool, 420 recorded match outcomes, 420 replay bundles, logs, and CSV/XLSX reports. The CAMBC CLI is not installed in this packaging environment, so no fresh build or match was run while preparing this ZIP.

# Cambridge Battlecode 2026a — Repository and Improvement-Pipeline Audit

**Research date:** 26 October 2026  
**Primary source inspected:** `Cambridge-Battlecode-main(1).zip` (711,527 bytes; 82 ZIP entries; 66 regular files)  
**Purpose:** identify the strongest evidence-supported external foundation, audit the current project, and define a safe improvement pipeline.  
**Validation boundary:** this environment does not contain the CAMBC CLI, maps, replay bundles, or current benchmark CSV/XLSX files. No new build or match was run in this audit.

## Executive findings

### Main conclusion

**The strongest directly relevant external bot candidate found is `dpark2380/cambridge-battlecode`, especially its Daniel v13 line.** It is explicitly a C++ bot project for `cambc2026a`; its repository documents iterative strategy versions, local league testing, map-specific fixes, and a current SDK pin of 4.0.0. Its public version index reports a sequence of measured local comparisons, including v13 improvements on a specific map and non-regression screens. Those numbers are repository-authored evidence, not independently reproduced here. The root `LICENSE` path was not found in direct lookup; therefore, **do not copy or port its code until its licensing status is verified or permission is obtained**.

**It is not yet proven that this bot is stronger than the supplied project.** The external repo reports results on its own opponent pools and maps; the supplied archive has no map files, replay bundles, current result CSVs, or runnable CAMBC toolchain with which to perform a matched head-to-head. Its SDK 4.0.0 also differs from the supplied root `cambc.toml` (`sdk_version = "2.1.6"`). Treat its strategy as a research lead, not a replacement to promote.

The highest-value immediate work is not wholesale bot replacement. It is:
1. resolve which local bot is actually the incumbent;
2. refresh and pin the official SDK, CLI, maps, and rules together;
3. preserve the existing experiment history and local league scripts;
4. add a fail-closed preflight audit and candidate promotion gate;
5. rerun matched, two-sided, multi-map benchmarks on the current toolchain before changing the incumbent.

### What was actually inspected

The supplied ZIP is a **bot/source-and-analysis snapshot**, not a self-contained runnable matchmaker. It contains:
- two substantial C++ bot trees: `bots/tungtungsahur` and `bots/tripytropa`;
- current and older API exports (`BotAPICpp (1).md` and `BotAPICpp.md`) and `GameRules.md`;
- long experiment and replay-analysis notes (`HANDOFF.md`, `PLAN.md`, `REPORT.md`, `analysis/*.md`);
- existing local analysis and league utilities in `tools/`;
- six agent skill files under `.claude/skills/`.

It does **not** contain:
- `cambc.exe` or a discoverable `cambc` executable;
- any `.map26a` maps;
- any `.repl26a` replay bundles;
- benchmark CSV/XLSX files;
- a `.github/workflows/` directory or an autonomous code-improver workflow.

The archive passes ZIP integrity testing, but that proves only archive readability.

## 1. Current-project audit

### Structure and existing capabilities

| Area | Evidence in the archive | Assessment |
|---|---|---|
| Bot source | `bots/tungtungsahur/src/{main.cpp,bot.cpp,bot.h,team.h,draft.cpp}` and corresponding `tripytropa` files | Substantial C++ strategy code exists; do not replace it without matched tests. |
| Decision logic | `bot.cpp` includes cached camp-group metadata, objective selection, retreat/banking, champion role turns, combat, shop decisions, and shared-memory coordination | Much more than a starter bot; many of the same broad strategy components exist in external candidates. |
| Navigation | Uses CAMBC `distance_field`/bitboards and cached blocked-tile/group data | Existing pathfinding is already nontrivial. Stuck detection and movement failure handling need current-runtime verification, not assumptions. |
| Map adaptation | Several map-gated rules and map-specific notes in source and experiment logs | Retain the idea of map-specific policy only when supported by broad regression tests; beware overfitting. |
| Evaluation | `tools/league.py`, `tools/direct_league.py`, `tools/eval.py`, `tools/race.py`, `tools/scout.py`, `tools/deaths.py`, `tools/errors.py`, `tools/moves.py` | A useful local evaluation and diagnostics foundation already exists. |
| Experiment history | `bots/tungtungsahur/PLAN.md`, `REPORT.md`, `HANDOFF.md`, `analysis/tripytropa-stage-20261022.md` and map/opponent reviews | Valuable historical evidence; keep rejected variants and their results. |
| Runtime/build | `cambc.toml` pins SDK 2.1.6; `CLAUDE.md` says the local header/runner is older than the 21 October handbook/API exports | **Major compatibility blocker.** Historical league results cannot be treated as current-rules validation. |
| Reproducible inputs | No maps, replays, benchmark exports, or CLI in this ZIP | No new match can be reproduced from this archive alone. |
| Automated improvement | No CI/workflow directory or autonomous improver in this ZIP | The experiment process is documented and scripted, but no connected automatic research → patch → benchmark → promotion workflow is present in this archive. |

### Conflicting incumbent descriptions

The archive has an important documentation inconsistency:
- `CLAUDE.md` says the bot being developed is `bots/tungtungsahur`.
- `HANDOFF.md` is centred on `tungtungsahur`, but its status text is dated 19 October.
- `bots/tripytropa/README.md` calls tripytropa a fixed-roster candidate and says the 23 October edits were not yet built/tested.
- `analysis/tripytropa-stage-20261022.md` says “Production remains `bots/tripytropa`” and reports a full local sweep.

These statements cannot all identify the same current incumbent. Before any experiment, select the actual incumbent by comparing the currently submitted source/ZIP checksum and the intended production folder. Do not infer that `tripytropa` or `tungtungsahur` is current solely from these notes.

### Existing bot ideas worth retaining

Static inspection of `bots/tungtungsahur/src/bot.cpp` shows:
- cached `GroupInfo` records to avoid repeatedly querying every camp group;
- a distance-field-based movement layer;
- camp selection using reward, travel/clear/capture cost and early-game gold value;
- banking/altar routing and a safety-aware route attempt;
- a shared-memory protocol for objective, anchor, fight alarm and danger alarm;
- role-specific champion behaviour and map-gated rules;
- stdout telemetry (`SCORE` and `SCOUT`) consumed by the local analysis scripts.

These are not reasons to assume the bot is strong; they are reasons to preserve the existing engineering investment while testing one change at a time.

### Demonstrated concerns versus hypotheses

**Directly supported by the supplied files**
- The root SDK pin is 2.1.6.
- `analysis/game-update-20261021-strategy.md` explicitly says the 21 October handbook/API are newer than the local SDK/header and warns that older match results are not current-rule validation.
- The current archive contains no map files, replay bundles, or CLI executable.
- The project docs disagree about the production bot.
- Existing local results in the notes are from older rule/toolchain conditions.

**Likely risks that still require a current build/replay to confirm**
- Any hard-coded camp stats or patch-sensitive mechanics may now be stale.
- Map-specific routing rules may regress under updated camp types, rewards, or champion values.
- Some historical “best” candidates may be inferior under the current patch.
- A local proxy win rate may not transfer to strong live opponents.

## 2. External repository search log

**Sources accessed on 26 October 2026:** GitHub repository/code search and file fetches; TinyFish public-web search; Firecrawl public-web search; official competition homepage search result; the uploaded project’s own research notes. This is a bounded search, not a claim to have searched the entire internet.

### Query families used

- `Cambridge Battlecode 2026a`
- `Cambridge Battlecode`
- `CAMBC 2026`
- `cambc2026a`
- `Cambridge Battlecode 2026 C++`
- `CAMBC 2026 bot`
- `Cambridge Battlecode 2026a GitHub bot repository`
- `site:github.com "cambc" "2026a" bot pathfinding camp capture`
- `tribute_gems` and `distance_field::compute_to` within the dpark repository
- targeted fetches of README, strategy source, evaluation scripts, SDK pins, and merge-gate documentation.

### Coverage and limitations

- At least 8 distinct candidate/repository names appeared across the search results.
- Deep file inspection was performed on the dpark C++ project, FHL Tarot Club project, mustbethewayforward Python bot, Coook13 iteration archive, Mada21v project, and k-cubeee project.
- Some requested paths were 404 because the guessed path did not exist; those misses were not counted as inspected source.
- The official documentation URLs provided in the prompt were not readable through the generic web-open tool in this session. The uploaded 21 October handbook/API exports and project version audit were therefore used as local evidence; the exact current official CLI/SDK version must be re-verified from a working official installation before build work.
- Repository code search can surface SDK copies/forks as if they were independent candidates. Shared SDK files are not evidence of independent strategic quality.
- License status was not established for several promising repos; absent/unclear licensing blocks code reuse pending verification.

## 3. Candidate comparison and ranking

### Scoring rubric

Scores are 0–5, with `?` meaning insufficient evidence. Weighted dimensions:
- exact game/version fit: **25%**;
- demonstrated competitive evidence: **25%**;
- strategy quality relevant to this bot (routing, resource conversion, combat/survival): **20%**;
- reproducible testing/evaluation: **15%**;
- maintainability and integration cost: **10%**;
- licensing clarity: **5%**.

The score is a screening rubric, not an Elo estimate. No aggregate score can override a license blocker or a version mismatch.

| Rank / role | Repository | Evidence and useful content | Compatibility and limitations |
|---|---|---|---|
| **1 — Best direct bot candidate for deeper inspection** | [dpark2380/cambridge-battlecode](https://github.com/dpark2380/cambridge-battlecode) | C++ CAMBC project with `bots/daniel/v13`, documented version-by-version tests, an arena runner with map/side reporting and 95% intervals, a parameter tuner, draft tools, and replay analysis. Its version index reports v13 as a map-specific improvement and lists prior measured comparisons. | Targets the same game, but current repo pins SDK 4.0.0 while this archive pins 2.1.6. Claims are author-reported and not independently reproduced here. Root `LICENSE` lookup returned 404: **do not copy code until licensing is resolved**. |
| **2 — Best pipeline architecture lead** | [FHL-08/Tarot-Club-Cambc2026](https://github.com/FHL-08/Tarot-Club-Cambc2026) | Has a factory workflow concept: scoped briefs, isolated worktrees, a merge gate, explicit verification evidence, and a submission-safety field. Its `factory/scripts/merge-gate.sh` refuses merges unless required metadata, file scope, and checks pass. | Same old SDK pin (2.1.6), so it does not solve the patch/version mismatch. The gate checks a PR’s *reported* `Verified:` field; that alone is not proof of test execution. Root `LICENSE` lookup returned 404; treat implementation reuse as blocked until clarified. Borrow the architecture, not the code. |
| **3 — Best Python strategy/diagnostics reference** | [mustbethewayforward/bird-business-battlecode-camb26](https://github.com/mustbethewayforward/bird-business-battlecode-camb26) | Python implementation with a map/side suite, replay log inspection, fuel analysis, target-abandonment safety valves, and camp-capture give-up behaviour. The code comments describe dropping a target after repeated non-progress and abandoning a capture that remains contested. | README says CLI 2.2.5 and its current branch/source appears older than the newest rule exports; language and SDK are different. Use concepts such as stuck-target expiry and failure telemetry as hypotheses, then reimplement against current C++ APIs. License was not verified. |
| **4 — Iteration-history reference only** | [Coook13/cambridge-battlecode-bot](https://github.com/Coook13/cambridge-battlecode-bot) | Public archive of many tested iterations and an MIT license shown on the repository page. Potentially useful as an example of documenting hypotheses and scrimmage results. | Its README describes miners/builders/raiders/turrets rather than the current champions/camps/gems game model. Treat as a different or stale game interface until the actual bot files prove otherwise; not a direct replacement. |
| **5 — Low-confidence lead** | [Mada21v/CAMBC-2026-Midnight-Wardens](https://github.com/Mada21v/CAMBC-2026-Midnight-Wardens) | Contains CAMBC SDK/C++ API files and pins SDK 2.2.2. | README is effectively blank; code-search hits were SDK API files, not evidence of a strong bot. Do not rank it above a candidate with inspectable strategy and measured results. |
| **6 — Possible related fork/code family** | [k-cubeee/battlecode-new](https://github.com/k-cubeee/battlecode-new) | Has a CAMBC workspace README, starter and v19 opponents, and a benchmark script. Search surfaced SDK/API code similar to other CAMBC repositories. | Independent competitive strength, fork ancestry, current SDK, and license need confirmation. Do not double-count it as independent evidence based on SDK matches alone. |

### Separate rankings

1. **Best directly relevant complete bot to inspect:** dpark’s Daniel v13 line, subject to SDK and licensing checks.
2. **Best transferable strategy techniques:** dpark’s reward-per-time camp planning, per-body target claims, danger memory, pathing/stall fixes, and versioned map-specific tests; Bird Business’s target expiry and failure telemetry are a useful secondary lead.
3. **Best evaluation framework:** dpark’s `tools/arena.py` + seeded matches + 95% interval reporting + `tools/tune.py` and draft/replay utilities.
4. **Best automated improvement workflow architecture:** FHL Tarot’s scoped briefs/worktrees/merge gate, but replace its self-reported verification with machine-generated, retained test artifacts.
5. **Best foundation for this project:** keep the existing C++ strategy and local league/replay tools, repair the SDK/map/runtime mismatch, then adopt the external testing ideas in a small independent gate. Do not wholesale replace the bot.

## 4. What to investigate in the strongest candidate

The dpark source and documentation provide the most concrete follow-up leads:
- **Target selection:** evaluate camps by reward versus travel, clear, capture, return, and tribute cost; avoid treating raw gem value as sufficient.
- **Stuck/pathing recovery:** the repository’s v7 notes mention walking-distance stall checks; v9 notes mention pathing around living camps out of sight; v13 notes a corridor-specific carrier route fix.
- **Target coordination:** per-body claims and sharing a group only when it is worth the extra bodies.
- **Danger and survivability:** avoid recently dangerous areas, leave costly groups when expected damage is too high, retreat/recall and protect carriers.
- **Draft and equipment:** map-aware draft scoring and measured equipment comparisons, rather than copying one lineup or item order.
- **Testing discipline:** keep old versions as opponents, test both sides across maps, run fresh-seed validation, report confidence intervals, and retain rejected experiments.

Do not paste any of these source files into the user's code until licensing is confirmed. The algorithms can be independently designed from public descriptions and the user's own code/rules.

## 5. Current bot vs external candidate: practical comparison

| Capability | Supplied project | External lead | Decision |
|---|---|---|---|
| Cached camp metadata | Present in `tungtungsahur/src/bot.cpp` | Also uses cached fields/target calculations | Retain; profile current fuel first. |
| Distance-field navigation | Present | External project documents cached target fields and pathing fixes | Compare behaviour via replay and failed-move/stall metrics, not by code size. |
| Camp reward prioritisation | Present, with map/phase gates | External project documents reward-per-time scoring and updated camp tables | Recalculate against current `GameRules.md` and current SDK; do not transplant old constants. |
| Team coordination | Eight-byte shared memory for objective/anchor/alarms | External bot documents per-body claims and danger marker | Benchmark claims against the existing shared-memory protocol; change one mechanism at a time. |
| Champion drafting | Map-/opponent-aware code and draft memory exist | External v13 uses map-aware draft scoring and current champion data | Candidate idea worth a dedicated draft-only experiment after rules/runtime refresh. |
| Replay telemetry | `SCORE`/`SCOUT` lines plus parser utilities | External tools produce per-map results, intervals and replay summaries | Retain and standardize telemetry schema. |
| Current-rule compatibility | Not proven; project notes explicitly warn of mismatch | dpark says SDK 4.0.0; external docs describe newer patch history | Block strategic promotion until toolchain and rules align. |
| Automated promotion | Experiment instructions, but no workflow in this ZIP | dpark tuner plus FHL factory/merge-gate architecture | Add a fail-closed local gate; no automatic production overwrite. |

## 6. Pipeline audit and recommended minimum implementation

### Existing components to keep

- `tools/league.py`: primary multi-opponent, multi-map local league runner.
- `tools/direct_league.py`: fallback for environments where `run-batch` has build/persistence issues.
- `tools/eval.py`: quick pairwise map/side evaluation.
- `tools/race.py`: compare round-to-1000 behaviour.
- `tools/scout.py`, `tools/moves.py`, `tools/deaths.py`, `tools/errors.py`: replay diagnostics.
- `PLAN.md`, `REPORT.md`, `HANDOFF.md`: experiment and decision history.
- Separate candidate folders: continue using `bots/cand-*` or another isolated candidate namespace.

### Missing or blocked components

1. **Environment preflight:** check CLI version, SDK pin, current API/rules, maps, and replay/report directories before a benchmark begins.
2. **Incumbent identity:** one authoritative file naming the incumbent bot folder, source hash, submission ZIP hash, SDK, maps version, and baseline date.
3. **Evidence-backed results:** each run should store the exact bot hashes, CLI version, SDK, map list, opponent list, side, commands, exit codes, result parser status, and replay paths.
4. **Matched comparisons:** candidate and incumbent must play the same map/opponent/side conditions; use seeds if the runtime supports them.
5. **Fail-closed promotion:** missing outcomes, build failures, invalid games, stale SDK, or too few games must block promotion.
6. **CI/automation:** none is present in the archive; do not imply that a continuous pipeline already exists.
7. **Rule-change review:** track the official patch/rules version and force revalidation after a rule/API change.

### Smallest reliable loop

1. Freeze incumbent folder and checksums.
2. Run preflight; stop if toolchain/maps/rules are incomplete or mismatched.
3. Select one measured weakness from telemetry.
4. Create a candidate copy/branch and make one logical change.
5. Build both candidate and incumbent with the same current CLI.
6. Run smoke tests on a representative easy map and a known difficult map, both sides.
7. Run the full paired league against a fixed strong-opponent set and the broad pool.
8. Compare total win rate, map/opponent slices, score/gems, round to tribute, deaths, faults, and fuel.
9. Require no build/runtime errors and no meaningful regression on critical maps; use a confidence interval where sample size supports it.
10. Promote only after the predefined gate passes; preserve incumbent, logs, manifests, replays and rejected candidate.
11. Keep live submission/upload as a separate manual approval step.

## 7. Acceptance criteria before replacing the incumbent

A candidate is **not eligible** for promotion unless all of these pass:
- [ ] CLI and SDK versions are recorded and match the current official rules/API.
- [ ] Candidate and incumbent compile successfully in release mode.
- [ ] Smoke matches complete on at least two distinct maps and both team sides.
- [ ] Full benchmark covers the same map pool, opponent versions and side order for both bots.
- [ ] Every planned match is accounted for; missing/invalid outcomes are not counted as losses or silently discarded.
- [ ] No crash, bot fault, invalid action, fuel violation or unexplained timeout appears in the candidate run.
- [ ] Candidate beats or credibly improves on incumbent under a predefined statistical rule, not just one match or a cherry-picked map.
- [ ] No severe per-map regression is hidden by the aggregate.
- [ ] Source hash, config, CLI version, SDK, map manifest, commands, reports and replays are retained.
- [ ] Original incumbent remains recoverable; production source/ZIP is not overwritten automatically.
- [ ] Any external code reuse has a verified compatible license and required attribution.

Suggested initial threshold for a small local screen: use it only to reject bad candidates, not to promote. For final promotion, use the full map pool and multiple opponent styles; compare a paired win-rate difference with a confidence interval and inspect map-specific deltas. Choose a numeric threshold only after baseline variance is measured on the refreshed runtime.

## 8. Validation report

| Check | Result |
|---|---|
| ZIP opened and integrity tested | PASS |
| Source/config/docs inventoried | PASS |
| External public repositories discovered and selected files fetched | PASS, bounded search |
| Current official docs directly fetched | PARTIAL: official URLs were inaccessible via the generic open tool; uploaded handbook/API exports were inspected |
| CAMBC CLI available in this environment | **NO** |
| Maps present in supplied ZIP | **NO** |
| Replay bundles present in supplied ZIP | **NO** |
| Current-rule build | **NOT RUN** |
| New matches / win rates | **NOT RUN** |
| Competitive superiority proven | **NO — unproven** |
| Original bot source modified | **NO** |
| Automated production promotion | **NOT implemented and must remain manual** |

### Exact blocker to resolve

Use a complete Windows workspace with the official current CLI/SDK and current maps, or re-upload the full updated workspace containing them. Confirm which bot is the actual incumbent and provide its current submission ZIP/checksum. Only then can a candidate-versus-incumbent benchmark answer whether an external bot or a specific change is genuinely stronger.

## Source links

- Official competition homepage: https://2026a.battlecode.co.uk/
- Official rules / first bot: https://2026a.battlecode.co.uk/game/docs/first-bot
- Official patch notes: https://2026a.battlecode.co.uk/game/patch-notes/b1a658bb-a409-4803-8e82-2ae16d7ecf5b
- dpark C++ project: https://github.com/dpark2380/cambridge-battlecode
- dpark arena runner: https://github.com/dpark2380/cambridge-battlecode/blob/main/tools/arena.py
- dpark tuner: https://github.com/dpark2380/cambridge-battlecode/blob/main/tools/tune.py
- FHL Tarot project: https://github.com/FHL-08/Tarot-Club-Cambc2026
- FHL merge gate: https://github.com/FHL-08/Tarot-Club-Cambc2026/blob/main/factory/scripts/merge-gate.sh
- Bird Business project: https://github.com/mustbethewayforward/bird-business-battlecode-camb26
- Coook13 iteration archive: https://github.com/Coook13/cambridge-battlecode-bot
- Mada21v project: https://github.com/Mada21v/CAMBC-2026-Midnight-Wardens
- k-cubeee project: https://github.com/k-cubeee/battlecode-new
