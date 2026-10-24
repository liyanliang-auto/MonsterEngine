"""Compile actual trusted producer transport; keep untrusted command guards separate."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[4]


class NativeTransport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.artifacts=Path(os.environ['MONSTER_P0_NATIVE_ARTIFACT_ROOT'])
        cls.output=Path(os.environ['MONSTER_P0_TEST_OUTPUT'])
        source_path=ROOT/'Source/Platform/Android/AndroidJNI.cpp'
        source=source_path.read_text(encoding='utf-8-sig')
        parser=source[source.index('void Need('):source.index('void Known(')]
        branch=source[source.index('    if(s->diagnosticsJSON.empty()){'):source.index('    payload["measurementSchemaVersion"]')]
        build=ROOT/'build/android-frame-optimization/p0-native-transport'
        build.mkdir(parents=True,exist_ok=True)
        probe=build/'parser-probe.cpp';cls.executable=build/'parser-probe.exe'
        probe.write_text('#include <string>\n#include <vector>\n#include <map>\n#include <charconv>\n'
            '#include <cmath>\n#include <limits>\n#include <stdexcept>\n#include <cstdint>\n'
            '#include <fstream>\n#include <iostream>\n#include <iterator>\n'
            'constexpr size_t OutputBytes=16u<<20;\n'+parser+
            '\nstruct Snapshot {std::string diagnosticsJSON,cameraInputReceiptsJSON;};\n'
            'int main(int argc,char** argv){try{std::ifstream f(argv[1],std::ios::binary);'
            'std::string input{std::istreambuf_iterator<char>(f),{}};'
            'if(argc>2){auto j=Parser(input,std::string(argv[2])=="wide"?OutputBytes:65536).parse();std::cout<<Dump(j);return 0;}'
            'Snapshot storage;auto* s=&storage;s->diagnosticsJSON=std::move(input);Json payload=Json::Obj();'+branch+
            'std::cout<<Dump(payload);return 0;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}')
        command=['cl','/nologo','/std:c++20','/EHsc',str(probe),'/Fe:'+str(cls.executable),
                 '/Fo:'+str(build/'parser-probe.obj')]
        vcvars=Path(shutil.which('cl')).parents[6]/'Auxiliary/Build/vcvars64.bat'
        batch=build/'compile-parser.cmd'
        batch.write_text('@echo off\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+
                         subprocess.list2cmdline(command)+'\nexit /b %errorlevel%\n')
        compiled=subprocess.run(['cmd','/d','/c',str(batch)],capture_output=True)
        (cls.output/'jni-parser-build.log').write_bytes(compiled.stdout+compiled.stderr)
        if compiled.returncode:raise RuntimeError('Actual AndroidJNI parser probe compile failed; see jni-parser-build.log')
        cls.source_identity=dict(path=str(source_path),bytes=source_path.stat().st_size,
            sha256=hashlib.sha256(source_path.read_bytes()).hexdigest())
        cls.node_limit=int(re.search(r'\+\+nodes<=(\d+)',parser).group(1))

    def test_actual_snapshot_admission_sizes_refusals_and_lease_lifetime(self):
        from native_transport_probe import compile_probe
        exe = compile_probe(ROOT, self.output)
        for mode in ['success', 'bootstrap', 'growth', 'overflow', 'caps', 'exception', 'jvm-null', 'jvm-exception', 'concurrency', 'receipt-race', 'teardown', 'lengths']:
            with self.subTest(mode=mode):
                run=subprocess.run([str(exe),mode],capture_output=True,text=True,timeout=30)
                (self.output/('transport-'+mode+'.log')).write_text(run.stdout+run.stderr)
                self.assertEqual(run.returncode,0,run.stderr)

    def consume(self,name):
        matches=list(self.artifacts.rglob(name+'.json'))
        self.assertEqual(len(matches),1,'Native export fixture must come from exactly one current real native test')
        path=matches[0];data=json.loads(path.read_text())
        def size(value,depth=0):
            children=value.values() if isinstance(value,dict) else value if isinstance(value,list) else []
            sizes=[size(v,depth+1) for v in children]
            return 1+sum(x[0] for x in sizes),max([depth]+[x[1] for x in sizes])
        nodes,depth=size(data)
        actual=subprocess.run([str(self.executable),str(path)],capture_output=True,text=True)
        report=dict(source=self.source_identity,input=dict(path=str(path),bytes=path.stat().st_size,
            sha256=hashlib.sha256(path.read_bytes()).hexdigest()),nodes=nodes,depth=depth,
            actualParserNodeLimit=self.node_limit,exitCode=actual.returncode,stdout=actual.stdout,stderr=actual.stderr)
        (self.output/(name+'-transport.json')).write_text(json.dumps(report,indent=2))
        self.assertEqual(actual.returncode,0,actual.stderr)
        decoded=json.loads(actual.stdout)['diagnostics']
        self.assertIs(decoded.pop('available'),True)
        self.assertEqual(decoded,data,'Every value and all retained history must survive production transport')

    def test_single_frame_native_export_crosses_actual_producer_transport(self):self.consume('p0-causes')
    def test_full_native_ring_crosses_actual_producer_transport(self):self.consume('p0-full-ring')

    def test_consumed_camera_native_row_crosses_actual_producer_transport(self):self.consume('camera-real-consumed')

    def test_untrusted_parser_keeps_complexity_depth_bytes_and_duplicate_guards(self):
        cases={'nodes':('['+','.join('0' for _ in range(65536))+']','JSON complexity limit'),
               'depth':('['*22+'0'+']'*22,'JSON complexity limit'),
               'duplicates':('{"frameId":1,"frameId":2}','Duplicate JSON key'),
               'bytes':(' '*65537,'JSON byte limit')}
        # The full real producer export intentionally exceeds both command byte
        # and node bounds. Test node complexity separately with default output cap.
        for name,(value,reason) in cases.items():
            with self.subTest(name=name):
                path=self.output/('untrusted-'+name+'.json');path.write_text(value)
                run=subprocess.run([str(self.executable),str(path),'wide' if name=='nodes' else 'command'],capture_output=True,text=True)
                self.assertNotEqual(run.returncode,0);self.assertIn(reason,run.stderr)

    def test_export_cap_names_omissions_and_json_csv_share_retained_origins(self):
        import csv
        paths=list(self.artifacts.rglob('p0-export-omissions.json'));self.assertEqual(len(paths),1)
        source=paths[0];data=json.loads(source.read_bytes());frames=data['frames']
        self.assertLessEqual(source.stat().st_size,8<<20);self.assertGreater(data['exportOmittedFrames'],0)
        self.assertEqual(len(frames)+data['exportOmittedFrames'],512);self.assertEqual(data['droppedFrames'],0)
        self.assertEqual(frames[-1]['frameId'],512)
        csv_rows=list(csv.DictReader(source.with_suffix('.csv').read_text().splitlines()))
        self.assertEqual(len(csv_rows),512)
        for frame in frames:
            row=next(r for r in csv_rows if int(r['frameId'])==frame['frameId'])
            for field in ['sceneGeneration','surfaceGeneration','configGeneration','submissionToken']:
                self.assertEqual(int(row[field]),frame[field])
            self.assertEqual(len(frame['diagnosticsP0']['detail']['events']),16)

    def test_native_producer_rejects_oversized_or_nonobject_export(self):
        for name,value in [('oversize','{'+(' '*(8<<20))+'}'),('array','[]')]:
            with self.subTest(name=name):
                path=self.output/('native-invalid-'+name+'.json');path.write_text(value)
                run=subprocess.run([str(self.executable),str(path)],capture_output=True,text=True)
                self.assertNotEqual(run.returncode,0);self.assertIn('Native diagnostics export limit',run.stderr)

if __name__=='__main__':unittest.main()
