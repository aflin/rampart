/* node-compat shim for `require('async_hooks')`.
 *
 * Promise-aware AsyncLocalStorage: store binding propagates across
 * `await`, `.then`, `.catch`, `.finally`, `setTimeout`, `setImmediate`,
 * and `queueMicrotask` callbacks via Promise.prototype.then patching
 * plus thin wrappers on the timer / microtask primitives.  Same trick
 * `cls-hooked` and `async-local-storage` use in userland.
 *
 * What's implemented:
 *   - AsyncLocalStorage — run, getStore, enterWith, exit, disable,
 *     bind (static + instance), snapshot
 *   - AsyncResource — runInAsyncScope, bind (static + instance),
 *     emitDestroy, asyncId / triggerAsyncId
 *   - Patches on Promise.prototype.then + setTimeout + setImmediate +
 *     queueMicrotask that capture-then-restore the ALS context
 *
 * Stubs (no engine-level hook surface to back them):
 *   - createHook returns a no-op object with enable/disable
 *   - executionAsyncId always 1, triggerAsyncId always 0
 *   - executionAsyncResource returns {}
 *
 * Honest gaps:
 *   - createHook's init/before/after/destroy callbacks never fire —
 *     APM/tracing tools (Sentry, datadog-trace) won't see resource
 *     lifecycle, though their AsyncLocalStorage usage still works.
 *   - IO callbacks (net.connect, raw fs callbacks, etc.) don't
 *     propagate context.  Modern code mostly uses the promise-based
 *     equivalents, which DO propagate through the .then patch.
 */
'use strict';

/* ---------- core context machinery ----------
 * One module-global "current context" — a Map<AsyncLocalStorage,store>.
 * run()/enterWith()/exit() install a NEW Map (don't mutate in place
 * — Promise.then captures the reference at registration time, and
 * mutating later would corrupt that capture).  The .then wrapper
 * captures the reference and restores it around the continuation. */

var _currentContext = new Map();

function _captureCtx() { return _currentContext; }
function _restoreCtx(ctx) { _currentContext = ctx; }

/* Wrap a callback so it runs with `ctx` as the current ALS context.
 * Returns fn unchanged if fn isn't a function (matches Promise.then's
 * spec — non-function onFulfilled is silently ignored). */
function _wrap(fn, ctx) {
    if (typeof fn !== 'function') return fn;
    return function _alsWrapped() {
        var prev = _currentContext;
        _currentContext = ctx;
        try { return fn.apply(this, arguments); }
        finally { _currentContext = prev; }
    };
}

/* ---------- AsyncLocalStorage ---------- */
class AsyncLocalStorage {
    constructor() { this._disabled = false; }

    run(store, fn) {
        var prev = _currentContext;
        var next = new Map(prev);
        next.set(this, store);
        _currentContext = next;
        try {
            var args = Array.prototype.slice.call(arguments, 2);
            return fn.apply(undefined, args);
        } finally {
            _currentContext = prev;
        }
    }

    getStore() {
        if (this._disabled) return undefined;
        return _currentContext.get(this);
    }

    enterWith(store) {
        if (this._disabled) return;
        var next = new Map(_currentContext);
        next.set(this, store);
        _currentContext = next;
    }

    exit(fn) {
        var prev = _currentContext;
        var next = new Map(prev);
        next.delete(this);
        _currentContext = next;
        try {
            var args = Array.prototype.slice.call(arguments, 1);
            return fn.apply(undefined, args);
        } finally {
            _currentContext = prev;
        }
    }

    disable() { this._disabled = true; }

    /* Static helpers — capture the current context to use later. */
    static bind(fn) {
        return _wrap(fn, _currentContext);
    }

    static snapshot() {
        var ctx = _currentContext;
        return function snapshotRun(fn) {
            var prev = _currentContext;
            _currentContext = ctx;
            try {
                var args = Array.prototype.slice.call(arguments, 1);
                return fn.apply(undefined, args);
            } finally {
                _currentContext = prev;
            }
        };
    }
}

/* ---------- AsyncResource ---------- */
var _asyncIdCounter = 1;

class AsyncResource {
    constructor(type, opts) {
        this.type = String(type || '');
        this._asyncId        = ++_asyncIdCounter;
        this._triggerAsyncId = (opts && typeof opts.triggerAsyncId === 'number') ? opts.triggerAsyncId : 0;
        this._capturedCtx    = _currentContext;
    }
    asyncId()        { return this._asyncId; }
    triggerAsyncId() { return this._triggerAsyncId; }
    runInAsyncScope(fn, thisArg) {
        var prev = _currentContext;
        _currentContext = this._capturedCtx;
        try {
            var args = Array.prototype.slice.call(arguments, 2);
            return fn.apply(thisArg, args);
        } finally {
            _currentContext = prev;
        }
    }
    emitDestroy() { return this; }
    bind(fn) { return _wrap(fn, this._capturedCtx); }
    static bind(fn /*, type */) { return _wrap(fn, _currentContext); }
}

/* ---------- Promise.prototype.then patch ----------
 * Capture context at .then() registration time; restore it around
 * the onFulfilled / onRejected callbacks.  In spec-compliant engines
 * .catch / .finally delegate to .then so they inherit the patch
 * automatically.  await is desugared to .then() chains by every JS
 * engine (and by rampart's transpiler), so async/await propagates
 * context through this patch as well. */
var _origThen = Promise.prototype.then;
Promise.prototype.then = function _alsPatchedThen(onFulfilled, onRejected) {
    var ctx = _currentContext;
    return _origThen.call(
        this,
        _wrap(onFulfilled, ctx),
        _wrap(onRejected, ctx)
    );
};

/* ---------- Timer / microtask patches ----------
 * Wrap each so the callback runs with the context that was current at
 * scheduling time.  Use the live original (the captured `_orig` var)
 * in the wrapper so further re-wraps don't infinitely recurse. */
if (typeof setTimeout === 'function') {
    var _origSetTimeout = setTimeout;
    /* eslint-disable no-global-assign */
    setTimeout = function _alsPatchedSetTimeout(fn, ms) {
        if (typeof fn !== 'function') {
            return _origSetTimeout.apply(this, arguments);
        }
        var ctx = _currentContext;
        var rest = Array.prototype.slice.call(arguments, 2);
        return _origSetTimeout.apply(this, [_wrap(fn, ctx), ms].concat(rest));
    };
}

if (typeof setImmediate === 'function') {
    var _origSetImmediate = setImmediate;
    setImmediate = function _alsPatchedSetImmediate(fn) {
        if (typeof fn !== 'function') {
            return _origSetImmediate.apply(this, arguments);
        }
        var ctx = _currentContext;
        var rest = Array.prototype.slice.call(arguments, 1);
        return _origSetImmediate.apply(this, [_wrap(fn, ctx)].concat(rest));
    };
}

if (typeof queueMicrotask === 'function') {
    var _origQueueMicrotask = queueMicrotask;
    queueMicrotask = function _alsPatchedQueueMicrotask(fn) {
        if (typeof fn !== 'function') {
            return _origQueueMicrotask.apply(this, arguments);
        }
        return _origQueueMicrotask(_wrap(fn, _currentContext));
    };
}

/* ---------- Stub callback-hook surface ---------- */
function createHook(_callbacks) {
    return {
        enable:  function () { return this; },
        disable: function () { return this; }
    };
}

function executionAsyncId()       { return 1; }
function triggerAsyncId()         { return 0; }
function executionAsyncResource() { return {}; }

module.exports = {
    AsyncLocalStorage:       AsyncLocalStorage,
    AsyncResource:           AsyncResource,
    createHook:              createHook,
    executionAsyncId:        executionAsyncId,
    triggerAsyncId:          triggerAsyncId,
    executionAsyncResource:  executionAsyncResource,
    /* Node lists provider name → id integers; rampart has no resource
     * lifecycle tracking so the map is empty. */
    asyncWrapProviders:      {}
};
