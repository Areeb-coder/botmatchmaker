#!/usr/bin/env python3
"""Analyze completed CAMBC tournament outcomes without changing bot sources or raw results."""
from __future__ import annotations
import argparse,csv,json,math
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read_csv(p):
    with p.open("r",encoding="utf-8-sig",newline="") as f: return list(csv.DictReader(f))
def wilson(w,n,z=1.959963984540054):
    if n<=0:return (None,None)
    p=w/n; den=1+z*z/n; c=(p+z*z/(2*n))/den
    h=z*math.sqrt((p*(1-p)+z*z/(4*n))/n)/den
    return max(0,c-h),min(1,c+h)
def write_csv(p,rows):
    p.parent.mkdir(parents=True,exist_ok=True)
    if not rows:p.write_text("",encoding="utf-8");return
    with p.open("w",encoding="utf-8-sig",newline="") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input",default="reports/tournament-match-results.csv")
    ap.add_argument("--candidate",default="candidate-bot")
    ap.add_argument("--out-dir",default="reports/research-analysis")
    a=ap.parse_args()
    src=Path(a.input);src=src if src.is_absolute() else ROOT/src
    out=Path(a.out_dir);out=out if out.is_absolute() else ROOT/out
    rows=read_csv(src)
    if not rows:raise SystemExit(f"No match results in {src}")
    required={"match_id","map","bot_a","bot_b","winner","loser","winner_seat"}
    if not required.issubset(rows[0]):raise SystemExit(f"Missing columns: {sorted(required-set(rows[0]))}")
    ids=[r["match_id"] for r in rows]
    if len(ids)!=len(set(ids)):raise SystemExit("Duplicate match_id values; refusing ambiguous analysis.")
    for r in rows:
        if r["winner"] not in (r["bot_a"],r["bot_b"]):raise SystemExit(f"Invalid winner in match {r['match_id']}")
        if r["loser"] not in (r["bot_a"],r["bot_b"]) or r["loser"]==r["winner"]:raise SystemExit(f"Invalid loser in match {r['match_id']}")
    bots=sorted({r["bot_a"] for r in rows}|{r["bot_b"] for r in rows})
    if a.candidate not in bots:raise SystemExit(f"Candidate {a.candidate!r} absent from results.")
    h2h=[]
    for opp in bots:
        if opp==a.candidate:continue
        sub=[r for r in rows if {r["bot_a"],r["bot_b"]}=={a.candidate,opp}]
        w=sum(r["winner"]==a.candidate for r in sub);lo,hi=wilson(w,len(sub))
        h2h.append({"candidate":a.candidate,"opponent":opp,"matches":len(sub),"wins":w,"losses":len(sub)-w,
          "win_pct":round(100*w/len(sub),2) if sub else "",
          "wilson95_low_pct":round(100*lo,2) if lo is not None else "",
          "wilson95_high_pct":round(100*hi,2) if hi is not None else ""})
    h2h.sort(key=lambda r:r["win_pct"] if r["win_pct"]!="" else -1,reverse=True)
    map_rows=[]
    for name in sorted({r["map"] for r in rows}):
        sub=[r for r in rows if r["map"]==name and a.candidate in (r["bot_a"],r["bot_b"])]
        w=sum(r["winner"]==a.candidate for r in sub);lo,hi=wilson(w,len(sub))
        rate=w/len(sub) if sub else 0
        map_rows.append({"map":name,"matches":len(sub),"wins":w,"losses":len(sub)-w,
          "win_pct":round(100*rate,2) if sub else "",
          "wilson95_low_pct":round(100*lo,2) if lo is not None else "",
          "wilson95_high_pct":round(100*hi,2) if hi is not None else "",
          "priority":"P1 investigate" if sub and rate<=1/3 else ("P2 investigate" if sub and rate<=.5 else "observe")})
    seat_rows=[]
    for seat in ("A","B"):
        sub=[r for r in rows if (r["bot_a"]==a.candidate and seat=="A") or (r["bot_b"]==a.candidate and seat=="B")]
        w=sum(r["winner"]==a.candidate for r in sub);lo,hi=wilson(w,len(sub))
        seat_rows.append({"candidate":a.candidate,"candidate_seat":seat,"matches":len(sub),"wins":w,"losses":len(sub)-w,
          "win_pct":round(100*w/len(sub),2) if sub else "",
          "wilson95_low_pct":round(100*lo,2) if lo is not None else "",
          "wilson95_high_pct":round(100*hi,2) if hi is not None else ""})
    write_csv(out/"candidate-head-to-head-with-ci.csv",h2h)
    write_csv(out/"candidate-by-map-with-ci.csv",map_rows)
    write_csv(out/"candidate-by-seat.csv",seat_rows)
    cand=[r for r in rows if a.candidate in (r["bot_a"],r["bot_b"])]
    wins=sum(r["winner"]==a.candidate for r in cand);lo,hi=wilson(wins,len(cand))
    weak=sorted([r for r in map_rows if r["priority"]=="P1 investigate"],key=lambda r:(r["win_pct"],r["map"]))
    mid=sorted([r for r in map_rows if r["priority"]=="P2 investigate"],key=lambda r:(r["win_pct"],r["map"]))
    report=["# Tournament analysis (raw results preserved)","",f"- Generated UTC: {datetime.now(timezone.utc).isoformat()}",
      f"- Input: `{src}`",f"- Candidate: `{a.candidate}`",f"- Raw rows: **{len(rows)}**; unique IDs: **{len(set(ids))}**",
      f"- Candidate appearances: **{len(cand)}**; wins **{wins}**, losses **{len(cand)-wins}**, win rate **{100*wins/len(cand):.2f}%**.",
      f"- Wilson 95% interval: **{100*lo:.2f}%–{100*hi:.2f}%**; descriptive only, not a paired/map-cluster adjusted test.",
      "","## Head-to-head","","| Opponent | W-L | Matches | Win rate | Wilson 95% interval |","|---|---:|---:|---:|---:|"]
    for r in h2h:report.append(f"| {r['opponent']} | {r['wins']}-{r['losses']} | {r['matches']} | {r['win_pct']}% | {r['wilson95_low_pct']}–{r['wilson95_high_pct']}% |")
    report += ["","## Candidate seat split","","| Seat | W-L | Matches | Win rate |","|---|---:|---:|---:|"]
    for r in seat_rows:report.append(f"| {r['candidate_seat']} | {r['wins']}-{r['losses']} | {r['matches']} | {r['win_pct']}% |")
    report += ["","## Map triage","", "Six games per map is a small sample. These are replay-review priorities, not confirmed causal weaknesses.",""]
    for r in weak:report.append(f"- **P1 `{r['map']}`:** {r['wins']}/{r['matches']} ({r['win_pct']}%). Inspect failed moves, target switches, camp/capture duration, carrier deaths, tribute timing, and fuel.")
    for r in mid:report.append(f"- **P2 `{r['map']}`:** {r['wins']}/{r['matches']} ({r['win_pct']}%).")
    report += ["","## Interpretation limits","","- This is a local matchmaker pool, not official ladder Elo.",
      "- Do not rank bots by aggregate rates when their number of appearances differs; use direct head-to-head records.",
      "- Per-map rates have small samples; rerun more seeds where supported and preserve both seat orders.",
      "- No bot source is changed by this analysis."]
    out.mkdir(parents=True,exist_ok=True);(out/"summary.md").write_text("\n".join(report)+"\n",encoding="utf-8")
    meta={"generated_at_utc":datetime.now(timezone.utc).isoformat(),"input":str(src),"candidate":a.candidate,
      "result_rows":len(rows),"unique_match_ids":len(set(ids)),"candidate_appearances":len(cand),"candidate_wins":wins,
      "candidate_losses":len(cand)-wins,"candidate_win_rate_pct":round(100*wins/len(cand),2),
      "wilson95_pct":[round(100*lo,2),round(100*hi,2)],"head_to_head":h2h,"candidate_seat":seat_rows,
      "priority_1_maps":[r["map"] for r in weak],"priority_2_maps":[r["map"] for r in mid],
      "limitations":["local matches only","not official Elo","small per-map samples","no fresh runtime build performed"]}
    (out/"analysis.json").write_text(json.dumps(meta,indent=2),encoding="utf-8")
    print(f"Analyzed {len(rows)} outcomes; {a.candidate}: {wins}/{len(cand)} ({100*wins/len(cand):.2f}%).")
    print("Reports:",out)
if __name__=="__main__":main()
