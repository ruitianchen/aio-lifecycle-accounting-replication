"""Validate recorded API interval and exclusive-phase accounting with stdlib."""
import pathlib,csv,json,collections,math
R=pathlib.Path(__file__).resolve().parents[1]
def read(p):return list(csv.DictReader(p.open(encoding="utf-8-sig")))
def metrics(iv):
    events=collections.Counter()
    total=0
    for a,b in iv:
        assert b>=a
        total+=b-a
        if b>a:events[a]+=1;events[b]-=1
    active=peak=union=0
    previous=None
    for time in sorted(events):
        if previous is not None and active:union+=time-previous
        active+=events[time]
        assert active>=0
        peak=max(peak,active);previous=time
    assert active==0 and 0<=union<=total
    return len(iv),total,union,peak
result=[]
for group in ["C8","C9"]:
    base=R/"data"/group
    windows={r["run_id"]:r for r in read(base/"window_values.csv")}
    segments=collections.defaultdict(list)
    for r in read(base/"exclusive_segments.csv"):segments[r["run_id"]].append(r)
    recorded={r["run_id"]:r for r in json.loads((base/"phase_accounting_checks.json").read_text(encoding="utf-8"))}
    assert segments.keys()==windows.keys()==recorded.keys(),group
    assert len(windows)=={"C8":42,"C9":21}[group]
    for run_id,rows in segments.items():
        rows.sort(key=lambda r:int(r["start_raw_ns"]))
        previous=None;total=marked=0
        for r in rows:
            a,b=int(r["start_raw_ns"]),int(r["end_raw_ns"])
            assert b>=a and (previous is None or a==previous),(run_id,a,previous)
            total+=b-a
            if r["group"] in ("maintenance_teardown","query_teardown","final_cleanup"):marked+=b-a
            previous=b
        w=windows[run_id]
        assert total==recorded[run_id]["exclusive_partition_ns"]==recorded[run_id]["window_ns"]
        assert abs(marked/1e9-float(w["marked_s"]))<1e-7
        assert abs((total-marked)/1e9-float(w["rest_s"]))<1e-7
        assert int(w["creates"])==int(w["destroys"])
        assert int(w["peak_capacity"])==int(w["peak_contexts"])*1024
    apis=collections.defaultdict(list)
    for r in read(base/"api_intervals_qualification.csv"):
        apis[(r["run_id"],r["operation"])].append((int(r["start_ns"]),int(r["end_ns"])))
    for row in read(base/"api_summary_qualification.csv"):
        key=(row["run_id"],row["operation"])
        n,total,union,peak=metrics(apis[key])
        assert n==int(row["count"]) and peak==int(row["peak_overlap"])
        assert abs(total/1e9-float(row["cumulative_s"]))<1e-8
        assert abs(union/1e9-float(row["union_s"]))<1e-8
    result.append({"group":group,"windows":len(windows),"phase_segments":sum(map(len,segments.values())),"api_calls":sum(map(len,apis.values())),"status":"PASS_RECORDED_INTERVAL_PHASE_AND_COUNTER_ACCOUNTING"})
out=R/"outputs";out.mkdir(exist_ok=True)
report={"groups":result,"new_scientific_runs":0,"claim_scope":"integer time partition and archived counters, not physical causal attribution or nanosecond accuracy"}
(out/"accounting_check.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print(json.dumps(report,indent=2))
