package com.monsterengine.viewer

import android.app.Instrumentation
import android.content.Intent
import android.graphics.Rect
import android.util.Base64
import android.os.Bundle
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.nio.file.Files
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicReference

/** Intentional-only capture runner. Never called by the ordinary contract suite. */
class ViewerMeasurementInstrumentation : Instrumentation() {
    /** Exact completion facts remain authoritative while the periodic ring is stale. */
    private fun screenshotFrame(screenshot:JSONObject,rows:JSONArray):JSONObject {
        val identity=listOf("frameId","submissionToken","sceneGeneration","surfaceGeneration","configGeneration","pipelineGeneration")
        fun positive(value:Any?):Boolean = value is Number && value !is Double && value !is Float && value.toLong()>0L
        for(key in identity)check(positive(screenshot.opt(key))){"Screenshot identity unavailable: $key"}
        val related=(0 until rows.length()).map{rows.getJSONObject(it)}.filter{row ->
            listOf("frameId","sceneGeneration","surfaceGeneration").all{row.opt(it)==screenshot.opt(it)}
        }
        check(related.size<=1){"Ambiguous same screenshot diagnostic frame"}
        val submitted=screenshot.optJSONObject("viewerFrame")
        check(!screenshot.has("viewerFrame") || screenshot.isNull("viewerFrame") || submitted!=null){"Invalid post-submit viewerFrame"}
        val candidates=listOfNotNull(submitted)+related
        check(candidates.isNotEmpty()){"Exact screenshot frame missing from stale diagnostic ring"}
        val orbit=listOf("cameraX","cameraY","cameraZ","cameraYaw","cameraPitch","cameraDistance")
        val presentation=listOf("presentationWidth","presentationHeight","displayWidth","displayHeight","presentationFormat","presentationColorSpace","presentationFormatSRGB","presentationDisplayScale","presentationSharpeningStrength")
        fun equivalent(a:Any?,b:Any?):Boolean = when {
            a is JSONObject && b is JSONObject -> a.keys().asSequence().toSet()==b.keys().asSequence().toSet() && a.keys().asSequence().all{equivalent(a.opt(it),b.opt(it))}
            a is Number && b is Number -> a.toDouble()==b.toDouble()
            else -> a==b
        }
        for(row in candidates) {
            val m=row.getJSONObject("measurement")
            for(key in identity) {
                val actual=if(key=="pipelineGeneration")m.opt(key) else row.opt(key)
                check(positive(actual) && equivalent(actual,screenshot.opt(key))){"Screenshot diagnostic identity mismatch: $key"}
            }
            check(row.optInt("schemaVersion")==2 && m.optInt("measurementSchemaVersion")==2 && row.optJSONObject("status")?.opt("ok")==true){"Observed diagnostic frame status/schema unsuccessful"}
            for((axis,field) in listOf("width" to "renderWidth","height" to "renderHeight"))check(positive(screenshot.opt(axis)) && equivalent(row.opt(field),screenshot.opt(axis)) && equivalent(m.opt(field),screenshot.opt(axis))){"Same-frame screenshot/render dimensions mismatch"}
            for(key in orbit)check((m.opt(key) as? Number)?.toDouble()?.isFinite()==true){"Observed camera unavailable: $key"}
            check(m.opt("externalCamera")==false || (m.opt("externalCamera") as? Number)?.toInt()==0){"External camera orientation unavailable"}
            for(key in listOf("contentHash","configHash","shaderIdentity")) {
                val expected=screenshot.optString(key)
                val actual=if(key=="contentHash")row.optString(key) else row.getJSONObject("effectiveConfig").optString(key)
                check(expected.matches(Regex("[0-9a-f]{64}")) && actual==expected){"Screenshot source/config/shader mismatch: $key"}
            }
            check(equivalent(row.opt("buildIdentity"),screenshot.opt("buildIdentity"))){"Screenshot build identity mismatch"}
            check(row.isNull("buildIdentity") || row.optString("buildIdentity").matches(Regex("[0-9a-f]{64}"))){"Invalid native build identity"}
        }
        if(candidates.size==2) {
            val a=candidates[0];val b=candidates[1]
            for(key in orbit+listOf("pipelineGeneration","externalCamera"))check(equivalent(a.getJSONObject("measurement").opt(key),b.getJSONObject("measurement").opt(key))){"Submitted screenshot/ring camera mismatch: $key"}
            for(key in presentation+"effectiveConfig")check(equivalent(a.opt(key),b.opt(key))){"Submitted screenshot/ring config/presentation mismatch: $key"}
        }
        return candidates.first()
    }
    /** Strict consumer of the native successful-compute extension, never latest-row inference. */
    internal fun consumedCamera(row:JSONObject,receipt:JSONObject,beforeToken:Long,scope:JSONObject):Boolean {
        val p=row.optJSONObject("cameraProvenance") ?: return false
        if(p.optInt("version")!=1 || p.optString("status")!="consumed" || p.opt("lineageComplete")!=true)return false
        if(row.optJSONObject("status")?.opt("ok")!=true || row.optLong("frameId")<=0 || row.optLong("submissionToken")<=beforeToken)return false
        if(p.optLong("frameId")!=row.optLong("frameId") || p.optLong("submissionToken")!=row.optLong("submissionToken"))return false
        for(key in listOf("sceneGeneration","surfaceGeneration","configGeneration","pipelineGeneration")) {
            val actual=if(key=="pipelineGeneration")row.getJSONObject("measurement").optLong(key) else row.optLong(key)
            if(actual<=0 || actual!=scope.optLong(key) || p.optLong(key)!=actual)return false
        }
        if(!receipt.optString("requestScope").matches(Regex("[1-9][0-9]{0,19}")) || receipt.optLong("surfaceGeneration")!=scope.optLong("surfaceGeneration"))return false
        if(p.optString("requestScope")!=receipt.optString("requestScope") || receipt.optString("disposition")!="queued" || receipt.optLong("requestId")<=0)return false
        val ids=p.optJSONArray("contributorIds") ?: return false
        if(ids.length()>16 || p.optInt("contributorCount")!=ids.length())return false
        val exact=(0 until ids.length()).map{ids.optLong(it)}
        if(exact.any{it<=0} || exact.distinct().size!=exact.size || receipt.getLong("requestId") !in exact)return false
        if(receipt.optBoolean("reset") && p.optLong("lastResetRequestId")!=receipt.getLong("requestId"))return false
        if(p.optString("uniformsEncoding")!="std140_le_hex176_unused_zero" || !p.optString("uniformsHex").matches(Regex("[0-9a-f]{352}")))return false
        val bytes=p.getString("uniformsHex").chunked(2).map{it.toInt(16).toByte()}.toByteArray()
        if((140..143).any{bytes[it].toInt()!=0} || (172..175).any{bytes[it].toInt()!=0})return false
        val uniforms=java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        if((0..39).any{!uniforms.getFloat(it*4).isFinite()})return false
        if(uniforms.getInt(160)<=0 || uniforms.getInt(164)<=0 || uniforms.getInt(160)!=row.optInt("renderWidth") || uniforms.getInt(164)!=row.optInt("renderHeight") || uniforms.getInt(168) !in 0..3)return false
        val orbit=p.optJSONObject("orbit") ?: return false
        val target=orbit.optJSONArray("target") ?: return false
        if(target.length()!=3 || (0..2).any{(target.opt(it) as? Number)?.toDouble()?.isFinite()!=true})return false
        if(listOf("yaw","pitch","distance").any{(orbit.opt(it) as? Number)?.toDouble()?.isFinite()!=true} || orbit.opt("external") !is Boolean || orbit.getDouble("distance")<=0)return false
        val measurement=row.getJSONObject("measurement")
        for((index,key) in listOf("cameraX","cameraY","cameraZ").withIndex())if(uniforms.getFloat(128+index*4).toDouble()!=measurement.optDouble(key,Double.NaN))return false
        for((key,field) in listOf("yaw" to "cameraYaw","pitch" to "cameraPitch","distance" to "cameraDistance"))if(orbit.getDouble(key)!=measurement.optDouble(field,Double.NaN))return false
        if(measurement.optInt("externalCamera",-1)!=(if(orbit.getBoolean("external"))1 else 0))return false
        return true
    }
    internal fun nativeAppliedCamera(row:JSONObject,receipt:JSONObject,payload:JSONObject):Boolean {
        val ring=payload.optJSONObject("cameraInputReceipts") ?: return false
        if(ring.optInt("version")!=1 || ring.optLong("lost",-1)<0)return false
        val receipts=ring.optJSONArray("receipts") ?: return false
        val p=row.getJSONObject("cameraProvenance");val ids=p.getJSONArray("contributorIds")
        if(ids.length()==0 || p.optLong("applicationGroup")<=0)return false
        for(index in 0 until ids.length()) {
            val id=ids.getLong(index)
            val exact=(0 until receipts.length()).map{receipts.getJSONObject(it)}.filter{it.optLong("requestId")==id && it.optString("requestScope")==p.optString("requestScope")}
            if(exact.size!=1)return false
            val r=exact.single()
            if(r.optLong("surfaceGeneration")!=p.optLong("surfaceGeneration") || r.optLong("applicationGroup")!=p.optLong("applicationGroup") || r.optString("disposition") !in listOf("applied_motion","applied_no_motion"))return false
            if(receipt.optBoolean("reset") && id==receipt.optLong("requestId") && (!r.optBoolean("reset") || r.optString("disposition")!="applied_motion"))return false
        }
        val m=row.getJSONObject("measurement")
        return m.optInt("gpuPipelineStatus")==1 && row.getJSONObject("counters").optLong("gpuTimingFrame")==row.getLong("frameId") && m.optInt("sortStatus",-1)==1 && m.optLong("sortOverflow",-1)==0L
    }
    private fun comparableConfig(value:Any?):Any?=when(value) {
        null,JSONObject.NULL->null
        is JSONObject->value.keys().asSequence().filter{it!="configHash"}.toList().sorted().associateWith{comparableConfig(value.get(it))}
        is JSONArray->(0 until value.length()).map{comparableConfig(value.get(it))}
        is Number->java.math.BigDecimal(value.toString()).stripTrailingZeros()
        else->value
    }
    internal fun modeConfigAcknowledged(payload:JSONObject,expected:JSONObject):Boolean {
        val requested=payload.optJSONObject("requestedConfig") ?: return false
        val effective=payload.optJSONObject("effectiveConfig") ?: return false
        if(payload.optJSONObject("status")?.opt("ok")!=true || payload.optBoolean("manualRetryRequired"))return false
        if(requested.optLong("generation")!=expected.optLong("generation") || effective.optLong("generation")!=expected.optLong("generation"))return false
        if(!requested.optString("configHash").matches(Regex("[0-9a-f]{64}")) || requested.optString("configHash")!=effective.optString("configHash"))return false
        return comparableConfig(requested)==comparableConfig(expected) && comparableConfig(effective)==comparableConfig(expected)
    }
    internal fun modeReleaseAcknowledged(payload:JSONObject,surface:Long):Boolean =
        payload.optJSONObject("status")?.opt("ok")==true && payload.getJSONObject("status").optString("code")=="scene_released" &&
        payload.optBoolean("manualRetryRequired") && payload.isNull("effectiveConfig") && payload.optJSONObject("diagnostics")?.opt("available")==false && payload.optLong("surfaceGeneration")==surface


private fun sha(bytes:ByteArray)=MessageDigest.getInstance("SHA-256").digest(bytes).joinToString(""){"%02x".format(it.toInt() and 255)}
private val stateOperations=JSONArray()
private fun syncWrite(file:File, bytes:ByteArray) {
    check(!file.exists() || (!Files.isSymbolicLink(file.toPath()) && file.isFile))
    val parent=file.parentFile.canonicalFile
    check(file.parentFile.canonicalFile==parent && !Files.isSymbolicLink(parent.toPath()))
    val temporary=File(parent,".${file.name}.p3-${java.util.UUID.randomUUID()}.tmp")
    check(temporary.createNewFile())
    // A failure retains the exclusive temporary and durable journal for diagnosis.
    FileOutputStream(temporary).use { it.write(bytes);it.fd.sync() }
    stateOperations.put(JSONObject().put("operation","file_fsync").put("path",temporary.absolutePath).put("bytes",bytes.size).put("sha256",sha(bytes)))
    check(temporary.readBytes().contentEquals(bytes))
    android.system.Os.rename(temporary.absolutePath,file.absolutePath)
    stateOperations.put(JSONObject().put("operation","atomic_rename").put("from",temporary.absolutePath).put("to",file.absolutePath))
    val directory=android.system.Os.open(parent.absolutePath,android.system.OsConstants.O_RDONLY or android.system.OsConstants.O_NOFOLLOW,0)
    try {check(android.system.OsConstants.S_ISDIR(android.system.Os.fstat(directory).st_mode));android.system.Os.fsync(directory)}finally {android.system.Os.close(directory)}
    stateOperations.put(JSONObject().put("operation","parent_fsync").put("path",parent.absolutePath))
    check(file.readBytes().contentEquals(bytes))
}
internal fun stateJournal(action:String, session:String, expectedJournalSHA256:String?, quiescenceWire:ByteArray?, expectedQuiescenceSHA256:String?, testRoot:File?=null):JSONObject {
    require(session.matches(Regex("[A-Za-z0-9_-]{1,96}")))
    val root=(testRoot ?: targetContext.filesDir).canonicalFile
    check(!Files.isSymbolicLink(root.toPath()) && root.isDirectory)
    val state=File(root,"viewer-settings.json")
    val journal=File(root,"p3-state-$session.json")
    check(!Files.isSymbolicLink(state.toPath()) && !Files.isSymbolicLink(journal.toPath()))
    fun envelope() {
        for(suffix in listOf(".bak",".new")) {
            val sibling=File(root,"viewer-settings.json$suffix")
            check(!Files.isSymbolicLink(sibling.toPath()) && !sibling.exists()) {"AtomicFile envelope requires separate reconciliation: $suffix"}
        }
    }
    fun observation():JSONObject {
        envelope()
        check(!state.exists() || (state.isFile && state.length() in 0L..65536L))
        val bytes=if(state.exists())state.readBytes()else byteArrayOf()
        return JSONObject().put("exists",state.exists()).put("bytes",bytes.size)
            .put("sha256",sha(bytes)).put("base64",Base64.encodeToString(bytes,Base64.NO_WRAP))
            .put("statePath",state.canonicalPath).put("atomicEnvelope",JSONArray(listOf(".bak",".new").map{suffix->val file=File(root,state.name+suffix);JSONObject().put("path",file.absolutePath).put("exists",file.exists()).put("symbolicLink",Files.isSymbolicLink(file.toPath()))}))
    }
    if(action=="observe")return observation()
    if(action=="begin") {
        check(!journal.exists()) {"Existing recovery journal must be reconciled first"}
        val record=JSONObject().put("schemaVersion",1).put("session",session).put("state",observation())
        syncWrite(journal,record.toString().toByteArray(Charsets.UTF_8))
        return record.put("durable",true).put("journalSHA256",sha(journal.readBytes())).put("journalBase64",Base64.encodeToString(journal.readBytes(),Base64.NO_WRAP))
    }
    require(action=="recover")
    // Wrapper receipt is host-hash-bound before invocation, containing raw force-stop
    // and pidof output/exits, not merely a claimed quiescent boolean. This new
    // helper process must begin strictly after the actual old-PID absence probe.
    val proofWire=checkNotNull(quiescenceWire)
    require(proofWire.size in 1..65536 && expectedQuiescenceSHA256?.matches(Regex("[0-9a-f]{64}"))==true)
    check(sha(proofWire)==expectedQuiescenceSHA256)
    val proof=JSONObject(String(proofWire,Charsets.UTF_8))
    check(proof.getString("session")==session && proof.getString("package")==targetContext.packageName)
    check(proof.getInt("forceStopExitCode")==0 && proof.getInt("pidProbeExitCode") in 0..1)
    check(proof.getString("pidProbeStdout").trim().isEmpty())
    check(proof.getLong("pidAbsentElapsedRealtimeMillis") < android.os.Process.getStartElapsedRealtime())
    check(proof.getLong("forceStopElapsedRealtimeMillis") <= proof.getLong("pidAbsentElapsedRealtimeMillis"))
    check(proof.getString("rawCommandsSHA256").matches(Regex("[0-9a-f]{64}")))
    envelope()
    val current=observation()
    check(current.getBoolean("exists")==proof.getBoolean("currentSettingsExists") &&
        current.getString("sha256")==proof.getString("currentSettingsSHA256")) {"Settings changed since registered recovery read"}
    require(expectedJournalSHA256?.matches(Regex("[0-9a-f]{64}"))==true)
    check(journal.isFile && journal.length() in 1L..1048576L)
    val journalBytes=journal.readBytes()
    check(sha(journalBytes)==expectedJournalSHA256) {"Unregistered or tampered recovery journal"}
    val record=JSONObject(String(journalBytes,Charsets.UTF_8))
    check(record.getInt("schemaVersion")==1 && record.getString("session")==session)
    val prior=record.getJSONObject("state")
    val bytes=Base64.decode(prior.getString("base64"),Base64.NO_WRAP)
    check(bytes.size in 0..65536 && bytes.size==prior.getInt("bytes") && sha(bytes)==prior.getString("sha256"))
    check(prior.getBoolean("exists") || bytes.isEmpty())
    if(prior.getBoolean("exists"))syncWrite(state,bytes)
    else {
        check(!state.exists() || state.delete())
        val directory=android.system.Os.open(root.absolutePath,android.system.OsConstants.O_RDONLY or android.system.OsConstants.O_NOFOLLOW,0)
        try {check(android.system.OsConstants.S_ISDIR(android.system.Os.fstat(directory).st_mode));android.system.Os.fsync(directory)}finally {android.system.Os.close(directory)}
    }
    if(!prior.getBoolean("exists"))stateOperations.put(JSONObject().put("operation","absence_parent_fsync").put("path",root.absolutePath))
    val observed=observation()
    check(observed.getBoolean("exists")==prior.getBoolean("exists") &&
        observed.getInt("bytes")==prior.getInt("bytes") &&
        observed.getString("sha256")==prior.getString("sha256") &&
        Base64.decode(observed.getString("base64"),Base64.NO_WRAP).contentEquals(bytes)) {"Recovery actual-byte mismatch"}
    return JSONObject().put("status","bytes_restored").put("state",observed)
}


    /** Runs before any Activity/controller access, including normal settings reads. */
    private fun earlyStateAction():Boolean {
        val action=args.getString("stateAction") ?: return false
        val receipt=JSONObject().put("schemaVersion",1).put("action",action)
            .put("package",targetContext.packageName).put("pid",android.os.Process.myPid())
            .put("helperProcessStartElapsedRealtimeMillis",android.os.Process.getStartElapsedRealtime())
            .put("beginElapsedRealtimeMillis",SystemClock.elapsedRealtime())
            .put("activityCreated",false).put("controllerCreated",false)
        var code=android.app.Activity.RESULT_CANCELED
        var output:File?=null
        try {
            require(action in listOf("observe","begin","recover"))
            val session=checkNotNull(args.getString("captureSession"))
            require(session.matches(Regex("[A-Za-z0-9_-]{1,96}")))
            receipt.put("session",session)
            val parent=checkNotNull(targetContext.getExternalFilesDir(null)).canonicalFile
            val invocation=checkNotNull(args.getString("stateInvocation"))
            require(invocation.matches(Regex("[A-Za-z0-9_-]{1,96}")))
            receipt.put("invocation",invocation)
            val dir=File(parent,"$session-state-$action-$invocation")
            check(!Files.isSymbolicLink(dir.toPath()) && !dir.exists() && dir.mkdir())
            output=File(dir,"state-action.json")
            val encoded=args.getString("quiescenceBase64")
            require(encoded==null || encoded.length<=87384)
            val wire=encoded?.let { android.util.Base64.decode(it,android.util.Base64.NO_WRAP) }
            require(wire==null || wire.size in 1..65536)
            val result=stateJournal(action,session,args.getString("expectedJournalSHA256"),wire,args.getString("expectedQuiescenceSHA256"))
            receipt.put("result",result).put("operations",JSONArray(stateOperations.toString())).put("status","completed")
            code=android.app.Activity.RESULT_OK
        }catch(error:Throwable){receipt.put("status","failed").put("failure",error.toString()).put("operations",stateOperations)}
        receipt.put("endElapsedRealtimeMillis",SystemClock.elapsedRealtime())
        try { checkNotNull(output);syncWrite(output,receipt.toString(2).toByteArray(Charsets.UTF_8)) }
        catch(error:Throwable){code=android.app.Activity.RESULT_CANCELED;receipt.put("receiptWriteFailure",error.toString())}
        finish(code,Bundle().apply{putString("stream",receipt.toString()+"\n")})
        return true
    }

    internal fun equivalentSettings(a:ByteArray,b:ByteArray):Boolean {
        fun semantic(value:Any?):Any?=when(value){
            null,JSONObject.NULL->null
            is JSONObject->value.keys().asSequence().toList().sorted().associateWith{semantic(value.get(it))}
            is JSONArray->(0 until value.length()).map{semantic(value.get(it))}
            is Number->java.math.BigDecimal(value.toString()).stripTrailingZeros()
            else->value
        }
        return try{semantic(JSONObject(String(a,Charsets.UTF_8)))==semantic(JSONObject(String(b,Charsets.UTF_8)))}catch(_:Throwable){a.contentEquals(b)}
    }
    internal fun guardedFinalRestore(root:File,original:ByteArray?,expectedCurrent:ByteArray?):JSONObject {
        val state=File(root,"viewer-settings.json")
        stateJournal("observe","final-guard",null,null,null,root)
        check(original!=null && expectedCurrent!=null && equivalentSettings(expectedCurrent,original)){"No original semantic envelope; preserving actual state"}
        check(state.isFile && equivalentSettings(state.readBytes(),expectedCurrent)){"Unexpected current user settings; preserving actual state"}
        if(original!=null)syncWrite(state,original)
        else {
            check(state.delete())
            val directory=android.system.Os.open(root.absolutePath,android.system.OsConstants.O_RDONLY or android.system.OsConstants.O_NOFOLLOW,0)
            try{check(android.system.OsConstants.S_ISDIR(android.system.Os.fstat(directory).st_mode));android.system.Os.fsync(directory)}finally{android.system.Os.close(directory)}
            stateOperations.put(JSONObject().put("operation","absence_parent_fsync").put("path",root.absolutePath))
        }
        val observed=stateJournal("observe","final-guard",null,null,null,root)
        check(observed.getBoolean("exists")== (original!=null) && Base64.decode(observed.getString("base64"),Base64.NO_WRAP).contentEquals(original ?: byteArrayOf()))
        return observed
    }
    private lateinit var args: Bundle
    override fun onCreate(arguments: Bundle?) { args=arguments ?: Bundle();super.onCreate(arguments);start() }
    override fun onStart() {
        if(earlyStateAction())return
        var activity:MainActivity?=null
        val settings=File(targetContext.filesDir,"viewer-settings.json")
        check(!Files.isSymbolicLink(settings.toPath()) && (!settings.exists() || (settings.isFile && settings.length() in 0L..65536L)))
        for(suffix in listOf(".bak",".new")){val envelope=File(settings.parentFile,settings.name+suffix);check(!Files.isSymbolicLink(envelope.toPath()) && !envelope.exists()){"AtomicFile envelope requires reconciliation"}}
        val original=settings.takeIf{it.isFile}?.readBytes()
        val session=args.getString("captureSession") ?: "measurement-${System.currentTimeMillis()}"
        val profilingMode=args.getString("profilingMode") ?: "off"
        require(profilingMode=="off" || profilingMode=="diagnostic")
        require(session.matches(Regex("[A-Za-z0-9_-]{1,96}")))
        val output=File(checkNotNull(targetContext.getExternalFilesDir(null)),session).apply{check(!exists());check(mkdirs())}
        val durationMillis=(args.getString("durationSeconds") ?: "12").toLong()*1000L
        val roundIndex=(args.getString("roundIndex") ?: "1").toInt()
        val purpose=args.getString("capturePurpose") ?: "diagnostic"
        val runtimeModeArm=args.getString("runtimeModeArm")?.toInt()
        val restorationOnly=args.getString("restorationOnly")=="true"
        val p3Fixed=args.getString("p3FixedSchedule")=="true"
        val p3ReferenceOrdinal=args.getString("p3ReferenceOrdinal")?.toInt()
        val p3ReferenceScenario=args.getString("p3ReferenceScenario")
        require(!p3Fixed || (durationMillis==6000L && runtimeModeArm==null && args.getString("candidateRegistrationHash")?.matches(Regex("[0-9a-f]{64}"))==true))
        require(p3ReferenceOrdinal==null || (p3Fixed && purpose=="quality" && p3ReferenceOrdinal in listOf(5,25,45) && p3ReferenceScenario in listOf("zoom","repeatzoom")))
        require(runtimeModeArm==null || runtimeModeArm in 0..1)
        require(runtimeModeArm==null || (purpose=="diagnostic" && profilingMode=="off" && durationMillis==6000L && args.getString("candidateRegistrationHash")?.matches(Regex("[0-9a-f]{64}"))==true))
        require(!restorationOnly || (runtimeModeArm==null && purpose=="diagnostic" && profilingMode=="off"))
        require(!restorationOnly || args.getString("originalSettingsSHA256")?.matches(Regex("[0-9a-f]{64}"))==true)
        val boundedP0=args.getString("boundedP0")=="true"
        val referenceWaitSeconds=(args.getString("referenceWaitSeconds") ?: "3").toLong()
        val scenarioNames=(args.getString("scenarios") ?: "zoom,repeatzoom,stationary,rotation").split(',')
        val segmented=args.getString("rawLayout")!="flat"
        val continuousMotion=!(purpose=="diagnostic" && durationMillis<=12000L)
        require(durationMillis in 6000L..60000L && roundIndex in 1..3)
        require(purpose in listOf("performance","diagnostic","quality"))
        require(purpose!="performance" || durationMillis==60000L)
        require(purpose!="quality" || profilingMode=="off")
        require(referenceWaitSeconds in 0L..15L)
        require(!boundedP0 || (purpose!="performance" && durationMillis<=12000L))
        require(scenarioNames.isNotEmpty() && scenarioNames.distinct()==scenarioNames && scenarioNames.all{it in listOf("stationary","rotation","zoom","repeatzoom")})
        require("repeatzoom" !in scenarioNames || ("zoom" in scenarioNames && scenarioNames.indexOf("zoom")<scenarioNames.indexOf("repeatzoom")))
        val rawRoot=File(output,"raw").apply{check(mkdir())}
        var raw=if(segmented)File(rawRoot,"preparation").apply{check(mkdir())}else rawRoot
        val profileRaw=File(output,"profiling").apply{check(mkdir())}
        var rawBytes=0L;var rawRows=0;var rawFiles=0;var profileBytes=0L
        val events=JSONArray();val windows=JSONArray();var chunks=0
        var controller:ViewerController?=null
        fun nativeHandle():NativeHandle {
            val owner=ViewerController::class.java.getDeclaredField("nativeOwner").apply{isAccessible=true}.get(checkNotNull(controller)) as AtomicReference<*>
            return owner.get() as NativeHandle
        }
        fun ui(body:()->Unit) {var failure:Throwable?=null;runOnMainSync{try{body()}catch(e:Throwable){failure=e}};failure?.let{throw it}}
        fun views(v:View):List<View> = listOf(v)+if(v is ViewGroup)(0 until v.childCount).flatMap{views(v.getChildAt(it))}else emptyList()
        var originalProfilingEnabled:Boolean?=null
        var createdPrivateExports:File?=null
        var restoreRuntimeMode:(()->Unit)?=null
        var samplerService:java.util.concurrent.ExecutorService?=null
        var expectedSettingsDuringCapture:ByteArray?=null
        var receiptHookRestore:(()->Unit)?=null
        var receiptBuffer:Array<CameraInputReceipt?>?=null
        var receiptInputs:Array<CameraInputState?>?=null
        var receiptEventOrdinals:LongArray?=null
        val activeEventOrdinal=java.util.concurrent.atomic.AtomicLong(-1)
        var receiptTimes:LongArray?=null
        var receiptBeginTimes:LongArray?=null
        var receiptMotion:BooleanArray?=null
        var receiptCount=0
        var receiptLease:BudgetCoordinator.Reservation?=null
        var receiptOverflow=false
        var receiptHookMissing=false
        var receiptHookActive=false
        fun receiptAt(index:Int):JSONObject {
            val receipt=checkNotNull(receiptBuffer!![index]);val input=checkNotNull(receiptInputs!![index])
            return receipt.toJson().put("requestBeginOwnerNanoseconds",receiptBeginTimes!![index]).put("observedOwnerNanoseconds",receiptTimes!![index]).put("requestedMotion",receiptMotion!![index])
                .put("eventOrdinal",receiptEventOrdinals!![index]).put("inputPayload",input.copy(requestId=receipt.requestId,requestScope=receipt.requestScope).toJson())
                .put("inputFloat32Bits",JSONObject().put("orbitX",input.orbitX.toRawBits()).put("orbitY",input.orbitY.toRawBits()).put("panX",input.panX.toRawBits()).put("panY",input.panY.toRawBits()).put("pinchScale",input.pinchScale.toRawBits()))
        }
        var latestSnapshotPayload:JSONObject?=null
        fun snapshot(allowAbsent:Boolean=false):JSONObject {
            val c=checkNotNull(controller)
            val owner=ViewerController::class.java.getDeclaredField("nativeOwner").apply{isAccessible=true}.get(c) as AtomicReference<*>
            val observedStart=System.nanoTime()
            val result=NativeBridge.snapshot(owner.get() as NativeHandle,JSONObject().put("schemaVersion",1).put("kind","diagnostics").put("includePrivatePaths",false))
            val observedEnd=System.nanoTime()
            check(result.ok){result.toString()}
            val payload=JSONObject(result.payload.toString())
            if(originalProfilingEnabled==null)originalProfilingEnabled=payload.getBoolean("profilingEnabled")
            // Keep the exact native profiling JSON separately; do not duplicate a 16384-event
            // ring in every measurement input or increase the offline consumer's limits.
            if(segmented && payload.has("profilingRawJSON")) {
                val profile=payload.getString("profilingRawJSON").toByteArray(Charsets.UTF_8)
                check(profile.size<=16*1024*1024 && profileBytes+profile.size<=1024L*1024*1024){"Profiling evidence byte bound exceeded"}
                val file=File(profileRaw,"profiling-${chunks.toString().padStart(4,'0')}.json")
                file.writeBytes(profile);profileBytes+=profile.size
                payload.remove("profilingRawJSON")
                payload.put("profilingRawJSONFile","profiling/${file.name}")
            }
            val document=JSONObject().put("payload",payload).put("captureSequence",chunks)
                .put("captureObservationOwnerNanoseconds",observedStart).put("captureObservationEndOwnerNanoseconds",observedEnd)
                .put("captureSession",session)
            val bytes=document.toString().toByteArray(Charsets.UTF_8)
            val rows=payload.getJSONObject("diagnostics").optJSONArray("frames")?.length() ?: 0
            check(bytes.size<=16*1024*1024 && rawBytes+bytes.size<=256L*1024*1024 && rawFiles<512 && rawRows+rows<=250000){"Measurement input bound exceeded"}
            File(raw,"chunk-${(chunks++).toString().padStart(4,'0')}.json").writeBytes(bytes)
            rawBytes+=bytes.size;rawRows+=rows;rawFiles++
            latestSnapshotPayload=payload
            return if(allowAbsent)payload else payload.getJSONObject("diagnostics").getJSONObject("latest")
        }
        fun event(action:Int,points:List<Pair<Float,Float>>,down:Long) {
            val properties=Array(points.size){index->MotionEvent.PointerProperties().apply{id=index;toolType=MotionEvent.TOOL_TYPE_FINGER}}
            val coords=Array(points.size){index->MotionEvent.PointerCoords().apply{x=points[index].first;y=points[index].second;pressure=1f;size=1f}}
            val now=SystemClock.uptimeMillis()
            val e=MotionEvent.obtain(down,now,action,points.size,properties,coords,0,0,1f,1f,0,0,android.view.InputDevice.SOURCE_TOUCHSCREEN,0)
            val requestBegin=System.nanoTime();val ordinal=events.length();activeEventOrdinal.set(ordinal.toLong())
            try{sendPointerSync(e);waitForIdleSync()}finally{e.recycle();activeEventOrdinal.set(-1)}
            events.put(JSONObject().put("eventOrdinal",ordinal).put("requestBeginOwnerNanoseconds",requestBegin).put("ownerNanoseconds",System.nanoTime()).put("uptimeMillis",now).put("elapsedRealtimeNanos",SystemClock.elapsedRealtimeNanos()).put("action",action).put("points",JSONArray(points.map{JSONArray(listOf(it.first,it.second))})))
        }
        var resultCode=android.app.Activity.RESULT_CANCELED
        var resultMessage="Capture failed"
        val metadata=JSONObject().put("schemaVersion",1).put("capturePID",android.os.Process.myPid())
            .put("captureProcessStartElapsedRealtimeMillis",android.os.Process.getStartElapsedRealtime())
            .put("captureBeginElapsedRealtimeMillis",SystemClock.elapsedRealtime()).put("captureBeginOwnerNanoseconds",System.nanoTime()).put("captureSession",session).put("scenarios",windows).put("gestures",events)
            .put("profilingMode",profilingMode).put("captureCompleted",false).put("roundIndex",roundIndex)
            .put("capturePurpose",purpose).put("requestedDurationNanoseconds",durationMillis*1000000L)
            .put("boundedP0",boundedP0).put("referenceWaitSeconds",referenceWaitSeconds)
            .put("selectedScenarios",JSONArray(scenarioNames)).put("acceptanceEligibleDuration",durationMillis==60000L && purpose=="performance")
            .put("scheduleId",if(continuousMotion)"triangular-original-speed-v1" else "legacy-forward6-idle-v1")
            .put("actionSchedule",if(continuousMotion)"One held pointer gesture per motion window; original six-second forward path/speed followed by mirrored six-second return, repeated continuously. Reset only before each scenario; no camera teleports. First zoom window includes first and subsequent traversals." else "Legacy short diagnostic: original six-second forward gesture followed by held-pointer idle until window end.")
            .put("cacheState","Existing process caches retained; each scenario resets camera only; no cold-cache claim")
            .put("lifecycleContract","native-detail-v2; immutable upstream/work/slot namespaces; no pool-id substitution")
            .put("lifecycleCoverage","Native pending queue 256, generation registry 64, deltas 16/frame; history before capture and after final export unavailable; profiling-off emits no new detail events")
            .put("clockMapping","Native owner is C++ steady_clock CLOCK_MONOTONIC; gesture uptimeMillis CLOCK_MONOTONIC; elapsedRealtime includes suspend. Offset observed below, never equate elapsedRealtime and owner directly.")
        try {
            if(p3Fixed || restorationOnly)check(original!=null && args.getString("settingsBase64")==null){"P3 capture requires existing original settings; absence preserved"}
            args.getString("settingsBase64")?.let {
                check(original==null){"Profile has existing settings; refusing overwrite"}
                val bytes=android.util.Base64.decode(it,android.util.Base64.DEFAULT)
                check(bytes.size in 1..65536);check(JSONObject(String(bytes)).getInt("schemaVersion")==1)
                settings.writeBytes(bytes)
            }
            val inputSettings=check(settings.isFile).let{settings.readBytes()}
            check(original!=null && equivalentSettings(inputSettings,original)){"Startup changed original semantic envelope; preserving actual state"}
            expectedSettingsDuringCapture=original.copyOf()
            if(restorationOnly)check(MessageDigest.getInstance("SHA-256").digest(inputSettings).joinToString(""){"%02x".format(it.toInt() and 255)}==args.getString("originalSettingsSHA256")){"Original persistent settings bytes changed before restoration"}
            File(output,"settings-before.json").writeBytes(inputSettings)
            val retainedBeforeOpen=runCatching{MainActivity::class.java.getDeclaredField("retainedController").apply{isAccessible=true}.get(null)!=null}.getOrNull()
            metadata.put("retainedControllerBeforeOpen",retainedBeforeOpen ?: JSONObject.NULL).put("processId",android.os.Process.myPid())
            metadata.put("activityOpenRequestOwnerNanoseconds",System.nanoTime())
            activity=startActivitySync(Intent(targetContext,MainActivity::class.java).putExtra("screen","viewer").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
            controller=MainActivity::class.java.getDeclaredField("controller").apply{isAccessible=true}.get(activity) as ViewerController
            ui {
                fun field(name:String)=MainActivity::class.java.getDeclaredField(name).apply{isAccessible=true}.get(activity)
                metadata.put("screenBeforeNavigation",field("screen")).put("panelBeforeNavigation",field("temporary") ?: JSONObject.NULL)
                val tabs=field("tabs") as Map<*,*>
                val viewerTab=tabs["viewer"] as android.widget.Button
                check(viewerTab.isShown && viewerTab.performClick()){"Viewer navigation unavailable"}
                val surface=views(activity!!.window.decorView).filterIsInstance<ViewerSurfaceView>().single()
                val cameraEnabled=ViewerSurfaceView::class.java.getDeclaredField("cameraInputEnabled").apply{isAccessible=true}.getBoolean(surface)
                check(field("screen")=="viewer" && field("temporary")==null && (field("viewerChrome") as View).isShown && !(field("overlay") as View).isShown && surface.isShown && cameraEnabled){"Visible camera-enabled viewer required"}
                metadata.put("viewerNavigation","Existing viewer navigation tab performClick closes covering panel")
                    .put("viewerVisibleBeforeCapture",true).put("cameraInputEnabledBeforeCapture",cameraEnabled)
            }
            val readyDeadline=SystemClock.elapsedRealtime()+90000
            while(controller!!.state.presentation!=ViewerPresentation.READY && SystemClock.elapsedRealtime()<readyDeadline)Thread.sleep(250)
            check(controller!!.state.presentation==ViewerPresentation.READY){"Viewer not READY"}
            val readyOwner=System.nanoTime();metadata.put("readyOwnerNanoseconds",readyOwner)
            check(controller!!.state.selectedSceneId==controller!!.state.builtin?.sceneId){"Requested current builtin is not selected; refusing scene change"}
            snapshot() // Preserve the observed profiling state before intentionally changing it.
            // Wrap the existing touch sink on main; preserve every original event and math.
            // Reading only latestCameraInputReceipt after MotionEvent would lose callbacks.
            val receiptAdmission=BudgetCoordinator.forNative(nativeHandle()).tryReserve(BudgetComponent.Java,2L shl 20)
            metadata.put("cameraReceiptAdmission",receiptAdmission.result.toJson()).put("cameraReceiptReservedBytes",2L shl 20).put("cameraReceiptCapacity",4096)
            receiptLease=checkNotNull(receiptAdmission.reservation){"Camera receipt memory admission failed"}
            receiptBuffer=arrayOfNulls(4096);receiptInputs=arrayOfNulls(4096);receiptEventOrdinals=LongArray(4096);receiptTimes=LongArray(4096);receiptBeginTimes=LongArray(4096);receiptMotion=BooleanArray(4096)
            ui {
                val surface=views(activity!!.window.decorView).filterIsInstance<ViewerSurfaceView>().single()
                val touch=ViewerSurfaceView::class.java.getDeclaredField("touch").apply{isAccessible=true}.get(surface)
                val sink=TouchCameraController::class.java.getDeclaredField("send").apply{isAccessible=true}
                @Suppress("UNCHECKED_CAST") val originalSink=sink.get(touch) as (CameraInputState)->Unit
                val wrapper:(CameraInputState)->Unit={ input ->
                    val requestBegin=if(receiptHookActive)System.nanoTime() else 0L
                    originalSink(input)
                    if(receiptHookActive) {
                    val receipt=controller?.latestCameraInputReceipt
                    if(receipt==null)receiptHookMissing=true else if(receiptCount>=4096)receiptOverflow=true else {
                        receiptBuffer!![receiptCount]=receipt;receiptInputs!![receiptCount]=input;receiptEventOrdinals!![receiptCount]=activeEventOrdinal.get();receiptBeginTimes!![receiptCount]=requestBegin;receiptTimes!![receiptCount]=System.nanoTime();receiptMotion!![receiptCount]=input.orbitX!=0f || input.orbitY!=0f || input.panX!=0f || input.panY!=0f || input.pinchScale!=1f;receiptCount++
                    }
                    }
                }
                receiptHookActive=true; sink.set(touch,wrapper)
                receiptHookRestore={sink.set(touch,originalSink)}
            }
            val profiling=NativeBridge.setProfiling(nativeHandle(),profilingMode=="diagnostic",clear=true)
            check(profiling.ok){"Profiling control failed: $profiling"}
            val enabled=profiling.payload.getBoolean("profilingEnabled")
            metadata.put("profilingEnabled",enabled)
            check(enabled==(profilingMode=="diagnostic")){"Native profiling acknowledgement mismatch"}
            val area=Rect();ui{check(views(activity!!.window.decorView).filterIsInstance<ViewerSurfaceView>().single().getGlobalVisibleRect(area))}
            metadata.put("surfaceBounds",JSONArray(listOf(area.left,area.top,area.right,area.bottom)))
            metadata.put("captureNativeOwner",nativeHandle().value).put("uptimeMillisAtReady",SystemClock.uptimeMillis()).put("elapsedRealtimeNanosAtReady",SystemClock.elapsedRealtimeNanos())
            val initial=snapshot();metadata.put("readyLatest",initial)
            val initialConfig=initial.getJSONObject("effectiveConfig")
            check(initialConfig.getInt("rasterSharedMemoryMode")==0 && initialConfig.getInt("displaySHDegree")==3 && initialConfig.getInt("shStorageMode")==0 && initialConfig.getInt("shStorageDegree")==-1){"Foundation proof requires original mode0 SH3/FP32 config"}
            check(initial.getString("contentHash")=="007121454769ff9d816c37af1f0381f12bfea8179bb10c82b34f42cca381c816" && initial.getInt("renderWidth")==1876 && initial.getInt("renderHeight")==1105 && initial.getJSONObject("budget").getLong("limitBytes")==805306368L){"Original content/extent/cap identity unavailable"}
            fun reset():JSONObject {
                val prior=snapshot()
                val rowsBefore=checkNotNull(latestSnapshotPayload).getJSONObject("diagnostics").getJSONArray("frames")
                val beforeToken=(0 until rowsBefore.length()).maxOfOrNull{rowsBefore.getJSONObject(it).optLong("submissionToken")} ?: 0L
                val scope=JSONObject()
                for(key in listOf("sceneGeneration","surfaceGeneration","configGeneration"))scope.put(key,prior.get(key))
                scope.put("pipelineGeneration",prior.getJSONObject("measurement").get("pipelineGeneration"))
                val begin=System.nanoTime()
                val receiptStart=receiptCount
                val ordinal=events.length();activeEventOrdinal.set(ordinal.toLong())
                try{ui{views(activity!!.window.decorView).filterIsInstance<ViewerSurfaceView>().single().resetCamera()}}finally{activeEventOrdinal.set(-1)}
                val requestEnd=System.nanoTime()
                val receipts=(receiptStart until receiptCount).map{receiptAt(it)}
                val receipt=receipts.single{it.optBoolean("reset")}
                val observation=JSONObject().put("kind","reset").put("method","ViewerSurfaceView.resetCamera; same action as reset button")
                    .put("eventOrdinal",ordinal)
                    .put("ownerNanoseconds",begin).put("endOwnerNanoseconds",requestEnd).put("controllerReceipt",receipt)
                    .put("beforeSubmissionToken",beforeToken).put("requiredScope",scope).put("matchingBudgetNanoseconds",10000000000L).put("matchingStatus","pending")
                events.put(observation)
                val deadline=System.nanoTime()+10000000000L
                try {
                    while(System.nanoTime()<deadline) {
                        snapshot()
                        val payload=checkNotNull(latestSnapshotPayload)
                        val rows=payload.getJSONObject("diagnostics").getJSONArray("frames")
                        val matches=(0 until rows.length()).map{rows.getJSONObject(it)}.filter{consumedCamera(it,receipt,beforeToken,scope)}
                        val tuples=matches.map{it.getLong("frameId") to it.getLong("submissionToken")}
                        check(tuples.map{it.first}.distinct().size==tuples.size && tuples.map{it.second}.distinct().size==tuples.size){"Duplicate consumed camera frame or token"}
                        val completed=matches.filter{nativeAppliedCamera(it,receipt,payload)}
                        if(completed.isNotEmpty() && !receiptOverflow && !receiptHookMissing) {
                            val match=completed.minBy{it.getLong("submissionToken")}
                            observation.put("matchingStatus","matched").put("gpuCompletionEvidence","resolved_pipeline_query_exact_origin").put("consumedFrame",match).put("nativeReceipts",payload.opt("cameraInputReceipts") ?: JSONObject.NULL)
                                .put("matchingEndOwnerNanoseconds",System.nanoTime())
                            return observation
                        }
                        Thread.sleep(100)
                    }
                    error("Specific reset request has no fresh successful consumed compute within 10 seconds")
                }catch(error:Throwable){observation.put("matchingStatus","failed").put("failure",error.toString()).put("matchingEndOwnerNanoseconds",System.nanoTime());throw error}
            }
            fun qualityImage(pose:String) {
                if(purpose!="quality" && !restorationOnly)return
                val directory=File(output,"quality").apply{check(isDirectory || mkdir())}
                val began=System.nanoTime()
                val observation=JSONObject().put("schemaVersion",1).put("capturePurpose","quality-replay")
                    .put("poseId",pose).put("requestOwnerNanoseconds",began).put("loadedStatus","unverified")
                    .put("qualityStatus","blocked")
                    .put("limitations",JSONArray(listOf("Physical native readiness is independent of pixel certification; original image and fixed ROI must still be inspected and frozen before any candidate.","Readback and PNG encoding replay is separate from performance captures; endpoint images do not prove motion quality.")))
                fun readiness(row:JSONObject):JSONObject {
                    val reasons=JSONArray()
                    val p0=row.optJSONObject("diagnosticsP0")
                    val identity=p0?.optJSONObject("identity")
                    val measurement=row.optJSONObject("measurement")
                    if(p0?.optInt("schemaVersion")!=1)reasons.put("native_detail_admission_extension_unavailable")
                    for(key in listOf("frameId","submissionToken","sceneGeneration","surfaceGeneration","configGeneration","pipelineGeneration")) {
                        val expected=if(key=="pipelineGeneration")measurement?.optLong(key,-1L) else row.optLong(key,-1L)
                        if(expected==null || expected<=0L || identity?.optLong(key,-2L)!=expected)reasons.put("native_identity_mismatch_$key")
                    }
                    val resources=p0?.optJSONObject("resources")
                    val cut=resources?.optJSONObject("cut")
                    if(resources?.optString("status")!="observed" || resources?.optLong("aggregateLimitBytes",-1L)!=805306368L)reasons.put("native_resources_or_unchanged_cap_unavailable")
                    if(resources?.optJSONObject("admission")?.optString("status")!="admitted")reasons.put("native_admission_not_proven")
                    if(cut?.opt("rootReady")!=true || cut?.opt("loading")!=false || cut?.optLong("requestPages",-1L)!=0L)reasons.put("necessary_detail_loading_incomplete_or_unknown")
                    val error=cut?.optDouble("maxSelectedProxyErrorPixels",Double.NaN) ?: Double.NaN
                    val threshold=row.optJSONObject("effectiveConfig")?.optDouble("qualityPixels",Double.NaN) ?: Double.NaN
                    if(!error.isFinite() || error<0 || !threshold.isFinite() || threshold<=0 || error>threshold)reasons.put("requested_detail_error_not_satisfied")
                    val pages=cut?.optLong("selectedPageCount",-1L) ?: -1L
                    if(pages<=0L || cut?.optLong("usedSelectedPages",-2L)!=pages)reasons.put("selected_cut_successful_consumption_unavailable")
                    if(cut?.optLong("consumptionSubmissionToken",-1L)!=row.optLong("submissionToken",-2L))reasons.put("selected_cut_consumption_token_unavailable")
                    if(cut?.opt("transitionAdmitted")!=true)reasons.put("necessary_transition_admission_unavailable")
                    if((cut?.optLong("activeCount",-1L) ?: -1L)<=0L)reasons.put("consumed_active_detail_unavailable")
                    for(key in listOf("selectionVersion","residencyVersion"))if((cut?.optLong(key,-1L) ?: -1L)<=0L)reasons.put("cut_identity_unavailable_$key")
                    if(row.optJSONObject("status")?.opt("ok")!=true || row.optInt("renderWidth")!=1876 || row.optInt("renderHeight")!=1105)reasons.put("successful_original_size_submission_unavailable")
                    return JSONObject().put("status",if(reasons.length()==0)"ready" else "blocked").put("reasons",reasons)
                        .put("identity",identity ?: JSONObject.NULL).put("resources",resources ?: JSONObject.NULL)
                }
                fun digest(file:File):String {
                    val hash=MessageDigest.getInstance("SHA-256")
                    file.inputStream().use { input -> val buffer=ByteArray(65536);var count=input.read(buffer);while(count>=0){if(count>0)hash.update(buffer,0,count);count=input.read(buffer)} }
                    return hash.digest().joinToString(""){"%02x".format(it.toInt() and 255)}
                }
                fun durableCopy(source:File,destination:File):JSONObject {
                    check(!destination.exists()){"Refusing overwrite of screenshot evidence"}
                    source.inputStream().use { input -> FileOutputStream(destination).use { target -> input.copyTo(target);target.fd.sync() } }
                    val originalHash=digest(source);val copyHash=digest(destination)
                    check(originalHash==copyHash && source.length()==destination.length()){"Durable screenshot copy hash mismatch"}
                    return JSONObject().put("privateName",source.name).put("path","quality/${destination.name}")
                        .put("bytes",destination.length()).put("sourceSha256",originalHash).put("copySha256",copyHash)
                }
                try {
                    // JNI writes only below this literal private child. Do not create
                    // arbitrary request paths, follow a child symlink or mkdir ancestors.
                    val privateRoot=targetContext.filesDir.canonicalFile
                    val exports=File(privateRoot,"exports")
                    check(!Files.isSymbolicLink(exports.toPath()) && exports.canonicalFile==exports && exports.parentFile==privateRoot){"Unsafe private screenshot parent"}
                    if(!exports.exists()){check(exports.mkdir()){"Cannot create private screenshot parent"};createdPrivateExports=exports;metadata.put("privateExportParentCreatedByCapture",true)}
                    check(exports.isDirectory){"Private screenshot parent is not a directory"}
                    observation.put("privateExportParentPrepared",true)
                    var before=snapshot()
                    val readinessDeadline=(if(p3ReferenceOrdinal!=null)began else System.nanoTime())+referenceWaitSeconds*1000000000L
                    var polls=1
                    while(readiness(before).getString("status")!="ready" && System.nanoTime()<readinessDeadline) {
                        val remaining=readinessDeadline-System.nanoTime()
                        if(remaining<=0)break
                        Thread.sleep(minOf(250L,maxOf(1L,remaining/1000000L)));before=snapshot();polls++
                    }
                    observation.put("beforeLatest",before).put("readinessPolls",polls).put("beforeReadiness",readiness(before))
                        .put("readinessWaitOwnerNanoseconds",System.nanoTime()-began)
                    val readyObserved=System.nanoTime()
                    observation.put("readinessObservedOwnerNanoseconds",readyObserved).put("readinessDeadlineOwnerNanoseconds",readinessDeadline)
                    if(p3ReferenceOrdinal!=null)check(readiness(before).getString("status")=="ready" && readyObserved<=readinessDeadline){"Independent settled reference not ready within registered15second bound"}
                    // Capture diagnostic original pixels even if readiness is blocked.
                    // Only a same-screenshot-frame join can establish reference loading.
                    val request=NativeBridge.snapshot(nativeHandle(),JSONObject().put("schemaVersion",1).put("kind","screenshot"))
                    observation.put("request",request.toJson())
                    check(request.ok){"Screenshot request rejected: $request"}
                    val deadline=if(p3ReferenceOrdinal!=null)readinessDeadline else System.nanoTime()+15000000000L
                    var completed=false
                    while(System.nanoTime()<deadline && !completed) {
                        Thread.sleep(100)
                        val status=NativeBridge.snapshot(nativeHandle(),JSONObject().put("schemaVersion",1).put("kind","screenshotStatus"))
                        if(status.ok) {
                            val source=File(status.payload.getString("path"))
                            check(source.parentFile?.canonicalFile==exports && source.name.matches(Regex("screenshot-[0-9]+-[0-9]+\\.png")) && !Files.isSymbolicLink(source.toPath()) && source.canonicalFile==source){"Unsafe screenshot export"}
                            check(source.isFile && source.length() in 1L..(128L*1024*1024)){"Invalid screenshot export"}
                            val completionObserved=System.nanoTime()
                            observation.put("nativeResult",status.payload).put("screenshotCompletionObservedOwnerNanoseconds",completionObserved).put("screenshotDeadlineOwnerNanoseconds",deadline)
                            val nativeSidecar=File(exports,source.name+".json")
                            check(!Files.isSymbolicLink(nativeSidecar.toPath()) && nativeSidecar.canonicalFile==nativeSidecar && nativeSidecar.isFile && nativeSidecar.length() in 1L..(16L*1024*1024)){"Unsafe screenshot metadata export"}
                            val receipt=JSONObject().put("schemaVersion",1).put("durableCopyVerified",true)
                                .put("files",JSONArray(listOf(durableCopy(source,File(directory,"$pose.png")),durableCopy(nativeSidecar,File(directory,"$pose.native.json")))))
                            FileOutputStream(File(directory,"$pose.private-copy-receipt.json")).use{it.write(receipt.toString(2).toByteArray(Charsets.UTF_8));it.fd.sync()}
                            // Profile need not permit run-as. Retained external files
                            // and receipt are durable before deleting our originals;
                            // a failed host pull leaves all device capture evidence.
                            check(source.delete() && nativeSidecar.delete()){"Private screenshot export cleanup failed"}
                            observation.put("status","captured").put("privateExportFilesRemoved",true)
                                .put("privateCopyReceipt","$pose.private-copy-receipt.json")
                            if(p3ReferenceOrdinal!=null)check(completionObserved<=deadline){"Independent reference screenshot completion exceeded original15second deadline"}
                            completed=true
                        }else check(status.payload.optString("status")=="pending"){"Screenshot failed: $status"}
                    }
                    check(completed){"Screenshot completion timeout"}
                }catch(error:Throwable){observation.put("status","blocked").put("reason",error.toString())}
                observation.put("endOwnerNanoseconds",System.nanoTime()).put("afterLatest",snapshot())
                val diagnosticPayload=checkNotNull(latestSnapshotPayload)
                File(directory,"$pose.diagnostics.json").writeText(JSONObject().put("payload",diagnosticPayload).toString())
                observation.put("diagnosticSidecar","$pose.diagnostics.json")
                val screenshot=observation.optJSONObject("nativeResult")?.optJSONObject("metadata")
                val rows=diagnosticPayload.getJSONObject("diagnostics").getJSONArray("frames")
                val association=JSONObject().put("status","blocked").put("policy","exact_post_submit_or_unique_ring_no_timestamp_fallback")
                val physical=try {
                    val completed=observation.getJSONObject("nativeResult")
                    check(completed.optString("status")=="completed" && completed.opt("gpuCompleted")==true){"GPU-completed screenshot unavailable"}
                    val frame=screenshotFrame(checkNotNull(screenshot),rows)
                    val source=if(screenshot.optJSONObject("viewerFrame")!=null)"native_post_submit_screenshot_viewerFrame" else "same_frame_native_diagnostic_ring"
                    association.put("status","matched").put("source",source)
                        .put("ringContainsExactFrame",(0 until rows.length()).any{rows.getJSONObject(it).optLong("frameId",-1L)==frame.getLong("frameId") && rows.getJSONObject(it).optLong("submissionToken",-1L)==frame.getLong("submissionToken")})
                        .put("nativeBuildIdentity",frame.opt("buildIdentity") ?: JSONObject.NULL)
                        .put("externalBuildBindingRequired",frame.isNull("buildIdentity"))
                    readiness(frame)
                }catch(error:Throwable){
                    association.put("reason",error.toString())
                    JSONObject().put("status","blocked").put("reasons",JSONArray(listOf("exact_screenshot_frame_association_unavailable",error.toString())))
                }
                observation.put("frameAssociation",association)
                    .put("physicalReadiness",physical).put("loadedStatus",if(physical.getString("status")=="ready")"physically_ready_pixel_quality_unverified" else "blocked")
                    .put("physicalReadinessSource",association.opt("source") ?: JSONObject.NULL)
                    .put("nextAction","Inspect real original image and frozen camera/source/config/Shader/presentation identities; freeze full-image and piano/bicycle detail ROI policy before any candidate. Resolve native readiness reasons first.")
                File(directory,"$pose.observation.json").writeText(observation.toString(2))
            }
            val sampler=java.util.concurrent.Executors.newSingleThreadExecutor { task -> Thread(task,"P3ScreenshotSampler") }
            samplerService=sampler
            var pending:java.util.concurrent.Future<JSONObject>?=null
            val movingShots=JSONArray()
            metadata.put("p3FixedSchedule",p3Fixed).put("p3MovingShots",movingShots)
            fun collectShot() {
                pending?.let { movingShots.put(it.get(16,java.util.concurrent.TimeUnit.SECONDS));pending=null }
            }
            fun enqueueMovingShot(name:String,ordinal:Int) {
                if(pending!=null){check(pending!!.isDone){"Prior registered screenshot still in flight"};collectShot()}
                val owner=nativeHandle()
                val controllerReceipt=receiptAt(receiptCount-1)
                check(controllerReceipt.optString("phase")=="Move" && controllerReceipt.optString("disposition")=="queued")
                val requested=System.nanoTime()
                pending=sampler.submit<JSONObject> {
                    val dir=File(output,"quality").apply{check(isDirectory || mkdir())}
                    val pose="$name-motion-$ordinal"
                    val o=JSONObject().put("ordinal",ordinal).put("scenario",name).put("requestOwnerNanoseconds",requested).put("controllerReceipt",controllerReceipt)
                    val root=targetContext.filesDir.canonicalFile;val exports=File(root,"exports")
                    check(!Files.isSymbolicLink(exports.toPath()) && exports.canonicalFile==exports && exports.isDirectory)
                    val request=NativeBridge.snapshot(owner,JSONObject().put("schemaVersion",1).put("kind","screenshot"))
                    o.put("request",request.toJson());check(request.ok){"Moving screenshot request rejected"}
                    val deadline=System.nanoTime()+15000000000L
                    var result:JSONObject?=null
                    while(System.nanoTime()<deadline && result==null) {
                        val status=NativeBridge.snapshot(owner,JSONObject().put("schemaVersion",1).put("kind","screenshotStatus"))
                        if(status.ok)result=JSONObject(status.payload.toString())
                        else {check(status.payload.optString("status")=="pending"){"Moving screenshot failed"};Thread.sleep(10)}
                    }
                    val native=checkNotNull(result){"Moving screenshot timeout"}
                    check(native.optString("status")=="completed" && native.opt("gpuCompleted")==true)
                    val source=File(native.getString("path"));val sidecar=File(exports,source.name+".json")
                    for((file,dest) in listOf(source to File(dir,"$pose.png"),sidecar to File(dir,"$pose.native.json"))) {
                        check(file.name.matches(Regex("screenshot-[0-9]+-[0-9]+\\.png(\\.json)?")) && file.parentFile.canonicalFile==exports && !Files.isSymbolicLink(file.toPath()) && file.canonicalFile==file && file.isFile && file.length() in 1L..134217728L)
                        check(!dest.exists());file.inputStream().use{input->FileOutputStream(dest).use{stream->input.copyTo(stream);stream.fd.sync()}}
                        check(sha(file.readBytes())==sha(dest.readBytes()))
                    }
                    check(source.delete() && sidecar.delete())
                    o.put("nativeResult",native).put("endOwnerNanoseconds",System.nanoTime())
                    FileOutputStream(File(dir,"$pose.observation.json")).use{it.write(o.toString(2).toByteArray(Charsets.UTF_8));it.fd.sync()}
                    o
                }
            }
            // The sampler never reads/writes snapshot counters, diagnostic chunks or the controller ring.
            val privateExports=File(targetContext.filesDir.canonicalFile,"exports")
            if(p3Fixed && purpose=="quality" && !privateExports.exists()) {check(privateExports.mkdir());createdPrivateExports=privateExports}
            val modeSetups=JSONArray();metadata.put("runtimeModeSetups",modeSetups).put("restorationOnly",restorationOnly)
            if(runtimeModeArm!=null) {
                metadata.put("candidateRegistrationHash",args.getString("candidateRegistrationHash"))
                val c=checkNotNull(controller)
                val klass=ViewerController::class.java
                val executor=klass.getDeclaredField("worker").apply{isAccessible=true}.get(c) as java.util.concurrent.ExecutorService
                fun work(body:()->NativeResult):NativeResult=executor.submit(java.util.concurrent.Callable{body()}).get(10,java.util.concurrent.TimeUnit.SECONDS)
                fun mainMethod(name:String){klass.getDeclaredMethod(name).apply{isAccessible=true}.invoke(c)}
                fun updateState(next:ViewerController.State){klass.getDeclaredMethod("update",ViewerController.State::class.java).apply{isAccessible=true}.invoke(c,next)}
                val originalNative=snapshot(true)
                val originalConfig=ViewerConfig.fromJson(originalNative.getJSONObject("effectiveConfig"))
                val builtin=checkNotNull(c.state.builtin)
                val sourceHash=initial.getString("contentHash")
                val surfaceGeneration=originalNative.getLong("surfaceGeneration")
                val owner=nativeHandle()
                fun compactModeObservation(payload:JSONObject):JSONObject {
                    val observation=JSONObject().put("rawCaptureSequence",chunks-1)
                    for(key in listOf("status","requestedConfig","effectiveConfig","surfaceGeneration","resumed","windowInUse","manualRetryRequired"))observation.put(key,payload.opt(key) ?: JSONObject.NULL)
                    val diagnostics=payload.getJSONObject("diagnostics")
                    observation.put("diagnosticsAvailable",diagnostics.opt("available") ?: JSONObject.NULL)
                    diagnostics.optJSONObject("latest")?.let { latest ->
                        val identity=JSONObject()
                        for(key in listOf("frameId","submissionToken","sceneGeneration","surfaceGeneration","configGeneration","contentHash","renderWidth","renderHeight"))identity.put(key,latest.opt(key) ?: JSONObject.NULL)
                        identity.put("pipelineGeneration",latest.getJSONObject("measurement").opt("pipelineGeneration") ?: JSONObject.NULL)
                        observation.put("latestIdentity",identity)
                    }
                    return observation
                }
                fun reAdmit(template:ViewerConfig,label:String) {
                    val setup=JSONObject().put("stage",label).put("requestedMode",template.runtime.getInt("rasterSharedMemoryMode")).put("beginOwnerNanoseconds",System.nanoTime()).put("status","pending")
                    modeSetups.put(setup)
                    fun persist(){File(output,"metadata.json").writeText(metadata.toString(2))}
                    try {
                        val before=snapshot(true)
                        check(nativeHandle()==owner && before.getLong("surfaceGeneration")==surfaceGeneration && before.getJSONObject("status").getBoolean("ok") && !before.optBoolean("manualRetryRequired") && before.optJSONObject("effectiveConfig")!=null && before.getJSONObject("diagnostics").optBoolean("available")){"Mode setup requires live good original owner/session"}
                        check(c.state.selectedSceneId==builtin.sceneId && !c.state.busy && !c.state.buildBlocked && !LodBuildService.hasActiveJob()){"Mode setup controller not idle on original builtin"}
                        check(settings.readBytes().contentEquals(inputSettings)){"Persistent settings changed before mode setup"}
                        val generation=Math.addExact(maxOf(c.state.requested.generation,before.getJSONObject("requestedConfig").getLong("generation"),before.getJSONObject("effectiveConfig").getLong("generation")),1L)
                        val config=template.copy(generation=generation,configHash="")
                        ui{mainMethod("stopFrames");mainMethod("resetFps");klass.getDeclaredField("openAccepted").apply{isAccessible=true}.setBoolean(c,false)}
                        setup.put("before",compactModeObservation(before)).put("requestedConfig",config.toJson());persist()
                        val settingsResult=work{NativeBridge.settings(owner,config)};setup.put("settingsQueueReceipt",settingsResult.toJson());check(settingsResult.ok){"Mode settings rejected"}
                        val settingsDeadline=System.nanoTime()+10000000000L
                        var acknowledged:JSONObject?=null
                        while(System.nanoTime()<settingsDeadline) {
                            val observed=snapshot(true)
                            if(modeConfigAcknowledged(observed,config.toJson())){acknowledged=observed;break}
                            check(observed.getJSONObject("status").optBoolean("ok") && !observed.optBoolean("manualRetryRequired")){"Mode effective settings failed"}
                            Thread.sleep(100)
                        }
                        check(acknowledged!=null){"Published exact effective settings acknowledgement timeout"}
                        setup.put("effectiveSettingsAck",compactModeObservation(acknowledged));persist()
                        val released=work{NativeResult.fromJson(NativeBridge.nativeSettings(owner.value,JSONObject().put("schemaVersion",1).put("viewerControl",JSONObject().put("action","releaseScene")).toString()))}
                        setup.put("releaseReceipt",released.toJson());check(released.ok){"Real release ACK failed"}
                        val releaseDeadline=System.nanoTime()+10000000000L
                        var absent:JSONObject?=null
                        while(System.nanoTime()<releaseDeadline){val observed=snapshot(true);if(modeReleaseAcknowledged(observed,surfaceGeneration)){absent=observed;break};Thread.sleep(100)}
                        check(absent!=null){"Published scene_released/no-session acknowledgement timeout"}
                        setup.put("releasedNoSession",compactModeObservation(absent));persist()
                        val ids=klass.getDeclaredField("sceneIds").apply{isAccessible=true}.get(c) as java.util.concurrent.atomic.AtomicLong
                        val sceneGeneration=ids.updateAndGet{Math.addExact(it,1L)}
                        ui{updateState(c.state.copy(requested=config,effective=null,busy=false,selectedSceneId=builtin.sceneId,presentation=ViewerPresentation.PREPARING,retryAvailable=false))}
                        // No Settings/Retry/Pressure/Thermal command is issued between Release and this direct Open.
                        val opened=work{NativeBridge.openScene(owner,builtin.copy(generation=sceneGeneration))}
                        setup.put("openReceipt",opened.toJson()).put("sceneGeneration",sceneGeneration);check(opened.ok){"Fresh original scene open rejected"}
                        ui {
                            klass.getDeclaredField("openAccepted").apply{isAccessible=true}.setBoolean(c,true)
                            mainMethod("scheduleFrames")
                            klass.getDeclaredMethod("observeSurface",NativeHandle::class.java,java.lang.Long.TYPE,Integer.TYPE).apply{isAccessible=true}.invoke(c,owner,surfaceGeneration,0)
                        }
                        val readyDeadline=System.nanoTime()+60000000000L
                        var ready:JSONObject?=null
                        while(System.nanoTime()<readyDeadline) {
                            val observed=snapshot(true)
                            check(nativeHandle()==owner && observed.getLong("surfaceGeneration")==surfaceGeneration && observed.getJSONObject("status").optBoolean("ok") && !observed.optBoolean("manualRetryRequired")){"Mode initialization scope/status failed"}
                            val latest=observed.getJSONObject("diagnostics").optJSONObject("latest")
                            if(modeConfigAcknowledged(observed,config.toJson()) && observed.optBoolean("windowInUse") && observed.optBoolean("resumed") && latest!=null && latest.optLong("sceneGeneration")==sceneGeneration && latest.optString("contentHash")==sourceHash && latest.optInt("renderWidth")==1876 && latest.optInt("renderHeight")==1105 && latest.getJSONObject("budget").optLong("limitBytes")==805306368L && c.state.presentation==ViewerPresentation.READY){ready=observed;break}
                            Thread.sleep(250)
                        }
                        check(ready!=null){"Fresh reinitialized mode READY timeout"}
                        setup.put("freshReady",compactModeObservation(ready)).put("freshResetAssociation",reset()).put("status","ready").put("endOwnerNanoseconds",System.nanoTime())
                        check(settings.readBytes().contentEquals(inputSettings)){"Persistent settings changed during mode setup"};persist()
                    }catch(error:Throwable){setup.put("status","failed").put("failure",error.toString()).put("endOwnerNanoseconds",System.nanoTime());persist();throw error}
                }
                restoreRuntimeMode={reAdmit(originalConfig,"restore-original-mode0")}
                reAdmit(originalConfig.copy(runtime=JSONObject(originalConfig.runtime.toString()).put("rasterSharedMemoryMode",runtimeModeArm)),"activate-registered-arm")
            }
            if(restorationOnly){metadata.put("restorationAssociation",reset()).put("restorationPerformanceWindowCount",0);qualityImage("restoration-live");repeat(3){Thread.sleep(100);snapshot()}}
            val cx=area.exactCenterX();val cy=area.exactCenterY()
            for(name in if(restorationOnly)emptyList() else scenarioNames) {
                if(p3ReferenceOrdinal!=null && p3ReferenceScenario=="zoom" && name!="zoom")continue
                if(segmented){raw=File(rawRoot,name).apply{check(mkdir())};rawBytes=0;rawRows=0;rawFiles=0}
                val resetAssociation=reset();if(!p3Fixed)qualityImage("$name-reset");val first=snapshot();var start=System.nanoTime()
                var cutoff=start+durationMillis*1000000L
                val reasons=JSONArray();val discontinuities=JSONArray()
                val window=JSONObject().put("name",name).put("roundIndex",roundIndex).put("capturePurpose",purpose)
                    .put("nativeOwnerAtStart",nativeHandle().value).put("requestedDurationNanoseconds",durationMillis*1000000L).put("startOwnerNanoseconds",start)
                    .put("endOwnerNanoseconds",cutoff).put("windowDurationNanoseconds",durationMillis*1000000L)
                    .put("boundaryPolicy","Predeclared CLOCK_MONOTONIC half-open cutoff; actual stop/drain observations recorded separately")
                    .put("startLatest",first).put("resetAssociation",resetAssociation).put("startElapsedRealtimeNanos",SystemClock.elapsedRealtimeNanos())
                    .put("completenessReasons",reasons).put("discontinuityReasons",discontinuities).put("captureComplete",false)
                    .put("rawDirectory",if(segmented)"raw/$name" else "raw").put("gestureTimelineStartIndex",events.length())
                val measurement=first.getJSONObject("measurement")
                val generations=JSONObject()
                for(key in listOf("sceneGeneration","surfaceGeneration","configGeneration"))generations.put(key,first.get(key))
                generations.put("pipelineGeneration",measurement.get("pipelineGeneration"))
                window.put("generations",generations)
                if(name=="zoom") {
                    val prewarm=start-readyOwner
                    val firstTraversal=retainedBeforeOpen==false && scenarioNames.first()=="zoom"
                    window.put("extraPrewarmNanoseconds",prewarm).put("priorScriptedZoomWindows",scenarioNames.take(scenarioNames.indexOf(name)).count{it=="zoom"||it=="repeatzoom"})
                        .put("firstTraversalStatus",if(firstTraversal)"verified" else "unverified")
                        .put("firstTraversalReason",if(firstTraversal)"New controller/owner opening; zoom is the first scripted traversal. Existing disk/CPU/GPU caches are retained; this is not a cold-cache claim." else "Retained controller or earlier scripted path prevents proving this round's first working-set traversal.")
                    if(!firstTraversal)reasons.put("First zoom working-set traversal is unverified")
                    if(scenarioNames.first()!="zoom")reasons.put("Earlier scenarios can warm the zoom working set")
                    if(prewarm>3000000000L)reasons.put("READY to first zoom exceeds three-second extra prewarm limit; all reset/snapshot/setup time included")
                }
                windows.put(window)
                File(output,"metadata.json").writeText(metadata.toString(2))
                val movementReceiptStart=receiptCount
                val down=SystemClock.uptimeMillis();var lastDrain=start;var gestureActive=false
                val pinch=name=="zoom"||name=="repeatzoom"
                var rotationPoint=cx to cy;var pinchRadius=60f
                fun release() {
                    if(!gestureActive)return
                    if(name=="rotation")event(MotionEvent.ACTION_UP,listOf(rotationPoint),down)
                    if(pinch){event(MotionEvent.ACTION_POINTER_UP or (1 shl 8),listOf((cx-pinchRadius) to cy,(cx+pinchRadius) to cy),down);event(MotionEvent.ACTION_UP,listOf((cx-pinchRadius) to cy),down)}
                    gestureActive=false
                }
                if(name=="rotation")event(MotionEvent.ACTION_DOWN,listOf(rotationPoint),down)
                if(pinch){event(MotionEvent.ACTION_DOWN,listOf((cx-60) to cy),down);event(MotionEvent.ACTION_POINTER_DOWN or (1 shl 8),listOf((cx-60) to cy,(cx+60) to cy),down)}
                gestureActive=name!="stationary"
                if(p3Fixed) {
                    // DOWN/POINTER_DOWN precede move0, whose frozen deadline is window start.
                    val preparationStart=start;start=System.nanoTime();cutoff=start+durationMillis*1000000L
                    window.put("gesturePreparationStartOwnerNanoseconds",preparationStart).put("startOwnerNanoseconds",start).put("endOwnerNanoseconds",cutoff).put("startElapsedRealtimeNanos",SystemClock.elapsedRealtimeNanos())
                    if(name=="zoom"){window.put("extraPrewarmNanoseconds",start-readyOwner);check(start-readyOwner<=3000000000L){"P3 first zoom exceeds registered3second extra prewarm bound"}}
                }
                var fixedOrdinal=0
                while(System.nanoTime()<cutoff && (!p3Fixed || fixedOrdinal<60)) {
                    if(p3Fixed) {
                        val remaining=start+fixedOrdinal*100000000L-System.nanoTime()
                        if(remaining>0){java.util.concurrent.locks.LockSupport.parkNanos(remaining);continue}
                    }
                    val elapsed=if(p3Fixed)fixedOrdinal*100L else (System.nanoTime()-start)/1000000L
                    if(name!="stationary" && (continuousMotion || elapsed<6000L)) {
                        val cyclePhase=(elapsed%12000L)/6000f
                        val phase=if(continuousMotion && cyclePhase>1f)2f-cyclePhase else cyclePhase
                        if(name=="rotation"){rotationPoint=(cx+area.width()*.22f*phase) to (cy+area.height()*.06f*phase);event(MotionEvent.ACTION_MOVE,listOf(rotationPoint),down)}
                        if(pinch){pinchRadius=60+120*phase;event(MotionEvent.ACTION_MOVE,listOf((cx-pinchRadius) to cy,(cx+pinchRadius) to cy),down)}
                    }
                    if(p3Fixed) {
                        val record=JSONObject().put("scenario",name).put("ordinal",fixedOrdinal).put("deadlineOwnerNanoseconds",start+fixedOrdinal*100000000L).put("deliveredOwnerNanoseconds",System.nanoTime()).put("eventOrdinal",events.length()-1)
                        metadata.optJSONArray("p3Schedule") ?: metadata.put("p3Schedule",JSONArray())
                        metadata.getJSONArray("p3Schedule").put(record)
                        check(kotlin.math.abs(record.getLong("deliveredOwnerNanoseconds")-record.getLong("deadlineOwnerNanoseconds"))<=25000000L){"P3 fixed move delivery outside registered25ms deadline"}
                        if(purpose=="quality" && p3ReferenceOrdinal==null && pinch && fixedOrdinal in listOf(5,25,45))enqueueMovingShot(name,fixedOrdinal)
                        if(p3ReferenceOrdinal!=null && name==p3ReferenceScenario && fixedOrdinal==p3ReferenceOrdinal) {fixedOrdinal++;break}
                        fixedOrdinal++
                    }
                    if(System.nanoTime()-lastDrain>=1000000000L){snapshot();lastDrain=System.nanoTime()}
                    if(!p3Fixed)Thread.sleep(minOf(100L,maxOf(1L,durationMillis-(System.nanoTime()-start)/1000000L)))
                }
                // Sixty moves at offsets 0..5.9s still hold the gesture through the full [0,6s) window.
                if(p3Fixed && !(p3ReferenceOrdinal!=null && name==p3ReferenceScenario)) {
                    while(System.nanoTime()<cutoff)java.util.concurrent.locks.LockSupport.parkNanos(minOf(10000000L,cutoff-System.nanoTime()))
                }
                val stopped=System.nanoTime();window.put("actualLoopStopOwnerNanoseconds",stopped).put("loopStopOverrunNanoseconds",stopped-cutoff)
                release()
                window.put("gestureStopOwnerNanoseconds",System.nanoTime()).put("nativeOwnerAtEnd",nativeHandle().value)
                val last=snapshot();window.put("endElapsedRealtimeNanos",SystemClock.elapsedRealtimeNanos()).put("endLatest",last)
                if(p3Fixed) {
                    collectShot()
                    if(p3ReferenceOrdinal!=null && name==p3ReferenceScenario)qualityImage("$name-reference-$p3ReferenceOrdinal")
                    window.put("fixedMoveCount",fixedOrdinal).put("referencePrefixOnly",p3ReferenceOrdinal!=null && name==p3ReferenceScenario)
                }else qualityImage("$name-end")
                for(key in listOf("sceneGeneration","surfaceGeneration","configGeneration"))if(last.getLong(key)!=generations.getLong(key))discontinuities.put("$key changed")
                if(last.getJSONObject("measurement").getLong("pipelineGeneration")!=generations.getLong("pipelineGeneration"))discontinuities.put("pipelineGeneration changed")
                // A JNI call after cutoff can still return an older cached ring.
                // Observe a committed owner start beyond cutoff, proving the single
                // owner finished every earlier attempt. This is not GPU idle.
                val drainDeadline=System.nanoTime()+10000000000L
                var closed=false;var drainPolls=0
                while(System.nanoTime()<drainDeadline && (!closed || drainPolls<3)) {
                    Thread.sleep(1000);snapshot();drainPolls++
                    val payload=checkNotNull(latestSnapshotPayload)
                    val frames=payload.getJSONObject("diagnostics").getJSONArray("frames")
                    for(i in 0 until frames.length()) {
                        val frame=frames.getJSONObject(i);val m=frame.getJSONObject("measurement")
                        val same=listOf("sceneGeneration","surfaceGeneration","configGeneration").all{frame.getLong(it)==generations.getLong(it)} && m.getLong("pipelineGeneration")==generations.getLong("pipelineGeneration")
                        if(same && frame.getJSONObject("status").optBoolean("ok",false) && frame.getLong("frameId")>0 && frame.getLong("submissionToken")>0 && m.getLong("ownerStartNanoseconds")>=cutoff)closed=true
                    }
                }
                if(!closed)reasons.put("No committed same-continuity post-cutoff owner origin within bounded drain")
                if(name!="stationary") {
                    val payload=checkNotNull(latestSnapshotPayload)
                    val rows=payload.getJSONObject("diagnostics").getJSONArray("frames")
                    val scopedRows=(0 until rows.length()).map{rows.getJSONObject(it)}.filter{row ->
                        listOf("sceneGeneration","surfaceGeneration","configGeneration").all{row.optLong(it)==generations.getLong(it)} && row.getJSONObject("measurement").optLong("pipelineGeneration")==generations.getLong("pipelineGeneration")
                    }.filter{it.optLong("frameId")>0 && it.optLong("submissionToken")>0}
                    check(scopedRows.map{it.getLong("frameId")}.distinct().size==scopedRows.size && scopedRows.map{it.getLong("submissionToken")}.distinct().size==scopedRows.size){"Duplicate movement frame or token"}
                    val resetRow=resetAssociation.getJSONObject("consumedFrame")
                    val resetP=resetRow.getJSONObject("cameraProvenance")
                    var movement:JSONObject?=null
                    for(index in movementReceiptStart until receiptCount) {
                        val receipt=receiptAt(index)
                        if(receipt.optString("phase")!="Move" || !receipt.optBoolean("requestedMotion"))continue
                        for(r in 0 until rows.length()) {
                            val row=rows.getJSONObject(r)
                            if(consumedCamera(row,receipt,resetRow.getLong("submissionToken"),generations) && nativeAppliedCamera(row,receipt,payload)) {
                                val p=row.getJSONObject("cameraProvenance")
                                if(p.getLong("cameraRevision")>resetP.getLong("cameraRevision") && p.getString("uniformsHex")!=resetP.getString("uniformsHex"))movement=JSONObject().put("controllerReceipt",receipt).put("consumedFrame",row)
                            }
                        }
                    }
                    window.put("movementAssociation",movement ?: JSONObject.NULL)
                    check(movement!=null && !receiptOverflow && !receiptHookMissing){"No exact requested Move consumed by resolved successful compute with changed uniform identity"}
                }
                window.put("ownerWindowClosed",closed).put("trailingPollCount",drainPolls).put("drainEndOwnerNanoseconds",System.nanoTime()).put("trailingPollBudgetNanoseconds",10000000000L)
                window.put("gestureTimelineEndIndex",events.length()).put("captureComplete",true)
                    .put("rawBytes",rawBytes).put("rawFiles",rawFiles).put("rawRowsBeforeDeduplication",rawRows)
                File(output,"metadata.json").writeText(metadata.toString(2))
            }
            collectShot();sampler.shutdown();check(sampler.awaitTermination(1,java.util.concurrent.TimeUnit.SECONDS))
            val liveSettings=settings.readBytes()
            File(output,"settings-after-live.json").writeBytes(liveSettings)
            fun semantic(value:Any?):Any? = when(value) {
                null,JSONObject.NULL -> null
                is JSONObject -> value.keys().asSequence().toList().sorted().associateWith { semantic(value.get(it)) }
                is JSONArray -> (0 until value.length()).map { semantic(value.get(it)) }
                is Number -> value.toDouble()
                else -> value
            }
            val settingsEquivalent=semantic(JSONObject(String(liveSettings)))==semantic(JSONObject(String(inputSettings)))
            metadata.put("settingsLiveBytesEqual",liveSettings.contentEquals(inputSettings))
                .put("settingsLiveSemanticallyEqual",settingsEquivalent)
            check(settingsEquivalent){"Settings values changed during capture"}
            metadata.put("captureCompleted",true).put("rawChunks",chunks)
            resultCode=android.app.Activity.RESULT_OK;resultMessage="Capture completed: ${output.absolutePath}"
        }catch(error:Throwable){metadata.put("captureCompleted",false).put("failure",error.toString());resultMessage=error.toString()}
        finally {
            try {samplerService?.shutdownNow();check(samplerService?.awaitTermination(16,java.util.concurrent.TimeUnit.SECONDS)!=false)}catch(error:Throwable){metadata.put("samplerCleanupFailure",error.toString()).put("captureCompleted",false);resultCode=android.app.Activity.RESULT_CANCELED}
            try {restoreRuntimeMode?.invoke();if(restoreRuntimeMode!=null)metadata.put("runtimeModeOriginalRestored",true)}
            catch(error:Throwable){metadata.put("runtimeModeRestoreFailure",error.toString()).put("runtimeModeOriginalRestored",false).put("captureCompleted",false);resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()}
            try {
                ui{receiptHookActive=false;receiptHookRestore?.invoke()}
                FileOutputStream(File(output,"camera-controller-receipts.jsonl")).use { stream ->
                    for(index in 0 until receiptCount)stream.write((receiptAt(index).toString()+"\n").toByteArray(Charsets.UTF_8))
                    stream.fd.sync()
                }
                metadata.put("controllerReceiptCount",receiptCount).put("controllerReceiptFile","camera-controller-receipts.jsonl").put("controllerReceiptOverflow",receiptOverflow).put("controllerReceiptHookMissing",receiptHookMissing)
                check(!receiptOverflow && !receiptHookMissing){"Camera receipt capacity exceeded; matching unavailable"}
            }catch(error:Throwable){metadata.put("receiptCleanupFailure",error.toString()).put("captureCompleted",false);resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()}
            finally { receiptBuffer=null;receiptInputs=null;receiptEventOrdinals=null;receiptTimes=null;receiptBeginTimes=null;receiptMotion=null;try{receiptLease?.close();metadata.put("cameraReceiptReservationRetired",true)}catch(error:Throwable){metadata.put("receiptReservationFailure",error.toString()).put("captureCompleted",false);resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()} }
            try {
                createdPrivateExports?.let { exports ->
                    val children=exports.listFiles() ?: error("Cannot inspect created private screenshot parent")
                    if(children.isEmpty())check(exports.delete()){"Cannot remove empty created screenshot parent"}
                    metadata.put("createdPrivateExportParentRemoved",!exports.exists()).put("privateExportParentRemainingEntries",children.size)
                }
            }catch(error:Throwable){metadata.put("privateExportCleanupFailure",error.toString()).put("captureCompleted",false);resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()}
            try {
                if(controller!=null && originalProfilingEnabled!=null) {
                    val expected=checkNotNull(originalProfilingEnabled)
                    val restored=NativeBridge.setProfiling(nativeHandle(),expected)
                    check(restored.ok && restored.payload.getBoolean("profilingEnabled")==expected){"Final profiling restoration failed"}
                    metadata.put("originalProfilingEnabled",expected).put("finalProfilingEnabled",expected).put("profilingStateRestored",true)
                }
            }catch(error:Throwable){
                metadata.put("captureCompleted",false).put("profilingDisableFailure",error.toString())
                resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()
            }
            File(output,"metadata.json").writeText(metadata.toString(2))
            try{activity?.let{a->ui{a.finish()}};waitForIdleSync()}
            catch(error:Throwable){metadata.put("activityFinishFailure",error.toString());metadata.put("captureCompleted",false);resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()}
            try {
                controller?.let { c ->
                    ui { c.close() }
                    val worker=ViewerController::class.java.getDeclaredField("worker").apply{isAccessible=true}.get(c) as java.util.concurrent.ExecutorService
                    val retired=worker.submit<NativeResult?> {
                        ViewerController::class.java.getDeclaredField("lastRetirement").apply{isAccessible=true}.get(null) as NativeResult?
                    }.get(15,java.util.concurrent.TimeUnit.SECONDS)
                    check(retired!=null && retired.ok && !nativeHandle().isValid){"Native owner retirement not acknowledged; no settings write"}
                    metadata.put("finalNativeRetirement",retired.toJson()).put("finalNativeOwnerAbsent",true)
                }
                val finalState=guardedFinalRestore(targetContext.filesDir.canonicalFile,original,expectedSettingsDuringCapture)
                metadata.put("finalSettingsActualObservation",finalState).put("settingsOriginalBytesRestored",original!=null)
                    .put("settingsOriginalAbsentRestored",original==null && !finalState.getBoolean("exists"))
            }catch(error:Throwable){metadata.put("settingsRestoreFailure",error.toString());metadata.put("captureCompleted",false);resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()}
            metadata.put("finalStateAtomicOperations",JSONArray(stateOperations.toString())).put("captureEndElapsedRealtimeMillis",SystemClock.elapsedRealtime()).put("captureEndOwnerNanoseconds",System.nanoTime())
            if(metadata.toString(2).toByteArray(Charsets.UTF_8).size>1024*1024) {
                metadata.put("captureCompleted",false).put("failure","Metadata exceeds 1 MiB consumer bound")
                resultCode=android.app.Activity.RESULT_CANCELED;resultMessage="Metadata bound exceeded"
            }
                File(output,"metadata.json").writeText(metadata.toString(2))
        }
        finish(resultCode,Bundle().apply{putString("stream",resultMessage+"\n")})
    }
}




