"""Recompute archived values without experimental execution."""
import argparse,csv,json,math,pathlib,statistics,collections,importlib.util
R=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument("--bootstrap",action="store_true")
a=p.parse_args()
rows=list(csv.DictReader((R/"data/formal_windows.csv").open(encoding="utf-8-sig")))
expected={"P1":84,"P2":90,"P3":36,"P4":9,"C1":16,"C2":24,"C7":9,"C8":36,"C9":18}
assert collections.Counter(r["group"] for r in rows)==expected
assert len(set((r["group"],r["profile"],r["policy"],r["run_id"]) for r in rows))==322
cells=collections.defaultdict(list)
for r in rows:
    cells[(r["group"],r["profile"],r["policy"],int(r["B"]),r["arm"])].append(float(r["complete_s"]))
summaries=[]
def quantile(values,p):
    ordered=sorted(values);i=(len(ordered)-1)*p;lo=math.floor(i);hi=math.ceil(i)
    return ordered[lo]+(ordered[hi]-ordered[lo])*(i-lo)
for key,values in sorted(cells.items()):
    assert all(math.isfinite(v) and v>0 for v in values)
    summaries.append({"group":key[0],"profile":key[1],"policy":key[2],"B":key[3],"arm":key[4],"n":len(values),"median_s":statistics.median(values),"q25_s":quantile(values,.25),"q75_s":quantile(values,.75),"iqr_s":quantile(values,.75)-quantile(values,.25),"min_s":min(values),"max_s":max(values)})
comparisons=[]
for group in ["C8","C9"]:
    original=json.loads((R/"data"/group/"analysis.json").read_text(encoding="utf-8"))
    raw=list(csv.DictReader((R/"data"/group/"window_values.csv").open(encoding="utf-8-sig")))
    core=None
    if a.bootstrap:
        spec=importlib.util.spec_from_file_location("core_"+group,R/"analysis"/("frozen_"+group)/"analysis_core.py")
        core=importlib.util.module_from_spec(spec);spec.loader.exec_module(core)
    recomputed=[]
    for c in original["comparisons"]:
        ref=sorted((r for r in raw if r["role"]=="performance" and int(r["B"])==c["B"] and r["arm_label"]==c["reference"]),key=lambda r:int(r["block"]))
        target=sorted((r for r in raw if r["role"]=="performance" and int(r["B"])==c["B"] and r["arm_label"]==c["target"]),key=lambda r:int(r["block"]))
        assert [x["block"] for x in ref]==[x["block"] for x in target]
        rv,tv=[float(x["complete_s"]) for x in ref],[float(x["complete_s"]) for x in target]
        rho=100*(1-statistics.median(tv)/statistics.median(rv))
        marked=statistics.mean(float(x["marked_s"])-float(y["marked_s"]) for x,y in zip(ref,target))
        rest=statistics.mean(float(x["rest_s"])-float(y["rest_s"]) for x,y in zip(ref,target))
        total=statistics.mean(x-y for x,y in zip(rv,tv))
        for x,y in [(rho,c["median_ratio_reduction_pct"]),(marked,c["mean_marked_seconds_saved"]),(rest,c["mean_rest_seconds_saved"]),(total,c["mean_paired_seconds_saved"]),(marked+rest,total)]:
            assert abs(x-y)<1e-7,(group,x,y)
        out={"group":group,"B":c["B"],"target":c["target"],"reference":c["reference"],"median_ratio_reduction_pct":rho,"marked_saved_s":marked,"rest_saved_s":rest,"total_saved_s":total}
        if core:
            q=core.describe_pair(rv,tv,c["bootstrap_samples"],c["bootstrap_seed"])
            for name in ["median_ratio_reduction_pct","mean_log_target_reference","geometric_mean_reduction_pct","sign_p_two_sided"]:
                assert abs(q[name]-c[name])<1e-10,(group,name)
            assert all(abs(x-y)<1e-10 for x,y in zip(q["geometric_reduction_ci95_pct"],c["geometric_reduction_ci95_pct"]))
            out["bootstrap_ci95_verified"]=True
            recomputed.append((c,q))
        comparisons.append(out)
    if core:
        secondary=[(c,q) for c,q in recomputed if c["family"]!="primary"]
        adj=core.holm_adjust([q["sign_p_two_sided"] for c,q in secondary])
        for value,(c,q) in zip(adj,secondary):assert abs(value-c["adjusted_p_holm"])<1e-12
outdir=R/"outputs";outdir.mkdir(exist_ok=True)
with (outdir/"arm_summaries.csv").open("w",encoding="utf-8",newline="") as f:
    w=csv.DictWriter(f,fieldnames=list(summaries[0]));w.writeheader();w.writerows(summaries)
result={"status":"PASS_ARCHIVED_NUMERICAL_REPRODUCTION","formal_windows":len(rows),"cell_summaries":len(summaries),"new_comparisons":comparisons,"bootstrap_holm_verified":a.bootstrap,"new_scientific_runs":0,"performance_requalification_performed":False}
(outdir/"reproduction_check.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
print(json.dumps({k:v for k,v in result.items() if k!="new_comparisons"},indent=2))
