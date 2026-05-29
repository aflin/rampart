/* node-compat shim for `require('v8')`.
 *
 * Node's `v8` module exposes V8-engine internals: heap statistics,
 * serialize/deserialize (V8 structured-clone wire format), heap
 * snapshots, promise hooks, GC profiler, flag toggling.  rampart
 * runs on duktape, not V8, so most of this is genuinely
 * unimplementable.  This shim satisfies the require-at-load case
 * (axios, koa, undici, etc.) and throws ERR_NOT_IMPLEMENTED on
 * anything that needs real V8 internals.
 *
 * Honest gaps:
 *   - serialize / deserialize throw — V8's structured-clone wire
 *     format isn't reproducible without V8.
 *   - Heap snapshots throw — no equivalent in duktape.
 *   - promiseHooks throw — no async-hooks integration in rampart.
 *   - GCProfiler throws — no GC instrumentation hooks exposed.
 *   - getHeapSpaceStatistics returns [] — duktape has no per-space
 *     breakdown.
 *   - setFlagsFromString is a no-op (duktape doesn't take V8 flags;
 *     duktape's runtime options are set at heap-create time).
 */
'use strict';

function notImplemented(name) {
    return function () {
        const err = new Error('v8.' + name + ' is not supported by rampart-nodeshim (duktape engine)');
        err.code = 'ERR_NOT_IMPLEMENTED';
        throw err;
    };
}

/* ---- heap statistics ----
 * Map what we can from process.memoryUsage() (which we DO have via
 * nodeshim) into v8.getHeapStatistics()'s expected shape.  Fields we
 * can't fill come back as 0 — callers that wanted real V8 numbers
 * see clearly-impossible values rather than fabricated ones. */
function getHeapStatistics() {
    let mem = { heapTotal: 0, heapUsed: 0, external: 0, rss: 0 };
    try { if (typeof process !== 'undefined' && process.memoryUsage) mem = process.memoryUsage(); } catch (_) {}
    return {
        total_heap_size:               mem.heapTotal | 0,
        total_heap_size_executable:    0,
        total_physical_size:           mem.heapTotal | 0,
        total_available_size:          0,
        used_heap_size:                mem.heapUsed  | 0,
        heap_size_limit:               0,
        malloced_memory:               mem.external  | 0,
        peak_malloced_memory:          mem.external  | 0,
        does_zap_garbage:              0,
        number_of_native_contexts:     1,
        number_of_detached_contexts:   0,
        total_global_handles_size:     0,
        used_global_handles_size:      0,
        external_memory:               mem.external  | 0
    };
}

/* Per-space stats — duktape has one heap, no spaces.  Return [] (node
 * accepts an empty array; some libs just len-check the result). */
function getHeapSpaceStatistics() { return []; }

/* Code-cache version tag.  Has to be stable across calls in the same
 * process (libs hash it as a cache key). */
const _versionTag = ((Date.now() & 0x7fffffff) ^ (Math.floor(Math.random() * 0x100000))) >>> 0;
function cachedDataVersionTag() { return _versionTag; }

/* Toggling V8 flags is meaningless on duktape — be a no-op.  Node
 * libraries that call this at init (mostly for --max-old-space-size
 * style tweaks) keep working. */
function setFlagsFromString(_flags) { /* intentional no-op */ }

/* Promise hooks: not implementable without async-hooks. */
const promiseHooks = {
    onInit:           notImplemented('promiseHooks.onInit'),
    onSettled:        notImplemented('promiseHooks.onSettled'),
    onBefore:         notImplemented('promiseHooks.onBefore'),
    onAfter:          notImplemented('promiseHooks.onAfter'),
    createHook:       notImplemented('promiseHooks.createHook')
};

/* GCProfiler stub — start/stop both throw so misuse is loud. */
class GCProfiler {
    start() { notImplemented('GCProfiler.start')(); }
    stop()  { notImplemented('GCProfiler.stop')();  }
}

/* startupSnapshot namespace — exists but every method throws. */
const startupSnapshot = {
    addSerializeCallback:           notImplemented('startupSnapshot.addSerializeCallback'),
    addDeserializeCallback:         notImplemented('startupSnapshot.addDeserializeCallback'),
    setDeserializeMainFunction:     notImplemented('startupSnapshot.setDeserializeMainFunction'),
    isBuildingSnapshot:             () => false
};

module.exports = {
    getHeapStatistics:               getHeapStatistics,
    getHeapSpaceStatistics:          getHeapSpaceStatistics,
    getHeapCodeStatistics:           () => ({ code_and_metadata_size: 0, bytecode_and_metadata_size: 0, external_script_source_size: 0 }),
    cachedDataVersionTag:            cachedDataVersionTag,
    setFlagsFromString:              setFlagsFromString,
    serialize:                       notImplemented('serialize'),
    deserialize:                     notImplemented('deserialize'),
    Serializer:                      class Serializer { constructor() { notImplemented('Serializer')(); } },
    Deserializer:                    class Deserializer { constructor() { notImplemented('Deserializer')(); } },
    DefaultSerializer:               class DefaultSerializer { constructor() { notImplemented('DefaultSerializer')(); } },
    DefaultDeserializer:             class DefaultDeserializer { constructor() { notImplemented('DefaultDeserializer')(); } },
    getHeapSnapshot:                 notImplemented('getHeapSnapshot'),
    writeHeapSnapshot:               notImplemented('writeHeapSnapshot'),
    setHeapSnapshotNearHeapLimit:    notImplemented('setHeapSnapshotNearHeapLimit'),
    stopCoverage:                    () => undefined,
    takeCoverage:                    () => undefined,
    queryObjects:                    notImplemented('queryObjects'),
    promiseHooks:                    promiseHooks,
    GCProfiler:                      GCProfiler,
    startupSnapshot:                 startupSnapshot
};
