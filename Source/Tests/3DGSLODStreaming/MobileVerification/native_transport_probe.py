"""Compile production JNI transport helpers and branch against the real account.
Only Android/JVM/host IO boundaries are doubles; never claims GPU acceptance.
"""
from pathlib import Path
import shutil
import subprocess

def compile_probe(root, output):
    source=(root/'Source/Platform/Android/AndroidJNI.cpp').read_text(encoding='utf-8-sig')
    parser=source[source.index('void Need('):source.index('void Known(')]
    result=source[source.index('Json Result(bool'):source.index('Json Status(')]
    jstring=source[source.index('jstring JString('):source.index('template<class F> void VoidBoundary')]
    helpers=source[source.index('std::mutex DiagnosticReaderMutex;'):source.index('jstring Snapshot(')]
    branch=source[source.index('            reader.Enter();'):source.index('        if(kind=="screenshot")')].rsplit('        }',1)[0]
    template=(Path(__file__).with_name('native_transport_probe.cpp.in')).read_text()
    code=template.replace('@PARSER@',parser).replace('@RESULT@',result).replace('@JSTRING@',jstring).replace('@HELPERS@',helpers).replace('@BRANCH@',branch)
    build=root/'build/android-frame-optimization/p0-native-transport';build.mkdir(parents=True,exist_ok=True)
    probe=build/'production-transport-probe.cpp';probe.write_text(code)
    exe=build/'production-transport-probe.exe'
    cl=Path(shutil.which('cl'));vcvars=cl.parents[6]/'Auxiliary/Build/vcvars64.bat'
    batch=build/'compile-production-transport.cmd'
    batch.write_text('@echo off\ncall "'+str(vcvars)+'" >nul\n'+subprocess.list2cmdline([str(cl),'/nologo','/std:c++20','/EHsc','/I'+str(root/'Include'),str(probe),str(root/'Source/Renderer/Splat/SplatBudgetCoordinator.cpp'),'/Fe:'+str(exe),'/Fo:'+str(build)+'/'])+'\nexit /b %errorlevel%\n')
    run=subprocess.run(['cmd','/d','/c',str(batch)],capture_output=True)
    (output/'production-transport-build.log').write_bytes(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError('Production transport probe compile failed; see production-transport-build.log')
    return exe
