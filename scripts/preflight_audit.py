#!/usr/bin/env python3
"""Read-only preflight for the local Windows CAMBC matchmaker workspace."""
from __future__ import annotations
import argparse,json,re,shutil,subprocess
from datetime import datetime,timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out",default="reports/research-analysis/preflight.json")
    a=ap.parse_args();findings=[]
    def add(level,code,msg):findings.append({"level":level,"code":code,"message":msg})
    if not (ROOT/"cambc.toml").exists():add("BLOCKER","ROOT_CONFIG_MISSING","Root cambc.toml is missing.")
    bots=[]
    for d in sorted((ROOT/"bots").iterdir() if (ROOT/"bots").is_dir() else []):
        if d.is_dir() and (d/"src/main.cpp").is_file():
            cfg=d/"cambc.toml";version=None
            if cfg.exists():
                m=re.search(r'^\s*sdk_version\s*=\s*["\']([^"\']+)["\']',cfg.read_text(encoding="utf-8",errors="replace"),re.M)
                version=m.group(1) if m else None
            bots.append({"name":d.name,"config_exists":cfg.exists(),"sdk_version":version})
            if not cfg.exists():add("BLOCKER","BOT_CONFIG_MISSING",f"{d.name}: cambc.toml missing.")
            if version=="2.1.6":add("WARN","SDK_PIN_2_1_6",f"{d.name}: SDK pin is 2.1.6. Confirm current official SDK compatibility; CLI version and sdk_version are different fields.")
    maps=sorted((ROOT/"maps").rglob("*.map26a")) if (ROOT/"maps").is_dir() else []
    if not maps:add("BLOCKER","NO_MAPS","No .map26a files under maps/.")
    if not bots:add("BLOCKER","NO_BOTS","No configured bots with src/main.cpp found.")
    cli=shutil.which("cambc");cli_out=None;cli_error=None
    if not cli:add("BLOCKER","CLI_NOT_FOUND","CAMBC CLI is not on PATH. Install/update it before building or matching.")
    else:
        try:
            p=subprocess.run([cli,"--version"],cwd=ROOT,capture_output=True,text=True,timeout=15,check=False)
            cli_out=(p.stdout+"\n"+p.stderr).strip()[:1000]
            if p.returncode:add("BLOCKER","CLI_VERSION_FAILED",f"cambc --version exited {p.returncode}.")
            elif not re.search(r'\b4\.',cli_out):add("WARN","CLI_VERSION_REVIEW","CLI output does not clearly identify a 4.x version; confirm against current official patch notes.")
        except Exception as e:cli_error=str(e);add("BLOCKER","CLI_VERSION_FAILED",cli_error)
    if (ROOT/"reports/tournament-match-results.csv").exists():add("INFO","HISTORICAL_RESULTS_PRESENT","Historical local results are included; fresh current-runtime results are still required.")
    blockers=sum(f["level"]=="BLOCKER" for f in findings);warnings=sum(f["level"]=="WARN" for f in findings)
    report={"generated_at_utc":datetime.now(timezone.utc).isoformat(),"workspace":str(ROOT),
      "status":"BLOCKED" if blockers else ("WARN" if warnings else "READY_FOR_MANUAL_REVIEW"),
      "cli_path":cli,"cli_version_output":cli_out,"cli_error":cli_error,"map_count":len(maps),"bot_count":len(bots),
      "bots":bots,"sdk_pins":sorted({b["sdk_version"] for b in bots if b["sdk_version"]}),"findings":findings,
      "safety":{"bot_sources_modified":False,"builds_run":False,"matches_run":False,"promotion_run":False,"submission_uploaded":False}}
    out=Path(a.out);out=out if out.is_absolute() else ROOT/out;out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(f"CAMBC preflight: {report['status']} | maps={len(maps)} bots={len(bots)} CLI={'found' if cli else 'missing'}")
    for f in findings:print(f"[{f['level']}] {f['code']}: {f['message']}")
    print("Report:",out)
    raise SystemExit(2 if blockers else (1 if warnings else 0))
if __name__=="__main__":main()
