"""Validate recorded diagnostic arithmetic only. No AIO or sampling is executed."""
import collections
import csv
import json
import math
from pathlib import Path
import statistics

R=Path(__file__).resolve().parents[1]
def read(rel):
    with (R/rel).open(encoding='utf-8-sig') as f:
        return list(csv.DictReader(f))

quota=read('data/figure_sources_A/resource_scenes.csv')
assert len(quota)==9 and collections.Counter(int(x['instances']) for x in quota)=={1:3,4:3,5:3}
for x in quota:
    count=int(x['instances']);n=min(count*13,64)
    assert int(x['setup_attempts'])==count*13
    assert int(x['setup_successes'])==int(x['destroy_successes'])==n
    assert int(x['aio_nr_held'])==int(x['aio_nr_idle'])==n*1024
    assert int(x['eagain_count'])==int(count==5) and int(x['aio_nr_after'])==0
    assert x['all_checks_pass']=='True'
assert sum(int(x['setup_attempts']) for x in quota)==390
assert sum(int(x['setup_successes']) for x in quota)==387
assert sum(int(x['destroy_successes']) for x in quota)==387

calls=read('data/diagnostics/C4/calls.csv')
assert len(calls)==2658 and collections.Counter(x['operation'] for x in calls)=={'io_setup':1329,'io_destroy':1329}
assert all(int(x['returncode'])==0 for x in calls)
for x in calls:
    assert abs((float(x['end_raw'])-float(x['start_raw']))-float(x['elapsed_s'])) < 1e-8
rounds=read('data/diagnostics/C4/rounds.csv')
centers=read('data/diagnostics/C4/case_centers.csv')
assert len(centers)==15
for x in centers:
    measured=[q for q in rounds if q['run_id']==x['run_id'] and q['warmup']=='0']
    if not measured:
        continue
    for operation in ('io_setup','io_destroy'):
        for a,b in [('wall','wall'),('sum_api','sum_api'),('union_api','union_api')]:
            val=statistics.median(float(q[operation+'_'+a+'_s']) for q in measured)
            assert abs(val-float(x[operation+'_round_median_'+b+'_s']))<1e-8
phases=read('data/diagnostics/C5/phases.csv')
per_operation=collections.Counter()
for x in phases:
    per_operation[x['operation']]+=int(x['group_size'])
assert per_operation=={'io_setup':1410,'io_destroy':1410}
cases=read('data/diagnostics/C5/cases.csv')
assert len(cases)==14
samples=read('data/diagnostics/C5/samples.csv')
valid=[x for x in samples if x['run_id'].startswith('D_') and x['alignment']=='MATCHED' and x['warmup']=='0']
assert len(valid)==3828
wchan=collections.Counter(x['wchan'] for x in valid)
assert wchan['__x64_sys_io_destroy']==3825 and wchan['0']==3
# Sampling observations are not independent replications or time fractions.
report={'status':'PASS_RECORDED_DIAGNOSTIC_ACCOUNTING','C4_processes':15,
        'C4_setup_successes':1329,'C4_destroy_successes':1329,'C5_processes':14,
        'C5_setup_successes':1410,'C5_destroy_successes':1410,
        'C5_valid_measured_snapshots':3828,'C5_wchan_counts':dict(wchan),
        'quota_scenes':9,'quota_setup_attempts':390,'quota_setup_successes':387,
        'quota_destroy_successes':387,'expected_EAGAIN':3,
        'idle_request_slots_per_process':13312,'system_slot_limit_in_diagnostic':65536,
        'idle_slot_fraction_pct':100*13312/65536,'new_scientific_runs':0}
(R/'outputs/diagnostic_check.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
