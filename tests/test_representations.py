"""Independent reference, input, resource guard and CLI contract tests."""
import itertools
import json
import importlib.util
from pathlib import Path
import random
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from representations import reference as ref
from representations import suite
from representations.suite import DEFAULT_OPS
from representations.process import run

class ReferenceTests(unittest.TestCase):
    def test_complete_small_graph(self):
        words={"".join(s) for s in itertools.product("ACGT",repeat=3)}
        m,out,inc=ref.metrics(words)
        self.assertEqual((m["oriented_kmers"],m["canonical_kmers"],m["arcs"]),(64,32,256))
        self.assertEqual(m["indegree_histogram"],[0,0,0,0,64])
        self.assertEqual(ref.component_metrics(out,inc),{"weak_components":1,"largest_weak_component_kmers":64})
        self.assertEqual(ref.strong_component_metrics(out,inc),{"strong_components":1,"largest_strong_component_kmers":64,"singleton_strong_components":0})
        self.assertEqual(ref.unitig_metrics(out,inc)["partitioned_arcs"],256)
        self.assertEqual(max(ref.distances(out,"AAA").values()),3)
        self.assertEqual(ref.walk_metrics(out,['AAA','XXX']),{'query_count':2,'hits':1,'neighbor_or_walk_steps':100})

    def test_nonbranching_cycle_and_chain(self):
        words={"AAA","AAT","ATG","TGC","GCA","CAT"}
        m,out,inc=ref.metrics(words)
        self.assertEqual(ref.unitig_metrics(out,inc)["partitioned_arcs"],m["arcs"])
        with tempfile.TemporaryDirectory() as d:
            trace=Path(d)/"trace.tsv"
            trace.write_text("B\tAAA\t0\nB\tAAT\t1\n")
            with self.assertRaises(AssertionError):ref.verify_traversal(trace,"bfs-stream",out,"AAA")

    def test_normalization_boundaries_and_sampling(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);path=d/"reads.fa"
            s="".join(random.Random(91).choices("ACGT",k=10000))
            records=[s,"aaa"*20+"NNN"+"ccc"*20,"AT"*15,"GGGG"]
            path.write_text("".join(f">{('ACGT'*3000) if i==1 else 'r'+str(i)}\n{x}\n" for i,x in enumerate(records)))
            prep,words=ref.prepare([path],d/"out.fa",d/"q.txt",31,17,9,True)
            expected=set()
            for seq in records:
                for i in range(len(seq)-30):
                    w=seq[i:i+31].upper()
                    if set(w)<=set("ACGT"):expected.update((w,ref.rc(w)))
            self.assertEqual(words,expected)
            self.assertLessEqual(len(prep["sampled_positive_kmers"]),17)
            first=(d/"q.txt").read_bytes()
            ref.prepare([path],d/"out2.fa",d/"q2.txt",31,17,9,False)
            self.assertEqual(first,(d/"q2.txt").read_bytes())

    def test_wrapped_fastq_and_truncation(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/"reads.fq"
            path.write_text("@one\nACG\nTAC\n+\n!!!\n!!!\n@two\nTTT\n+\n###\n")
            self.assertEqual(list(ref.sequences(path,3)),["ACGTAC","TTT"])
            path.write_text("@one\nACGT\n+\n!\n")
            with self.assertRaises(ValueError):list(ref.sequences(path,3))

    def test_empty_input_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);(d/"empty.fa").write_text(">empty\nNNN\n")
            with self.assertRaisesRegex(ValueError,"No valid"):
                ref.prepare([d/"empty.fa"],d/"out.fa",d/"q.txt",3,1,1)

    def test_fixture_has_isolated_vertices_and_cycles(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);path=d/'fixture.fa';ref.mini_fixture(path,11)
            _,words=ref.prepare([path],d/'normalized.fa',d/'queries.txt',11,8,1729,True)
            metrics,outgoing,incoming=ref.metrics(words)
            self.assertGreaterEqual(metrics['isolates'],2)
            self.assertGreater(metrics['branch_vertices'],0)
            self.assertGreater(metrics['indegree_histogram'][4],0)
            self.assertGreater(metrics['outdegree_histogram'][4],0)
            self.assertGreater(ref.unitig_metrics(outgoing,incoming)['isolated_directed_cycles'],0)

    def test_excluded_variants(self):
        self.assertNotIn("bfs-se",DEFAULT_OPS);self.assertNotIn("bfs-se32",DEFAULT_OPS)

    def test_source_pins_are_full_commits(self):
        pins=json.loads((Path(__file__).resolve().parents[1]/'representations/upstreams.json').read_text())
        for entry in pins.values():self.assertRegex(entry['revision'],r'^[0-9a-f]{40}$')

    def test_cli_rejects_two_frontier_methods(self):
        script=Path(__file__).resolve().parents[1]/'scripts/run_representations.py'
        for method in ('bfs-se','bfs-se32'):
            p=subprocess.run([sys.executable,str(script),'mini','--tools','missing.json','--operations',method],capture_output=True,text=True)
            self.assertEqual(p.returncode,2);self.assertIn('invalid choice',p.stderr)

    def test_setup_patch_is_idempotent_and_refuses_unknown_source(self):
        script=Path(__file__).resolve().parents[1]/'scripts/setup_representations.py'
        spec=importlib.util.spec_from_file_location('repr_setup',script);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'source.cpp';path.write_text('original\n');records=[]
            module.patch(path,'original','#pragma example\noriginal',records)
            module.patch(path,'original','#pragma example\noriginal',records)
            self.assertEqual(path.read_text(),'#pragma example\noriginal\n')
            with self.assertRaises(RuntimeError):module.patch(path,'unknown','replacement',records)

    def test_resume_refusal_preserves_original_fixture(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);out=root/'run';out.mkdir()
            fixture=out/'fixture.fa';fixture.write_text('original fixture bytes\n')
            (out/'manifest.json').write_text(json.dumps({'signature':{}}))
            tools=root/'tools.json';tools.write_text(json.dumps({'megahit':{'builder':sys.executable,'probe':sys.executable}}))
            with self.assertRaisesRegex(ValueError,'Resume refused'):
                suite.main(['mini','--tools',str(tools),'--out',str(out),'--backends','megahit','--seed','2','--resume'])
            self.assertEqual(fixture.read_text(),'original fixture bytes\n')

    def test_resume_only_accepts_completed_process(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'result.json'
            for value in ('{',json.dumps({'status':'ok'}),json.dumps({'status':'ok','process':{'status':'timeout','returncode':0}})):
                path.write_text(value);self.assertIsNone(suite.completed_result(path))
            value={'status':'ok','process':{'status':'ok','returncode':0}}
            path.write_text(json.dumps(value));self.assertEqual(suite.completed_result(path),value)

    def test_timeout_is_opt_in_and_preserved_in_manifest(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);reads=root/'reads.fa';reads.write_text('>one\n'+'A'*31+'\n')
            tools=root/'tools.json';tools.write_text(json.dumps({'megahit':{'builder':sys.executable,'probe':sys.executable}}))
            with patch.object(suite.reference,'prepare',side_effect=RuntimeError('stop after manifest')):
                with self.assertRaisesRegex(RuntimeError,'stop after manifest'):
                    suite.main(['mini','--tools',str(tools),'--input',str(reads),'--out',str(root/'results'),'--backends','megahit'])
            self.assertIsNone(json.loads((root/'results/manifest.json').read_text())['signature']['timeout'])

    def test_resume_relaxes_only_timeout(self):
        old={'timeout':3600,'k':31,'driver_sources':[]}
        for seconds in (None,3600,86400):
            self.assertEqual(suite.check_resume_signature(old,{**old,'timeout':seconds}),[])
        for changed in ({**old,'timeout':1},{**old,'k':21},{**old,'inputs':['different']}):
            with self.assertRaisesRegex(ValueError,'Resume refused'):suite.check_resume_signature(old,changed)
        with self.assertRaisesRegex(ValueError,'Resume refused'):
            suite.check_resume_signature({**old,'timeout':None},old)

    def test_only_fingerprinted_timeout_patch_can_migrate(self):
        root=Path(__file__).resolve().parents[1]
        rules=json.loads((root/'scripts/representation-resume-compatibility.json').read_text())['files']
        files=suite.inventory([root/name for name in rules])
        previous=[{**f,'sha256':rules[str(Path(f['path']).relative_to(root))]['before_sha256']} for f in files]
        old={'timeout':3600,'driver_sources':previous};new={'timeout':None,'driver_sources':files}
        self.assertEqual(set(suite.check_resume_signature(old,new)),set(rules))
        wrong=json.loads(json.dumps(new));wrong['driver_sources'][0]['sha256']='unknown source'
        with self.assertRaisesRegex(ValueError,'unrecognized code change'):suite.check_resume_signature(old,wrong)

    def test_retry_preserves_failed_files_and_manifest(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d);old={'signature':{'timeout':3600},'started_unix':123}
            original=json.dumps(old);(out/'manifest.json').write_text(original)
            failed=out/'build/bifrost';failed.mkdir(parents=True);(failed/'build-0.log').write_text('timed out')
            successful=out/'build/megahit';successful.mkdir();(successful/'build.json').write_text('completed')
            history=suite.record_resume(out,old,{'timeout':None},[])
            suite.preserve_retry(failed,out,history)
            self.assertEqual((history/'manifest.json').read_text(),original)
            self.assertEqual((history/'build/bifrost/build-0.log').read_text(),'timed out')
            self.assertEqual((successful/'build.json').read_text(),'completed')
            current=json.loads((out/'manifest.json').read_text())
            self.assertIsNone(current['signature']['timeout']);self.assertEqual(current['started_unix'],123)

    def test_recovery_refuses_an_active_experiment(self):
        from types import SimpleNamespace
        script=Path(__file__).resolve().parents[1]/'scripts/resume_representations.py'
        spec=importlib.util.spec_from_file_location('repr_recovery',script);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);out=root/'results'
            active=SimpleNamespace(pid=-1,info={'cmdline':['python','scripts/run_representations.py','run','--out','results'],'cwd':str(root)})
            with patch('psutil.process_iter',return_value=[active]):
                with self.assertRaisesRegex(ValueError,'still running'):module.require_idle(out)
                module.require_idle(root/'another-experiment')

class ProcessTests(unittest.TestCase):
    def test_memory_guard_stops_own_job(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d)
            code="import time; b=bytearray(64*1024*1024); b[::4096]=bytes([1])*len(b[::4096]); time.sleep(10)"
            with self.assertRaisesRegex(RuntimeError,"memory_budget_exceeded"):
                run([sys.executable,"-c",code],d,d/"memory.log",.03,None)

    def test_no_timeout_survives_elapsed_clock_over_one_hour(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d)
            with patch('representations.process.time.monotonic',side_effect=itertools.count(step=4000)):
                result=run([sys.executable,'-c','import time;time.sleep(.15)'],d,d/'unlimited.log',1,None)
            self.assertEqual(result['status'],'ok');self.assertIsNone(result['timeout_seconds'])
            self.assertGreater(result['wall_seconds'],3600)

    def test_failure_and_timeout_are_not_results(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d)
            with self.assertRaisesRegex(RuntimeError,"command_failed"):
                run([sys.executable,"-c","raise SystemExit(7)"],d,d/"failure.log",1,5)
            with self.assertRaisesRegex(RuntimeError,"timeout"):
                run([sys.executable,"-c","import time;time.sleep(10)"],d,d/"timeout.log",1,.2)

if __name__=="__main__":unittest.main()
