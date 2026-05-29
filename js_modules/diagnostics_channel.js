/* node-compat shim for `require('diagnostics_channel')`.
 *
 * Pub/sub instrumentation framework used by pino, fastify, undici,
 * cheerio, http internals, etc.  Spec-correct in-process semantics:
 * `publish(msg)` is SYNCHRONOUS — every subscriber callback runs in
 * the publisher's stack before `publish` returns.  Without that,
 * `TracingChannel.traceSync(fn)` can't keep `start`/`end` events
 * bracketing the `fn` call, and `hasSubscribers` perf-gating becomes
 * unreliable.
 *
 * What this covers:
 *   - channel(name) / Channel class with publish / subscribe /
 *     unsubscribe / hasSubscribers
 *   - top-level subscribe / unsubscribe / hasSubscribers helpers
 *   - tracingChannel(nameOrChannels) / TracingChannel class with
 *     start / end / asyncStart / asyncEnd / error sub-channels and
 *     traceSync / tracePromise / traceCallback wrappers
 *
 * Honest gaps:
 *   - No AsyncLocalStorage-based context propagation across `await`.
 *     Subscribers see published events but any "context" they try to
 *     set up before fn runs and tear down after doesn't survive
 *     awaits inside fn (we have no host-side async-hooks integration
 *     to lean on).  Publish events still fire correctly; only
 *     continuation-bound state propagation is missing.
 */
'use strict';

const channels = new Map();

class Channel {
    constructor(name) {
        this.name = name;
        this._subs = new Set();
    }
    get hasSubscribers() {
        return this._subs.size > 0;
    }
    publish(message) {
        if (this._subs.size === 0) return;
        for (const fn of this._subs) {
            try { fn(message, this.name); }
            catch (e) {
                /* Spec: errors in subscribers must not abort the publisher
                   or other subscribers.  Emit on process if we can. */
                try {
                    if (typeof process !== 'undefined' && typeof process.emit === 'function')
                        process.emit('uncaughtException', e);
                } catch (_) {}
            }
        }
    }
    subscribe(fn) {
        if (typeof fn !== 'function') {
            const err = new TypeError('subscriber must be a function');
            err.code = 'ERR_INVALID_ARG_TYPE';
            throw err;
        }
        this._subs.add(fn);
    }
    unsubscribe(fn) {
        return this._subs.delete(fn);
    }
    bindStore(store, transform)   { /* AsyncLocalStorage gap — no-op */ }
    unbindStore(store)            { return false; }
    runStores(context, fn, thisArg, ...args) { return fn.apply(thisArg, args); }
}

function channel(name) {
    let c = channels.get(name);
    if (!c) channels.set(name, c = new Channel(name));
    return c;
}

function subscribe(name, fn)   { channel(name).subscribe(fn); }
function unsubscribe(name, fn) { return channel(name).unsubscribe(fn); }
function hasSubscribers(name)  {
    const c = channels.get(name);
    return c ? c.hasSubscribers : false;
}

/* ---- TracingChannel ----
   `tracingChannel('foo')` returns an object with sub-channels:
     foo:start, foo:end, foo:asyncStart, foo:asyncEnd, foo:error
   Wrappers publish to those in the right order around `fn`. */
const TRACE_EVENTS = ['start', 'end', 'asyncStart', 'asyncEnd', 'error'];

class TracingChannel {
    constructor(nameOrChannels) {
        if (typeof nameOrChannels === 'string') {
            for (const ev of TRACE_EVENTS) this[ev] = channel(`tracing:${nameOrChannels}:${ev}`);
        } else if (nameOrChannels && typeof nameOrChannels === 'object') {
            for (const ev of TRACE_EVENTS) {
                const c = nameOrChannels[ev];
                this[ev] = (c instanceof Channel) ? c
                         : (typeof c === 'string') ? channel(c)
                         : channel(`tracing:anon:${ev}`);
            }
        } else {
            const err = new TypeError('tracingChannel argument must be a string or channels object');
            err.code = 'ERR_INVALID_ARG_TYPE';
            throw err;
        }
    }
    get hasSubscribers() {
        for (const ev of TRACE_EVENTS) if (this[ev].hasSubscribers) return true;
        return false;
    }
    subscribe(handlers) {
        for (const ev of TRACE_EVENTS) {
            if (typeof handlers[ev] === 'function') this[ev].subscribe(handlers[ev]);
        }
    }
    unsubscribe(handlers) {
        let ok = true;
        for (const ev of TRACE_EVENTS) {
            if (typeof handlers[ev] === 'function')
                ok = this[ev].unsubscribe(handlers[ev]) && ok;
        }
        return ok;
    }
    traceSync(fn, context, thisArg, ...args) {
        const ctx = context || {};
        this.start.publish(ctx);
        try {
            const result = fn.apply(thisArg, args);
            ctx.result = result;
            return result;
        } catch (e) {
            ctx.error = e;
            this.error.publish(ctx);
            throw e;
        } finally {
            this.end.publish(ctx);
        }
    }
    tracePromise(fn, context, thisArg, ...args) {
        const ctx = context || {};
        this.start.publish(ctx);
        let p;
        try { p = fn.apply(thisArg, args); }
        catch (e) {
            ctx.error = e;
            this.error.publish(ctx);
            this.end.publish(ctx);
            throw e;
        }
        this.end.publish(ctx);
        this.asyncStart.publish(ctx);
        return Promise.resolve(p).then(
            (result) => { ctx.result = result; this.asyncEnd.publish(ctx); return result; },
            (err)    => { ctx.error  = err;    this.asyncEnd.publish(ctx); throw err; }
        );
    }
    traceCallback(fn, position, context, thisArg, ...args) {
        const ctx = context || {};
        const cbPos = (typeof position === 'number') ? position : args.length - 1;
        const origCb = args[cbPos];
        if (typeof origCb !== 'function') {
            this.start.publish(ctx);
            try { return fn.apply(thisArg, args); }
            finally { this.end.publish(ctx); }
        }
        const self = this;
        args[cbPos] = function wrappedCb(err, result) {
            if (err) { ctx.error = err; self.error.publish(ctx); }
            else     { ctx.result = result; }
            self.asyncEnd.publish(ctx);
            return origCb.apply(this, arguments);
        };
        this.start.publish(ctx);
        try {
            const r = fn.apply(thisArg, args);
            this.end.publish(ctx);
            this.asyncStart.publish(ctx);
            return r;
        } catch (e) {
            ctx.error = e;
            this.error.publish(ctx);
            this.end.publish(ctx);
            throw e;
        }
    }
}

function tracingChannel(arg) { return new TracingChannel(arg); }

module.exports = {
    Channel:         Channel,
    TracingChannel:  TracingChannel,
    channel:         channel,
    tracingChannel:  tracingChannel,
    subscribe:       subscribe,
    unsubscribe:     unsubscribe,
    hasSubscribers:  hasSubscribers
};
