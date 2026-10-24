package com.monsterengine.viewer

import android.content.Context
import android.content.ContextWrapper
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID
import java.util.zip.ZipFile

object ViewerMeasurementExportTests {
    fun run(context:Context,test:(String,()->Unit)->Unit) {
        fun isolated(body:(DiagnosticsExporter)->Unit) {
            val root=File(context.cacheDir,"measurement-export-${UUID.randomUUID()}");check(root.mkdirs())
            val wrapper=object:ContextWrapper(context){override fun getApplicationContext():Context=this;override fun getFilesDir():File=File(root,"files").apply{mkdirs()};override fun getCacheDir():File=root}
            try{body(DiagnosticsExporter(wrapper))}finally{check(root.deleteRecursively())}
        }
        val matcher=ViewerMeasurementInstrumentation()
        fun stateFixture(body:(File)->Unit) {
            val root=File(context.cacheDir,"p3-state-contract-${UUID.randomUUID()}");check(root.mkdir())
            try{body(root)}finally{check(root.deleteRecursively())}
        }
        test("p3_state_journal_preserves_exact_noncanonical_bytes_and_actual_absence") {
            for(exists in listOf(false,true))stateFixture { root ->
                val settings=File(root,"viewer-settings.json");val bytes="{ \"mode\" : 0 }\n".toByteArray()
                if(exists)settings.writeBytes(bytes)
                val initial=matcher.stateJournal("observe","contract",null,null,null,root)
                check(initial.getBoolean("exists")==exists)
                val begun=matcher.stateJournal("begin","contract",null,null,null,root)
                val raw=File(root,"p3-state-contract.json").readBytes()
                check(begun.getBoolean("durable") && java.util.Base64.getDecoder().decode(begun.getString("journalBase64")).contentEquals(raw))
                val saved=JSONObject(String(raw)).getJSONObject("state")
                check(java.util.Base64.getDecoder().decode(saved.getString("base64")).contentEquals(if(exists)bytes else byteArrayOf()))
                check(settings.exists()==exists && (!exists || settings.readBytes().contentEquals(bytes)))
            }
        }
        test("p3_state_journal_refuses_atomicfile_backup_new_and_symlink_envelopes") {
            for(suffix in listOf(".bak",".new"))stateFixture { root ->
                val sibling=File(root,"viewer-settings.json$suffix");sibling.writeText("preserve")
                var refused=false;try{matcher.stateJournal("begin","contract",null,null,null,root)}catch(_:Throwable){refused=true}
                check(refused && sibling.readText()=="preserve" && !File(root,"p3-state-contract.json").exists())
            }
            stateFixture { root ->
                val real=File(root,"original").apply{writeText("preserve")}
                java.nio.file.Files.createSymbolicLink(File(root,"viewer-settings.json").toPath(),real.toPath())
                var refused=false;try{matcher.stateJournal("observe","contract",null,null,null,root)}catch(_:Throwable){refused=true};check(refused && real.readText()=="preserve")
            }
        }
        test("p3_state_journal_refuses_oversize_and_duplicate_journal_without_mutation") {
            stateFixture { root ->
                val settings=File(root,"viewer-settings.json");settings.writeBytes(ByteArray(65537){7})
                var refused=false;try{matcher.stateJournal("begin","contract",null,null,null,root)}catch(_:Throwable){refused=true};check(refused && settings.length()==65537L)
            }
            stateFixture { root ->
                matcher.stateJournal("begin","contract",null,null,null,root)
                val journal=File(root,"p3-state-contract.json");val raw=journal.readBytes()
                var refused=false;try{matcher.stateJournal("begin","contract",null,null,null,root)}catch(_:Throwable){refused=true};check(refused && journal.readBytes().contentEquals(raw))
            }
        }
        test("p3_final_restore_never_overwrites_new_user_values_or_deletes_unowned_absent_state") {
            stateFixture { root ->
                val original="{\"mode\":0}".toByteArray();val changed="{\"mode\":1}".toByteArray()
                val file=File(root,"viewer-settings.json").apply{writeBytes(changed)}
                var refused=false;try{matcher.guardedFinalRestore(root,original,original)}catch(_:Throwable){refused=true};check(refused && file.readBytes().contentEquals(changed))
                refused=false;try{matcher.guardedFinalRestore(root,original,changed)}catch(_:Throwable){refused=true};check(refused && file.readBytes().contentEquals(changed))
                refused=false;try{matcher.guardedFinalRestore(root,null,changed)}catch(_:Throwable){refused=true};check(refused && file.readBytes().contentEquals(changed))
            }
        }
        test("p3_final_restore_accepts_only_original_semantics_and_rereads_exact_original_bytes") {
            stateFixture { root ->
                val original="{ \"mode\" : 0 }\n".toByteArray();val canonical="{\"mode\":0.0}".toByteArray()
                val file=File(root,"viewer-settings.json").apply{writeBytes(canonical)}
                val result=matcher.guardedFinalRestore(root,original,original)
                check(file.readBytes().contentEquals(original) && java.util.Base64.getDecoder().decode(result.getString("base64")).contentEquals(original))
                check(!matcher.equivalentSettings("{\"x\":9007199254740992}".toByteArray(),"{\"x\":9007199254740993}".toByteArray()))
                check(!matcher.equivalentSettings("{\"x\":1}".toByteArray(),"{\"x\":\"1\"}".toByteArray()))
            }
        }
        test("p3_recovery_rejects_missing_or_tampered_quiescence_before_state_write") {
            stateFixture { root ->
                val settings=File(root,"viewer-settings.json").apply{writeText("original")}
                for(proof in listOf<ByteArray?>(null,"{}".toByteArray())) {
                    var refused=false;try{matcher.stateJournal("recover","contract","a".repeat(64),proof,"b".repeat(64),root)}catch(_:Throwable){refused=true};check(refused && settings.readText()=="original")
                }
            }
        }

        test("mode_arm_requires_published_full_config_and_derived_identity_ack") {
            val expected=JSONObject().put("schemaVersion",1).put("generation",8).put("configHash","").put("shaderIdentity","b".repeat(64)).put("runtime",JSONObject().put("rasterSharedMemoryMode",1).put("displaySHDegree",3))
            val actual=JSONObject(expected.toString()).put("configHash","a".repeat(64))
            val payload=JSONObject().put("status",JSONObject().put("ok",true)).put("manualRetryRequired",false).put("requestedConfig",actual).put("effectiveConfig",JSONObject(actual.toString()))
            check(matcher.modeConfigAcknowledged(payload,expected))
            for(kind in 0..5) {
                val changed=JSONObject(payload.toString())
                when(kind){0->changed.put("effectiveConfig",JSONObject.NULL);1->changed.getJSONObject("effectiveConfig").put("generation",7);2->changed.getJSONObject("effectiveConfig").getJSONObject("runtime").put("displaySHDegree",2);3->changed.getJSONObject("requestedConfig").put("configHash","c".repeat(64));4->changed.put("manualRetryRequired",true);5->changed.getJSONObject("status").put("ok",false)}
                check(!matcher.modeConfigAcknowledged(changed,expected))
            }
        }
        test("mode_arm_release_requires_real_published_no_session_and_same_surface") {
            val released=JSONObject().put("status",JSONObject().put("ok",true).put("code","scene_released")).put("manualRetryRequired",true).put("effectiveConfig",JSONObject.NULL).put("diagnostics",JSONObject().put("available",false)).put("surfaceGeneration",2)
            check(matcher.modeReleaseAcknowledged(released,2))
            for(kind in 0..4) {val changed=JSONObject(released.toString());when(kind){0->changed.getJSONObject("status").put("code","settings_applied");1->changed.put("effectiveConfig",JSONObject());2->changed.getJSONObject("diagnostics").put("available",true);3->changed.put("surfaceGeneration",3);4->changed.put("manualRetryRequired",false)};check(!matcher.modeReleaseAcknowledged(changed,2))}
        }
        test("mode_arm_existing_controller_boundaries_resolve_exact_runtime_signatures") {
            val klass=ViewerController::class.java
            check(java.util.concurrent.ExecutorService::class.java.isAssignableFrom(klass.getDeclaredField("worker").type))
            for(name in listOf("stopFrames","resetFps","scheduleFrames"))check(klass.getDeclaredMethod(name).returnType==java.lang.Void.TYPE)
            check(klass.getDeclaredMethod("observeSurface",NativeHandle::class.java,java.lang.Long.TYPE,Integer.TYPE).returnType==java.lang.Void.TYPE)
            check(klass.getDeclaredMethod("update",ViewerController.State::class.java).returnType==java.lang.Void.TYPE)
            check(klass.getDeclaredField("sceneIds").type==java.util.concurrent.atomic.AtomicLong::class.java)
            check(klass.getDeclaredField("openAccepted").type==java.lang.Boolean.TYPE)
        }
        fun cameraFixture():Triple<JSONObject,JSONObject,JSONObject> {
            val bytes=java.nio.ByteBuffer.allocate(176).order(java.nio.ByteOrder.LITTLE_ENDIAN)
            bytes.putInt(160,64).putInt(164,64).putInt(168,3)
            val hex=bytes.array().joinToString(""){"%02x".format(it.toInt() and 255)}
            val receipt=JSONObject().put("requestScope","77").put("requestId",8).put("surfaceGeneration",2).put("disposition","queued").put("reset",true)
            val scope=JSONObject().put("sceneGeneration",1).put("surfaceGeneration",2).put("configGeneration",3).put("pipelineGeneration",4)
            val p=JSONObject(scope.toString()).put("version",1).put("status","consumed").put("lineageComplete",true).put("requestScope","77")
                .put("frameId",9).put("submissionToken",10).put("contributorIds",JSONArray(listOf(8))).put("contributorCount",1).put("applicationGroup",7)
                .put("lastResetRequestId",8).put("uniformsEncoding","std140_le_hex176_unused_zero").put("uniformsHex",hex)
                .put("orbit",JSONObject().put("target",JSONArray(listOf(0,0,0))).put("yaw",0).put("pitch",0).put("distance",8).put("external",false))
            val row=JSONObject(scope.toString()).put("frameId",9).put("submissionToken",10).put("status",JSONObject().put("ok",true)).put("renderWidth",64).put("renderHeight",64)
                .put("measurement",JSONObject().put("pipelineGeneration",4).put("cameraX",0).put("cameraY",0).put("cameraZ",0).put("cameraYaw",0).put("cameraPitch",0).put("cameraDistance",8).put("externalCamera",0).put("gpuPipelineStatus",1).put("sortStatus",1).put("sortOverflow",0)).put("counters",JSONObject().put("gpuTimingFrame",9)).put("cameraProvenance",p)
            return Triple(row,receipt,scope)
        }
        test("camera_matcher_exact_consumed_reset_tuple_and_all_member_owner_receipts") {
            val (row,receipt,scope)=cameraFixture();check(matcher.consumedCamera(row,receipt,5,scope))
            val native=JSONObject().put("version",1).put("lost",17).put("receipts",JSONArray().put(JSONObject().put("requestId",8).put("requestScope","77").put("surfaceGeneration",2).put("applicationGroup",7).put("reset",true).put("disposition","applied_motion")))
            check(matcher.nativeAppliedCamera(row,receipt,JSONObject().put("cameraInputReceipts",native)))
        }
        for(status in listOf("unavailable_unsubmitted","unavailable_upload_only","unavailable_legacy","unavailable_scope_change","unavailable_lineage_overflow"))test("camera_matcher_refuses_"+status) {
            val (row,receipt,scope)=cameraFixture();row.getJSONObject("cameraProvenance").put("status",status);check(!matcher.consumedCamera(row,receipt,5,scope))
        }
        test("camera_matcher_refuses_stale_token_rejected_receipt_missing_extension_and_scope_change") {
            for(kind in 0..4) {val (row,receipt,scope)=cameraFixture()
                when(kind){0->row.put("submissionToken",5);1->receipt.put("disposition","jni_rejected");2->row.remove("cameraProvenance");3->scope.put("configGeneration",99);4->receipt.put("surfaceGeneration",99)}
                check(!matcher.consumedCamera(row,receipt,5,scope))
            }
        }
        test("camera_matcher_refuses_missing_reset_sparse_range_duplicate_or_overflow_members") {
            for(kind in 0..3) {val (row,receipt,scope)=cameraFixture();val p=row.getJSONObject("cameraProvenance")
                when(kind){0->p.put("lastResetRequestId",7);1->p.put("contributorIds",JSONArray(listOf(7,9))).put("contributorCount",2);2->p.put("contributorIds",JSONArray(listOf(8,8))).put("contributorCount",2);3->p.put("lineageComplete",false).put("contributorCount",17)}
                check(!matcher.consumedCamera(row,receipt,5,scope))
            }
        }
        test("camera_matcher_refuses_noncanonical_nonfinite_missing_or_zero_dimensions") {
            for(kind in 0..7) {val (row,receipt,scope)=cameraFixture();val p=row.getJSONObject("cameraProvenance")
                val bytes=java.nio.ByteBuffer.wrap(p.getString("uniformsHex").chunked(2).map{it.toInt(16).toByte()}.toByteArray()).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                when(kind){0->bytes.putInt(140,1);1->bytes.putInt(172,1);2->bytes.putFloat(144,Float.NaN);3->{bytes.putInt(160,0);row.put("renderWidth",0)};4->p.getJSONObject("orbit").remove("yaw");5->bytes.putInt(168,4);6->row.getJSONObject("measurement").put("cameraDistance",9);7->row.getJSONObject("measurement").put("cameraX",1)}
                p.put("uniformsHex",bytes.array().joinToString(""){"%02x".format(it.toInt() and 255)})
                check(!matcher.consumedCamera(row,receipt,5,scope))
            }
        }
        test("camera_native_membership_refuses_queued_missing_duplicate_wrong_group_and_unresolved_gpu") {
            for(kind in 0..7) {val (row,receipt,_)=cameraFixture()
                val applied=JSONObject().put("requestId",8).put("requestScope","77").put("surfaceGeneration",2).put("applicationGroup",7).put("reset",true).put("disposition","applied_motion")
                val rows=JSONArray().put(applied);val native=JSONObject().put("version",1).put("lost",17).put("receipts",rows)
                when(kind){0->applied.put("disposition","queued");1->rows.remove(0);2->rows.put(JSONObject(applied.toString()));3->applied.put("applicationGroup",99);4->row.getJSONObject("measurement").put("gpuPipelineStatus",0);5->row.getJSONObject("counters").put("gpuTimingFrame",8);6->row.getJSONObject("measurement").put("sortStatus",0);7->row.getJSONObject("measurement").put("sortOverflow",1)}
                check(!matcher.nativeAppliedCamera(row,receipt,JSONObject().put("cameraInputReceipts",native)))
            }
        }
        test("measurement_export_preserves_zero_null_origin_and_status") {isolated{exporter->
            val measurement=JSONObject().put("measurementSchemaVersion",2).put("pipelineGeneration",Long.MAX_VALUE)
                .put("gpuRenderMilliseconds",0).put("gpuRenderStatus",1).put("gpuScanMilliseconds",JSONObject.NULL).put("gpuScanStatus",2)
                .put("privatePath","/private/piano.ply")
            val source=JSONObject().put("diagnostics",JSONObject().put("schemaVersion",2).put("frames",JSONArray().put(JSONObject().put("frameId",Long.MAX_VALUE).put("measurement",measurement))))
            val zip=exporter.createBundle(source)
            ZipFile(zip).use{z->val result=JSONObject(z.getInputStream(z.getEntry("report.json")).bufferedReader().readText()).getJSONObject("viewer").getJSONObject("diagnostics").getJSONArray("frames").getJSONObject(0)
                check(result.getLong("frameId")==Long.MAX_VALUE)
                val m=result.getJSONObject("measurement");check(m.getLong("pipelineGeneration")==Long.MAX_VALUE);check(m.getDouble("gpuRenderMilliseconds")==0.0);check(m.isNull("gpuScanMilliseconds"));check(m.getInt("gpuScanStatus")==2);check(!m.has("privatePath"))}
        }}
        test("measurement_export_rejects_unknown_schema_and_mistyped_measurement") {isolated{exporter->
            for(wrong in listOf<Any>("2",3,true)) {
                var rejected=false;try{exporter.createBundle(JSONObject().put("measurement",JSONObject().put("measurementSchemaVersion",wrong)))}catch(e:IllegalArgumentException){rejected=true};check(rejected)
            }
            var rejected=false;try{exporter.createBundle(JSONObject().put("measurement",JSONObject().put("ownerWorkMilliseconds","/private/model")))}catch(e:IllegalArgumentException){rejected=true};check(rejected)
        }}
    }
}
