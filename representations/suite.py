"""Build, validate and measure actual upstream representations."""
import argparse
from collections import defaultdict
import json
import os
from pathlib import Path
import platform
import random
import statistics
import sys
import time

from .process import run, save, sha256
from . import reference
from .reporting import render_report

BACKENDS=("megahit","metagraph","bifrost","bcalm","sbwt","sshash")
TRAVERSALS=("bfs-base","bfs-base32","bfs-stream","bfs-stream32","dfs-base","dfs-base32","dfs-packed","dfs-packed32","dfs-se")
DEFAULT_OPS=("load","stats","lookup","neighbors","walk","components","scc","unitigs")+TRAVERSALS

def resolve_config(path):
    data=json.loads(Path(path).read_text())
    for name in BACKENDS:
        if name not in data: continue
        for field in ("builder","probe"):
            if field not in data[name]: continue
            p=Path(data[name][field]).expanduser()
            if not p.is_absolute(): p=Path(path).resolve().parent/p
            data[name][field]=str(p.resolve())
    return data

def check_tools(tools,backends):
    errors=[]
    needed=set(backends)
    if "sshash" in needed: needed.add("bcalm")
    for name in sorted(needed):
        fields=("builder",) if name=="bcalm" else ("builder","probe")
        for field in fields:
            p=tools.get(name,{}).get(field)
            if not p or not Path(p).is_file() or not os.access(p,os.X_OK): errors.append(f"{name}.{field}: missing executable {p}")
    if errors: raise ValueError("Tool configuration incomplete:\n"+"\n".join(errors))

def inventory(paths):
    return [{"path":str(p),"bytes":p.stat().st_size,"sha256":sha256(p)} for p in sorted(paths) if p.is_file()]

def completed_result(path):
    try: result=json.loads(Path(path).read_text())
    except (OSError,ValueError):return None
    if result.get('status')=='ok' and result.get('process',{}).get('status')=='ok' and result['process'].get('returncode')==0:
        return result
    return None

def build(name,tools,input_file,k,directory,threads,memory_gib,timeout,bcalm_file=None):
    directory.mkdir(parents=True,exist_ok=True);tmp=directory/"tmp";tmp.mkdir(exist_ok=True)
    prefix=directory/"graph";builder=tools[name]["builder"]
    budget=max(1,int(memory_gib*.5));commands=[]
    if name=="megahit":
        lib=directory/"reads.lib";lib.write_text(f"normalized strand-closed sequences\nse {input_file}\n")
        commands=[[builder,"buildlib",lib,directory/"reads"],
                  [builder,"read2sdbg","--read_lib_file",directory/"reads","--output_prefix",prefix,
                   "--kmer_k",k-1,"--min_kmer_frequency",1,"--num_cpu_threads",threads,
                   "--host_mem",budget*1024**3,"--mem_flag",0]]
        artifact=str(prefix);pattern="graph.sdbg*"
    elif name=="metagraph":
        commands=[[builder,"build","--graph","succinct","--state","stat","--mode","basic","--min-count",1,
                   "-k",k,"-p",threads,"--mem-cap-gb",budget,"--disk-swap",tmp,"--index-ranges",min(6,k-1),
                   "-o",prefix,input_file]]
        artifact=str(prefix)+".dbg";pattern="graph.*"
    elif name=="bifrost":
        commands=[[builder,"build","-r",input_file,"-k",k,"-m",min(15,k-2),"-t",threads,"-o",prefix,"-b","-T",tmp]]
        artifact=str(prefix)+".bfg";pattern="graph.bf*"
    elif name=="bcalm":
        commands=[[builder,"-in",input_file,"-kmer-size",k,"-abundance-min",1,"-abundance-max",2147483647,
                   "-minimizer-size",min(10,k-1),"-nb-cores",threads,"-max-memory",budget*1024,
                   "-out",prefix,"-out-dir",directory,"-out-tmp",tmp]]
        artifact=str(prefix)+".unitigs.fa";pattern="graph.unitigs.fa"
    elif name=="sbwt":
        artifact=str(prefix)+".sbwt";pattern="graph.sbwt"
        commands=[[builder,"build","-i",input_file,"-o",artifact,"-k",k,"--variant","plain-matrix",
                   "-p",min(6,k-1),"-a",1,"-t",threads,"-m",max(2,budget),"-d",tmp]]
    elif name=="sshash":
        if not bcalm_file: raise ValueError("SSHash requires BCALM2's spectrum-preserving strings")
        artifact=str(prefix)+".sshash";pattern="graph.sshash"
        commands=[[builder,"build","-i",bcalm_file,"-o",artifact,"-k",k,"-m",min(11,k-1),"-t",threads,"-g",budget,"-d",tmp]]
    else: raise ValueError(name)
    stages=[]
    for i,command in enumerate(commands):
        stages.append(run(command,directory,directory/f"build-{i}.log",memory_gib,timeout,disk_directory=directory))
    files=inventory(directory.glob(pattern))
    if not files or (name!="megahit" and not Path(artifact).is_file()): raise RuntimeError(f"{name} did not produce expected graph files")
    result={"backend":name,"artifact":artifact,"files":files,"serialized_bytes":sum(f["bytes"] for f in files),
            "build_wall_seconds":sum(s["wall_seconds"] for s in stages),
            "sampled_peak_build_tree_rss_bytes":max(s["sampled_peak_tree_rss_bytes"] for s in stages),"stages":stages,
            "status":"ok","graph_policy":"unweighted; abundance >=1; no graph simplification"}
    os_peaks=[s['os_reported_maxrss_bytes'] for s in stages if 'os_reported_maxrss_bytes' in s]
    result['os_reported_build_maxrss_bytes']=max(os_peaks) if len(os_peaks)==len(stages) else None
    result['sampled_peak_workdir_logical_bytes']=max(s['sampled_peak_workdir_logical_bytes'] for s in stages)
    if name=="sshash": result["preprocessing_dependency"]="bcalm"
    if name=="bcalm":
        count=bases=0
        with open(artifact) as stream:
            for line in stream:
                if line.startswith(">"):count+=1
                else:bases+=len(line.strip())
        result["unitig_count"]=count;result["unitig_bases"]=bases
        result["unitig_kmer_occurrences"]=bases-count*(k-1)
        result["query_capability"]="not provided: native output is a unitig FASTA, not a persistent query index"
    save(directory/"build.json",result);return result

def probe(tools,name,artifact,k,op,directory,queries,memory_gib,timeout,trace=False,source="forest"):
    directory.mkdir(parents=True,exist_ok=True);result_file=directory/"result.json"
    trace_file=directory/"trace.tsv" if trace else "-"
    command=[tools[name]["probe"],artifact,k,op,result_file,queries,trace_file,source]
    env=dict(os.environ);env['OMP_NUM_THREADS']='1';env['OMP_DYNAMIC']='FALSE'
    process=run(command,directory,directory/"probe.log",memory_gib,timeout,env=env)
    result=json.loads(result_file.read_text());result["process"]=process
    if result.get("status")!="ok": raise RuntimeError(f"Probe reported failure: {name}/{op}")
    save(result_file,result);return result

def render(out,builds,runs,verification):
    signature=json.loads((out/'manifest.json').read_text())['signature']
    workload={key:signature[key] for key in ('mode','k','repeats','threads','queries','sources','traversal_scope','memory_gib')}
    groups=defaultdict(list)
    for r in runs:
        if r.get("status")=="ok": groups[(r["backend"],r["operation"],r.get("source","n/a"))].append(r)
    summary=[]
    for name in BACKENDS:
        b=builds.get(name)
        if not b: continue
        stats=groups.get((name,"stats","n/a"),[]);n=stats[0].get("oriented_kmers") if stats else None
        storage=b.get("serialized_bytes");dep=builds.get(b.get("preprocessing_dependency"),{})
        item={"backend":name,"status":b.get("status"),"oriented_kmers":n,"serialized_bytes":storage,
              "bits_per_oriented_kmer":8*storage/n if storage is not None and n else None,
              "bits_per_canonical_kmer":16*storage/n if storage is not None and n else None,
              "build_wall_seconds_including_dependency":b.get("build_wall_seconds",0)+dep.get("build_wall_seconds",0),
              "sampled_peak_build_tree_rss_bytes_including_dependency":max(b.get("sampled_peak_build_tree_rss_bytes",0),dep.get("sampled_peak_build_tree_rss_bytes",0)),"operations":{}}
        if b.get("status")!="ok":
            item["build_wall_seconds_including_dependency"]=None
            item["sampled_peak_build_tree_rss_bytes_including_dependency"]=None
        for (backend,op,source),values in groups.items():
            if backend!=name:continue
            aux=[v.get("auxiliary_peak_bytes",v.get("auxiliary_payload_bytes")) for v in values]
            item["operations"][op if source=="n/a" else op+"|"+source]={"repeats":len(values),"operation":op,"source":source,
                "operation_ms_median":statistics.median(v["operation_ms"] for v in values),
                "operation_ms_min":min(v["operation_ms"] for v in values),"operation_ms_max":max(v["operation_ms"] for v in values),
                "loaded_process_rss_kib_median":statistics.median(v["loaded_process_rss_kib"] for v in values),
                "peak_process_rss_kib_median":statistics.median(v["peak_process_rss_kib"] for v in values),
                "auxiliary_peak_bytes_median":statistics.median(aux) if all(a is not None for a in aux) else None,
                "navigation":values[0]["navigation"]}
        summary.append(item)
    save(out/"summary.json",{"schema":"sega-representations-v1","workload":workload,"backends":summary,"verification":verification})
    render_report(out,builds,runs,verification,signature)

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode",choices=("doctor","mini","run"))
    p.add_argument("--tools",type=Path,required=True,help="JSON generated by setup_representations.py")
    p.add_argument("--input",nargs="+",type=Path,help="FASTA/FASTQ, optionally .gz; all are treated as unweighted input")
    p.add_argument("--out",type=Path,default=Path("results/representations"))
    p.add_argument("--backends",nargs="+",choices=BACKENDS,default=list(BACKENDS))
    p.add_argument("--operations",nargs="+",choices=DEFAULT_OPS,default=list(DEFAULT_OPS))
    p.add_argument("--k",type=int,default=31);p.add_argument("--threads",type=int,default=2)
    p.add_argument("--repeats",type=int,default=3,help="Run each identical measurement this many times in fresh programs; build each graph only once")
    p.add_argument("--queries",type=int,default=256,help="Reservoir positions; also query each sampled k-mer's eight candidate neighbors")
    p.add_argument("--sources",type=int,default=3,help="Number of starting DNA fragments; the same starting points are used for every tool")
    p.add_argument("--traversal-scope",choices=("forest","sampled","both"),default="sampled",
                   help="sampled (default): search from shared starting points; forest: cover the graph using each tool's own restart order; both: record separately")
    p.add_argument("--memory-gib",type=float,default=2,help="Sampled subprocess-tree RSS stop threshold; not a hard cap")
    p.add_argument("--timeout",type=float,default=3600,help="Seconds per subprocess")
    p.add_argument("--seed",type=int,default=1729);p.add_argument("--resume",action="store_true")
    args=p.parse_args(argv)
    if args.k<11 or args.k>31 or args.k%2!=1:p.error("Common K must be odd and between 11 and 31")
    if min(args.threads,args.repeats,args.queries,args.sources)<=0 or args.memory_gib<=0 or args.timeout<=0:p.error("Counts, timeout and memory budget must be positive")
    tools=resolve_config(args.tools);check_tools(tools,args.backends)
    if args.mode=="doctor": print("All selected builders and native probes are present.");return 0
    try:import psutil
    except ImportError:p.error("psutil is missing: use the setup-generated launcher, or install psutil into this Python environment")
    out=args.out.resolve()
    if out.exists() and any(out.iterdir()) and not args.resume:p.error("Output directory is not empty; choose a new --out or use --resume")
    out.mkdir(parents=True,exist_ok=True)
    if args.mode=="mini" and not args.input:
        fixture=out/"fixture.fa"
        if not fixture.exists():reference.mini_fixture(fixture,args.k,args.seed)
        args.input=[fixture]
    if not args.input:p.error("run requires --input reads.fa [reads2.fq.gz ...]")
    inputs=[path.resolve() for path in args.input]
    for path in inputs:
        if not path.is_file():p.error(f"Input file does not exist: {path}")
    source_files=list(Path(__file__).parent.glob("*.py"))+list(Path(__file__).parent.glob("*.hpp"))+[Path(__file__).with_name("probe.cpp"),Path(__file__).with_name("CMakeLists.txt"),Path(__file__).with_name("upstreams.json")]
    source_files+=list((Path(__file__).parents[1]/"include/sdbg_traverse").glob("*.hpp"))
    source_files+=[Path(__file__).parents[1]/"bench/benchmark_driver.hpp",Path(__file__).parents[1]/"backends/megahit_oracle.hpp"]
    source_files+=[Path(__file__).parents[1]/'scripts'/s for s in ('run_representations.py','setup_representations.py')]
    signature={"k":args.k,"backends":args.backends,"operations":args.operations,"repeats":args.repeats,
               "threads":args.threads,"queries":args.queries,"sources":args.sources,"traversal_scope":args.traversal_scope,"seed":args.seed,
               "memory_gib":args.memory_gib,"timeout":args.timeout,"mode":args.mode,"inputs":inventory(inputs),"input_order":[str(p) for p in inputs],
               "tools":tools,"binaries":inventory({Path(t[f]) for t in tools.values() for f in ("builder","probe") if f in t}),
               "driver_sources":inventory(source_files)}
    manifest_path=out/"manifest.json"
    if args.resume and manifest_path.exists():
        old=json.loads(manifest_path.read_text())
        if old["signature"]!=signature:raise ValueError("Resume refused: inputs, binaries, code or settings differ from manifest")
    else:
        import psutil
        save(manifest_path,{"schema":"sega-representations-v1","signature":signature,"platform":platform.platform(),
             "machine":platform.machine(),"processor":platform.processor(),"logical_cpus":os.cpu_count(),
             "physical_memory_bytes":psutil.virtual_memory().total,"thread_environment":{k:os.environ.get(k) for k in ('OMP_NUM_THREADS','OMP_PROC_BIND','OMP_PLACES')},
             "python":sys.version,"started_unix":time.time(),"construction_repeats":1,
             "memory_scope":"native subprocess; Python coordinator/reference excluded; query buffer included in probe RSS"})
    normalized=out/"normalized.fa";queries=out/"queries.txt"
    print("Preparing shared k-mer input and query sample...",flush=True)
    if args.resume and args.mode=="run" and (out/"input.json").exists() and normalized.exists() and queries.exists():
        prep=json.loads((out/"input.json").read_text());words=None
        if sha256(normalized)!=prep['normalized_sha256'] or sha256(queries)!=prep['queries_sha256']:
            raise ValueError('Resume refused: normalized input or queries changed')
    else:
        started=time.monotonic();cpu_started=time.process_time()
        prep,words=reference.prepare(inputs,normalized,queries,args.k,args.queries,args.seed,args.mode=="mini")
        prep.update(normalization_wall_seconds=time.monotonic()-started,normalization_cpu_seconds=time.process_time()-cpu_started,
                    normalized_bytes=normalized.stat().st_size,queries_bytes=queries.stat().st_size,
                    normalization_scope='Python shared input normalization/query sampling; includes reference construction in mini; excludes file hashing and native construction')
        metadata={**prep,"normalized_sha256":sha256(normalized),"queries_sha256":sha256(queries)}
        if args.resume and (out/'input.json').exists():
            previous=json.loads((out/'input.json').read_text())
            if any(metadata[key]!=previous[key] for key in ('normalized_sha256','queries_sha256')):raise ValueError('Resume normalization differs from original')
            prep=previous
        else:save(out/"input.json",metadata)
    builds={};failures=[]
    order=[b for b in BACKENDS if b in args.backends or (b=="bcalm" and "sshash" in args.backends)]
    # Dependency first, other construction order reproducibly randomized.
    random.Random(args.seed).shuffle(order)
    if "bcalm" in order:order.remove("bcalm");order.insert(0,"bcalm")
    for name in order:
        directory=out/"build"/name;record=directory/"build.json"
        try:
            if args.resume and record.exists():
                result=json.loads(record.read_text())
                for f in result["files"]:
                    if not Path(f["path"]).is_file() or sha256(f["path"])!=f["sha256"]:raise ValueError("Stored graph artifact changed")
            else:
                print(f"Building {name}...",flush=True)
                result=build(name,tools,normalized,args.k,directory,args.threads,args.memory_gib,args.timeout,builds.get("bcalm",{}).get("artifact"))
            builds[name]=result
        except Exception as e:
            failures.append({"stage":"build","backend":name,"error":str(e)});builds[name]={"backend":name,"status":"failed","error":str(e)}
            print(f"FAILED {name}: {e}",flush=True)
        save(out/"builds.json",builds)
    verification={"status":"pending","scope":"exhaustive-independent-mini" if words is not None else "sampled-cross-backend-agreement","checks":[],"failures":failures}
    good=[b for b in args.backends if b!="bcalm" and builds.get(b,{}).get("status")=="ok"]
    expected=outgoing=incoming=None
    if words is not None:expected,outgoing,incoming=reference.metrics(words)
    sampled={}; slots={}; sources=prep["sampled_positive_kmers"][:args.sources]
    for name in good:
        artifact=builds[name]["artifact"]
        try:
            print(f"Validating {name}...",flush=True)
            root=out/"verification"/name
            if words is not None:
                r=probe(tools,name,artifact,args.k,"audit",root/"audit",queries,args.memory_gib,args.timeout,True)
                verification["checks"].append({"backend":name,"operation":"exact-topology",**reference.verify_trace(root/"audit"/"trace.tsv",words)})
                for key,value in expected.items():
                    if r[key]!=value:raise AssertionError(f"{key} mismatch: {r[key]} != {value}")
                for operation,expect in (("components",reference.component_metrics(outgoing,incoming)),("scc",reference.strong_component_metrics(outgoing,incoming)),("unitigs",reference.unitig_metrics(outgoing,incoming)),('walk',reference.walk_metrics(outgoing,queries.read_text().splitlines()))):
                    r=probe(tools,name,artifact,args.k,operation,root/operation,queries,args.memory_gib,args.timeout)
                    for key,value in expect.items():
                        if r[key]!=value:raise AssertionError(f"{operation}/{key}: {r[key]} != {value}")
                    verification["checks"].append({"backend":name,"operation":operation,"status":"pass"})
                for source_i,source in enumerate(sources+[None]):
                    dfs_traces=[]
                    for op in TRAVERSALS:
                        directory=root/f"source-{source_i}"/op
                        probe(tools,name,artifact,args.k,op,directory,queries,args.memory_gib,args.timeout,True,source if source is not None else "forest")
                        verification["checks"].append({"backend":name,"operation":op,"source":source,**reference.verify_traversal(directory/"trace.tsv",op,outgoing,source)})
                        if op.startswith("dfs"):dfs_traces.append((directory/"trace.tsv").read_bytes())
                    if len(set(dfs_traces))!=1:raise AssertionError("DFS implementations disagree on event order within backend")
            directory=root/"sampled-neighbors"
            sample_result=probe(tools,name,artifact,args.k,"neighbors",directory,queries,args.memory_gib,args.timeout,True)
            slots[name]=sample_result["identifier_slots"]
            data={}
            for line in (directory/"trace.tsv").read_text().splitlines():
                row=line.split("\t");data[row[0]]=(int(row[1]),tuple(sorted(row[2].split(","))) if len(row)>2 and row[2] else (),tuple(sorted(row[3].split(","))) if len(row)>3 and row[3] else ())
            if words is not None:
                for s,(hit,a,b) in data.items():
                    if bool(hit)!=(s in words) or (hit and (list(a)!=sorted(outgoing[s]) or list(b)!=sorted(incoming[s]))):raise AssertionError(f"Sample query mismatch at {s}")
            sampled[name]=data
            verification["checks"].append({"backend":name,"operation":"sampled-membership-and-neighbors","status":"pass","query_count":len(data)})
        except Exception as e:
            failures.append({"stage":"verification","backend":name,"error":str(e)});print(f"FAILED validation {name}: {e}",flush=True)
    if words is not None and builds.get("bcalm",{}).get("status")=="ok":
        try:
            unitig_words=[]
            for s in reference.sequences(builds["bcalm"]["artifact"],args.k,chunk_size=2**40):
                unitig_words.extend(min(s[i:i+args.k],reference.rc(s[i:i+args.k])) for i in range(len(s)-args.k+1))
            if set(unitig_words)!={min(s,reference.rc(s)) for s in words}:raise AssertionError("BCALM2 spectrum mismatch")
            if len(set(unitig_words))!=len(unitig_words):raise AssertionError("BCALM2 output repeats canonical k-mers; invalid SSHash input")
            verification["checks"].append({"backend":"bcalm","operation":"exact-spectrum-and-uniqueness","status":"pass"})
        except Exception as e:failures.append({"stage":"verification","backend":"bcalm","error":str(e)})
    if len(sampled)>1:
        first=next(iter(sampled.values()))
        if any(data!=first for data in sampled.values()):
            for name in sampled:failures.append({"stage":"verification","backend":name,"error":"Cross-backend sampled neighbor disagreement; no backend assumed correct"})
        verification["checks"].append({"operation":"cross-backend-sampled-agreement","backends":list(sampled),"status":"pass" if all(d==first for d in sampled.values()) else "fail"})
    elif words is None:
        failures.append({"stage":"verification","error":"At least two query backends are required for sampled cross-backend agreement"})
    verification["status"]="failed" if failures else "passed"
    save(out/"verification.json",verification)
    # A backend with a known semantic failure must not produce persuasive-looking
    # performance rows. Other verified backends can still finish their work.
    bad={f.get("backend") for f in failures if f["stage"] in ("build","verification")}
    timing_sources=(["forest"] if args.traversal_scope in ("forest","both") else [])+(sources if args.traversal_scope in ("sampled","both") else [])
    schedule=[(name,op,rep,source) for rep in range(args.repeats) for name in good if name not in bad
              for op in args.operations for source in (timing_sources if op in TRAVERSALS else ["n/a"])]
    random.Random(args.seed+1).shuffle(schedule);save(out/"schedule.json",schedule)
    runs=[]
    for name,op,rep,source in schedule:
        directory=out/"measurements"/name/op/source/f"repeat-{rep}"
        if op.endswith("32") and slots.get(name,0)>2**32-1:
            runs.append({"backend":name,"operation":op,"repeat":rep,"source":source,"status":"unsupported",
                         "reason":"Native identifier universe exceeds supported 32-bit ID range"});continue
        try:
            r=completed_result(directory/"result.json") if args.resume else None
            if r is None:
                print(f"Measuring {name}/{op} source={source} repeat {rep+1}/{args.repeats}",flush=True)
                r=probe(tools,name,builds[name]["artifact"],args.k,op,directory,queries,args.memory_gib,args.timeout,source=source if source!="n/a" else "forest")
            r["repeat"]=rep;runs.append(r)
        except Exception as e:
            r={"backend":name,"operation":op,"repeat":rep,"source":source,"status":"failed","error":str(e)};runs.append(r);failures.append({"stage":"measurement",**r})
        save(out/"runs.json",runs)
    save(out/"runs.json",runs)
    for op,keys in (("stats",("oriented_kmers","arcs","indegree_histogram","outdegree_histogram")),
                    ("components",("weak_components","largest_weak_component_kmers")),
                    ("scc",("strong_components","largest_strong_component_kmers","singleton_strong_components")),
                    ("unitigs",("oriented_maximal_nonbranching_paths","isolated_directed_cycles","partitioned_arcs")),
                    ('lookup',('query_count','hits')),('neighbors',('query_count','hits','neighbor_or_walk_steps')),('walk',('query_count','hits','neighbor_or_walk_steps'))):
        values=[{key:r[key] for key in keys} for r in runs if r.get("status")=="ok" and r["operation"]==op]
        if values:
            same=all(v==values[0] for v in values)
            verification["checks"].append({"operation":"cross-run-aggregate-"+op,"status":"pass" if same else "fail","rows":len(values)})
            if not same:failures.append({"stage":"verification","error":"Aggregate disagreement: "+op})
    counts={r["backend"]:r["oriented_kmers"] for r in runs if r.get("status")=="ok" and r["operation"]=="stats"}
    for r in runs:
        if r.get("status")!="ok" or r["operation"] not in TRAVERSALS:continue
        if r.get("source")=="forest" and r["backend"] in counts and r["visited"]!=counts[r["backend"]]:
            failures.append({"stage":"verification","error":"Forest coverage mismatch","backend":r["backend"],"operation":r["operation"]})
        if r["operation"].startswith("dfs") and r["visited"]!=r["finished"]:
            failures.append({"stage":"verification","error":"DFS discover/finish count mismatch","backend":r["backend"],"operation":r["operation"]})
    verification["status"]="failed" if failures else "passed";save(out/"verification.json",verification)
    render(out,builds,runs,verification)
    print(f"{verification['status'].upper()}: {out/'report.html'}",flush=True)
    return 1 if failures else 0
