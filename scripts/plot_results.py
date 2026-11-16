"""Plot archived values, with all formal blocks retained; no experiments run."""
import csv
import json
from pathlib import Path
import statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

R=Path(__file__).resolve().parents[1]
OUT=R/'outputs/figures'
OUT.mkdir(parents=True,exist_ok=True)
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,
                     'pdf.fonttype':42,'ps.fonttype':42})
COLORS=['#0072B2','#E69F00','#009E73']

def rows(group):
    with (R/'data'/group/'window_values.csv').open(encoding='utf-8-sig') as f:
        return [x for x in csv.DictReader(f) if x['role']=='performance']

def save(fig,stem):
    fig.savefig(OUT/(stem+'.pdf'),bbox_inches='tight')
    fig.savefig(OUT/(stem+'.png'),dpi=180,bbox_inches='tight')
    plt.close(fig)

fig,axes=plt.subplots(1,3,figsize=(11.8,3.7),layout='constrained')
source=[]
for ax,(group,B) in zip(axes,[('C8',200),('C8',2000),('C9',200)]):
    raw=[x for x in rows(group) if int(x['B'])==B]
    arms=['DIRECT_SERIAL','DIRECT_PARALLEL','A1_F'] if group=='C8' else ['DIRECT_PARALLEL','DIRECT_POOL','A1_F']
    for block in sorted({int(x['block']) for x in raw}):
        own={x['arm_label']:float(x['complete_s']) for x in raw if int(x['block'])==block}
        ax.plot(range(3),[own[a] for a in arms],color='#777777',alpha=.3,linewidth=.7,zorder=1)
    for i,arm in enumerate(arms):
        vals=[float(x['complete_s']) for x in raw if x['arm_label']==arm]
        jitter=[(j-(len(vals)-1)/2)*.018 for j in range(len(vals))]
        ax.scatter([i+j for j in jitter],vals,color=COLORS[i],s=24,zorder=2)
        ax.scatter([i],[statistics.median(vals)],marker='D',color='black',s=37,zorder=3)
    ax.set_xticks(range(3),['Serial','Dynamic','Pool'] if group=='C8' else ['Dynamic','Resident','Pool'])
    ax.set_title(f'{group}: B={B}, n=6 blocks')
    ax.set_ylim(bottom=0)
    ax.set_ylabel('Complete task time (s)')
    ax.grid(axis='y',alpha=.2)
    source.extend(dict(group=group,**x) for x in raw)
fig.suptitle('Complete windows: paired blocks and arm medians (black diamonds)')
save(fig,'complete_windows')

for group in ['C8','C9']:
    sizes=[200,2000] if group=='C8' else [200]
    fig,axes=plt.subplots(1,len(sizes),figsize=(6.1*len(sizes),3.7),layout='constrained',squeeze=False)
    for ax,B in zip(axes[0],sizes):
        raw=[x for x in rows(group) if int(x['B'])==B]
        refs={int(x['block']):x for x in raw if x['arm_label']=='DIRECT_PARALLEL'}
        targets={int(x['block']):x for x in raw if x['arm_label']=='A1_F'}
        blocks=sorted(refs)
        marked=[float(refs[b]['marked_s'])-float(targets[b]['marked_s']) for b in blocks]
        rest=[float(refs[b]['rest_s'])-float(targets[b]['rest_s']) for b in blocks]
        total=[float(refs[b]['complete_s'])-float(targets[b]['complete_s']) for b in blocks]
        assert all(abs(m+r-t)<1e-7 for m,r,t in zip(marked,rest,total))
        ax.bar([b-.19 for b in blocks],marked,width=.35,color='#009E73',label='Marked cleanup')
        ax.bar([b+.19 for b in blocks],rest,width=.35,color='#CC79A7',label='All other stages')
        ax.scatter(blocks,total,marker='D',color='black',s=30,label='Complete difference',zorder=3)
        ax.axhline(0,color='#444444',linewidth=.8)
        ax.set_xticks(blocks)
        ax.set_xlabel('Paired formal block')
        ax.set_ylabel('Dynamic minus pool (s)')
        ax.set_title(f'{group}, B={B}; all 6 formal blocks')
        ax.grid(axis='y',alpha=.15)
    handles,labels=axes[0][0].get_legend_handles_labels()
    fig.legend(handles,labels,loc='outside upper center',ncols=3,frameon=False)
    save(fig,group+'_stage_differences')

(OUT/'source_values.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
print('Created three audit figures (PDF and PNG) and exact plotted source records.')
