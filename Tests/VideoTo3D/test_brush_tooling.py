"""Behavior checks for new capture/baseline tooling; no external tools mocked."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
def load(name):
    s=importlib.util.spec_from_file_location(name,ROOT/'Tools/VideoTo3D'/f'{name}.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
baseline=load('run_brush_baseline');prepare=load('prepare_brush_scene')
class BrushToolingTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
    def ply(self,values=None):
        props=['x','y','z','f_dc_0','f_dc_1','f_dc_2','opacity','scale_0','scale_1','scale_2','rot_0','rot_1','rot_2','rot_3']
        p=self.root/'model.ply';p.write_bytes(('ply\nformat binary_little_endian 1.0\nelement vertex 1\n'+''.join('property float '+n+'\n' for n in props)+'end_header\n').encode()+struct.pack('<14f',*(values or [0.0]*14)));return p
    def test_valid_gaussian_export_is_accepted(self):self.assertEqual(baseline.validate_ply(self.ply(),1)['gaussians'],1)
    def test_truncated_export_rejected_and_original_preserved(self):
        p=self.ply();p.write_bytes(p.read_bytes()[:-1]);raw=p.read_bytes()
        with self.assertRaisesRegex(ValueError,'length'):baseline.validate_ply(p,1)
        self.assertEqual(p.read_bytes(),raw)
    def test_nonfinite_export_rejected(self):
        with self.assertRaisesRegex(ValueError,'nonfinite'):baseline.validate_ply(self.ply([float('nan')]+[0.]*13),1)
    def test_overbudget_export_rejected(self):
        with self.assertRaisesRegex(ValueError,'out-of-budget'):baseline.validate_ply(self.ply(),0)
    def test_evidence_write_never_replaces_existing_result(self):
        for writer in (baseline.write_json,prepare.write_json):
            p=self.root/str(id(writer));p.write_bytes(b'prior evidence')
            with self.assertRaises(FileExistsError):writer(p,{'new':1})
            self.assertEqual(p.read_bytes(),b'prior evidence')
    def test_geometry_admission_rejects_each_insufficient_capture_measure(self):
        metrics={'extractedFrames':44,'registeredImages':44,'registeredFraction':1.,'temporalCoverage':1.,'sparsePoints':1000,'medianReprojectionErrorPixels':.5,'positiveObservedDepthFraction':1.,'cameraSpanToSceneDiagonal':.1}
        self.assertTrue(prepare.admission(metrics)['passed'])
        for key in metrics:
            bad=dict(metrics);bad[key]=1000 if key=='medianReprojectionErrorPixels' else 0
            self.assertFalse(prepare.admission(bad)['passed'],key)
if __name__=='__main__':unittest.main()
