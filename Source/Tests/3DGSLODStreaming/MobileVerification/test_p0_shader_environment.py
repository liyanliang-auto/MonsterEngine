"""Exercise the real PowerShell entry point and actual child process environment."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[4]

class ShaderInvocation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=Path(os.environ['MONSTER_P0_TEST_BUILD']).resolve()
        cls.shader=cls.build/'splat-shaders'
        cls.pwsh=shutil.which('pwsh')
        if not cls.pwsh: raise RuntimeError('PowerShell7 required')
        cls.temp=None
        if os.environ.get('MONSTER_P0_TEST_OUTPUT'):
            cls.folder=Path(tempfile.mkdtemp(prefix='shader-child-',dir=os.environ['MONSTER_P0_TEST_OUTPUT']))
        else:
            cls.temp=tempfile.TemporaryDirectory();cls.folder=Path(cls.temp.name)
        source=cls.folder/'child.cs';cls.child=cls.folder/'child.exe'
        source.write_text('using System; class Child {static int Main(string[] args) {'
            'Console.WriteLine(Environment.GetEnvironmentVariable("MONSTER_SPLAT_SHADER_DIR"));'
            'Console.WriteLine(String.Join("|",args));return 0;}}')
        run=subprocess.run([r'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe',
            '/nologo','/out:'+str(cls.child),str(source)],capture_output=True,text=True)
        if run.returncode:raise RuntimeError(run.stderr+run.stdout)

    @classmethod
    def tearDownClass(cls):
        if cls.temp:cls.temp.cleanup()

    def invoke(self,name,inherited=None,explicit=None):
        env=os.environ.copy();env.pop('MONSTER_SPLAT_SHADER_DIR',None)
        if inherited is not None:env['MONSTER_SPLAT_SHADER_DIR']=inherited
        out=self.folder/name
        cmd=[self.pwsh,'-NoProfile','-File',str(ROOT/'Tools/Android/run_p0_windows_checks.ps1'),
            '-BuildDirectory',str(self.build),'-OutputDirectory',str(out),'-CTestPath',str(self.child)]
        if explicit is not None:cmd+=['-ShaderDirectory',explicit]
        run=subprocess.run(cmd,env=env,capture_output=True,text=True)
        report=json.loads((out/'windows-checks.json').read_text())
        return run,report,out

    def assert_child(self,result,selection):
        run,report,out=result
        self.assertEqual(run.returncode,0,run.stderr)
        self.assertEqual(report['shaderSelection'],selection)
        self.assertTrue(report['environmentRestored'])
        self.assertTrue(report['command']['childStarted'])
        self.assertEqual(Path((out/'ctest.stdout.log').read_text().splitlines()[0]),self.shader)
        self.assertIn('--no-tests=error',(out/'ctest.stdout.log').read_text())
        self.assertEqual(Path(report['childEnvironment']['MONSTER_SPLAT_SHADER_DIR']),self.shader)

    def test_explicit_wins_over_invalid_inherited_and_restores(self):
        result=self.invoke('explicit','Z:/invalid',str(self.shader));self.assert_child(result,'explicit')
        self.assertEqual(result[1]['inheritedEnvironment']['MONSTER_SPLAT_SHADER_DIR'],'Z:/invalid')

    def test_inherited_selection(self):self.assert_child(self.invoke('inherited',str(self.shader)),'inherited')

    def test_absent_environment_resolves_matching_build(self):
        self.assert_child(self.invoke('cache'),'matching_cmake_cache')

    def test_invalid_explicit_and_inherited_fail_before_child(self):
        for name,kwargs in [('bad-explicit',dict(explicit='Z:/invalid')),
                            ('bad-inherited',dict(inherited='Z:/invalid'))]:
            run,report,_=self.invoke(name,**kwargs)
            self.assertNotEqual(run.returncode,0);self.assertIsNone(report['command'])
            self.assertTrue(report['environmentRestored'])

    def test_empty_inherited_environment_preserved_with_explicit_child_selection(self):
        result=self.invoke('empty-inherited','',str(self.shader));self.assert_child(result,'explicit')
        self.assertEqual(result[1]['inheritedEnvironment']['MONSTER_SPLAT_SHADER_DIR'],'')

    def test_existing_output_is_immutable(self):
        result=self.invoke('exclusive');self.assert_child(result,'matching_cmake_cache')
        before=(result[2]/'windows-checks.json').read_bytes()
        with self.assertRaises(FileExistsError):
            (result[2]/'windows-checks.json').open('xb')
        run,_,_=self.invoke('exclusive')
        self.assertNotEqual(run.returncode,0)
        self.assertEqual((result[2]/'windows-checks.json').read_bytes(),before)

if __name__=='__main__':unittest.main()
