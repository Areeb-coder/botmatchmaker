import csv, json, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
STATS = ROOT / "reports" / "stats"

def norm(s): return re.sub(r'[^a-z0-9]', '', str(s).lower())
def walk(obj):
    if isinstance(obj, dict):
        for k,v in obj.items():
            yield norm(k), v
            yield from walk(v)
    elif isinstance(obj, list):
        for v in obj: yield from walk(v)

def winner_from_json(path, a, b):
    try: data=json.loads(path.read_text(encoding='utf-8-sig'))
    except Exception: return 'UNKNOWN', 'invalid JSON'
    keys = {'winner','winningteam','winnerteam','winningSide','winningside','victor','winningplayer','winnername'}
    for k,v in walk(data):
        if k not in keys or isinstance(v,(dict,list)): continue
        s=str(v).strip().lower()
        if not s or s in ('none','null','unknown','draw','tie'): continue
        if norm(a) in norm(s): return a, f'explicit winner field {k}={v}'
        if norm(b) in norm(s): return b, f'explicit winner field {k}={v}'
        if s in ('a','teama','team_a','team  a','blue'): return a, f'explicit winner field {k}={v}'
        if s in ('b','teamb','team_b','team  b','red'): return b, f'explicit winner field {k}={v}'
    return 'UNKNOWN', 'No recognized winner field; inspect this JSON schema'

def main():
    manifests=sorted(STATS.glob('manifest-*.csv'), key=lambda p:p.stat().st_mtime, reverse=True)
    if not manifests:
        print('No tournament manifest found. Run Run-Stats-Tournament.ps1 first.'); return 2
    manifest=manifests[0]
    rows=[]
    with manifest.open(newline='',encoding='utf-8-sig') as f:
        for m in csv.DictReader(f):
            stats=ROOT/m['stats_json']
            if m.get('status') != 'completed' or not stats.exists():
                winner, note='UNKNOWN','match failed or stats file missing'
            else: winner,note=winner_from_json(stats,m['bot_a'],m['bot_b'])
            if winner == m['bot_a']: result_a='W'
            elif winner == m['bot_b']: result_a='L'
            else: result_a='UNKNOWN'
            rows.append({**m,'winner_detected':winner,'result_for_bot_a':result_a,'analysis_note':note})
    out=STATS/'summary-latest.csv'
    with out.open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0].keys()) if rows else ['status']); w.writeheader(); w.writerows(rows)
    print(f'Input manifest: {manifest.relative_to(ROOT)}')
    print(f'Detailed summary: {out.relative_to(ROOT)}')
    agg={}
    for r in rows:
        for bot in (r['bot_a'],r['bot_b']): agg.setdefault(bot,{'W':0,'L':0,'UNKNOWN':0})
        if r['result_for_bot_a']=='W': agg[r['bot_a']]['W']+=1; agg[r['bot_b']]['L']+=1
        elif r['result_for_bot_a']=='L': agg[r['bot_a']]['L']+=1; agg[r['bot_b']]['W']+=1
        else: agg[r['bot_a']]['UNKNOWN']+=1; agg[r['bot_b']]['UNKNOWN']+=1
    print('\nBot results (only explicitly recognized winners are counted):')
    print(f"{'BOT':32} {'W':>5} {'L':>5} {'Unknown':>8} {'Known win rate':>16}")
    for bot,v in sorted(agg.items(),key=lambda x: ((x[1]['W']/(x[1]['W']+x[1]['L'])) if x[1]['W']+x[1]['L'] else -1),reverse=True):
        known=v['W']+v['L']; rate=f"{100*v['W']/known:.1f}%" if known else 'n/a'
        print(f"{bot:32} {v['W']:5d} {v['L']:5d} {v['UNKNOWN']:8d} {rate:>16}")
    if rows and all(r['result_for_bot_a']=='UNKNOWN' for r in rows):
        print('\nNo winner field was recognized in the installed CLI statistics format. Do not treat this as a zero win rate.')
        print('Open one reports/stats/stats-*.json and send its structure so the parser can be adapted to the actual schema.')
    return 0
if __name__=='__main__': sys.exit(main())
