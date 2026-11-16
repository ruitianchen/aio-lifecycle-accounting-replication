"""External monotonic launch-to-fsync timing with wait4 RSS and fail-closed status."""
from datetime import datetime,timezone
import os
from pathlib import Path
import re
import signal
import subprocess
import time

def run_process(argv, log, env, cwd, limit_s, rss_limit_bytes, fsync_paths=()):
    if Path(log).exists():raise ValueError('Refuse overwrite '+str(log))
    if limit_s<=0:raise ValueError('Positive execution budget required')
    started_utc=datetime.now(timezone.utc).isoformat()
    failure=None;observed_peak=0
    with Path(log).open('x') as stream:
        start=time.clock_gettime(time.CLOCK_MONOTONIC_RAW)
        child=subprocess.Popen(list(map(str,argv)),stdout=stream,stderr=subprocess.STDOUT,env=env,cwd=cwd)
        try:
            while True:
                pid,status,usage=os.wait4(child.pid,os.WNOHANG)
                if pid:
                    code=os.waitstatus_to_exitcode(status);child.returncode=code;break
                try:
                    text=Path(f'/proc/{child.pid}/status').read_text()
                    match=re.search(r'^VmRSS:\s+(\d+)',text,re.M)
                    if match:observed_peak=max(observed_peak,int(match.group(1))*1024)
                except FileNotFoundError:pass
                if observed_peak>rss_limit_bytes:failure='RSS_BOUND'
                if time.clock_gettime(time.CLOCK_MONOTONIC_RAW)-start>limit_s:failure='TIMEOUT'
                if failure:
                    try:os.kill(child.pid,signal.SIGKILL)
                    except ProcessLookupError:pass
                    _,status,usage=os.wait4(child.pid,0)
                    code=os.waitstatus_to_exitcode(status);child.returncode=code;break
                time.sleep(.005)
        except BaseException:
            if child.returncode is None:
                try:os.kill(child.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                _,status,_=os.wait4(child.pid,0);child.returncode=os.waitstatus_to_exitcode(status)
            raise
        exit_observed_s=time.clock_gettime(time.CLOCK_MONOTONIC_RAW)-start
        peak_rss=max(observed_peak,int(usage.ru_maxrss)*1024)
        if peak_rss>rss_limit_bytes:failure='RSS_BOUND'
        if code and not failure:failure='NONZERO_EXIT'
        if not failure:
            try:
                for path in fsync_paths:
                    fd=os.open(path,os.O_RDONLY)
                    try:os.fsync(fd)
                    finally:os.close(fd)
            except OSError as exc:failure='FSYNC_FAILED: '+str(exc)
        end=time.clock_gettime(time.CLOCK_MONOTONIC_RAW)
    return {'argv':list(map(str,argv)),'log':str(log),'started_at_utc':started_utc,
        'ended_at_utc':datetime.now(timezone.utc).isoformat(),'returncode':code,'failure':failure,
        'completion':'FAILED' if failure else ('EXITED_AND_FSYNCED' if fsync_paths else 'EXITED'),
        'process_exit_observed_s':exit_observed_s,'window_complete_s':end-start,
        'peak_rss_bytes':peak_rss,'rss_evidence':'wait4 ru_maxrss plus live /proc samples',
        'fsync_targets':list(map(str,fsync_paths))}
