import argparse,json
from pathlib import Path
from oracle_dim import check
p=argparse.ArgumentParser()
for n in ('prefix','fixture','result-prefix','out'):p.add_argument('--'+n,type=Path,required=True)
p.add_argument('--stage',type=int,required=True);a=p.parse_args()
assert not a.out.exists(),'No validation output overwrite'
r=check(a.prefix,a.fixture,a.stage,a.result_prefix)
a.out.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r))
