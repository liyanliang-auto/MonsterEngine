"""Actual preflight CLI JSON/exit tests; paths selected via local environment."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

BUDGETS = ['media-copies', 'prepared-images', 'reconstruction-database', 'checkpoints', 'temporary-exports', 'lod', 'new-assets', 'retained-overlap', 'reserve']

class PreflightCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cli = Path(os.environ['MONSTER_PREFLIGHT_CLI']).resolve(strict=True)
        cls.native = Path(os.environ['MONSTER_PREFLIGHT_FIXTURE']).resolve(strict=True)
        cls.evidence = Path(os.environ['MONSTER_PREFLIGHT_EVIDENCE'])
        cls.evidence.mkdir(parents=True, exist_ok=True)
        cls.records = []
        cls.identities = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [cls.cli, cls.native]}

    @classmethod
    def tearDownClass(cls):
        (cls.evidence / 'cli-observations.json').write_text(json.dumps({'binarySha256': cls.identities, 'invocations': cls.records}, indent=2, ensure_ascii=False), encoding='utf-8')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='MonsterPreflight-cli-')
        self.root = Path(self.temp.name)

    def tearDown(self):
        shutil.rmtree('\\\\?\\'+str(self.root))
        self.temp.cleanup()

    def invoke(self, args, exit_code, code):
        p = subprocess.run([str(self.cli), *map(str, args)], capture_output=True, timeout=30)
        result = json.loads(p.stdout.decode('utf-8', errors='strict'))
        self.records.append({'arguments': list(map(str, args)), 'exitCode': p.returncode, 'stdout': result, 'stderr': p.stderr.decode('utf-8', errors='strict')})
        self.assertEqual(p.returncode, exit_code)
        self.assertEqual(result['code'], code)
        self.assertEqual(result['schemaVersion'], 1)
        self.assertTrue(result['message'])
        self.assertEqual(p.stderr, b'')
        return result

    def budget(self, directory=None):
        return ['space', '--directory', str(directory or self.root)] + [x for name in BUDGETS for x in ['--'+name, '0']]

    def fixture(self, directory):
        subprocess.run([str(self.native), '--fixture', str(directory)], check=True, capture_output=True, timeout=30)

    def inventory(self, directory):
        return {str(p.relative_to(directory)): ('directory' if p.is_dir() else hashlib.sha256(p.read_bytes()).hexdigest()) for p in directory.rglob('*')}

    def test_help_and_invalid_commands(self):
        self.invoke(['--help'], 0, 'Ok')
        for args in [[], ['invalid'], ['--help', 'extra'], ['write'], ['write', '--directory'], ['write', '--directory', '']]:
            self.invoke(args, 2, 'InvalidArgument')

    def test_missing_duplicate_unknown_options(self):
        for i in range(len(BUDGETS)):
            args = self.budget(); del args[3+2*i:5+2*i]
            self.invoke(args, 2, 'InvalidArgument')
        for tail in [['--reserve','0'], ['--unknown','0'], ['--directory',str(self.root)]]:
            self.invoke(self.budget()+tail, 2, 'InvalidArgument')
        self.invoke(['write', '--directory', self.root, '--reserve', '0'], 2, 'InvalidArgument')

    def test_invalid_uint64_and_addition_overflow(self):
        for value in ['-1', '+1', '1.0', '1x', '', '18446744073709551616', '\uff11']:
            args = self.budget(); args[-1] = value
            self.invoke(args, 2, 'InvalidArgument')
        args = self.budget(); args[4] = '18446744073709551615'; args[-1] = '1'
        r = self.invoke(args, 3, 'ArithmeticOverflow')
        self.assertIsNone(r['sample'])
        args[-1] = '0'
        self.invoke(args, 3, 'InsufficientSpace')

    def test_actual_capacity_json(self):
        result = self.invoke(self.budget(), 0, 'Ok')
        self.assertEqual(result['requiredBytes'], 0)
        self.assertEqual(set(result['sample']), {'availableToCaller','volumeFree','volumeTotal'})
        self.assertGreater(result['sample']['volumeTotal'], 0)
        self.assertFalse(result['capacityReserved'])

    def test_unicode_write_and_failed_service_exits(self):
        directory = self.root / '\u9879\u76ee space'; directory.mkdir()
        result = self.invoke(['write', '--directory', directory], 0, 'Ok')
        self.assertEqual(result['directory'], str(directory))
        self.assertEqual(result['bytesWritten'], 4096)
        for field in ['created','flushed','closed','removed']: self.assertTrue(result[field])
        self.assertEqual(list(directory.iterdir()), [])
        self.invoke(['write','--directory',directory/'missing'], 3, 'MissingDestination')
        self.invoke(['write','--directory','relative'], 3, 'UnsupportedPath')
        file = directory/'file'; file.write_bytes(b'keep')
        self.invoke(['write','--directory',file], 3, 'NotDirectory')
        self.assertEqual(file.read_bytes(), b'keep')

    def test_compatibility_hash_inventory_preservation(self):
        for mode in ['supported', 'unknown_project', 'corrupt_project', 'unknown_input', 'corrupt_input', 'identity']:
            directory = self.root / mode; self.fixture(directory)
            target = directory / ('project.mstudio' if mode.endswith('project') else 'input.mstudio')
            if mode.startswith('unknown'):
                data = bytearray(target.read_bytes()); data[8:12]=(999).to_bytes(4,'little'); target.write_bytes(data)
            elif mode.startswith('corrupt'): target.write_bytes(b'truncated')
            elif mode == 'identity':
                data=bytearray(target.read_bytes()); data[16] = ord('a') if data[16] != ord('a') else ord('b'); target.write_bytes(data)
            before=self.inventory(self.root)
            code = 'Ok' if mode=='supported' else 'UnsupportedSchema' if mode.startswith('unknown') else 'IdentityMismatch' if mode=='identity' else 'CorruptData'
            result=self.invoke(['compatibility','--directory',directory], 0 if mode=='supported' else 3, code)
            self.assertTrue(result['stableObservation'])
            self.assertFalse(result['mediaIntegrityVerified'])
            self.assertEqual(self.inventory(self.root), before)
            self.assertFalse((directory/'.monsterstudio.lock').exists())

    def test_optional_input_and_missing_project(self):
        directory=self.root/'optional'; self.fixture(directory); (directory/'input.mstudio').unlink()
        before=self.inventory(self.root)
        result=self.invoke(['compatibility','--directory',directory],0,'Ok')
        self.assertEqual(result['input']['code'],'NoInput')
        self.assertFalse(result['inputPresent'])
        self.assertEqual(self.inventory(self.root),before)
        self.invoke(['compatibility','--directory',self.root],3,'MissingProject')

    def test_reparse_directory_and_metadata_hardlink_rejected(self):
        target=self.root/'target'; target.mkdir()
        link=self.root/'junction'
        junction=subprocess.run(['cmd.exe','/c','mklink','/J',str(link),str(target)],capture_output=True,timeout=30)
        self.assertEqual(junction.returncode,0)
        try:
            self.invoke(['write','--directory',link],3,'UnsafePath')
            self.assertEqual(list(target.iterdir()),[])
        finally:
            os.rmdir(link)
        directory=self.root/'project'; self.fixture(directory)
        os.link(directory/'project.mstudio',self.root/'metadata-alias')
        before=self.inventory(self.root)
        self.invoke(['compatibility','--directory',directory],3,'UnsafePath')
        self.assertEqual(self.inventory(self.root),before)
    def test_real_long_unicode_cli_write_and_read(self):
        directory=self.root
        for _ in range(5): directory=directory/('\u9879\u76ee space '+('a'*45))
        extended=Path('\\\\?\\'+str(directory)); extended.mkdir(parents=True)
        seed=self.root/'seed'; self.fixture(seed)
        for path in seed.rglob('*'):
            target=extended/path.relative_to(seed)
            if path.is_dir(): target.mkdir()
            else: shutil.copyfile(path,target)
        self.assertGreater(len(str(directory)),300)
        before=self.inventory(extended)
        self.invoke(['write','--directory',directory],0,'Ok')
        result=self.invoke(['compatibility','--directory',extended],0,'Ok')
        self.assertTrue(result['input']['fullyValidatedStructure'])
        self.assertEqual(self.inventory(extended),before)

if __name__ == '__main__': unittest.main()



