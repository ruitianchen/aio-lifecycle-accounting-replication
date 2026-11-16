"""Restricted original-order validation; no approximate equivalence expansion."""
from pathlib import Path
import struct
import numpy as np

def require(ok,msg):
    if not ok:raise ValueError(msg)

def loadbin(path,dtype):
    with Path(path).open('rb') as f:n,d=struct.unpack('<II',f.read(8))
    require(Path(path).stat().st_size==8+n*d*np.dtype(dtype).itemsize,'Binary shape')
    return np.memmap(path,dtype=dtype,mode='r',offset=8,shape=(n,d))

def allowed_order(expected,actual,raw,pq):
    expected=list(map(int,expected));actual=list(map(int,actual))
    if len(set(expected))!=len(expected) or len(set(actual))!=len(actual) or set(expected)!=set(actual):return False
    return all(i==j or (raw(i)==raw(j) and pq(i)==pq(j)) for i,j in zip(expected,actual))

def exact_query_distances(queries,vectors,ids):
    delta=np.asarray(queries,dtype=np.float64)[:,None,:]-np.asarray(vectors[ids],dtype=np.float64)
    return np.sort(np.einsum('qkd,qkd->qk',delta,delta),axis=1)

def query_signature(prefix,fixture,stage):
    x=loadbin(fixture/'full.bin','<f4');q=loadbin(fixture/'query.bin','<f4')
    ids=loadbin(str(prefix)+'_100_tags_uint32.bin','<u4')
    stored=loadbin(str(prefix)+'_100_dists_float.bin','<f4')
    live=set(map(int,np.load(fixture/f'live{stage}.npy')))
    require(ids.shape==stored.shape==(232,10),'Query result shape')
    require(all(len(set(map(int,row)))==10 and set(map(int,row))<=live for row in ids),'Invalid query labels')
    delta=np.asarray(q,dtype=np.float64)[:,None,:]-np.asarray(x[ids],dtype=np.float64)
    recomputed=np.einsum('qkd,qkd->qk',delta,delta)
    require(np.all(np.isfinite(stored)) and np.allclose(stored,recomputed,rtol=1e-5,atol=1e-5),'Stored query distance mismatch')
    return np.sort(recomputed,axis=1)

def topk(queries,labels,vectors,k=10,block_size=8192):
    require(len(labels)>=k and k>0 and block_size>0,'Truth shape')
    q=np.asarray(queries,dtype=np.float64);qn=np.einsum('ij,ij->i',q,q)
    best=np.full((len(q),k),np.inf);ids=np.zeros((len(q),k),dtype='<u4')
    for offset in range(0,len(labels),block_size):
        li=labels[offset:offset+block_size];block=np.asarray(vectors[li],dtype=np.float64)
        distances=qn[:,None]+np.einsum('ij,ij->i',block,block)[None,:]-2*(q@block.T)
        combined=np.concatenate((best,distances),axis=1)
        candidates=np.concatenate((ids,np.broadcast_to(li,(len(q),len(li)))),axis=1)
        take=np.argpartition(combined,k-1,axis=1)[:,:k]
        best=np.take_along_axis(combined,take,axis=1);ids=np.take_along_axis(candidates,take,axis=1)
    order=np.lexsort((ids,best),axis=1)
    return np.take_along_axis(ids,order,axis=1),np.take_along_axis(best,order,axis=1)

def compare_indexes(actual,expected,actual_hashes,expected_hashes):
    if actual_hashes==expected_hashes:return {'status':'BYTE_IDENTICAL','needs_independent_query':False,'changed_nodes':[]}
    for suffix in actual_hashes:
        if suffix not in ('_disk.index','_disk.index_with_only_nbrs'):
            require(actual_hashes[suffix]==expected_hashes[suffix],'Non-graph file differs: '+suffix)
    def mapped(prefix,suffix):return np.memmap(str(prefix)+suffix,dtype='u1',mode='r')
    a=mapped(actual,'_disk.index');e=mapped(expected,'_disk.index')
    require(a.shape==e.shape and np.array_equal(a[:4096],e[:4096]),'Graph size/header differs')
    n,d,med,record,per,frozen,loc,size=struct.unpack_from('<8Q',e,8)
    cap=(record-d*4)//4-1;require(per==4096//record,'Graph packing')
    tags=loadbin(str(expected)+'_disk.index.tags','<u4').ravel();pq=loadbin(str(expected)+'_pq_compressed.bin','u1')
    diff=np.flatnonzero(a!=e);nodes=set()
    for offset in diff:
        page,pos=divmod(int(offset),4096);slot,inside=divmod(pos,record);node=(page-1)*per+slot
        require(page>0 and slot<per and 0<=node<n and tags[node]!=np.uint32(4294967295),'Graph padding/inactive-node difference')
        off=4096*page+slot*record;degree=struct.unpack_from('<I',e,off+d*4)[0]
        require(d*4+4<=inside<d*4+4+degree*4,'Difference outside active adjacency')
        nodes.add(node)
    aa=mapped(actual,'_disk.index_with_only_nbrs');ee=mapped(expected,'_disk.index_with_only_nbrs')
    require(aa.shape==ee.shape and np.array_equal(aa[:4096],ee[:4096]),'Auxiliary size/header differs')
    tlen=(cap+1)*4;tper=4096//tlen;toponodes=set()
    for offset in np.flatnonzero(aa!=ee):
        page,pos=divmod(int(offset),4096);slot,inside=divmod(pos,tlen);node=(page-1)*tper+slot
        require(page>0 and slot<tper and 0<=node<n and tags[node]!=np.uint32(4294967295),'Aux padding/inactive difference')
        toff=page*4096+slot*tlen;degree=struct.unpack_from('<I',ee,toff)[0]
        require(4<=inside<4+degree*4,'Aux difference outside adjacency')
        toponodes.add(node)
    require(nodes==toponodes and bool(nodes),'Main/aux changed-node mismatch')
    def raw(node):
        off=4096*(1+node//per)+(node%per)*record
        return e[off:off+d*4].tobytes()
    changed=[]
    for node in sorted(nodes):
        off=4096*(1+node//per)+(node%per)*record
        ad=struct.unpack_from('<I',a,off+d*4)[0];ed=struct.unpack_from('<I',e,off+d*4)[0]
        require(ad==ed and ad<=cap,'Node degree differs')
        an=np.frombuffer(a,dtype='<u4',count=ad,offset=off+d*4+4)
        en=np.frombuffer(e,dtype='<u4',count=ed,offset=off+d*4+4)
        require(allowed_order(en,an,raw,lambda i:pq[i].tobytes()),'Not a duplicate-vector/PQ permutation')
        toff=4096*(1+node//tper)+(node%tper)*tlen
        require(struct.unpack_from('<I',aa,toff)[0]==ad and struct.unpack_from('<I',ee,toff)[0]==ed,'Aux degree mismatch')
        require(np.array_equal(an,np.frombuffer(aa,dtype='<u4',count=ad,offset=toff+4)) and np.array_equal(en,np.frombuffer(ee,dtype='<u4',count=ed,offset=toff+4)),'Main/aux neighbor order mismatch')
        changed.append({'node':node,'positions':[[i,int(x),int(y)] for i,(x,y) in enumerate(zip(en,an)) if x!=y]})
    return {'status':'DUPLICATE_VECTOR_PQ_PERMUTATION_REQUIRES_QUERY','needs_independent_query':True,'changed_nodes':changed,'main_differing_bytes':len(diff)}
