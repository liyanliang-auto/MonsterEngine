"""Behavioral contracts for diagnostic-only P0 extensions; no GPU acceptance claims."""
import copy
import os
import json
from pathlib import Path
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'Tools/Android'))
import audit_frame_diagnostics as audit
import frame_optimization_quality as quality
import frame_optimization_budget as budget


def ready_row():
    identity = dict(frameId=17, submissionToken=31, sceneGeneration=2,
                    surfaceGeneration=3, configGeneration=4, pipelineGeneration=5)
    cut = dict(rootReady=True, loading=False, requestPages=0,
               maxSelectedProxyErrorPixels=.1, selectedPageCount=3,
               usedSelectedPages=3, consumptionSubmissionToken=31,
               transitionAdmitted=True, activeCount=23, selectionVersion=7,
               residencyVersion=9, qualitySatisfied=None)
    config = dict(shaderIdentity='d'*64, configHash='c'*64,
                  displaySHDegree=3, shStorageMode=0, qualityPixels=.1)
    return dict(**{k: v for k, v in identity.items() if k != 'pipelineGeneration'},
                measurement=dict(pipelineGeneration=5), status=dict(ok=True),
                renderWidth=1876, renderHeight=1105, contentHash=quality.MANIFEST_SHA256,
                effectiveConfig=config, buildIdentity=None,
                diagnosticsP0=dict(schemaVersion=1, identity=identity,
                    resources=dict(status='observed', aggregateLimitBytes=805306368,
                        componentCharges={name: 0 for name in budget.COMPONENTS},
                        reservedBytes=0, peakBytes=0, quotas={}, costs={},
                        admission=dict(status='admitted'), cut=cut)))


class NativePhysicalEvidence(unittest.TestCase):
    def test_ready_consumed_cut_does_not_circularly_require_pixel_pass(self):
        self.assertEqual(quality.reference_readiness(ready_row())['status'], 'ready')

    def test_rejected_submission_and_upload_only_cannot_prove_readiness(self):
        for key, value in [('usedSelectedPages', 0), ('consumptionSubmissionToken', 30),
                           ('transitionAdmitted', False), ('selectionVersion', 0),
                           ('maxSelectedProxyErrorPixels', .100001), ('requestPages', 1)]:
            with self.subTest(key=key):
                row = ready_row(); row['diagnosticsP0']['resources']['cut'][key] = value
                self.assertEqual(quality.reference_readiness(row)['status'], 'blocked')

    def test_each_stale_generation_and_bool_identity_blocks(self):
        for key in ready_row()['diagnosticsP0']['identity']:
            with self.subTest(key=key):
                row = ready_row(); row['diagnosticsP0']['identity'][key] += 1
                self.assertEqual(quality.reference_readiness(row)['status'], 'blocked')
        row = ready_row(); row['frameId'] = True
        self.assertEqual(quality.reference_readiness(row)['status'], 'blocked')

    def test_absent_native_extension_is_unknown_not_zero(self):
        self.assertEqual(quality.reference_readiness({})['reasons'],
                         ['native_detail_admission_extension_unavailable'])

    def test_refusal_does_not_establish_all_cuts_impossible(self):
        row = ready_row(); row['diagnosticsP0']['resources']['admission'] = dict(
            status='refused', reasonCodes=['aggregate_limit'], requestedBytes=100, availableBytes=50)
        report = budget._resource_observation(row, {}, row['effectiveConfig'])
        self.assertEqual(report['reservationObservationStatus'], 'refused')
        self.assertEqual(report['admission']['requestedBytes'], 100)
        self.assertEqual(report['qualitySatisfyingAdmission']['status'], 'blocked')

    def test_independent_pixel_linkage_gap_keeps_physical_facts(self):
        row = ready_row(); report = budget._resource_observation(row, {}, row['effectiveConfig'])
        self.assertEqual(report['physicalCutReadiness']['status'], 'observed')
        self.assertEqual(report['accountStatus'], 'observed')
        self.assertIsNone(report['processMemory']['pssBytes'])
        self.assertEqual(report['qualitySatisfyingAdmission']['requiredConsumerInput'], 'qualityReceipt')
        self.assertEqual(report['qualitySatisfyingAdmission']['status'], 'blocked')
        self.assertIsNone(report['cut']['qualitySatisfied'])

    def test_accounting_rejects_double_charge_and_cap_change(self):
        for field, value in [('reservedBytes', 1), ('aggregateLimitBytes', 805306369)]:
            row = ready_row(); row['diagnosticsP0']['resources'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                budget._resource_observation(row, {}, row['effectiveConfig'])

    def test_unknown_allocation_is_not_invented_and_overlaps_not_summed(self):
        row = ready_row(); costs = row['diagnosticsP0']['resources']['costs']
        costs['worker'] = dict(kind='overlapping', logicalBytes=4096, accounting='overlaps_cpu_cache')
        report = budget._resource_observation(row, {}, row['effectiveConfig'])
        self.assertEqual(report['account']['reservedBytes'], 0)
        self.assertNotIn('source', report['costs'])
        self.assertTrue(any('actual Vulkan' in gap for gap in report['gaps']))



class NativeConfigIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Actual compiled C++ SnapshotJSON output; controlled inputs, not runtime proof.
        path=next(Path(os.environ['MONSTER_P0_NATIVE_ARTIFACT_ROOT']).rglob('p0-controlled-config.json'))
        cls.source=path
        cls.row=json.loads(path.read_text())['frames'][-1]

    def observe(self,row=None,config=None):
        row=copy.deepcopy(self.row if row is None else row)
        report=budget._resource_observation(row,[],row['effectiveConfig'] if config is None else config)
        self.assertEqual(report['qualitySatisfyingAdmission']['status'],'blocked')
        return report

    @staticmethod
    def envelope(config):
        streaming=('sceneGPUBytes','stagingBytes','cpuCacheBytes','temporaryGPUBytes',
            'maxUploadBytesPerFrame','maxConcurrentReads','retries','retryDelayMilliseconds')
        lod=('qualityPixels','hysteresis','activeBudget','sortBudget','sourceBudget','freeze')
        top=('generation','configHash','shaderIdentity','maximumInternalEdge','maximumInternalPixels')
        envelope={k:config[k] for k in top}
        runtime={k:v for k,v in config.items() if k not in top+streaming+lod+('fusionImplementation','fusionRelativeRGBAError')}
        runtime.update(benchmark=False,validation=False,streaming={k:config[k] for k in streaming},lod={k:config[k] for k in lod})
        runtime['lod']['qualityPixels']=.1
        envelope.update(schemaVersion=1,runtime=runtime)
        return envelope

    def test_actual_cpp_flat_config_and_consumed_cut_are_recognized(self):
        config=self.row['effectiveConfig']
        self.assertNotIn('runtime',config)
        self.assertEqual(config['displaySHDegree'],3)
        self.assertEqual(config['shStorageMode'],0)
        self.assertEqual(config['qualityPixels'],0.10000000149011612)
        self.assertEqual(config['configHash'],'c'*64)
        report=self.observe()
        self.assertEqual(report['physicalCutReadiness']['status'],'observed')
        self.assertEqual(report['accountStatus'],'observed')
        self.assertEqual(report['configurationEvidence']['effectiveShape'],'flat_frame')

    def test_legitimate_jni_envelope_joins_both_directions_and_discloses_raw_f32(self):
        config=self.row['effectiveConfig'];envelope=self.envelope(config)
        report=self.observe(config=envelope)
        self.assertEqual(report['physicalCutReadiness']['status'],'observed')
        evidence=report['configurationEvidence']
        self.assertEqual(evidence['suppliedRawQualityPixels'],.1)
        self.assertEqual(evidence['effectiveRawQualityPixels'],0.10000000149011612)
        self.assertEqual(evidence['suppliedQualityPixels'],evidence['effectiveQualityPixels'])
        self.assertEqual(set(evidence['unavailableAcrossShapes']),{'fusionImplementation','fusionRelativeRGBAError'})
        nested=copy.deepcopy(self.row);nested['effectiveConfig']=envelope
        for supplied in (config,envelope):
            self.assertEqual(self.observe(nested,supplied)['physicalCutReadiness']['status'],'observed')

    def test_missing_invalid_or_conflicting_config_facts_never_prove_readiness(self):
        for field,value in [('displaySHDegree',3.0),('shStorageMode',False),('qualityPixels',None),('qualityPixels',True),('generation',True),('configHash',None),('shaderIdentity','E'*64)]:
            with self.subTest(field=field,value=value):
                row=copy.deepcopy(self.row);row['effectiveConfig'][field]=value
                self.assertEqual(self.observe(row)['physicalCutReadiness']['status'],'blocked')
        for field in ('displaySHDegree','shStorageMode','qualityPixels','configHash','shaderIdentity'):
            row=copy.deepcopy(self.row);del row['effectiveConfig'][field]
            self.assertEqual(self.observe(row)['physicalCutReadiness']['status'],'blocked')
        for field,value in [('displaySHDegree',2),('shStorageMode',False),('qualityPixels',.2)]:
            row=copy.deepcopy(self.row);row['effectiveConfig']['runtime']={'displaySHDegree':3,'shStorageMode':0,'lod':{'qualityPixels':.1}}
            row['effectiveConfig'][field]=value
            with self.subTest(conflict=field),self.assertRaisesRegex(ValueError,'conflicting'):
                self.observe(row)

    def test_exact_nonfloat_types_hashes_and_native_threshold_are_preserved(self):
        for field,value in [('configHash','e'*64),('shaderIdentity','e'*64),('sortCapacity',2000001),('vsync',1),('qualityPixels',.1)]:
            supplied=copy.deepcopy(self.row['effectiveConfig']);supplied[field]=value
            with self.subTest(field=field):
                self.assertEqual(self.observe(config=supplied)['physicalCutReadiness']['status'],'blocked')
        row=copy.deepcopy(self.row)
        row['diagnosticsP0']['resources']['cut']['maxSelectedProxyErrorPixels']=0.10000000149011613
        self.assertEqual(self.observe(row)['physicalCutReadiness']['status'],'blocked')
        # No broad tolerance or rounding of native flat values.
        row=copy.deepcopy(self.row);row['effectiveConfig']['qualityPixels']=.1
        self.assertEqual(self.observe(row)['physicalCutReadiness']['status'],'blocked')


def event(seq, phase='I', name='owner.render', **changes):
    value = dict(eventSequence=seq, timestampNs=10000000000000000+seq*1000,
        threadId=1, captureEpoch=7, clockDomain='CLOCK_MONOTONIC', phase=phase,
        name=name, requestId=9, frameId=17, submissionId=31, sceneId=2, pageId=1, value=0)
    value.update(changes); return value


class ExactTimingEvidence(unittest.TestCase):
    def test_overlap_recovers_replacements_and_preserves_uint64(self):
        with tempfile.TemporaryDirectory() as tmp:
            for index, sequences in enumerate(([1,2,4], [2,3,4,5])):
                export = dict(schemaVersion=1, clockDomain='CLOCK_MONOTONIC',captureEpoch=7,
                    events=[event(s) for s in sequences], droppedEvents=2,
                    overwrittenEvents=index+10, recordCalls=index+20)
                Path(tmp, str(index)+'.json').write_text(json.dumps(dict(captureSequence=index,
                    profilingRawJSON=json.dumps(export))))
            rows, _, loss = audit.native_union(tmp)
            self.assertEqual(len(rows), 5); self.assertEqual(loss['duplicatesMerged'], 2)
            epoch = loss['epochs'][0]
            self.assertEqual(epoch['missingInternalSequenceCount'], 0)
            self.assertEqual(epoch['cumulativeCounters']['overwrittenEvents']['delta'], 1)
            self.assertEqual(rows[0]['timestampNs'], 10000000000001000)

    def test_conflicting_overlap_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            for i in range(2):
                export = dict(schemaVersion=1, clockDomain='CLOCK_MONOTONIC', captureEpoch=7,
                    events=[event(1, value=i)], droppedEvents=0, overwrittenEvents=0, recordCalls=1)
                Path(tmp, str(i)+'.json').write_text(json.dumps(dict(captureSequence=i,
                    profilingRawJSON=json.dumps(export))))
            with self.assertRaisesRegex(ValueError, 'Conflicting'): audit.native_union(tmp)

    def test_gap_and_generation_context_never_form_complete_scope(self):
        for end in (event(3, 'E'), event(2, 'E', sceneId=3)):
            report = audit.pair_intervals([event(1, 'B'), end])
            self.assertEqual(report['scopes'], [])

    def test_signed_vkresult_only_and_exact_duration(self):
        report = audit.pair_intervals([event(1,'B','vulkan.acquire'),
            event(2,'E','vulkan.acquire', value=2**64-4),
            event(3,'I','vulkan.submit_dispatch',value=1)])
        self.assertEqual(report['scopes'][0]['endValue'], -4)
        self.assertEqual(report['scopes'][0]['milliseconds'], .001)
        self.assertEqual(report['outcomeValues']['vulkan.submit_dispatch'], {'1':1})

    def test_ambiguous_request_or_sequence_gap_has_no_latency(self):
        report = audit.pair_intervals([event(1,name='frame.request_received'),
            event(2,name='frame.request_received'),event(3,'B')])
        self.assertEqual(report['requestToOwner'], [])
        report = audit.pair_intervals([event(1,name='frame.request_received'),event(3,'B')])
        self.assertEqual(report['requestToOwner'], [])

    def test_duplicate_json_keys_cannot_override_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp,'raw.json');path.write_text('{"frameId":1,"frameId":2}')
            with self.assertRaises(ValueError):audit.read_document(path)


class DurableCopyContracts(unittest.TestCase):
    def check_receipt(self, folder):
        # Compile the actual nested host verifier with only its closure inputs.
        import ast, argparse, hashlib
        tree=ast.parse((ROOT/'Tools/Android/capture_profile.py').read_text(encoding='utf-8-sig'))
        node=next(n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name=='cleanup_quality_exports')
        namespace=dict(json=json,args=argparse.Namespace(purpose='quality'),evidence={},
            sha256=lambda p:hashlib.sha256(p.read_bytes()).hexdigest())
        exec(compile(ast.Module(body=[node],type_ignores=[]),'actual-capture-receipt','exec'),namespace)
        namespace['cleanup_quality_exports'](folder)
        return namespace['evidence']['privateScreenshotCleanup']

    def test_durable_receipt_requires_exact_files_hashes_and_cleanup_without_changing_evidence(self):
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);quality_dir=folder/'quality';quality_dir.mkdir();files=[]
            for suffix,data in [('.png',b'png-original'),('.native.json',b'{"frameId":7}')]:
                file=quality_dir/('pose'+suffix);file.write_bytes(data);digest=hashlib.sha256(data).hexdigest()
                files.append(dict(path='quality/'+file.name,bytes=len(data),sourceSha256=digest,copySha256=digest))
            obs=quality_dir/'pose.observation.json';obs.write_text(json.dumps(dict(status='captured',privateExportFilesRemoved=True)))
            receipt=quality_dir/'pose.private-copy-receipt.json';original=dict(schemaVersion=1,durableCopyVerified=True,files=files)
            receipt.write_text(json.dumps(original));before={p.name:p.read_bytes() for p in quality_dir.iterdir()}
            self.assertEqual(self.check_receipt(folder)['status'],'durable_device_copy_and_host_hash_verified')
            self.assertEqual(before,{p.name:p.read_bytes() for p in quality_dir.iterdir()})
            for mutation in ['hash','path','source','not_durable','not_removed']:
                with self.subTest(mutation=mutation):
                    value=copy.deepcopy(original);observation=dict(status='captured',privateExportFilesRemoved=True)
                    if mutation=='hash':value['files'][0]['copySha256']='0'*64
                    elif mutation=='path':value['files'][0]['path']='../private.png'
                    elif mutation=='source':value['files'][0]['sourceSha256']='0'*64
                    elif mutation=='not_durable':value['durableCopyVerified']=False
                    else:observation['privateExportFilesRemoved']=False
                    receipt.write_text(json.dumps(value));obs.write_text(json.dumps(observation))
                    with self.assertRaises(ValueError):self.check_receipt(folder)


class FixedRoiContracts(unittest.TestCase):
    def policy(self):
        import frame_optimization_quality as q
        camera=dict(representation='observed-orbit-v1',pathSha256='a'*64,sampleIndex=0,externalCamera=False,
                    **{key:1.0 for key in q.ORBIT_FIELDS})
        camera['projection']={'verticalFovDegrees':60,'nearPlane':.1,'farPlane':100,'rollDegrees':0,'configIdentity':dict(path='synthetic',bytes=2,sha256='b'*64)}
        image=dict(path='synthetic',bytes=1,sha256='c'*64)
        return dict(schemaVersion=1,manifestSha256=q.MANIFEST_SHA256,modelSha256='d'*64,
            manifestIdentity=dict(sha256=q.MANIFEST_SHA256),modelIdentity=dict(sha256='d'*64),
            qualitySettings=dict(shDegree=3,sourcePrecision='FP32',renderWidth=1876,renderHeight=1105,
                lodErrorThreshold=.1,resolutionScale=1,presentationPolicy={'exposure':1}),
            encoding=dict(format='png-rgb8',alpha='opaque',rowOrigin='top_left',colorSpace='srgb-encoded'),
            thresholds=q.THRESHOLDS,algorithm=q.ALGORITHM,poses=[dict(id='original',reference=image,metadata=image,
                cameraIdentity=camera,nativeQualityBinding=dict(contentHash=q.MANIFEST_SHA256,
                    configHash='e'*64,shaderIdentity='f'*64,presentation={k:1 for k in q.PRESENTATION_FIELDS}),
                roiInspection=dict(imageSha256='c'*64,inspectedBeforeCandidate=True,
                    features=dict(piano_keys='present',bicycle_thin_structures='absent')),
                rois=[dict(id='keys',feature='piano_keys',xywh=[0,0,11,11])])])

    def test_visible_features_need_fixed_inspected_bounded_rois(self):
        policy=self.policy();quality._validate_policy(policy)
        mutations=[('no-roi',lambda p:p['poses'][0].update(rois=[])),
            ('wrong-feature',lambda p:p['poses'][0]['rois'][0].update(feature='other')),
            ('outside',lambda p:p['poses'][0]['rois'][0].update(xywh=[1870,0,11,11])),
            ('bool-coordinate',lambda p:p['poses'][0]['rois'][0].update(xywh=[True,0,11,11])),
            ('changed-image',lambda p:p['poses'][0]['roiInspection'].update(imageSha256='0'*64)),
            ('late-inspection',lambda p:p['poses'][0]['roiInspection'].update(inspectedBeforeCandidate=False))]
        for name,apply in mutations:
            with self.subTest(name=name):
                changed=copy.deepcopy(policy);apply(changed)
                with self.assertRaises((ValueError,quality.EvidenceUnavailable)):quality._validate_policy(changed)

    def test_registered_roi_coordinates_cannot_change_after_policy_digest(self):
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            model=Path(tmp)/'fixture-model';model.write_bytes(b'public-synthetic-model')
            policy=self.policy();policy['modelSha256']=hashlib.sha256(model.read_bytes()).hexdigest()
            policy['modelIdentity']=dict(path=str(model),bytes=model.stat().st_size,sha256=policy['modelSha256'])
            manifest=next(p for p in (ROOT/'build/android-builtin-lod').rglob('scene.mgs')
                if hashlib.sha256(p.read_bytes()).hexdigest()==quality.MANIFEST_SHA256)
            policy['manifestIdentity']=dict(path=str(manifest),bytes=manifest.stat().st_size,sha256=quality.MANIFEST_SHA256)
            digest=quality._sha(quality._json_bytes(policy))
            frozen=dict(status='pass',policy=policy,policySha256=digest)
            policy['poses'][0]['rois'][0]['xywh']=[1,0,11,11]
            with self.assertRaisesRegex(ValueError,'mutable/unregistered'):
                quality.compare(frozen,dict(registeredPolicySha256=digest),[])

if __name__ == '__main__': unittest.main()
