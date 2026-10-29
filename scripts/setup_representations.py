#!/usr/bin/env python3
"""Install isolated dependencies and build pinned native graph probes.

Requires Git, a C++20 compiler and make. No system-wide installation or sudo.
This command downloads and compiles third-party software; subsequent benchmark
runs are offline. An environment lock, patches and complete build logs are kept.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from representations.process import save,sha256

def command(args,log,env=None,cwd=None):
    print(" ".join(map(str,args)),flush=True)
    with Path(log).open("a") as out:
        result=subprocess.run(list(map(str,args)),cwd=cwd,env=env,stdout=out,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError(f"Command failed ({result.returncode}); see {log}")

def patch(path,old,new,records):
    s=path.read_text();before=sha256(path)
    if new in s:pass
    elif old in s:path.write_text(s.replace(old,new))
    else:raise RuntimeError(f"Expected upstream text absent; refusing unknown patch: {path}")
    records.append({"path":str(path),"old":old,"new":new,"before_sha256":before,"after_sha256":sha256(path)})

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--prefix",type=Path,default=ROOT/"representation-tools")
    p.add_argument("--jobs",type=int,default=2)
    p.add_argument("--micromamba",type=Path)
    p.add_argument("--skip-deps",action="store_true",help="Use the existing PREFIX/env without updating it")
    p.add_argument("--megahit-builder",type=Path,help="Reuse a previously built megahit_core_no_hw_accel")
    p.add_argument("--llvm-prefix",type=Path,help="Modern LLVM with libc++ on macOS (MetaGraph needs std::atomic_ref)")
    p.add_argument("--openmp-prefix",type=Path,help="Reuse a macOS libomp prefix; default is the isolated environment")
    args=p.parse_args()
    if args.jobs<1:p.error("--jobs must be positive")
    prefix=args.prefix.resolve();logs=prefix/"logs";logs.mkdir(parents=True,exist_ok=True)
    sources=prefix/"upstream";sources.mkdir(exist_ok=True)
    env_prefix=prefix/"env";pins=json.loads((ROOT/"representations/upstreams.json").read_text())
    if not shutil.which("git"):p.error("Git must be installed")
    mamba=str(args.micromamba.resolve()) if args.micromamba else shutil.which("micromamba")
    if not args.skip_deps:
        if not mamba:
            system=platform.system();machine=platform.machine()
            triplet={('Darwin','x86_64'):'osx-64',('Darwin','arm64'):'osx-arm64',('Linux','x86_64'):'linux-64',('Linux','aarch64'):'linux-aarch64'}.get((system,machine))
            if not triplet:p.error(f"Unsupported platform {system}/{machine}")
            archive=prefix/"micromamba.tar.bz2"
            url=f"https://micro.mamba.pm/api/micromamba/{triplet}/2.10.0"
            urllib.request.urlretrieve(url,archive)
            target=prefix/"bin/micromamba";target.parent.mkdir(exist_ok=True)
            with tarfile.open(archive) as tar:
                with target.open("wb") as out:shutil.copyfileobj(tar.extractfile("bin/micromamba"),out)
            target.chmod(0o755);mamba=str(target)
            save(prefix/"micromamba-download.json",{"url":url,"sha256":sha256(archive)})
        action="install" if (env_prefix/"conda-meta").exists() else "create"
        command([mamba,action,"-y","-r",prefix/"mamba-cache","-p",env_prefix,"-c","conda-forge","-c","bioconda",
                 "metagraph=0.5.2","libboost-devel=1.85.0","openssl","zstd","automake","autoconf","pkg-config",
                 "cmake>=3.25,<4","python=3.11","psutil>=5.9,<8","llvm-openmp" if platform.system()=="Darwin" else "libgomp"],logs/"environment.log")
        with (prefix/"environment-explicit.txt").open("w") as out:
            subprocess.run([mamba,"list","-p",str(env_prefix),"--explicit"],stdout=out,check=True)
    environment=os.environ.copy();environment["PATH"]=str(env_prefix/"bin")+os.pathsep+environment["PATH"]
    environment["LC_ALL"]="C";environment["LANG"]="C"
    cmake=shutil.which("cmake",path=environment["PATH"])
    if not cmake:p.error("CMake is missing; run without --skip-deps")
    if mamba and args.skip_deps:
        with (prefix/"environment-explicit.txt").open("w") as out:
            subprocess.run([mamba,"list","-p",str(env_prefix),"--explicit"],stdout=out,check=True)
    omp_prefix=args.openmp_prefix.resolve() if args.openmp_prefix else env_prefix
    config={};patches=[]
    for name,pin in pins.items():
        src=sources/name
        if not src.exists():
            src.mkdir()
            command(["git","init",src],logs/f"clone-{name}.log")
            command(["git","remote","add","origin",pin["url"]],logs/f"clone-{name}.log",cwd=src)
            command(["git","fetch","--depth","1","origin",pin["revision"]],logs/f"clone-{name}.log",cwd=src)
            command(["git","checkout","--detach","FETCH_HEAD"],logs/f"clone-{name}.log",cwd=src)
        revision=subprocess.check_output(["git","rev-parse","HEAD"],cwd=src,text=True).strip()
        if revision!=pin["revision"]:raise RuntimeError(f"Source revision mismatch in {src}; existing checkout preserved")
        command(["git","submodule","update","--init","--recursive","--depth","1"],logs/f"clone-{name}.log",cwd=src)
        if name=="metagraph":
            patch(src/"metagraph/CMakeLists.txt","set(Boost_USE_STATIC_LIBS ON)","set(Boost_USE_STATIC_LIBS OFF)",patches)
            patch(src/"metagraph/CMakeListsHelpers.txt",'if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")',
                  'if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")',patches)
            patch(src/"metagraph/src/common/bloom_filter.cpp",'#include "bloom_filter.hpp"',
                  '#if defined(__clang__)\n#pragma clang diagnostic warning "-Wpsabi"\n#endif\n#include "bloom_filter.hpp"',patches)
        save(prefix/"patches.json",patches)
        build=prefix/"build"/name
        flags=["-DCMAKE_BUILD_TYPE=Release","-DCMAKE_EXPORT_COMPILE_COMMANDS=ON","-DCMAKE_POLICY_VERSION_MINIMUM=3.5","-DCMAKE_PREFIX_PATH="+str(env_prefix)]
        if platform.system()=="Darwin":
            flags += ["-DCMAKE_C_COMPILER=/usr/bin/clang","-DCMAKE_CXX_COMPILER=/usr/bin/clang++",
                      "-DOpenMP_ROOT="+str(omp_prefix),"-DICU_ROOT="+str(env_prefix),
                      "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,"+str(omp_prefix/"lib")]
            if name=="metagraph":
                llvm=args.llvm_prefix or next((d for d in (Path('/usr/local/opt/llvm'),Path('/opt/homebrew/opt/llvm')) if (d/'include/c++/v1/__atomic/atomic_ref.h').is_file()),None)
                if llvm is None:raise RuntimeError("MetaGraph requires modern LLVM/libc++ on this Mac. Install LLVM, then pass --llvm-prefix /path/to/llvm; existing work is retained.")
                flags=[x for x in flags if not x.startswith(('-DCMAKE_C_COMPILER=','-DCMAKE_CXX_COMPILER=','-DCMAKE_EXE_LINKER_FLAGS='))]
                flags += ['-DCMAKE_C_COMPILER='+str(llvm/'bin/clang'),'-DCMAKE_CXX_COMPILER='+str(llvm/'bin/clang++'),
                          '-DCMAKE_CXX_FLAGS=-nostdinc++ -isystem '+str(llvm/'include/c++/v1'),
                          '-DCMAKE_EXE_LINKER_FLAGS=-L'+str(llvm/'lib/c++')+' -Wl,-rpath,'+str(llvm/'lib/c++')+' -Wl,-rpath,'+str(omp_prefix/'lib')]
        command([cmake,"-S",ROOT/"representations","-B",build,"-DBACKEND="+name,"-DUPSTREAM="+str(src),*flags],logs/f"{name}-configure.log",environment)
        targets=["repr_probe"]+{"bifrost":["Bifrost"]}.get(name,[])
        command([cmake,"--build",build,"--target",*targets,"-j",args.jobs],logs/f"{name}-build.log",environment)
        builders={"metagraph":env_prefix/"bin/metagraph","bifrost":build/"upstream/src/Bifrost"}
        if name=="megahit":
            if args.megahit_builder:builder=args.megahit_builder.resolve()
            else:
                builder_dir=prefix/"build/megahit-builder"
                omp=[]
                if platform.system()=="Darwin":
                    omp=["-DOpenMP_C_FLAGS=-Xpreprocessor -fopenmp -I"+str(omp_prefix/"include"),
                         "-DOpenMP_CXX_FLAGS=-Xpreprocessor -fopenmp -I"+str(omp_prefix/"include"),
                         "-DOpenMP_C_LIB_NAMES=omp","-DOpenMP_CXX_LIB_NAMES=omp",
                         "-DOpenMP_omp_LIBRARY="+str(omp_prefix/"lib/libomp.dylib"),
                         "-DOpenMP_EXE_LINKER_FLAGS=-L"+str(omp_prefix/'lib')+' -lomp']
                command([cmake,"-S",src,"-B",builder_dir,"-DCMAKE_BUILD_TYPE=Release",*flags,*omp],logs/"megahit-builder-configure.log",environment)
                command([cmake,"--build",builder_dir,"--target","megahit_core_no_hw_accel","-j",args.jobs],logs/"megahit-builder-build.log",environment)
                builder=builder_dir/"megahit_core_no_hw_accel"
        else:builder=builders[name]
        config[name]={"builder":str(builder),"probe":str(build/"repr_probe"),"source_revision":revision,
                      "source_url":pin["url"],"source_directory":str(src),"cmake_cache_sha256":sha256(build/"CMakeCache.txt")}
        commands=json.loads((build/'compile_commands.json').read_text())
        config[name]['probe_compile_command']=next(c for c in commands if Path(c['file']).resolve()==ROOT/'representations/probe.cpp')
        cache=(build/'CMakeCache.txt').read_text().splitlines()
        compiler=next(s.split('=',1)[1] for s in cache if s.startswith('CMAKE_CXX_COMPILER:'))
        config[name]['compiler_version']=subprocess.check_output([compiler,'--version'],text=True,env=environment)
        config[name]['source_revision_scope']='probe-and-builder'
        if name=='metagraph':
            config[name]['source_revision_scope']='probe-only'
            config[name]['builder_origin']='bioconda metagraph 0.5.2; exact package and dependencies in environment-explicit.txt'
        elif name=='megahit' and args.megahit_builder:
            config[name]['source_revision_scope']='probe-only'
            config[name]['builder_origin']='supplied executable; fingerprinted, source revision not attested by setup'
        config[name]["submodules"]=subprocess.check_output(["git","submodule","status","--recursive"],cwd=src,text=True)
        save(prefix/"tools.json",config)
    for entry in config.values():
        for key in ("builder","probe"):
            if key in entry:
                if not Path(entry[key]).is_file():raise RuntimeError(f"Missing executable: {entry[key]}")
                entry[key+"_sha256"]=sha256(entry[key])
        entry["patch_manifest_sha256"]=sha256(prefix/"patches.json")
    config['_provenance']={'compatibility_patches':patches,'cmake_version':subprocess.check_output([cmake,'--version'],text=True)}
    if (prefix/'environment-explicit.txt').exists():
        config['_provenance']['environment_explicit']=(prefix/'environment-explicit.txt').read_text()
    save(prefix/"tools.json",config)
    python=env_prefix/"bin/python"
    if not python.exists():python=Path(sys.executable)
    launcher=prefix/"run"
    launcher.write_text("#!/usr/bin/env python3\nimport os,sys\nos.execv("+repr(str(python))+",["+repr(str(python))+","+repr(str(ROOT/"scripts/run_representations.py"))+",*sys.argv[1:],'--tools',"+repr(str(prefix/"tools.json"))+"])\n")
    launcher.chmod(0o755)
    command([python,ROOT/"scripts/run_representations.py","doctor","--tools",prefix/"tools.json"],logs/"doctor.log",environment)
    print(f"Setup complete. Test with: {launcher} mini --out results/mini",flush=True)

if __name__=="__main__":
    try:main()
    except (OSError,RuntimeError,subprocess.CalledProcessError) as e:
        print(f"Setup failed: {e}",file=sys.stderr);raise SystemExit(2)
