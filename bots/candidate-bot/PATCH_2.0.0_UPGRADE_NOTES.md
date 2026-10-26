# Cambridge Battlecode Patch 2.0.0 — candidate upgrade notes

Research date: 26 October 2026

## What was changed in this candidate

This is a focused, experimental upgrade to the uploaded source, not a claim that the bot has already gained Elo. The source's map-specific draft picks, combat routines, movement, and pathfinding were left unchanged so the first test can isolate the effect of the shop/build changes.

### 1. Use Patch 2.0.0 emblem families

The previous shopping logic primarily purchased Power shards/emblems, which offer extra stats but no passive. The updated build plans prioritize completed, distinct passive families:

- **Stellar:** Capture Focus → Monster Focus → Tribute Focus → Prospector Focus → Collectors Focus.
- **Varg:** Capture Strength → Monster Strength → Prospector Strength → Treasure Strength → Collectors Strength.
- **Tanks:** Jungle Health → Durability Health → Monster Strength → Skirmish Strength → Collectors Strength.
- **Fighters:** Monster Strength → Skirmish Strength → Hunter Strength → Treasure Strength → Collectors Strength.
- **Marksman:** Monster Focus → Skirmish Focus → Tribute Focus → Treasure Focus → Collectors Focus.
- **Support:** Durability Health → Skirmish Focus → Tribute Focus → Treasure Focus → Collectors Focus.
- **Mage:** Monster Focus → Skirmish Focus → Tribute Focus → Prospector Focus → Collectors Focus.
- **Farmer:** Capture Strength → Monster Strength → Prospector Strength → Treasure Strength → Collectors Strength.

Existing map-specific Ryth and Tavi profiles are preserved in spirit but migrated off the non-passive Power line. Collectors is placed after four distinct families so its damage passive can scale from completed family diversity. Stellar and Varg both prioritize Capture because the existing movement logic designates them as the team's capturers; the Capture passive only helps if a holder owns it when capture begins.

The code uses the official `cambc::get_item_enum(family, stat, true)` helper documented in the current [Bot API](https://2026a.battlecode.co.uk/game/api), rather than manually spelling each of the 84 item enums.

### 2. Avoid spending early gold on the altar lock before finishing shards

Previously `shop()` bought a 10-gold altar lock before buying shards. If a champion arrived with 150 gold, that left 140 gold, enough for only four 30-gold shards. The upgraded order purchases affordable shards first and considers a lock only with leftover gold that is insufficient for the next shard. This is intended to prevent an optional consumable from delaying an emblem passive.

### 3. Keep the emblem-step counter aligned to five-shard families

Each planned item in the new build queue belongs to a five-shard family, so the existing `shards_bought / 5` progression now lines up with the family completion threshold. This avoids the old queue's mixed five-shard/six-shard Power/other-family assumptions and removes a seven-shard Bruiser queue from the Ren tank route.

## Patch facts that informed the changes

The official [Patch 2.0.0 Notes — Sprint 3](https://2026a.battlecode.co.uk/game/patch-notes/b1a658bb-a409-4803-8e82-2ae16d7ecf5b), dated 25 October 2026, reports:

- Ryth Strength growth: 5 → 4.5; Vesper Vale base Strength: 55 → 45.
- Cassi base Focus: 70 → 100; Issac base Strength: 40 → 45.
- Stellar Focus growth: 7 → 8.5; Prism Health growth: 60 → 65.
- Marcella Strength growth: 5 → 4.5 and Focus growth: 15 → 14.
- Willow Focus: 120 → 140; Ren's S1 stun damage: 0.75×Focus → 1.1×Focus.
- Varg Strength: 40 → 45; Tavi's S2 stun damage: 1.4×Focus → 1.6×Focus.
- Heart of Ruin Strength growth: 3.5 → 3 and Focus growth: 12 → 10.
- Treasure bonus gold: 20% → 10%; Monster bonus damage to camps: 10% → 15%.
- New Tribute, Capture, Prospector, Hunter, Skirmish, Greed and Collectors emblem families.
- The draft first-turn fuel budget increased by 10×.
- Engine 2.0.0 / CLI 4.0.0; inventory expanded from 42 to 84 item types. Custom WASM bindings for `env.champion_items` must output 84 little-endian `u32` counts (336 bytes).
- Capture-state queries now report state and a timer while the camp group is in vision.

Relevant official handbook pages:
- [Items & gold](https://2026a.battlecode.co.uk/game/docs/items)
- [Camps & capture](https://2026a.battlecode.co.uk/game/docs/camps)
- [Combat & abilities](https://2026a.battlecode.co.uk/game/docs/combat)
- [Gems & scoring](https://2026a.battlecode.co.uk/game/docs/gems-and-scoring)
- [Maps & movement](https://2026a.battlecode.co.uk/game/docs/maps-and-movement)
- [Getting started / local testing](https://2026a.battlecode.co.uk/game/docs/first-bot)

## What has not been established

- **No Elo increase is measured yet.** Elo cannot be guaranteed from source edits alone; it must be measured over repeated match groups on the current engine.
- The current container does not have the CAMBC CLI or `cambc.h` SDK headers available, and network installation was not available during this run. Therefore this candidate has **not** been compiled or run through official matches here. Do not submit it as a proven winner before running the tests below.
- The uploaded source uses the managed C++ SDK and contains no custom `env.champion_items` WASM binding, so the raw 336-byte buffer warning does not appear to apply directly to this project.
- No change was made to movement/capture-state API calls because the exact SDK method names and runtime behavior were not safely verifiable from this environment. The candidate uses the verified Capture emblem passive through the helper, without inventing a new capture-state call.
- The existing draft lineups already use several newly adjusted champions (including Cassi/Issac, Stellar, Ryth, Ren and Varg). Without matched post-patch replay data, changing the draft as well as the shop would confound the first A/B experiment.

## Required validation before submission

1. Update the local CAMBC CLI following the official guide (`python -m pip install --upgrade cambc`) and confirm `cambc --version`; then use the current CLI/SDK to rebuild this source.
2. Run a short smoke match and a **full match** against the same baseline bot, then repeat across the published map pool. Keep all seeds/configurations comparable.
3. Export match statistics and inspect replay timelines. Track tributed gems, gold earned, deaths with carried gems, rounds to first Capture emblem, camps cleared vs. captures completed, idle turns, and altar-tribute interruption.
4. Run at least 20–50 matched games per version if possible, alternate team side, and report win rate with sample size rather than trusting a single match.
5. If a family choice regresses performance on a specific map, change only that map/role profile next and repeat the A/B set.

## Suggested official local test commands

```bash
python -m pip install --upgrade cambc
cambc --version
cambc build <your-bot-directory>
cambc run <your-bot-directory> starter-cpp --view
cambc run <your-bot-directory> starter-cpp --statistics replays/stats.json
```

Use the bot directory name appropriate to your local CAMBC workspace. Test full games and multiple real maps; `--rounds 50` is only a smoke test.
