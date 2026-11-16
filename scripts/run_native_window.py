"""Plan one external-input Linux window; --execute is explicit and never retried.

This portable transport wrapper is new artifact code, not an original campaign
controller. The offline replay never imports or calls this script.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[1]
SUFFIXES=('_disk.index','_disk.index.tags','_disk.index_with_only_nbrs',
          '_pq_compressed.bin','_pq_pivots.bin','_sample_data.bin','_sample_ids.bin')

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
    return h.hexdigest()

def build_plan(config,config_dir):
    def resolve(value):
        p=Path(value).expanduser()
        return (p if p.is_absolute() else config_dir/p).resolve()
    variant=config['variant'];arm=config['arm'];B=int(config['B'])
    if variant not in ('C8','C9') or arm not in ('DIRECT_SERIAL','DIRECT_PARALLEL','DIRECT_POOL','A1_F'):
        raise ValueError('Unknown source variant or arm')
    if B not in (200,2000) or (variant=='C9' and (B!=200 or arm=='DIRECT_SERIAL')):
        raise ValueError('Setting outside the documented variant matrix')
    if variant=='C8' and arm=='DIRECT_POOL':
        raise ValueError('C8 has no resident-worker branch; select C9')
    fixture=resolve(config['fixture'])
    snapshot=resolve(config['initial_snapshot_prefix'])
    out=resolve(config['output'])
    if out.exists():raise ValueError('Output exists; choose a new window directory')
    binary=resolve(config['binary'])
    prefix=out/'index/work'
    argv=[str(binary),'float',str(fixture/'base.bin'),'100','34','1.2','100','34','1.2',
          '0','1','32','0',str(prefix),'true','false',str(fixture/'full.bin'),
          str(fixture/'query.bin'),str(fixture/'gt'),'10','100','4',
          str(fixture/f'B{B}/trace'),str(10000//B),'2']
    assert len(argv)==25
    detail=bool(config.get('detailed_qualification',True))
    env={'OPENBLAS_NUM_THREADS':'1','OMP_NUM_THREADS':'4','OMP_DYNAMIC':'FALSE',
         'OMP_MAX_ACTIVE_LEVELS':'1','TBB_NUM_THREADS':'1','PYTHONHASHSEED':'0',
         'GRT_ARM':'A1_F' if arm=='A1_F' else 'A0_DIRECT',
         'GRT_COMPOSE_A':'1' if arm=='A1_F' else '0','GRT_FINE_LOCK':'1',
         'GRT_EXEC_PARALLEL':'0','GRT_COMPOSE_C':'none',
         'GRT_RECLAIM_PARALLEL':str({'DIRECT_SERIAL':0,'DIRECT_PARALLEL':1,'DIRECT_POOL':2,'A1_F':0}[arm]),
         'GRT_COMPOSE_METRICS':str(out/'runtime.json'),
         'GRT_TRACE':'1' if detail else '0','GRT_TRACE_PATH':str(out/'events.tsv'),
         'GRT_PHASE_PATH':str(out/'phases.tsv'),'GRT_MAINTENANCE_POLICY':'per_batch',
         'GRT_DRIVER_RESULT_ROOT':str(out/'driver_results')}
    if detail:env['GRT_COMPOSE_SNAPSHOT_ROOT']=str(out/'snapshots')
    expected=(10+3)*(10000//B)+3 if arm!='A1_F' else 13
    return {'variant':variant,'arm':arm,'B':B,'argv':argv,'environment':env,
            'fixture':str(fixture),'snapshot_prefix':str(snapshot),'output':str(out),
            'final_fsync_targets':[str(prefix)+s for s in SUFFIXES]+[str(prefix.parent)],
            'expected_creates_and_destroys':expected,'detailed_qualification':detail,
            'automatic_retry':False,'limit_s':int(config.get('limit_s',600)),
            'rss_limit_bytes':int(config.get('rss_limit_bytes',24*1024**3)),
            'affinity':config.get('affinity')}

def verify_inputs(plan):
    manifest=json.loads((ROOT/'configs/native_input_hashes.json').read_text(encoding='utf-8'))
    fixture=Path(plan['fixture']);snapshot=Path(plan['snapshot_prefix'])
    for item in manifest['fixture_files']:
        p=fixture/item['relative_path']
        if not p.is_file() or p.stat().st_size!=item['bytes'] or digest(p)!=item['sha256']:
            raise ValueError('Missing or changed fixture input: '+item['relative_path'])
    for item in manifest['initial_index_files']:
        p=Path(str(snapshot)+item['suffix'])
        if not p.is_file() or digest(p)!=item['sha256']:
            raise ValueError('Missing or changed initial index: '+item['suffix'])
    if not Path(plan['argv'][0]).is_file():raise ValueError('Build the selected binary first')

def execute(plan):
    if sys.platform!='linux':raise ValueError('Native measurement requires Linux')
    verify_inputs(plan)
    if plan['limit_s']<=0 or plan['rss_limit_bytes']<=0:raise ValueError('Positive budgets required')
    quota=Path('/proc/sys/fs/aio-nr')
    if int(quota.read_text())!=0:raise ValueError('External AIO occupancy: stop without changing system quota')
    if int(Path('/proc/sys/fs/aio-max-nr').read_text())!=65536:
        raise ValueError('AIO quota differs from the archived protocol; no setting is changed')
    out=Path(plan['output']);out.mkdir(parents=True,exist_ok=False)
    def save(name,value):
        (out/name).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    save('portable_window_plan.json',plan)
    (out/'index/work_temp').mkdir(parents=True)
    (out/'driver_results').mkdir()
    for suffix in SUFFIXES:
        src=Path(plan['snapshot_prefix']+suffix);dest=out/('index/work'+suffix)
        shutil.copyfile(src,dest)
        if digest(src)!=digest(dest):raise ValueError('Restored index hash mismatch')
    # Flush restoration outside the task timer. Never remove or overwrite input files.
    for name in plan['final_fsync_targets']:
        fd=os.open(name,os.O_RDONLY)
        try:os.fsync(fd)
        finally:os.close(fd)
    if plan['detailed_qualification']:
        for n in (2000,4000,6000,8000,10000):
            (out/f'snapshots/stage{n}').mkdir(parents=True)
    env=dict(os.environ)
    for key in list(env):
        if key.startswith('GRT_'):env.pop(key)
    env.update(plan['environment'])
    if plan['affinity'] is not None:
        allowed=set(os.sched_getaffinity(0));chosen=set(map(int,plan['affinity']))
        if not chosen or not chosen<=allowed:raise ValueError('CPU set is unavailable on this host')
        os.sched_setaffinity(0,chosen)
    spec=importlib.util.spec_from_file_location('artifact_execution',ROOT/'src/native/dependencies/execution_native.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    result=module.run_process(plan['argv'],out/'stdout.log',env,ROOT,plan['limit_s'],
                              plan['rss_limit_bytes'],plan['final_fsync_targets'])
    save('execution.json',result)
    if result['completion']!='EXITED_AND_FSYNCED':
        raise ValueError('Window stopped; retain output and do not retry automatically')
    rt=json.loads((out/'runtime.json').read_text())
    for key in ('aio_live','aio_borrowed','aio_live_capacity','aio_pending','dirty','resident'):
        if rt[key]!=0:raise ValueError('Final resource ledger is not zero: '+key)
    if rt['aio_setup']!=plan['expected_creates_and_destroys'] or rt['aio_destroy']!=plan['expected_creates_and_destroys']:
        raise ValueError('Lifecycle count differs from the selected task')
    if int(quota.read_text())!=0:raise ValueError('Final system AIO occupancy is not zero')
    save('completion.json',{'status':'COMPLETED_REQUIRES_INDEPENDENT_CORRECTNESS_AND_QUALIFICATION_AUDIT',
         'native_window_completed':True,'original_archived_data_changed':False,
         'output_index_sha256':{s:digest(out/('index/work'+s)) for s in SUFFIXES},
         'complete_s':result['window_complete_s'],'automatic_retry':False})
    print('Window completed. Run the independent output/query checks described in the native reproduction guide.')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('config',type=Path)
    p.add_argument('--verify-inputs',action='store_true')
    p.add_argument('--execute',action='store_true')
    a=p.parse_args()
    plan=build_plan(json.loads(a.config.read_text(encoding='utf-8')),a.config.resolve().parent)
    if a.verify_inputs:verify_inputs(plan)
    if a.execute:execute(plan)
    else:print(json.dumps(plan,indent=2))

if __name__=='__main__':
    main()
