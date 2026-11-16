"""Integer-nanosecond disjoint temporal accounting; no causal allocation."""
from collections import defaultdict

def partition(start,end,intervals):
    if end<=start:raise ValueError('Invalid window')
    root={'start':start,'end':end,'label':'window_other','children':[]}
    stack=[root]
    for original in sorted(intervals,key=lambda r:(r['start'],-r['end'],r['label'])):
        node={**original,'children':[]}
        a,b=node['start'],node['end']
        if not (start<=a<=b<=end):raise ValueError('Interval outside window: '+str(original))
        if a==b:continue
        while len(stack)>1 and stack[-1]['end']<=a:stack.pop()
        parent=stack[-1]
        if b>parent['end']:raise ValueError('Crossing intervals: '+str(original)+' against '+str({k:v for k,v in parent.items() if k!='children'}))
        parent['children'].append(node);stack.append(node)
    result=[]
    def visit(node,path):
        path=path+[node['label']];at=node['start']
        for child in node['children']:
            if child['start']>at:result.append({'start':at,'end':child['start'],'label':node['label'],'path':path})
            visit(child,path);at=child['end']
        if at<node['end']:result.append({'start':at,'end':node['end'],'label':node['label'],'path':path})
    visit(root,[])
    if not result or result[0]['start']!=start or result[-1]['end']!=end:
        raise ValueError('Uncovered endpoints')
    for left,right in zip(result,result[1:]):
        if left['end']!=right['start']:raise ValueError('Gap/overlap in partition')
    if sum(s['end']-s['start'] for s in result)!=end-start:raise ValueError('Nonconserving partition')
    return result

def union_length(intervals):
    events=defaultdict(int)
    for a,b in intervals:
        if b<a:raise ValueError('Negative interval')
        if a==b:continue
        events[a]+=1;events[b]-=1
    active=peak=total=0;last=None
    for at,delta in sorted(events.items()):
        if last is not None and active:total+=at-last
        active+=delta;peak=max(peak,active);last=at
        if active<0:raise ValueError('Negative overlap count')
    if active:raise ValueError('Unclosed overlap count')
    return total,peak

def intersect_length(intervals,start,end):
    return union_length([(max(a,start),min(b,end)) for a,b in intervals if a<end and b>start])[0]
