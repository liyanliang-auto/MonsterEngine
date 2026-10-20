"""Real-file numeric checkpoint comparator acceptance and boundary tests.
No mocked decoding, synthetic PASS markers, runtime changes, or tolerance widening.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('numeric_recovery', ROOT/'Tools/VideoTo3D/run_brush_recovery.py')
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)
U64_MAX = (1 << 64) - 1


class NumericComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.store = Path(self.temporary.name)
        self.write('left', [1.0])
        self.write('right', [1.0])

    def write(self, name, values, shape=None):
        root = self.store/name
        (root/'payloads').mkdir(parents=True, exist_ok=True)
        raw = struct.pack('<'+'f'*len(values), *values)
        (root/'payloads'/'tensor').write_bytes(raw)
        descriptor = {'name':'means','payload':'tensor','shape':list(shape) if shape is not None else [len(values)],
                      'dtype':'f32le','bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'param_id':'stable_param_1'}
        manifest = {'schema_version':1,'identity':{'source_commit':'a'*40,'build_sha256':'b'*64,'config_sha256':'c'*64,'dataset_sha256':'d'*64,'backend':'Vulkan','device':'test-device'},
                    'next_iteration':1,'metadata':{'schema_version':1,'trainer':{'step':1},'sampler':{'next_sequence':1}},'tensors':[descriptor]}
        self.save(name, manifest)
        return manifest

    def read(self, name):
        return json.loads((self.store/name/'manifest.json').read_text())

    def save(self, name, manifest):
        (self.store/name/'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')

    def mutate(self, side, change):
        manifest = self.read(side)
        change(manifest)
        self.save(side, manifest)

    def compare(self, absolute=0.0, relative=0.0, exact=False):
        return recovery.compare_checkpoints(self.store,'left',self.store,'right',absolute,relative,exact)

    def rejects_mutation_each_side(self, change):
        for side in ('left','right'):
            with self.subTest(side=side):
                self.write('left',[1.0]);self.write('right',[1.0])
                self.mutate(side,change)
                with self.assertRaises(AssertionError):self.compare()

    def nonfinite_rejected(self, value):
        for exact in (False,True):
            for sides in (('left',),('right',),('left','right')):
                with self.subTest(exact=exact,sides=sides):
                    self.write('left',[1.0]);self.write('right',[1.0])
                    for side in sides:self.write(side,[value])
                    if len(sides)==2:
                        self.assertEqual((self.store/'left/payloads/tensor').read_bytes(),(self.store/'right/payloads/tensor').read_bytes())
                    with self.assertRaisesRegex(AssertionError,'Nonfinite tensor value'):self.compare(exact=exact)

    def test_nan_rejected_on_left_right_or_both_even_identical_exact(self):self.nonfinite_rejected(float('nan'))
    def test_positive_infinity_rejected_on_left_right_or_both_even_identical_exact(self):self.nonfinite_rejected(float('inf'))
    def test_negative_infinity_rejected_on_left_right_or_both_even_identical_exact(self):self.nonfinite_rejected(-float('inf'))

    def test_each_invalid_numeric_tolerance_rejected_in_both_modes(self):
        for exact in (False,True):
            for position in (0,1):
                for value in (float('nan'),float('inf'),-float('inf'),-1.0,-1,1 << 1100):
                    with self.subTest(exact=exact,position=position,value=str(value)):
                        tolerances=[0.0,0.0];tolerances[position]=value
                        with self.assertRaisesRegex(AssertionError,'Invalid .* tolerance'):self.compare(*tolerances,exact=exact)

    def test_non_numeric_or_boolean_tolerance_is_not_accepted_as_a_number(self):
        for value in (None,'1',[],{},True,False):
            for position in (0,1):
                with self.subTest(position=position,value=value):
                    t=[0.,0.];t[position]=value
                    with self.assertRaisesRegex(AssertionError,'tolerance'):self.compare(*t)

    def test_invalid_tolerance_is_rejected_before_operand_loading(self):
        with self.assertRaisesRegex(AssertionError,'Invalid absolute tensor tolerance'):
            recovery.compare_checkpoints(self.store,'missing-left',self.store,'missing-right',float('nan'),0.,True)

    def test_multiplication_overflowed_threshold_rejected_even_exact_zero_error(self):
        self.write('left',[2.0]);self.write('right',[2.0])
        for exact in (False,True):
            with self.subTest(exact=exact):
                with self.assertRaisesRegex(AssertionError,'arithmetic'):self.compare(0.,1e308,exact)

    def test_addition_overflowed_threshold_rejected_even_exact_zero_error(self):
        for exact in (False,True):
            with self.subTest(exact=exact):
                with self.assertRaisesRegex(AssertionError,'arithmetic'):self.compare(1e308,1e308,exact)

    def test_valid_finite_exact_payload_returns_original_result_contract(self):
        self.write('left',[0.,-2.,3.5]);self.write('right',[0.,-2.,3.5])
        self.assertEqual(self.compare(exact=True),{'passed':True,'exact':True,'maximumAbsoluteTensorError':0.0})

    def test_finite_error_exactly_at_original_absolute_bound_is_accepted(self):
        self.write('left',[8.]);self.write('right',[9.])
        self.assertEqual(self.compare(1.,0.)['maximumAbsoluteTensorError'],1.)

    def test_finite_error_inside_combined_bound_is_accepted(self):
        self.write('left',[8.]);self.write('right',[10.])
        self.assertTrue(self.compare(.5,.25)['passed'])

    def test_finite_error_outside_original_bound_is_rejected(self):
        self.write('left',[8.]);self.write('right',[10.])
        with self.assertRaisesRegex(AssertionError,'tolerance exceeded'):self.compare(1.,0.)

    def test_relative_bound_uses_reference_operand_not_symmetric_maximum(self):
        self.write('left',[8.]);self.write('right',[10.])
        with self.assertRaisesRegex(AssertionError,'tolerance exceeded'):self.compare(0.,.2)
        self.write('left',[10.]);self.write('right',[8.])
        self.assertTrue(self.compare(0.,.2)['passed'])

    def test_relative_bound_uses_absolute_reference_for_negative_values(self):
        self.write('left',[-8.]);self.write('right',[-10.])
        self.assertTrue(self.compare(0.,.25)['passed'])

    def test_exact_mode_keeps_byte_semantics_even_for_signed_zero(self):
        self.write('left',[0.]);self.write('right',[-0.])
        with self.assertRaisesRegex(AssertionError,'Exact boundary tensor mismatch'):self.compare(exact=True)
        self.assertEqual(self.compare()['maximumAbsoluteTensorError'],0.)

    def test_exact_mode_cannot_be_relaxed_by_large_finite_tolerance(self):
        self.write('right',[2.])
        with self.assertRaisesRegex(AssertionError,'Exact boundary tensor mismatch'):self.compare(100.,100.,True)

    def test_nonempty_zero_extent_tensor_remains_supported(self):
        for shape in ([0],[2,0,3],[0,U64_MAX]):
            with self.subTest(shape=shape):
                self.write('left',[],shape);self.write('right',[],shape)
                self.assertEqual(self.compare(exact=True)['maximumAbsoluteTensorError'],0.)

    def test_scalar_empty_shape_is_one_element(self):
        self.write('left',[1.],[]);self.write('right',[1.],[])
        self.assertTrue(self.compare(exact=True)['passed'])
        self.write('right',[],[])
        with self.assertRaises(AssertionError):self.compare()

    def test_rank_eight_valid_shape_is_supported(self):
        self.write('left',[1.],[1]*8);self.write('right',[1.],[1]*8)
        self.assertTrue(self.compare(exact=True)['passed'])

    def test_empty_missing_and_wrong_type_inventories_are_rejected(self):
        for value in ([],None,{},'tensor',1):
            with self.subTest(value=value):self.rejects_mutation_each_side(lambda m,v=value:m.update(tensors=v))
        self.rejects_mutation_each_side(lambda m:m.pop('tensors'))

    def test_inventory_above_runtime_count_limit_is_rejected(self):
        self.rejects_mutation_each_side(lambda m:m.update(tensors=m['tensors']*65537))

    def test_invalid_descriptor_entries_and_required_fields_rejected(self):
        for value in (None,[],1,'tensor'):
            with self.subTest(value=value):self.rejects_mutation_each_side(lambda m,v=value:m.update(tensors=[v]))
        for key in ('name','payload','shape','dtype','bytes','sha256'):
            with self.subTest(missing=key):self.rejects_mutation_each_side(lambda m,k=key:m['tensors'][0].pop(k))
        self.rejects_mutation_each_side(lambda m:m['tensors'][0].update(unknown_field=1))

    def test_duplicate_tensor_names_cannot_silently_overwrite(self):
        def change(m):
            other=copy.deepcopy(m['tensors'][0]);other['payload']='second';m['tensors'].append(other)
        for side in ('left','right'):
            self.write('left',[1.]);self.write('right',[1.]);(self.store/side/'payloads/second').write_bytes(struct.pack('<f',1.));self.mutate(side,change)
            with self.subTest(side=side):
                with self.assertRaises(AssertionError):self.compare()

    def test_duplicate_payload_names_cannot_alias_distinct_tensor_entries(self):
        def change(m):
            other=copy.deepcopy(m['tensors'][0]);other['name']='second';m['tensors'].append(other)
        self.rejects_mutation_each_side(change)

    def test_duplicate_json_fields_are_not_silently_overwritten(self):
        for side in ('left','right'):
            for field in ('inventory','descriptor'):
                with self.subTest(side=side,field=field):
                    self.write('left',[1.]);self.write('right',[1.]);p=self.store/side/'manifest.json';raw=p.read_text()
                    raw=raw.replace('"tensors":','"tensors": [], "tensors":',1) if field=='inventory' else raw.replace('"name": "means"','"name": "means", "name": "means"',1)
                    p.write_text(raw)
                    with self.assertRaisesRegex(AssertionError,'Duplicate checkpoint JSON field'):self.compare()

    def test_unsupported_dtype_never_reinterpreted_as_f32(self):
        for dtype in ('u32le','i32le','u64le','u8','f64le','float32',None):
            with self.subTest(dtype=dtype):self.rejects_mutation_each_side(lambda m,v=dtype:m['tensors'][0].update(dtype=v))

    def test_invalid_dimensions_and_shape_types_rejected(self):
        for shape in (None,{},'1',1,[1]*9,[-1],[True],[False],[1.5],['1'],[None],[U64_MAX+1],[0,U64_MAX+1]):
            with self.subTest(shape=shape):self.rejects_mutation_each_side(lambda m,v=shape:m['tensors'][0].update(shape=v))

    def test_u64_checked_byte_product_overflow_rejected_before_later_zero(self):
        for shape in ([1<<62],[1<<32,1<<32],[U64_MAX,0]):
            with self.subTest(shape=shape):self.rejects_mutation_each_side(lambda m,v=shape:m['tensors'][0].update(shape=v,bytes=0))

    def test_invalid_declared_byte_counts_rejected(self):
        for count in (-1,0,1,3,5,True,False,4.0,'4',None,U64_MAX+1):
            with self.subTest(count=count):self.rejects_mutation_each_side(lambda m,v=count:m['tensors'][0].update(bytes=v))

    def test_declared_shape_cannot_hide_count_or_payload_tail(self):
        self.rejects_mutation_each_side(lambda m:m['tensors'][0].update(shape=[2],bytes=8))
        for side in ('left','right'):
            self.write('left',[1.]);self.write('right',[1.]);(self.store/side/'payloads/tensor').write_bytes(struct.pack('<2f',1.,2.))
            with self.subTest(side=side):
                with self.assertRaises(AssertionError):self.compare()

    def test_truncated_or_unaligned_payload_is_rejected_without_zip_truncation(self):
        for side in ('left','right'):
            for size in (0,1,2,3):
                with self.subTest(side=side,size=size):
                    self.write('left',[1.]);self.write('right',[1.]);p=self.store/side/'payloads/tensor';p.write_bytes(p.read_bytes()[:size])
                    with self.assertRaisesRegex(AssertionError,'length/hash mismatch'):self.compare()

    def test_invalid_or_mismatched_payload_hash_rejected(self):
        for digest in ('a'*64,'A'*64,'0'*63,'',None,1):
            with self.subTest(digest=digest):self.rejects_mutation_each_side(lambda m,v=digest:m['tensors'][0].update(sha256=v))
        for side in ('left','right'):
            self.write('left',[1.]);self.write('right',[1.]);(self.store/side/'payloads/tensor').write_bytes(struct.pack('<f',2.))
            with self.subTest(side=side):
                with self.assertRaisesRegex(AssertionError,'length/hash mismatch'):self.compare()

    def test_unsafe_or_invalid_tensor_payload_and_parameter_components_rejected(self):
        for key in ('name','payload','param_id'):
            for value in ('','../outside','CON','aux','bad.name','x'*181,1):
                with self.subTest(key=key,value=value):self.rejects_mutation_each_side(lambda m,k=key,v=value:m['tensors'][0].update({k:v}))

    def test_valid_optional_parameter_id_can_be_omitted_on_both_sides(self):
        for side in ('left','right'):self.mutate(side,lambda m:m['tensors'][0].pop('param_id'))
        self.assertTrue(self.compare(exact=True)['passed'])

    def test_different_inventory_shape_or_parameter_mapping_rejected(self):
        for change in (lambda m:m['tensors'][0].update(name='other'),lambda m:m['tensors'][0].update(shape=[1,1]),lambda m:m['tensors'][0].update(param_id='different')):
            with self.subTest(change=change):self.rejects_mutation_each_side(change)

    def test_missing_payload_or_manifest_is_rejected(self):
        for side in ('left','right'):
            for path in ('manifest.json','payloads/tensor'):
                with self.subTest(side=side,path=path):
                    self.write('left',[1.]);self.write('right',[1.]);(self.store/side/path).unlink()
                    with self.assertRaises(AssertionError):self.compare()

    def test_malformed_manifest_json_and_nonobject_root_rejected(self):
        for content in ('{','[]','null','1','"manifest"'):
            for side in ('left','right'):
                with self.subTest(side=side,content=content):
                    self.write('left',[1.]);self.write('right',[1.]);(self.store/side/'manifest.json').write_text(content)
                    with self.assertRaises(AssertionError):self.compare()

    def test_runtime_identity_iteration_and_training_metadata_must_still_match(self):
        changes=(lambda m:m.update(next_iteration=2),lambda m:m['identity'].update(device='other'),lambda m:m['metadata']['trainer'].update(step=2),lambda m:m['metadata']['sampler'].update(next_sequence=2),lambda m:m['metadata'].update(schema_version=2))
        for change in changes:
            with self.subTest(change=change):self.rejects_mutation_each_side(change)

    def test_rejected_operand_files_are_preserved_byte_for_byte(self):
        self.write('right',[float('nan')]);before={p.relative_to(self.store):p.read_bytes() for p in self.store.rglob('*') if p.is_file()}
        with self.assertRaises(AssertionError):self.compare(exact=True)
        self.assertEqual(before,{p.relative_to(self.store):p.read_bytes() for p in self.store.rglob('*') if p.is_file()})

if __name__=='__main__':unittest.main(verbosity=2)
