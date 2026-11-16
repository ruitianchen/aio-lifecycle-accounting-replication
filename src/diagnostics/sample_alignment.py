"""Conservative interval containment for samples; no interpolation of gaps."""
from collections import defaultdict

def align_samples(samples, calls):
    by_worker=defaultdict(list)
    for c in calls:
        by_worker[(c['pid'],c['tid'],c['operation'])].append(c)
    result=[]
    for index,s in enumerate(samples):
        record={'sample_index':index,'alignment':'BOUNDARY_OR_UNMATCHED','has_destroy_frame':False}
        if s['errors']:
            record['alignment']='READ_UNAVAILABLE'
        else:
            matches=[c for c in by_worker[(s['pid'],s['tid'],s['operation'])]
                     if c['start_raw']<=s['read_begin_raw']<=s['read_end_raw']<=c['end_raw']]
            if len(matches)>1:
                raise ValueError('Overlapping API ownership intervals')
            if matches:
                c=matches[0]
                record.update(alignment='MATCHED',round=c['round'],warmup=c['warmup'],
                    group_size=c['group_size'],slot=c['slot'],operation=c['operation'],
                    has_destroy_frame=c['operation']=='io_destroy' and 'io_destroy' in s['stack'])
        result.append(record)
    return result
