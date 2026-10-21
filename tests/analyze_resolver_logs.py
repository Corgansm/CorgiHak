"""Read-only summaries of the explicitly supplied recording sessions."""
import csv
import json
from collections import Counter
from pathlib import Path

root = Path('D:/SteamLibrary/steamapps/common/csgo legacy/resolver_logs')
report = {}
for prefix, count in [('aa_1792533090',107), ('aa_1792534184',295), ('rs_1792532725',3)]:
    kinds, reasons, yaws, flags = Counter(), Counter(), Counter(), Counter()
    first, last, missing, rows, records, near_zero = None, None, [], 0, 0, 0
    shots = []
    selected, selected_zero = 0, 0
    for part in range(1, count+1):
        path = root / f'{prefix}_{part:03d}.csv'
        if not path.exists():
            missing.append(path.name)
            continue
        with path.open(newline='', encoding='utf-8-sig') as f:
            for r in csv.DictReader(f):
                rows += 1
                kinds[r.get('kind','')] += 1
                if r.get('unix_ms'):
                    t = int(r['unix_ms'])
                    first = min(first,t) if first is not None else t
                    last = max(last,t) if last is not None else t
                if r.get('kind') == 'record':
                    records += 1
                    if r.get('resolver_body_yaw'):
                        yaw = float(r['resolver_body_yaw'])
                        yaws[round(yaw,1)] += 1
                        near_zero += abs(yaw) <= 2
                        if r.get('multi_matrix') == '1' and r.get('resolver_side') in ('1','2','3','4'):
                            selected += 1
                            selected_zero += abs(yaw) <= 2
                    for key in ['defensive','in_shift','lc_broken','flicker','status']:
                        flags[f'{key}={r.get(key)}'] += 1
                if r.get('reason'): reasons[r['reason']] += 1
                if r.get('kind') not in ['record','tick','setting','packet','net','note','round'] and len(shots)<15:
                    shots.append(r)
    report[prefix] = dict(rows=rows, missing=missing, unix_ms=[first,last], kinds=dict(kinds),
        reasons=dict(reasons), records=records, body_yaw_within_2=near_zero,
        noncenter_multi_records=selected, noncenter_multi_within_2=selected_zero,
        common_body_yaws=yaws.most_common(8), flags=dict(flags), example_events=shots)
out = Path('build/resolver-log-analysis.json')
out.write_text(json.dumps(report, indent=2))
for name, r in report.items():
    print(name, json.dumps({k:v for k,v in r.items() if k!='example_events'}))
    if name.startswith('rs'):
        print('events',json.dumps(r['example_events'][:4]))
