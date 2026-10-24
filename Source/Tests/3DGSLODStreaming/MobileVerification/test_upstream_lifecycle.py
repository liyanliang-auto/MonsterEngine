"""Native serializer/host causal contracts; controlled fixtures are not runtime acceptance."""
import copy
import contextlib
import io
import itertools
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'Tools/Android'))
import audit_frame_diagnostics as audit


class NativeLifecycleContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = max(Path(os.environ['MONSTER_P0_NATIVE_ARTIFACT_ROOT']).rglob('lifecycle-v2-native.json'), key=lambda p: p.stat().st_mtime_ns)
        cls.document = json.loads(cls.path.read_text())
        cls.events = cls.document['frames'][-1]['diagnosticsP0']['detail']['events']

    def reconcile(self, rows=None, observations=None):
        return audit.reconcile_lifecycle(copy.deepcopy(self.events if rows is None else rows), observations or [])

    def test_real_native_serialization_complete_retained_subset(self):
        result = self.reconcile()
        chain = result['chains'][0]
        self.assertEqual(chain['status'], 'complete_retained_chain')
        self.assertEqual((chain['upstreamOriginId'], chain['workId'], chain['workOriginId']), (101, 201, 101))
        self.assertEqual(chain['origin']['originRequestId'], 41)
        self.assertEqual(chain['roles'], ['desired'])
        self.assertGreaterEqual(chain['originToSuccessfulConsumptionMilliseconds'], 0)
        self.assertNotEqual(chain['upstreamOriginId'], self.events[-1]['demandSequence'])

    def test_missing_each_physical_stage_never_grants_complete_chain(self):
        for stage in ('queued', 'read_started', 'read_complete', 'validated', 'cpu_ready', 'ready_transferred', 'upload_recorded', 'upload_submitted', 'gpu_complete', 'active_ready', 'first_use'):
            with self.subTest(stage=stage):
                chain = self.reconcile([e for e in self.events if e['stage'] != stage])['chains'][0]
                self.assertNotEqual(chain['status'], 'complete_retained_chain')
                self.assertIsNone(chain['originToSuccessfulConsumptionMilliseconds'])

    def test_clock_reordering_loss_and_conflicting_slots_are_explicit(self):
        for mutation in ('clock', 'slot', 'generation', 'token', 'retired', 'dropped'):
            rows = copy.deepcopy(self.events)
            obs = []
            if mutation == 'clock': rows[3]['timestampNanoseconds'] = 1
            if mutation == 'slot': rows[-1]['pageLifetime'] = 999
            if mutation == 'generation': rows[-1]['pipelineGeneration'] = None
            if mutation == 'token': rows[-1]['submissionToken'] = None
            if mutation == 'retired': rows[-2]['stage'] = 'upstream_retired'
            if mutation == 'dropped': obs = [dict(sceneGeneration=1, dropped=1)]
            with self.subTest(mutation=mutation):
                chain = self.reconcile(rows, obs)['chains'][0]
                self.assertNotEqual(chain['status'], 'complete_retained_chain')
                self.assertIsNone(chain['originToSuccessfulConsumptionMilliseconds'])

    def test_repeated_retry_stages_without_attempt_identity_stay_ambiguous(self):
        rows = copy.deepcopy(self.events)
        extra = copy.deepcopy(rows[2]); rows.insert(3, extra)
        for n, e in enumerate(rows): e['sequence'] = n + 1; e['timestampNanoseconds'] = 1000 + n
        chain = self.reconcile(rows)['chains'][0]
        self.assertIn('repeated_read_started_without_attempt_identity', chain['ambiguities'])
        self.assertIsNone(chain['originToSuccessfulConsumptionMilliseconds'])

    def test_cache_hit_bypasses_only_read_not_validation(self):
        rows = [copy.deepcopy(e) for e in self.events if e['stage'] != 'read_complete']
        next(e for e in rows if e['stage'] == 'read_started')['stage'] = 'cache_hit'
        for n, e in enumerate(rows): e['sequence'] = n + 1
        self.assertEqual(self.reconcile(rows)['chains'][0]['status'], 'complete_retained_chain')
        rows = [e for e in rows if e['stage'] != 'validated']
        self.assertIn('missing_validated', self.reconcile(rows)['chains'][0]['missingStages'])

    def test_normal_upload_chunking_is_not_a_retry(self):
        rows = copy.deepcopy(self.events)
        rows[9:9] = [copy.deepcopy(rows[7]), copy.deepcopy(rows[8])]
        for n, e in enumerate(rows): e['sequence'] = n + 1; e['timestampNanoseconds'] = 1000 + n
        chain = self.reconcile(rows)['chains'][0]
        self.assertEqual(chain['status'], 'complete_retained_chain')
        self.assertEqual(len(chain['physicalStageSequences']['upload_submitted']), 2)

    def test_repeated_coalescing_preserves_original_physical_work(self):
        rows = copy.deepcopy(self.events)
        later = copy.deepcopy(rows[0]); later.update(upstreamOriginId=102, representationFlags=2)
        relation = copy.deepcopy(later); relation.update(stage='work_coalesced', workId=201, workOriginId=101)
        use = copy.deepcopy(rows[-1]); use.update(upstreamOriginId=102, representationFlags=2)
        rows = rows[:-1] + [later, relation, copy.deepcopy(relation), use]
        for n, e in enumerate(rows): e['sequence'] = n + 1; e['timestampNanoseconds'] = 1000 + n
        chain = next(c for c in self.reconcile(rows)['chains'] if c['upstreamOriginId'] == 102)
        self.assertEqual(chain['status'], 'complete_retained_chain')
        self.assertEqual(chain['workOriginId'], 101)
        self.assertEqual(chain['roles'], ['selected_fallback'])
        self.assertEqual(len(self.reconcile(rows)['coalescingRelations']), 2)

    def test_pool_namespace_never_fills_missing_upstream_origin(self):
        rows = copy.deepcopy(self.events)
        for e in rows: e['upstreamOriginId'] = None
        result = self.reconcile(rows)
        self.assertEqual(result['chains'], [])
        self.assertEqual(len(result['unassignedEvents']), len(rows))

    def test_native_raw_capture_overlap_conflict_and_disabled_bounds(self):
        with tempfile.TemporaryDirectory(dir=os.environ['MONSTER_P0_TEST_OUTPUT']) as tmp:
            root = Path(tmp); raw = root/'harness/raw'; raw.mkdir(parents=True)
            (root/'harness/metadata.json').write_text(json.dumps(dict(captureSession='unit', captureCompleted=True, profilingMode='on')))
            doc = dict(captureSession='unit', payload=dict(diagnostics=copy.deepcopy(self.document), profilingEnabled=True))
            for n in range(2): (raw/f'{n}.json').write_text(json.dumps(doc))
            result = audit.audit_lifecycle_capture(root)
            self.assertEqual(result['uniqueEvents'], 12)
            self.assertEqual(result['duplicateObservationsMerged'], 12)
            self.assertEqual(result['chainStatusCounts']['complete_retained_chain'], 1)
            doc['payload']['diagnostics']['frames'][-1]['diagnosticsP0']['detail']['events'][3]['workId'] = 999
            (raw/'1.json').write_text(json.dumps(doc))
            result = audit.audit_lifecycle_capture(root)
            self.assertEqual(result['status'], 'fail')
            self.assertNotIn('complete_retained_chain', result['chainStatusCounts'])
            (raw/'1.json').unlink(); doc['payload']['diagnostics']['frames'][-1]['diagnosticsP0']['detail']['events'] = []
            doc['payload']['profilingEnabled'] = False
            (raw/'0.json').write_text(json.dumps(doc))
            result = audit.audit_lifecycle_capture(root)
            self.assertEqual(result['upstreamEvidenceStatus'], 'unavailable')
            self.assertEqual(result['chains'], [])

    @staticmethod
    def renumber(rows):
        for n, event in enumerate(rows, 1):
            event['sequence'], event['timestampNanoseconds'] = n, 1000 + n
        return rows

    def coalesced(self, count):
        rows = copy.deepcopy(self.events)
        for n in range(count):
            demand = copy.deepcopy(self.events[0])
            demand.update(upstreamOriginId=102+n, representationFlags=2 if n%2 == 0 else 8,
                originFrameId=100+n, originRequestId=200+n, frameId=100+n, requestId=200+n)
            relation = copy.deepcopy(demand)
            relation.update(stage='work_coalesced', workId=201, workOriginId=101)
            use = copy.deepcopy(self.events[-1])
            use.update({key:demand[key] for key in ('upstreamOriginId','representationFlags','originFrameId','originRequestId','frameId','requestId')})
            use['submissionToken'] = 500+n
            rows.extend((demand,relation,use))
        return self.renumber(rows)

    def reloads(self, count):
        rows = [copy.deepcopy(self.events[0])]
        for n in range(count):
            work = copy.deepcopy(self.events[1:])
            for event in work:
                event.update(workId=201+n, pageLifetime=301+n, submissionToken=31+2*n, frameId=17+n)
            work[-1]['submissionToken'] += 1
            rows.extend(work)
        return self.renumber(rows)

    def assert_borrowers_unavailable(self, rows):
        result = self.reconcile(rows)
        borrowers = [c for c in result['chains'] if c['upstreamOriginId'] in (102,103)]
        self.assertEqual(len(borrowers), 2)
        for chain in borrowers:
            self.assertNotEqual(chain['status'], 'complete_retained_chain')
            self.assertIsNone(chain['originToSuccessfulConsumptionMilliseconds'])

    def test_native_demand_has_no_future_work_slot_or_successful_submission(self):
        demand = self.events[0]
        self.assertEqual(demand['stage'], 'upstream_demand')
        for field in ('workId','workOriginId','pageLifetime','submissionToken','selectionVersion','residencyVersion','demandSequence'):
            with self.subTest(field=field): self.assertIsNone(demand[field])
        self.assertEqual(self.events[1]['workId'], 201)
        self.assertEqual(self.events[1]['workOriginId'], 101)
        self.assertEqual(self.reconcile()['chains'][0]['status'], 'complete_retained_chain')

    def test_each_contradictory_accepted_demand_invalidates_all_borrowers(self):
        for field in ('sceneGeneration','pageId','originSceneGeneration','originPageId','originFrameId','originRequestId',
                'originSurfaceGeneration','originConfigGeneration','originPipelineGeneration','representationFlags'):
            rows = self.coalesced(2); rows[0][field] = 999
            with self.subTest(field=field): self.assert_borrowers_unavailable(rows)
        rows = self.coalesced(2); rows[0].update(pageId=999, originPageId=999)
        self.assert_borrowers_unavailable(rows)

    def test_missing_or_duplicate_accepted_origin_invalidates_all_borrowers(self):
        for mutation in ('missing','duplicate','unknown_identity'):
            rows = self.coalesced(2)
            if mutation == 'missing': rows.pop(0)
            elif mutation == 'duplicate': rows.insert(0,copy.deepcopy(rows[0]))
            else: rows[0]['originConfigGeneration'] = None
            with self.subTest(mutation=mutation): self.assert_borrowers_unavailable(self.renumber(rows))

    def test_every_repeated_upload_chunk_slot_page_and_cause_is_checked(self):
        for stage in ('upload_recorded','upload_submitted'):
            for field in ('pageLifetime','pageId','originPageId','originFrameId','representationFlags'):
                rows = copy.deepcopy(self.events)
                extra = copy.deepcopy(next(e for e in rows if e['stage']==stage)); extra[field]=999
                rows.insert(9, extra)
                with self.subTest(stage=stage,field=field):
                    chain = self.reconcile(self.renumber(rows))['chains'][0]
                    self.assertNotEqual(chain['status'],'complete_retained_chain')
                    self.assertIsNone(chain['originToSuccessfulConsumptionMilliseconds'])

    def test_last_upload_completion_ready_tokens_and_distinct_consumer(self):
        rows = copy.deepcopy(self.events)
        recorded,submitted = copy.deepcopy(rows[7]),copy.deepcopy(rows[8]); submitted['submissionToken']=32
        rows[9:9]=[recorded,submitted]
        for e in rows:
            if e['stage'] in ('gpu_complete','active_ready'): e['submissionToken']=32
            if e['stage']=='first_use': e['submissionToken']=99
        rows=self.renumber(rows)
        self.assertEqual(self.reconcile(rows)['chains'][0]['status'],'complete_retained_chain')
        for stage in ('upload_submitted','gpu_complete','active_ready'):
            for token in (None,0,999999):
                broken=copy.deepcopy(rows)
                [e for e in broken if e['stage']==stage][-1]['submissionToken']=token
                with self.subTest(stage=stage,token=token):
                    chain=self.reconcile(broken)['chains'][0]
                    self.assertNotEqual(chain['status'],'complete_retained_chain')
                    self.assertIsNone(chain['originToSuccessfulConsumptionMilliseconds'])

    def test_first_consumer_cutoff_does_not_borrow_later_gap_or_terminal(self):
        for mutation in ('gap','terminal'):
            rows=self.coalesced(1)
            if mutation=='gap':
                for e in rows[12:]: e['sequence']+=1
            else:
                terminal=copy.deepcopy(rows[1]);terminal['stage']='cancelled';rows.insert(12,terminal);self.renumber(rows)
            report=self.reconcile(rows)
            first=next(c for c in report['chains'] if c['upstreamOriginId']==101)
            later=next(c for c in report['chains'] if c['upstreamOriginId']==102)
            with self.subTest(mutation=mutation):
                self.assertEqual(first['status'],'complete_retained_chain')
                self.assertNotEqual(later['status'],'complete_retained_chain')
                self.assertIsNone(later['originToSuccessfulConsumptionMilliseconds'])

    def structural_growth(self, kind, builder, sizes):
        metrics=[]
        for size in sizes:
            counter=[0]
            class CountedEvent(dict):
                def __getitem__(self,key): counter[0]+=1; return super().__getitem__(key)
                def get(self,key,default=None): counter[0]+=1; return super().get(key,default)
                def items(self): counter[0]+=len(self); return super().items()
            rows=[CountedEvent(e) for e in builder(size)]
            report=audit.reconcile_lifecycle(rows,[])
            accesses=counter[0]
            self.assertTrue(all(c['status']=='complete_retained_chain' for c in report['chains']))
            self.assertEqual(report['lifecycleReportSchemaVersion'],2)
            inline=sum(len(c['originEventSequences'] or [])+sum(map(len,(c['physicalStageSequences'] or {}).values())) for c in report['chains'])
            shared=sum(len(h['eventSequences']) for h in report['histories'])
            self.assertLessEqual(inline,16*len(rows))
            self.assertLessEqual(shared,2*len(rows))
            self.assertEqual(len(report['retainedEvents']),len(rows))
            self.assertFalse(report['analysisBounds']['truncated'])
            for chain in report['chains']:
                history=report['histories'][chain['originHistoryRef']]
                self.assertEqual(history['kind'],'origin')
                if len(history['eventSequences'])>32:self.assertIsNone(chain['originEventSequences'])
            encoded=len(json.dumps(report,indent=2,allow_nan=False).encode('utf-8'))
            metrics.append(dict(size=size,events=len(rows),chains=len(report['chains']),inputAccesses=accesses,
                outputBytes=encoded,inlineSequenceIntegers=inline,sharedHistorySequenceIntegers=shared))
        for small,large in zip(metrics,metrics[1:]):
            self.assertLessEqual(large['inputAccesses'],2.6*small['inputAccesses'])
            self.assertLessEqual(large['outputBytes'],2.6*small['outputBytes'])
        path=Path(os.environ['MONSTER_P0_TEST_OUTPUT'])/('lifecycle-'+kind+'-structural-growth.json')
        path.write_text(json.dumps(dict(scope='Controlled structural regression; no wall-clock performance threshold.',metrics=metrics),indent=2))

    def test_reload_work_access_and_output_growth_stay_near_linear(self):
        self.structural_growth('reload',self.reloads,(20,40,80))

    def test_coalesced_origin_access_and_output_growth_stay_near_linear(self):
        self.structural_growth('coalesced',self.coalesced,(32,64,128))

    def test_small_explicit_analysis_and_output_limits_fail_closed(self):
        for field,limit,rows,observations in (
                ('MAX_ANALYSIS_EVENTS',11,self.events,[]),
                ('MAX_ANALYSIS_OBSERVATIONS',0,self.events,[dict(sceneGeneration=1,dropped=0)]),
                ('MAX_LIFECYCLE_CHAINS',1,self.coalesced(1),[]),
                ('MAX_LIFECYCLE_REPORT_BYTES',100,self.events,[])):
            with self.subTest(bound=field),patch.object(audit,field,limit),self.assertRaisesRegex(ValueError,'bound exceeded'):
                audit.reconcile_lifecycle(copy.deepcopy(rows),observations)
        report={'label':'bounded'}
        exact=len(json.dumps(report,indent=2,allow_nan=False).encode('utf-8'))+1
        with patch.object(audit,'MAX_LIFECYCLE_REPORT_BYTES',exact):
            self.assertIs(audit.bounded_lifecycle_report(report),report)
        with patch.object(audit,'MAX_LIFECYCLE_REPORT_BYTES',exact-1),self.assertRaisesRegex(ValueError,'byte bound exceeded'):
            audit.bounded_lifecycle_report(report)

    def test_capture_raw_unknown_and_aggregate_event_limits_fail_closed(self):
        with tempfile.TemporaryDirectory(dir=os.environ['MONSTER_P0_TEST_OUTPUT']) as tmp:
            root=Path(tmp);raw=root/'harness/raw';raw.mkdir(parents=True)
            (root/'harness/metadata.json').write_text(json.dumps(dict(captureSession='bounded-unit',captureCompleted=True)))
            for mutation,bound in (('raw','MAX_RAW_LIFECYCLE_EVENT_OBSERVATIONS'),
                    ('unknown','MAX_ANALYSIS_EVENTS'),('mixed_aggregate','MAX_ANALYSIS_EVENTS')):
                document=copy.deepcopy(self.document)
                incoming=document['frames'][-1]['diagnosticsP0']['detail']['events'][:4]
                document['frames'][-1]['diagnosticsP0']['detail']['events']=incoming
                if mutation=='unknown':
                    for event in incoming:event['sceneGeneration']=None
                elif mutation=='mixed_aggregate':
                    for event in incoming[2:]:event['sceneGeneration']=None
                (raw/'snapshot.json').write_text(json.dumps(dict(captureSession='bounded-unit',payload=dict(diagnostics=document))))
                with self.subTest(mutation=mutation),patch.object(audit,bound,3),self.assertRaisesRegex(ValueError,'bound exceeded'):
                    audit.audit_lifecycle_capture(root)

    def write_bounded_capture(self, root, groups):
        raw = root/'harness/raw'; raw.mkdir(parents=True)
        (root/'harness/metadata.json').write_text(json.dumps(dict(captureSession='aggregate-unit',captureCompleted=True)))
        for n, rows in enumerate(groups):
            doc = copy.deepcopy(self.document)
            doc['frames'][-1]['diagnosticsP0']['detail']['events'] = copy.deepcopy(rows)
            (raw/f'{n:03}.json').write_text(json.dumps(dict(captureSession='aggregate-unit',payload=dict(diagnostics=doc))))

    def aggregate_rows(self, order):
        known = copy.deepcopy(self.events[0])
        conflict = copy.deepcopy(known); conflict['originPageId'] += 1
        unknown = copy.deepcopy(known); unknown['sceneGeneration'] = None
        return [copy.deepcopy({'K':known,'C':conflict,'U':unknown}[kind]) for kind in order]

    def assert_aggregate_boundary(self, order, known, conflicts, unknown, limit):
        with tempfile.TemporaryDirectory(dir=os.environ['MONSTER_P0_TEST_OUTPUT']) as tmp:
            root = Path(tmp)
            # Separate native-shaped snapshots also exercise cross-input retention.
            self.write_bounded_capture(root, [[row] for row in self.aggregate_rows(order)])
            with patch.object(audit,'MAX_ANALYSIS_EVENTS',limit):
                result = audit.audit_lifecycle_capture(root)
            self.assertEqual(result['uniqueEvents'],known)
            self.assertEqual(len(result['conflictingDuplicates']),conflicts)
            self.assertEqual(len(result['unknownIdentityEvents']),unknown)
            self.assertEqual(result['analysisBounds']['truncated'],False)
            with patch.object(audit,'MAX_ANALYSIS_EVENTS',limit-1), \
                    patch.object(audit,'reconcile_lifecycle',wraps=audit.reconcile_lifecycle) as reconcile, \
                    self.assertRaisesRegex(ValueError,'Lifecycle event bound exceeded'):
                audit.audit_lifecycle_capture(root)
            reconcile.assert_not_called()

    def test_aggregate_known_then_unknown_exact_and_first_excess(self):
        self.assert_aggregate_boundary('KUU',1,0,2,3)

    def test_aggregate_unknown_then_known_exact_and_first_excess(self):
        self.assert_aggregate_boundary('UUK',1,0,2,3)

    def test_aggregate_unknown_interleaved_exact_and_first_excess(self):
        self.assert_aggregate_boundary('UKU',1,0,2,3)

    def test_aggregate_only_unknown_charges_every_identical_observation(self):
        self.assert_aggregate_boundary('UUU',0,0,3,3)

    def test_aggregate_only_known_conflicting_sides_exact_and_first_excess(self):
        self.assert_aggregate_boundary('KCC',1,2,0,3)

    def test_aggregate_every_known_unknown_conflict_branch_order(self):
        for order in itertools.permutations('KCU'):
            with self.subTest(order=''.join(order)):
                self.assert_aggregate_boundary(order,1,1,1,3)

    def test_aggregate_known_identical_duplicates_do_not_charge_retained_rows(self):
        with tempfile.TemporaryDirectory(dir=os.environ['MONSTER_P0_TEST_OUTPUT']) as tmp:
            root = Path(tmp); self.write_bounded_capture(root,[self.aggregate_rows('K'),self.aggregate_rows('KK')])
            with patch.object(audit,'MAX_ANALYSIS_EVENTS',1): result=audit.audit_lifecycle_capture(root)
            self.assertEqual(result['uniqueEvents'],1)
            self.assertEqual(result['duplicateObservationsMerged'],2)
            self.assertEqual(result['conflictingDuplicates'],[])
            self.assertEqual(result['unknownIdentityEvents'],[])

    def test_aggregate_overflow_cli_exit_two_no_output_all_branch_orders(self):
        for order in ('KUU','UUK','UKU','UUU','KCC',*map(''.join,itertools.permutations('KCU'))):
            with self.subTest(order=order),tempfile.TemporaryDirectory(dir=os.environ['MONSTER_P0_TEST_OUTPUT']) as tmp:
                root=Path(tmp); self.write_bounded_capture(root,[[row] for row in self.aggregate_rows(order)])
                output=root/'analysis/result.json'; stderr=io.StringIO()
                with patch.object(audit,'MAX_ANALYSIS_EVENTS',2),contextlib.redirect_stderr(stderr):
                    code=audit.main(['--lifecycle-capture',str(root),'--output',str(output)])
                self.assertEqual(code,2)
                self.assertIn('Lifecycle event bound exceeded',stderr.getvalue())
                self.assertFalse(output.exists())

    def test_aggregate_overflow_cannot_truncate_complete_chain_or_hide_unknown_tail(self):
        with tempfile.TemporaryDirectory(dir=os.environ['MONSTER_P0_TEST_OUTPUT']) as tmp:
            root=Path(tmp); unknown=self.aggregate_rows('U')
            self.write_bounded_capture(root,[self.events,unknown])
            with patch.object(audit,'MAX_ANALYSIS_EVENTS',len(self.events)+1):
                result=audit.audit_lifecycle_capture(root)
            self.assertEqual(result['chainStatusCounts']['complete_retained_chain'],1)
            self.assertEqual(len(result['unknownIdentityEvents']),1)
            output=root/'analysis/result.json'
            with patch.object(audit,'MAX_ANALYSIS_EVENTS',len(self.events)), \
                    contextlib.redirect_stderr(io.StringIO()), \
                    patch.object(audit,'reconcile_lifecycle',wraps=audit.reconcile_lifecycle) as reconcile:
                code=audit.main(['--lifecycle-capture',str(root),'--output',str(output)])
            self.assertEqual(code,2)
            self.assertFalse(output.exists())
            reconcile.assert_not_called()

    def assert_no_successful_latency(self, result):
        for chain in result['chains']:
            self.assertNotEqual(chain['status'],'complete_retained_chain')
            self.assertIsNone(chain['originToSuccessfulConsumptionMilliseconds'])
            self.assertTrue(chain['missingStages'] or chain['ambiguities'])
        for pool in result['poolOnlySelectionLatency']:
            self.assertIsNone(pool['milliseconds'])

    def with_pool_demand(self, rows):
        rows=copy.deepcopy(rows)
        demand=copy.deepcopy(rows[-1]); demand.update(stage='demand',submissionToken=None)
        rows.insert(-1,demand)
        return self.renumber(rows)

    def raw_consumer_result(self, rows):
        with tempfile.TemporaryDirectory(dir=os.environ['MONSTER_P0_TEST_OUTPUT']) as tmp:
            root=Path(tmp); self.write_bounded_capture(root,[rows])
            return audit.audit_lifecycle_capture(root)

    def test_direct_consumer_exact_positive_id_contract_and_pool_semantics(self):
        for field in ('submissionToken','frameId','selectionVersion','residencyVersion',
                'surfaceGeneration','configGeneration','pipelineGeneration','pageLifetime'):
            for value in (None,0,False,True,1.0):
                with self.subTest(field=field,value=value):
                    rows=copy.deepcopy(self.events); rows[-1][field]=value
                    self.assert_no_successful_latency(self.reconcile(self.with_pool_demand(rows)))

    def test_raw_consumer_exact_positive_id_contract_preserves_unavailable_or_rejects(self):
        for field in ('submissionToken','frameId','selectionVersion','residencyVersion',
                'surfaceGeneration','configGeneration','pipelineGeneration','pageLifetime'):
            for value in (None,0,False,True,1.0):
                with self.subTest(field=field,value=value):
                    rows=self.with_pool_demand(self.events); rows[-1][field]=value
                    if type(value) in (bool,float):
                        with self.assertRaisesRegex(ValueError,'Invalid lifecycle numeric field'):
                            self.raw_consumer_result(rows)
                    else:
                        result=self.raw_consumer_result(rows)
                        self.assert_no_successful_latency(result)
                        self.assertEqual(result['retainedEvents'][-1][field],value)

    def test_raw_unknown_consumer_scene_cannot_join_successful_chain(self):
        for value in (None,0,False,True,1.0):
            with self.subTest(value=value):
                rows=copy.deepcopy(self.events);rows[-1]['sceneGeneration']=value
                if type(value) in (bool,float):
                    with self.assertRaisesRegex(ValueError,'Invalid lifecycle numeric field'):
                        self.raw_consumer_result(rows)
                else:
                    result=self.raw_consumer_result(rows)
                    self.assert_no_successful_latency(result)
                    if value is None:self.assertEqual(len(result['unknownIdentityEvents']),1)

    def test_direct_and_raw_earlier_consumer_token_refuses_upstream_and_pool_latency(self):
        rows=self.with_pool_demand(self.events)
        rows[-1]['submissionToken']=next(e['submissionToken'] for e in rows if e['stage']=='upload_submitted')-1
        for consume in (self.reconcile,self.raw_consumer_result):
            with self.subTest(consumer=consume.__name__):
                result=consume(rows); self.assert_no_successful_latency(result)
                self.assertIn('successful_consumer_submission_precedes_last_upload',result['chains'][0]['ambiguities'])
                self.assertTrue(any(p['consumerIssue']=='successful_consumer_submission_precedes_last_upload'
                    for p in result['poolOnlySelectionLatency']))

    def test_direct_and_raw_equal_or_later_independent_consumer_tokens_are_valid(self):
        upload=next(e['submissionToken'] for e in self.events if e['stage']=='upload_submitted')
        for token in (upload,upload+100):
            rows=self.with_pool_demand(self.events); rows[-1]['submissionToken']=token
            for consume in (self.reconcile,self.raw_consumer_result):
                with self.subTest(token=token,consumer=consume.__name__):
                    result=consume(rows)
                    self.assertEqual(result['chains'][0]['status'],'complete_retained_chain')
                    self.assertIsNotNone(result['chains'][0]['originToSuccessfulConsumptionMilliseconds'])
                    self.assertTrue(any(p['milliseconds'] is not None for p in result['poolOnlySelectionLatency']))

    def test_direct_and_raw_each_upload_chunk_lifetime_and_token_are_required(self):
        for stage in ('upload_recorded','upload_submitted','gpu_complete','active_ready'):
            for value in (None,0,999):
                rows=self.with_pool_demand(self.events)
                next(e for e in rows if e['stage']==stage)['pageLifetime']=value
                for consume in (self.reconcile,self.raw_consumer_result):
                    with self.subTest(stage=stage,lifetime=value,consumer=consume.__name__):
                        self.assert_no_successful_latency(consume(rows))
        for value in (None,0,False,True,1.0):
            rows=self.with_pool_demand(self.events)
            extra=copy.deepcopy(next(e for e in rows if e['stage']=='upload_submitted'))
            extra['submissionToken']=value; rows.insert(9,extra);self.renumber(rows)
            self.assert_no_successful_latency(self.reconcile(rows))
            if type(value) in (bool,float):
                with self.assertRaisesRegex(ValueError,'Invalid lifecycle numeric field'):
                    self.raw_consumer_result(rows)
            else:self.assert_no_successful_latency(self.raw_consumer_result(rows))

    def test_direct_and_raw_future_upload_does_not_change_earlier_consumer_cutoff(self):
        rows=copy.deepcopy(self.events)
        future=copy.deepcopy(next(e for e in rows if e['stage']=='upload_submitted'))
        future.update(submissionToken=999,pageLifetime=999)
        rows.append(future);self.renumber(rows)
        for consume in (self.reconcile,self.raw_consumer_result):
            with self.subTest(consumer=consume.__name__):
                result=consume(rows); chain=result['chains'][0]
                self.assertEqual(chain['status'],'complete_retained_chain')
                self.assertEqual(chain['physicalHistoryThroughSequence'],rows[-2]['sequence'])
                self.assertEqual(chain['physicalStagePrefixCounts']['upload_submitted'],1)

    def test_direct_and_raw_invalid_first_use_cannot_borrow_later_valid_consumer(self):
        for value in (None,0):
            rows=copy.deepcopy(self.events)
            later=copy.deepcopy(rows[-1]);rows[-1]['submissionToken']=value;rows.append(later);self.renumber(rows)
            for consume in (self.reconcile,self.raw_consumer_result):
                with self.subTest(value=value,consumer=consume.__name__):
                    result=consume(rows);self.assert_no_successful_latency(result)
                    self.assertIn('missing_successful_first_use',result['chains'][0]['missingStages'])


if __name__ == '__main__': unittest.main()
