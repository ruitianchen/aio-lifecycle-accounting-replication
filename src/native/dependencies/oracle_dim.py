"""Independent final-file structure, label/vector and query-distance oracle."""
import argparse
from graph_header_revision1 import parse_graph_header
import hashlib
import json
from pathlib import Path
import struct
import numpy as np

def require(ok,msg):
    if not ok:raise ValueError(msg)
def read_bin(p,dtype):
    raw=Path(p).read_bytes();n,d=struct.unpack_from('<II',raw)
    out=np.frombuffer(raw,dtype=dtype,offset=8)
    require(out.size==n*d,'binary shape '+str(p));return out.reshape(n,d)
def check(prefix,fixture,stage,result_prefix):
    x=read_bin(fixture/'full.bin','<f4');q=read_bin(fixture/'query.bin','<f4')
    live=np.load(fixture/f'live{stage}.npy');live_set=set(map(int,live))
    graph=Path(str(prefix)+'_disk.index').read_bytes();nr,nc=struct.unpack_from('<II',graph)
    n,d,med,record,per,frozen,loc,size=parse_graph_header(graph)
    require(d==x.shape[1] and frozen==0 and len(graph)==size and per==4096//record,'graph metadata')
    cap=(record-d*4)//4-1
    tags=read_bin(str(prefix)+'_disk.index.tags','<u4').ravel();require(len(tags)==n,'tag length')
    valid=tags!=np.uint32(4294967295)
    require(len(set(map(int,tags[valid])))==len(live) and set(map(int,tags[valid]))==live_set,'live labels')
    topo=Path(str(prefix)+'_disk.index_with_only_nbrs').read_bytes();tn,tr=struct.unpack_from('<QQ',topo)
    require((tn,tr)==(n,cap),'aux header');tlen=(cap+1)*4;tper=4096//tlen
    require(len(topo)==(1+(n+tper-1)//tper)*4096,'aux size')
    edge_count=0
    for i in range(n):
        if not valid[i]:continue
        off=4096*(1+i//per)+(i%per)*record
        vec=np.frombuffer(graph,dtype='<f4',count=d,offset=off)
        require(np.array_equal(vec,x[tags[i]]),'vector/tag mismatch '+str(i))
        degree=struct.unpack_from('<I',graph,off+d*4)[0];require(degree<=cap,'degree overflow')
        nbr=np.frombuffer(graph,dtype='<u4',count=degree,offset=off+d*4+4)
        require(np.all(nbr<n),'out-of-range edge');require(np.all(valid[nbr]),'edge to deleted slot')
        require(len(set(map(int,nbr)))==degree,'duplicate edge')
        toff=4096*(1+i//tper)+(i%tper)*tlen;td=struct.unpack_from('<I',topo,toff)[0]
        require(td==degree and np.array_equal(nbr,np.frombuffer(topo,dtype='<u4',count=td,offset=toff+4)),'graph/aux mismatch '+str(i))
        edge_count+=degree
    pq=read_bin(str(prefix)+'_pq_compressed.bin','u1');require(pq.shape[0]==n and pq.shape[1]==32,'PQ shape')
    result=read_bin(str(result_prefix)+'_100_tags_uint32.bin','<u4')
    distances=read_bin(str(result_prefix)+'_100_dists_float.bin','<f4')
    require(result.shape==distances.shape==(232,10),'query result shapes')
    require(all(len(set(map(int,row)))==10 and set(map(int,row))<=live_set for row in result),'query IDs invalid/deleted/duplicate')
    delta=q.astype(np.float64)[:,None,:]-x[result].astype(np.float64)
    recomputed=np.einsum('qkd,qkd->qk',delta,delta)
    require(np.all(np.isfinite(distances)) and np.allclose(distances,recomputed,rtol=1e-5,atol=1e-5),'query distances')
    gt_raw=(fixture/f'gt{stage}.fbin').read_bytes();gt=np.frombuffer(gt_raw,dtype='<u4',count=2320,offset=8).reshape(232,10)
    kth=np.load(fixture/f'd10_stage{stage}.npy')
    strict=np.array([len(set(map(int,g))&set(map(int,r))) for g,r in zip(gt,result)])
    tie=(recomputed<=kth[:,None]+1e-9*np.maximum(1.,np.abs(kth[:,None]))).sum(axis=1)
    return {'stage':stage,'structure_pass':True,'query_checks_pass':True,'live_points':len(live),
        'graph_sha256':hashlib.sha256(graph).hexdigest(),'edges':edge_count,
        'strict_global':float(strict[:200].sum()/2000),'strict_local':float(strict[200:].sum()/320),
        'tie_global':float(tie[:200].sum()/2000),'tie_local':float(tie[200:].sum()/320),
        'raw_strict_hits':strict.tolist(),'raw_tie_hits':tie.tolist(),
        'limits':['one frozen graph for this dataset','PQ shape checked; exact PQ encoder equivalence not yet checked']}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--prefix',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--stage',type=int,required=True);p.add_argument('--result-prefix',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();require(not a.out.exists(),'refuse overwrite')
    report=check(a.prefix,a.fixture,a.stage,a.result_prefix)
    a.out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if not k.startswith('raw_')}))
