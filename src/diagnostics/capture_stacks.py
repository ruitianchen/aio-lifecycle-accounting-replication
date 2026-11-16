"""Sample only this wrapper's own native child. Never attaches or sends ptrace."""
from pathlib import Path
import csv
import errno
import gzip
import json
import subprocess
import sys
import time
import traceback
from sample_alignment import align_samples

R=Path(__file__).resolve().parent
raw=lambda:time.clock_gettime(time.CLOCK_MONOTONIC_RAW)

def api_calls(path):
    with path.open() as f:rows=list(csv.DictReader(f))
    integer={'warmup','round','group_size','slot','pid','tid','returncode'}
    for c in rows:
        for k in c:
            if k!='operation':c[k]=int(c[k]) if k in integer else float(c[k])
    return rows

def main():
    case=json.loads(Path(sys.argv[1]).read_text())
    plan=json.loads((R/'protocol.json').read_text())
    folder=Path(sys.argv[1]).parent
    prefix=folder/'result'
    numbers=json.loads((R/'syscall_numbers.json').read_text())['numbers']
    reverse={v:k for k,v in numbers.items()}
    rows=[];encoded=[];size=sweeps=reads=skips=0;child=None;failure=None
    started=raw()
    try:
        child=subprocess.Popen([str(R/'native_aio'),case['mode'],str(case['warmup']),str(case['measured']),str(prefix),str(case['group1']),str(case['group2'])])
        while child.poll() is None:
            if raw()-started>50:
                raise TimeoutError('Wrapper 50-second internal bound')
            if case['sampling']:
                sweeps+=1
                try:tasks=sorted((Path('/proc')/str(child.pid)/'task').iterdir(),key=lambda p:int(p.name))
                except (FileNotFoundError,ProcessLookupError):tasks=[]
                for task in tasks:
                    tid=int(task.name)
                    if tid==child.pid:continue
                    begin=raw();reads+=1;errors=[]
                    try:syscall=(task/'syscall').read_text().strip()
                    except OSError as exc:
                        if exc.errno not in plan['transient_proc_errnos']:raise
                        syscall='';errors.append({'field':'syscall','errno':exc.errno,'error':repr(exc)})
                    token=syscall.split(maxsplit=1)[0] if syscall else ''
                    number=int(token) if token.lstrip('-').isdigit() else None
                    if number not in reverse and not errors:
                        skips+=1
                        continue
                    stack=wchan=''
                    if not errors:
                        for name in ['stack','wchan']:
                            try:value=(task/name).read_text().strip()
                            except OSError as exc:
                                if exc.errno not in plan['transient_proc_errnos']:raise
                                value='';errors.append({'field':name,'errno':exc.errno,'error':repr(exc)})
                            if name=='stack':stack=value
                            else:wchan=value
                    sample={'pid':child.pid,'tid':tid,'operation':reverse.get(number,'UNAVAILABLE'),
                        'read_begin_raw':begin,'read_end_raw':raw(),'syscall':syscall,'stack':stack,'wchan':wchan,'errors':errors}
                    b=(json.dumps(sample,separators=(',',':'))+'\n').encode()
                    size+=len(b)
                    if len(rows)>=plan['max_captured_samples_per_case'] or size>plan['max_buffer_bytes_per_case']:
                        raise MemoryError('Frozen sample buffer bound')
                    rows.append(sample);encoded.append(b)
            time.sleep(plan['sampling_interval_after_sweep_s'])
        if child.returncode:
            raise RuntimeError('Native child exit '+str(child.returncode))
        calls=api_calls(Path(str(prefix)+'.calls.csv'))
        alignments=align_samples(rows,calls)
        matched=[x for x in alignments if x['alignment']=='MATCHED' and x['has_destroy_frame'] and not x['warmup']]
        summary={'status':'PASS','sampling':case['sampling'],'child_pid':child.pid,
            'samples':len(rows),'buffer_bytes':size,'sweeps':sweeps,'syscall_reads':reads,'non_aio_reads_skipped':skips,
            'matched_measured_destroy_samples':len(matched),
            'matched_measured_destroy_calls':len({(x['round'],x['group_size'],x['slot']) for x in matched}),
            'unexpected_access_errors':0,'failure':None,'elapsed_raw_s':raw()-started,
            'interpretation':'Containment-aligned snapshots only; sample counts are not time fractions or independent replicates'}
        (folder/'sample_alignment.json').write_text(json.dumps(alignments,indent=2))
    except BaseException as exc:
        failure=repr(exc)
        summary={'status':'FAIL','failure':failure,'traceback':traceback.format_exc(),'sampling':case['sampling'],'child_pid':getattr(child,'pid',None),'samples':len(rows),'elapsed_raw_s':raw()-started}
    finally:
        if child is not None and child.poll() is None:
            child.terminate()
            try:child.wait(timeout=3)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        with gzip.open(folder/'kernel_samples.jsonl.gz','xb') as f:
            for b in encoded:f.write(b)
        (folder/'capture_summary.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary),flush=True)
    return 2 if failure else 0

if __name__=='__main__':
    sys.exit(main())
