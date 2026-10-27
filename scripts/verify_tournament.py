#!/usr/bin/env python3
"""Fail-closed consistency check for a CAMBC manifest, parsed results and replay files."""
from __future__ import annotations
import argparse,csv,json
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):
    with p.open("r",encoding="utf-8-sig",newline="") as f:return list(csv.DictReader(f))
def resolve(s):
    p=Path(s);return p if p.is_absolute() else ROOT/p
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest",default="matches-candidate-vs-all-AllMaps-20261026-162935.csv")
    ap.add_argument("--results",default="reports/tournament-match-results.csv")
    ap.add_argument("--check-replays",action="store_true")
    ap.add_argument("--out",default="reports/research-analysis/verification.json")
    a=ap.parse_args();mp,rp=resolve(a.manifest),resolve(a.results)
    if not mp.exists():raise SystemExit(f"Manifest not found: {mp}")
    if not rp.exists():raise SystemExit(f"Results not found: {rp}")
    plan,res=read(mp),read(rp);errors=[]
    if not plan:errors.append("Manifest contains zero planned matches.")
    if not res:errors.append("Results contain zero outcomes.")
    pids=[r.get("id","") for r in plan];rids=[r.get("match_id","") for r in res]
    if len(pids)!=len(set(pids)):errors.append("Manifest has duplicate IDs.")
    if len(rids)!=len(set(rids)):errors.append("Results have duplicate match IDs.")
    pm={r.get("id"):r for r in plan};rm={r.get("match_id"):r for r in res}
    missing=sorted(set(pm)-set(rm));extra=sorted(set(rm)-set(pm))
    if missing:errors.append(f"{len(missing)} planned matches have no result.")
    if extra:errors.append(f"{len(extra)} results are not in the manifest.")
    checked=0;missing_replays=[]
    for mid in sorted(set(pm)&set(rm)):
        p,r=pm[mid],rm[mid];checked+=1
        if (p.get("bot_a"),p.get("bot_b"),Path(p.get("map","")).stem)!=(r.get("bot_a"),r.get("bot_b"),r.get("map")):
            errors.append(f"Match {mid}: participants/map differ between manifest and result.")
        if r.get("winner") not in (r.get("bot_a"),r.get("bot_b")):errors.append(f"Match {mid}: invalid winner.")
        if r.get("loser") not in (r.get("bot_a"),r.get("bot_b")) or r.get("loser")==r.get("winner"):errors.append(f"Match {mid}: invalid loser.")
        if r.get("winner_seat")!=("A" if r.get("winner")==r.get("bot_a") else "B"):errors.append(f"Match {mid}: winner seat inconsistent.")
        if a.check_replays:
            replay=resolve(p.get("replay",""))
            if not replay.exists():missing_replays.append(str(replay))
    if missing_replays:errors.append(f"{len(missing_replays)} manifest replay files are missing.")
    map_count=len({Path(r.get("map","")).stem for r in plan})
    replay_count=len(list((ROOT/"replays").rglob("*.repl26a")))
    out=resolve(a.out);out.parent.mkdir(parents=True,exist_ok=True)
    data={"generated_at_utc":datetime.now(timezone.utc).isoformat(),"status":"FAIL" if errors else "PASS",
      "manifest":str(mp),"results":str(rp),"planned_matches":len(plan),"result_rows":len(res),
      "unique_manifest_ids":len(set(pids)),"unique_result_ids":len(set(rids)),"matched_ids_checked":checked,
      "maps_in_manifest":map_count,"replay_files_in_workspace":replay_count,"replays_checked":a.check_replays,
      "missing_replays":missing_replays,"errors":errors,
      "note":"PASS validates artifact consistency only; it does not prove current SDK compatibility or competitive strength."}
    out.write_text(json.dumps(data,indent=2),encoding="utf-8")
    print(f"Verification: {data['status']} | planned={len(plan)} outcomes={len(res)} matched={checked} maps={map_count}")
    for e in errors:print("ERROR:",e)
    print("Report:",out)
    if errors:raise SystemExit(1)
if __name__=="__main__":main()
