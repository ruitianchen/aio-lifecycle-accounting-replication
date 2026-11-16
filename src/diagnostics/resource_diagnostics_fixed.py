"""Controlled quota scenes; no graph maintenance, tuning, or system-setting writes."""
from pathlib import Path
import ctypes,ctypes.util,errno,json,multiprocessing as mp,os,time
from execution_support import *
def load_libaio():
    name=ctypes.util.find_library('aio')
    require(bool(name),'No libaio SONAME found')
    lib=ctypes.CDLL(name)
    lib.io_setup.argtypes=[ctypes.c_int,ctypes.POINTER(ctypes.c_ulong)];lib.io_setup.restype=ctypes.c_int
    lib.io_destroy.argtypes=[ctypes.c_ulong];lib.io_destroy.restype=ctypes.c_int
    return lib,name

def child(pipe):
    lib,library_name=load_libaio()
    contexts=[];created=[];failed=None;destroyed=[]
    try:
        for i in range(13):
            ctx=ctypes.c_ulong(0);start=raw();rc=lib.io_setup(1024,ctypes.byref(ctx));end=raw()
            created.append({'ordinal':i,'start_raw':start,'end_raw':end,'returncode':rc})
            if rc:failed=rc;break
            contexts.append(ctx.value)
        pipe.send({'pid':os.getpid(),'created':created,'live_contexts':len(contexts),'failed':failed,'library_soname':library_name})
        require(pipe.poll(15),'Parent release command timeout');require(pipe.recv()=='release','Invalid release command')
    finally:
        for ctx in contexts:
            start=raw();rc=lib.io_destroy(ctx);destroyed.append({'start_raw':start,'end_raw':raw(),'returncode':rc})
        pipe.send({'pid':os.getpid(),'destroyed':destroyed});pipe.close()
def run(root,expected_settings):
    root=Path(root);out=root/'resources';out.mkdir();records=[];start=raw();context=mp.get_context('spawn')
    for count in [1,4,5]:
        for repeat in range(1,4):
            require(raw()-start<3600,'Resource stage budget exhausted');folder=out/f'instances{count}_repeat{repeat}';folder.mkdir()
            quiet(folder,root,expected_settings,600);require(int(read('/proc/sys/fs/aio-max-nr'))==65536,'Quota setting changed')
            pipes=[];processes=[];allocated=[];reclaimed=[]
            try:
                for _ in range(count):
                    parent,conn=context.Pipe();proc=context.Process(target=child,args=(conn,));proc.start();conn.close();pipes.append(parent);processes.append(proc)
                for pipe in pipes:require(pipe.poll(15),'Allocation diagnostic timeout');allocated.append(pipe.recv())
                held=int(read('/proc/sys/fs/aio-nr'));time.sleep(.5);held_idle=int(read('/proc/sys/fs/aio-nr'))
                require(held==held_idle==sum(x['live_contexts'] for x in allocated)*1024,'Held quota does not match allocation ledger')
                for pipe in pipes:pipe.send('release')
                for pipe in pipes:require(pipe.poll(15),'Reclamation diagnostic timeout');reclaimed.append(pipe.recv())
                for proc in processes:proc.join(5);require(proc.exitcode==0,'Diagnostic child failed')
                final=int(read('/proc/sys/fs/aio-nr'));require(final==0,'Resource diagnostic did not restore baseline')
                require(all(x['failed'] in [None,-errno.EAGAIN] for x in allocated),'Unexpected allocation failure')
                if count<5:require(all(x['failed'] is None for x in allocated),'Unexpected shortage in lower-concurrency scene')
                require(all(x['returncode']==0 for r in reclaimed for x in r['destroyed']),'Context destruction failed')
                record={'instances':count,'repeat':repeat,'allocated':allocated,'reclaimed':reclaimed,'aio_nr_held':held,'aio_nr_idle':held_idle,'aio_nr_after':final,'interpretation':'AIO quota diagnostic; not ANN throughput or runtime timeout-eviction evidence'}
                save(folder/'result.json',record);records.append(record);save(out/'results.json',records)
            finally:
                # Ownership-limited release on any error; never touch contexts of other processes.
                for pipe,proc in zip(pipes,processes):
                    if proc.is_alive():
                        try:pipe.send('release')
                        except (BrokenPipeError,EOFError,OSError):pass
                        proc.join(15)
                    if proc.is_alive():proc.terminate();proc.join(5)
                    pipe.close()
    return records
