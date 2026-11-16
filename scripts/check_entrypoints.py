"""Bounded transport checks using synthetic paths; no native executable runs."""
import importlib.util
import json
from pathlib import Path
import tempfile

R=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('window_planner',R/'scripts/run_native_window.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

with tempfile.TemporaryDirectory(prefix='aio artifact path ') as temp:
    base=Path(temp)
    config={'variant':'C9','arm':'DIRECT_PARALLEL','B':200,'binary':'build/overall',
            'fixture':'prepared input/fixture','initial_snapshot_prefix':'original snapshot/work',
            'output':'new window'}
    plan=m.build_plan(config,base)
    assert len(plan['argv'])==25 and plan['argv'][24]=='2'
    assert plan['argv'][13]==str((base/'new window/index/work').resolve())
    assert plan['argv'][23]=='50'
    assert plan['environment']['GRT_DRIVER_RESULT_ROOT']==str((base/'new window/driver_results').resolve())
    assert len(plan['final_fsync_targets'])==8
    assert plan['expected_creates_and_destroys']==653
    pool=m.build_plan(dict(config,arm='A1_F'),base)
    assert pool['expected_creates_and_destroys']==13
    resident=m.build_plan(dict(config,arm='DIRECT_POOL'),base)
    assert resident['environment']['GRT_RECLAIM_PARALLEL']=='2'
    large=m.build_plan(dict(config,variant='C8',B=2000),base)
    assert large['argv'][23]=='5' and large['expected_creates_and_destroys']==68
    rejected=0
    for update in ({'variant':'C8','arm':'DIRECT_POOL'}, {'variant':'C9','arm':'DIRECT_SERIAL'},
                   {'variant':'C9','B':2000}, {'B':333}):
        try:m.build_plan(dict(config,**update),base)
        except ValueError:rejected+=1
    assert rejected==4
    (base/'new window').mkdir()
    try:m.build_plan(config,base)
    except ValueError:pass
    else:raise RuntimeError('Existing output was accepted')

print(json.dumps({'status':'PASS_TRANSPORT_ENTRYPOINT_CHECKS',
                  'synthetic_path_and_policy_checks':True,'new_scientific_runs':0,
                  'native_executables_run':0}))
