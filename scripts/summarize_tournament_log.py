import csv
import re
from pathlib import Path
from collections import Counter, defaultdict

root = Path(__file__).resolve().parent.parent
manifest = root / "matches-candidate-vs-all-AllMaps-20261026-162805.csv"
log = root / "reports/logs/candidate-vs-all-AllMaps-20261026-162935.log"
out = root / "reports"
out.mkdir(exist_ok=True)

with manifest.open(newline="", encoding="utf-8-sig") as f:
    planned = list(csv.DictReader(f))

# Normalize terminal formatting and whitespace, including wrapped output.
text = log.read_text(encoding="utf-16", errors="replace")
text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
text = re.sub(r"\s+", " ", text)

bots = sorted(
    {r["bot_a"] for r in planned} | {r["bot_b"] for r in planned},
    key=len, reverse=True
)
bot_re = "(?:" + "|".join(re.escape(b) for b in bots) + ")"

pattern = re.compile(
    r"TEAM\s+[AB]\s+WIN\s+\((?P<winner>[^)]+)\)\s+"
    r"(?P<a>" + bot_re + r")\s+vs\s+"
    r"(?P<b>" + bot_re + r")\s+"
    r"(?P<map>[A-Za-z0-9_.-]+)\s+"
    r"Round\s+(?P<round>\d+)\s*/\s*(?P<maxround>\d+)"
)

parsed = list(pattern.finditer(text))
if not parsed:
    raise SystemExit(
        "Still could not parse results. No statistics were generated."
    )

# Index planned matches by exact ordered matchup and map.
index = defaultdict(list)
for row in planned:
    key = (row["bot_a"], row["bot_b"], Path(row["map"]).stem)
    index[key].append(row)

used = Counter()
results = []
unmatched = []

for match in parsed:
    d = match.groupdict()
    key = (d["a"], d["b"], d["map"])
    available = [r for r in index.get(key, []) if not used[r["id"]]]

    if not available:
        unmatched.append(f'{d["a"]} vs {d["b"]} on {d["map"]}')
        continue

    row = available[0]
    winner = d["winner"].strip()
    if winner not in (row["bot_a"], row["bot_b"]):
        unmatched.append(f'Unexpected winner {winner} for {key}')
        continue

    used[row["id"]] += 1
    results.append({
        "match_id": row["id"],
        "map": d["map"],
        "bot_a": row["bot_a"],
        "bot_b": row["bot_b"],
        "winner": winner,
        "loser": row["bot_b"] if winner == row["bot_a"] else row["bot_a"],
        "winner_seat": "A" if winner == row["bot_a"] else "B",
        "round": int(d["round"]),
        "max_round": int(d["maxround"]),
    })

bots = sorted({r["bot_a"] for r in planned} | {r["bot_b"] for r in planned})
stats = {b: Counter() for b in bots}

for r in results:
    stats[r["winner"]]["wins"] += 1
    stats[r["loser"]]["losses"] += 1
    stats[r["bot_a"]]["matches"] += 1
    stats[r["bot_b"]]["matches"] += 1
    stats[r["winner"]]["wins_as_" + r["winner_seat"]] += 1

standings = []
for b, s in stats.items():
    n = s["wins"] + s["losses"]
    standings.append({
        "bot": b,
        "matches": n,
        "wins": s["wins"],
        "losses": s["losses"],
        "win_pct": round(100 * s["wins"] / n, 2) if n else 0,
        "wins_as_A": s["wins_as_A"],
        "wins_as_B": s["wins_as_B"],
    })
standings.sort(key=lambda x: (-x["win_pct"], -x["wins"], x["bot"]))

def save_csv(filename, rows):
    if rows:
        with (out / filename).open("w", newline="", encoding="utf-8-sig") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

save_csv("tournament-standings.csv", standings)
save_csv("tournament-match-results.csv", results)

candidate = "candidate-bot"
h2h = []
for opponent in bots:
    if opponent == candidate:
        continue
    subset = [r for r in results
              if {r["bot_a"], r["bot_b"]} == {candidate, opponent}]
    wins = sum(r["winner"] == candidate for r in subset)
    losses = sum(r["loser"] == candidate for r in subset)
    h2h.append({
        "candidate": candidate,
        "opponent": opponent,
        "matches": len(subset),
        "wins": wins,
        "losses": losses,
        "win_pct": round(100 * wins / len(subset), 2) if subset else 0,
    })
save_csv("candidate-head-to-head.csv", h2h)

by_map = defaultdict(list)
for r in results:
    if candidate in (r["bot_a"], r["bot_b"]):
        by_map[r["map"]].append(r)

map_rows = []
for name, subset in sorted(by_map.items()):
    wins = sum(r["winner"] == candidate for r in subset)
    losses = sum(r["loser"] == candidate for r in subset)
    map_rows.append({
        "map": name,
        "matches": len(subset),
        "wins": wins,
        "losses": losses,
        "win_pct": round(100 * wins / len(subset), 2) if subset else 0,
    })
save_csv("candidate-by-map.csv", map_rows)

planned_ids = {r["id"] for r in planned}
matched_ids = {r["match_id"] for r in results}

print("\n=== OVERALL STANDINGS ===")
for s in standings:
    print(f'{s["bot"]:16} {s["wins"]:3} W - {s["losses"]:3} L | '
          f'{s["win_pct"]:6.2f}% | {s["matches"]} matches')

print("\n=== CANDIDATE VS OPPONENTS ===")
for r in h2h:
    print(f'{r["opponent"]:16} {r["wins"]:3} W - {r["losses"]:3} L | '
          f'{r["win_pct"]:6.2f}% | {r["matches"]} matches')

print("\n=== VALIDATION ===")
print(f"Manifest matches: {len(planned)}")
print(f"Parsed winner records: {len(parsed)}")
print(f"Matched outcomes: {len(results)}")
print(f"Unmatched records: {len(unmatched)}")
print(f"Planned matches without outcomes: {len(planned_ids - matched_ids)}")
print(f"Candidate maps covered: {len(map_rows)}")
print(f"\nCSV reports saved to: {out}")

if unmatched:
    print("\nExamples of unmatched records:")
    for item in unmatched[:10]:
        print(" -", item)

if len(results) != len(planned) or unmatched:
    print("\nWARNING: Treat results as partial until the missing records are resolved.")
