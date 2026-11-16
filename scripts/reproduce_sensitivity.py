"""Replay P1 LOBO/lock comparisons, the retained C8 overlap and headline effects."""
import csv
import json
from pathlib import Path
import statistics

R = Path(__file__).resolve().parents[1]

def read(rel):
    with (R / rel).open(encoding='utf-8-sig') as f:
        return list(csv.DictReader(f))

def comparison(rows, reference, target, time_key='complete_s'):
    ref = {int(x['block']): float(x[time_key]) for x in rows if x['arm'] == reference}
    arm = {int(x['block']): float(x[time_key]) for x in rows if x['arm'] == target}
    assert ref.keys() == arm.keys() and ref
    blocks = sorted(ref)
    pct = [100*(1-arm[b]/ref[b]) for b in blocks]
    return {'reference':reference, 'target':target, 'n':len(blocks),
            'median_ratio_reduction_pct':100*(1-statistics.median(arm.values())/statistics.median(ref.values())),
            'paired_median_reduction_pct':statistics.median(pct),
            'positive_pairs':sum(x > 0 for x in pct), 'paired_pct':pct}

rows = read('data/formal_windows.csv')
locks = []
lobo = []
for B in (200, 2000):
    p1 = [x for x in rows if x['group'] == 'P1' and int(x['B']) == B]
    for policy in ('A0', 'A1', 'TLS'):
        c = comparison(p1, policy+'_C', policy+'_F')
        c['B'] = B
        locks.append(c)
    for ref in ('A0_DIRECT', 'TLS_F'):
        whole = comparison(p1, ref, 'A1_F')
        leaves = []
        for block in range(1, 7):
            c = comparison([x for x in p1 if int(x['block']) != block], ref, 'A1_F')
            c['left_out_block'] = block
            leaves.append(c)
        lobo.append({'B':B,'full':whole,'leave_one_block_out':leaves,
                     'ratio_range_pct':[min(x['median_ratio_reduction_pct'] for x in leaves),
                                        max(x['median_ratio_reduction_pct'] for x in leaves)]})

headline = []
for group, B, ref, target, expected in (
    ('C7',200,'DIRECT_SERIAL','DIRECT_PARALLEL',3.77),
    ('C7',200,'DIRECT_PARALLEL','A1_F',1.89),
    ('C8',200,'DIRECT_SERIAL','DIRECT_PARALLEL',7.28),
    ('C8',200,'DIRECT_PARALLEL','A1_F',-13.20),
    ('C9',200,'DIRECT_PARALLEL','DIRECT_POOL',-1.06)):
    c = comparison([x for x in rows if x['group'] == group and int(x['B']) == B], ref, target)
    assert round(c['median_ratio_reduction_pct'],2) == expected, (group,c)
    headline.append({'group':group,'B':B,**c})

c8 = [x for x in read('data/C8/window_values.csv') if x['role']=='performance' and int(x['B'])==200]
overlap = [x for x in c8 if x['package_overlap'].lower()=='true']
assert len(overlap)==1 and int(overlap[0]['block'])==2 and overlap[0]['arm_label']=='DIRECT_PARALLEL'
without = [dict(x,arm=x['arm_label']) for x in c8 if int(x['block'])!=2]
excluded = comparison(without,'DIRECT_PARALLEL','A1_F')
assert round(excluded['median_ratio_reduction_pct'],2)==-12.94 and excluded['positive_pairs']==0
report = {'status':'PASS_SENSITIVITY_AND_HEADLINE_REPLAY','lock_comparisons':locks,
          'P1_lobo':lobo,'headline':headline,'C8_overlap_run_id':overlap[0]['run_id'],
          'C8_leave_block_2_out':excluded,'all_original_formal_values_retained':True,
          'new_scientific_runs':0}
(R/'outputs/sensitivity_check.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
with (R/'outputs/P1_lock_comparisons.csv').open('w',encoding='utf-8',newline='') as f:
    columns=['B','reference','target','n','median_ratio_reduction_pct','paired_median_reduction_pct','positive_pairs']
    w=csv.DictWriter(f,fieldnames=columns,extrasaction='ignore');w.writeheader();w.writerows(locks)
print(json.dumps({'status':report['status'],'P1_lock_cells':len(locks),'P1_LOBO_comparisons':24,
                  'headline_checks':len(headline),'C8_without_block_2_pct':excluded['median_ratio_reduction_pct']}))
