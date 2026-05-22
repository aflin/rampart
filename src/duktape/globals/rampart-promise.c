/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * MIT license -- https://opensource.org/licenses/MIT
 *
 * rampart-promise.c
 *
 * Install a JavaScript `Promise` constructor in vanilla rampart.
 * Duktape ships ES5 only and has no native Promise.  Without this,
 * `new Promise(...)`, `util.promisify`, `fs.promises`, and the bare
 * `.then()` idiom all throw `ReferenceError: identifier 'Promise'
 * undefined` unless `-t` (transpiler) or `-b` (babel) is in effect.
 *
 * Polyfill body is the standalone Taylor Hakes `promise-polyfill`
 * (MIT, ~5 KB) — same source the transpiler's PROMISE_PF emits, with
 * one small patch: the upstream `var d = setTimeout; ... d(fl, 0);`
 * pattern captures setTimeout at IIFE-eval time, but `duk_init_context`
 * runs before setTimeout is registered (cmdline.c installs it later,
 * worker threads inherit it via rpthr_copy_global).  We replace the
 * captured reference with a direct `setTimeout(fl, 0)` call so the
 * lookup happens at first promise-resolution, by which time setTimeout
 * exists in every code path that can reach a `.then()`.
 *
 * Self-guarding (`'function' != typeof p.Promise ? install : patch`)
 * so the transpiler's redundant install under `-t` is a no-op.
 *
 * Wired into register.c via duk_rp_promise_init() from
 * duk_init_context(), next to install_proxy_revocable() and
 * install_modern_polyfills().
 */

#include "duktape.h"
#include "rampart.h"
#include "rampart-promise.h"

static const char *_promise_polyfill_js =
    "(function(e, t) {'object' == typeof exports && 'undefined' != typeof module ? t() :'function' == typeof define && define.amd              ? define(t) :t()})(0, function() {'use strict';function e(e) {var t = this.constructor;return this.then(function(n) {return t.resolve(e()).then(function() {return n})},function(n) {return t.resolve(e()).then(function() {return t.reject(n)})})}function t(e) {return new this(function(t, n) {function r(e, n) {if (n && ('object' == typeof n || 'function' == typeof n)) {var f = n.then;if ('function' == typeof f)return void f.call(n,function(t) {r(e, t)},function(n) {o[e] = {status: 'rejected', reason: n}, 0 == --i && t(o)})}o[e] = {status: 'fulfilled', value: n}, 0 == --i && t(o)}if (!e || 'undefined' == typeof e.length)return n(new TypeError(typeof e + ' ' + e +' is not iterable(cannot read property Symbol(Symbol.iterator))'));var o = Array.prototype.slice.call(e);if (0 === o.length) return t([]);for (var i = o.length, f = 0; o.length > f; f++) r(f, o[f])})}function n(e, t) {this.name = 'AggregateError', this.errors = e, this.message = t || ''}function r(e) {var t = this;return new t(function(r, o) {if (!e || 'undefined' == typeof e.length)return o(new TypeError('Promise.any accepts an array'));var i = Array.prototype.slice.call(e);if (0 === i.length) return o(new n([],'All promises were rejected'));for (var f = [], u = 0; i.length > u; u++) try {t.resolve(i[u]).then(r)['catch'](function(e) {f.push(e),f.length === i.length && o(new n(f, 'All promises were rejected'))})} catch (c) {o(c)}})}function o(e) {return !(!e || 'undefined' == typeof e.length)}function i() {}function f(e) {if (!(this instanceof f))throw new TypeError('Promises must be constructed via new');if ('function' != typeof e) throw new TypeError('not a function');this._state = 0, this._handled = !1, this._value = undefined,this._deferreds = [], s(e, this)}function u(e, t) {for (; 3 === e._state;) e = e._value;0 !== e._state ? (e._handled = !0, f._immediateFn(function() {var n = 1 === e._state ? t.onFulfilled : t.onRejected;if (null !== n) {var r;try {r = n(e._value)} catch (o) {return void a(t.promise, o)}c(t.promise, r)} else(1 === e._state ? c : a)(t.promise, e._value)})) :e._deferreds.push(t)}function c(e, t) {try {if (t === e)throw new TypeError('A promise cannot be resolved with itself.');if (t && ('object' == typeof t || 'function' == typeof t)) {var n = t.then;if (t instanceof f) return e._state = 3, e._value = t, void l(e);if ('function' == typeof n)return void s(function(e, t) {return function() {e.apply(t, arguments)}}(n, t), e)}e._state = 1, e._value = t, l(e)} catch (r) {a(e, r)}}function a(e, t) {e._state = 2, e._value = t, l(e)}function l(e) {2 === e._state && 0 === e._deferreds.length && f._immediateFn(function() {e._handled || f._unhandledRejectionFn(e._value)});for (var t = 0, n = e._deferreds.length; n > t; t++) u(e, e._deferreds[t]);e._deferreds = null}function s(e, t) {var n = !1;try {e(function(e) {n || (n = !0, c(t, e))},function(e) {n || (n = !0, a(t, e))})} catch (r) {if (n) return;n = !0, a(t, r)}}n.prototype = Error.prototype;f.prototype['catch'] = function(e) {return this.then(null, e)}, f.prototype.then = function(e, t) {var n = new this.constructor(i);return u(this, new function(e, t, n) {this.onFulfilled = 'function' == typeof e ? e : null,this.onRejected = 'function' == typeof t ? t : null, this.promise = n}(e, t, n)), n}, f.prototype['finally'] = e, f.all = function(e) {return new f(function(t, n) {function r(e, o) {try {if (o && ('object' == typeof o || 'function' == typeof o)) {var u = o.then;if ('function' == typeof u)return void u.call(o, function(t) {r(e, t)}, n)}i[e] = o, 0 == --f && t(i)} catch (c) {n(c)}}if (!o(e)) return n(new TypeError('Promise.all accepts an array'));var i = Array.prototype.slice.call(e);if (0 === i.length) return t([]);for (var f = i.length, u = 0; i.length > u; u++) r(u, i[u])})}, f.any = r, f.allSettled = t, f.resolve = function(e) {return e && 'object' == typeof e && e.constructor === f ? e :new f(function(t) {t(e)})}, f.reject = function(e) {return new f(function(t, n) {n(e)})}, f.race = function(e) {return new f(function(t, n) {if (!o(e)) return n(new TypeError('Promise.race accepts an array'));for (var r = 0, i = e.length; i > r; r++) f.resolve(e[r]).then(t, n)})}, f._immediateFn = (function(){var q=[],s=false;function fl(){var c=q;q=[];s=false;for(var j=0;j<c.length;j++)c[j]();}return function(e){q.push(e);if(!s){s=true;setTimeout(fl,0);}};})(), f._unhandledRejectionFn = function(e) {if (typeof console === 'undefined' || !console) return;var warn = (typeof rampart === 'undefined') ? true : (rampart.warnUnhandledPromise !== false);if (warn) console.warn('Possible Unhandled Promise Rejection:', e);};var p = function() {if ('undefined' != typeof self) return self;if ('undefined' != typeof window) return window;if ('undefined' != typeof global) return global;throw Error('unable to locate global object')}();'function' != typeof p.Promise ?p.Promise = f :(p.Promise.prototype['finally'] || (p.Promise.prototype['finally'] = e),p.Promise.allSettled || (p.Promise.allSettled = t),p.Promise.any || (p.Promise.any = r))});";

void duk_rp_promise_init(duk_context *ctx)
{
    if (duk_peval_string(ctx, _promise_polyfill_js) != 0)
    {
        fprintf(stderr, "Promise install failed: %s\n",
                duk_safe_to_string(ctx, -1));
    }
    duk_pop(ctx);
}
