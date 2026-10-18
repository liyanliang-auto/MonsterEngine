package com.monsterengine.viewer

import org.json.JSONException
import org.json.JSONObject

/** Disjoint retained backing. Generic Java/Native exclude already charged cache/GPU/build bytes. */
enum class BudgetComponent { CPUCache, DecodedReady, SourceGPU, StagingGPU, TemporaryGPU,
    Swapchain, Metadata, Builder, Java, Native }

/** One process-wide native accounting authority; this class never maintains a second byte total.
 * Reserve before allocation, including old+new overlap. Retire only after actual backing is gone.
 * Keep the native owner handle alive until all tickets have retired. JNI owns reservation tokens.
 */
class BudgetCoordinator(private val backend: Backend) {
    /** Injectable bridge to the same native coordinator used by renderer/build workers. */
    interface Backend {
        /** Version-1 budgetControl object; reservation IDs are opaque decimal strings. */
        fun control(command: JSONObject): NativeResult
        /** Return one native immutable aggregate snapshot, without synthetic observed totals. */
        fun snapshot(): NativeResult
        /** Dispatch pressure to render owner; success here acknowledges dispatch only. */
        fun trim(level: Int): NativeResult
        /** Explicit user recovery; native owner rechecks actual backing admission. */
        fun retry(): NativeResult
    }

    /** Explicit admission outcome; failure never grants an uncharged allocation. */
    data class Admission(val result: NativeResult, val reservation: Reservation? = null)

    /** Native accounting snapshot; reserved values are reservations, not process RSS. */
    data class Snapshot(val limitBytes: Long, val reservedBytes: Long, val peakBytes: Long,
        val exclusiveBuild: Boolean, val components: Map<BudgetComponent, Long>)

    /** Unavailable/malformed accounting remains absent instead of appearing as zero. */
    data class Observation(val result: NativeResult, val snapshot: Snapshot? = null)

    /** Explicit lifetime ticket. No GC finalizer can decide when GPU/CPU backing is retired. */
    class Reservation internal constructor(private val backend: Backend,
        val reservationId: String, val component: BudgetComponent, val bytes: Long) : AutoCloseable {
        private var released = false

        /** Retryable release: a failed bridge call retains ownership and its charge. */
        @Synchronized fun retire(): NativeResult {
            if (released) return NativeResult(true, "already_released")
            val result = backend.control(JSONObject().put("action", "release").put("reservationId", reservationId))
            if (result.ok) released = true
            return result
        }

        /** Use only after underlying backing has retired; failure is explicit. */
        override fun close() {
            val result = retire()
            check(result.ok) { "Budget reservation release failed: ${result.code}" }
        }
    }

    /** Atomically reserve native aggregate headroom; zero bytes still produces a real lease. */
    fun tryReserve(component: BudgetComponent, bytes: Long): Admission {
        require(bytes >= 0)
        val result = backend.control(JSONObject().put("action", "reserve")
            .put("component", component.name).put("bytes", bytes))
        if (!result.ok) return Admission(result)
        return try {
            val token = result.payload.getString("reservationId")
            require(token.length in 1..20 && token.all { it in '0'..'9' } && token.any { it != '0' })
            require(result.payload.getString("component") == component.name && result.payload.getLong("bytes") == bytes)
            Admission(result, Reservation(backend, token, component, bytes))
        } catch (error: JSONException) {
            Admission(NativeResult(false, "invalid_budget_result", "Malformed native admission response"))
        } catch (error: IllegalArgumentException) {
            Admission(NativeResult(false, "invalid_budget_result", "Inconsistent native admission response"))
        }
    }

    /** JNI must await renderer-safe scene retirement before exclusive native admission. */
    fun requestExclusiveBuild(): NativeResult = backend.control(JSONObject().put("action", "requestExclusiveBuild"))

    /** Native refuses release while any Builder lease remains; no unsafe local flag exists. */
    fun releaseExclusiveBuild(): NativeResult = backend.control(JSONObject().put("action", "releaseExclusiveBuild"))

    /** Decode only a complete consistent shared native snapshot. */
    fun snapshot(): Observation {
        val result = backend.snapshot()
        if (!result.ok) return Observation(result)
        return try {
            val payload = result.payload
            val limit = payload.getLong("limitBytes")
            val reserved = payload.getLong("reservedBytes")
            val peak = payload.getLong("peakBytes")
            require(limit >= 0 && reserved in 0..limit && peak in reserved..limit)
            val charges = payload.getJSONObject("components")
            val components = BudgetComponent.entries.associateWith { charges.getLong(it.name) }
            var sum = 0L
            for (value in components.values) {
                require(value >= 0 && value <= Long.MAX_VALUE - sum)
                sum += value
            }
            require(sum == reserved)
            Observation(result, Snapshot(limit, reserved, peak, payload.getBoolean("exclusiveBuild"), components.toMap()))
        } catch (error: JSONException) {
            Observation(NativeResult(false, "invalid_budget_snapshot", "Malformed native accounting snapshot"))
        } catch (error: IllegalArgumentException) {
            Observation(NativeResult(false, "invalid_budget_snapshot", "Inconsistent native accounting snapshot"))
        }
    }

    /** Native owner stops prefetch, shrinks safe cache, retires safe pages, then releases if needed. */
    fun onTrimMemory(level: Int): NativeResult = backend.trim(level)

    /** User-triggered recovery only; no automatic scene reopening after critical pressure. */
    fun retryAfterPressure(): NativeResult = backend.retry()

    companion object {
        /** Uses existing JNI methods; J23 routes budgetControl to the shared native account.
         * No independent Kotlin limit or approximate memory totals are used. */
        fun forNative(handle: NativeHandle): BudgetCoordinator = BudgetCoordinator(NativeBackend(handle))
    }

    private class NativeBackend(private val handle: NativeHandle) : Backend {
        private fun result(call: () -> String): NativeResult {
            if (!handle.isValid) return NativeResult(false, "invalid_handle")
            if (!NativeBridge.isAvailable) return NativeResult(false, "native_unavailable")
            return try { NativeResult.fromJson(call()) }
            catch (error: LinkageError) { NativeResult(false, "native_unavailable") }
            catch (error: JSONException) { NativeResult(false, "invalid_native_result") }
            catch (error: IllegalArgumentException) { NativeResult(false, "invalid_native_result") }
        }
        override fun control(command: JSONObject): NativeResult = result {
            NativeBridge.nativeSettings(handle.value, JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
                .put("budgetControl", command).toString())
        }
        override fun snapshot(): NativeResult = result {
            NativeBridge.nativeSnapshot(handle.value, JSONObject().put("schemaVersion", VIEWER_SCHEMA_VERSION)
                .put("kind", "budget").toString())
        }
        override fun trim(level: Int): NativeResult = NativeBridge.trim(handle, level)
        override fun retry(): NativeResult = NativeBridge.retry(handle)
    }
}
