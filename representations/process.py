"""Fresh-process measurements with bounded, task-owned process groups."""
import hashlib
import json
import os
from pathlib import Path
import signal
import platform
import re
import subprocess
import time

def sha256(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda:f.read(8*1024*1024),b""): h.update(chunk)
    return h.hexdigest()

def save(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    tmp=path.with_name(path.name+".tmp")
    tmp.write_text(json.dumps(data,indent=2,sort_keys=True)+"\n");tmp.replace(path)

def stop(process):
    import psutil
    try: descendants=psutil.Process(process.pid).children(recursive=True)
    except psutil.NoSuchProcess: descendants=[]
    if process.poll() is not None and not descendants:return
    try: os.killpg(process.pid,signal.SIGTERM)
    except ProcessLookupError: return
    try: process.wait(timeout=2)
    except subprocess.TimeoutExpired: pass
    # Keep the original process identities: a time wrapper may exit before an
    # uncooperative child. Do not signal a vanished/reused process-group ID.
    _,alive=psutil.wait_procs(descendants,timeout=2)
    for child in alive:
        try:
            if child.status()!=psutil.STATUS_ZOMBIE:child.kill()
        except psutil.NoSuchProcess:pass
    if process.poll() is None:process.kill()
    process.wait()

def run(command,cwd,log,memory_gib,timeout,env=None,disk_directory=None):
    import psutil
    command=[str(x) for x in command];log=Path(log);log.parent.mkdir(parents=True,exist_ok=True)
    start=time.monotonic(); peak=0; peak_cpu=0.; samples=0;status="ok";disk_peak=0;disk_next=0
    def workdir_bytes():
        total=0
        if disk_directory is None:return 0
        for base,dirs,files in os.walk(disk_directory,followlinks=False):
            for name in files:
                try:total+=(Path(base)/name).lstat().st_size
                except FileNotFoundError:pass
        return total
    info={"command":command,"cwd":str(cwd),"sample_interval_seconds":0.05,
          "rss_limit_bytes":int(memory_gib*1024**3),"timeout_seconds":timeout,
          "rss_note":"Sampled sum of process-tree RSS; shared pages may be counted repeatedly; NOT a hard cap or minimum RAM requirement.",
          "cache_policy":"Uncontrolled OS page cache; no cold-cache claim."}
    timed=Path('/usr/bin/time').is_file() and platform.system() in ('Linux','Darwin')
    launch=(['/usr/bin/time','-l' if platform.system()=='Darwin' else '-v'] if timed else [])+command
    info['monitored_command']=launch
    child_env=dict(os.environ if env is None else env);child_env['LC_ALL']='C'
    info['environment_subset']={key:child_env.get(key) for key in ('LC_ALL','OMP_NUM_THREADS','OMP_DYNAMIC','OMP_PROC_BIND','OMP_PLACES')}
    with log.open("w") as stream:
        process=subprocess.Popen(launch,cwd=cwd,env=child_env,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        tracked=psutil.Process(process.pid)
        try:
            while process.poll() is None:
                rss=0;cpu=0.
                try: members=[tracked]+tracked.children(recursive=True)
                except psutil.NoSuchProcess: members=[]
                for member in members:
                    try:
                        rss+=member.memory_info().rss
                        t=member.cpu_times();cpu+=t.user+t.system
                    except psutil.NoSuchProcess: pass
                peak=max(peak,rss);peak_cpu=max(peak_cpu,cpu);samples+=1
                if disk_directory is not None and time.monotonic()>=disk_next:
                    disk_peak=max(disk_peak,workdir_bytes());disk_next=time.monotonic()+1
                if rss>info["rss_limit_bytes"]: status="memory_budget_exceeded";stop(process);break
                if timeout is not None and time.monotonic()-start>timeout: status="timeout";stop(process);break
                time.sleep(.05)
            process.wait()
        except BaseException:
            status="monitor_or_interruption_failure";stop(process);raise
        finally:
            info.update(status=status if status!="ok" or process.returncode==0 else "command_failed",
                        returncode=process.returncode,wall_seconds=time.monotonic()-start,
                        sampled_peak_tree_rss_bytes=peak,sampled_tree_cpu_seconds_lower_bound=peak_cpu,samples=samples)
            if disk_directory is not None:
                info['sampled_peak_workdir_logical_bytes']=max(disk_peak,workdir_bytes())
                info['disk_scope']='Logical bytes under native build directory; sampled once per second plus completion; excludes shared input/dependencies outside it.'
            save(log.with_suffix(".process.json"),info)
    if timed:
        # Read a bounded tail; native builders can produce very large logs.
        with log.open('rb') as f:
            f.seek(max(0,log.stat().st_size-16384));tail=f.read().decode('utf-8','replace')
        pattern=r'^\s*(\d+)\s+maximum resident set size\s*$' if platform.system()=='Darwin' else r'^\s*Maximum resident set size \(kbytes\):\s*(\d+)\s*$'
        matches=re.findall(pattern,tail,re.MULTILINE)
        if matches:info['os_reported_maxrss_bytes']=int(matches[-1])*(1 if platform.system()=='Darwin' else 1024)
        info['os_maxrss_note']='Platform time/wait resource high-water mark; distinct from the sampled concurrent process-tree RSS sum.'
        save(log.with_suffix('.process.json'),info)
    if info["status"]!="ok": raise RuntimeError(f"{info['status']}: {command[0]}; see {log}")
    return info
