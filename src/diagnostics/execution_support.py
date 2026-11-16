"""Portable support for the archived quota scene function; no remote deployment.

This artifact adapter is newly written. The original quota implementation and
archived measurements are unchanged. No function runs at module import.
"""
from pathlib import Path
import hashlib
import json
import shutil
import time

def raw():
    return time.clock_gettime(time.CLOCK_MONOTONIC_RAW)

def read(path):
    return Path(path).read_text().strip()

def require(condition,message):
    if not condition:raise RuntimeError(message)

def save(path,value):
    p=Path(path)
    p.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
    return h.hexdigest()

def settings():
    patterns=('/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor',
              '/sys/devices/system/cpu/cpu*/cpufreq/scaling_driver',
              '/sys/devices/system/cpu/intel_pstate/no_turbo',
              '/sys/devices/system/cpu/cpufreq/boost')
    found={}
    import glob
    for pattern in patterns:
        for path in sorted(glob.glob(pattern)):found[path]=read(path)
    found['/proc/sys/fs/aio-max-nr']=read('/proc/sys/fs/aio-max-nr')
    return found

def quiet(folder,root,expected_settings,timeout_s):
    started=raw();previous=None;clean=0;samples=[]
    while raw()-started<timeout_s:
        stat=list(map(int,read('/proc/stat').splitlines()[0].split()[1:]))
        total=sum(stat[:8]);idle=stat[3]+stat[4]
        busy=1.0 if previous is None else 1-(idle-previous[1])/max(1,total-previous[0])
        previous=(total,idle)
        mem={l.split(':')[0]:int(l.split()[1])*1024 for l in read('/proc/meminfo').splitlines()}
        vm={l.split()[0]:int(l.split()[1]) for l in read('/proc/vmstat').splitlines()}
        sample={'raw':raw(),'cpu_busy':busy,'mem_available':mem['MemAvailable'],
                'aio_nr':int(read('/proc/sys/fs/aio-nr')),'disk_free':shutil.disk_usage(root).free,
                'pswpin':vm['pswpin'],'pswpout':vm['pswpout']}
        swapped=bool(samples and any(sample[k]!=samples[-1][k] for k in ('pswpin','pswpout')))
        good=(busy<.30 and sample['mem_available']>=6*1024**3 and sample['aio_nr']==0
              and sample['disk_free']>=20*1024**3 and not swapped and settings()==expected_settings)
        clean=clean+1 if good else 0;samples.append(sample)
        if clean>=5:
            save(Path(folder)/'portable_quiet_gate.json',{'samples':samples,'gate':'new_artifact_adapter'})
            return
        time.sleep(.5)
    raise TimeoutError('Quiet gate not met; no settings changed and no automatic retry')
