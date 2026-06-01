"noTranspile";
/* node-compat: require('timers/promises').
   Promise-returning variants of the standard timers.  setInterval is
   intentionally omitted — node's setInterval returns an AsyncIterable
   and duktape does not support async iterators (Symbol.asyncIterator
   protocol). */
var t = require('rampart-nodeshim').timers;

function _abortReason(signal) {
    if (!signal) return undefined;
    return signal.reason !== undefined ? signal.reason
        : Object.assign(new Error('The operation was aborted'), {name: 'AbortError'});
}

function setTimeoutPromise(ms, value, opts) {
    opts = opts || {};
    var signal = opts.signal;
    return new Promise(function(resolve, reject) {
        if (signal && signal.aborted) { reject(_abortReason(signal)); return; }
        var handle = t.setTimeout(function() {
            if (signal) signal.removeEventListener && signal.removeEventListener('abort', onAbort);
            resolve(value);
        }, ms);
        function onAbort() {
            t.clearTimeout(handle);
            reject(_abortReason(signal));
        }
        if (signal) signal.addEventListener && signal.addEventListener('abort', onAbort, { once: true });
    });
}

function setImmediatePromise(value, opts) {
    opts = opts || {};
    var signal = opts.signal;
    return new Promise(function(resolve, reject) {
        if (signal && signal.aborted) { reject(_abortReason(signal)); return; }
        var handle = t.setImmediate(function() {
            if (signal) signal.removeEventListener && signal.removeEventListener('abort', onAbort);
            resolve(value);
        });
        function onAbort() {
            t.clearImmediate(handle);
            reject(_abortReason(signal));
        }
        if (signal) signal.addEventListener && signal.addEventListener('abort', onAbort, { once: true });
    });
}

/* scheduler.wait(ms) and scheduler.yield() — newer Node additions. */
var scheduler = {
    wait: function(ms, opts) { return setTimeoutPromise(ms, undefined, opts); },
    yield: function() { return setImmediatePromise(); }
};

module.exports = {
    setTimeout:    setTimeoutPromise,
    setImmediate:  setImmediatePromise,
    /* setInterval intentionally omitted — see header comment. */
    scheduler:     scheduler
};
