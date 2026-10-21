package com.monsterengine.viewer

import android.app.Instrumentation
import android.content.Intent
import android.graphics.Rect
import android.os.Bundle
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.concurrent.atomic.AtomicReference

/** Intentional-only capture runner. Never called by the ordinary contract suite. */
class ViewerMeasurementInstrumentation : Instrumentation() {
    private lateinit var args: Bundle
    override fun onCreate(arguments: Bundle?) { args=arguments ?: Bundle();super.onCreate(arguments);start() }
    override fun onStart() {
        var activity:MainActivity?=null
        val settings=File(targetContext.filesDir,"viewer-settings.json")
        val original=settings.takeIf{it.isFile}?.readBytes()
        val session=args.getString("captureSession") ?: "measurement-${System.currentTimeMillis()}"
        val profilingMode=args.getString("profilingMode") ?: "off"
        require(profilingMode=="off" || profilingMode=="diagnostic")
        require(session.matches(Regex("[A-Za-z0-9_-]{1,96}")))
        val output=File(checkNotNull(targetContext.getExternalFilesDir(null)),session).apply{check(!exists());check(mkdirs())}
        val raw=File(output,"raw").apply{check(mkdir())}
        val events=JSONArray();val windows=JSONArray();var chunks=0
        var controller:ViewerController?=null
        fun nativeHandle():NativeHandle {
            val owner=ViewerController::class.java.getDeclaredField("nativeOwner").apply{isAccessible=true}.get(checkNotNull(controller)) as AtomicReference<*>
            return owner.get() as NativeHandle
        }
        fun ui(body:()->Unit) {var failure:Throwable?=null;runOnMainSync{try{body()}catch(e:Throwable){failure=e}};failure?.let{throw it}}
        fun views(v:View):List<View> = listOf(v)+if(v is ViewGroup)(0 until v.childCount).flatMap{views(v.getChildAt(it))}else emptyList()
        fun snapshot():JSONObject {
            val c=checkNotNull(controller)
            val owner=ViewerController::class.java.getDeclaredField("nativeOwner").apply{isAccessible=true}.get(c) as AtomicReference<*>
            val result=NativeBridge.snapshot(owner.get() as NativeHandle,JSONObject().put("schemaVersion",1).put("kind","diagnostics").put("includePrivatePaths",false))
            check(result.ok){result.toString()}
            val payload=result.payload
            File(raw,"chunk-${(chunks++).toString().padStart(4,'0')}.json").writeText(JSONObject().put("payload",payload).toString())
            return payload.getJSONObject("diagnostics").getJSONObject("latest")
        }
        fun event(action:Int,points:List<Pair<Float,Float>>,down:Long) {
            val properties=Array(points.size){index->MotionEvent.PointerProperties().apply{id=index;toolType=MotionEvent.TOOL_TYPE_FINGER}}
            val coords=Array(points.size){index->MotionEvent.PointerCoords().apply{x=points[index].first;y=points[index].second;pressure=1f;size=1f}}
            val now=SystemClock.uptimeMillis()
            val e=MotionEvent.obtain(down,now,action,points.size,properties,coords,0,0,1f,1f,0,0,android.view.InputDevice.SOURCE_TOUCHSCREEN,0)
            try{sendPointerSync(e)}finally{e.recycle()}
            events.put(JSONObject().put("uptimeMillis",now).put("elapsedRealtimeNanos",SystemClock.elapsedRealtimeNanos()).put("action",action).put("points",JSONArray(points.map{JSONArray(listOf(it.first,it.second))})))
        }
        var resultCode=android.app.Activity.RESULT_CANCELED
        var resultMessage="Capture failed"
        val metadata=JSONObject().put("schemaVersion",1).put("captureSession",session).put("scenarios",windows).put("gestures",events)
            .put("profilingMode",profilingMode).put("captureCompleted",false)
            .put("cacheState","Existing process caches retained; each scenario resets camera only; no cold-cache claim")
            .put("clockMapping","Native owner is C++ steady_clock CLOCK_MONOTONIC; gesture uptimeMillis CLOCK_MONOTONIC; elapsedRealtime includes suspend. Offset observed below, never equate elapsedRealtime and owner directly.")
        try {
            args.getString("settingsBase64")?.let {
                check(original==null){"Profile has existing settings; refusing overwrite"}
                val bytes=android.util.Base64.decode(it,android.util.Base64.DEFAULT)
                check(bytes.size in 1..65536);check(JSONObject(String(bytes)).getInt("schemaVersion")==1)
                settings.writeBytes(bytes)
            }
            val inputSettings=check(settings.isFile).let{settings.readBytes()}
            File(output,"settings-before.json").writeBytes(inputSettings)
            activity=startActivitySync(Intent(targetContext,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
            controller=MainActivity::class.java.getDeclaredField("controller").apply{isAccessible=true}.get(activity) as ViewerController
            val readyDeadline=SystemClock.elapsedRealtime()+90000
            while(controller!!.state.presentation!=ViewerPresentation.READY && SystemClock.elapsedRealtime()<readyDeadline)Thread.sleep(250)
            check(controller!!.state.presentation==ViewerPresentation.READY){"Viewer not READY"}
            check(controller!!.state.selectedSceneId==controller!!.state.builtin?.sceneId){"Requested current builtin is not selected; refusing scene change"}
            val profiling=NativeBridge.setProfiling(nativeHandle(),profilingMode=="diagnostic",clear=true)
            check(profiling.ok){"Profiling control failed: $profiling"}
            val enabled=profiling.payload.getBoolean("profilingEnabled")
            metadata.put("profilingEnabled",enabled)
            check(enabled==(profilingMode=="diagnostic")){"Native profiling acknowledgement mismatch"}
            val area=Rect();ui{check(views(activity!!.window.decorView).filterIsInstance<ViewerSurfaceView>().single().getGlobalVisibleRect(area))}
            metadata.put("surfaceBounds",JSONArray(listOf(area.left,area.top,area.right,area.bottom)))
            metadata.put("uptimeMillisAtReady",SystemClock.uptimeMillis()).put("elapsedRealtimeNanosAtReady",SystemClock.elapsedRealtimeNanos())
            val initial=snapshot();metadata.put("readyLatest",initial)
            fun reset() {ui{check(views(activity!!.window.decorView).first{it.contentDescription?.toString()=="复位"&&it.isShown}.performClick())};Thread.sleep(1500);snapshot()}
            val cx=area.exactCenterX();val cy=area.exactCenterY()
            for(name in listOf("stationary","rotation","zoom","repeatzoom")) {
                reset();val first=snapshot();val start=first.getJSONObject("measurement").getLong("ownerStartNanoseconds")
                val window=JSONObject().put("name",name).put("startOwnerNanoseconds",start).put("startLatest",first).put("startElapsedRealtimeNanos",SystemClock.elapsedRealtimeNanos())
                val down=SystemClock.uptimeMillis();val beginning=SystemClock.elapsedRealtime();var lastDrain=beginning
                val pinch=name=="zoom"||name=="repeatzoom"
                if(name=="rotation")event(MotionEvent.ACTION_DOWN,listOf(cx to cy),down)
                if(pinch){event(MotionEvent.ACTION_DOWN,listOf((cx-60) to cy),down);event(MotionEvent.ACTION_POINTER_DOWN or (1 shl 8),listOf((cx-60) to cy,(cx+60) to cy),down)}
                while(SystemClock.elapsedRealtime()-beginning<12000) {
                    val elapsed=SystemClock.elapsedRealtime()-beginning
                    if(elapsed<6000) {
                        val phase=elapsed/6000f
                        if(name=="rotation")event(MotionEvent.ACTION_MOVE,listOf((cx+area.width()*.22f*phase) to (cy+area.height()*.06f*phase)),down)
                        if(pinch){val radius=60+120*phase;event(MotionEvent.ACTION_MOVE,listOf((cx-radius) to cy,(cx+radius) to cy),down)}
                    }
                    if(SystemClock.elapsedRealtime()-lastDrain>=500){snapshot();lastDrain=SystemClock.elapsedRealtime()}
                    Thread.sleep(100)
                }
                if(name=="rotation")event(MotionEvent.ACTION_UP,listOf((cx+area.width()*.22f) to (cy+area.height()*.06f)),down)
                if(pinch){event(MotionEvent.ACTION_POINTER_UP or (1 shl 8),listOf((cx-180) to cy,(cx+180) to cy),down);event(MotionEvent.ACTION_UP,listOf((cx-180) to cy),down)}
                val last=snapshot();window.put("endOwnerNanoseconds",last.getJSONObject("measurement").getLong("ownerStartNanoseconds")).put("endElapsedRealtimeNanos",SystemClock.elapsedRealtimeNanos()).put("endLatest",last);windows.put(window)
                File(output,"metadata.json").writeText(metadata.toString(2))
            }
            repeat(6){Thread.sleep(500);snapshot()}
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
            try {
                if(controller!=null) {
                    val disabled=NativeBridge.setProfiling(nativeHandle(),false)
                    check(disabled.ok && !disabled.payload.getBoolean("profilingEnabled")){"Final profiling disable failed"}
                    metadata.put("finalProfilingEnabled",false)
                }
            }catch(error:Throwable){
                metadata.put("captureCompleted",false).put("profilingDisableFailure",error.toString())
                resultCode=android.app.Activity.RESULT_CANCELED;resultMessage=error.toString()
            }
            File(output,"metadata.json").writeText(metadata.toString(2))
            activity?.let{a->ui{a.finish()}}
            // Fresh Profile receives validated settings for measurement only; preserve a prior file exactly.
            if(original!=null) {
                settings.writeBytes(original)
                metadata.put("settingsOriginalBytesRestored",settings.readBytes().contentEquals(original))
                File(output,"metadata.json").writeText(metadata.toString(2))
            }
        }
        finish(resultCode,Bundle().apply{putString("stream",resultMessage+"\n")})
    }
}




