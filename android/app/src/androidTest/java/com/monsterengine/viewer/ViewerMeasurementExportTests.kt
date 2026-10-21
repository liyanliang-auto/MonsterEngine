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
