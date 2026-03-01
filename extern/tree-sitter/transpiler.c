/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT

   To build the test
   cc -g -DTEST -o transpiler -Ilib/include/ transpiler.c \
       -I../../src/include \
       tree-sitter-javascript/src/parser.c tree-sitter-javascript/src/scanner.c \
       lib/src/lib.c
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

//#define RP_STRING_REPORT_FREES

#include "transpiler.h"
#define RP_STRING_IMPLEMENTATION // include the functions
#include "rp_string.h"
#undef RP_STRING_IMPLEMENTATION
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef REMALLOC
#define REMALLOC(s, t)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        (s) = realloc((s), (t));                                                                                       \
        if ((char *)(s) == (char *)NULL)                                                                               \
        {                                                                                                              \
            fprintf(stderr, "error: realloc(var, %d) in %s at %d\n", (int)(t), __FILE__, __LINE__);                    \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#endif

#ifndef CALLOC
#define CALLOC(s, t)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        (s) = calloc(1, (t));                                                                                          \
        if ((char *)(s) == (char *)NULL)                                                                               \
        {                                                                                                              \
            fprintf(stderr, "error: calloc(var, %d) in %s at %d\n", (int)(t), __FILE__, __LINE__);                     \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#endif

// ============== range bookkeeping for one-pass ==============
typedef struct
{
    size_t s, e;
} Range;
typedef struct
{
    Range *a;
    size_t len, cap;
} RangeList;

static void rl_init(RangeList *rl)
{
    rl->a = NULL;
    rl->len = 0;
    rl->cap = 0;
}

/*
static int rl_overlaps(const RangeList *rl, size_t s, size_t e)
{
    for (size_t i = 0; i < rl->len; i++)
    {
        size_t S = rl->a[i].s, E = rl->a[i].e;
        if (!(e <= S || E <= s))
            return 1;
    }
    return 0;
}
*/
#define rl_overlaps(rl, _s, _e, origin)                                                                                \
    ({                                                                                                                 \
        int r = 0;                                                                                                     \
        for (size_t i = 0; i < (rl)->len; i++)                                                                         \
        {                                                                                                              \
            size_t S = (rl)->a[i].s, E = (rl)->a[i].e;                                                                 \
            if (!((_e) <= S || E <= (_s)))                                                                             \
            {                                                                                                          \
                r = 1;                                                                                                 \
                break;                                                                                                 \
            }                                                                                                          \
        }                                                                                                              \
        /*printf("%s at %s\n",r?"OVERLAP":"no overlap",origin);*/                                                      \
        r;                                                                                                             \
    })

static void rl_add(RangeList *rl, size_t s, size_t e)
{
    if (rl->len == rl->cap)
    {
        size_t nc = rl->cap ? rl->cap * 2 : 8;

        REMALLOC(rl->a, nc * sizeof(Range));

        rl->cap = nc;
    }
    rl->a[rl->len++] = (Range){s, e};
}

static int cmp_desc(const void *a, const void *b)
{
    const Edit *ea = (const Edit *)a;
    const Edit *eb = (const Edit *)b;
    if (ea->start < eb->start)
        return 1; // sort by start descending
    if (ea->start > eb->start)
        return -1;
    return 0;
}

void init_edits(EditList *e)
{
    e->items = NULL;
    e->len = 0;
    e->cap = 0;
}

static void push(EditList *e, Edit it)
{
    if (e->len == e->cap)
    {
        size_t ncap = e->cap ? e->cap * 2 : 8;
        REMALLOC(e->items, ncap * sizeof(Edit));
        e->cap = ncap;
    }
    e->items[e->len++] = it;
}

void add_edit(EditList *e, size_t start, size_t end, const char *replacement, RangeList *claimed)
{
    Edit it = {start, end, strdup(replacement), 1};
    push(e, it);
    if (claimed)
        rl_add(claimed, start, end);
}

void add_edit_take_ownership(EditList *e, size_t start, size_t end, char *replacement, RangeList *claimed)
{
    Edit it = {start, end, replacement, 1};
    push(e, it);
    if (claimed)
        rl_add(claimed, start, end);
}

static const char *poly_start = "if(!global._TrN_Sp){global._TrN_Sp={};};_TrN_Sp.load=function(){";

typedef struct {
    const char *polyfill;
    size_t      size;
    uint32_t    flag;
} polyfills;

#define SPREAD_PF   (1<<0)
#define IMPORT_PF   (1<<1)
#define CLASS_PF    (1<<2)
#define FOROF_PF    (1<<3)
#define PROMISE_PF  (1<<4)
#define ASYNC_PF    (1<<5)
#define BASE_PF     (1<<6)  // ensures _TrN_Sp preamble is emitted even with no specific polyfill
#define ES2017_PF   (1<<7)  // Object.entries, padStart/padEnd, getOwnPropertyDescriptors
#define COLLECT_PF  (1<<8)  // Set, Map, WeakSet, WeakMap polyfills


polyfills allpolys[] = {
    // stolen from babel.  Babel, like this prog is MIT licensed - see https://github.com/babel/babel/blob/main/LICENSE
    {
        "_TrN_Sp.__spreadO = function(target) {function ownKeys(object, enumerableOnly){var keys = Object.keys(object);if (Object.getOwnPropertySymbols){var symbols = Object.getOwnPropertySymbols(object);if (enumerableOnly)symbols = symbols.filter(function(sym) {return Object.getOwnPropertyDescriptor(object, sym).enumerable;});keys.push.apply(keys, symbols);}return keys;}function _defineProperty(obj, key, value){if (key in obj){Object.defineProperty(obj, key, {value : value, enumerable : true, configurable : true, writable : true});}else{obj[key] = value;}return obj;}for (var i = 1; i < arguments.length; i++){var source = arguments[i] != null ? arguments[i] : {};if (i % 2){ownKeys(Object(source), true).forEach(function(key) {_defineProperty(target, key, source[key]);});}else if (Object.getOwnPropertyDescriptors){Object.defineProperties(target, Object.getOwnPropertyDescriptors(source));}else{ownKeys(Object(source)).forEach(function(key) {Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(source, key));});}}return target;};_TrN_Sp.__spreadA = function(target, arr) {if (arr instanceof Array)return target.concat(arr);function _nonIterableSpread(){throw new TypeError(\"Invalid attempt to spread non-iterable instance. In order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");}function _unsupportedIterableToArray(o, minLen){if (!o)return;if (typeof o === \"string\")return _arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor);n = o.constructor.name;if (n === \"Map\" || n === \"Set\")return Array.from(o);if (n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n))return target.concat(_arrayLikeToArray(o, minLen));}function _iterableToArray(iter){if (typeof Symbol !== \"undefined\" && Symbol.iterator in Object(iter)){var _it=iter[Symbol.iterator](),_a=[],_s;while(!(_s=_it.next()).done)_a.push(_s.value);return target.concat(_a);}}function _arrayLikeToArray(arr, len){if (len == null || len > arr.length)len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++){arr2[i] = arr[i];}return target.concat(arr2);}function _arrayWithoutHoles(arr){if (Array.isArray(arr))return target.concat(_arrayLikeToArray(arr));}return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread();};_TrN_Sp._arrayConcat = function(items){var self = this;items.forEach(function(item) {self.push(item);});return this;};_TrN_Sp._newArray = function() {Object.defineProperty(Array.prototype, '_addchain', {value: _TrN_Sp._arrayConcat,writable: true,configurable: true,enumerable: false});Object.defineProperty(Array.prototype, '_concat', {value: Array.prototype._addchain,writable: true,configurable: true,enumerable: false});return [];};_TrN_Sp._objectAddchain = function(key, value) {if (typeof key == 'object'){Object.assign(this, key)}else{this[key] = value;}return this;};_TrN_Sp._newObject = function() {Object.defineProperty(Object.prototype, '_addchain', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});Object.defineProperty(Object.prototype, '_concat', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});return {};};",
        0, (uint32_t)SPREAD_PF },
    {
        "_TrN_Sp._typeof=function(obj) {\"@babel/helpers - typeof\";if (typeof Symbol === \"function\" && typeof Symbol.iterator === \"symbol\") {_TrN_Sp._typeof = function(obj) {return typeof obj;};} else {_TrN_Sp._typeof = function(obj) {return obj && typeof Symbol === \"function\" && obj.constructor === Symbol && obj !== Symbol.prototype ? \"symbol\" : typeof obj;};}return _TrN_Sp._typeof(obj);};_TrN_Sp._getRequireWildcardCache=function() {if (typeof WeakMap !== \"function\") return null;var cache = new WeakMap();_TrN_Sp._getRequireWildcardCache=function(){return cache;};return cache;};_TrN_Sp._interopRequireWildcard=function(obj) {if (obj && obj.__esModule) {return obj;}if (obj === null || _TrN_Sp._typeof(obj) !== \"object\" && typeof obj !== \"function\") {return { \"default\": obj };}var cache = _TrN_Sp._getRequireWildcardCache();if (cache && cache.has(obj)) {return cache.get(obj);}var newObj = {};var hasPropertyDescriptor = Object.defineProperty && Object.getOwnPropertyDescriptor;for (var key in obj) {if (Object.prototype.hasOwnProperty.call(obj, key)) {var desc = hasPropertyDescriptor ? Object.getOwnPropertyDescriptor(obj, key) : null;if (desc && (desc.get || desc.set)) {Object.defineProperty(newObj, key, desc);} else {newObj[key] = obj[key];}}}newObj[\"default\"] = obj;if (cache) {cache.set(obj, newObj);}return newObj;};_TrN_Sp._interopDefault=function(m){if(typeof m =='object' && m.__esModule){return m.default}return m;};",
        0, (uint32_t)IMPORT_PF },
    {
        "_TrN_Sp.typeof =function(obj) {'@babel/helpers - typeof';if (typeof Symbol === 'function' && typeof Symbol.iterator === 'symbol') {_typeof = function _typeof(obj) {return typeof obj;};} else {_typeof = function _typeof(obj) {return obj && typeof Symbol === 'function' &&obj.constructor === Symbol && obj !== Symbol.prototype ?'symbol' :typeof obj;};}return _typeof(obj);}; _TrN_Sp.inherits =function(subClass, superClass) {if (typeof superClass !== 'function' && superClass !== null) {throw new TypeError('Super expression must either be null or a function');}subClass.prototype = Object.create(superClass && superClass.prototype,{constructor: {value: subClass, writable: true, configurable: true}});if (superClass) _TrN_Sp.setPrototypeOf(subClass, superClass);}; _TrN_Sp.setPrototypeOf =function(o, p) {_setPrototypeOf = Object.setPrototypeOf || function _setPrototypeOf(o, p) {o.__proto__ = p;return o;};return _setPrototypeOf(o, p);}; _TrN_Sp.createSuper =function(Derived) {var hasNativeReflectConstruct = _TrN_Sp.isNativeReflectConstruct();return function _createSuperInternal() {var Super = _TrN_Sp.getPrototypeOf(Derived), result;result = Super.apply(this, arguments);return _TrN_Sp.possibleConstructorReturn(this, result);};}; _TrN_Sp.possibleConstructorReturn =function(self, call) {if (call && (typeof call === 'object' || typeof call === 'function')) {return call;}return _TrN_Sp.assertThisInitialized(self);}; _TrN_Sp.assertThisInitialized =function(self) {if (self === void 0) {throw new ReferenceError('this hasn\\'t been initialised - super() hasn\\'t been called');}return self;}; _TrN_Sp.isNativeReflectConstruct =function() {if (typeof Reflect === 'undefined' || !Reflect.construct) return false;if (Reflect.construct.sham) return false;if (typeof Proxy === 'function') return true;try {Date.prototype.toString.call(Reflect.construct(Date, [], function() {}));return true;} catch (e) {return false;}}; _TrN_Sp.getPrototypeOf =function(o) {_getPrototypeOf = Object.setPrototypeOf ?Object.getPrototypeOf :function _getPrototypeOf(o) {return o.__proto__ || Object.getPrototypeOf(o);};return _getPrototypeOf(o);}; _TrN_Sp.classCallCheck =function(instance, Constructor) {if (!(instance instanceof Constructor)) {throw new TypeError('Cannot call a class as a function');}}; _TrN_Sp.defineProperties =function(target, props) {for (var i = 0; i < props.length; i++) {var descriptor = props[i];descriptor.enumerable = descriptor.enumerable || false;descriptor.configurable = true;if ('value' in descriptor) descriptor.writable = true;Object.defineProperty(target, descriptor.key, descriptor);}}; _TrN_Sp.createClass =function(Constructor, protoProps,staticProps) {if (protoProps) _TrN_Sp.defineProperties(Constructor.prototype, protoProps);if (staticProps) _TrN_Sp.defineProperties(Constructor, staticProps);return Constructor;};",
        0, (uint32_t)CLASS_PF  },
    {
        "_TrN_Sp.slicedToArray=function (arr, i) {return _TrN_Sp.arrayWithHoles(arr) || _TrN_Sp.iterableToArrayLimit(arr, i) || _TrN_Sp.unsupportedIterableToArray(arr, i) || _TrN_Sp.nonIterableRest();};_TrN_Sp.nonIterableRest=function(){throw new TypeError(\"Invalid attempt to destructure non-iterable instance.\\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");};_TrN_Sp.unsupportedIterableToArray=function(o, minLen) {if (!o) return;if (typeof o === \"string\") return _TrN_Sp.arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor) n = o.constructor.name;if (n === \"Map\" || n === \"Set\") return Array.from(o);if (n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n)) return _TrN_Sp.arrayLikeToArray(o, minLen);};_TrN_Sp.arrayLikeToArray=function(arr, len) {if (len == null || len > arr.length) len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++) {arr2[i] = arr[i];}return arr2;};_TrN_Sp.iterableToArrayLimit=function(arr, i){if (typeof Symbol === \"undefined\" || !(Symbol.iterator in Object(arr))) return;var _arr = [];var _n = true;var _d = false;var _e = undefined;try {for (var _i = arr[Symbol.iterator](), _s; !(_n = (_s = _i.next()).done); _n = true) {_arr.push(_s.value);if (i && _arr.length === i) break;}} catch (err) {_d = true;_e = err;} finally {try {if (!_n && _i[\"return\"] != null) _i[\"return\"]();} finally {if (_d) throw _e;}}return _arr;};_TrN_Sp.arrayWithHoles=function(arr) {if (Array.isArray(arr)) return arr;};",
        0, (uint32_t)FOROF_PF  },
    {
        // Debug controls: pending warnings (ON by default, turn off with _TrN_Sp.warnOnLongPending=false)
        // do not warn for merely pending promises.
        // Enable only for debugging "long-pending" promises.
        "_TrN_Sp._wrapLongPending = function(p, label) {if (!_TrN_Sp.warnOnLongPending) return p;try {var id = setTimeout(function () {if (typeof console !== 'undefined' && console && console.warn) {console.warn('Promise still pending after', _TrN_Sp.pendingWarnMs, 'ms', label ? '(' + label + ')' : '');}}, _TrN_Sp.pendingWarnMs);if (p && typeof p['finally'] === 'function') {p['finally'](function () { clearTimeout(id); });} else if (p && typeof p.then === 'function') {p.then(function(){ clearTimeout(id); }, function(){ clearTimeout(id); });}} catch (_e) {}return p;};_TrN_Sp.asyncGeneratorStep = function(gen, resolve, reject, _next, _throw, key, arg) {try {var info = gen[key](arg);var value = info.value;} catch (error) {reject(error);return;}if (info.done) {resolve(value);} else {Promise.resolve(value).then(_next, _throw);}};_TrN_Sp.asyncToGenerator = function(fn) {return function() {var self = this, args = arguments;var __p = new Promise(function(resolve, reject) {var gen = fn.apply(self, args);function _next(value) {_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, 'next', value);}function _throw(err) {_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, 'throw', err);}_next(undefined);});return _TrN_Sp._wrapLongPending(__p, fn && fn.name ? fn.name : 'async');};_TrN_Sp.warnOnLongPending = (_TrN_Sp.warnOnLongPending === undefined ? true: _TrN_Sp.warnOnLongPending);_TrN_Sp.pendingWarnMs = (typeof _TrN_Sp.pendingWarnMs === 'number' && _TrN_Sp.pendingWarnMs >= 0) ? _TrN_Sp.pendingWarnMs : 2000;};_TrN_Sp.regeneratorRuntime = (function() {function mark(genFn) {return genFn;}function wrap(innerFn, outerFn, outerThis) {var _s=void 0,_t=false,_te;var context = {prev: 0,next: 0,done: false,rval: void 0,stop: function() {this.done = true;return this.rval;}};Object.defineProperty(context,'sent',{get:function(){if(_t){_t=false;var e=_te;_te=void 0;throw e;}return _s;},set:function(v){_s=v;},configurable:true});var _iter={next: function(arg) {context.sent = arg;context._y=false;var value = innerFn.call(outerThis, context);if (context.done || context.next === 'end') {return {value: context.rval, done: true};}if (!context._y) {context.done = true;context.rval = value;return {value: context.rval, done: true};}return {value: value, done: false};},throw: function(err) {_t=true;_te=err;return this.next(err);}};if(typeof Symbol!=='undefined'&&Symbol.iterator)_iter[Symbol.iterator]=function(){return this;};return _iter;}return {mark: mark, wrap: wrap};})();",
        // old overly verbose version:
        //"_TrN_Sp.asyncGeneratorStep = function(gen, resolve, reject, _next, _throw, key, arg) {try{var info = gen[key](arg);var value = info.value;}catch (error){reject(error);return;}if (info.done){resolve(value);}else{Promise.resolve(value).then(_next, _throw);}};_TrN_Sp.asyncToGenerator = function(fn) {return function() {var self = this, args = arguments;return new Promise(function(resolve, reject) {var gen = fn.apply(self, args);function _next(value){_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, \"next\", value);}function _throw(err){_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, \"throw\", err);}_next(undefined);});};};_TrN_Sp.regeneratorRuntime = (function () {function mark(genFn) { return genFn; }function wrap(innerFn) {var context = {prev: 0,next: 0,sent: void 0,done: false,rval: void 0,stop: function () { this.done = true; return this.rval; }};return {next: function (arg) {var prevNext = context.next;context.sent = arg;var value = innerFn(context);if (context.done || context.next === \"end\") {return { value: context.rval, done: true };}if (context.next === prevNext) {context.done = true;context.rval = value;return { value: context.rval, done: true };}return { value: value, done: false };},throw: function (err) { throw err; }};}return { mark: mark, wrap: wrap };})();",
        0, (uint32_t)ASYNC_PF  },

    // from https://www.npmjs.com/package/promise-polyfill MIT license included in this dir.
    // the delete global.Promise is for rampart.thread reload.
    {
        // set _TrN_Sp.warnUnhandledPromise=false to not get those warnings
        "delete global.Promise;_TrN_Sp.warnUnhandledPromise = (_TrN_Sp.warnUnhandledPromise === undefined ? true: _TrN_Sp.warnUnhandledPromise);(function(e, t) {'object' == typeof exports && 'undefined' != typeof module ? t() :'function' == typeof define && define.amd              ? define(t) :t()})(0, function() {'use strict';function e(e) {var t = this.constructor;return this.then(function(n) {return t.resolve(e()).then(function() {return n})},function(n) {return t.resolve(e()).then(function() {return t.reject(n)})})}function t(e) {return new this(function(t, n) {function r(e, n) {if (n && ('object' == typeof n || 'function' == typeof n)) {var f = n.then;if ('function' == typeof f)return void f.call(n,function(t) {r(e, t)},function(n) {o[e] = {status: 'rejected', reason: n}, 0 == --i && t(o)})}o[e] = {status: 'fulfilled', value: n}, 0 == --i && t(o)}if (!e || 'undefined' == typeof e.length)return n(new TypeError(typeof e + ' ' + e +' is not iterable(cannot read property Symbol(Symbol.iterator))'));var o = Array.prototype.slice.call(e);if (0 === o.length) return t([]);for (var i = o.length, f = 0; o.length > f; f++) r(f, o[f])})}function n(e, t) {this.name = 'AggregateError', this.errors = e, this.message = t || ''}function r(e) {var t = this;return new t(function(r, o) {if (!e || 'undefined' == typeof e.length)return o(new TypeError('Promise.any accepts an array'));var i = Array.prototype.slice.call(e);if (0 === i.length) return o(new n([],'All promises were rejected'));for (var f = [], u = 0; i.length > u; u++) try {t.resolve(i[u]).then(r)['catch'](function(e) {f.push(e),f.length === i.length && o(new n(f, 'All promises were rejected'))})} catch (c) {o(c)}})}function o(e) {return !(!e || 'undefined' == typeof e.length)}function i() {}function f(e) {if (!(this instanceof f))throw new TypeError('Promises must be constructed via new');if ('function' != typeof e) throw new TypeError('not a function');this._state = 0, this._handled = !1, this._value = undefined,this._deferreds = [], s(e, this)}function u(e, t) {for (; 3 === e._state;) e = e._value;0 !== e._state ? (e._handled = !0, f._immediateFn(function() {var n = 1 === e._state ? t.onFulfilled : t.onRejected;if (null !== n) {var r;try {r = n(e._value)} catch (o) {return void a(t.promise, o)}c(t.promise, r)} else(1 === e._state ? c : a)(t.promise, e._value)})) :e._deferreds.push(t)}function c(e, t) {try {if (t === e)throw new TypeError('A promise cannot be resolved with itself.');if (t && ('object' == typeof t || 'function' == typeof t)) {var n = t.then;if (t instanceof f) return e._state = 3, e._value = t, void l(e);if ('function' == typeof n)return void s(function(e, t) {return function() {e.apply(t, arguments)}}(n, t), e)}e._state = 1, e._value = t, l(e)} catch (r) {a(e, r)}}function a(e, t) {e._state = 2, e._value = t, l(e)}function l(e) {2 === e._state && 0 === e._deferreds.length && f._immediateFn(function() {e._handled || f._unhandledRejectionFn(e._value)});for (var t = 0, n = e._deferreds.length; n > t; t++) u(e, e._deferreds[t]);e._deferreds = null}function s(e, t) {var n = !1;try {e(function(e) {n || (n = !0, c(t, e))},function(e) {n || (n = !0, a(t, e))})} catch (r) {if (n) return;n = !0, a(t, r)}}n.prototype = Error.prototype;var d = setTimeout;f.prototype['catch'] = function(e) {return this.then(null, e)}, f.prototype.then = function(e, t) {var n = new this.constructor(i);return u(this, new function(e, t, n) {this.onFulfilled = 'function' == typeof e ? e : null,this.onRejected = 'function' == typeof t ? t : null, this.promise = n}(e, t, n)), n}, f.prototype['finally'] = e, f.all = function(e) {return new f(function(t, n) {function r(e, o) {try {if (o && ('object' == typeof o || 'function' == typeof o)) {var u = o.then;if ('function' == typeof u)return void u.call(o, function(t) {r(e, t)}, n)}i[e] = o, 0 == --f && t(i)} catch (c) {n(c)}}if (!o(e)) return n(new TypeError('Promise.all accepts an array'));var i = Array.prototype.slice.call(e);if (0 === i.length) return t([]);for (var f = i.length, u = 0; i.length > u; u++) r(u, i[u])})}, f.any = r, f.allSettled = t, f.resolve = function(e) {return e && 'object' == typeof e && e.constructor === f ? e :new f(function(t) {t(e)})}, f.reject = function(e) {return new f(function(t, n) {n(e)})}, f.race = function(e) {return new f(function(t, n) {if (!o(e)) return n(new TypeError('Promise.race accepts an array'));for (var r = 0, i = e.length; i > r; r++) f.resolve(e[r]).then(t, n)})}, f._immediateFn = (function(){var q=[],s=false;function fl(){var c=q;q=[];s=false;for(var j=0;j<c.length;j++)c[j]();}return function(e){q.push(e);if(!s){s=true;d(fl,0);}};})(), f._unhandledRejectionFn = function(e) {void 0 !== console && console && _TrN_Sp.warnUnhandledPromise &&console.warn('Possible Unhandled Promise Rejection:', e)};var p = function() {if ('undefined' != typeof self) return self;if ('undefined' != typeof window) return window;if ('undefined' != typeof global) return global;throw Error('unable to locate global object')}();'function' != typeof p.Promise ?p.Promise = f :(p.Promise.prototype['finally'] || (p.Promise.prototype['finally'] = e),p.Promise.allSettled || (p.Promise.allSettled = t),p.Promise.any || (p.Promise.any = r))});_TrN_Sp._pAS=Promise.allSettled;_TrN_Sp._pAn=Promise.any;_TrN_Sp._pF=Promise.prototype['finally'];_TrN_Sp._pP=function(){if(typeof Promise==='function'){if(!Promise.allSettled&&_TrN_Sp._pAS)Promise.allSettled=_TrN_Sp._pAS;if(!Promise.any&&_TrN_Sp._pAn)Promise.any=_TrN_Sp._pAn;if(Promise.prototype&&!Promise.prototype['finally']&&_TrN_Sp._pF)Promise.prototype['finally']=_TrN_Sp._pF;}};",
        // oldver
        //"delete global.Promise;(function(e,t){\"object\"==typeof exports&&\"undefined\"!=typeof module?t():\"function\"==typeof define&&define.amd?define(t):t()})(0,function(){\"use strict\";function e(e){var t=this.constructor;return this.then(function(n){return t.resolve(e()).then(function(){return n})},function(n){return t.resolve(e()).then(function(){return t.reject(n)})})}function t(e){return new this(function(t,n){function r(e,n){if(n&&(\"object\"==typeof n||\"function\"==typeof n)){var f=n.then;if(\"function\"==typeof f)return void f.call(n,function(t){r(e,t)},function(n){o[e]={status:\"rejected\",reason:n},0==--i&&t(o)})}o[e]={status:\"fulfilled\",value:n},0==--i&&t(o)}if(!e||\"undefined\"==typeof e.length)return n(new TypeError(typeof e+\" \"+e+\" is not iterable(cannot read property Symbol(Symbol.iterator))\"));var o=Array.prototype.slice.call(e);if(0===o.length)return t([]);for(var i=o.length,f=0;o.length>f;f++)r(f,o[f])})}function n(e,t){this.name=\"AggregateError\",this.errors=e,this.message=t||\"\"}function r(e){var t=this;return new t(function(r,o){if(!e||\"undefined\"==typeof e.length)return o(new TypeError(\"Promise.any accepts an array\"));var i=Array.prototype.slice.call(e);if(0===i.length)return o();for(var f=[],u=0;i.length>u;u++)try{t.resolve(i[u]).then(r)[\"catch\"](function(e){f.push(e),f.length===i.length&&o(new n(f,\"All promises were rejected\"))})}catch(c){o(c)}})}function o(e){return!(!e||\"undefined\"==typeof e.length)}function i(){}function f(e){if(!(this instanceof f))throw new TypeError(\"Promises must be constructed via new\");if(\"function\"!=typeof e)throw new TypeError(\"not a function\");this._state=0,this._handled=!1,this._value=undefined,this._deferreds=[],s(e,this)}function u(e,t){for(;3===e._state;)e=e._value;0!==e._state?(e._handled=!0,f._immediateFn(function(){var n=1===e._state?t.onFulfilled:t.onRejected;if(null!==n){var r;try{r=n(e._value)}catch(o){return void a(t.promise,o)}c(t.promise,r)}else(1===e._state?c:a)(t.promise,e._value)})):e._deferreds.push(t)}function c(e,t){try{if(t===e)throw new TypeError(\"A promise cannot be resolved with itself.\");if(t&&(\"object\"==typeof t||\"function\"==typeof t)){var n=t.then;if(t instanceof f)return e._state=3,e._value=t,void l(e);if(\"function\"==typeof n)return void s(function(e,t){return function(){e.apply(t,arguments)}}(n,t),e)}e._state=1,e._value=t,l(e)}catch(r){a(e,r)}}function a(e,t){e._state=2,e._value=t,l(e)}function l(e){2===e._state&&0===e._deferreds.length&&f._immediateFn(function(){e._handled||f._unhandledRejectionFn(e._value)});for(var t=0,n=e._deferreds.length;n>t;t++)u(e,e._deferreds[t]);e._deferreds=null}function s(e,t){var n=!1;try{e(function(e){n||(n=!0,c(t,e))},function(e){n||(n=!0,a(t,e))})}catch(r){if(n)return;n=!0,a(t,r)}}n.prototype=Error.prototype;var d=setTimeout;f.prototype[\"catch\"]=function(e){return this.then(null,e)},f.prototype.then=function(e,t){var n=new this.constructor(i);return u(this,new function(e,t,n){this.onFulfilled=\"function\"==typeof e?e:null,this.onRejected=\"function\"==typeof t?t:null,this.promise=n}(e,t,n)),n},f.prototype[\"finally\"]=e,f.all=function(e){return new f(function(t,n){function r(e,o){try{if(o&&(\"object\"==typeof o||\"function\"==typeof o)){var u=o.then;if(\"function\"==typeof u)return void u.call(o,function(t){r(e,t)},n)}i[e]=o,0==--f&&t(i)}catch(c){n(c)}}if(!o(e))return n(new TypeError(\"Promise.all accepts an array\"));var i=Array.prototype.slice.call(e);if(0===i.length)return t([]);for(var f=i.length,u=0;i.length>u;u++)r(u,i[u])})},f.any=r,f.allSettled=t,f.resolve=function(e){return e&&\"object\"==typeof e&&e.constructor===f?e:new f(function(t){t(e)})},f.reject=function(e){return new f(function(t,n){n(e)})},f.race=function(e){return new f(function(t,n){if(!o(e))return n(new TypeError(\"Promise.race accepts an array\"));for(var r=0,i=e.length;i>r;r++)f.resolve(e[r]).then(t,n)})},f._immediateFn=\"function\"==typeof setImmediate&&function(e){setImmediate(e)}||function(e){d(e,0)},f._unhandledRejectionFn=function(e){void 0!==console&&console&&console.warn(\"Possible Unhandled Promise Rejection:\",e)};var p=function(){if(\"undefined\"!=typeof self)return self;if(\"undefined\"!=typeof window)return window;if(\"undefined\"!=typeof global)return global;throw Error(\"unable to locate global object\")}();\"function\"!=typeof p.Promise?p.Promise=f:(p.Promise.prototype[\"finally\"]||(p.Promise.prototype[\"finally\"]=e),p.Promise.allSettled||(p.Promise.allSettled=t),p.Promise.any||(p.Promise.any=r))});",
        0, (uint32_t)PROMISE_PF},
    {
        // Set, Map, WeakSet, WeakMap polyfills
        "if(typeof Set==='undefined'){global.Set=(function(){function Set(iter){this._items=[];this.size=0;if(iter){if(iter instanceof Array){for(var i=0;i<iter.length;i++)this.add(iter[i]);}else if(typeof Symbol!=='undefined'&&iter[Symbol.iterator]){var it=iter[Symbol.iterator](),s;while(!(s=it.next()).done)this.add(s.value);}}}Set.prototype.add=function(v){if(!this.has(v)){this._items.push(v);this.size=this._items.length;}return this;};Set.prototype.has=function(v){for(var i=0;i<this._items.length;i++){if(this._items[i]===v||(this._items[i]!==this._items[i]&&v!==v))return true;}return false;};Set.prototype['delete']=function(v){for(var i=0;i<this._items.length;i++){if(this._items[i]===v||(this._items[i]!==this._items[i]&&v!==v)){this._items.splice(i,1);this.size=this._items.length;return true;}}return false;};Set.prototype.clear=function(){this._items=[];this.size=0;};Set.prototype.forEach=function(cb,thisArg){for(var i=0;i<this._items.length;i++)cb.call(thisArg,this._items[i],this._items[i],this);};Set.prototype.values=function(){var items=this._items,idx=0;var iter={next:function(){if(idx<items.length)return{value:items[idx++],done:false};return{value:undefined,done:true};}};if(typeof Symbol!=='undefined'&&Symbol.iterator)iter[Symbol.iterator]=function(){return this;};return iter;};Set.prototype.keys=Set.prototype.values;Set.prototype.entries=function(){var items=this._items,idx=0;var iter={next:function(){if(idx<items.length){var v=items[idx++];return{value:[v,v],done:false};}return{value:undefined,done:true};}};if(typeof Symbol!=='undefined'&&Symbol.iterator)iter[Symbol.iterator]=function(){return this;};return iter;};if(typeof Symbol!=='undefined'&&Symbol.iterator)Set.prototype[Symbol.iterator]=Set.prototype.values;return Set;})();}"
        "if(typeof Map==='undefined'){global.Map=(function(){function Map(iter){this._keys=[];this._vals=[];this.size=0;if(iter){if(iter instanceof Array){for(var i=0;i<iter.length;i++)this.set(iter[i][0],iter[i][1]);}else if(typeof Symbol!=='undefined'&&iter[Symbol.iterator]){var it=iter[Symbol.iterator](),s;while(!(s=it.next()).done)this.set(s.value[0],s.value[1]);}}}Map.prototype._idx=function(k){for(var i=0;i<this._keys.length;i++){if(this._keys[i]===k||(this._keys[i]!==this._keys[i]&&k!==k))return i;}return-1;};Map.prototype.set=function(k,v){var i=this._idx(k);if(i<0){this._keys.push(k);this._vals.push(v);}else{this._vals[i]=v;}this.size=this._keys.length;return this;};Map.prototype.get=function(k){var i=this._idx(k);return i<0?undefined:this._vals[i];};Map.prototype.has=function(k){return this._idx(k)>=0;};Map.prototype['delete']=function(k){var i=this._idx(k);if(i<0)return false;this._keys.splice(i,1);this._vals.splice(i,1);this.size=this._keys.length;return true;};Map.prototype.clear=function(){this._keys=[];this._vals=[];this.size=0;};Map.prototype.forEach=function(cb,thisArg){for(var i=0;i<this._keys.length;i++)cb.call(thisArg,this._vals[i],this._keys[i],this);};Map.prototype.keys=function(){var keys=this._keys,idx=0;var iter={next:function(){if(idx<keys.length)return{value:keys[idx++],done:false};return{value:undefined,done:true};}};if(typeof Symbol!=='undefined'&&Symbol.iterator)iter[Symbol.iterator]=function(){return this;};return iter;};Map.prototype.values=function(){var vals=this._vals,idx=0;var iter={next:function(){if(idx<vals.length)return{value:vals[idx++],done:false};return{value:undefined,done:true};}};if(typeof Symbol!=='undefined'&&Symbol.iterator)iter[Symbol.iterator]=function(){return this;};return iter;};Map.prototype.entries=function(){var keys=this._keys,vals=this._vals,idx=0;var iter={next:function(){if(idx<keys.length){var r={value:[keys[idx],vals[idx]],done:false};idx++;return r;}return{value:undefined,done:true};}};if(typeof Symbol!=='undefined'&&Symbol.iterator)iter[Symbol.iterator]=function(){return this;};return iter;};if(typeof Symbol!=='undefined'&&Symbol.iterator)Map.prototype[Symbol.iterator]=Map.prototype.entries;return Map;})();}"
        "if(typeof WeakSet==='undefined'){global.WeakSet=(function(){var _id=0;function _key(ws){if(!ws._wsid)ws._wsid='__ws'+(_id++);return ws._wsid;}function WeakSet(){this._wsid='__ws'+(_id++);}WeakSet.prototype.add=function(o){if(o===null||typeof o!=='object'&&typeof o!=='function')throw new TypeError('Invalid value used in weak set');Object.defineProperty(o,_key(this),{value:true,configurable:true,enumerable:false});return this;};WeakSet.prototype.has=function(o){if(o===null||typeof o!=='object'&&typeof o!=='function')return false;return!!o[_key(this)];};WeakSet.prototype['delete']=function(o){if(o===null||typeof o!=='object'&&typeof o!=='function')return false;var k=_key(this);if(!o[k])return false;delete o[k];return true;};return WeakSet;})();}"
        "if(typeof WeakMap==='undefined'){global.WeakMap=(function(){var _id=0;function _key(wm){if(!wm._wmid)wm._wmid='__wm'+(_id++);return wm._wmid;}function WeakMap(){this._wmid='__wm'+(_id++);}WeakMap.prototype.set=function(k,v){if(k===null||typeof k!=='object'&&typeof k!=='function')throw new TypeError('Invalid value used as weak map key');Object.defineProperty(k,_key(this),{value:v,writable:true,configurable:true,enumerable:false});return this;};WeakMap.prototype.get=function(k){if(k===null||typeof k!=='object'&&typeof k!=='function')return undefined;return k[_key(this)];};WeakMap.prototype.has=function(k){if(k===null||typeof k!=='object'&&typeof k!=='function')return false;return _key(this) in k;};WeakMap.prototype['delete']=function(k){if(k===null||typeof k!=='object'&&typeof k!=='function')return false;var key=_key(this);if(!(key in k))return false;delete k[key];return true;};return WeakMap;})();}",
        0, (uint32_t)COLLECT_PF },
    {
        // ES2017: Object.entries, padStart/padEnd, getOwnPropertyDescriptors
        "if(!Object.entries){Object.entries=function(o){var k=Object.keys(o),r=[],i;for(i=0;i<k.length;i++)r.push([k[i],o[k[i]]]);return r;};}"
        "if(!String.prototype.padStart){String.prototype.padStart=function(n,c){c=c||' ';var s=String(this);while(s.length<n)s=c+s;return s.slice(-n);};}"
        "if(!String.prototype.padEnd){String.prototype.padEnd=function(n,c){c=c||' ';var s=String(this);while(s.length<n)s=s+c;return s.slice(0,n);};}"
        "if(!Object.getOwnPropertyDescriptors){Object.getOwnPropertyDescriptors=function(o){var k=Object.getOwnPropertyNames(o),r={},i;for(i=0;i<k.length;i++)r[k[i]]=Object.getOwnPropertyDescriptor(o,k[i]);if(Object.getOwnPropertySymbols){var s=Object.getOwnPropertySymbols(o);for(i=0;i<s.length;i++)r[s[i]]=Object.getOwnPropertyDescriptor(o,s[i]);}return r;};}",
        0, (uint32_t)ES2017_PF },
    { NULL, 0}
};


static const char *poly_end = "};_TrN_Sp.load();";

char *apply_edits(const char *src, size_t src_len, EditList *e, uint32_t polysneeded)
{
    size_t out_cap, out_len;
    char *out = NULL, *ret = NULL;
    size_t out_offset = 0;

    // Sort by start desc so offsets stay valid while splicing
    qsort(e->items, e->len, sizeof(Edit), cmp_desc);

    // Estimate final size

    out_cap = src_len + 1;
    out_len = src_len;

    // find space needed
    for (size_t i = 0; i < e->len; i++)
    {
        Edit *ed = &e->items[i];
        size_t before = ed->start - out_offset;
        size_t after = ed->end - out_offset;

        // Byte lengths
        size_t rep_len = strlen(ed->text);
        long removed = (long)(after - before);

        // --- Count lines in replacement and in the removed section ---
        size_t repl_lines = 0;
        for (size_t k = 0; k < rep_len; k++)
        {
            repl_lines += (ed->text[k] == '\n');
        }

        size_t removed_lines = 0;
        for (size_t k = before; k < after; k++)
        {
            removed_lines += (src[k] == '\n');
        }

        // If the removed region has more lines than the replacement, pad with '\n'
        size_t pad_nls = (removed_lines > repl_lines) ? (removed_lines - repl_lines) : 0;
        size_t rep_padded_len = rep_len + pad_nls;

        long diff = (long)rep_padded_len - removed;

        if (diff != 0)
            out_len = (size_t)((long)out_len + diff + pad_nls);

        if (out_len > out_cap)
            out_cap = out_len;
    }

    out_cap++;
    // printf("src_len=%d, outcap=%d\n", (int)src_len, (int)out_cap);

    // check for needed polyfills
    if (polysneeded)
    {
        size_t //spread_sz = 0, import_sz = 0, class_sz = 0, for_of_sz = 0, promise_sz=0, 
            start_sz = strlen(poly_start), end_sz = strlen(poly_end);        

        polyfills *polys = &allpolys[0];
        while(polys->polyfill)
        {
            if(polysneeded & polys->flag)
            {
                polys->size=strlen(polys->polyfill);
                out_cap += polys->size;
            }
            polys=polys+1;
        }

        out_cap += start_sz + end_sz;

        REMALLOC(out, out_cap);
        ret = out;

        // check for !#/my/prog\n
        if (*src == '#' && *(src + 1) == '!')
        {
            const char *p = src;
            size_t len = 0;

            while (p && *p != '\n')
                p++;
            if (p == src)
                return NULL;
            p++;
            len = p - src;
            memcpy(out, src, len);
            src = p;
            src_len -= len;
            out += len;
            out_offset = len;
        }

        memcpy(out, poly_start, start_sz);
        out += start_sz;

        polys = &allpolys[0];
        while(polys->polyfill)
        {
            if(polysneeded & polys->flag)
            {
                memcpy(out, polys->polyfill, polys->size);
                out += polys->size;
            }
            polys=polys+1;
        }

        memcpy(out, poly_end, end_sz);
        out += end_sz;
    }
    else
    {
        REMALLOC(out, out_cap);
        ret = out;
    }

    out_len = src_len;
    memcpy(out, src, src_len);
    out[out_len] = '\0';

    /*
    first we add some:
    outlen = 100
    replen = 25
    before = 75;
    after  = 80
    removed = 5
    diff   = 20
    move to 100 (before + replen)
       from  80 (after)
       size  20 (outlen - after)
    newsize  120 (outlen + diff)

    Then we take some away
    outlen = 120
    replen = 5
    before = 50;
    after  = 75
    removed =25
    diff  = -20
    move to  55 (before + replen)
       from  75 (after)
       size  45 (outlen - after)
    newsize 100 (outlen + diff)

    end size is 100, but we need 120.  See above.
    */

    // this version should retain line numbering, unless the replacement somehow has more lines (shouldn't happen)
    for (size_t i = 0; i < e->len; i++)
    {
        Edit *ed = &e->items[i];
        size_t before = ed->start - out_offset;
        size_t after = ed->end - out_offset;

        // Byte lengths
        size_t rep_len = strlen(ed->text);
        long removed = (long)(after - before);

        // --- Count lines in replacement and in the removed section ---
        size_t repl_lines = 0;
        for (size_t k = 0; k < rep_len; k++)
        {
            repl_lines += (ed->text[k] == '\n');
        }

        size_t removed_lines = 0;
        for (size_t k = before; k < after; k++)
        {
            removed_lines += (out[k] == '\n');
        }

        // If the removed region has more lines than the replacement, pad with '\n'
        size_t pad_nls = (removed_lines > repl_lines) ? (removed_lines - repl_lines) : 0;
        size_t rep_padded_len = rep_len + pad_nls;

        long diff = (long)rep_padded_len - removed;
        size_t edlen = out_len - after;

        // Make room or close gap based on the *padded* replacement length
        if (diff != 0)
        {
            // printf("start:%lu, moving to %lu (before+rep_padded_len) from %lu (after), size=%lu (out_len-after)\n",
            //         before, before + rep_padded_len, after, edlen);
            // printf("'%s'\n", out);
            memmove(out + before + rep_padded_len, out + after, edlen);
            out_len = (size_t)((long)out_len + diff);
        }

        // Write replacement bytes
        memcpy(out + before, ed->text, rep_len);
        //printf("replaced:\n'%s'\n", out);

        // Write any newline padding to preserve original line positions
        if (pad_nls)
        {
            memset(out + before + rep_len, '\n', pad_nls);
        }

        out[out_len] = '\0';
    }

    return ret;
}




void free_edits(EditList *e)
{
    for (size_t i = 0; i < e->len; i++)
    {
        if (e->items[i].own_text && e->items[i].text)
            free(e->items[i].text);
    }
    free(e->items);
    e->items = NULL;
    e->len = e->cap = 0;
}

static inline bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Provided by tree-sitter-javascript (parser.c)
extern const TSLanguage *tree_sitter_javascript(void);

/*
// Returns the field name for `node` relative to its parent, or NULL if none.
// The returned pointer is owned by the language and remains valid for the
// lifetime of the language; do not free it.
static const char *ts_node_field_name(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent))
    {
        return NULL; // no parent => no field
    }

    const char *result = NULL;
    TSTreeCursor cursor = ts_tree_cursor_new(parent);

    if (ts_tree_cursor_goto_first_child(&cursor))
    {
        do
        {
            TSNode child = ts_tree_cursor_current_node(&cursor);
            if (ts_node_eq(child, node))
            {
                result = ts_tree_cursor_current_field_name(&cursor); // may be NULL
                break;
            }
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
    return result;
}
/* Check if node is inside a loop (for/while/do) within the same function scope. */
static int is_inside_loop(TSNode node)
{
    TSNode anc = ts_node_parent(node);
    while (!ts_node_is_null(anc))
    {
        const char *t = ts_node_type(anc);
        if (strcmp(t, "for_statement") == 0 || strcmp(t, "while_statement") == 0 ||
            strcmp(t, "do_statement") == 0 || strcmp(t, "for_in_statement") == 0)
            return 1;
        /* Stop at function boundaries */
        if (strcmp(t, "function_declaration") == 0 || strcmp(t, "function_expression") == 0 ||
            strcmp(t, "function") == 0 || strcmp(t, "arrow_function") == 0 ||
            strcmp(t, "generator_function_declaration") == 0 || strcmp(t, "generator_function") == 0 ||
            strcmp(t, "generator_function_expression") == 0 || strcmp(t, "method_definition") == 0 ||
            strcmp(t, "program") == 0)
            return 0;
        anc = ts_node_parent(anc);
    }
    return 0;
}

/* Walk the AST and emit warnings for unsupported patterns.
   This is a read-only scan — no edits, no claimed ranges. */
/* Scan AST for unsupported patterns and warn on stderr (read-only — no edits).
   Does not stop transpilation — the script still runs. */
static void warn_unsupported_patterns(TSNode root)
{
    TSTreeCursor cur = ts_tree_cursor_new(root);
    for (;;)
    {
        TSNode n = ts_tree_cursor_current_node(&cur);
        const char *nt = ts_node_type(n);

        if (strcmp(nt, "await_expression") == 0)
        {
            if (is_inside_loop(n))
            {
                TSPoint p = ts_node_start_point(n);
                fprintf(stderr, "Transpiler warning (line %u): 'await' inside a loop is not supported "
                        "and may not work correctly. Move the await outside the loop or use Promise.all().\n", p.row + 1);
            }
            else
            {
                TSNode par = ts_node_parent(n);
                if (!ts_node_is_null(par) && strcmp(ts_node_type(par), "variable_declarator") == 0)
                {
                    TSNode decl_name = ts_node_child_by_field_name(par, "name", 4);
                    if (!ts_node_is_null(decl_name))
                    {
                        const char *dnt = ts_node_type(decl_name);
                        if (strcmp(dnt, "object_pattern") == 0 || strcmp(dnt, "array_pattern") == 0)
                        {
                            TSPoint p = ts_node_start_point(n);
                            fprintf(stderr, "Transpiler warning (line %u): destructuring with 'await' "
                                    "(e.g. 'const {a, b} = await expr') is not supported. "
                                    "Use a temp variable: 'const tmp = await expr; const {a, b} = tmp;'\n", p.row + 1);
                        }
                    }
                }
            }
        }
        else if (strcmp(nt, "yield_expression") == 0)
        {
            if (is_inside_loop(n))
            {
                TSPoint p = ts_node_start_point(n);
                fprintf(stderr, "Transpiler warning (line %u): 'yield' inside a loop is not supported "
                        "and may not work correctly. Use a manual iteration pattern instead.\n", p.row + 1);
            }
        }

        if (ts_tree_cursor_goto_first_child(&cur))
            continue;
        while (!ts_tree_cursor_goto_next_sibling(&cur))
        {
            if (!ts_tree_cursor_goto_parent(&cur))
            {
                ts_tree_cursor_delete(&cur);
                return;
            }
        }
    }
}

/*
// Optional AST outline (use with --printTree)

static void print_outline(const char *src, TSNode node, int depth, FILE *f)
{
    const char *type = ts_node_type(node);
    const char *field_name = ts_node_field_name(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    int bare = !ts_node_is_named(node);

    for (int i = 0; i < depth; i++)
        fputs("  ", f);
    fprintf(f, "%s%s%s%s%s%s [%u,%u]\n", bare ? "\"" : "", type, bare ? (field_name ? "\" " : "\"") : "",
            field_name ? "(" : "", field_name ? field_name : "", field_name ? ")" : "", start, end);

    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        print_outline(src, ts_node_child(node, i), depth + 1, f);
    }
}
*/
static void print_outline(const char *src, TSNode root, int depth, FILE *f, int with_text)
{
    TSTreeCursor cur = ts_tree_cursor_new(root);
    int d = depth;

    for (;;)
    {
        TSNode node = ts_tree_cursor_current_node(&cur);

        const char *type = ts_node_type(node);
        const char *field_name = ts_tree_cursor_current_field_name(&cur); // field in parent
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        int bare = !ts_node_is_named(node);

        for (int i = 0; i < d; i++)
            fputs("  ", f);
        fprintf(f, "%s%s%s%s%s%s [%u,%u]", bare ? "\"" : "", type, bare ? (field_name ? "\" " : "\"") : "",
                field_name ? "(" : "", field_name ? field_name : "", field_name ? ")" : "", start, end);

        if (with_text)
            fprintf(f, "\x1B[31m \"%.*s\"\x1B[0m\n", end - start, src + start);
        else
            fputc('\n', f);

        // Preorder traversal using the cursor:
        if (ts_tree_cursor_goto_first_child(&cur))
        {
            d++;
            continue;
        }
        // No children; walk up until we can take a next sibling
        for (;;)
        {
            if (ts_tree_cursor_goto_next_sibling(&cur))
            {
                // same depth
                break;
            }
            if (!ts_tree_cursor_goto_parent(&cur))
            {
                // back at the root; we're done
                ts_tree_cursor_delete(&cur);
                return;
            }
            d--;
        }
    }
}

// ===================== Helper functions =====================
static TSNode find_child_type(TSNode node, const char *type, uint32_t *start)
{
    uint32_t n = ts_node_child_count(node);
    TSNode ret = {{0}};
    uint32_t curpos = (start ? *start : 0);

    for (uint32_t i = curpos; i < n; i++)
    {
        TSNode child = ts_node_child(node, i);
        const char *t = ts_node_type(child);
        if (t && strcmp(t, type) == 0)
        {
            ret = child;
            if (start)
                *start = i;
            break;
        }
    }

    return ret;
}


// === Added: default parameter lowering helpers & passes ===

static int params_has_assignment_pattern(TSNode params)
{
    if (ts_node_is_null(params))
        return 0;
    uint32_t c = ts_node_named_child_count(params);
    for (uint32_t i = 0; i < c; i++)
    {
        TSNode p = ts_node_named_child(params, i);
        if (strcmp(ts_node_type(p), "assignment_pattern") == 0)
            return 1;
    }
    return 0;
}

// Build injected "var ..." initializers from params in order using arguments[i].
// Supports identifiers and assignment_pattern with identifier on the left.
static char *build_param_default_inits(const char *src, TSNode params)
{
    uint32_t c = ts_node_named_child_count(params);
    size_t cap = 256, len = 0;
    char *buf = NULL;
    REMALLOC(buf, cap);
    if (buf)
        buf[0] = '\0';
#define APPEND_FMT(...)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        char tmp[1024];                                                                                                \
        int n = snprintf(tmp, sizeof(tmp), __VA_ARGS__);                                                               \
        if (n > 0)                                                                                                     \
        {                                                                                                              \
            if (len + (size_t)n + 1 > cap)                                                                             \
            {                                                                                                          \
                cap = (len + n + 1) * 2;                                                                               \
                REMALLOC(buf, cap);                                                                                    \
            }                                                                                                          \
            memcpy(buf + len, tmp, (size_t)n);                                                                         \
            len += (size_t)n;                                                                                          \
            buf[len] = 0;                                                                                              \
        }                                                                                                              \
    } while (0)

    for (uint32_t i = 0, pi = 0; i < c; i++, pi++)
    {
        TSNode p = ts_node_named_child(params, i);
        const char *pt = ts_node_type(p);
        if (strcmp(pt, "identifier") == 0)
        {
            size_t ns = ts_node_start_byte(p), ne = ts_node_end_byte(p);
            APPEND_FMT("var %.*s = arguments.length > %u ? arguments[%u] : undefined;", (int)(ne - ns), src + ns, pi,
                       pi);
        }
        else if (strcmp(pt, "assignment_pattern") == 0)
        {
            TSNode left = ts_node_child_by_field_name(p, "left", 4);
            TSNode right = ts_node_child_by_field_name(p, "right", 5);
            if (!ts_node_is_null(left) && !ts_node_is_null(right) && strcmp(ts_node_type(left), "identifier") == 0)
            {
                size_t ls = ts_node_start_byte(left), le = ts_node_end_byte(left);
                size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                APPEND_FMT("var %.*s = arguments.length > %u && arguments[%u] !== undefined ? arguments[%u] : %.*s;",
                           (int)(le - ls), src + ls, pi, pi, pi, (int)(re - rs), src + rs);
            }
            else
            {
                // unsupported here
                free(buf);
                return NULL;
            }
        }
        else
        {
            free(buf);
            return NULL;
        }
    }
    return buf;
}

// Lower defaults for function-like nodes (decls, expressions, generators, methods).
// Only fires when params contain at least one assignment_pattern.
static int rewrite_function_like_default_params(EditList *edits, const char *src, TSNode node, RangeList *claimed,
                                                int overlaps)
{
    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(params) || ts_node_is_null(body))
        return 0;
    if (!params_has_assignment_pattern(params))
        return 0;

    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    size_t bs = ts_node_start_byte(body);
    // if (rl_overlaps(claimed, ps, pe, "rewrite_function_like_default_params") || rl_overlaps(claimed, bs,
    // be,"rewrite_function_like_default_params"))
    //     return 0;

    char *decls = build_param_default_inits(src, params);
    if (!decls)
        return 0;

    if (overlaps)
        return 1;

    // Insert after the opening '{'
    size_t insert_at = bs + 1;
    add_edit_take_ownership(edits, insert_at, insert_at, decls, claimed);

    // Replace params list with "()"
    add_edit(edits, ps, pe, "()", claimed);

    return 1;
}

// Single-pass: convert `var f = function (…) {` → `function f() {` AND inject default initializers.

// Preserve `var f = function (…) { … }` but lower defaults:
//   var f = function() { var a = arguments[0]…; … }
static int rewrite_var_function_expression_defaults(EditList *edits, const char *src, TSNode node, RangeList *claimed,
                                                    int overlaps)
{
    if (strcmp(ts_node_type(node), "variable_declaration") != 0)
        return 0;
    if (ts_node_named_child_count(node) != 1)
        return 0;

    TSNode decl = ts_node_named_child(node, 0);
    if (strcmp(ts_node_type(decl), "variable_declarator") != 0)
        return 0;

    TSNode val = ts_node_child_by_field_name(decl, "value", 5);
    if (ts_node_is_null(val))
        return 0;

    const char *vt = ts_node_type(val);
    if (strcmp(vt, "function") != 0 && strcmp(vt, "generator_function") != 0 &&
        strcmp(vt, "function_expression") != 0 && strcmp(vt, "generator_function_expression") != 0)
        return 0;

    TSNode params = ts_node_child_by_field_name(val, "parameters", 10);
    TSNode body = ts_node_child_by_field_name(val, "body", 4);
    if (ts_node_is_null(params) || ts_node_is_null(body))
        return 0;
    if (!params_has_assignment_pattern(params))
        return 0;

    // Build initializers
    char *decls = build_param_default_inits(src, params);
    if (!decls)
        return 0;

    if (overlaps)
        return 1;

    // Replace params with "()"
    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    add_edit(edits, ps, pe, "()", claimed);

    // Insert declarations at start of body
    size_t bs = ts_node_start_byte(body);
    add_edit_take_ownership(edits, bs + 1, bs + 1, decls, claimed);

    return 1;
}

// === End added helpers/passes ===
// ============== generic helpers ==============
static int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static int slice_starts_with_paren(const char *s, size_t a, size_t b)
{
    while (a < b && is_space_char(s[a]))
        a++;
    return (a < b && s[a] == '(');
}

// Quote a JS string literal using double quotes (basic escapes).
static char *js_quote_literal(const char *src, size_t start, size_t end, int *nnl)
{
    size_t cap = (end - start) * 2 + 3;
    char *out = NULL;

    REMALLOC(out, cap);

    size_t j = 0;
    out[j++] = '"';
    *nnl = 0;

    for (size_t i = start; i < end; i++)
    {
        unsigned char c = (unsigned char)src[i];
        if (j + 6 >= cap)
        {
            cap *= 2;
            REMALLOC(out, cap);
        }
        switch (c)
        {
        case '\\':
            // out[j++] = '\\';
            out[j++] = '\\';
            break;
        case '"':
            out[j++] = '\\';
            out[j++] = '"';
            break;
        case '\n':
            out[j++] = '\\';
            out[j++] = 'n';
            (*nnl)++;
            break;
        case '\r':
            out[j++] = '\\';
            out[j++] = 'r';
            break;
        case '\t':
            out[j++] = '\\';
            out[j++] = 't';
            break;
        case '\b':
            out[j++] = '\\';
            out[j++] = 'b';
            break;
        case '\f':
            out[j++] = '\\';
            out[j++] = 'f';
            break;
        default:
            out[j++] = (char)c;
        }
    }
    out[j++] = '"';
    out[j++] = '\0';

    return out;
}

// ============== template slicing helpers ==============
typedef struct
{
    int is_expr;
    size_t start, end;
} Piece;

static void collect_template_by_offsets(TSNode tpl, Piece **lits, size_t *nl, Piece **exprs, size_t *ne)
{
    *lits = NULL;
    *exprs = NULL;
    *nl = 0;
    *ne = 0;
    size_t capL = 0, capE = 0;
    size_t tpl_start = ts_node_start_byte(tpl), tpl_end = ts_node_end_byte(tpl);
    uint32_t c = ts_node_child_count(tpl);
    size_t open_tick = tpl_start, close_tick = tpl_end;

    for (uint32_t i = 0; i < c; i++)
    {
        TSNode kid = ts_node_child(tpl, i);
        if (strcmp(ts_node_type(kid), "`") == 0)
        {
            if (open_tick == tpl_start)
                open_tick = ts_node_end_byte(kid);
            close_tick = ts_node_start_byte(kid);
        }
    }
    if (open_tick == tpl_start)
        open_tick = tpl_start + 1;
    if (close_tick == tpl_end)
        close_tick = (tpl_end > tpl_start) ? tpl_end - 1 : tpl_end;

    size_t cursor = open_tick;
    for (uint32_t i = 0; i < c; i++)
    {
        TSNode kid = ts_node_child(tpl, i);
        if (strcmp(ts_node_type(kid), "template_substitution") == 0)
        {
            size_t sub_s = ts_node_start_byte(kid);

            if (sub_s > cursor)
            { // literal before ${...}
                if (*nl == capL)
                {
                    capL = capL ? capL * 2 : 8;
                    REMALLOC(*lits, capL * sizeof(Piece));
                }
                (*lits)[(*nl)++] = (Piece){0, cursor, sub_s};
            }
            uint32_t nexp = ts_node_child_count(kid);
            TSNode expr = ts_node_named_child(kid, 0);
            TSNode lexpr = expr;

            // get all text in expression, even if one is ERROR
            // i.e. ${%s:myvar} -> rampart.utils.printf (see below)
            for (uint32_t i = 1; i < nexp; i++)
            {
                TSNode t = ts_node_named_child(kid, i);
                if (!ts_node_is_null(t))
                    lexpr = t;
            }

            if (!ts_node_is_null(expr))
            {

                size_t es = ts_node_start_byte(expr), ee = ts_node_end_byte(lexpr);
                if (*ne == capE)
                {
                    capE = capE ? capE * 2 : 8;
                    REMALLOC(*exprs, capE * sizeof(Piece));
                }
                (*exprs)[(*ne)++] = (Piece){1, es, ee};
            }

            cursor = ts_node_end_byte(kid);
        }
    }
    if (close_tick > cursor)
    { // trailing literal
        if (*nl == capL)
        {
            capL = capL ? capL * 2 : 8;
            REMALLOC(*lits, capL * sizeof(Piece));
        }
        (*lits)[(*nl)++] = (Piece){0, cursor, close_tick};
    }
    if (*nl == 0)
    { // ensure at least 1 slot
        if (*nl == capL)
        {
            capL = capL ? capL * 2 : 8;
            REMALLOC(*lits, capL * sizeof(Piece));
        }
        (*lits)[(*nl)++] = (Piece){0, open_tick, open_tick};
    }
}

// nearest previous named sibling
static TSNode prev_named_sibling(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent))
        return (TSNode){0};
    uint32_t n = ts_node_child_count(parent);
    TSNode prev = (TSNode){0};
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode kid = ts_node_child(parent, i);
        if (ts_node_eq(kid, node))
            return prev;
        if (ts_node_is_named(kid))
            prev = kid;
    }
    return (TSNode){0};
}

// ============== arrow destructuring helpers ==============
typedef struct
{
    char *name;
    char *repl;
    char *defval;  // default value expression (NULL if none)
} Binding;
typedef struct
{
    Binding *a;
    size_t len, cap;
} Bindings;

static void binds_init(Bindings *b)
{
    b->a = NULL;
    b->len = 0;
    b->cap = 0;
}
static void binds_add_def(Bindings *b, const char *name, size_t nlen, const char *repl, const char *defval)
{
    if (b->len == b->cap)
    {
        size_t nc = b->cap ? b->cap * 2 : 8;
        REMALLOC(b->a, nc * sizeof(Binding));
        b->cap = nc;
    }

    b->a[b->len].name = NULL;
    REMALLOC(b->a[b->len].name, nlen + 1);
    memcpy(b->a[b->len].name, name, nlen);
    b->a[b->len].name[nlen] = '\0';
    b->a[b->len].repl = strdup(repl);
    b->a[b->len].defval = defval ? strdup(defval) : NULL;
    b->len++;
}
static void binds_add(Bindings *b, const char *name, size_t nlen, const char *repl)
{
    binds_add_def(b, name, nlen, repl, NULL);
}
static void binds_free(Bindings *b)
{
    for (size_t i = 0; i < b->len; i++)
    {
        free(b->a[i].name);
        free(b->a[i].repl);
        if (b->a[i].defval)
            free(b->a[i].defval);
    }
    free(b->a);
    b->a = NULL;
    b->len = b->cap = 0;
}

static int collect_flat_destructure_bindings(TSNode pattern, const char *src, const char *base, Bindings *out)
{
    const char *pt = ts_node_type(pattern);
    if (strcmp(pt, "array_pattern") == 0)
    {
        uint32_t c = ts_node_child_count(pattern);
        int idx = 0;
        int last_was_comma_or_open = 1; // at start, as if after '['
        for (uint32_t i = 0; i < c; i++)
        {
            TSNode k = ts_node_child(pattern, i);
            if (!ts_node_is_named(k))
            {
                const char *ktok = ts_node_type(k);
                if (strcmp(ktok, ",") == 0)
                {
                    if (last_was_comma_or_open)
                        idx++; // hole/elision
                    last_was_comma_or_open = 1;
                }
                else if (strcmp(ktok, "[") == 0)
                {
                    last_was_comma_or_open = 1;
                }
                else
                {
                    // ']' or other punct
                }
                continue;
            }
            const char *kt = ts_node_type(k);
            if (strcmp(kt, "identifier") == 0)
            {
                size_t ns = ts_node_start_byte(k), ne = ts_node_end_byte(k);
                char buf[64];
                snprintf(buf, sizeof(buf), "%s[%d]", base, idx);
                binds_add(out, src + ns, ne - ns, buf);
                idx++;
                last_was_comma_or_open = 0;
            }
            else if (strcmp(kt, "assignment_pattern") == 0)
            {
                TSNode left = ts_node_child_by_field_name(k, "left", 4);
                TSNode right = ts_node_child_by_field_name(k, "right", 5);
                if (!ts_node_is_null(left) && strcmp(ts_node_type(left), "identifier") == 0)
                {
                    size_t ns = ts_node_start_byte(left), ne = ts_node_end_byte(left);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s[%d]", base, idx);
                    char *dv = NULL;
                    if (!ts_node_is_null(right))
                    {
                        size_t ds = ts_node_start_byte(right), de = ts_node_end_byte(right);
                        dv = (char *)malloc(de - ds + 1);
                        memcpy(dv, src + ds, de - ds);
                        dv[de - ds] = '\0';
                    }
                    binds_add_def(out, src + ns, ne - ns, buf, dv);
                    if (dv) free(dv);
                }
                idx++;
                last_was_comma_or_open = 0;
            }
            else if (strcmp(kt, "rest_pattern") == 0)
            {
                /* [...rest] → Array.prototype.slice.call(base, idx) */
                TSNode child = ts_node_named_child(k, 0);
                if (!ts_node_is_null(child) && strcmp(ts_node_type(child), "identifier") == 0)
                {
                    size_t ns = ts_node_start_byte(child), ne = ts_node_end_byte(child);
                    char *repl = NULL;
                    REMALLOC(repl, strlen(base) + 64);
                    sprintf(repl, "Array.prototype.slice.call(%s, %d)", base, idx);
                    binds_add(out, src + ns, ne - ns, repl);
                    free(repl);
                }
                last_was_comma_or_open = 0;
            }
            else if (strcmp(kt, "object_pattern") == 0 || strcmp(kt, "array_pattern") == 0)
            {
                /* nested pattern in array element */
                char buf[64];
                snprintf(buf, sizeof(buf), "%s[%d]", base, idx);
                if (!collect_flat_destructure_bindings(k, src, buf, out))
                    return 0;
                idx++;
                last_was_comma_or_open = 0;
            }
            else
            {
                return 0;
            }
        }
        return 1;
    }
    else if (strcmp(pt, "object_pattern") == 0)
    {
        uint32_t c = ts_node_child_count(pattern);
        /* Track extracted keys for rest pattern support */
        rp_string *extracted_keys = rp_string_new(64);
        int nkeys = 0;

        for (uint32_t i = 0; i < c; i++)
        {
            TSNode k = ts_node_child(pattern, i);
            if (!ts_node_is_named(k))
                continue;
            const char *kt = ts_node_type(k);
            if (strcmp(kt, "pair_pattern") == 0 || strcmp(kt, "pair") == 0)
            {
                TSNode key = ts_node_child_by_field_name(k, "key", 3);
                TSNode val = ts_node_child_by_field_name(k, "value", 5);
                if (ts_node_is_null(key) || ts_node_is_null(val))
                {
                    rp_string_free(extracted_keys);
                    return 0;
                }
                const char *keytype = ts_node_type(key);
                size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
                size_t klen = ke - ks;
                char *nested_base = NULL;

                if (strcmp(keytype, "computed_property_name") == 0)
                {
                    /* {[expr]: name} — computed key */
                    /* The expression is inside brackets: [expr] — extract the inner expr */
                    TSNode inner = ts_node_named_child(key, 0);
                    if (ts_node_is_null(inner))
                    {
                        rp_string_free(extracted_keys);
                        return 0;
                    }
                    size_t is = ts_node_start_byte(inner), ie = ts_node_end_byte(inner);
                    REMALLOC(nested_base, strlen(base) + 1 + (ie - is) + 2);
                    sprintf(nested_base, "%s[%.*s]", base, (int)(ie - is), src + is);
                    /* Track computed key for rest exclusion */
                    if (nkeys > 0) rp_string_puts(extracted_keys, ", ");
                    rp_string_putsn(extracted_keys, src + is, ie - is);
                    nkeys++;
                }
                else if (strcmp(keytype, "property_identifier") == 0 ||
                         strcmp(keytype, "identifier") == 0)
                {
                    REMALLOC(nested_base, strlen(base) + 1 + klen + 1);
                    sprintf(nested_base, "%s.%.*s", base, (int)klen, src + ks);
                    /* Track key for rest exclusion */
                    if (nkeys > 0) rp_string_puts(extracted_keys, ", ");
                    rp_string_puts(extracted_keys, "\"");
                    rp_string_putsn(extracted_keys, src + ks, klen);
                    rp_string_puts(extracted_keys, "\"");
                    nkeys++;
                }
                else
                {
                    rp_string_free(extracted_keys);
                    return 0;
                }

                const char *vt = ts_node_type(val);
                if (strcmp(vt, "identifier") == 0)
                {
                    size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
                    binds_add(out, src + vs, ve - vs, nested_base);
                }
                else if (strcmp(vt, "object_pattern") == 0 || strcmp(vt, "array_pattern") == 0)
                {
                    if (!collect_flat_destructure_bindings(val, src, nested_base, out))
                    {
                        free(nested_base);
                        rp_string_free(extracted_keys);
                        return 0;
                    }
                }
                else if (strcmp(vt, "assignment_pattern") == 0)
                {
                    /* rename + default: {x: rx = 5} — val is assignment_pattern */
                    TSNode aleft = ts_node_child_by_field_name(val, "left", 4);
                    TSNode aright = ts_node_child_by_field_name(val, "right", 5);
                    if (!ts_node_is_null(aleft) && strcmp(ts_node_type(aleft), "identifier") == 0)
                    {
                        size_t als = ts_node_start_byte(aleft), ale = ts_node_end_byte(aleft);
                        char *dv = NULL;
                        if (!ts_node_is_null(aright))
                        {
                            size_t ds = ts_node_start_byte(aright), de = ts_node_end_byte(aright);
                            dv = (char *)malloc(de - ds + 1);
                            memcpy(dv, src + ds, de - ds);
                            dv[de - ds] = '\0';
                        }
                        binds_add_def(out, src + als, ale - als, nested_base, dv);
                        if (dv) free(dv);
                    }
                    else if (!ts_node_is_null(aleft) &&
                             (strcmp(ts_node_type(aleft), "object_pattern") == 0 ||
                              strcmp(ts_node_type(aleft), "array_pattern") == 0))
                    {
                        /* {a: {b: x} = {}} — nested pattern with intermediate default */
                        char *dv = NULL;
                        if (!ts_node_is_null(aright))
                        {
                            size_t ds = ts_node_start_byte(aright), de = ts_node_end_byte(aright);
                            dv = (char *)malloc(de - ds + 1);
                            memcpy(dv, src + ds, de - ds);
                            dv[de - ds] = '\0';
                        }
                        /* Use temp expression: (base.key !== undefined ? base.key : default) */
                        char *safe_base = NULL;
                        if (dv)
                        {
                            REMALLOC(safe_base, strlen(nested_base) * 2 + strlen(dv) + 32);
                            sprintf(safe_base, "(%s !== undefined ? %s : %s)", nested_base, nested_base, dv);
                            free(dv);
                        }
                        else
                        {
                            safe_base = strdup(nested_base);
                        }
                        if (!collect_flat_destructure_bindings(aleft, src, safe_base, out))
                        {
                            free(safe_base);
                            free(nested_base);
                            rp_string_free(extracted_keys);
                            return 0;
                        }
                        free(safe_base);
                    }
                    else
                    {
                        free(nested_base);
                        rp_string_free(extracted_keys);
                        return 0;
                    }
                }
                else
                {
                    free(nested_base);
                    rp_string_free(extracted_keys);
                    return 0;
                }
                free(nested_base);
            }
            else if (strcmp(kt, "shorthand_property_identifier_pattern") == 0 ||
                     strcmp(kt, "shorthand_property_identifier") == 0)
            {
                size_t ns = ts_node_start_byte(k), ne = ts_node_end_byte(k);
                size_t nlen = ne - ns;
                char *repl = NULL;
                REMALLOC(repl, strlen(base) + 1 + nlen + 1);
                sprintf(repl, "%s.%.*s", base, (int)nlen, src + ns);
                binds_add(out, src + ns, nlen, repl);
                free(repl);
                /* Track key for rest exclusion */
                if (nkeys > 0) rp_string_puts(extracted_keys, ", ");
                rp_string_puts(extracted_keys, "\"");
                rp_string_putsn(extracted_keys, src + ns, nlen);
                rp_string_puts(extracted_keys, "\"");
                nkeys++;
            }
            else if (strcmp(kt, "object_assignment_pattern") == 0 ||
                     strcmp(kt, "assignment_pattern") == 0)
            {
                // { b = 2 } — shorthand with default
                TSNode left = ts_node_child_by_field_name(k, "left", 4);
                TSNode right = ts_node_child_by_field_name(k, "right", 5);
                if (ts_node_is_null(left))
                {
                    rp_string_free(extracted_keys);
                    return 0;
                }
                const char *lt = ts_node_type(left);
                if (strcmp(lt, "shorthand_property_identifier_pattern") != 0 &&
                    strcmp(lt, "shorthand_property_identifier") != 0 &&
                    strcmp(lt, "identifier") != 0)
                {
                    rp_string_free(extracted_keys);
                    return 0;
                }
                size_t ns = ts_node_start_byte(left), ne = ts_node_end_byte(left);
                size_t nlen = ne - ns;
                char *repl = NULL;
                REMALLOC(repl, strlen(base) + 1 + nlen + 1);
                sprintf(repl, "%s.%.*s", base, (int)nlen, src + ns);
                char *dv = NULL;
                if (!ts_node_is_null(right))
                {
                    size_t ds = ts_node_start_byte(right), de = ts_node_end_byte(right);
                    dv = (char *)malloc(de - ds + 1);
                    memcpy(dv, src + ds, de - ds);
                    dv[de - ds] = '\0';
                }
                binds_add_def(out, src + ns, nlen, repl, dv);
                free(repl);
                if (dv) free(dv);
                /* Track key for rest exclusion */
                if (nkeys > 0) rp_string_puts(extracted_keys, ", ");
                rp_string_puts(extracted_keys, "\"");
                rp_string_putsn(extracted_keys, src + ns, nlen);
                rp_string_puts(extracted_keys, "\"");
                nkeys++;
            }
            else if (strcmp(kt, "rest_pattern") == 0)
            {
                /* {...rest} — collect all properties not already extracted */
                TSNode child = ts_node_named_child(k, 0);
                if (!ts_node_is_null(child) && strcmp(ts_node_type(child), "identifier") == 0)
                {
                    size_t ns = ts_node_start_byte(child), ne = ts_node_end_byte(child);
                    /* Generate: (function(s,e){var r={};for(var k in s)if(Object.prototype.hasOwnProperty.call(s,k)&&e.indexOf(k)<0)r[k]=s[k];return r;})(base, [keys]) */
                    rp_string *repl = rp_string_new(128);
                    rp_string_puts(repl, "(function(s,e){var r={};for(var k in s)if(Object.prototype.hasOwnProperty.call(s,k)&&e.indexOf(k)<0)r[k]=s[k];return r;})(");
                    rp_string_puts(repl, base);
                    rp_string_puts(repl, ",[");
                    if (extracted_keys->str)
                        rp_string_putsn(repl, extracted_keys->str, extracted_keys->len);
                    rp_string_puts(repl, "])");
                    binds_add(out, src + ns, ne - ns, repl->str);
                    rp_string_free(repl);
                }
            }
            else
            {
                rp_string_free(extracted_keys);
                return 0;
            }
        }
        rp_string_free(extracted_keys);
        return 1;
    }
    return 0;
}

// ============== general destructuring (declarations + assignments) ==============

static unsigned _destr_counter = 0;

// Rewrite: var [a, , b] = expr;  ->  var _d = expr; var a = _d[0]; var b = _d[2];
// Rewrite: var {x, y} = expr;    ->  var _d = expr; var x = _d.x; var y = _d.y;
static int rewrite_destructuring_declaration(EditList *edits, const char *src, TSNode node, RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(node), "variable_declaration") != 0)
        return 0;

    // Check each declarator for destructuring patterns
    uint32_t dc = ts_node_named_child_count(node);
    for (uint32_t di = 0; di < dc; di++)
    {
        TSNode decl = ts_node_named_child(node, di);
        if (strcmp(ts_node_type(decl), "variable_declarator") != 0)
            continue;
        TSNode name = ts_node_child_by_field_name(decl, "name", 4);
        TSNode val  = ts_node_child_by_field_name(decl, "value", 5);
        if (ts_node_is_null(name) || ts_node_is_null(val))
            continue;
        const char *nt = ts_node_type(name);
        if (strcmp(nt, "array_pattern") != 0 && strcmp(nt, "object_pattern") != 0)
            continue;

        // We have a destructuring declaration
        if (overlaps)
            return 1;

        char tmpvar[32];
        snprintf(tmpvar, sizeof(tmpvar), "_d%u", ++_destr_counter);

        Bindings binds;
        binds_init(&binds);
        if (!collect_flat_destructure_bindings(name, src, tmpvar, &binds))
        {
            binds_free(&binds);
            return 0;
        }

        size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
        size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);

        rp_string *out = rp_string_new(256);
        rp_string_puts(out, "var ");
        rp_string_puts(out, tmpvar);
        rp_string_puts(out, " = ");
        rp_string_putsn(out, src + vs, ve - vs);
        rp_string_puts(out, "; ");

        for (size_t i = 0; i < binds.len; i++)
        {
            rp_string_puts(out, "var ");
            rp_string_puts(out, binds.a[i].name);
            rp_string_puts(out, " = ");
            if (binds.a[i].defval)
            {
                rp_string_puts(out, binds.a[i].repl);
                rp_string_puts(out, " !== undefined ? ");
                rp_string_puts(out, binds.a[i].repl);
                rp_string_puts(out, " : ");
                rp_string_puts(out, binds.a[i].defval);
            }
            else
            {
                rp_string_puts(out, binds.a[i].repl);
            }
            rp_string_puts(out, "; ");
        }

        binds_free(&binds);
        add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
        out = rp_string_free(out);
        return 1;
    }
    return 0;
}

// Rewrite: [b, a] = [a, b];  ->  var _d = [a, b]; b = _d[0]; a = _d[1];
static int rewrite_destructuring_assignment(EditList *edits, const char *src, TSNode node, RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(node), "expression_statement") != 0)
        return 0;

    TSNode expr = ts_node_named_child(node, 0);
    if (ts_node_is_null(expr))
        return 0;
    /* Handle ({a, b} = obj) — parenthesized destructuring assignment */
    if (strcmp(ts_node_type(expr), "parenthesized_expression") == 0)
    {
        TSNode inner = ts_node_named_child(expr, 0);
        if (!ts_node_is_null(inner) && strcmp(ts_node_type(inner), "assignment_expression") == 0)
            expr = inner;
    }
    if (strcmp(ts_node_type(expr), "assignment_expression") != 0)
        return 0;

    TSNode left = ts_node_child_by_field_name(expr, "left", 4);
    TSNode right = ts_node_child_by_field_name(expr, "right", 5);
    if (ts_node_is_null(left) || ts_node_is_null(right))
        return 0;

    const char *lt = ts_node_type(left);
    if (strcmp(lt, "array_pattern") != 0 && strcmp(lt, "object_pattern") != 0)
        return 0;

    if (overlaps)
        return 1;

    char tmpvar[32];
    snprintf(tmpvar, sizeof(tmpvar), "_d%u", ++_destr_counter);

    Bindings binds;
    binds_init(&binds);
    if (!collect_flat_destructure_bindings(left, src, tmpvar, &binds))
    {
        binds_free(&binds);
        return 0;
    }

    size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);

    rp_string *out = rp_string_new(256);
    rp_string_puts(out, "var ");
    rp_string_puts(out, tmpvar);
    rp_string_puts(out, " = ");
    rp_string_putsn(out, src + rs, re - rs);
    rp_string_puts(out, "; ");

    for (size_t i = 0; i < binds.len; i++)
    {
        rp_string_puts(out, binds.a[i].name);
        rp_string_puts(out, " = ");
        if (binds.a[i].defval)
        {
            rp_string_puts(out, binds.a[i].repl);
            rp_string_puts(out, " !== undefined ? ");
            rp_string_puts(out, binds.a[i].repl);
            rp_string_puts(out, " : ");
            rp_string_puts(out, binds.a[i].defval);
        }
        else
        {
            rp_string_puts(out, binds.a[i].repl);
        }
        rp_string_puts(out, "; ");
    }

    binds_free(&binds);
    add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    return 1;
}

static char *rewrite_concise_body_with_bindings(const char *src, TSNode expr, const Bindings *b, RangeList *claimed)
{
    size_t es = ts_node_start_byte(expr), ee = ts_node_end_byte(expr);
    EditList tmp;
    init_edits(&tmp);
    TSTreeCursor c = ts_tree_cursor_new(expr);
    for (;;)
    {
        TSNode n = ts_tree_cursor_current_node(&c);
        if (strcmp(ts_node_type(n), "identifier") == 0)
        {
            size_t ns = ts_node_start_byte(n), ne = ts_node_end_byte(n), nlen = ne - ns;
            for (size_t i = 0; i < b->len; i++)
            {
                size_t blen = strlen(b->a[i].name);
                if (nlen == blen && strncmp(src + ns, b->a[i].name, nlen) == 0)
                {
                    if (b->a[i].defval)
                    {
                        /* Emit (repl !== undefined ? repl : defval) */
                        rp_string *dout = rp_string_new(64);
                        rp_string_puts(dout, "(");
                        rp_string_puts(dout, b->a[i].repl);
                        rp_string_puts(dout, " !== undefined ? ");
                        rp_string_puts(dout, b->a[i].repl);
                        rp_string_puts(dout, " : ");
                        rp_string_puts(dout, b->a[i].defval);
                        rp_string_puts(dout, ")");
                        add_edit_take_ownership(&tmp, ns - es, ne - es, rp_string_steal(dout), claimed);
                        dout = rp_string_free(dout);
                    }
                    else
                    {
                        add_edit(&tmp, ns - es, ne - es, b->a[i].repl, claimed);
                    }
                    break;
                }
            }
        }
        if (ts_tree_cursor_goto_first_child(&c))
            continue;
        while (!ts_tree_cursor_goto_next_sibling(&c))
        {
            if (!ts_tree_cursor_goto_parent(&c))
            {
                ts_tree_cursor_delete(&c);
                goto APPLY;
            }
        }
    }
APPLY:;
    char *slice = NULL;

    REMALLOC(slice, ee - es + 1);
    memcpy(slice, src + es, ee - es);
    slice[ee - es] = '\0';
    char *out = apply_edits(slice, ee - es, &tmp, 0);
    free(slice);
    free_edits(&tmp);
    return out;
}

/* helpers for exports below */

static char *dup_range(const char *s, size_t a, size_t b)
{
    if (b < a)
    {
        b = a;
    }
    size_t n = b - a;
    char *r = (char *)malloc(n + 1);
    if (!r)
    {
        return NULL;
    }
    memcpy(r, s + a, n);
    r[n] = '\0';
    return r;
}

/* append exports for a CSV of identifiers */
static void append_exports_for_csv(rp_string *out, const char *csv)
{
    const char *p = csv;
    while (p && *p)
    {
        const char *q = strchr(p, ',');
        const char *e = q ? q : (p + strlen(p));
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
        while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
            e--;
        if (e > p)
        {
            rp_string_puts(out, " exports.");
            rp_string_putsn(out, p, (size_t)(e - p));
            rp_string_puts(out, " = ");
            rp_string_putsn(out, p, (size_t)(e - p));
            rp_string_putc(out, ';');
        }
        p = q ? (q + 1) : NULL;
    }
}

/* collect bound identifiers from binding patterns (object/array/default/rest) */
static void collect_pattern_names(TSNode node, const char *src, rp_string *csv)
{
    const char *t = ts_node_type(node);

    if (strcmp(t, "identifier") == 0 || strcmp(t, "shorthand_property_identifier_pattern") == 0)
    {
        if (csv->len)
        {
            rp_string_putc(csv, ',');
        }
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        rp_string_putsn(csv, src + s, e - s);
        return;
    }
    if (strcmp(t, "rest_pattern") == 0)
    {
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++)
        {
            collect_pattern_names(ts_node_named_child(node, i), src, csv);
        }
        return;
    }
    if (strcmp(t, "assignment_pattern") == 0 || strcmp(t, "object_assignment_pattern") == 0)
    {
        TSNode left = ts_node_child_by_field_name(node, "left", 4);
        if (ts_node_is_null(left) && ts_node_named_child_count(node) > 0)
        {
            left = ts_node_named_child(node, 0);
        }
        if (!ts_node_is_null(left))
        {
            collect_pattern_names(left, src, csv);
        }
        return;
    }
    if (strcmp(t, "object_pattern") == 0)
    {
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++)
        {
            TSNode ch = ts_node_named_child(node, i);
            const char *ct = ts_node_type(ch);
            if (strcmp(ct, "pair_pattern") == 0 || strcmp(ct, "pair") == 0)
            {
                TSNode val = ts_node_child_by_field_name(ch, "value", 5);
                if (ts_node_is_null(val) && ts_node_named_child_count(ch) > 1)
                {
                    val = ts_node_named_child(ch, ts_node_named_child_count(ch) - 1);
                }
                if (!ts_node_is_null(val))
                {
                    collect_pattern_names(val, src, csv);
                }
            }
            else
            {
                collect_pattern_names(ch, src, csv);
            }
        }
        return;
    }
    if (strcmp(t, "array_pattern") == 0)
    {
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++)
        {
            collect_pattern_names(ts_node_named_child(node, i), src, csv);
        }
        return;
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        collect_pattern_names(ts_node_named_child(node, i), src, csv);
    }
}

/* for object rest: append excluded top-level key names to a CSV of quoted strings */
static void append_excluded_key_node(rp_string *excluded_csv, TSNode prop, const char *src)
{
    const char *pt = ts_node_type(prop);

    if (strcmp(pt, "pair_pattern") == 0 || strcmp(pt, "pair") == 0)
    {
        TSNode key = ts_node_child_by_field_name(prop, "key", 3);
        if (!ts_node_is_null(key))
        {
            if (excluded_csv->len)
            {
                rp_string_putc(excluded_csv, ',');
            }
            size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
            rp_string_putc(excluded_csv, '"');
            rp_string_putsn(excluded_csv, src + ks, ke - ks);
            rp_string_putc(excluded_csv, '"');
        }
        return;
    }
    if (strcmp(pt, "shorthand_property_identifier_pattern") == 0)
    {
        if (excluded_csv->len)
        {
            rp_string_putc(excluded_csv, ',');
        }
        size_t ks = ts_node_start_byte(prop), ke = ts_node_end_byte(prop);
        rp_string_putc(excluded_csv, '"');
        rp_string_putsn(excluded_csv, src + ks, ke - ks);
        rp_string_putc(excluded_csv, '"');
        return;
    }
    if (strcmp(pt, "object_assignment_pattern") == 0)
    {
        TSNode left = ts_node_child_by_field_name(prop, "left", 4);
        if (!ts_node_is_null(left))
        {
            if (excluded_csv->len)
            {
                rp_string_puts(excluded_csv, ",");
            }
            size_t ks = ts_node_start_byte(left), ke = ts_node_end_byte(left);
            rp_string_putc(excluded_csv, '"');
            rp_string_putsn(excluded_csv, src + ks, ke - ks);
            rp_string_putc(excluded_csv, '"');
        }
        return;
    }
}

/* ============================  export rewriter  ============================ */
static int rewrite_export_node(EditList *edits, const char *src, TSNode snode, RangeList *claimed, int overlaps)
{
    size_t ns = ts_node_start_byte(snode), ne = ts_node_end_byte(snode);
    int is_default_export = !ts_node_is_null(ts_node_child_by_field_name(snode, "default", 7));
    /* export default */
    {
        TSNode val =
            ts_node_child_by_field_name(snode, "value", 5); /* e.g., identifier, call, object, function expr, etc. */
        TSNode decl =
            ts_node_child_by_field_name(snode, "declaration", 11); /* named function_declaration / class_declaration */

        /* Case A: export default function f() {}  OR  export default class C {}  (named decl) */
        if (!ts_node_is_null(decl))
        {
            const char *dt = ts_node_type(decl);
            if (strcmp(dt, "function_declaration") == 0 || strcmp(dt, "class_declaration") == 0)
            {

                size_t stmt_s = ts_node_start_byte(snode); /* start of 'export' */
                size_t decl_s = ts_node_start_byte(decl);  /* start of 'function' */
                size_t decl_e = ts_node_end_byte(decl);    /* end of function decl */

                /* Append after the declaration */
                TSNode id = ts_node_child_by_field_name(decl, "name", 4);
                if (!ts_node_is_null(id))
                {
                    if (overlaps)
                        return 1;
                    size_t is = ts_node_start_byte(id), ie = ts_node_end_byte(id);
                    rp_string *post = rp_string_new(32);
                    if (is_default_export)
                    {
                        /* default: function f(...){} ; module.exports = f; */
                        rp_string_puts(post, "; module.exports = ");
                        rp_string_putsn(post, src + is, ie - is);
                        rp_string_puts(post, ";");
                    }
                    else
                    {
                        /* named: function f(...){} ; exports.f = f; */
                        rp_string_puts(post, "; exports.");
                        rp_string_putsn(post, src + is, ie - is);
                        rp_string_puts(post, " = ");
                        rp_string_putsn(post, src + is, ie - is);
                        rp_string_puts(post, ";");
                    }
                    add_edit_take_ownership(edits, decl_e, decl_e, rp_string_steal(post), claimed); /* insert AFTER function */
                    rp_string_free(post);

                    /* Remove only the 'export default' prefix */
                    add_edit(edits, stmt_s, decl_s, "", claimed);
                    return 1;
                }
                /* anonymous fn/class as default falls through to Case C */
            }
        }

        /* Case B: export default <Identifier>;  =>  module.exports = <Identifier>; */
        if (!ts_node_is_null(val) && strcmp(ts_node_type(val), "identifier") == 0)
        {
            if (overlaps)
                return 1;
            size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
            rp_string *out = rp_string_new(64);
            rp_string_puts(out, "exports.__esModule=true;exports.default = ");
            rp_string_putsn(out, src + vs, ve - vs);
            rp_string_puts(out, ";");
            add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed); /* replace whole export statement */
            rp_string_free(out);
            return 1;
        }

        /* Case C: export default <any non-identifier expression or anonymous fn/class>;
           => var __default__ = <expr>; module.exports = __default__; */
        if (!ts_node_is_null(val))
        {
            if (overlaps)
                return 1;
            size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
            char *body = dup_range(src, vs, ve);
            rp_string *out = rp_string_new(64);
            rp_string_puts(out, "var __default__ = ");
            rp_string_puts(out, body);
            rp_string_puts(out, "; exports.default = __default__;");
            add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed); /* replace whole export statement */
            rp_string_free(out);
            free(body);
            return 1;
        }
    }

    /* export declaration */
    TSNode decl = ts_node_child_by_field_name(snode, "declaration", 11);
    if (!ts_node_is_null(decl))
    {
        const char *dt = ts_node_type(decl);

        /* export function f(...) { ... }
           Write: exports.f = f; function f(...) { ... } */
        if (strcmp(dt, "function_declaration") == 0)
        {
            if (overlaps)
                return 1;
            size_t stmt_s = ts_node_start_byte(snode); // start of `export ...`
            size_t decl_s = ts_node_start_byte(decl);  // start of `function`

            // Insert "exports.f = f; " BEFORE the function
            TSNode id = ts_node_child_by_field_name(decl, "name", 4);
            if (!ts_node_is_null(id))
            {
                size_t is = ts_node_start_byte(id), ie = ts_node_end_byte(id);
                rp_string *pre = rp_string_new(64);
                rp_string_puts(pre, "exports.");
                rp_string_putsn(pre, src + is, ie - is);
                rp_string_puts(pre, " = ");
                rp_string_putsn(pre, src + is, ie - is);
                rp_string_puts(pre, "; ");
                add_edit_take_ownership(edits, decl_s, decl_s, rp_string_steal(pre), claimed); // insertion
                rp_string_free(pre);
            }

            // Remove just the 'export' token and following space(s)
            add_edit(edits, stmt_s, decl_s, "", claimed);

            return 1;
        }

        /* export class C { ... }
           Write: exports.C = C; class C { ... }   */
        if (strcmp(dt, "class_declaration") == 0)
        {
            if (overlaps)
                return 1;
            size_t stmt_s = ts_node_start_byte(snode); // start of `export ...`
            size_t decl_s = ts_node_start_byte(decl);  // start of `class`

            // Insert "exports.C = C; " BEFORE the class
            TSNode id = ts_node_child_by_field_name(decl, "name", 4);
            if (!ts_node_is_null(id))
            {
                size_t is = ts_node_start_byte(id), ie = ts_node_end_byte(id);
                rp_string *pre = rp_string_new(64);
                rp_string_puts(pre, "exports.");
                rp_string_putsn(pre, src + is, ie - is);
                rp_string_puts(pre, " = ");
                rp_string_putsn(pre, src + is, ie - is);
                rp_string_puts(pre, "; ");
                add_edit(edits, decl_s, decl_s, rp_string_steal(pre), claimed); // insertion
                rp_string_free(pre);
            }

            // Remove just the 'export' token and following space(s)
            add_edit(edits, stmt_s, decl_s, "", claimed);

            return 1;
        }

        /* export const|let … (lexical_declaration) — handles object/array patterns */
        if (strcmp(dt, "lexical_declaration") == 0)
        {
            if (overlaps)
                return 1;
            uint32_t ndecls = ts_node_named_child_count(decl);
            if (ndecls == 1)
            {
                TSNode d = ts_node_named_child(decl, 0);
                if (strcmp(ts_node_type(d), "variable_declarator") == 0)
                {
                    TSNode nameNode = ts_node_child_by_field_name(d, "name", 4);
                    TSNode initNode = ts_node_child_by_field_name(d, "value", 5);
                    if (!ts_node_is_null(nameNode) && !ts_node_is_null(initNode))
                    {
                        const char *nameType = ts_node_type(nameNode);

                        /* ---------- object pattern ---------- */
                        if (strcmp(nameType, "object_pattern") == 0)
                        {
                            if (overlaps)
                                return 1;
                            size_t vs = ts_node_start_byte(initNode), ve = ts_node_end_byte(initNode);
                            char *val = dup_range(src, vs, ve);

                            rp_string *out = rp_string_new(512);
                            rp_string *names_csv = rp_string_new(64);
                            rp_string *excl = rp_string_new(64);

                            rp_string_puts(out, "var __tmpD0 = ");
                            rp_string_puts(out, val);
                            rp_string_puts(out, ";");

                            uint32_t nprops = ts_node_named_child_count(nameNode);
                            for (uint32_t i = 0; i < nprops; i++)
                            {
                                TSNode prop = ts_node_named_child(nameNode, i);
                                const char *pt = ts_node_type(prop);
                                if (strcmp(pt, "rest_pattern") == 0)
                                    continue;

                                append_excluded_key_node(excl, prop, src);

                                if (strcmp(pt, "pair_pattern") == 0 || strcmp(pt, "pair") == 0)
                                {
                                    TSNode key = ts_node_child_by_field_name(prop, "key", 3);
                                    TSNode valpat = ts_node_child_by_field_name(prop, "value", 5);
                                    if (ts_node_is_null(key) || ts_node_is_null(valpat))
                                        continue;
                                    const char *vt = ts_node_type(valpat);

                                    if (strcmp(vt, "identifier") == 0)
                                    {
                                        size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
                                        size_t is = ts_node_start_byte(valpat), ie = ts_node_end_byte(valpat);
                                        rp_string_puts(out, " var ");
                                        rp_string_putsn(out, src + is, ie - is);
                                        rp_string_puts(out, " = __tmpD0.");
                                        rp_string_putsn(out, src + ks, ke - ks);
                                        rp_string_puts(out, ";");
                                        if (names_csv->len)
                                        {
                                            rp_string_putc(names_csv, ',');
                                        }
                                        rp_string_putsn(names_csv, src + is, ie - is);
                                    }
                                    else if (strcmp(vt, "object_pattern") == 0)
                                    {
                                        rp_string *inner = rp_string_new(64);
                                        collect_pattern_names(valpat, src, inner);
                                        size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
                                        char *keytxt = dup_range(src, ks, ke);
                                        const char *p = inner->str;
                                        while (p && *p)
                                        {
                                            const char *q = strchr(p, ',');
                                            const char *e = q ? q : (p + strlen(p));
                                            while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                                                p++;
                                            while (e > p &&
                                                   (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
                                                e--;
                                            if (e > p)
                                            {
                                                rp_string_puts(out, " var ");
                                                rp_string_putsn(out, p, (size_t)(e - p));
                                                rp_string_puts(out, " = __tmpD0.");
                                                rp_string_puts(out, keytxt);
                                                rp_string_puts(out, ".");
                                                rp_string_putsn(out, p, (size_t)(e - p));
                                                rp_string_puts(out, ";");
                                                if (names_csv->len)
                                                {
                                                    rp_string_putc(names_csv, ',');
                                                }
                                                rp_string_putsn(names_csv, p, (size_t)(e - p));
                                            }
                                            p = q ? (q + 1) : NULL;
                                        }
                                        rp_string_free(inner);
                                        free(keytxt);
                                    }
                                    else if (strcmp(vt, "assignment_pattern") == 0 ||
                                             strcmp(vt, "object_assignment_pattern") == 0)
                                    {
                                        TSNode left = ts_node_child_by_field_name(valpat, "left", 4);
                                        TSNode right = ts_node_child_by_field_name(valpat, "right", 5);
                                        if (ts_node_is_null(left) || ts_node_is_null(right))
                                            continue;
                                        size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
                                        size_t ls = ts_node_start_byte(left), le = ts_node_end_byte(left);
                                        size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                                        rp_string_puts(out, " var ");
                                        rp_string_putsn(out, src + ls, le - ls);
                                        rp_string_puts(out, " = (__tmpD0.");
                                        rp_string_putsn(out, src + ks, ke - ks);
                                        rp_string_puts(out, " === undefined ? ");
                                        rp_string_putsn(out, src + rs, re - rs);
                                        rp_string_puts(out, " : __tmpD0.");
                                        rp_string_putsn(out, src + ks, ke - ks);
                                        rp_string_puts(out, ");");
                                        if (names_csv->len)
                                        {
                                            rp_string_putc(names_csv, ',');
                                        }
                                        rp_string_putsn(names_csv, src + ls, le - ls);
                                    }
                                    continue;
                                }

                                if (strcmp(pt, "shorthand_property_identifier_pattern") == 0)
                                {
                                    size_t is = ts_node_start_byte(prop), ie = ts_node_end_byte(prop);
                                    rp_string_puts(out, " var ");
                                    rp_string_putsn(out, src + is, ie - is);
                                    rp_string_puts(out, " = __tmpD0.");
                                    rp_string_putsn(out, src + is, ie - is);
                                    rp_string_puts(out, ";");
                                    if (names_csv->len)
                                    {
                                        rp_string_putc(names_csv, ',');
                                    }
                                    rp_string_putsn(names_csv, src + is, ie - is);
                                    continue;
                                }

                                if (strcmp(pt, "object_assignment_pattern") == 0)
                                {
                                    TSNode left = ts_node_child_by_field_name(prop, "left", 4);
                                    TSNode right = ts_node_child_by_field_name(prop, "right", 5);
                                    if (!ts_node_is_null(left) && !ts_node_is_null(right))
                                    {
                                        size_t is = ts_node_start_byte(left), ie = ts_node_end_byte(left);
                                        size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                                        rp_string_puts(out, " var ");
                                        rp_string_putsn(out, src + is, ie - is);
                                        rp_string_puts(out, " = (__tmpD0.");
                                        rp_string_putsn(out, src + is, ie - is);
                                        rp_string_puts(out, " === undefined ? ");
                                        rp_string_putsn(out, src + rs, re - rs);
                                        rp_string_puts(out, " : __tmpD0.");
                                        rp_string_putsn(out, src + is, ie - is);
                                        rp_string_puts(out, ");");
                                        if (names_csv->len)
                                        {
                                            rp_string_putc(names_csv, ',');
                                        }
                                        rp_string_putsn(names_csv, src + is, ie - is);
                                    }
                                    continue;
                                }
                            }

                            /* ...rest */
                            for (uint32_t i = 0; i < nprops; i++)
                            {
                                TSNode prop = ts_node_named_child(nameNode, i);
                                if (strcmp(ts_node_type(prop), "rest_pattern") != 0)
                                    continue;
                                uint32_t c = ts_node_named_child_count(prop);
                                for (uint32_t j = 0; j < c; j++)
                                {
                                    TSNode id = ts_node_named_child(prop, j);
                                    if (strcmp(ts_node_type(id), "identifier") != 0)
                                        continue;
                                    size_t is = ts_node_start_byte(id), ie = ts_node_end_byte(id);
                                    rp_string_puts(out, " var ");
                                    rp_string_putsn(out, src + is, ie - is);
                                    rp_string_puts(
                                        out,
                                        " = (function(o,e){var t={},k; for(k in o){ if(Object.prototype.hasOwnProperty.call(o,k) && ");
                                    if (excl->len)
                                    {
                                        rp_string_puts(out, "e.indexOf(k)<0");
                                    }
                                    else
                                    {
                                        rp_string_puts(out, "true");
                                    }
                                    rp_string_puts(out, ") t[k]=o[k]; } return t; })(__tmpD0, [");
                                    rp_string_puts(out, excl->str);
                                    rp_string_puts(out, "]);");
                                    if (names_csv->len)
                                    {
                                        rp_string_putc(names_csv, ',');
                                    }
                                    rp_string_putsn(names_csv, src + is, ie - is);
                                }
                            }

                            append_exports_for_csv(out, names_csv->str);
                            add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
                            rp_string_free(out);
                            rp_string_free(names_csv);
                            rp_string_free(excl);
                            free(val);
                            return 1;
                        }

                        /* ---------- array pattern ---------- */
                        if (strcmp(nameType, "array_pattern") == 0)
                        {
                            if (overlaps)
                                return 1;
                            size_t vs = ts_node_start_byte(initNode), ve = ts_node_end_byte(initNode);
                            char *val = dup_range(src, vs, ve);

                            rp_string *out = rp_string_new(256);
                            rp_string *names_csv = rp_string_new(64);

                            rp_string_puts(out, "var __tmpA0 = ");
                            rp_string_puts(out, val);
                            rp_string_puts(out, ";");

                            size_t pat_start = ts_node_start_byte(nameNode);
                            size_t after_bracket = pat_start + 1;

                            uint32_t nelems = ts_node_named_child_count(nameNode);
                            for (uint32_t j = 0; j < nelems; j++)
                            {
                                TSNode el = ts_node_named_child(nameNode, j);
                                const char *et = ts_node_type(el);

                                size_t es = ts_node_start_byte(el);
                                size_t idx = 0;
                                for (size_t i = after_bracket; i < es; i++)
                                {
                                    if (src[i] == ',')
                                        idx++;
                                }

                                if (strcmp(et, "identifier") == 0)
                                {
                                    size_t is = ts_node_start_byte(el), ie = ts_node_end_byte(el);
                                    rp_string_puts(out, " var ");
                                    rp_string_putsn(out, src + is, ie - is);
                                    rp_string_puts(out, " = __tmpA0[");
                                    char buf[32];
                                    snprintf(buf, sizeof(buf), "%zu", idx);
                                    rp_string_puts(out, buf);
                                    rp_string_puts(out, "];");
                                    if (names_csv->len)
                                    {
                                        rp_string_putc(names_csv, ',');
                                    }
                                    rp_string_putsn(names_csv, src + is, ie - is);
                                    continue;
                                }

                                if (strcmp(et, "assignment_pattern") == 0)
                                {
                                    TSNode left = ts_node_child_by_field_name(el, "left", 4);
                                    TSNode right = ts_node_child_by_field_name(el, "right", 5);
                                    if (ts_node_is_null(left) || ts_node_is_null(right))
                                        continue;
                                    size_t ls = ts_node_start_byte(left), le = ts_node_end_byte(left);
                                    size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                                    rp_string_puts(out, " var ");
                                    rp_string_putsn(out, src + ls, le - ls);
                                    rp_string_puts(out, " = (__tmpA0[");
                                    char buf[32];
                                    snprintf(buf, sizeof(buf), "%zu", idx);
                                    rp_string_puts(out, buf);
                                    rp_string_puts(out, "] === undefined ? ");
                                    rp_string_putsn(out, src + rs, re - rs);
                                    rp_string_puts(out, " : __tmpA0[");
                                    rp_string_puts(out, buf);
                                    rp_string_puts(out, "]);");
                                    if (names_csv->len)
                                    {
                                        rp_string_putc(names_csv, ',');
                                    }
                                    rp_string_putsn(names_csv, src + ls, le - ls);
                                    continue;
                                }

                                if (strcmp(et, "rest_pattern") == 0)
                                {
                                    uint32_t cn = ts_node_named_child_count(el);
                                    for (uint32_t k = 0; k < cn; k++)
                                    {
                                        TSNode id = ts_node_named_child(el, k);
                                        if (strcmp(ts_node_type(id), "identifier") != 0)
                                            continue;
                                        size_t is = ts_node_start_byte(id), ie = ts_node_end_byte(id);
                                        rp_string_puts(out, " var ");
                                        rp_string_putsn(out, src + is, ie - is);
                                        rp_string_puts(out, " = Array.prototype.slice.call(__tmpA0, ");
                                        char buf[32];
                                        snprintf(buf, sizeof(buf), "%zu", idx);
                                        rp_string_puts(out, buf);
                                        rp_string_puts(out, ");");
                                        if (names_csv->len)
                                        {
                                            rp_string_putc(names_csv, ',');
                                        }
                                        rp_string_putsn(names_csv, src + is, ie - is);
                                    }
                                    continue;
                                }

                                if (strcmp(et, "object_pattern") == 0)
                                {
                                    rp_string *inner = rp_string_new(64);
                                    collect_pattern_names(el, src, inner);
                                    const char *p = inner->str;
                                    while (p && *p)
                                    {
                                        const char *q = strchr(p, ',');
                                        const char *e = q ? q : (p + strlen(p));
                                        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                                            p++;
                                        while (e > p &&
                                               (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
                                            e--;
                                        if (e > p)
                                        {
                                            rp_string_puts(out, " var ");
                                            rp_string_putsn(out, p, (size_t)(e - p));
                                            rp_string_puts(out, " = __tmpA0[");
                                            char buf[32];
                                            snprintf(buf, sizeof(buf), "%zu", idx);
                                            rp_string_puts(out, buf);
                                            rp_string_puts(out, "].");
                                            rp_string_putsn(out, p, (size_t)(e - p));
                                            rp_string_puts(out, ";");
                                            if (names_csv->len)
                                            {
                                                rp_string_putc(names_csv, ',');
                                            }
                                            rp_string_putsn(names_csv, p, (size_t)(e - p));
                                        }
                                        p = q ? (q + 1) : NULL;
                                    }
                                    rp_string_free(inner);
                                    continue;
                                }
                            }

                            append_exports_for_csv(out, names_csv->str);
                            add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
                            rp_string_free(out);
                            rp_string_free(names_csv);
                            free(val);
                            return 1;
                        }
                    }
                }
            }

            /* fallback: keep lexical declaration text + export collected names */
            size_t ds = ts_node_start_byte(decl), de = ts_node_end_byte(decl);
            char *decl_txt = dup_range(src, ds, de);
            rp_string *names = rp_string_new(64);
            for (uint32_t i = 0; i < ndecls; i++)
            {
                TSNode vd = ts_node_named_child(decl, i);
                if (strcmp(ts_node_type(vd), "variable_declarator") != 0)
                    continue;
                TSNode nm = ts_node_child_by_field_name(vd, "name", 4);
                if (ts_node_is_null(nm))
                    continue;
                collect_pattern_names(nm, src, names);
            }
            rp_string *out = rp_string_new(64);
            rp_string_puts(out, decl_txt);
            append_exports_for_csv(out, names->str);
            add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
            rp_string_free(out);
            rp_string_free(names);
            free(decl_txt);
            return 1;
        }

        /* export variable_declaration (just in case) */
        if (strcmp(dt, "variable_declaration") == 0)
        {
            if (overlaps)
                return 1;
            size_t ds = ts_node_start_byte(decl), de = ts_node_end_byte(decl);
            char *decl_txt = dup_range(src, ds, de);
            rp_string *names = rp_string_new(64);
            uint32_t n = ts_node_named_child_count(decl);
            for (uint32_t i = 0; i < n; i++)
            {
                TSNode d = ts_node_named_child(decl, i);
                if (strcmp(ts_node_type(d), "variable_declarator") != 0)
                    continue;
                TSNode nm = ts_node_child_by_field_name(d, "name", 4);
                if (ts_node_is_null(nm))
                    continue;
                collect_pattern_names(nm, src, names);
            }
            rp_string *out = rp_string_new(64);
            rp_string_puts(out, decl_txt);
            append_exports_for_csv(out, names->str);
            add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
            rp_string_free(out);
            rp_string_free(names);
            free(decl_txt);
            return 1;
        }
    }

    /* export { a, b as c } [from "mod"]  OR  local re-exports */
    TSNode specs = ts_node_child_by_field_name(snode, "specifiers", 10);
    /* Some grammars expose `export_clause` instead of the `specifiers` field */
    if (ts_node_is_null(specs))
    {
        uint32_t n = ts_node_named_child_count(snode);
        for (uint32_t i = 0; i < n; i++)
        {
            TSNode ch = ts_node_named_child(snode, i);
            if (strcmp(ts_node_type(ch), "export_clause") == 0)
            {
                specs = ch;
                break;
            }
        }
    }
    if (!ts_node_is_null(specs))
    {
        if (overlaps)
            return 1;
        TSNode srcnode = ts_node_child_by_field_name(snode, "source", 6);
        char *mod = NULL;
        if (!ts_node_is_null(srcnode))
        {
            size_t ms = ts_node_start_byte(srcnode), me = ts_node_end_byte(srcnode);
            mod = dup_range(src, ms, me);
        }

        rp_string *out = rp_string_new(64);
        char tmp[24];
        tmp[0] = '\0';

        if (mod)
        {
            snprintf(tmp, sizeof(tmp), "__tmpExp0");
            rp_string_puts(out, "var ");
            rp_string_puts(out, tmp);
            rp_string_puts(out, " = require(");
            rp_string_puts(out, mod);
            rp_string_puts(out, "); ");
        }

        uint32_t k = ts_node_named_child_count(specs);
        for (uint32_t i = 0; i < k; i++)
        {
            TSNode s = ts_node_named_child(specs, i);
            if (strcmp(ts_node_type(s), "export_specifier") != 0)
                continue;

            TSNode local = ts_node_child_by_field_name(s, "name", 4);
            TSNode alias = ts_node_child_by_field_name(s, "alias", 5);
            if (ts_node_is_null(local))
                continue;

            size_t ls = ts_node_start_byte(local), le = ts_node_end_byte(local);

            rp_string_puts(out, "exports.");
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                rp_string_putsn(out, src + as, ae - as); /* alias */
            }
            else
            {
                rp_string_putsn(out, src + ls, le - ls); /* same-name export */
            }
            rp_string_puts(out, " = ");
            if (mod)
            {
                rp_string_puts(out, tmp);
                rp_string_puts(out, ".");
            }
            rp_string_putsn(out, src + ls, le - ls); /* local or tmp.prop */
            rp_string_puts(out, ";");
        }

        add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
        rp_string_free(out);
        free(mod);
        return 1;
    }

    /* export * from "mod" */
    {
        TSNode star = ts_node_child_by_field_name(snode, "wildcard", 8);
        TSNode srcnode = ts_node_child_by_field_name(snode, "source", 6);
        if (!ts_node_is_null(star) && !ts_node_is_null(srcnode))
        {
            if (overlaps)
                return 1;
            size_t ms = ts_node_start_byte(srcnode), me = ts_node_end_byte(srcnode);
            char *mod = dup_range(src, ms, me);
            rp_string *out = rp_string_new(64);
            rp_string_puts(out, "var __tmpExp = require(");
            rp_string_puts(out, mod);
            rp_string_puts(out,
                        "); for (var k in __tmpExp) { if (k === \"default\") continue; exports[k] = __tmpExp[k]; }");
            add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
            rp_string_free(out);
            free(mod);
            return 1;
        }
    }

    /* fallback: drop export, keep inner statement */
    {
        uint32_t n = ts_node_named_child_count(snode);
        if (n > 0)
        {
            if (overlaps)
                return 1;
            TSNode inner = ts_node_named_child(snode, n - 1);
            size_t s = ts_node_start_byte(inner), e = ts_node_end_byte(inner);
            char *txt = dup_range(src, s, e);
            add_edit_take_ownership(edits, ns, ne, txt, claimed);
            return 1;
        }
    }

    return 0;
}

// Detect whether a subtree contains a `this` that belongs to the current lexical scope of an arrow function.
// We DO NOT descend into non-arrow function bodies or class bodies (they introduce a new `this`).
static int contains_lexical_this(TSNode node)
{
    const char *t = ts_node_type(node);
    // If this node itself is the `this` token (anonymous/unnamed)
    if (strcmp(t, "this") == 0)
    {
        return 1;
    }
    // Do not descend into nodes that create their own `this` binding (non-arrow functions, classes)
    if (ts_node_is_named(node))
    {
        // function_declaration, function_expression, generator_function, method_definition, etc.
        // We avoid any node type that contains "function" but is NOT "arrow_function".
        if (strstr(t, "function") && strcmp(t, "arrow_function") != 0)
            return 0;
        if (strstr(t, "method") != NULL)
            return 0;
        if (strstr(t, "class") != NULL)
            return 0;
    }
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode c = ts_node_child(node, i);
        if (contains_lexical_this(c))
            return 1;
    }
    return 0;
}
// ============== handlers ==============

// Rewrite JS rest parameters:  function f(x,y,...a){ ... }
// -> function f(x,y){ var a = Object.values(arguments).slice(2); ... }
static int rewrite_function_rest(EditList *edits, const char *src, TSNode func_node, RangeList *claimed, int overlaps)
{
    TSNode params = ts_node_child_by_field_name(func_node, "parameters", 10);
    TSNode body = ts_node_child_by_field_name(func_node, "body", 4);
    if (ts_node_is_null(params) || ts_node_is_null(body))
        return 0;

    // Find a rest_parameter
    uint32_t nparams = ts_node_named_child_count(params);
    TSNode rest = {0};
    uint32_t rest_index = 0;
    bool found = false;

    for (uint32_t i = 0; i < nparams; i++)
    {
        TSNode ch = ts_node_named_child(params, i);
        if (strcmp(ts_node_type(ch), "rest_pattern") == 0)
        {
            rest = ch;
            rest_index = i;
            found = true;
            break;
        }
    }
    if (!found)
        return 0;

    // Count non-rest params before the rest (slice start)
    uint32_t before_count = rest_index;

    // Name of the rest identifier
    nparams = ts_node_named_child_count(rest);
    TSNode rest_pat = {{0}};

    for (int i = 0; i < nparams; i++)
    {
        TSNode ch = ts_node_named_child(rest, i);
        if (strcmp(ts_node_type(ch), "identifier") == 0)
        {
            rest_pat = ch;
            rest_index = i;
            found = true;
            break;
        }
    }

    if (ts_node_is_null(rest_pat))
        return 0;

    if (overlaps)
        return 1;

    size_t name_s = ts_node_start_byte(rest_pat);
    size_t name_e = ts_node_end_byte(rest_pat);

    // Remove the rest parameter token, including a preceding comma if present
    size_t params_s = ts_node_start_byte(params);
    size_t params_e = ts_node_end_byte(params);
    size_t rest_s = ts_node_start_byte(rest);
    size_t rest_e = ts_node_end_byte(rest);

    // Walk back over whitespace to see if there is a preceding comma
    size_t del_s = rest_s;
    size_t i = rest_s;
    while (i > params_s && is_ws(src[i - 1]))
        i--;
    if (i > params_s && src[i - 1] == ',')
    {
        del_s = i - 1;
        while (del_s > params_s && is_ws(src[del_s - 1]))
            del_s--;
    }
    else
    {
        // Otherwise try to eat a trailing comma if present (rest first in list)
        size_t j = rest_e;
        while (j < params_e && is_ws(src[j]))
            j++;
        if (j < params_e && src[j] == ',')
        {
            // remove trailing comma and the rest param
            rest_e = j + 1;
        }
    }

    add_edit(edits, del_s, rest_e, "", claimed);

    // Insert the shim at the start of the body block (after '{' and any whitespace)
    size_t body_s = ts_node_start_byte(body);
    size_t body_e = ts_node_end_byte(body);
    if (body_e <= body_s)
        return 1;                  // odd, but we've already removed the rest
    size_t insert_at = body_s + 1; // skip '{'
    while (insert_at < body_e && is_ws(src[insert_at]))
        insert_at++;

    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned)before_count);

    const char *p1 = "var ";
    const char *p2 = " = Object.values(arguments).slice(";
    const char *p3 = "); ";
    size_t name_len = name_e - name_s;
    size_t repl_len = strlen(p1) + name_len + strlen(p2) + strlen(numbuf) + strlen(p3);
    char *repl = NULL;
    size_t k = 0;

    REMALLOC(repl, repl_len + 1);

    memcpy(repl + k, p1, strlen(p1));
    k += strlen(p1);
    memcpy(repl + k, src + name_s, name_len);
    k += name_len;
    memcpy(repl + k, p2, strlen(p2));
    k += strlen(p2);
    memcpy(repl + k, numbuf, strlen(numbuf));
    k += strlen(numbuf);
    memcpy(repl + k, p3, strlen(p3));
    k += strlen(p3);
    repl[k] = '\0';

    add_edit_take_ownership(edits, insert_at, insert_at, repl, claimed);
    // rl_add(claimed, params_s, params_e);
    // rl_add(claimed, body_s, body_e);
    return 1;
}

/* Rewrite destructuring parameters in regular functions:
 *   function f({x, y}, a) { ... }
 *   → function f(_dp0, a) { var x = _dp0.x, y = _dp0.y; ... }
 * Also handles: function f({x = 1} = {}) { ... }
 */
static unsigned _dp_counter = 0;
static int rewrite_function_destructuring_params(EditList *edits, const char *src, TSNode func_node,
                                                  RangeList *claimed, int overlaps)
{
    TSNode params = ts_node_child_by_field_name(func_node, "parameters", 10);
    TSNode body = ts_node_child_by_field_name(func_node, "body", 4);
    if (ts_node_is_null(params) || ts_node_is_null(body))
        return 0;

    /* Scan for destructuring params */
    uint32_t np = ts_node_named_child_count(params);
    int has_destr = 0;
    for (uint32_t i = 0; i < np; i++)
    {
        TSNode p = ts_node_named_child(params, i);
        const char *pt = ts_node_type(p);
        if (strcmp(pt, "object_pattern") == 0 || strcmp(pt, "array_pattern") == 0)
        {
            has_destr = 1;
            break;
        }
        if (strcmp(pt, "assignment_pattern") == 0)
        {
            TSNode left = ts_node_child_by_field_name(p, "left", 4);
            if (!ts_node_is_null(left))
            {
                const char *lt = ts_node_type(left);
                if (strcmp(lt, "object_pattern") == 0 || strcmp(lt, "array_pattern") == 0)
                {
                    has_destr = 1;
                    break;
                }
            }
        }
    }
    if (!has_destr)
        return 0;

    if (overlaps)
        return 1;

    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    size_t bs = ts_node_start_byte(body);

    /* Build new param list and body injections */
    rp_string *new_params = rp_string_new(64);
    rp_string *body_inj = rp_string_new(128);

    rp_string_puts(new_params, "(");
    for (uint32_t i = 0; i < np; i++)
    {
        if (i > 0)
            rp_string_puts(new_params, ", ");

        TSNode p = ts_node_named_child(params, i);
        const char *pt = ts_node_type(p);
        size_t pps = ts_node_start_byte(p), ppe = ts_node_end_byte(p);

        TSNode destr_pat = {{0}};
        char *param_def = NULL;

        if (strcmp(pt, "object_pattern") == 0 || strcmp(pt, "array_pattern") == 0)
        {
            destr_pat = p;
        }
        else if (strcmp(pt, "assignment_pattern") == 0)
        {
            TSNode left = ts_node_child_by_field_name(p, "left", 4);
            TSNode right = ts_node_child_by_field_name(p, "right", 5);
            if (!ts_node_is_null(left))
            {
                const char *lt = ts_node_type(left);
                if (strcmp(lt, "object_pattern") == 0 || strcmp(lt, "array_pattern") == 0)
                {
                    destr_pat = left;
                    if (!ts_node_is_null(right))
                    {
                        size_t ds = ts_node_start_byte(right), de = ts_node_end_byte(right);
                        param_def = (char *)malloc(de - ds + 1);
                        memcpy(param_def, src + ds, de - ds);
                        param_def[de - ds] = '\0';
                    }
                }
            }
        }

        if (!ts_node_is_null(destr_pat))
        {
            /* Generate temp name and collect bindings */
            char tmpname[32];
            snprintf(tmpname, sizeof(tmpname), "_dp%u", _dp_counter++);
            rp_string_puts(new_params, tmpname);

            /* Inject param default if present */
            if (param_def)
            {
                rp_string_appendf(body_inj, "if (%s === void 0) %s = %s; ", tmpname, tmpname, param_def);
                free(param_def);
            }

            /* Collect bindings and emit var declarations */
            const char *base = (strcmp(ts_node_type(destr_pat), "array_pattern") == 0) ? tmpname : tmpname;
            Bindings binds;
            binds_init(&binds);
            if (collect_flat_destructure_bindings(destr_pat, src, base, &binds) && binds.len > 0)
            {
                rp_string_puts(body_inj, "var ");
                for (size_t bi = 0; bi < binds.len; bi++)
                {
                    if (bi > 0)
                        rp_string_puts(body_inj, ", ");
                    rp_string_puts(body_inj, binds.a[bi].name);
                    rp_string_puts(body_inj, " = ");
                    if (binds.a[bi].defval)
                    {
                        rp_string_puts(body_inj, binds.a[bi].repl);
                        rp_string_puts(body_inj, " !== undefined ? ");
                        rp_string_puts(body_inj, binds.a[bi].repl);
                        rp_string_puts(body_inj, " : ");
                        rp_string_puts(body_inj, binds.a[bi].defval);
                    }
                    else
                    {
                        rp_string_puts(body_inj, binds.a[bi].repl);
                    }
                }
                rp_string_puts(body_inj, "; ");
            }
            binds_free(&binds);
        }
        else
        {
            /* Non-destructured param: copy as-is */
            rp_string_putsn(new_params, src + pps, ppe - pps);
            if (param_def) free(param_def);
        }
    }
    rp_string_puts(new_params, ")");

    /* Apply edits */
    add_edit_take_ownership(edits, ps, pe, rp_string_steal(new_params), claimed);
    new_params = rp_string_free(new_params);

    if (body_inj->len)
        add_edit_take_ownership(edits, bs + 1, bs + 1, rp_string_steal(body_inj), claimed);
    else
        body_inj = rp_string_free(body_inj);
    body_inj = rp_string_free(body_inj);

    return 1;
}

static char *make_raw_rep(const char *orig, size_t l)
{
    const char *s = orig;
    const char *e = s + l;
    size_t newsz = 3; // beginning " and ending "'\0'

    while (s < e)
    {
        switch (*s)
        {
        case '"':
        case '\\':
            newsz++;
            break;
        case '\n': // literal "\n"
            newsz += 2;
            break;
        }
        newsz++;
        s++;
    }

    char *ret, *out = NULL;

    REMALLOC(out, newsz);
    ret = out;

    *(out++) = '"';

    s = orig;
    while (s < e)
    {
        switch (*s)
        {
        case '"':
        case '\\':
            *(out++) = '\\';
            *(out++) = *(s++);
            break;
        case '\n': // +"\n"
                   //            *(out++) = '"';
                   //            *(out++) = '\n';
                   //            *(out++) = '+';
                   //            *(out++) = '"';
            *(out++) = '\\';
            *(out++) = 'n';
            s++;
            break;
        default:
            *(out++) = *(s++);
        }
    }
    *(out++) = '"';
    *(out++) = '\0';
    // printf("strlen=%d, malloc=%d + 1, l=%d\n", (int)strlen(ret), (int) newsz-1, (int)l);
    return ret;
}

static int rewrite_raw_node(EditList *edits, const char *src, TSNode snode, RangeList *claimed, int overlaps)
{

    TSNode raw = find_child_type(snode, "string_fragment_raw", NULL);

    if (ts_node_is_null(raw))
        return 0;

    if (overlaps)
        return 1;

    size_t start = ts_node_start_byte(raw), end = ts_node_end_byte(raw);

    char *out = make_raw_rep(src + start, end - start);

    start = ts_node_start_byte(snode);
    end = ts_node_end_byte(snode);

    add_edit_take_ownership(edits, start, end, out, claimed);
    return 1;
}

static int do_named_imports(EditList *edits, const char *src, TSNode snode, TSNode named_imports, TSNode string_frag,
                            size_t start, size_t end, RangeList *claimed)
{
    static uint32_t tmpn = 0;
    uint32_t pos = 0;
    char buf[32];

    TSNode spec = find_child_type(named_imports, "import_specifier", &pos);
    if (ts_node_is_null(spec))
        return 0;

    size_t mod_s = ts_node_start_byte(string_frag), mod_e = ts_node_end_byte(string_frag);

    /* temp module binding */
    sprintf(buf, "__tmpModImp%u", tmpn);
    rp_string *out = rp_string_new(64);

    rp_string_appendf(out, "var %s=require(\"%.*s\");if(_TrN_Sp._pP)_TrN_Sp._pP();", buf, (int)(mod_e - mod_s), src + mod_s);

    /* each specifier: var <aliasOrName> = tmp.<name>; */
    while (!ts_node_is_null(spec))
    {
        TSNode local = ts_node_child_by_field_name(spec, "name", 4);
        TSNode alias = ts_node_child_by_field_name(spec, "alias", 5);

        if (ts_node_is_null(local))
        {
            /* fallback: use raw slice */
            size_t s = ts_node_start_byte(spec), e = ts_node_end_byte(spec);
            rp_string_appendf(out, "var %.*s=%s.%.*s;", (int)(e - s), src + s, buf, (int)(e - s), src + s);
        }
        else
        {
            size_t ls = ts_node_start_byte(local), le = ts_node_end_byte(local);
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                rp_string_appendf(out, "var %.*s=%s.%.*s;", (int)(ae - as), src + as, /* alias binding on LHS */
                              buf, (int)(le - ls), src + ls);                           /* local name on RHS from tmp */
            }
            else
            {
                rp_string_appendf(out, "var %.*s=%s.%.*s;", (int)(le - ls), src + ls, /* same name */
                              buf, (int)(le - ls), src + ls);
            }
        }

        pos++;
        spec = find_child_type(named_imports, "import_specifier", &pos);
    }
    add_edit_take_ownership(edits, start, end, rp_string_steal(out), claimed);
    out=rp_string_free(out);

    tmpn++;
    return 1;
}

static int do_namespace_import(EditList *edits, const char *src, TSNode snode, TSNode namespace_import,
                               TSNode string_frag, size_t start, size_t end, uint32_t *polysneeded, RangeList *claimed)
{
    rp_string *out;
    TSNode id = find_child_type(namespace_import, "identifier", NULL);

    if (ts_node_is_null(id))
        return 0;

    size_t mod_name_end = ts_node_end_byte(string_frag), mod_name_start = ts_node_start_byte(string_frag),
           id_end = ts_node_end_byte(id), id_start = ts_node_start_byte(id);

    // var math = _interopRequireWildcard(require("math"));
    out=rp_string_new(0);
    rp_string_appendf(out, "var %.*s=_TrN_Sp._interopRequireWildcard(require(\"%.*s\"));if(_TrN_Sp._pP)_TrN_Sp._pP();", (id_end - id_start),
                  src + id_start, (mod_name_end - mod_name_start), src + mod_name_start);

    add_edit_take_ownership(edits, start, end, rp_string_steal(out), claimed);

    out = rp_string_free(out);

    *polysneeded |= IMPORT_PF;

    return 1;
}

static int do_default_import(EditList *edits, const char *src, TSNode snode, TSNode default_ident, TSNode string_frag,
                             size_t start, size_t end, RangeList *claimed)
{
    rp_string *out;
    size_t mod_s = ts_node_start_byte(string_frag), mod_e = ts_node_end_byte(string_frag);
    size_t id_s = ts_node_start_byte(default_ident), id_e = ts_node_end_byte(default_ident);

    /* With our export lowering, default import is the entire module.exports */
    out=rp_string_new(0);
    rp_string_appendf(out, "var %.*s=_TrN_Sp._interopDefault(require(\"%.*s\"));if(_TrN_Sp._pP)_TrN_Sp._pP();", (int)(id_e - id_s), src + id_s,
                  (int)(mod_e - mod_s), src + mod_s);

    add_edit_take_ownership(edits, start, end, rp_string_steal(out), claimed);

    out = rp_string_free(out);

    return 1;
}

static int do_default_and_named_imports(EditList *edits, const char *src, TSNode snode, TSNode default_ident,
                                        TSNode named_imports, TSNode string_frag, size_t start, size_t end,
                                        RangeList *claimed)
{
    static uint32_t tmpn = 0;
    char tbuf[32];
    sprintf(tbuf, "__tmpModImpdn%u", tmpn);

    size_t mod_s = ts_node_start_byte(string_frag), mod_e = ts_node_end_byte(string_frag);
    size_t id_s = ts_node_start_byte(default_ident), id_e = ts_node_end_byte(default_ident);

    /* require once */
    rp_string *out=rp_string_new(512);
    rp_string_appendf(out, "var %s=require(\"%.*s\");if(_TrN_Sp._pP)_TrN_Sp._pP();", tbuf, (int)(mod_e - mod_s), src + mod_s);

    /* bind default: var def = __tmp;  (we lower default export to module.exports) */
    rp_string_appendf(out, "var %.*s=%s.default;", (int)(id_e - id_s), src + id_s, tbuf);

    /* named specifiers with aliases */
    uint32_t pos = 0;
    TSNode spec = find_child_type(named_imports, "import_specifier", &pos);
    while (!ts_node_is_null(spec))
    {
        TSNode local = ts_node_child_by_field_name(spec, "name", 4);
        TSNode alias = ts_node_child_by_field_name(spec, "alias", 5);

        if (ts_node_is_null(local))
        {
            /* fallback: raw slice */
            size_t s = ts_node_start_byte(spec), e = ts_node_end_byte(spec);
            rp_string_appendf(out, "var %.*s=%s.%.*s;", (int)(e - s), src + s, tbuf, (int)(e - s), src + s);
        }
        else
        {
            size_t ls = ts_node_start_byte(local), le = ts_node_end_byte(local);
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                rp_string_appendf(out, "var %.*s=%s.%.*s;", (int)(ae - as), src + as, /* alias */
                              tbuf, (int)(le - ls), src + ls);                          /* local */
            }
            else
            {
                rp_string_appendf(out, "var %.*s=%s.%.*s;", (int)(le - ls), src + ls, /* same name */
                              tbuf, (int)(le - ls), src + ls);
            }
        }

        pos++;
        spec = find_child_type(named_imports, "import_specifier", &pos);
    }

    add_edit_take_ownership(edits, start, end, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    tmpn++;
    return 1;
}

static int rewrite_import_node(EditList *edits, const char *src, TSNode snode, RangeList *claimed,
                               uint32_t *polysneeded, int overlaps)
{
    size_t ns = ts_node_start_byte(snode), ne = ts_node_end_byte(snode);

    TSNode string = find_child_type(snode, "string", NULL);
    if (ts_node_is_null(string))
        return 0;

    TSNode string_frag = find_child_type(string, "string_fragment", NULL);
    if (ts_node_is_null(string_frag))
        string_frag = find_child_type(string, "string_fragment_raw", NULL);
    if (ts_node_is_null(string_frag))
        return 0;

    if (overlaps)
        return 1;

    // look for template string here:
    TSNode child = find_child_type(snode, "import_clause", NULL);
    if (!ts_node_is_null(child))
    {
        TSNode defid = find_child_type(child, "identifier", NULL);       /* default import */
        TSNode named = find_child_type(child, "named_imports", NULL);    /* { ... } */
        TSNode nsimp = find_child_type(child, "namespace_import", NULL); /* * as ns */

        if (!ts_node_is_null(defid) && !ts_node_is_null(named))
        {
            return do_default_and_named_imports(edits, src, snode, defid, named, string_frag, ns, ne, claimed);
        }
        if (!ts_node_is_null(defid))
        {
            return do_default_import(edits, src, snode, defid, string_frag, ns, ne, claimed);
        }
        if (!ts_node_is_null(named))
        {
            return do_named_imports(edits, src, snode, named, string_frag, ns, ne, claimed);
        }
        if (!ts_node_is_null(nsimp))
        {
            return do_namespace_import(edits, src, snode, nsimp, string_frag, ns, ne, polysneeded, claimed);
        }
    }

    size_t sstart = ts_node_start_byte(string_frag), send = ts_node_end_byte(string_frag), slen = send - sstart;

    // require(""); + if(_TrN_Sp._pP)_TrN_Sp._pP();
    char *out = NULL;
    size_t outlen = 13 + slen + 30;
    REMALLOC(out, outlen);
    snprintf(out, outlen, "require(\"%.*s\");if(_TrN_Sp._pP)_TrN_Sp._pP();", (int)slen, src + sstart);
    // check for newlines between ns and ne.  add an edit to insert however many, cuz this rewrites on one line
    add_edit_take_ownership(edits, ns, ne, out, claimed);
    return 1;
}

// Templates (tagged + untagged)
static int rewrite_template_node(EditList *edits, const char *src, TSNode tpl_node, RangeList *claimed, int overlaps)
{
    if (overlaps)
        return 1;

    size_t ns = ts_node_start_byte(tpl_node), ne = ts_node_end_byte(tpl_node);

    TSNode tag = prev_named_sibling(tpl_node);
    int is_tagged = (!ts_node_is_null(tag) && ts_node_end_byte(tag) == ns);

    Piece *lits = NULL, *exprs = NULL;
    size_t nl = 0, neP = 0;
    collect_template_by_offsets(tpl_node, &lits, &nl, &exprs, &neP);

    if (is_tagged)
    {
        size_t ts = ts_node_start_byte(tag), te = ts_node_end_byte(tag);
        size_t cap = 128 + (te - ts) + 32 * nl;
        size_t j = 0;
        char *out = NULL;

        for (size_t i = 0; i < nl; i++)
            cap += 2 * (lits[i].end - lits[i].start) + 4;
        for (size_t i = 0; i < neP; i++)
            cap += (exprs[i].end - exprs[i].start) + 8;

        REMALLOC(out, cap);

#define adds(S)                                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        const char *_s = (S);                                                                                          \
        size_t _sL = strlen(_s);                                                                                       \
        memcpy(out + j, _s, _sL);                                                                                      \
        j += _sL;                                                                                                      \
    } while (0)

        memcpy(out + j, src + ts, te - ts);
        j += (te - ts);
        adds("(");
        adds("[");
        for (size_t i = 0; i < nl; i++)
        {
            int nnl = 0;
            if (i)
                adds(",");
            char *q = js_quote_literal(src, lits[i].start, lits[i].end, &nnl);
            adds(q);
            for (int k = 0; k < nnl; k++)
                adds("\n");
            free(q);
        }
        adds("]");
        for (size_t i = 0; i < neP; i++)
        {
            adds(",(");
            size_t L = exprs[i].end - exprs[i].start;
            memcpy(out + j, src + exprs[i].start, L);
            j += L;
            adds(")");
        }
        adds(")");
        out[j] = '\0';

        add_edit_take_ownership(edits, ts, ne, out, claimed);
        rl_add(claimed, ts, ne);
        free(lits);
        free(exprs);
        return 1;
    }

    // Untagged → concatenation
    Piece *pieces = NULL;
    size_t np = 0, capP = 0;
    size_t li = 0, ei = 0;
    while (li < nl || ei < neP)
    {
        // if it begins with expression
        if (nl && neP && !li && !ei && exprs[0].start < lits[0].start)
        {
            Piece E = exprs[ei++];
            if (np == capP)
            {
                capP = capP ? capP * 2 : 8;
                REMALLOC(pieces, capP * sizeof(Piece));
            }
            pieces[np++] = (Piece){1, E.start, E.end};
        }
        if (li < nl)
        {
            Piece L = lits[li++];
            if (L.end > L.start)
            {
                if (np == capP)
                {
                    capP = capP ? capP * 2 : 8;
                    REMALLOC(pieces, capP * sizeof(Piece));
                }
                pieces[np++] = (Piece){0, L.start, L.end};
            }
        }
        if (ei < neP)
        {
            Piece E = exprs[ei++];
            if (np == capP)
            {
                capP = capP ? capP * 2 : 8;
                REMALLOC(pieces, capP * sizeof(Piece));
            }
            pieces[np++] = (Piece){1, E.start, E.end};
        }
    }

    if (np == 0)
    {
        if (np == capP)
        {
            capP = capP ? capP * 2 : 8;
            REMALLOC(pieces, capP * sizeof(Piece));
        }
        pieces[np++] = (Piece){0, ns, ns};
    }

    int need_leading_empty = (np > 0 && pieces[0].is_expr);
    size_t cap = 16 + (need_leading_empty ? 4 : 0);
    for (size_t i = 0; i < np; i++)
    {
        if (pieces[i].is_expr)
            cap += 4 + (pieces[i].end - pieces[i].start);
        else
            cap += 2 * (pieces[i].end - pieces[i].start) + 8;
        if (i || need_leading_empty)
            cap += 3;
    }

    char *out = NULL;
    size_t j = 0;

    REMALLOC(out, cap);

    if (need_leading_empty)
        adds("\"\"");
    for (size_t i = 0; i < np; i++)
    {
        if (i || need_leading_empty)
            adds(" + ");
        if (pieces[i].is_expr)
        {
            size_t L = pieces[i].end - pieces[i].start;
            // printf("piece='%.*s'\n", (int)L, src+pieces[i].start);
            // check for ':' and '%'
            const char *p = src + pieces[i].start;
            const char *e = src + pieces[i].end + 1;
            const char *fmtstart = NULL;
            const char *expstart = p;
            int fmtlen = 0;

            do
            {
                while (p < e && isspace(*p))
                    p++;
                if (*p == '%')
                {
                    fmtstart = p;
                    while (p < e && !isspace(*p) && *p != ':')
                        p++, fmtlen++;
                    while (p < e && isspace(*p))
                        p++;
                    if (*p != ':')
                    {
                        fmtstart = NULL;
                        break;
                    }
                }
                else if (*p == '"')
                {
                    p++;
                    fmtstart = p;
                    while (p < e && *p != '"')
                        p++, fmtlen++;
                    if (*p != '"')
                    {
                        fmtstart = NULL;
                        break;
                    }
                    p++;
                    while (p < e && isspace(*p))
                        p++;
                    if (*p != ':')
                    {
                        fmtstart = NULL;
                        break;
                    }
                }
                else
                    break;
                p++;
                L -= (p - expstart);
                expstart = p;
                // rampart.utils.sprintf("",
                cap += 25;
                out = realloc(out, cap);

            } while (0);
            if (fmtstart)
            {
                adds("rampart.utils.sprintf(\"");
                memcpy(out + j, fmtstart, fmtlen);
                j += fmtlen;
                adds("\",");
            }
            else
                adds("(");

            memcpy(out + j, expstart, L);
            j += L;
            adds(")");
        }
        else
        {
            int nnl;
            char *q = js_quote_literal(src, pieces[i].start, pieces[i].end, &nnl);

            adds(q);
            for (int k = 0; k < nnl; k++)
                adds("\n");
            free(q);
        }
    }
    out[j] = '\0';
#undef adds
    // printf("strlen=%d, cap=%d\n", strlen(out), (int)cap);
    add_edit_take_ownership(edits, ns, ne, out, claimed);
    rl_add(claimed, ns, ne);
    free(lits);
    free(exprs);
    free(pieces);
    return 1;
}

// Arrow functions (concise + block, with flat destructuring lowering)
static int rewrite_arrow_function_node(EditList *edits, const char *src, TSNode arrow_node, RangeList *claimed,
                                       int overlaps)
{
    size_t ns = ts_node_start_byte(arrow_node), ne = ts_node_end_byte(arrow_node);

    TSNode params = ts_node_child_by_field_name(arrow_node, "parameters", 10);
    TSNode body = ts_node_child_by_field_name(arrow_node, "body", 4);
    uint32_t n = ts_node_child_count(arrow_node);
    int arrow_idx = -1;
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode kid = ts_node_child(arrow_node, i);
        if (!ts_node_is_named(kid) && strcmp(ts_node_type(kid), "=>") == 0)
        {
            arrow_idx = (int)i;
            break;
        }
    }
    if (ts_node_is_null(params) && arrow_idx >= 0)
    {
        for (int i = arrow_idx - 1; i >= 0; i--)
        {
            TSNode kid = ts_node_child(arrow_node, (uint32_t)i);
            if (ts_node_is_named(kid))
            {
                params = kid;
                break;
            }
        }
    }
    if (ts_node_is_null(body) && arrow_idx >= 0)
    {
        for (uint32_t i = (uint32_t)arrow_idx + 1; i < n; i++)
        {
            TSNode kid = ts_node_child(arrow_node, i);
            if (ts_node_is_named(kid))
            {
                body = kid;
                break;
            }
        }
    }
    if (ts_node_is_null(params) || ts_node_is_null(body))
        return 0;

    if (overlaps)
        return 1;

    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    size_t bs = ts_node_start_byte(body), be = ts_node_end_byte(body);
    int bind_this = contains_lexical_this(body);
    int is_block = (strcmp(ts_node_type(body), "statement_block") == 0); // Single-parameter pattern detection
    TSNode pattern = (TSNode){0};
    int single = 0;
    if (strcmp(ts_node_type(params), "formal_parameters") == 0)
    {
        if (ts_node_named_child_count(params) == 1)
        {
            pattern = ts_node_named_child(params, 0);
            single = 1;
        }
    }
    else
    {
        pattern = params;
        single = 1;
    }

    // Destructuring bindings (flat)
    int did = 0;
    Bindings binds;
    binds_init(&binds);
    const char *temp = NULL;
    char *param_default = NULL;  /* parameter-level default e.g. ({x}={}) => ... */
    if (single)
    {
        const char *pt = ts_node_type(pattern);
        if (strcmp(pt, "array_pattern") == 0)
        {
            temp = "_arr";
            did = collect_flat_destructure_bindings(pattern, src, temp, &binds);
        }
        else if (strcmp(pt, "object_pattern") == 0)
        {
            temp = "_obj";
            did = collect_flat_destructure_bindings(pattern, src, temp, &binds);
        }
        else if (strcmp(pt, "assignment_pattern") == 0)
        {
            TSNode left = ts_node_child_by_field_name(pattern, "left", 4);
            TSNode right = ts_node_child_by_field_name(pattern, "right", 5);
            if (!ts_node_is_null(left))
            {
                const char *lt = ts_node_type(left);
                if (strcmp(lt, "array_pattern") == 0)
                {
                    temp = "_arr";
                    did = collect_flat_destructure_bindings(left, src, temp, &binds);
                }
                else if (strcmp(lt, "object_pattern") == 0)
                {
                    temp = "_obj";
                    did = collect_flat_destructure_bindings(left, src, temp, &binds);
                }
            }
            /* capture the parameter default (e.g. {} in ({x}={}) => ...) */
            if (did && !ts_node_is_null(right))
            {
                size_t ds = ts_node_start_byte(right), de = ts_node_end_byte(right);
                param_default = malloc(de - ds + 1);
                memcpy(param_default, src + ds, de - ds);
                param_default[de - ds] = '\0';
            }
        }
    }

    char *rep = NULL;
    if (did && !is_block)
    {
        char *rew = rewrite_concise_body_with_bindings(src, body, &binds, claimed);
        rp_string *rbuf = rp_string_new(64);
        rp_string_appendf(rbuf, "function (%s) { ", temp);
        if (param_default)
            rp_string_appendf(rbuf, "if (%s === void 0) %s = %s; ", temp, temp, param_default);
        rp_string_appendf(rbuf, "return %s; }", rew);
        free(rew);
        rep = rp_string_steal(rbuf);
        rbuf = rp_string_free(rbuf);
    }
    else if (did && is_block)
    {
        size_t body_len = be - bs;
        const char *bsrc = src + bs;
        size_t brace = 0;
        while (brace < body_len && bsrc[brace] != '{')
            brace++;
        if (brace < body_len)
            brace++;

        rp_string *decls = rp_string_new(64);
        rp_string_puts(decls, "var ");
        for (size_t i = 0; i < binds.len; i++)
        {
            rp_string_puts(decls, binds.a[i].name);
            rp_string_puts(decls, "=");
            if (binds.a[i].defval)
            {
                rp_string_puts(decls, binds.a[i].repl);
                rp_string_puts(decls, " !== undefined ? ");
                rp_string_puts(decls, binds.a[i].repl);
                rp_string_puts(decls, " : ");
                rp_string_puts(decls, binds.a[i].defval);
            }
            else
            {
                rp_string_puts(decls, binds.a[i].repl);
            }
            if (i + 1 < binds.len)
                rp_string_puts(decls, ", ");
        }
        rp_string_puts(decls, "; ");

        /* Inject param default if present */
        rp_string *pdef = NULL;
        if (param_default)
        {
            pdef = rp_string_new(32);
            rp_string_appendf(pdef, "if (%s === void 0) %s = %s; ", temp, temp, param_default);
        }

        rp_string *rbuf2 = rp_string_new(128);
        rp_string_appendf(rbuf2, "function (%s) ", temp);
        rp_string_putsn(rbuf2, bsrc, brace); /* up to and including '{' */
        if (pdef)
            rp_string_puts(rbuf2, pdef->str);
        rp_string_puts(rbuf2, decls->str);
        rp_string_putsn(rbuf2, bsrc + brace, body_len - brace);
        rep = rp_string_steal(rbuf2);
        rbuf2 = rp_string_free(rbuf2);
        if (pdef)
            pdef = rp_string_free(pdef);
        decls = rp_string_free(decls);
    }
    else
    {
        int needs_paren = !slice_starts_with_paren(src, ps, pe);
        size_t cap = 96 + (pe - ps) + (be - bs) + 1;

        REMALLOC(rep, cap);

        if (is_block)
        {
            if (needs_paren)
                snprintf(rep, cap, "function (%.*s) %.*s", (int)(pe - ps), src + ps, (int)(be - bs), src + bs);
            else
                snprintf(rep, cap, "function %.*s %.*s", (int)(pe - ps), src + ps, (int)(be - bs), src + bs);
        }
        else
        {
            if (needs_paren)
                snprintf(rep, cap, "function (%.*s) { return %.*s; }", (int)(pe - ps), src + ps, (int)(be - bs),
                         src + bs);
            else
                snprintf(rep, cap, "function %.*s { return %.*s; }", (int)(pe - ps), src + ps, (int)(be - bs),
                         src + bs);
        }
    }

    // Inject default initializers for arrows if any assignment_pattern exists
    {
        int has_defaults = params_has_assignment_pattern(params);
        if (has_defaults && rep)
        {
            char *default_inits = build_param_default_inits(src, params);
            if (default_inits)
            {
                const char *fun_kw = "function ";
                char *p = strstr(rep, fun_kw);
                if (p)
                {
                    char *po = strchr(p + strlen(fun_kw), '(');
                    if (po)
                    {
                        // find closing paren
                        int dp = 0;
                        char *pc = po;
                        while (*pc)
                        {
                            if (*pc == '(')
                                dp++;
                            else if (*pc == ')')
                            {
                                dp--;
                                if (dp == 0)
                                    break;
                            }
                            pc++;
                        }
                        if (*pc == ')')
                        {
                            char *brace = strchr(rep, '{');
                            if (brace)
                            {
                                size_t head_len = (size_t)(po - rep);
                                size_t pre_block = (size_t)(brace - rep + 1);
                                size_t deflen = strlen(default_inits);
                                size_t new_len = head_len + 3 + (strlen(rep) - (po - rep)) + deflen;
                                char *nr = NULL;
                                REMALLOC(nr, new_len + 1);
                                size_t k = 0;
                                memcpy(nr + k, rep, head_len);
                                k += head_len;
                                memcpy(nr + k, "() ", 3);
                                k += 3;
                                memcpy(nr + k, pc + 1, pre_block - (pc + 1 - rep));
                                k += pre_block - (pc + 1 - rep);
                                memcpy(nr + k, default_inits, deflen);
                                k += deflen;
                                memcpy(nr + k, rep + pre_block, strlen(rep) - pre_block);
                                k += strlen(rep) - pre_block;
                                nr[k] = 0;
                                free(rep);
                                rep = nr;
                            }
                        }
                    }
                }
                free(default_inits);
            }
        }
    }
    if (bind_this)
    {
        size_t cap_bind = strlen(rep) + 20;
        char *wrapped = NULL;

        REMALLOC(wrapped, cap_bind);

        if (!wrapped)
        {
            fprintf(stderr, "oom\n");
            exit(1);
        }
        snprintf(wrapped, cap_bind, "(%s).bind(this)", rep);
        free(rep);
        rep = wrapped;
    }
    add_edit_take_ownership(edits, ns, ne, rep, claimed);
    binds_free(&binds);
    if (param_default)
        free(param_default);
    return 1;
}
static int rewrite_array_spread(EditList *edits, const char *src, TSNode arr, int isObject, RangeList *claimed,
                                uint32_t *polysneeded, int overlaps)
{
    (void)src;
    uint32_t cnt2, cnt1 = ts_node_child_count(arr);
    uint32_t i, j;
    char *out = NULL;
// ._addchain(rampart.__spreadO({},))
#define spreadsize 34
// ._concat({})
#define addcsize 12
#define isplain 0
#define isspread 1
#define newobj "_TrN_Sp._newObject()"
#define newobjsz 20
#define newarr "_TrN_Sp._newArray()"
#define newarrsz 19

    size_t needed = 1 + newobjsz; // for "_TrN_Sp._newObject()" and '\0'

    int nshort = 0;
    int nspread = 0;

    for (i = 0; i < cnt1; i++)
    {
        TSNode kid = ts_node_child(arr, i);
        if (strcmp(ts_node_type(kid), "spread_element") == 0)
        {
            cnt2 = ts_node_child_count(kid);
            for (j = 0; j < cnt2; j++)
            {
                TSNode gkid = ts_node_child(kid, j);
                /* Handle any expression after "..." (identifier, call_expression, etc.) */
                if (ts_node_is_named(gkid) && strcmp(ts_node_type(gkid), "spread_element") != 0)
                {
                    needed += ((ts_node_end_byte(gkid) - ts_node_start_byte(gkid)) + spreadsize);
                    nspread++;
                }
            }
        }
        else if (ts_node_is_named(kid))
        {
            if (strcmp(ts_node_type(kid), "shorthand_property_identifier") == 0)
            {
                needed += (ts_node_end_byte(kid) - ts_node_start_byte(kid)) * 2 + addcsize + 1; // x -> x:x
                nshort++;
            }
            else
                needed += (ts_node_end_byte(kid) - ts_node_start_byte(kid)) + addcsize;
        }
    }

    if (!nspread)
    {
        if (!nshort)
            return 0;

        for (i = 0; i < cnt1; i++)
        {
            TSNode kid = ts_node_child(arr, i);
            if (ts_node_is_named(kid))
            {
                if (strcmp(ts_node_type(kid), "shorthand_property_identifier") == 0)
                {
                    if (overlaps)
                        return 1;
                    size_t beg = ts_node_start_byte(kid), end = ts_node_end_byte(kid);
                    size_t kidsz = end - beg, repsz = 2 * (end - beg) + 2;
                    char rep[repsz];

                    memcpy(rep, src + beg, kidsz);
                    *(rep + kidsz) = ':';
                    memcpy(rep + kidsz + 1, src + beg, kidsz);
                    rep[repsz - 1] = '\0';
                    add_edit(edits, beg, end, rep, claimed);
                }
            }
        }
        return 1;
    }

    if (overlaps)
        return 1;

    /* process ...var */
    const char *fpref = "._addchain(_TrN_Sp.__spreadA([],";
    char open = '[', close = ']';
    int spos = 0;

    out = calloc(1, needed);
    if (isObject)
    {
        fpref = "._addchain(_TrN_Sp.__spreadO({},";
        open = '{';
        close = '}';
    }

#define addchar(c)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        *(out + spos++) = (c);                                                                                         \
    } while (0)

#define addstr(s, l)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        memcpy((out + spos), (s), l);                                                                                  \
        spos += l;                                                                                                     \
    } while (0)

    if (isObject)
        addstr(newobj, newobjsz);
    else
        addstr(newarr, newarrsz);

    int lasttype = -1;

    for (i = 0; i < cnt1; i++)
    {
        TSNode kid = ts_node_child(arr, i);

        if (strcmp(ts_node_type(kid), "spread_element") == 0)
        {
            cnt2 = ts_node_child_count(kid);
            for (j = 0; j < cnt2; j++)
            {
                TSNode gkid = ts_node_child(kid, j);
                /* Handle any expression after "..." */
                if (ts_node_is_named(gkid) && strcmp(ts_node_type(gkid), "spread_element") != 0)
                {
                    size_t start = ts_node_start_byte(gkid), end = ts_node_end_byte(gkid);
                    addstr(fpref, 32);
                    addstr(src + start, (end - start));
                    addchar(')');
                    addchar(')');
                    lasttype = isspread;
                }
            }
        }
        else if (ts_node_is_named(kid))
        {
            size_t start = ts_node_start_byte(kid), end = ts_node_end_byte(kid);

            if (lasttype == isplain)
            {
                spos -= 2; // go back to the }
                addchar(',');
            }
            else
            {
                addstr("._concat(", 9);
                addchar(open);
            }

            addstr(src + start, (end - start));
            if (strcmp(ts_node_type(kid), "shorthand_property_identifier") == 0)
            {
                addchar(':');
                addstr(src + start, (end - start));
            }
            addchar(close);
            addchar(')');
            lasttype = isplain;
        }
    }
#undef spreadsize
#undef addcsize
#undef isplain
#undef isspread
#undef addchar
#undef addstr
#undef newobj
#undef newobjsz
#undef newarr
#undef newarrsz
    // printf("strlen=%d, alloc'ed=%d + 1\n", strlen(out), (int)needed-1);
    // printf("edit is '%s' at %u\n", out, ts_node_start_byte(arr) );
    uint32_t ns = ts_node_start_byte(arr), ne = ts_node_end_byte(arr);
    add_edit_take_ownership(edits, ns, ne, out, claimed);
    *polysneeded |= SPREAD_PF;
    return 1;
}


// === Variable hoisting for async/generator state machines ===
// Collect all var/let/const identifier names declared at any nesting level
// in the function body, stopping at function/class boundaries.
// Returns a malloc'd comma-separated string of names, or NULL if none found.
static void _collect_var_names_recursive(const char *src, TSNode node, rp_string *names, int *first)
{
    const char *t = ts_node_type(node);

    // Stop at function/class boundaries — vars inside those are scoped there
    if (strstr(t, "function") || strcmp(t, "arrow_function") == 0 ||
        strstr(t, "class") || strstr(t, "method") != NULL)
        return;

    int is_decl = (strcmp(t, "variable_declaration") == 0 || strcmp(t, "lexical_declaration") == 0);
    if (is_decl)
    {
        uint32_t dc = ts_node_named_child_count(node);
        for (uint32_t d = 0; d < dc; d++)
        {
            TSNode declarator = ts_node_named_child(node, d);
            if (strcmp(ts_node_type(declarator), "variable_declarator") != 0)
                continue;
            TSNode nm = ts_node_child_by_field_name(declarator, "name", 4);
            if (ts_node_is_null(nm))
                continue;
            // Only handle simple identifiers (not destructuring patterns)
            if (strcmp(ts_node_type(nm), "identifier") != 0)
                continue;
            size_t ns = ts_node_start_byte(nm), ne = ts_node_end_byte(nm);
            if (!*first)
                rp_string_puts(names, ", ");
            rp_string_putsn(names, src + ns, ne - ns);
            *first = 0;
        }
        // Don't recurse into the declarators — we already handled them
        return;
    }

    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        _collect_var_names_recursive(src, ts_node_child(node, i), names, first);
}

static char *_collect_body_var_names(const char *src, TSNode body)
{
    if (strcmp(ts_node_type(body), "statement_block") != 0)
        return NULL;

    rp_string *names = rp_string_new(64);
    int first = 1;

    uint32_t sc = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < sc; i++)
    {
        TSNode stmt = ts_node_named_child(body, i);
        _collect_var_names_recursive(src, stmt, names, &first);
    }

    if (names->len == 0)
    {
        names = rp_string_free(names);
        return NULL;
    }

    char *ret = rp_string_steal(names);
    names = rp_string_free(names);
    return ret;
}

// Emit a variable_declaration or lexical_declaration as assignments (without the keyword).
// For declarators with initializers: "name = value;"
// For declarators without initializers: skipped (the hoisted decl handles it)
static void _emit_var_decl_as_assignments(rp_string *out, const char *src, TSNode decl)
{
    uint32_t dc = ts_node_named_child_count(decl);
    for (uint32_t d = 0; d < dc; d++)
    {
        TSNode declarator = ts_node_named_child(decl, d);
        if (strcmp(ts_node_type(declarator), "variable_declarator") != 0)
            continue;
        TSNode val = ts_node_child_by_field_name(declarator, "value", 5);
        if (ts_node_is_null(val))
            continue; // no initializer — skip
        TSNode nm = ts_node_child_by_field_name(declarator, "name", 4);
        if (ts_node_is_null(nm))
            continue;
        size_t ns = ts_node_start_byte(nm), ne = ts_node_end_byte(nm);
        size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
        rp_string_putsn(out, src + ns, ne - ns);
        rp_string_puts(out, " = ");
        rp_string_putsn(out, src + vs, ve - vs);
        rp_string_puts(out, ";");
    }
}

// === Async/Await -> _TrN_Sp.asyncToGenerator + _TrN_Sp.regeneratorRuntime (compact style) ===
typedef struct
{
    TSNode *a;
    size_t len, cap;
} _AsyncNodeVec;
static void _anv_push(_AsyncNodeVec *v, TSNode n)
{
    if (v->len == v->cap)
    {
        size_t nc = v->cap ? v->cap * 2 : 4;
        v->a = (TSNode *)realloc(v->a, nc * sizeof(TSNode));
        v->cap = nc;
    }
    v->a[v->len++] = n;
}
static void _collect_awaits_shallow(TSNode node, _AsyncNodeVec *out)
{
    const char *t = ts_node_type(node);
    if (strcmp(t, "await_expression") == 0)
    {
        _anv_push(out, node);
        return;
    }
    if (strstr(t, "function") || strcmp(t, "arrow_function") == 0 || strstr(t, "class") || strstr(t, "method") != NULL)
        return;
    uint32_t c = ts_node_child_count(node);
    for (uint32_t i = 0; i < c; i++)
        _collect_awaits_shallow(ts_node_child(node, i), out);
}

// Lower a statement containing 0..N awaits into state-machine steps
static void _emit_stmt_async_lower(rp_string *dst, const char *src, size_t ss, size_t se, TSNode stmt_node,
                                   int *p_next_label)
{
    _AsyncNodeVec av = {0};
    _collect_awaits_shallow(stmt_node, &av);
    if (av.len == 0)
    {
        rp_string_putsn(dst, src+ss, se-ss);
        if (av.a)
            free(av.a);
        return;
    }
    for (size_t i = 0; i + 1 < av.len; i++)
        for (size_t j = i + 1; j < av.len; j++)
            if (ts_node_start_byte(av.a[j]) < ts_node_start_byte(av.a[i]))
            {
                TSNode t = av.a[i];
                av.a[i] = av.a[j];
                av.a[j] = t;
            }
    size_t cursor = ss;

    rp_string *acc = rp_string_new(256);

    for (size_t k = 0; k < av.len; k++)
    {
        TSNode aw = av.a[k];
        TSNode arg = ts_node_child_by_field_name(aw, "argument", 8);
        if (ts_node_is_null(arg))
            arg = ts_node_named_child(aw, 0);
        *p_next_label += 3;
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%d", *p_next_label);
        // make sure there is a semicolon before writing context.next
        if(dst->len)
        {
            char *p = dst->str + dst->len-1;
            while( p > dst->str && isspace(*p))
                p--;
            if(*p!=';')
                rp_string_putc(dst, ';');
        }
        rp_string_puts(dst, "_context._y=true;_context.next = ");
        rp_string_puts(dst, tmp);
        rp_string_puts(dst, "; return (");

        size_t as = ts_node_start_byte(arg), ae = ts_node_end_byte(arg);
        rp_string_putsn(dst, src + as, ae-as);
        rp_string_puts(dst, ");");
        rp_string_puts(dst, " case ");
        rp_string_puts(dst, tmp);
        rp_string_puts(dst, ":");

        size_t aws = ts_node_start_byte(aw), awe = ts_node_end_byte(aw);
        rp_string_putsn(acc, src+cursor, aws-cursor);
        rp_string_puts(acc, "_context.sent");
        cursor = awe;
    }
    rp_string_putsn(acc, src+cursor, se-cursor);
    rp_string_puts(dst, acc->str);
    if (acc->len == 0 || acc->str[acc->len - 1] != ';')
        rp_string_puts(dst, ";");
    acc = rp_string_free(acc);
    if (av.a)
        free(av.a);
}

// Build the body: return _TrN_Sp.regeneratorRuntime.wrap(function
// _callee$(_context){while(1){switch(_context.prev=_context.next){case 0: ... }} , _callee);
static char *_build_regenerator_switch_body(const char *src, TSNode body)
{
    rp_string *out = rp_string_new(384);

    int next_label = 0;

    // Hoist var/let/const declarations so they persist across _callee$ invocations via closure
    char *hoisted = _collect_body_var_names(src, body);
    if (hoisted)
    {
        rp_string_puts(out, "var ");
        rp_string_puts(out, hoisted);
        rp_string_puts(out, ";");
        free(hoisted);
    }

    rp_string_puts(
        out,
        "return _TrN_Sp.regeneratorRuntime.wrap(function _callee$(_context){while(1){switch(_context.prev=_context.next){case 0:");
    const char *bt = ts_node_type(body);
    if (strcmp(bt, "statement_block") == 0)
    {
        uint32_t sc = ts_node_named_child_count(body);
        for (uint32_t i = 0; i < sc; i++)
        {
            TSNode stmt = ts_node_named_child(body, i);
            size_t ss = ts_node_start_byte(stmt), se = ts_node_end_byte(stmt);
            const char *stmt_type = ts_node_type(stmt);
            int is_var_decl = (strcmp(stmt_type, "variable_declaration") == 0 ||
                               strcmp(stmt_type, "lexical_declaration") == 0);

            // await detection + join-first-line behavior
            int has_await = 0;
            for (size_t k = ss; k + 5 < se; k++)
            {
                if (src[k] == 'a' && src[k + 1] == 'w' && src[k + 2] == 'a' && src[k + 3] == 'i' && src[k + 4] == 't')
                {
                    has_await = 1;
                    break;
                }
            }
            int next_has_await = 0;
            if (i + 1 < sc)
            {
                TSNode n2 = ts_node_named_child(body, i + 1);
                size_t s2 = ts_node_start_byte(n2), e2 = ts_node_end_byte(n2);
                for (size_t k = s2; k + 5 < e2; k++)
                {
                    if (src[k] == 'a' && src[k + 1] == 'w' && src[k + 2] == 'a' && src[k + 3] == 'i' &&
                        src[k + 4] == 't')
                    {
                        next_has_await = 1;
                        break;
                    }
                }
            }

            /* include white space (particularly \n) in the replacement */
            while(ss>0 && isspace(*(src + ss -1)) )
                ss--;

            // For var/let/const declarations with await: skip the keyword by
            // adjusting ss to start at the first declarator
            if (is_var_decl && has_await)
            {
                TSNode first_decl = ts_node_named_child(stmt, 0);
                if (!ts_node_is_null(first_decl))
                {
                    /* Emit leading newlines before adjusting ss past the keyword */
                    size_t stmt_s = ts_node_start_byte(stmt);
                    if (ss < stmt_s)
                        rp_string_putsn(out, src + ss, stmt_s - ss);
                    ss = ts_node_start_byte(first_decl);
                }
            }

            if (strcmp(stmt_type, "comment") == 0)
            {
                /* Convert // line comments to block comments so they don't
                   consume following state-machine code on the same line.
                   Preserve leading whitespace/newlines for line numbering. */
                size_t orig_ss = ts_node_start_byte(stmt);
                if (ss < orig_ss)
                    rp_string_putsn(out, src + ss, orig_ss - ss);
                if (se - orig_ss >= 2 && src[orig_ss] == '/' && src[orig_ss + 1] == '/')
                {
                    rp_string_puts(out, "/*");
                    rp_string_putsn(out, src + orig_ss + 2, se - orig_ss - 2);
                    rp_string_puts(out, "*/");
                }
                else
                    rp_string_putsn(out, src + orig_ss, se - orig_ss);
            }
            else if (is_var_decl && !has_await)
            {
                // Emit as assignments without var/let/const keyword
                _emit_var_decl_as_assignments(out, src, stmt);
            }
            else if (!has_await && next_has_await && i == 0)
                rp_string_putsn(out, src+ss, se-ss);
            else if (has_await)
                _emit_stmt_async_lower(out, src, ss, se, stmt, &next_label);
            else
                rp_string_putsn(out, src+ss, se-ss);
        }
    }
    else
    {
        // Concise arrow body: the expression is an implicit return.
        TSNode expr = body;
        size_t ss = ts_node_start_byte(expr), se = ts_node_end_byte(expr);
        rp_string *tmp = rp_string_new(64);
        _emit_stmt_async_lower(tmp, src, ss, se, expr, &next_label);
        if (strstr(tmp->str, "_context.next") == NULL)
        {
            rp_string_puts(out, " return ");
            rp_string_puts(out, tmp->str);
            rp_string_puts(out, ";");
        }
        else
        {
            // The await was lowered. The last segment (after the final "case N:")
            // contains _context.sent which is the value to implicitly return.
            // Insert "return " before that final segment.
            char *last_case = tmp->str;
            char *p;
            for (p = tmp->str; *p; p++)
            {
                if (p[0] == 'c' && p[1] == 'a' && p[2] == 's' && p[3] == 'e' && p[4] == ' ')
                    last_case = p;
            }
            // Find the ':' after "case N"
            char *colon = strchr(last_case, ':');
            if (colon)
            {
                // Emit everything up to and including the ':'
                rp_string_putsn(out, tmp->str, (size_t)(colon + 1 - tmp->str));
                rp_string_puts(out, "return ");
                rp_string_puts(out, colon + 1);
            }
            else
            {
                rp_string_puts(out, tmp->str);
            }
        }
        tmp = rp_string_free(tmp);
    }
    int end_label = next_label + 3;
    char etmp[24];
    snprintf(etmp, sizeof(etmp), "%d", end_label);
    // make sure there is a semicolon before writing case
    if(out->len)
    {
        char *p = out->str + out->len-1;
        while( p > out->str && isspace(*p))
            p--;
        if(*p!=';')
            rp_string_putc(out, ';');
    }
    rp_string_puts(out, "case ");
    rp_string_puts(out, etmp);
    rp_string_puts(out, ":case \"end\":return _context.stop();}}}, _callee, this);");
    char *ret = rp_string_steal(out);
    out=rp_string_free(out);
    return ret;
}
static void _append_params_sig(rp_string *out, const char *src, TSNode func_like)
{
    TSNode params = ts_node_child_by_field_name(func_like, "parameters", 10);
    if (!ts_node_is_null(params)) {
        size_t s = ts_node_start_byte(params), e=ts_node_end_byte(params);
        rp_string_putsn(out, src+s, e-s);
        return;
    }
    TSNode param = ts_node_child_by_field_name(func_like, "parameter", 9);
    if (!ts_node_is_null(param)) {
        size_t s = ts_node_start_byte(params), e=ts_node_end_byte(params);
        rp_string_puts(out, "(");
        rp_string_putsn(out, src+s, e-s);
        rp_string_putc(out, ')');
        return;
    }
    rp_string_puts(out, "()");
}

// Emitters: declaration and expression
static char *_emit_async_decl_replacement(const char *src, TSNode node)
{
    TSNode name = ts_node_child_by_field_name(node, "name", 4), body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body))
        return NULL;
    size_t ns = 0, ne = 0;
    const char *fallback = "_async";
    if (!ts_node_is_null(name))
    {
        ns = ts_node_start_byte(name);
        ne = ts_node_end_byte(name);
    }

    rp_string *out = rp_string_new(64);

    rp_string_puts(out, "function ");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, fallback);
    rp_string_puts(out, "() { return _");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, fallback);
    rp_string_puts(out, ".apply(this, arguments); };");
    rp_string_puts(out, "function _");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, fallback);
    rp_string_puts(out, "() {_");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, fallback);
    rp_string_puts(out, " = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function _callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    char *wrap = _build_regenerator_switch_body(src, body);
    if (!wrap)
    {
        out=rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "}));return _");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, fallback);
    rp_string_puts(out, ".apply(this, arguments);}");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}
static char *_emit_async_expr_replacement(const char *src, TSNode node)
{
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body))
        return NULL;

    rp_string *out = rp_string_new(768);
    rp_string_puts(out, "(function(){var _ref = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function _callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    char *wrap = _build_regenerator_switch_body(src, body);
    if (!wrap)
    {
        out = rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "})); return function(){ return _ref.apply(this, arguments); };})()");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

static char *_emit_async_method_replacement(const char *src, TSNode node)
{
    // node is method_definition inside an object literal (or class, but we only handle object here)
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    TSNode nname = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(body) || ts_node_is_null(nname))
        return NULL;
    size_t ns = ts_node_start_byte(nname), ne = ts_node_end_byte(nname);
    rp_string *out = rp_string_new(512);

    // property label: <name>:
    rp_string_putsn(out, src+ns, ne-ns);
    rp_string_puts(out, ": ");
    // value: (function(){ var _ref = asyncToGenerator(mark(function <name>(params){...}));
    //                     return function <name>(params){ return _ref.apply(this, arguments); };})()
    rp_string_puts(out, "(function(){var _ref = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function ");
    // Try to preserve method name in inner generator function for stack traces
    // When name is not an identifier (e.g., string literal), fallback to _callee
    const char *nt = ts_node_type(nname);
    int named = (strcmp(nt, "property_identifier") == 0 || strcmp(nt, "identifier") == 0);
    if (named)
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, "_callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    char *wrap = _build_regenerator_switch_body(src, body);
    if (!wrap)
    {
        out=rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "})); return function ");
    if (named)
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, "_callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " { return _ref.apply(this, arguments); };})()");

    char *ret = rp_string_steal(out);
    out=rp_string_free(out);
    return ret;
}

static int _is_async_function_like(TSNode node)
{
    const char *t = ts_node_type(node);
    if (!(strcmp(t, "function_declaration") == 0 || strcmp(t, "function_expression") == 0 ||
          strcmp(t, "function") == 0 || strcmp(t, "arrow_function") == 0 || strcmp(t, "method_definition") == 0))
        return 0;
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode k = ts_node_child(node, i);
        if (strcmp(ts_node_type(k), "async") == 0)
            return 1;
    }
    return 0;
}
static int rewrite_async_await_to_regenerator(EditList *edits, const char *src, TSNode node, RangeList *claimed,
                                              int overlaps)
{
    if (!_is_async_function_like(node))
        return 0;
    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);
    const char *t = ts_node_type(node);
    char *rep = NULL;
    if (strcmp(t, "function_declaration") == 0)
        rep = _emit_async_decl_replacement(src, node);
    else if (strcmp(t, "method_definition") == 0)
        rep = _emit_async_method_replacement(src, node);
    else
        rep = _emit_async_expr_replacement(src, node);
    if (!rep)
        return 0;

    if (overlaps)
    {
        if(rep)
            free(rep);
        return 1;
    }

    add_edit_take_ownership(edits, ns, ne, rep, claimed);
    return 1;
}
// === Generator functions -> regeneratorRuntime (same state machine, no asyncToGenerator wrapper) ===

static void _collect_yields_shallow(TSNode node, _AsyncNodeVec *out)
{
    const char *t = ts_node_type(node);
    if (strcmp(t, "yield_expression") == 0)
    {
        _anv_push(out, node);
        return;
    }
    if (strstr(t, "function") || strcmp(t, "arrow_function") == 0 || strstr(t, "class") || strstr(t, "method") != NULL)
        return;
    uint32_t c = ts_node_child_count(node);
    for (uint32_t i = 0; i < c; i++)
        _collect_yields_shallow(ts_node_child(node, i), out);
}

/* Quick text scan for "yield" keyword in a source range */
static int _text_has_yield(const char *src, size_t ss, size_t se)
{
    for (size_t k = ss; k + 5 <= se; k++)
        if (src[k] == 'y' && src[k+1] == 'i' && src[k+2] == 'e'
            && src[k+3] == 'l' && src[k+4] == 'd')
            return 1;
    return 0;
}

// Lower a statement containing 0..N yields into state-machine steps
static void _emit_stmt_yield_lower(rp_string *dst, const char *src, size_t ss, size_t se, TSNode stmt_node,
                                   int *p_next_label)
{
    _AsyncNodeVec av = {0};
    _collect_yields_shallow(stmt_node, &av);
    if (av.len == 0)
    {
        rp_string_putsn(dst, src+ss, se-ss);
        if (av.a)
            free(av.a);
        return;
    }
    for (size_t i = 0; i + 1 < av.len; i++)
        for (size_t j = i + 1; j < av.len; j++)
            if (ts_node_start_byte(av.a[j]) < ts_node_start_byte(av.a[i]))
            {
                TSNode t = av.a[i];
                av.a[i] = av.a[j];
                av.a[j] = t;
            }
    size_t cursor = ss;

    rp_string *acc = rp_string_new(256);

    for (size_t k = 0; k < av.len; k++)
    {
        TSNode yw = av.a[k];
        // yield_expression's first named child is the argument (if any)
        TSNode arg = ts_node_named_child(yw, 0);
        *p_next_label += 3;
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%d", *p_next_label);
        if(dst->len)
        {
            char *p = dst->str + dst->len-1;
            while( p > dst->str && isspace(*p))
                p--;
            if(*p!=';')
                rp_string_putc(dst, ';');
        }
        rp_string_puts(dst, "_context._y=true;_context.next = ");
        rp_string_puts(dst, tmp);
        rp_string_puts(dst, "; return (");

        if (!ts_node_is_null(arg))
        {
            size_t as = ts_node_start_byte(arg), ae = ts_node_end_byte(arg);
            rp_string_putsn(dst, src + as, ae-as);
        }
        else
        {
            rp_string_puts(dst, "undefined");
        }
        rp_string_puts(dst, ");");
        rp_string_puts(dst, " case ");
        rp_string_puts(dst, tmp);
        rp_string_puts(dst, ":");

        size_t yws = ts_node_start_byte(yw), ywe = ts_node_end_byte(yw);
        rp_string_putsn(acc, src+cursor, yws-cursor);
        rp_string_puts(acc, "_context.sent");
        cursor = ywe;
    }
    rp_string_putsn(acc, src+cursor, se-cursor);
    rp_string_puts(dst, acc->str);
    if (acc->len == 0 || acc->str[acc->len - 1] != ';')
        rp_string_puts(dst, ";");
    acc = rp_string_free(acc);
    if (av.a)
        free(av.a);
}

/* Process children of a statement_block, lowering yields into state-machine
   cases.  Recursively decomposes while/for loops that contain yields. */
static void _emit_yield_body(rp_string *out, const char *src, TSNode block, int *p_next_label)
{
    uint32_t sc = ts_node_named_child_count(block);
    for (uint32_t i = 0; i < sc; i++)
    {
        TSNode stmt = ts_node_named_child(block, i);
        size_t ss = ts_node_start_byte(stmt), se = ts_node_end_byte(stmt);
        const char *stmt_type = ts_node_type(stmt);
        int is_var_decl = (strcmp(stmt_type, "variable_declaration") == 0 ||
                           strcmp(stmt_type, "lexical_declaration") == 0);
        int has_yield = _text_has_yield(src, ss, se);

        /* Back up whitespace for line-number preservation */
        while (ss > 0 && isspace(*(src + ss - 1)))
            ss--;

        /* Strip var/let/const keyword for declarations containing yield */
        if (is_var_decl && has_yield)
        {
            TSNode first_decl = ts_node_named_child(stmt, 0);
            if (!ts_node_is_null(first_decl))
            {
                /* Emit leading newlines before adjusting ss past the keyword */
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s)
                    rp_string_putsn(out, src + ss, stmt_s - ss);
                ss = ts_node_start_byte(first_decl);
            }
        }

        if (strcmp(stmt_type, "comment") == 0)
        {
            /* Convert // to block comment */
            size_t orig_ss = ts_node_start_byte(stmt);
            if (ss < orig_ss)
                rp_string_putsn(out, src + ss, orig_ss - ss);
            if (se - orig_ss >= 2 && src[orig_ss] == '/' && src[orig_ss + 1] == '/')
            {
                rp_string_puts(out, "/*");
                rp_string_putsn(out, src + orig_ss + 2, se - orig_ss - 2);
                rp_string_puts(out, "*/");
            }
            else
                rp_string_putsn(out, src + orig_ss, se - orig_ss);
        }
        else if (is_var_decl && !has_yield)
        {
            _emit_var_decl_as_assignments(out, src, stmt);
        }
        else if (strcmp(stmt_type, "while_statement") == 0 && has_yield)
        {
            /* Decompose: while (cond) { body }
               -> case COND: if(!cond){_context.next=EXIT;break;}
                  <body stmts with yields lowered>
                  _context.next=COND;break;
                  case EXIT: */
            TSNode cond = ts_node_child_by_field_name(stmt, "condition", 9);
            TSNode wbody = ts_node_child_by_field_name(stmt, "body", 4);

            *p_next_label += 3;
            int cond_label = *p_next_label;
            char ctmp[24];
            snprintf(ctmp, sizeof(ctmp), "%d", cond_label);

            /* Process body first so yield labels are allocated */
            rp_string *wbuf = rp_string_new(256);
            if (!ts_node_is_null(wbody) && strcmp(ts_node_type(wbody), "statement_block") == 0)
                _emit_yield_body(wbuf, src, wbody, p_next_label);

            *p_next_label += 3;
            int exit_label = *p_next_label;
            char etmp[24];
            snprintf(etmp, sizeof(etmp), "%d", exit_label);

            /* Condition check */
            rp_string_puts(out, "case ");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ":");
            if (!ts_node_is_null(cond))
            {
                size_t cs = ts_node_start_byte(cond), ce = ts_node_end_byte(cond);
                rp_string_puts(out, "if(!");
                rp_string_putsn(out, src + cs, ce - cs);
                rp_string_puts(out, "){_context.next=");
                rp_string_puts(out, etmp);
                rp_string_puts(out, ";break;}");
            }

            /* Body */
            rp_string_puts(out, wbuf->str);
            wbuf = rp_string_free(wbuf);

            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');

            /* Loop back */
            rp_string_puts(out, "_context.next=");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ";break;");

            /* Exit label */
            rp_string_puts(out, "case ");
            rp_string_puts(out, etmp);
            rp_string_puts(out, ":");
        }
        else if (strcmp(stmt_type, "for_statement") == 0 && has_yield)
        {
            /* Decompose: for (init; cond; incr) { body }
               -> init; case COND: if(!(cond)){..break;}
                  <body> incr; _context.next=COND;break;
                  case EXIT: */
            TSNode init = ts_node_child_by_field_name(stmt, "initializer", 11);
            TSNode cond = ts_node_child_by_field_name(stmt, "condition", 9);
            TSNode incr = ts_node_child_by_field_name(stmt, "increment", 9);
            TSNode fbody = ts_node_child_by_field_name(stmt, "body", 4);

            /* Emit initializer (skip empty_statement) */
            if (!ts_node_is_null(init) && strcmp(ts_node_type(init), "empty_statement") != 0)
            {
                size_t is2 = ts_node_start_byte(init), ie2 = ts_node_end_byte(init);
                rp_string_putsn(out, src + is2, ie2 - is2);
                if (src[ie2 - 1] != ';')
                    rp_string_putc(out, ';');
            }

            *p_next_label += 3;
            int cond_label = *p_next_label;
            char ctmp[24];
            snprintf(ctmp, sizeof(ctmp), "%d", cond_label);

            rp_string *fbuf = rp_string_new(256);
            if (!ts_node_is_null(fbody) && strcmp(ts_node_type(fbody), "statement_block") == 0)
                _emit_yield_body(fbuf, src, fbody, p_next_label);

            *p_next_label += 3;
            int exit_label = *p_next_label;
            char etmp[24];
            snprintf(etmp, sizeof(etmp), "%d", exit_label);

            rp_string_puts(out, "case ");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ":");

            /* Condition check (skip if empty/missing — infinite loop) */
            if (!ts_node_is_null(cond) && strcmp(ts_node_type(cond), "empty_statement") != 0)
            {
                size_t cs = ts_node_start_byte(cond), ce = ts_node_end_byte(cond);
                /* Strip trailing ; from expression_statement */
                while (ce > cs && (src[ce - 1] == ';' || isspace(src[ce - 1])))
                    ce--;
                rp_string_puts(out, "if(!(");
                rp_string_putsn(out, src + cs, ce - cs);
                rp_string_puts(out, ")){_context.next=");
                rp_string_puts(out, etmp);
                rp_string_puts(out, ";break;}");
            }

            rp_string_puts(out, fbuf->str);
            fbuf = rp_string_free(fbuf);

            /* Increment */
            if (!ts_node_is_null(incr))
            {
                size_t is2 = ts_node_start_byte(incr), ie2 = ts_node_end_byte(incr);
                rp_string_putsn(out, src + is2, ie2 - is2);
                rp_string_putc(out, ';');
            }

            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');

            rp_string_puts(out, "_context.next=");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ";break;");

            rp_string_puts(out, "case ");
            rp_string_puts(out, etmp);
            rp_string_puts(out, ":");
        }
        else if (has_yield)
        {
            _emit_stmt_yield_lower(out, src, ss, se, stmt, p_next_label);
        }
        else
        {
            rp_string_putsn(out, src + ss, se - ss);
        }
    }
}

static char *_build_regenerator_switch_body_for_yield(const char *src, TSNode body)
{
    rp_string *out = rp_string_new(384);

    int next_label = 0;

    // Hoist var/let/const declarations so they persist across _callee$ invocations via closure
    char *hoisted = _collect_body_var_names(src, body);
    if (hoisted)
    {
        rp_string_puts(out, "var ");
        rp_string_puts(out, hoisted);
        rp_string_puts(out, ";");
        free(hoisted);
    }

    rp_string_puts(
        out,
        "return _TrN_Sp.regeneratorRuntime.wrap(function _callee$(_context){while(1){switch(_context.prev=_context.next){case 0:");
    const char *bt = ts_node_type(body);
    if (strcmp(bt, "statement_block") == 0)
    {
        _emit_yield_body(out, src, body, &next_label);
    }
    else
    {
        // Concise arrow body: the expression is an implicit return.
        TSNode expr = body;
        size_t ss = ts_node_start_byte(expr), se = ts_node_end_byte(expr);
        rp_string *tmp = rp_string_new(64);
        _emit_stmt_yield_lower(tmp, src, ss, se, expr, &next_label);
        if (strstr(tmp->str, "_context.next") == NULL)
        {
            rp_string_puts(out, " return ");
            rp_string_puts(out, tmp->str);
            rp_string_puts(out, ";");
        }
        else
        {
            // The yield was lowered. The last segment (after the final "case N:")
            // contains _context.sent which is the value to implicitly return.
            // Insert "return " before that final segment.
            char *last_case = tmp->str;
            char *p;
            for (p = tmp->str; *p; p++)
            {
                if (p[0] == 'c' && p[1] == 'a' && p[2] == 's' && p[3] == 'e' && p[4] == ' ')
                    last_case = p;
            }
            char *colon = strchr(last_case, ':');
            if (colon)
            {
                rp_string_putsn(out, tmp->str, (size_t)(colon + 1 - tmp->str));
                rp_string_puts(out, "return ");
                rp_string_puts(out, colon + 1);
            }
            else
            {
                rp_string_puts(out, tmp->str);
            }
        }
        tmp = rp_string_free(tmp);
    }
    int end_label = next_label + 3;
    char etmp[24];
    snprintf(etmp, sizeof(etmp), "%d", end_label);
    if(out->len)
    {
        char *p = out->str + out->len-1;
        while( p > out->str && isspace(*p))
            p--;
        if(*p!=';')
            rp_string_putc(out, ';');
    }
    rp_string_puts(out, "case ");
    rp_string_puts(out, etmp);
    rp_string_puts(out, ":case \"end\":return _context.stop();}}}, null, this);");
    char *ret = rp_string_steal(out);
    out=rp_string_free(out);
    return ret;
}

static int _is_generator_function_like(const char *src, TSNode node)
{
    const char *t = ts_node_type(node);
    if (strcmp(t, "generator_function_declaration") == 0 || strcmp(t, "generator_function") == 0 ||
        strcmp(t, "generator_function_expression") == 0)
        return 1;
    if (strcmp(t, "method_definition") == 0)
    {
        uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; i++)
        {
            TSNode k = ts_node_child(node, i);
            if (!ts_node_is_named(k))
            {
                size_t ks = ts_node_start_byte(k), ke = ts_node_end_byte(k);
                if (ke - ks == 1 && src[ks] == '*')
                    return 1;
            }
        }
    }
    return 0;
}

// generator_function_declaration: function* name(params) { body }
// -> var name = _TrN_Sp.regeneratorRuntime.mark(function name(params) { <switch body> });
static char *_emit_generator_decl_replacement(const char *src, TSNode node)
{
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body))
        return NULL;
    size_t ns = 0, ne = 0;
    if (!ts_node_is_null(name))
    {
        ns = ts_node_start_byte(name);
        ne = ts_node_end_byte(name);
    }

    rp_string *out = rp_string_new(256);
    rp_string_puts(out, "var ");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, "_gen");
    rp_string_puts(out, " = _TrN_Sp.regeneratorRuntime.mark(function ");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, "_gen");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    char *wrap = _build_regenerator_switch_body_for_yield(src, body);
    if (!wrap)
    {
        out = rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "})");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

// generator method in object literal: *name(params) { body }
// -> name: _TrN_Sp.regeneratorRuntime.mark(function name(params) { <switch body> })
static char *_emit_generator_method_replacement(const char *src, TSNode node)
{
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    TSNode nname = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(body) || ts_node_is_null(nname))
        return NULL;
    size_t ns = ts_node_start_byte(nname), ne = ts_node_end_byte(nname);
    const char *nt = ts_node_type(nname);
    int named = (strcmp(nt, "property_identifier") == 0 || strcmp(nt, "identifier") == 0);

    rp_string *out = rp_string_new(512);
    rp_string_putsn(out, src+ns, ne-ns);
    rp_string_puts(out, ": _TrN_Sp.regeneratorRuntime.mark(function ");
    if (named)
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, "_callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    char *wrap = _build_regenerator_switch_body_for_yield(src, body);
    if (!wrap)
    {
        out = rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "})");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

// generator function expression: function*(params) { body }
// -> _TrN_Sp.regeneratorRuntime.mark(function name(params) { <switch body> })
static char *_emit_generator_expr_replacement(const char *src, TSNode node)
{
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body))
        return NULL;

    rp_string *out = rp_string_new(256);
    rp_string_puts(out, "_TrN_Sp.regeneratorRuntime.mark(function ");
    if (!ts_node_is_null(name))
    {
        size_t ns = ts_node_start_byte(name), ne = ts_node_end_byte(name);
        rp_string_putsn(out, src+ns, ne-ns);
    }
    else
        rp_string_puts(out, "_gen");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    char *wrap = _build_regenerator_switch_body_for_yield(src, body);
    if (!wrap)
    {
        out = rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "})");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

static int rewrite_generator_to_regenerator(EditList *edits, const char *src, TSNode node, RangeList *claimed,
                                            int overlaps)
{
    if (!_is_generator_function_like(src, node))
        return 0;
    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);
    const char *t = ts_node_type(node);
    char *rep = NULL;
    if (strcmp(t, "generator_function_declaration") == 0)
        rep = _emit_generator_decl_replacement(src, node);
    else if (strcmp(t, "method_definition") == 0)
        rep = _emit_generator_method_replacement(src, node);
    else
        rep = _emit_generator_expr_replacement(src, node);
    if (!rep)
        return 0;

    if (overlaps)
    {
        if(rep)
            free(rep);
        return 1;
    }

    add_edit_take_ownership(edits, ns, ne, rep, claimed);
    return 1;
}
// === End generator pass ===

// === End async/await pass ===

// let/const -> var (token edit)
static void collect_ids_from_pattern(const char *src, TSNode name_node, rp_string *params, rp_string *args)
{
    // Simple fixed-size stack; grow if needed
    TSNode stack[256];
    int top = 0;
    stack[top++] = name_node;
    int first = (params->len == 0);

    while (top > 0)
    {
        TSNode cur = stack[--top];
        const char *t = ts_node_type(cur);
        if (strcmp(t, "identifier") == 0 || strcmp(t, "shorthand_property_identifier_pattern") == 0)
        {
            size_t ns = ts_node_start_byte(cur);
            size_t ne = ts_node_end_byte(cur);
            if (!first)
            {
                rp_string_putc(params, ',');
                rp_string_putc(args, ',');
            }
            rp_string_putsn(params, src + ns, ne - ns);
            rp_string_putsn(args, src + ns, ne - ns);
            first = 0;
            continue;
        }
        uint32_t cc = ts_node_child_count(cur);
        for (uint32_t j = 0; j < cc; j++)
        {
            TSNode ch = ts_node_child(cur, j);
            if (!ts_node_is_null(ch))
            {
                if (top < 256)
                    stack[top++] = ch;
            }
        }
    }
}

static int span_has_flow_ctrl_tokens(const char *src, size_t s, size_t e)
{
    size_t len = (e > s) ? (e - s) : 0;
    if (len == 0)
        return 0;
    const char *p = src + s;
    if (memmem(p, len, "break", 5))
        return 1;
    if (memmem(p, len, "continue", 8))
        return 1;
    if (memmem(p, len, "return", 6))
        return 1;
    return 0;
}

static int rewrite_lexical_declaration(EditList *edits, const char *src, TSNode lexical_decl, RangeList *claimed,
                                       int overlaps, int no_program_wrap)
{
    int ret = 0;
    uint32_t c = ts_node_child_count(lexical_decl);
    int have_let = 0;

    // --- 1) Replace 'let'/'const' keyword with 'var'
    for (uint32_t i = 0; i < c; i++)
    {
        TSNode kid = ts_node_child(lexical_decl, i);
        if (ts_node_is_named(kid))
            break;
        const char *kw = ts_node_type(kid);
        if (strcmp(kw, "let") == 0)
        {
            if (overlaps)
                return 1;
            add_edit(edits, ts_node_start_byte(kid), ts_node_end_byte(kid), "var", NULL);
            have_let = 1;
            break;
        }

        if (strcmp(kw, "const") == 0)
        {
            if (overlaps)
                return 1;

            add_edit(edits, ts_node_start_byte(kid), ts_node_end_byte(kid), "var  ", NULL);
            have_let = 1;
            break;
        }
    }

    if (!have_let)
        return ret;

    // 2) Handle special contexts
    TSNode parent = ts_node_parent(lexical_decl);
    if (!ts_node_is_null(parent))
    {
        const char *ptype = ts_node_type(parent);
        int in_for = (strcmp(ptype, "for_statement") == 0) || (strcmp(ptype, "for_in_statement") == 0) ||
                     (strcmp(ptype, "for_of_statement") == 0);

        if (in_for)
        {
            // Only when this decl is the initializer (for_statement) or left (for_in_statement/for_of)
            TSNode init = ts_node_child_by_field_name(parent, "initializer", 11);
            if (ts_node_is_null(init))
                init = ts_node_child_by_field_name(parent, "left", 4);
            if (!ts_node_is_null(init) && ts_node_start_byte(init) == ts_node_start_byte(lexical_decl))
            {
                // Collect identifiers from all declarators (destructuring supported)
                rp_string *params = rp_string_new(64);
                rp_string *args = rp_string_new(64);

                uint32_t nchild = ts_node_child_count(lexical_decl);
                for (uint32_t k = 0; k < nchild; k++)
                {
                    TSNode dec = ts_node_child(lexical_decl, k);
                    if (strcmp(ts_node_type(dec), "variable_declarator") == 0)
                    {
                        TSNode name = ts_node_child_by_field_name(dec, "name", 4);
                        if (!ts_node_is_null(name))
                            collect_ids_from_pattern(src, name, params, args);
                    }
                }

                if (params->len > 0)
                {
                    TSNode body = ts_node_child_by_field_name(parent, "body", 4);
                    if (!ts_node_is_null(body))
                    {
                        size_t bs = ts_node_start_byte(body);
                        size_t be = ts_node_end_byte(body);
                        int is_block = (strcmp(ts_node_type(body), "statement_block") == 0);

                        rp_string *pref = rp_string_new(64);
                        rp_string *suff = rp_string_new(64);
                        rp_string_puts(pref, "(function(");
                        rp_string_putsn(pref, params->str ? params->str : "", params->len);
                        rp_string_puts(pref, "){ ");
                        rp_string_puts(suff, " })( ");
                        rp_string_putsn(suff, args->str ? args->str : "", args->len);
                        rp_string_puts(suff, " );");

                        if (is_block)
                        {
                            add_edit_take_ownership(edits, bs + 1, bs + 1, rp_string_steal(pref), claimed);
                            add_edit_take_ownership(edits, be - 1, be - 1, rp_string_steal(suff), claimed);
                        }
                        else
                        {
                            add_edit_take_ownership(edits, bs, bs, rp_string_steal(pref), claimed);
                            add_edit_take_ownership(edits, be, be, rp_string_steal(suff), claimed);
                        }
                        pref=rp_string_free(pref);
                        suff=rp_string_free(suff);
                    }
                }

                params = rp_string_free(params);
                args = rp_string_free(args);
            }
        }
        else
        {
            // Not in a for-header. Consider block-wrapping for plain blocks to preserve let scope.
            // Find nearest enclosing statement_block stopping at structural boundaries.
            TSNode anc = parent;
            TSNode block = (TSNode){0};
            while (!ts_node_is_null(anc))
            {
                const char *t = ts_node_type(anc);
                if (strcmp(t, "statement_block") == 0)
                {
                    block = anc;
                    break;
                }
                if (strcmp(t, "function") == 0 || strcmp(t, "function_declaration") == 0 ||
                    strcmp(t, "method_definition") == 0 || strcmp(t, "arrow_function") == 0 ||
                    strcmp(t, "class_body") == 0 || strcmp(t, "program") == 0 || strcmp(t, "switch_statement") == 0 ||
                    strcmp(t, "for_statement") == 0 || strcmp(t, "for_in_statement") == 0 ||
                    strcmp(t, "for_of_statement") == 0 || strcmp(t, "while_statement") == 0 ||
                    strcmp(t, "do_statement") == 0)
                {
                    break;
                }
                anc = ts_node_parent(anc);
            }
            if (!ts_node_is_null(block))
            {
                size_t bs = ts_node_start_byte(block);
                size_t be = ts_node_end_byte(block);
                if (!span_has_flow_ctrl_tokens(src, bs, be))
                {
                    add_edit(edits, bs + 1, bs + 1, "(function(){ ", claimed);
                    add_edit(edits, be - 1, be - 1, " }());", claimed);
                }
            }

            /* Top-level let/const at program level: converting to var is sufficient.
               IIFE wrapping would break eval() since the C eval replacement
               always runs as indirect eval (global scope). */
        }
    }

    return 1;
}

static int rewrite_for_of_destructuring(EditList *edits, const char *src, TSNode forof, RangeList *claimed,
                                        uint32_t *polysneeded, int overlaps)
{
    // Handle `for (let <pattern> of <right>) <body>`
    TSNode left = ts_node_child_by_field_name(forof, "left", 4);
    TSNode right = ts_node_child_by_field_name(forof, "right", 5);
    TSNode body = ts_node_child_by_field_name(forof, "body", 4);
    if (ts_node_is_null(left) || ts_node_is_null(right) || ts_node_is_null(body))
        return 0;

    const char *lt = ts_node_type(left);
    TSNode pattern = {{0}};
    TSNode kind = ts_node_child_by_field_name(forof, "kind", 4); // 'let' token when left is a pattern

    if (strcmp(lt, "lexical_declaration") == 0 || strcmp(lt, "variable_declaration") == 0)
    {
        // Expect one declarator with a destructuring name
        if (ts_node_named_child_count(left) != 1)
            return 0;
        TSNode decl = ts_node_named_child(left, 0);
        if (strcmp(ts_node_type(decl), "variable_declarator") != 0)
            return 0;
        TSNode name = ts_node_child_by_field_name(decl, "name", 4);
        if (ts_node_is_null(name))
            return 0;
        const char *nt = ts_node_type(name);
        if (!(strcmp(nt, "array_pattern") == 0 || strcmp(nt, "object_pattern") == 0 ||
              strcmp(nt, "assignment_pattern") == 0))
            return 0;
        pattern = name;
    }
    else
    {
        // Tree-sitter shape: left itself is the pattern; there should be a 'kind' token "let"/"const"
        if (ts_node_is_null(kind))
            return 0;
        const char *nt = ts_node_type(left);
        if (!(strcmp(nt, "array_pattern") == 0 || strcmp(nt, "object_pattern") == 0 ||
              strcmp(nt, "assignment_pattern") == 0))
            return 0;
        pattern = left;
    }

    const char *pt = ts_node_type(pattern);
    if (strcmp(pt, "array_pattern") != 0 && strcmp(pt, "object_pattern") != 0)
        return 0;

    /* Object pattern: use collect_flat_destructure_bindings approach */
    if (strcmp(pt, "object_pattern") == 0)
    {
        if (overlaps)
            return 1;
        *polysneeded |= FOROF_PF;

        char tmpvar[32];
        snprintf(tmpvar, sizeof(tmpvar), "_dof%u", ++_destr_counter);

        Bindings binds;
        binds_init(&binds);
        if (!collect_flat_destructure_bindings(pattern, src, tmpvar, &binds))
        {
            binds_free(&binds);
            return 0;
        }

        size_t fs = ts_node_start_byte(forof), fe = ts_node_end_byte(forof);
        size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
        size_t bs = ts_node_start_byte(body), be = ts_node_end_byte(body);
        int is_block = (strcmp(ts_node_type(body), "statement_block") == 0);

        rp_string *out = rp_string_new(256);
        /* Emit: var _x = <right>, _it = ...; while (...) { var _dof = ...; var n = _dof.name; body } */
        rp_string_puts(out, "var _x = ");
        rp_string_putsn(out, src + rs, re - rs);
        rp_string_puts(out, ", _it = (typeof Symbol!=='undefined'&&typeof _x[Symbol.iterator]==='function')?_x[Symbol.iterator]():null, _i = 0, _r; while(_it?!(_r=_it.next()).done:_i<_x.length) { var ");
        rp_string_puts(out, tmpvar);
        rp_string_puts(out, " = _it?_r.value:_x[_i++]; ");

        for (size_t i = 0; i < binds.len; i++)
        {
            rp_string_puts(out, "var ");
            rp_string_puts(out, binds.a[i].name);
            rp_string_puts(out, " = ");
            if (binds.a[i].defval)
            {
                rp_string_puts(out, binds.a[i].repl);
                rp_string_puts(out, " !== undefined ? ");
                rp_string_puts(out, binds.a[i].repl);
                rp_string_puts(out, " : ");
                rp_string_puts(out, binds.a[i].defval);
            }
            else
                rp_string_puts(out, binds.a[i].repl);
            rp_string_puts(out, "; ");
        }

        if (is_block)
            rp_string_putsn(out, src + bs + 1, (be - 1) - (bs + 1));
        else
            rp_string_putsn(out, src + bs, be - bs);
        rp_string_puts(out, " }");

        binds_free(&binds);
        add_edit_take_ownership(edits, fs, fe, rp_string_steal(out), claimed);
        out = rp_string_free(out);
        return 1;
    }

    /* Array pattern handling below */

    if (overlaps)
        return 1;

    *polysneeded |= FOROF_PF;

    // Gather identifiers and their indices, and compute N for slicedToArray.
    typedef struct
    {
        char *name;
        int index;
    } ArrBind;
    ArrBind *arr = NULL;
    size_t alen = 0, acap = 0;
    int idx = 0;
    int last_was_value = 0; // have we just consumed a value element?
    uint32_t c = ts_node_child_count(pattern);
    for (uint32_t i = 0; i < c; i++)
    {
        TSNode ch = ts_node_child(pattern, i);
        if (!ts_node_is_named(ch))
        {
            const char *tok = ts_node_type(ch);
            if (strcmp(tok, ",") == 0)
            {
                if (!last_was_value)
                {
                    // elision advances index
                    idx++;
                }
                // after a comma, we are not "just consumed value"
                last_was_value = 0;
            }
            else if (strcmp(tok, "[") == 0)
            {
                last_was_value = 0;
            }
            continue;
        }

        const char *ct = ts_node_type(ch);
        if (strcmp(ct, "identifier") == 0)
        {
            size_t ns = ts_node_start_byte(ch), ne = ts_node_end_byte(ch);
            char *nm = strndup(src + ns, ne - ns);
            if (alen == acap)
            {
                acap = acap ? acap * 2 : 4;
                REMALLOC(arr, acap * sizeof(ArrBind));
            }
            arr[alen++] = (ArrBind){nm, idx};
            idx++; // position after this element
            last_was_value = 1;
        }
        else if (strcmp(ct, "assignment_pattern") == 0)
        {
            TSNode left_id = ts_node_child_by_field_name(ch, "left", 4);
            if (ts_node_is_null(left_id) || strcmp(ts_node_type(left_id), "identifier") != 0)
            { /* unsupported */
            }
            else
            {
                size_t ns = ts_node_start_byte(left_id), ne = ts_node_end_byte(left_id);
                char *nm = strndup(src + ns, ne - ns);
                if (alen == acap)
                {
                    acap = acap ? acap * 2 : 4;
                    REMALLOC(arr, acap * sizeof(ArrBind));
                }
                arr[alen++] = (ArrBind){nm, idx};
            }
            idx++;
            last_was_value = 1;
        }
        else if (strcmp(ct, "rest_pattern") == 0)
        {
            // ignore for now
            last_was_value = 1;
        }
        else
        {
            // nested pattern not supported in this pass
            // clean up
            for (size_t k = 0; k < alen; k++)
                free(arr[k].name);
            free(arr);
            return 0;
        }
    }
    int N = idx; // number of slots up to last specified element (including elisions)

    // Build replacement code
    size_t fs = ts_node_start_byte(forof), fe = ts_node_end_byte(forof);
    size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
    size_t bs = ts_node_start_byte(body), be = ts_node_end_byte(body);

    int is_block = (strcmp(ts_node_type(body), "statement_block") == 0);

    rp_string *out = rp_string_new(256);

    // _loop declaration
    rp_string_puts(out, "var _loop = function _loop() { var _pairs$_i = _TrN_Sp.slicedToArray(_pairs[_i], ");
    rp_string_appendf(out, "%d", N);
    rp_string_puts(out, "), ");

    // bindings: a = _pairs$_i[0], b = _pairs$_i[1];
    for (size_t k = 0; k < alen; k++)
    {
        if (k)
            rp_string_puts(out, ",");
        rp_string_puts(out, arr[k].name);
        rp_string_puts(out, " = _pairs$_i[");
        char ibuf[32];
        snprintf(ibuf, sizeof(ibuf), "%d", arr[k].index);
        rp_string_puts(out, ibuf);
        rp_string_puts(out, "]");
    }
    rp_string_puts(out, "; ");

    // body content
    if (is_block)
    {
        // insert inner of block
        rp_string_putsn(out, src + bs + 1, (be - 1) - (bs + 1));
        rp_string_puts(out, " }; ");
    }
    else
    {
        rp_string_putsn(out, src + bs, be - bs);
        rp_string_puts(out, "; }; ");
    }

    // for loop header using array length
    rp_string_puts(out, "for (var _i = 0, _pairs = ");
    rp_string_putsn(out, src + rs, re - rs);
    rp_string_puts(out, "; _i < _pairs.length; _i++) { _loop(); }");

    // Replace entire for-of statement
    add_edit_take_ownership(edits, fs, fe, rp_string_steal(out), claimed);

    out = rp_string_free(out);

    // cleanup
    for (size_t k = 0; k < alen; k++)
        free(arr[k].name);
    free(arr);

    return 1;
}

// ============== unified dispatcher/pass ==============

static int hexv(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return -1;
}
static void cp_to_surrogates_ul(unsigned long cp, unsigned *hi, unsigned *lo)
{
    unsigned long v = cp - 0x10000UL;
    *hi = 0xD800u + (unsigned)((v >> 10) & 0x3FFu);
    *lo = 0xDC00u + (unsigned)(v & 0x3FFu);
}

static inline void _rp_string_put_u4(rp_string *out, unsigned x)
{
    rp_string_appendf(out, "\\u%04X", x & 0xFFFFu);
}

static const char *BIG_DOT =
    "(?:[\\0-\\t\\x0B\\f\\x0E-\\u2027\\u202A-\\uD7FF\\uE000-\\uFFFF]|[\\uD800-\\uDBFF][\\uDC00-\\uDFFF]|[\\uD800-\\uDBFF](?![\\uDC00-\\uDFFF])|(?:[^\\uD800-\\uDBFF]|^)[\\uDC00-\\uDFFF])";

static void append_astral_range(rp_string *out, unsigned long a, unsigned long z)
{
    unsigned ha, la, hz, lz;
    cp_to_surrogates_ul(a, &ha, &la);
    cp_to_surrogates_ul(z, &hz, &lz);

    if (ha == hz)
    {
        rp_string_puts(out, "(?:");
        _rp_string_put_u4(out, ha);
        rp_string_putc(out, '[');
        _rp_string_put_u4(out, la);
        rp_string_putc(out, '-');
        _rp_string_put_u4(out, lz);
        rp_string_putc(out, ']');
        rp_string_putc(out, ')');
        return;
    }

    /* first block: ha [la-DFFF] */
    rp_string_puts(out, "(?:");
    _rp_string_put_u4(out, ha);
    rp_string_putc(out, '[');
    _rp_string_put_u4(out, la);
    rp_string_putc(out, '-');
    _rp_string_put_u4(out, 0xDFFFu);
    rp_string_putc(out, ']');
    rp_string_putc(out, ')');

    /* middle blocks: [ha+1 - hz-1][DC00-DFFF] */
    if (ha + 1 <= hz - 1)
    {
        rp_string_putc(out, '|');
        rp_string_puts(out, "(?:");
        rp_string_putc(out, '[');
        _rp_string_put_u4(out, ha + 1);
        rp_string_putc(out, '-');
        _rp_string_put_u4(out, hz - 1);
        rp_string_putc(out, ']');
        rp_string_putc(out, '[');
        _rp_string_put_u4(out, 0xDC00u);
        rp_string_putc(out, '-');
        _rp_string_put_u4(out, 0xDFFFu);
        rp_string_putc(out, ']');
        rp_string_putc(out, ')');
    }

    /* last block: hz [DC00-lz] */
    rp_string_putc(out, '|');
    rp_string_puts(out, "(?:");
    _rp_string_put_u4(out, hz);
    rp_string_putc(out, '[');
    _rp_string_put_u4(out, 0xDC00u);
    rp_string_putc(out, '-');
    _rp_string_put_u4(out, lz);
    rp_string_putc(out, ']');
    rp_string_putc(out, ')');
}

static char *rewrite_class_es5(const char *s, size_t len, size_t *i)
{
    size_t p = *i;
    if (p >= len || s[p] != '[')
        return NULL;
    p++;
    int neg = 0;
    if (p < len && s[p] == '^')
    {
        neg = 1;
        p++;
    }

    rp_string *bmp = rp_string_new(64);
    rp_string *astral= rp_string_new(64);

    int esc = 0;
    int first = 1;
    int have_dash = 0;

    int pending = 0;
    unsigned long pend_cp = 0;
    int pend_is_cp = 0;

#define EMIT_PENDING()                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (pending)                                                                                                   \
        {                                                                                                              \
            if (pend_is_cp)                                                                                            \
            {                                                                                                          \
                if (pend_cp <= 0xFFFFUL)                                                                               \
                {                                                                                                      \
                    _rp_string_put_u4(bmp, pend_cp);                                                                   \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    if (astral->len)                                                                                   \
                        rp_string_putc(astral, '|');                                                                   \
                    append_astral_range(astral, pend_cp, pend_cp);                                                     \
                }                                                                                                      \
            }                                                                                                          \
            pending = 0;                                                                                               \
            pend_is_cp = 0;                                                                                            \
            pend_cp = 0;                                                                                               \
        }                                                                                                              \
    } while (0)

    while (p < len)
    {
        char c = s[p];
        if (!esc)
        {
            if (c == '\\')
            {
                esc = 1;
                p++;
                continue;
            }
            if (c == ']' && !first)
            {
                if (have_dash)
                {
                    rp_string_putc(bmp, '-');
                    have_dash = 0;
                }
                p++;
                break;
            }
            if (c == '-' && !first && !have_dash)
            {
                have_dash = 1;
                p++;
                continue;
            }

            first = 0;
            if (have_dash && pending && pend_is_cp)
            {
                unsigned long a = pend_cp, b = (unsigned char)c;
                if (a > b)
                {
                    unsigned long t = a;
                    a = b;
                    b = t;
                }
                if (a <= 0xFFFFUL && b <= 0xFFFFUL)
                {
                    _rp_string_put_u4(bmp, a);
                    rp_string_putc(bmp, '-');
                    _rp_string_put_u4(bmp, b);
                }
                else
                {
                    if (a < 0x10000UL)
                        a = 0x10000UL;
                    if (astral->len)
                        rp_string_putc(astral, '|');
                    append_astral_range(astral, a, b);
                }
                pending = 0;
                pend_is_cp = 0;
                pend_cp = 0;
                have_dash = 0;
                p++;
                continue;
            }
            EMIT_PENDING();
            pending = 1;
            pend_is_cp = 1;
            pend_cp = (unsigned char)c;
            p++;
            continue;
        }
        else
        {
            first = 0;
            size_t savep = p;
            unsigned long cp = 0;
            int r = 0;
            if (p < len && s[p] == 'u')
            {
                size_t q = p;
                if (q + 1 < len && s[q + 1] == '{')
                {
                    q += 2;
                    unsigned long v = 0;
                    int hv;
                    while (q < len && s[q] != '}')
                    {
                        hv = hexv((unsigned char)s[q++]);
                        if (hv < 0)
                        {
                            v = 0;
                            q = 0;
                            break;
                        }
                        v = (v << 4) + hv;
                        if (v > 0x10FFFFUL)
                        {
                            v = 0;
                            q = 0;
                            break;
                        }
                    }
                    if (q && q < len && s[q] == '}')
                    {
                        r = 1;
                        cp = v;
                        p = q + 1;
                    }
                }
                else if (q + 4 < len)
                {
                    int v1 = hexv((unsigned char)s[q + 1]), v2 = hexv((unsigned char)s[q + 2]),
                        v3 = hexv((unsigned char)s[q + 3]), v4 = hexv((unsigned char)s[q + 4]);
                    if (v1 >= 0 && v2 >= 0 && v3 >= 0 && v4 >= 0)
                    {
                        r = 1;
                        cp = (unsigned)((v1 << 12) | (v2 << 8) | (v3 << 4) | v4);
                        p = q + 5;
                    }
                }
            }
            else if (p < len && s[p] == 'x')
            {
                if (p + 2 < len)
                {
                    int v1 = hexv((unsigned char)s[p + 1]), v2 = hexv((unsigned char)s[p + 2]);
                    if (v1 >= 0 && v2 >= 0)
                    {
                        r = 1;
                        cp = (unsigned)(v1 << 4 | v2);
                        p += 3;
                    }
                }
            }
            if (r == 1)
            {
                if (!have_dash)
                {
                    EMIT_PENDING();
                    pending = 1;
                    pend_is_cp = 1;
                    pend_cp = cp;
                }
                else
                {
                    if (!pending || !pend_is_cp)
                    {
                        have_dash = 0;
                        rp_string_putc(bmp, '-');
                        EMIT_PENDING();
                        pending = 1;
                        pend_is_cp = 1;
                        pend_cp = cp;
                    }
                    else
                    {
                        unsigned long a = pend_cp, b = cp;
                        if (a > b)
                        {
                            unsigned long t = a;
                            a = b;
                            b = t;
                        }
                        if (a <= 0xFFFFUL && b <= 0xFFFFUL)
                        {
                            _rp_string_put_u4(bmp, a);
                            rp_string_putc(bmp, '-');
                            _rp_string_put_u4(bmp, b);
                        }
                        else
                        {
                            if (a < 0x10000UL)
                                a = 0x10000UL;
                            if (astral->len)
                                rp_string_putc(astral, '|');
                            append_astral_range(astral, a, b);
                        }
                        pending = 0;
                        pend_is_cp = 0;
                        pend_cp = 0;
                        have_dash = 0;
                    }
                }
                esc = 0;
                continue;
            }
            else
            {
                EMIT_PENDING();
                rp_string_putc(bmp, '\\');
                rp_string_putc(bmp, s[savep]);
                p = savep + 1;
                esc = 0;
                continue;
            }
        }
    }

    EMIT_PENDING();
    if (have_dash)
    {
        rp_string_putc(bmp, '-');
        have_dash = 0;
    }

    rp_string *out = rp_string_new(64);;
    if (!neg)
    {
        if (astral->len == 0)
        {
            rp_string_putc(out, '[');
            rp_string_putsn(out, bmp->str, bmp->len);
            rp_string_putc(out, ']');
        }
        else
        {
            rp_string_puts(out, "(?:");
            if (bmp->len)
            {
                rp_string_putc(out, '[');
                rp_string_putsn(out, bmp->str, bmp->len);
                rp_string_putc(out, ']');
                rp_string_putc(out, '|');
            }
            rp_string_putsn(out, astral->str, astral->len);
            rp_string_putc(out, ')');
        }
    }
    else
    {
        rp_string_puts(out, "(?:");
        rp_string_puts(out, "[^\\uD800-\\uDFFF");
        if (bmp->len)
            rp_string_putsn(out, bmp->str, bmp->len);
        rp_string_putc(out, ']');
        rp_string_putc(out, '|');
        if (astral->len)
        {
            rp_string_puts(out, "(?!");
            rp_string_putsn(out, astral->str, astral->len);
            rp_string_putc(out, ')');
        }
        rp_string_puts(out, "(?:[\\uD800-\\uDBFF][\\uDC00-\\uDFFF])");
        rp_string_putc(out, ')');
    }
    bmp = rp_string_free(bmp);
    astral = rp_string_free(astral);
    *i = p;

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

// ===== ES5 Unicode regex transformer  =====
static char *regex_u_to_es5_pattern(const char *in, size_t len)
{
    rp_string *out = rp_string_new(256);
    int esc = 0;
    for (size_t i = 0; i < len;)
    {
        char c = in[i];
        if (!esc)
        {
            if (c == '\\')
            {
                esc = 1;
                rp_string_putc(out, '\\');
                i++;
                continue;
            }
            if (c == '[')
            {
                char *cls = rewrite_class_es5(in, len, &i);
                if (!cls)
                {
                    out = rp_string_free(out);
                    return NULL;
                }
                rp_string_puts(out, cls);
                free(cls);
                continue;
            }
            if (c == '.')
            {
                rp_string_puts(out, BIG_DOT);
                i++;
                continue;
            }
            rp_string_putc(out, c);
            i++;
            continue;
        }
        else
        {
            size_t j = i + 1;
            if (in[i] == 'u' && j < len && in[j] == '{')
            {
                out->len--;
                out->str[out->len] = '\0';
                unsigned long cp = 0;
                int hv;
                j++;
                while (j < len && in[j] != '}')
                {
                    hv = hexv((unsigned char)in[j++]);
                    if (hv < 0)
                    {
                        out = rp_string_free(out);;
                        return NULL;
                    }
                    cp = (cp << 4) + hv;
                    if (cp > 0x10FFFFUL)
                    {
                        out = rp_string_free(out);
                        return NULL;
                    }
                }
                if (j >= len || in[j] != '}')
                {
                    out = rp_string_free(out);
                    return NULL;
                }
                j++;
                if (cp <= 0xFFFFUL)
                {
                    _rp_string_put_u4(out, (unsigned)cp);
                }
                else
                {
                    unsigned hi, lo;
                    cp_to_surrogates_ul(cp, &hi, &lo);
                    _rp_string_put_u4(out, hi);
                    _rp_string_put_u4(out, lo);
                }
                i = j;
                esc = 0;
                continue;
            }
            else
            {
                rp_string_putc(out, in[i]);
                i++;
                esc = 0;
                continue;
            }
        }
    }
    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

/* Copy a body or body fragment, replacing super.X patterns for ES5 compatibility.
 * Instance methods: super.X(args) → _Super.prototype.X.call(this, args)
 *                   super.X       → _Super.prototype.X
 * Static methods:   super.X(args) → _Super.X.call(this, args)
 *                   super.X       → _Super.X
 */
static void copy_body_replace_super(rp_string *bucket, const char *body, size_t len, int is_static)
{
    size_t i = 0;
    size_t last_copy = 0; /* start of uncopied text */

    while (i + 6 <= len)
    {
        if (strncmp(body + i, "super.", 6) == 0)
        {
            /* Make sure "super" is a standalone keyword, not part of e.g. "_super." */
            if (i > 0 && (isalnum((unsigned char)body[i - 1]) || body[i - 1] == '_' || body[i - 1] == '$'))
            {
                i++;
                continue;
            }

            /* read identifier after "super." */
            size_t id_start = i + 6;
            size_t j = id_start;
            while (j < len && (isalnum((unsigned char)body[j]) || body[j] == '_' || body[j] == '$'))
                j++;
            size_t id_len = j - id_start;

            if (id_len == 0)
            {
                /* no valid identifier after super., skip */
                i++;
                continue;
            }

            /* Copy everything before this "super." */
            if (i > last_copy)
                rp_string_putsn(bucket, body + last_copy, i - last_copy);

            /* skip whitespace after identifier to check for '(' */
            size_t wi = j;
            while (wi < len && (body[wi] == ' ' || body[wi] == '\t' || body[wi] == '\n' || body[wi] == '\r'))
                wi++;

            if (wi < len && body[wi] == '(')
            {
                /* method call: super.name(...) → _Super[.prototype].name.call(this[, ...]) */
                if (is_static)
                    rp_string_puts(bucket, "_Super.");
                else
                    rp_string_puts(bucket, "_Super.prototype.");
                rp_string_putsn(bucket, body + id_start, id_len);
                rp_string_puts(bucket, ".call(this");

                /* skip past the '(' */
                i = wi + 1;

                /* check if empty args */
                size_t ai = i;
                while (ai < len && (body[ai] == ' ' || body[ai] == '\t' || body[ai] == '\n' || body[ai] == '\r'))
                    ai++;

                if (ai < len && body[ai] == ')')
                {
                    /* no args: super.name() → ...call(this) */
                    rp_string_puts(bucket, ")");
                    i = ai + 1;
                }
                else
                {
                    /* has args: emit ", " then let the rest of args + ')' copy naturally */
                    rp_string_puts(bucket, ", ");
                }
                last_copy = i;
            }
            else
            {
                /* property access: super.name → _Super[.prototype].name */
                if (is_static)
                    rp_string_puts(bucket, "_Super.");
                else
                    rp_string_puts(bucket, "_Super.prototype.");
                rp_string_putsn(bucket, body + id_start, id_len);
                i = j;
                last_copy = i;
            }
        }
        else
        {
            i++;
        }
    }

    /* Copy remaining text */
    if (last_copy < len)
        rp_string_putsn(bucket, body + last_copy, len - last_copy);
}

/* === ES6 class -> ES5 function/prototype rewrite (minimal) ===
   Handles:
     class C { constructor(...) { ... } m(...) { ... } static s(...) { ... } }
     class C extends B { ... }  with super(...) in constructor
     super.method() and super.prop in methods (rewritten to _Super.prototype.method.call(this))
   Limitations:
     - No private fields, no decorators, no async/generator methods.
*/

/* mode: 0=declaration (function C... + stmts), 1=expression (IIFE that returns C)
   cname is provided (never NULL). sups/supe valid iff has_super.
   Handles: instance/static methods; simple getters/setters; computed names using bracket; rewrites super.method(...) in
   method bodies.
*/
/* Emits:
   // no extends
   var Name = function() {
     function Name(params) { _TrN_Sp.classCallCheck(this, Name); * body * }
     _TrN_Sp.createClass(Name, [ {key:'m', value:function m(){...}}, ... ]);
     return Name;
   }();

   // with extends Super
   var Name = function(_Super) {
     _TrN_Sp.inherits(Name, _Super);
     var _super = _TrN_Sp.createSuper(Name);
     function Name(params) {
       var _this;
       _TrN_Sp.classCallCheck(this, Name);
       _this = _super.call(this, *super args as written*);
       * remainder of body, with this. field inits left as-is *
       return _this;
     }
     _TrN_Sp.createClass(Name, [ ...proto... ], [ ...static... ]);
     return Name;
   }(Super);
*/
static void es5_emit_class_core(rp_string *out, const char *src, const char *cname, size_t cname_len, int has_super,
                                size_t sups, size_t supe, TSNode body)
{
    /* ——— gather constructor and methods ——— */
    int ctor_found = 0;
    TSNode ctor_params = {{0}};
    TSNode ctor_body = {{0}};
    uint32_t n = ts_node_child_count(body);

    // Buckets for methods
    rp_string *proto_arr = rp_string_new(128);
    rp_string *static_arr = rp_string_new(64);

    // Collect class field definitions (ES2022)
    rp_string *field_inits = rp_string_new(64);
    rp_string *static_field_inits = rp_string_new(64);
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode ch = ts_node_child(body, i);
        if (!ts_node_is_named(ch))
            continue;
        if (strcmp(ts_node_type(ch), "field_definition") != 0)
            continue;
        TSNode fprop = ts_node_child_by_field_name(ch, "property", 8);
        if (ts_node_is_null(fprop))
            continue;
        /* detect "static" modifier */
        int is_static_field = 0;
        for (uint32_t j = 0, cn = ts_node_child_count(ch); j < cn; j++)
        {
            TSNode fch = ts_node_child(ch, j);
            if (ts_node_is_named(fch))
                continue;
            size_t ss = ts_node_start_byte(fch), se = ts_node_end_byte(fch);
            if (se - ss == 6 && strncmp(src + ss, "static", 6) == 0)
            { is_static_field = 1; break; }
        }
        size_t fps = ts_node_start_byte(fprop), fpe = ts_node_end_byte(fprop);
        TSNode fval = ts_node_child_by_field_name(ch, "value", 5);
        rp_string *dest = is_static_field ? static_field_inits : field_inits;
        if (is_static_field)
        {
            rp_string_putsn(dest, cname, cname_len);
            rp_string_puts(dest, ".");
        }
        else
        {
            rp_string_puts(dest, "this.");
        }
        rp_string_putsn(dest, src + fps, fpe - fps);
        if (!ts_node_is_null(fval))
        {
            size_t fvs = ts_node_start_byte(fval), fve = ts_node_end_byte(fval);
            rp_string_puts(dest, " = ");
            rp_string_putsn(dest, src + fvs, fve - fvs);
        }
        else
        {
            rp_string_puts(dest, " = undefined");
        }
        rp_string_puts(dest, ";");
    }

    for (uint32_t i = 0; i < n; i++)
    {
        TSNode mth = ts_node_child(body, i);
        if (!ts_node_is_named(mth))
            continue;
        if (strcmp(ts_node_type(mth), "method_definition") != 0)
            continue;

        // constructor?
        TSNode nname = ts_node_child_by_field_name(mth, "name", 4);
        const char *nt = ts_node_is_null(nname) ? "" : ts_node_type(nname);
        int is_ctor = (!ts_node_is_null(nname) && strcmp(nt, "property_identifier") == 0) &&
                      (strncmp(src + ts_node_start_byte(nname), "constructor", 11) == 0);

        TSNode params = ts_node_child_by_field_name(mth, "parameters", 10);
        TSNode mb = ts_node_child_by_field_name(mth, "body", 4);
        int is_static = 0;
        int is_getter = 0;
        int is_setter = 0;
        // detect "static", "get", "set" modifiers
        for (uint32_t j = 0, cn = ts_node_child_count(mth); j < cn; j++)
        {
            TSNode ch = ts_node_child(mth, j);
            if (ts_node_is_named(ch))
                continue;
            size_t ss = ts_node_start_byte(ch), se = ts_node_end_byte(ch);
            size_t slen = se - ss;
            if (slen == 6 && strncmp(src + ss, "static", 6) == 0)
                is_static = 1;
            else if (slen == 3 && strncmp(src + ss, "get", 3) == 0)
                is_getter = 1;
            else if (slen == 3 && strncmp(src + ss, "set", 3) == 0)
                is_setter = 1;
        }

        if (is_ctor)
        {
            ctor_found = 1;
            ctor_params = params;
            ctor_body = mb;
            continue;
        }

        int is_computed = 0;
        size_t ks, ke;

        if (!ts_node_is_null(nname) && strcmp(ts_node_type(nname), "property_identifier") == 0)
        {
            ks = ts_node_start_byte(nname);
            ke = ts_node_end_byte(nname);
        }
        else if (!ts_node_is_null(nname) && strcmp(ts_node_type(nname), "computed_property_name") == 0)
        {
            is_computed = 1;
            // The inner expression is between '[' and ']'
            ks = ts_node_start_byte(nname) + 1;  // skip '['
            ke = ts_node_end_byte(nname) - 1;    // skip ']'
        }
        else
        {
            continue;
        }

        size_t ps = ts_node_is_null(params) ? 0 : ts_node_start_byte(params);
        size_t pe = ts_node_is_null(params) ? 0 : ts_node_end_byte(params);
        size_t bs = ts_node_is_null(mb) ? 0 : ts_node_start_byte(mb);
        size_t be = ts_node_is_null(mb) ? 0 : ts_node_end_byte(mb);

        rp_string *bucket = is_static ? static_arr : proto_arr;
        if (bucket->len)
            rp_string_puts(bucket, ",");

        const char *desc_field = "value";
        if (is_getter) desc_field = "get";
        else if (is_setter) desc_field = "set";

        if (is_computed)
        {
            // {key:<expr>,value:function(){...}}  or get/set variant
            rp_string_puts(bucket, "{key:");
            rp_string_putsn(bucket, src + ks, ke - ks);
            rp_string_appendf(bucket, ",%s:function ", desc_field);
        }
        else
        {
            // {key:'name',value:function name(){...}}  or get/set variant
            rp_string_puts(bucket, "{key:'");
            rp_string_putsn(bucket, src + ks, ke - ks);
            rp_string_appendf(bucket, "',%s:function ", desc_field);
            if (!is_getter && !is_setter)
                rp_string_putsn(bucket, src + ks, ke - ks);
        }
        /* Check for rest parameter in method params */
        const char *rest_name = NULL;
        size_t rest_name_len = 0;
        uint32_t rest_before_count = 0;
        size_t rest_remove_s = 0, rest_remove_e = 0;
        if (ps && pe && !ts_node_is_null(params))
        {
            uint32_t np = ts_node_named_child_count(params);
            for (uint32_t pi = 0; pi < np; pi++)
            {
                TSNode pch = ts_node_named_child(params, pi);
                if (strcmp(ts_node_type(pch), "rest_pattern") == 0)
                {
                    rest_before_count = pi;
                    rest_remove_s = ts_node_start_byte(pch);
                    rest_remove_e = ts_node_end_byte(pch);
                    /* walk back over whitespace and comma */
                    size_t wi = rest_remove_s;
                    while (wi > ps && is_ws(src[wi - 1]))
                        wi--;
                    if (wi > ps && src[wi - 1] == ',')
                        rest_remove_s = wi - 1;
                    /* get identifier inside rest_pattern */
                    uint32_t rnc = ts_node_named_child_count(pch);
                    for (uint32_t ri = 0; ri < rnc; ri++)
                    {
                        TSNode rid = ts_node_named_child(pch, ri);
                        if (strcmp(ts_node_type(rid), "identifier") == 0)
                        {
                            rest_name = src + ts_node_start_byte(rid);
                            rest_name_len = ts_node_end_byte(rid) - ts_node_start_byte(rid);
                            break;
                        }
                    }
                    break;
                }
            }
        }

        /* Emit params (minus rest if present) */
        if (ps && pe)
        {
            if (rest_name)
            {
                /* Copy params but skip the rest portion */
                rp_string_putsn(bucket, src + ps, rest_remove_s - ps);
                rp_string_putsn(bucket, src + rest_remove_e, pe - rest_remove_e);
            }
            else
            {
                rp_string_putsn(bucket, src + ps, pe - ps);
            }
        }
        else
            rp_string_puts(bucket, "()");

        /* Emit body (with rest shim and super rewrite as needed) */
        rp_string_puts(bucket, " ");
        if (bs && be)
        {
            if (rest_name || has_super)
            {
                /* Need to modify body: open brace */
                rp_string_putc(bucket, '{');
                /* inject rest param shim */
                if (rest_name)
                {
                    rp_string_puts(bucket, "var ");
                    rp_string_putsn(bucket, rest_name, rest_name_len);
                    rp_string_appendf(bucket, " = Object.values(arguments).slice(%u); ", rest_before_count);
                }
                /* copy body contents (skip outer braces), with super rewrite if needed */
                if (has_super)
                    copy_body_replace_super(bucket, src + bs + 1, be - bs - 2, is_static);
                else
                    rp_string_putsn(bucket, src + bs + 1, be - bs - 2);
                rp_string_putc(bucket, '}');
            }
            else
            {
                rp_string_putsn(bucket, src + bs, be - bs);
            }
        }
        else
            rp_string_puts(bucket, "{}");
        rp_string_puts(bucket, "}");
    }

    /* ——— open wrapper ——— */
    if (!has_super)
    {
        rp_string_puts(out, "var ");
        rp_string_putsn(out, cname, cname_len);
        rp_string_puts(out, " = (function() {");
    }
    else
    {
        rp_string_puts(out, "var ");
        rp_string_putsn(out, cname, cname_len);
        rp_string_puts(out, " = (function(_Super) {_TrN_Sp.inherits(");
        rp_string_putsn(out, cname, cname_len);
        rp_string_puts(out, ", _Super);var _super = _TrN_Sp.createSuper(");
        rp_string_putsn(out, cname, cname_len);
        rp_string_puts(out, ");");
    }

    /* ——— constructor ——— */
    rp_string_puts(out, "  function ");
    rp_string_putsn(out, cname, cname_len);

    /* Detect rest param in constructor */
    const char *ctor_rest_name = NULL;
    size_t ctor_rest_name_len = 0;
    uint32_t ctor_rest_before = 0;

    if (ctor_found && !ts_node_is_null(ctor_params))
    {
        size_t ps = ts_node_start_byte(ctor_params), pe = ts_node_end_byte(ctor_params);
        /* scan for rest_pattern */
        uint32_t np = ts_node_named_child_count(ctor_params);
        size_t crest_remove_s = 0, crest_remove_e = 0;
        for (uint32_t pi = 0; pi < np; pi++)
        {
            TSNode pch = ts_node_named_child(ctor_params, pi);
            if (strcmp(ts_node_type(pch), "rest_pattern") == 0)
            {
                ctor_rest_before = pi;
                crest_remove_s = ts_node_start_byte(pch);
                crest_remove_e = ts_node_end_byte(pch);
                size_t wi = crest_remove_s;
                while (wi > ps && is_ws(src[wi - 1]))
                    wi--;
                if (wi > ps && src[wi - 1] == ',')
                    crest_remove_s = wi - 1;
                uint32_t rnc = ts_node_named_child_count(pch);
                for (uint32_t ri = 0; ri < rnc; ri++)
                {
                    TSNode rid = ts_node_named_child(pch, ri);
                    if (strcmp(ts_node_type(rid), "identifier") == 0)
                    {
                        ctor_rest_name = src + ts_node_start_byte(rid);
                        ctor_rest_name_len = ts_node_end_byte(rid) - ts_node_start_byte(rid);
                        break;
                    }
                }
                break;
            }
        }
        if (ctor_rest_name)
        {
            rp_string_putsn(out, src + ps, crest_remove_s - ps);
            rp_string_putsn(out, src + crest_remove_e, pe - crest_remove_e);
        }
        else
        {
            rp_string_putsn(out, src + ps, pe - ps);
        }
    }
    else
    {
        rp_string_puts(out, "()"); // synthesize if missing
    }
    rp_string_puts(out, " {");
    /* inject rest param shim for constructor */
    if (ctor_rest_name)
    {
        rp_string_puts(out, "var ");
        rp_string_putsn(out, ctor_rest_name, ctor_rest_name_len);
        rp_string_appendf(out, " = Object.values(arguments).slice(%u); ", ctor_rest_before);
    }

    if (has_super)
    {
        // In the simple/most common case, `super(args)` is the first statement.
        // Textually grab constructor body and rewrite a single leading "super(" call.
        if (ctor_found && !ts_node_is_null(ctor_body))
        {
            size_t bs = ts_node_start_byte(ctor_body), be = ts_node_end_byte(ctor_body);
            /* ts 'body' includes the surrounding braces; slice to just the contents */
            const char *b = src + bs + 1;
            size_t blen = (be > bs + 1) ? (be - bs - 2) : 0; /* drop leading '{' and trailing '}' */
            // naive rewrite: look for "super(" at top-level of body text once
            const char *open = strstr(b, "super(");
            if (open)
            {
                /* Find '(' right after 'super' and its matching ')' */
                const char *lp = strchr(open, '(');
                if (!lp)
                    goto NO_SUPER_REWRITE;

                size_t args_s = (size_t)(lp + 1 - b); /* first char inside '(' */
                int depth = 1;
                size_t i = args_s;
                size_t call_rp = blen; /* fallback */
                for (; i < blen; i++)
                {
                    char c = b[i];
                    if (c == '(')
                        depth++;
                    else if (c == ')')
                    {
                        depth--;
                        if (depth == 0)
                        {
                            call_rp = i;
                            break;
                        }
                    }
                }
                if (depth != 0)
                    goto NO_SUPER_REWRITE;

                /* Emit prelude */
                rp_string_puts(out, "var _this;_TrN_Sp.classCallCheck(this, ");
                rp_string_putsn(out, cname, cname_len);
                rp_string_puts(out, ");");

                /* _this = _super.call(this, <args>);
                   If args is a single spread like ...x, use apply instead */
                {
                    const char *atext = b + args_s;
                    size_t alen = call_rp - args_s;
                    /* trim leading whitespace */
                    while (alen > 0 && (*atext == ' ' || *atext == '\t' || *atext == '\n' || *atext == '\r'))
                    { atext++; alen--; }
                    /* trim trailing whitespace */
                    while (alen > 0 && (atext[alen-1] == ' ' || atext[alen-1] == '\t' || atext[alen-1] == '\n' || atext[alen-1] == '\r'))
                        alen--;
                    if (alen > 3 && atext[0] == '.' && atext[1] == '.' && atext[2] == '.'
                        && memchr(atext + 3, ',', alen - 3) == NULL)
                    {
                        /* single spread: super(...expr) -> _super.apply(this, expr) */
                        rp_string_puts(out, "_this = _super.apply(this, ");
                        rp_string_putsn(out, atext + 3, alen - 3);
                        rp_string_puts(out, ");");
                    }
                    else
                    {
                        if (alen > 0)
                        {
                            rp_string_puts(out, "_this = _super.call(this, ");
                            rp_string_putsn(out, atext, alen);
                        }
                        else
                        {
                            rp_string_puts(out, "_this = _super.call(this");
                        }
                        rp_string_puts(out, ");");
                    }
                }

                /* Copy remainder of ctor body after the super(...) statement's semicolon,
                   rewriting any super.method() calls */
                size_t after = call_rp + 1; /* position after ')' */
                if (after < blen && b[after] == ';')
                    after++; /* swallow trailing semicolon if any */
                if (after < blen)
                    copy_body_replace_super(out, b + after, blen - after, 0);

                /* Ensure the constructor returns _this */
                rp_string_puts(out, "return _this;");
            }
            else
            {
            NO_SUPER_REWRITE:
                rp_string_puts(out, "_TrN_Sp.classCallCheck(this, ");
                rp_string_putsn(out, cname, cname_len);
                rp_string_puts(out, ");");
                if (blen)
                    copy_body_replace_super(out, b, blen, 0);
            }
        }
        else
        {
            rp_string_puts(out, "var _this;_TrN_Sp.classCallCheck(this, ");
            rp_string_putsn(out, cname, cname_len);
            rp_string_puts(out, ");_this = _super.apply(this, arguments);return _this;");
        }
    }
    else
    {
        // no extends
        rp_string_puts(out, "_TrN_Sp.classCallCheck(this, ");
        rp_string_putsn(out, cname, cname_len);
        rp_string_puts(out, ");");
        /* inject class field initializations */
        if (field_inits->len)
            rp_string_puts(out, field_inits->str);
        if (ctor_found && !ts_node_is_null(ctor_body))
        {
            size_t bs = ts_node_start_byte(ctor_body), be = ts_node_end_byte(ctor_body);
            rp_string_putsn(out, src + bs, be - bs);
        }
    }
    rp_string_puts(out, "};");

    /* ——— _TrN_Sp.createClass(Name, [proto], [static]) ——— */
    rp_string_puts(out, "_TrN_Sp.createClass(");
    rp_string_putsn(out, cname, cname_len);
    rp_string_puts(out, ",[");
    rp_string_puts(out, proto_arr->str);
    rp_string_puts(out, "],[");
    rp_string_puts(out, static_arr->str);
    rp_string_puts(out, "]);");

    /* ——— return + close wrapper ——— */
    rp_string_puts(out, "return ");
    rp_string_putsn(out, cname, cname_len);
    rp_string_puts(out, ";");

    if (!has_super)
    {
        rp_string_puts(out, "})();");
    }
    else
    {
        rp_string_puts(out, "})(");
        rp_string_putsn(out, src + sups, supe - sups);
        rp_string_puts(out, ");");
    }

    /* Emit static field initializations after the IIFE */
    if (static_field_inits->len)
        rp_string_puts(out, static_field_inits->str);

    proto_arr = rp_string_free(proto_arr);
    static_arr = rp_string_free(static_arr);
    field_inits = rp_string_free(field_inits);
    static_field_inits = rp_string_free(static_field_inits);
}

static int rewrite_class_to_es5(EditList *edits, const char *src, TSNode class_node, RangeList *claimed, int overlaps)
{
    const char *ctype = ts_node_type(class_node);
    int has_super = 0;

    if (strcmp(ctype, "class_declaration") != 0)
        return 0;
    size_t cs = ts_node_start_byte(class_node), ce = ts_node_end_byte(class_node);

    TSNode id = ts_node_child_by_field_name(class_node, "name", 4);
    if (ts_node_is_null(id))
        return 0;

    size_t ids = ts_node_start_byte(id), ide = ts_node_end_byte(id);
    const char *nameptr = src + ids;
    size_t namelen = ide - ids;

    TSNode body = ts_node_child_by_field_name(class_node, "body", 4);
    if (ts_node_is_null(body))
        return 0;

    if (overlaps)
        return 1;

    size_t sups = 0, supe = 0;
    TSNode heritage = (TSNode){0};
    /* find the `class_heritage` child node on this class */
    for (uint32_t i = 0, n = ts_node_child_count(class_node); i < n; i++)
    {
        TSNode ch = ts_node_child(class_node, i);
        if (!ts_node_is_named(ch))
            continue;
        if (strcmp(ts_node_type(ch), "class_heritage") == 0)
        {
            heritage = ch;
            break;
        }
    }

    if (!ts_node_is_null(heritage))
    {
        /* inside class_heritage, the first NAMED child after the `extends` token
           is the superclass expression (identifier, member_expression, etc.) */
        TSNode sup_expr = (TSNode){0};
        for (uint32_t j = 0, m = ts_node_child_count(heritage); j < m; j++)
        {
            TSNode hch = ts_node_child(heritage, j);
            if (!ts_node_is_named(hch))
                continue; /* skip the literal 'extends' token */
            sup_expr = hch;
            break;
        }
        if (!ts_node_is_null(sup_expr))
        {
            has_super = 1;
            sups = ts_node_start_byte(sup_expr);
            supe = ts_node_end_byte(sup_expr);
        }
    }

    rp_string *out = rp_string_new(256);
    es5_emit_class_core(out, src, nameptr, namelen, has_super, sups, supe, body);
    add_edit_take_ownership(edits, cs, ce, rp_string_steal(out), claimed);
    out=rp_string_free(out);
    return 1;
}

static int rewrite_class_expression_to_es5(EditList *edits, const char *src, TSNode class_node, RangeList *claimed,
                                           uint32_t *polysneeded, int overlaps)
{
    if (strcmp(ts_node_type(class_node), "class") != 0)
        return 0;

    size_t cs = ts_node_start_byte(class_node), ce = ts_node_end_byte(class_node);

    TSNode id = ts_node_child_by_field_name(class_node, "name", 4);
    char tmpname[64];
    const char *nameptr = NULL;
    size_t namelen = 0;
    if (ts_node_is_null(id))
    {
        snprintf(tmpname, sizeof(tmpname), "__TrC%u", (unsigned)cs);
        nameptr = tmpname;
        namelen = strlen(tmpname);
    }
    else
    {
        size_t ids = ts_node_start_byte(id), ide = ts_node_end_byte(id);
        nameptr = src + ids;
        namelen = ide - ids;
    }

    int has_super = 0;
    size_t sups = 0, supe = 0;

    TSNode heritage = (TSNode){0};
    /* find the `class_heritage` child node on this class */
    for (uint32_t i = 0, n = ts_node_child_count(class_node); i < n; i++)
    {
        TSNode ch = ts_node_child(class_node, i);
        if (!ts_node_is_named(ch))
            continue;
        if (strcmp(ts_node_type(ch), "class_heritage") == 0)
        {
            heritage = ch;
            break;
        }
    }

    if (!ts_node_is_null(heritage))
    {
        /* inside class_heritage, the first NAMED child after the `extends` token
           is the superclass expression (identifier, member_expression, etc.) */
        TSNode sup_expr = (TSNode){0};
        for (uint32_t j = 0, m = ts_node_child_count(heritage); j < m; j++)
        {
            TSNode hch = ts_node_child(heritage, j);
            if (!ts_node_is_named(hch))
                continue; /* skip the literal 'extends' token */
            sup_expr = hch;
            break;
        }
        if (!ts_node_is_null(sup_expr))
        {
            has_super = 1;
            sups = ts_node_start_byte(sup_expr);
            supe = ts_node_end_byte(sup_expr);
        }
    }
    /*
    TSNode supercls = ts_node_child_by_field_name(class_node, "superclass", 10);
    if (!ts_node_is_null(supercls)) {
        has_super = 1;
        sups = ts_node_start_byte(supercls);
        supe = ts_node_end_byte(supercls);
    }
    */
    TSNode body = ts_node_child_by_field_name(class_node, "body", 4);
    if (ts_node_is_null(body))
        return 0;

    if (overlaps)
        return 1;

    rp_string *expr = rp_string_new(256);
    rp_string_puts(expr, "(function(){");
    // emit the same var Name = function(){...}(); but as an expression we only need the IIFE value.
    // So we generate the same code and then reference the Name immediately.
    es5_emit_class_core(expr, src, nameptr, namelen, has_super, sups, supe, body);

    // Replace with just the identifier, because the class expression should yield the constructor.
    // The var/IIFE we just emitted must be inserted *before* and we return the name here.
    // Simpler approach: replace the class expression node with (function(){...return Name;}())
    // However, to keep parity with your target, we reuse the emitted var but wrap as expression:
    // For safety in expressions, emit directly as an IIFE returning the constructor:
    //   (function(){ ...; return Name; }())
    // If you prefer the "var" form only for declarations, keep the simple IIFE here:
    // We'll do that:

    rp_string_puts(expr, "return ");
    rp_string_putsn(expr, nameptr, namelen);
    rp_string_puts(expr, ";}())");

    add_edit_take_ownership(edits, cs, ce, rp_string_steal(expr), claimed);
    expr = rp_string_free(expr); // expr now owns the final buffer

    return 1;
}

static int rewrite_regex_u_to_es5(EditList *edits, const char *src, TSNode regex_node, RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(regex_node), "regex") != 0)
        return 0;
    size_t rs = ts_node_start_byte(regex_node), re = ts_node_end_byte(regex_node);
    TSNode pattern = find_child_type(regex_node, "regex_pattern", NULL);
    TSNode flags = find_child_type(regex_node, "regex_flags", NULL);
    if (ts_node_is_null(pattern) || ts_node_is_null(flags))
        return 0;
    size_t ps = ts_node_start_byte(pattern), pe = ts_node_end_byte(pattern);
    size_t fs = ts_node_start_byte(flags), fe = ts_node_end_byte(flags);
    int has_u = 0;
    for (size_t i = fs; i < fe; i++)
        if (src[i] == 'u')
        {
            has_u = 1;
            break;
        }
    if (!has_u)
        return 0;
    char *newpat = regex_u_to_es5_pattern(src + ps, pe - ps);
    if (!newpat)
        return 0;

    if (overlaps)
        return 1;

    size_t nflen = 0;
    char *newflags = NULL;
    if (fe > fs)
    {
        newflags = (char *)malloc((fe - fs) + 1);
        for (size_t i = fs; i < fe; i++)
            if (src[i] != 'u')
                newflags[nflen++] = src[i];
        newflags[nflen] = '\0';
    }
    size_t outlen = 1 + strlen(newpat) + 1 + nflen;
    char *rep = (char *)malloc(outlen + 1);
    size_t k = 0;
    rep[k++] = '/';
    memcpy(rep + k, newpat, strlen(newpat));
    k += strlen(newpat);
    rep[k++] = '/';
    if (nflen)
    {
        memcpy(rep + k, newflags, nflen);
        k += nflen;
    }
    rep[k] = '\0';
    free(newpat);
    if (newflags)
        free(newflags);
    add_edit_take_ownership(edits, rs, re, rep, claimed);

    return 1;
}
// helper: generate fresh temporary names following _i, _x, _i2, _x2, ...
static void make_fresh_forof_names(char *ibuf, size_t ibufsz, char *xbuf, size_t xbufsz)
{
    static unsigned counter = 0;
    ++counter;
    // first pair is "_i" / "_x", then suffix numbers for subsequent pairs
    if (counter == 1)
    {
        snprintf(ibuf, ibufsz, "_i");
        snprintf(xbuf, xbufsz, "_x");
    }
    else
    {
        snprintf(ibuf, ibufsz, "_i%u", counter);
        snprintf(xbuf, xbufsz, "_x%u", counter);
    }
}

// Rewrite plain (non-destructuring) for-of loops:
//   for (var a of X) { body }   =>  for (var _i=0,_x=X; _i<_x.length; _i++) { var a=_x[_i]; body }
//   for (a of X) { body }       =>  for (var _i=0,_x=X; _i<_x.length; _i++) { a=_x[_i]; body }
static int rewrite_for_of_simple(EditList *edits, const char *src, TSNode forof, RangeList *claimed,
                                 uint32_t *polysneeded, int overlaps)
{
    (void)polysneeded;
    (void)overlaps; // currently unused

    if (ts_node_is_null(forof))
        return 0;
    if (strcmp(ts_node_type(forof), "for_in_statement") != 0)
        return 0;

    // Ensure this is actually a "for … of …" (tree-sitter encodes both in the same node type)
    TSNode op = ts_node_child_by_field_name(forof, "operator", 8);
    if (ts_node_is_null(op))
        return 0;
    size_t ops = ts_node_start_byte(op), ope = ts_node_end_byte(op);
    if (ope <= ops || strncmp(src + ops, "of", (size_t)(ope - ops)) != 0)
        return 0;

    TSNode left = ts_node_child_by_field_name(forof, "left", 4);
    TSNode right = ts_node_child_by_field_name(forof, "right", 5);
    TSNode body = ts_node_child_by_field_name(forof, "body", 4);
    if (ts_node_is_null(left) || ts_node_is_null(right) || ts_node_is_null(body))
        return 0;

    const char *lt = ts_node_type(left);

    // reject cases already handled by destructuring path
    if (strcmp(lt, "lexical_declaration") == 0)
    {
        // let/const with possible pattern — let the destructuring rewriter take it
        return 0;
    }

    // Identify the loop target identifier and whether it is a declaration or a bare identifier
    TSNode name = {{0}};
    bool is_decl = false;
    bool is_let = false; /* true if original was let/const — needs IIFE wrapping for fresh bindings */

    if (strcmp(lt, "variable_declaration") == 0)
    {
        // Expect exactly one declarator: var a
        uint32_t n = ts_node_named_child_count(left);
        if (n != 1)
            return 0;
        TSNode decl = ts_node_named_child(left, 0);
        if (strcmp(ts_node_type(decl), "variable_declarator") != 0)
            return 0;
        name = ts_node_child_by_field_name(decl, "name", 4);
        if (ts_node_is_null(name) || strcmp(ts_node_type(name), "identifier") != 0)
            return 0;
        is_decl = true;
    }
    else if (strcmp(lt, "identifier") == 0)
    {
        // for (a of X) or for (let a of X) — check kind field
        TSNode kind = ts_node_child_by_field_name(forof, "kind", 4);
        if (!ts_node_is_null(kind))
        {
            size_t ks = ts_node_start_byte(kind), ke = ts_node_end_byte(kind);
            if ((ke - ks == 3 && strncmp(src + ks, "let", 3) == 0) ||
                (ke - ks == 5 && strncmp(src + ks, "const", 5) == 0))
            {
                is_decl = true;
                is_let = true;
            }
        }
        name = left;
    }
    else
    {
        // not a simple case
        return 0;
    }

    // Extract right-hand expression text
    size_t rs = ts_node_start_byte(right);
    size_t re = ts_node_end_byte(right);
    // Body range and block-ness
    size_t bs = ts_node_start_byte(body);
    size_t be = ts_node_end_byte(body);
    bool is_block = (strcmp(ts_node_type(body), "statement_block") == 0);

    // Identifier text (target)
    size_t ns = ts_node_start_byte(name);
    size_t ne = ts_node_end_byte(name);

    // Fresh temps
#define TPSMALLBUFSZ 32
    char ibuf[TPSMALLBUFSZ], xbuf[TPSMALLBUFSZ], itbuf[TPSMALLBUFSZ+1], rbuf[TPSMALLBUFSZ];
    make_fresh_forof_names(ibuf, sizeof ibuf, xbuf, sizeof xbuf);
    // derive iterator and result names from the same counter suffix
    {
        const char *suffix = ibuf + 2; /* skip "_i" prefix to get number suffix */
        snprintf(itbuf, TPSMALLBUFSZ+1, "_it%s", suffix);
        snprintf(rbuf, TPSMALLBUFSZ, "_r%s", suffix);
    }

    // Build replacement — supports both arrays and iterables (Symbol.iterator)
    // Pattern: var _x=<rhs>, _it=(...)?_x[Symbol.iterator]():null, _i=0, _r;
    //          while(_it?!(_r=_it.next()).done:_i<_x.length){<assign> <body>}
    rp_string *out = rp_string_new(256);

    rp_string_appendf(out, "var %s = ", xbuf);
    rp_string_putsn(out, src + rs, re - rs);
    rp_string_appendf(out,
        ", %s = (typeof Symbol!=='undefined'&&typeof %s[Symbol.iterator]==='function')?%s[Symbol.iterator]():null, %s = 0, %s; "
        "while(%s?!(%s=%s.next()).done:%s<%s.length) {",
        itbuf, xbuf, xbuf, ibuf, rbuf,       /* var line */
        itbuf, rbuf, itbuf, ibuf, xbuf);      /* while condition */

    // Assignment inside loop body
    if (is_decl)
        rp_string_puts(out, "var ");
    rp_string_putsn(out, src + ns, ne - ns);
    rp_string_appendf(out, " = %s?%s.value:%s[%s++]; ", itbuf, rbuf, xbuf, ibuf);

    // For let/const: IIFE-wrap body so closures get fresh per-iteration binding
    if (is_let)
    {
        rp_string_puts(out, "(function(");
        rp_string_putsn(out, src + ns, ne - ns);
        rp_string_puts(out, "){ ");
    }

    // splice body
    if (is_block)
    {
        // copy inner of the block (without braces)
        rp_string_putsn(out, src + bs + 1, (be - 1) - (bs + 1));
    }
    else
    {
        rp_string_putsn(out, src + bs, be - bs);
    }

    if (is_let)
    {
        rp_string_puts(out, " })(");
        rp_string_putsn(out, src + ns, ne - ns);
        rp_string_puts(out, ");");
    }
    rp_string_puts(out, "}");

    // Replace the whole for-of node
    size_t fs = ts_node_start_byte(forof);
    size_t fe = ts_node_end_byte(forof);
    add_edit_take_ownership(edits, fs, fe, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    return 1;
}

/* ——— Spread in function call arguments (ES2015) ———
 *  fn(...args)           → fn.apply(void 0, args)
 *  fn(a, ...args)        → fn.apply(void 0, [a].concat(args))
 *  obj.m(...args)        → obj.m.apply(obj, args)
 *  obj.m(a, ...args)     → obj.m.apply(obj, [a].concat(args))
 *  new Foo(...args)      → new (Function.prototype.bind.apply(Foo, [null].concat(args)))()
 *  new Foo(a, ...args)   → new (Function.prototype.bind.apply(Foo, [null, a].concat(args)))()
 */
static int rewrite_call_spread(EditList *edits, const char *src, TSNode node,
                               RangeList *claimed, int overlaps)
{
    const char *nt = ts_node_type(node);
    int is_new = (strcmp(nt, "new_expression") == 0);
    if (!is_new && strcmp(nt, "call_expression") != 0)
        return 0;

    TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
    if (ts_node_is_null(args_node))
        return 0;

    /* Check if any argument is a spread_element */
    uint32_t nargs = ts_node_named_child_count(args_node);
    int has_spread = 0;
    for (uint32_t i = 0; i < nargs; i++)
    {
        TSNode arg = ts_node_named_child(args_node, i);
        if (strcmp(ts_node_type(arg), "spread_element") == 0)
        {
            has_spread = 1;
            break;
        }
    }
    if (!has_spread)
        return 0;

    if (overlaps)
        return 1;

    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);

    /* Get the callee/constructor */
    TSNode callee = is_new
        ? ts_node_child_by_field_name(node, "constructor", 11)
        : ts_node_child_by_field_name(node, "function", 8);
    if (ts_node_is_null(callee))
        return 0;

    size_t cs = ts_node_start_byte(callee), ce = ts_node_end_byte(callee);

    /* For call_expression: determine context for .apply() */
    const char *callee_type = ts_node_type(callee);
    int is_member = (strcmp(callee_type, "member_expression") == 0);

    /* Build the args array expression */
    rp_string *argsexpr = rp_string_new(64);
    int spread_count = 0;

    for (uint32_t i = 0; i < nargs; i++)
    {
        TSNode arg = ts_node_named_child(args_node, i);
        if (strcmp(ts_node_type(arg), "spread_element") == 0)
            spread_count++;
    }

    if (nargs == 1 && spread_count == 1)
    {
        /* Single spread only: fn(...args) — just use the spread expr directly */
        TSNode arg = ts_node_named_child(args_node, 0);
        /* The spread_element has a child that is the actual expression */
        uint32_t snc = ts_node_named_child_count(arg);
        if (snc > 0)
        {
            TSNode inner = ts_node_named_child(arg, 0);
            size_t is = ts_node_start_byte(inner), ie = ts_node_end_byte(inner);
            rp_string_putsn(argsexpr, src + is, ie - is);
        }
    }
    else
    {
        /* Mixed args: build [a, b].concat(spread1, [c], spread2, ...) */
        /* Collect leading non-spread args into an array literal */
        rp_string *leading = rp_string_new(32);
        rp_string *concats = rp_string_new(64);

        int in_leading = 1;
        for (uint32_t i = 0; i < nargs; i++)
        {
            TSNode arg = ts_node_named_child(args_node, i);
            size_t as = ts_node_start_byte(arg), ae = ts_node_end_byte(arg);

            if (strcmp(ts_node_type(arg), "spread_element") == 0)
            {
                in_leading = 0;
                /* extract inner expression */
                uint32_t snc = ts_node_named_child_count(arg);
                if (snc > 0)
                {
                    TSNode inner = ts_node_named_child(arg, 0);
                    size_t is2 = ts_node_start_byte(inner), ie2 = ts_node_end_byte(inner);
                    if (concats->len)
                        rp_string_puts(concats, ", ");
                    rp_string_putsn(concats, src + is2, ie2 - is2);
                }
            }
            else
            {
                if (in_leading)
                {
                    if (leading->len)
                        rp_string_puts(leading, ", ");
                    rp_string_putsn(leading, src + as, ae - as);
                }
                else
                {
                    /* non-spread after a spread: wrap in array */
                    if (concats->len)
                        rp_string_puts(concats, ", ");
                    rp_string_puts(concats, "[");
                    rp_string_putsn(concats, src + as, ae - as);
                    rp_string_puts(concats, "]");
                }
            }
        }

        rp_string_puts(argsexpr, "[");
        if (leading->len)
            rp_string_puts(argsexpr, leading->str);
        rp_string_puts(argsexpr, "]");
        if (concats->len)
        {
            rp_string_puts(argsexpr, ".concat(");
            rp_string_puts(argsexpr, concats->str);
            rp_string_puts(argsexpr, ")");
        }
        leading = rp_string_free(leading);
        concats = rp_string_free(concats);
    }

    /* Build the final replacement */
    rp_string *out = rp_string_new(128);

    if (is_new)
    {
        /* new Foo(...args) → new (Function.prototype.bind.apply(Foo, [null].concat(args)))()  */
        rp_string_puts(out, "new (Function.prototype.bind.apply(");
        rp_string_putsn(out, src + cs, ce - cs);
        rp_string_puts(out, ", [null].concat(");
        rp_string_puts(out, argsexpr->str);
        rp_string_puts(out, ")))()");
    }
    else if (is_member)
    {
        /* obj.method(...args) → obj.method.apply(obj, args)
           Need to extract the object part of the member expression */
        TSNode obj = ts_node_child_by_field_name(callee, "object", 6);
        if (!ts_node_is_null(obj))
        {
            size_t os = ts_node_start_byte(obj), oe = ts_node_end_byte(obj);
            rp_string_putsn(out, src + cs, ce - cs);
            rp_string_puts(out, ".apply(");
            rp_string_putsn(out, src + os, oe - os);
            rp_string_puts(out, ", ");
            rp_string_puts(out, argsexpr->str);
            rp_string_puts(out, ")");
        }
        else
        {
            /* fallback: treat as simple call */
            rp_string_putsn(out, src + cs, ce - cs);
            rp_string_puts(out, ".apply(void 0, ");
            rp_string_puts(out, argsexpr->str);
            rp_string_puts(out, ")");
        }
    }
    else
    {
        /* fn(...args) → fn.apply(void 0, args) */
        rp_string_putsn(out, src + cs, ce - cs);
        rp_string_puts(out, ".apply(void 0, ");
        rp_string_puts(out, argsexpr->str);
        rp_string_puts(out, ")");
    }

    add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    argsexpr = rp_string_free(argsexpr);
    return 1;
}

/* ——— Nullish coalescing (ES2020): a ?? b  →  (a != null ? a : b) ——— */
static unsigned _nc_counter = 0;

static int rewrite_nullish_coalescing(EditList *edits, const char *src, TSNode node,
                                      RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(node), "binary_expression") != 0)
        return 0;

    /* find the ?? operator */
    TSNode op = ts_node_child_by_field_name(node, "operator", 8);
    if (ts_node_is_null(op))
        return 0;
    size_t ops = ts_node_start_byte(op), ope = ts_node_end_byte(op);
    if (ope - ops != 2 || src[ops] != '?' || src[ops + 1] != '?')
        return 0;

    if (overlaps)
        return 1;

    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    TSNode right = ts_node_child_by_field_name(node, "right", 5);
    if (ts_node_is_null(left) || ts_node_is_null(right))
        return 0;

    size_t ls = ts_node_start_byte(left), le = ts_node_end_byte(left);
    size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
    size_t es = ts_node_start_byte(node), ee = ts_node_end_byte(node);

    /* check if left is a simple identifier (safe to repeat without side effects) */
    int is_simple = (strcmp(ts_node_type(left), "identifier") == 0);

    rp_string *out = rp_string_new(64);

    if (is_simple)
    {
        /* (left != null ? left : right) */
        rp_string_puts(out, "(");
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " != null ? ");
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " : ");
        rp_string_putsn(out, src + rs, re - rs);
        rp_string_puts(out, ")");
    }
    else
    {
        /* (_nc = left, _nc != null ? _nc : right) */
        char tvar[32];
        snprintf(tvar, sizeof(tvar), "_nc%u", _nc_counter++);
        rp_string_appendf(out, "(%s = ", tvar);
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_appendf(out, ", %s != null ? %s : ", tvar, tvar);
        rp_string_putsn(out, src + rs, re - rs);
        rp_string_puts(out, ")");
    }

    add_edit_take_ownership(edits, es, ee, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    return 1;
}

/* ——— Optional chaining (ES2020): a?.b → (a == null ? void 0 : a.b) ——— */

/* Check if a node has an optional_chain child */
static int _has_optional_chain(TSNode node)
{
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
    {
        TSNode ch = ts_node_child(node, i);
        if (strcmp(ts_node_type(ch), "optional_chain") == 0)
            return 1;
    }
    return 0;
}

/* Check if the chain (object/function children, not arguments) contains ?. */
static int _chain_has_optional_chain(TSNode node)
{
    if (_has_optional_chain(node))
        return 1;
    /* walk down the chain: object or function child only, NOT arguments */
    TSNode obj = ts_node_child_by_field_name(node, "object", 6);
    if (ts_node_is_null(obj))
        obj = ts_node_child_by_field_name(node, "function", 8);
    if (!ts_node_is_null(obj))
        return _chain_has_optional_chain(obj);
    return 0;
}

/* Check if parent's chain includes this node and has ?. */
static int _parent_is_optional_chain(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent))
        return 0;
    const char *pt = ts_node_type(parent);
    if (strcmp(pt, "member_expression") != 0 &&
        strcmp(pt, "call_expression") != 0 &&
        strcmp(pt, "subscript_expression") != 0)
        return 0;
    /* check if this node is the parent's chain child (not an argument) */
    TSNode pobj = ts_node_child_by_field_name(parent, "object", 6);
    if (ts_node_is_null(pobj))
        pobj = ts_node_child_by_field_name(parent, "function", 8);
    if (!ts_node_is_null(pobj) &&
        ts_node_start_byte(pobj) == ts_node_start_byte(node) &&
        ts_node_end_byte(pobj) == ts_node_end_byte(node))
    {
        if (_chain_has_optional_chain(parent))
            return 1;
    }
    return 0;
}

static int rewrite_optional_chaining(EditList *edits, const char *src, TSNode node,
                                     RangeList *claimed, int overlaps)
{
    const char *nt = ts_node_type(node);

    /* only handle member_expression, call_expression, subscript_expression with ?. */
    if (strcmp(nt, "member_expression") != 0 &&
        strcmp(nt, "call_expression") != 0 &&
        strcmp(nt, "subscript_expression") != 0)
        return 0;

    if (!_chain_has_optional_chain(node))
        return 0;

    /* only handle from the outermost optional chain expression */
    if (_parent_is_optional_chain(node))
        return 0;

    if (overlaps)
        return 1;

    size_t es = ts_node_start_byte(node), ee = ts_node_end_byte(node);
    size_t elen = ee - es;

    /* Build clean expression (with ?. replaced by . or removed for ?.[/?.() */
    char *clean = malloc(elen + 1);
    size_t ci = 0;

    /* Collect positions of ?. in original, and map to clean positions */
    size_t oc_orig[32], oc_clean[32];
    int noc = 0;

    for (size_t i = 0; i < elen; i++)
    {
        if (i + 1 < elen && src[es + i] == '?' && src[es + i + 1] == '.')
        {
            if (noc < 32)
            {
                oc_orig[noc] = i;
                oc_clean[noc] = ci;
                noc++;
            }
            /* check if ?. is followed by [ or ( — skip ?. entirely */
            if (i + 2 < elen && (src[es + i + 2] == '[' || src[es + i + 2] == '('))
            {
                i++; /* skip ?, loop skips . */
            }
            else
            {
                /* replace ?. with . */
                clean[ci++] = '.';
                i++; /* skip ? */
            }
        }
        else
        {
            clean[ci++] = src[es + i];
        }
    }
    clean[ci] = '\0';

    if (noc == 0)
    {
        free(clean);
        return 0;
    }

    /* Build properly nested ternaries:
       obj?.a?.b?.c  →  (obj == null ? void 0 : (obj.a == null ? void 0 : (obj.a.b == null ? void 0 : obj.a.b.c)))

       For each ?. at clean position cp[i], the base to check is clean[0..cp[i]-1].
       We nest from left (outermost) to right (innermost).

       If the base before the first ?. is non-trivial (contains parens or brackets),
       use a temp var to avoid double evaluation:
       getObj()?.x  →  (_oc0 = getObj(), _oc0 == null ? void 0 : _oc0.x) */

    rp_string *out = rp_string_new(elen * 2);

    /* Check if the base before first ?. needs a temp var */
    static int oc_counter = 0;
    size_t base_len = oc_clean[0];
    int needs_temp = 0;
    for (size_t j = 0; j < base_len; j++)
    {
        char c = clean[j];
        if (c == '(' || c == ')' || c == '[' || c == ']')
        {
            needs_temp = 1;
            break;
        }
    }

    if (needs_temp)
    {
        char tmpname[32];
        snprintf(tmpname, sizeof(tmpname), "_oc%d", oc_counter++);

        /* (_ocN = <base>, nested ternaries using _ocN as base) */
        rp_string_puts(out, "(");
        rp_string_puts(out, tmpname);
        rp_string_puts(out, " = ");
        rp_string_putsn(out, clean, base_len);
        rp_string_puts(out, ", ");

        for (int i = 0; i < noc; i++)
            rp_string_puts(out, "(");

        for (int i = 0; i < noc; i++)
        {
            size_t cp = oc_clean[i];
            rp_string_puts(out, tmpname);
            if (cp > base_len)
                rp_string_putsn(out, clean + base_len, cp - base_len);
            rp_string_puts(out, " == null ? void 0 : ");
        }

        /* final full expression */
        rp_string_puts(out, tmpname);
        if (ci > base_len)
            rp_string_putsn(out, clean + base_len, ci - base_len);

        for (int i = 0; i < noc; i++)
            rp_string_puts(out, ")");
        rp_string_puts(out, ")");
    }
    else
    {
        for (int i = 0; i < noc; i++)
            rp_string_puts(out, "(");

        for (int i = 0; i < noc; i++)
        {
            size_t cp = oc_clean[i];
            rp_string_putsn(out, clean, cp);
            rp_string_puts(out, " == null ? void 0 : ");
        }

        /* final full expression */
        rp_string_putsn(out, clean, ci);

        for (int i = 0; i < noc; i++)
            rp_string_puts(out, ")");
    }

    add_edit_take_ownership(edits, es, ee, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    free(clean);
    return 1;
}

/* ——— Logical assignment (ES2021): a ??= b → a = (a != null ? a : b), etc. ——— */
static int rewrite_logical_assignment(EditList *edits, const char *src, TSNode node,
                                      RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(node), "augmented_assignment_expression") != 0)
        return 0;

    /* find the operator */
    size_t node_s = ts_node_start_byte(node), node_e = ts_node_end_byte(node);
    TSNode left = ts_node_child_by_field_name(node, "left", 4);
    TSNode right = ts_node_child_by_field_name(node, "right", 5);
    if (ts_node_is_null(left) || ts_node_is_null(right))
        return 0;

    /* scan for the operator between left end and right start */
    size_t le = ts_node_end_byte(left);
    size_t rs = ts_node_start_byte(right);
    int op_type = 0; /* 1=??=  2=||=  3=&&= */

    for (size_t i = le; i + 2 < rs; i++)
    {
        if (src[i] == '?' && src[i + 1] == '?' && src[i + 2] == '=')
            { op_type = 1; break; }
        if (src[i] == '|' && src[i + 1] == '|' && src[i + 2] == '=')
            { op_type = 2; break; }
        if (src[i] == '&' && src[i + 1] == '&' && src[i + 2] == '=')
            { op_type = 3; break; }
    }

    if (op_type == 0)
        return 0;

    if (overlaps)
        return 1;

    size_t ls = ts_node_start_byte(left);
    size_t re = ts_node_end_byte(right);

    rp_string *out = rp_string_new(64);

    if (op_type == 1)
    {
        /* a ??= b  →  a = (a != null ? a : b) */
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " = (");
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " != null ? ");
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " : ");
        rp_string_putsn(out, src + rs, re - rs);
        rp_string_puts(out, ")");
    }
    else if (op_type == 2)
    {
        /* a ||= b  →  a = a || b */
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " = ");
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " || ");
        rp_string_putsn(out, src + rs, re - rs);
    }
    else
    {
        /* a &&= b  →  a = a && b */
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " = ");
        rp_string_putsn(out, src + ls, le - ls);
        rp_string_puts(out, " && ");
        rp_string_putsn(out, src + rs, re - rs);
    }

    add_edit_take_ownership(edits, node_s, node_e, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    return 1;
}

// Rewrite computed method shorthand in object literals:
//   [expr]() { body }  =>  [expr]: function() { body }
// Also handles get/set:
//   get [expr]() { body }  =>  get [expr]() { body }  (these are already valid? No, Duktape fails on them too)
// For now, only handle the plain method case.
// Rewrite plain method shorthand in object literals when name is "get" or "set":
//   get(params) { body }  ->  get: function(params) { body }
// Duktape misparses "get"/"set" as accessor keywords in this context.
static int rewrite_plain_method_shorthand(EditList *edits, const char *src, TSNode node,
                                          RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(node), "method_definition") != 0)
        return 0;
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent) || strcmp(ts_node_type(parent), "object") != 0)
        return 0;
    TSNode nname = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(nname))
        return 0;
    /* Only handle plain identifiers (computed handled by rewrite_computed_method_shorthand) */
    if (strcmp(ts_node_type(nname), "property_identifier") != 0)
        return 0;
    size_t ns = ts_node_start_byte(nname), ne_name = ts_node_end_byte(nname);
    size_t namelen = ne_name - ns;
    /* Only rewrite "get" and "set" -- other shorthand methods work in Duktape */
    if (!((namelen == 3 && strncmp(src + ns, "get", 3) == 0) ||
          (namelen == 3 && strncmp(src + ns, "set", 3) == 0)))
        return 0;
    if (overlaps)
        return 1;
    /* Insert ": function" between name and params */
    add_edit(edits, ne_name, ne_name, ": function", claimed);
    return 1;
}

static int rewrite_computed_method_shorthand(EditList *edits, const char *src, TSNode node, RangeList *claimed,
                                             int overlaps)
{
    if (strcmp(ts_node_type(node), "method_definition") != 0)
        return 0;

    // Only handle methods inside object literals (pair parent), not classes
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent) || strcmp(ts_node_type(parent), "object") != 0)
        return 0;

    TSNode nname = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(nname) || strcmp(ts_node_type(nname), "computed_property_name") != 0)
        return 0;

    if (overlaps)
        return 1;

    TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body))
        return 0;

    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);
    size_t ks = ts_node_start_byte(nname), ke = ts_node_end_byte(nname);
    size_t ps = ts_node_is_null(params) ? 0 : ts_node_start_byte(params);
    size_t pe = ts_node_is_null(params) ? 0 : ts_node_end_byte(params);
    size_t bs = ts_node_start_byte(body), be = ts_node_end_byte(body);

    rp_string *out = rp_string_new(64);
    // [expr]: function(params) { body }
    rp_string_putsn(out, src + ks, ke - ks);
    rp_string_puts(out, ": function");
    if (ps && pe)
        rp_string_putsn(out, src + ps, pe - ps);
    else
        rp_string_puts(out, "()");
    rp_string_puts(out, " ");
    rp_string_putsn(out, src + bs, be - bs);

    add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    return 1;
}

static void tp_linecol_from_src_offset_utf8(const char *src, size_t src_len, uint32_t byte_off, int *out_line,
                                            int *out_col)
{
    if (!src)
    {
        if (out_line)
            *out_line = 1;
        if (out_col)
            *out_col = 1;
        return;
    }

    if (src_len == 0)
    {
        src_len = strlen(src); // safe if NUL-terminated; otherwise pass length explicitly
    }

    if (byte_off > src_len)
    {
        byte_off = (uint32_t)src_len; // clamp
    }

    // Count lines up to byte_off, tracking start index of current line.
    uint32_t line = 1;     // 1-based
    size_t line_start = 0; // byte index of current line start

    for (size_t i = 0; i < byte_off;)
    {
        unsigned char c = (unsigned char)src[i];

        if (c == '\n')
        {
            line++;
            line_start = i + 1;
            i++;
            continue;
        }
        if (c == '\r')
        {
            // Treat CRLF as one newline; lone CR as newline too.
            if (i + 1 < byte_off && (unsigned char)src[i + 1] == '\n')
            {
                i += 2;
            }
            else
            {
                i += 1;
            }
            line++;
            line_start = i;
            continue;
        }
        // Not a newline; advance by one byte
        i++;
    }

    // Column in UTF-8 code points from line_start to byte_off (exclusive).
    // Count only leading bytes of code points (bytes where (b & 0xC0) != 0x80).
    int col = 1; // 1-based
    for (size_t i = line_start; i < byte_off; i++)
    {
        unsigned char b = (unsigned char)src[i];
        if ((b & 0xC0) != 0x80)
        {
            col++;
        }
    }
    col -= 1; // we started at 1 then increment before first char; normalize

    if (out_line)
        *out_line = (int)line;
    if (out_col)
        *out_col = (int)col;
}


// Core doubling rule used for this pass:
//
// Turn '\' -> '\\' except:
//   - if the next char is '$' or '`' (leave the '\' alone),
//   - OR if the next char is '\' *and* the following char is '$' or '`'
//     (i.e., this '\' is the first of a pair that precedes $ or `; leave it alone).
//
// Everything else is preserved unchanged.
static char *double_backslashes_except_dollar_or_tick(const char *body, size_t len) {
    // generous capacity: worst case every '\' doubles
    size_t cap = len * 2 + 3;
    char *out = (char *)malloc(cap);
    size_t o = 0;

    out[o++] = '`';

    for (size_t i = 0; i < len; i++) {
        const char *s = &body[i];

        int nbs=0;
        while(*s == '\\' && i<len)
        {
            out[o++] = '\\';
            out[o++] = '\\';
            s++;
            i++;
            nbs++;
        }
        if( ( *s=='`' || *s=='$' ) && (nbs % 2) )
            out[o++] = '\\';

        out[o++] = *s;
    }
    out[o++] = '`';

    out[o] = '\0';
    return out;
}

int rewrite_string_raw(EditList *edits, const char *src, TSNode call_expr, RangeList *claimed, int overlaps) {
    if (ts_node_is_null(call_expr) || strcmp(ts_node_type(call_expr), "call_expression") != 0) {
        return 0;
    }
    size_t cs = ts_node_start_byte(call_expr), ce = ts_node_end_byte(call_expr);

    // Ensure callee is exactly "String.raw"
    TSNode func = ts_node_child_by_field_name(call_expr, "function", 8);
    if (ts_node_is_null(func) || strcmp(ts_node_type(func), "member_expression") != 0) {
        return 0;
    }
    TSNode obj = ts_node_child_by_field_name(func, "object", 6);
    TSNode prop = ts_node_child_by_field_name(func, "property", 8);
    if (ts_node_is_null(obj) || ts_node_is_null(prop)) return 0;

    size_t obj_a = ts_node_start_byte(obj), obj_b = ts_node_end_byte(obj);
    size_t prop_a = ts_node_start_byte(prop), prop_b = ts_node_end_byte(prop);
    if ((obj_b - obj_a) != 6 || strncmp(src + obj_a, "String", 6) != 0) return 0;
    if ((prop_b - prop_a) != 3 || strncmp(src + prop_a, "raw", 3) != 0) return 0;

    // Argument must be a template_string (may contain substitutions; that's OK for this pass)
    TSNode args = ts_node_child_by_field_name(call_expr, "arguments", 9);
    if (ts_node_is_null(args) || strcmp(ts_node_type(args), "template_string") != 0) {
        return 0;
    }

    if(overlaps)
        return 1;

    // Backtick-delimited source span
    size_t t_a = ts_node_start_byte(args);
    size_t t_b = ts_node_end_byte(args);
    if (t_b <= t_a + 2) {
        add_edit(edits, cs, ce, "\"\"", claimed);
        return 1;
    }

    // Body between backticks
    size_t body_a = t_a + 1;
    size_t body_b = t_b - 1;

    size_t body_len = body_b - body_a;

    // Apply the specific doubling rule
    char *rewritten = double_backslashes_except_dollar_or_tick(src+body_a, body_len);
    add_edit_take_ownership(edits, cs, ce, rewritten, claimed);

    return 1;
}

/* Check whether any ancestor statement_block (up to a function boundary)
   contains a function_declaration with the given name.  Used to detect
   shadowing for block-scoped function rewrites. */
static int _ancestor_has_function_decl(TSNode block, const char *src,
                                       const char *name, size_t nlen)
{
    TSNode anc = ts_node_parent(block);
    while (!ts_node_is_null(anc))
    {
        const char *t = ts_node_type(anc);
        /* Stop at function / class boundaries */
        if (strstr(t, "function") || strcmp(t, "arrow_function") == 0 ||
            strstr(t, "method") != NULL || strstr(t, "class") != NULL)
            break;
        if (strcmp(t, "statement_block") == 0)
        {
            uint32_t c = ts_node_named_child_count(anc);
            for (uint32_t j = 0; j < c; j++)
            {
                TSNode ch = ts_node_named_child(anc, j);
                if (strcmp(ts_node_type(ch), "function_declaration") != 0)
                    continue;
                TSNode nn = ts_node_child_by_field_name(ch, "name", 4);
                if (ts_node_is_null(nn))
                    continue;
                size_t s = ts_node_start_byte(nn), e = ts_node_end_byte(nn);
                if (e - s == nlen && memcmp(src + s, name, nlen) == 0)
                    return 1;
            }
        }
        anc = ts_node_parent(anc);
    }
    return 0;
}

/* Block-scoped function declarations (ES2015):
   When the same function name is declared in nested blocks, the inner
   declaration shadows the outer one for the duration of that block.
   In ES5 both are hoisted to function scope, so the inner one clobbers
   the outer.  We fix this only when shadowing is detected:

       {                                    {
         function foo() { return 1; }         function foo() { return 1; }
         {                            →       {
           function foo() { return 2; }         var _bsf0=foo;
         }                                      var foo = function(){ return 2; };
         foo() // should be 1                   foo=_bsf0;
       }                                      }
                                               foo() // 1 ✓
                                             }                                          */
static int rewrite_block_scoped_functions(EditList *edits, const char *src, TSNode block,
                                          RangeList *claimed, int overlaps)
{
    /* Parent must NOT be a function-like node (whose body this block is) */
    TSNode parent = ts_node_parent(block);
    if (ts_node_is_null(parent))
        return 0;
    const char *ptype = ts_node_type(parent);
    if (strstr(ptype, "function") != NULL || strcmp(ptype, "arrow_function") == 0 ||
        strstr(ptype, "method") != NULL || strstr(ptype, "class") != NULL)
        return 0;

    /* Scan for function_declarations that shadow an ancestor declaration */
    uint32_t count = ts_node_named_child_count(block);
    int nfuncs = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        TSNode child = ts_node_named_child(block, i);
        if (strcmp(ts_node_type(child), "function_declaration") != 0)
            continue;
        if (_is_async_function_like(child))
            continue;
        TSNode nn = ts_node_child_by_field_name(child, "name", 4);
        if (ts_node_is_null(nn))
            continue;
        size_t ns = ts_node_start_byte(nn), ne = ts_node_end_byte(nn);
        if (_ancestor_has_function_decl(block, src, src + ns, ne - ns))
            nfuncs++;
    }
    if (!nfuncs)
        return 0;
    if (overlaps)
        return 1;

    static int bsf_counter = 0;

    rp_string *saves = rp_string_new(64);
    rp_string *restores = rp_string_new(64);

    for (uint32_t i = 0; i < count; i++)
    {
        TSNode child = ts_node_named_child(block, i);
        if (strcmp(ts_node_type(child), "function_declaration") != 0)
            continue;
        if (_is_async_function_like(child))
            continue;
        TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
        TSNode params_node = ts_node_child_by_field_name(child, "parameters", 10);
        if (ts_node_is_null(name_node) || ts_node_is_null(params_node))
            continue;

        size_t name_s = ts_node_start_byte(name_node);
        size_t name_e = ts_node_end_byte(name_node);
        size_t nlen = name_e - name_s;

        /* Only transform declarations that shadow an ancestor */
        if (!_ancestor_has_function_decl(block, src, src + name_s, nlen))
            continue;

        size_t func_s = ts_node_start_byte(child);
        size_t params_s = ts_node_start_byte(params_node);

        int id = bsf_counter++;

        /* save: var _bsfN = NAME; */
        rp_string_appendf(saves, "var _bsf%d=%.*s;", id, (int)nlen, src + name_s);

        /* restore: NAME = _bsfN; */
        rp_string_appendf(restores, "%.*s=_bsf%d;", (int)nlen, src + name_s, id);

        /* convert "function NAME" → "var NAME = function" */
        rp_string *repl = rp_string_new(32);
        rp_string_appendf(repl, "var %.*s = function", (int)nlen, src + name_s);
        add_edit_take_ownership(edits, func_s, params_s, rp_string_steal(repl), claimed);
        repl = rp_string_free(repl);
    }

    /* Insert saves after opening '{' */
    size_t bs = ts_node_start_byte(block);
    add_edit_take_ownership(edits, bs + 1, bs + 1, rp_string_steal(saves), claimed);
    saves = rp_string_free(saves);

    /* Insert restores before closing '}' */
    size_t be = ts_node_end_byte(block);
    add_edit_take_ownership(edits, be - 1, be - 1, rp_string_steal(restores), claimed);
    restores = rp_string_free(restores);

    return 1;
}

RP_ParseRes transpiler_rewrite_pass(EditList *edits, const char *src, size_t src_len, TSNode root,
                                    uint32_t *polysneeded, int *unresolved, int no_program_wrap)
{
    RP_ParseRes ret;
    RangeList claimed;
    TSTreeCursor cur;

    ret.err = 0;
    ret.line_num = 0;
    ret.col_num = 0;
    ret.altered = 0;
    ret.pos = 0;
    ret.transpiled = NULL;

    *unresolved = 0;

    rl_init(&claimed);
    cur = ts_tree_cursor_new(root);

    for (;;)
    {
        TSNode n = ts_tree_cursor_current_node(&cur);
        const char *nt = ts_node_type(n);
        size_t ns = ts_node_start_byte(n), ne = ts_node_end_byte(n);

        // errors
        if (!ret.err && (nt && strcmp(nt, "ERROR") == 0))
        {
            ret.err = 1;
            ret.pos = ts_node_start_byte(n);
            tp_linecol_from_src_offset_utf8(src, src_len, ret.pos, &ret.line_num, &ret.col_num);
        }

        int overlaps = rl_overlaps(&claimed, ns, ne, "transpiler_rewrite");
        int handled = 0;

        /* functions return handled==1 when overlap==1, but do not actually do any edits.
           read as "would_overlap", and so we need another pass                             */

        if (strcmp(nt, "regex") == 0)
        {
            handled = rewrite_regex_u_to_es5(edits, src, n, &claimed, overlaps);
        }

        if (!handled && (strcmp(nt, "template_string") == 0 || strcmp(nt, "template_literal") == 0))
        {
            handled = rewrite_template_node(edits, src, n, &claimed, overlaps);
        }

        if (!handled && (strcmp(nt, "call_expression") == 0))
        {
            handled = rewrite_string_raw(edits, src, n, &claimed, overlaps);
        }

        if (!handled && (strcmp(nt, "string") == 0 || strcmp(nt, "template_literal") == 0))
        {
            handled = rewrite_raw_node(edits, src, n, &claimed, overlaps);
        }

        /* class transpile produces functions, and then in pass2, handle them */
        if (!handled && strcmp(nt, "class_declaration") == 0)
        {
            handled = rewrite_class_to_es5(edits, src, n, &claimed, overlaps);
            if (handled)
                *polysneeded |= CLASS_PF;
        }

        if (!handled && strcmp(nt, "class") == 0)
        {
            handled = rewrite_class_expression_to_es5(edits, src, n, &claimed, polysneeded, overlaps);
        }

        /* Block-scoped function declarations (ES2015) */
        if (!handled && strcmp(nt, "statement_block") == 0)
        {
            handled = rewrite_block_scoped_functions(edits, src, n, &claimed, overlaps);
        }

        if (!handled && (strcmp(nt, "import_statement") == 0))
        {
            handled = rewrite_import_node(edits, src, n, &claimed, polysneeded, overlaps);
        }
        if (!handled && (strcmp(nt, "export_statement") == 0))
        {
            handled = rewrite_export_node(edits, src, n, &claimed, overlaps);
            if (handled)
                *polysneeded |= IMPORT_PF;
        }

        if (!handled && (strcmp(nt, "function_declaration") == 0 || strcmp(nt, "function") == 0 ||
                         strcmp(nt, "function_expression") == 0 || strcmp(nt, "arrow_function") == 0 || strcmp(nt, "method_definition") == 0))
        {
            handled = rewrite_async_await_to_regenerator(edits, src, n, &claimed, overlaps);
            if (handled)
                *polysneeded |= ASYNC_PF | PROMISE_PF;
        }

        if (!handled && (strcmp(nt, "generator_function_declaration") == 0 ||
                         strcmp(nt, "generator_function") == 0 ||
                         strcmp(nt, "generator_function_expression") == 0 ||
                         strcmp(nt, "method_definition") == 0))
        {
            handled = rewrite_generator_to_regenerator(edits, src, n, &claimed, overlaps);
            if (handled)
                *polysneeded |= ASYNC_PF;
        }

        if (!handled && strcmp(nt, "method_definition") == 0)
        {
            handled = rewrite_plain_method_shorthand(edits, src, n, &claimed, overlaps);
        }
        if (!handled && strcmp(nt, "method_definition") == 0)
        {
            handled = rewrite_computed_method_shorthand(edits, src, n, &claimed, overlaps);
        }
        if (!handled && strcmp(nt, "arrow_function") == 0)
        {
            handled = rewrite_arrow_function_node(edits, src, n, &claimed, overlaps);
        }
        if (!handled && strcmp(nt, "variable_declaration") == 0)
        {
            handled = rewrite_var_function_expression_defaults(edits, src, n, &claimed, overlaps);
        }
        if (!handled && strcmp(nt, "variable_declaration") == 0)
        {
            handled = rewrite_destructuring_declaration(edits, src, n, &claimed, overlaps);
        }
        if (!handled && (strcmp(nt, "function_declaration") == 0 || strcmp(nt, "function") == 0 ||
                         strcmp(nt, "function_expression") == 0 || strcmp(nt, "generator_function_declaration") == 0 ||
                         strcmp(nt, "generator_function") == 0 || strcmp(nt, "generator_function_expression") == 0))
        {
            handled = rewrite_function_like_default_params(edits, src, n, &claimed, overlaps);
            if (!handled)
                handled = rewrite_function_rest(edits, src, n, &claimed, overlaps);
            if (!handled)
                handled = rewrite_function_destructuring_params(edits, src, n, &claimed, overlaps);
        }

        if (!handled && strcmp(nt, "expression_statement") == 0)
        {
            TSNode expr = ts_node_named_child(n, 0);
            if (!ts_node_is_null(expr))
            {
                const char *et = ts_node_type(expr);
                if (strcmp(et, "function_expression") == 0 || strcmp(et, "function") == 0 ||
                    strcmp(et, "generator_function_expression") == 0 || strcmp(et, "generator_function") == 0)
                {
                    TSNode ep = ts_node_child_by_field_name(expr, "parameters", 10);
                    if (!ts_node_is_null(ep) && params_has_assignment_pattern(ep))
                    {
                        handled = rewrite_function_like_default_params(edits, src, expr, &claimed, overlaps);
                    }
                }
            }
        }
        if (!handled && strcmp(nt, "expression_statement") == 0)
        {
            handled = rewrite_destructuring_assignment(edits, src, n, &claimed, overlaps);
        }

        if (!handled && strcmp(nt, "for_in_statement") == 0)
        {
            // First try destructuring (let/const with patterns)
            handled = rewrite_for_of_destructuring(edits, src, n, &claimed, polysneeded, overlaps);
            // Then handle the common simple cases (var a of X, a of X)
            if (!handled)
            {
                handled = rewrite_for_of_simple(edits, src, n, &claimed, polysneeded, overlaps);
            }
        }

        if (!handled && strcmp(nt, "lexical_declaration") == 0)
        {
            handled = rewrite_lexical_declaration(edits, src, n, &claimed, overlaps, no_program_wrap);
        }

        if (!handled && strcmp(nt, "array") == 0)
        {
            handled = rewrite_array_spread(edits, src, n, 0, &claimed, polysneeded, overlaps);
        }
        if (!handled && strcmp(nt, "object") == 0)
        {
            handled = rewrite_array_spread(edits, src, n, 1, &claimed, polysneeded, overlaps);
        }

        /* Spread in function/new call args (ES2015) */
        if (!handled && (strcmp(nt, "call_expression") == 0 || strcmp(nt, "new_expression") == 0))
        {
            handled = rewrite_call_spread(edits, src, n, &claimed, overlaps);
        }

        /* Nullish coalescing (ES2020): a ?? b  →  (a != null ? a : b) */
        if (!handled && strcmp(nt, "binary_expression") == 0)
        {
            handled = rewrite_nullish_coalescing(edits, src, n, &claimed, overlaps);
        }

        /* Optional chaining (ES2020): obj?.a  →  (obj == null ? void 0 : obj.a) */
        if (!handled && (strcmp(nt, "member_expression") == 0 ||
                         strcmp(nt, "call_expression") == 0 ||
                         strcmp(nt, "subscript_expression") == 0))
        {
            handled = rewrite_optional_chaining(edits, src, n, &claimed, overlaps);
        }

        /* Logical assignment (ES2021): a ??= b, a ||= b, a &&= b */
        if (!handled && strcmp(nt, "augmented_assignment_expression") == 0)
        {
            handled = rewrite_logical_assignment(edits, src, n, &claimed, overlaps);
        }

        /* Optional catch binding (ES2019): catch {} -> catch(_unused) {} */
        if (!handled && strcmp(nt, "catch_clause") == 0)
        {
            TSNode param = ts_node_child_by_field_name(n, "parameter", 9);
            if (ts_node_is_null(param))
            {
                TSNode cbody = ts_node_child_by_field_name(n, "body", 4);
                if (!ts_node_is_null(cbody))
                {
                    if (!overlaps)
                    {
                        size_t body_s = ts_node_start_byte(cbody);
                        add_edit(edits, body_s, body_s, "(_unused) ", &claimed);
                    }
                    handled = 1;
                }
            }
        }

        /* Numeric separators (ES2021): strip underscores from number literals */
        if (strcmp(nt, "number") == 0)
        {
            int has_sep = 0;
            for (size_t j = ns; j < ne; j++)
            {
                if (src[j] == '_') { has_sep = 1; break; }
            }
            if (has_sep && !overlaps)
            {
                char *buf = malloc(ne - ns + 1);
                size_t k = 0;
                for (size_t j = ns; j < ne; j++)
                {
                    if (src[j] != '_')
                        buf[k++] = src[j];
                }
                buf[k] = '\0';
                add_edit(edits, ns, ne, buf, &claimed);
                free(buf);
                handled = 1;
            }
            else if (has_sep)
                handled = 1;
        }

        /* Strip trailing commas from function params and call arguments (ES2017 -> ES5) */
        if (strcmp(nt, "formal_parameters") == 0 || strcmp(nt, "arguments") == 0)
        {
            uint32_t nc = ts_node_named_child_count(n);
            if (nc > 0)
            {
                TSNode last = ts_node_named_child(n, nc - 1);
                size_t after = ts_node_end_byte(last);
                size_t paren = ne - 1; /* position of ')' */
                for (size_t j = after; j < paren; j++)
                {
                    if (src[j] == ',')
                    {
                        add_edit(edits, j, j + 1, "", &claimed);
                        break;
                    }
                }
            }
        }

        /* just need the polyfill if we see this */
        if (strcmp(nt, "identifier") == 0)
        {
            size_t start = ts_node_start_byte(n), end = ts_node_end_byte(n);
            size_t ilen = end - start;
            if (strncmp("Promise", src + start, ilen) == 0 && ilen == 7)
                *polysneeded |= PROMISE_PF;
            /* Set, Map, WeakSet, WeakMap polyfills */
            if ((ilen == 3 && strncmp(src + start, "Set", 3) == 0) ||
                (ilen == 3 && strncmp(src + start, "Map", 3) == 0) ||
                (ilen == 7 && strncmp(src + start, "WeakSet", 7) == 0) ||
                (ilen == 7 && strncmp(src + start, "WeakMap", 7) == 0))
                *polysneeded |= COLLECT_PF;
        }

        /* ES2017 polyfills: Object.entries, padStart/padEnd, getOwnPropertyDescriptors */
        if (strcmp(nt, "member_expression") == 0)
        {
            TSNode prop = ts_node_child_by_field_name(n, "property", 8);
            if (!ts_node_is_null(prop))
            {
                size_t ps = ts_node_start_byte(prop), pe = ts_node_end_byte(prop);
                size_t plen = pe - ps;
                if ((plen == 8 && strncmp(src + ps, "padStart", 8) == 0) ||
                    (plen == 6 && strncmp(src + ps, "padEnd", 6) == 0))
                {
                    *polysneeded |= ES2017_PF;
                }
                else if ((plen == 7 && strncmp(src + ps, "entries", 7) == 0) ||
                         (plen == 25 && strncmp(src + ps, "getOwnPropertyDescriptors", 25) == 0))
                {
                    TSNode obj = ts_node_child_by_field_name(n, "object", 6);
                    if (!ts_node_is_null(obj))
                    {
                        size_t os = ts_node_start_byte(obj), oe = ts_node_end_byte(obj);
                        if (oe - os == 6 && strncmp(src + os, "Object", 6) == 0)
                            *polysneeded |= ES2017_PF;
                    }
                }
            }
        }

        if (handled && overlaps)
            *unresolved = 1;

        if (ts_tree_cursor_goto_first_child(&cur))
            continue;
        while (!ts_tree_cursor_goto_next_sibling(&cur))
        {
            if (!ts_tree_cursor_goto_parent(&cur))
            {
                ts_tree_cursor_delete(&cur);
                free(claimed.a);
                return ret;
            }
        }
    }
    return ret;
}

#define MAX_PASSES 10

static RP_ParseRes transpile_code(const char *src, size_t src_len, int printTree, int track_polys, int no_program_wrap)
{
    TSParser *parser;
    TSTree *tree;
    TSNode root;
    uint32_t polysneeded = 0;
    FILE *f = stdout;
    EditList edits;
    RP_ParseRes res;
    int npasses = 0;
    int unresolved = 1;
    char *free_src = NULL;
    static uint32_t polysdone = 0;

    if (!track_polys)
        polysdone = 0;

    while (unresolved)
    {
        parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_javascript());

        init_edits(&edits);

        if (free_src)
            src = free_src;

        // pass a -1 or a 0 to get length, but use TRANSPILE_CALC_SIZE (0)
        if (!src_len || (ssize_t)src_len == -1)
            src_len = strlen(src);

        tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)src_len);
        root = ts_tree_root_node(tree);

        if (!npasses)
        {
            /* Warn about unsupported patterns on first pass (read-only scan) */
            warn_unsupported_patterns(root);

            if (printTree == 2)
                f = stderr;

            if (printTree)
            {
                fputs(
                    "=== Outline ===\n  node_type(node_field_name) [start, end]\n     or if field_name is NULL\n  node_type [start, end]\n",
                    f);
                print_outline(src, root, 0, f, 1);
                fputs("---------------------------------------------\n", f);
            }
        }

        npasses++;

        if (npasses > MAX_PASSES)
        {
            fprintf(stderr, "Transpiler: Giving up after %d passes over source\n", MAX_PASSES);
            exit(1);
        }

        res = transpiler_rewrite_pass(&edits, src, src_len, root, &polysneeded, &unresolved, no_program_wrap);
        res.errmsg = NULL;

        if (edits.len || polysneeded)
        {
            // Always emit the _TrN_Sp preamble when code is altered
            if (edits.len && !polysneeded)
                polysneeded = BASE_PF;
            uint32_t polysneed_not_added = polysneeded & ~polysdone;

            res.transpiled = apply_edits(src, src_len, &edits, polysneed_not_added);

            polysdone |= polysneeded;

            res.altered = 1;

            if (res.err)
            {
                const char *p = src + res.pos, *s = p, *e = p, *fe = src + src_len, *ple = NULL, *pls = NULL;
                rp_string *out = rp_string_new(64);
                while (s >= src && *s != '\n')
                    s--;
                if (*s == '\n')
                {
                    const char *bline = "";
                    ple = s;
                    pls = ple;
                    pls--;
                    while (pls >= src && *pls == '\n')
                        pls--, ple--;
                    if (ple != s)
                        bline = "\n...";
                    while (pls >= src && *pls != '\n')
                        pls--;
                    pls++;
                    rp_string_appendf(out, "%.*s%s\n", (int)(ple - pls), pls, bline);
                }
                s++;
                while (e <= fe && *e != '\n')
                    e++;
                rp_string_appendf(out, "%.*s\n", (int)(e - s), s);
                rp_string_appendf(out, "%*s", 1 + (p - s), "^");
                res.errmsg = rp_string_steal(out);
                out = rp_string_free(out);
            }
            else if (unresolved)
            {
                if (free_src)
                    free(free_src);

                free_src = res.transpiled;
                res.transpiled = NULL;

                free_edits(&edits);
                ts_tree_delete(tree);
                ts_parser_delete(parser);
                src_len = 0; // recalc on next pass
                continue;
            }
        }
        else
        {
            if (free_src)
                free(free_src);
            free_src = NULL;

            free_edits(&edits);
            ts_tree_delete(tree);
            ts_parser_delete(parser);

            return res;
        }
        if (free_src)
            free(free_src);
        free_src = NULL;

        free_edits(&edits);
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        break;
    }
    // printf("npasses=%d\n",npasses);
    return res;
}

RP_ParseRes transpile(const char *src, size_t src_len, int printTree)
{
    return transpile_code(src, src_len, printTree, 1, 0);
}

RP_ParseRes transpile_standalone(const char *src, size_t src_len, int printTree)
{
    return transpile_code(src, src_len, printTree, 0, 0);
}

RP_ParseRes transpile_eval(const char *src, size_t src_len, int printTree)
{
    return transpile_code(src, src_len, printTree, 1, 1);
}

char *stealParseRes(RP_ParseRes *res)
{
    char *ret = res->transpiled;
    res->transpiled = NULL;
    return ret;
}

void freeParseRes(RP_ParseRes *res)
{
    if (res->transpiled)
    {
        free(res->transpiled);
        res->transpiled = NULL;
    }
    if (res->errmsg)
    {
        free(res->errmsg);
        res->errmsg = NULL;
    }
}

#ifdef TEST
// ============== small IO utils ==============
static char *read_entire_file(const char *path, size_t *out_len)
{
    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "rb");
    long n;
    size_t r;
    char *buf = NULL;

    if (!f)
    {
        perror("open");
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    n = ftell(f);

    if (n < 0)
    {
        perror("ftell");
        exit(1);
    }

    fseek(f, 0, SEEK_SET);

    REMALLOC(buf, (size_t)n + 1);

    r = fread(buf, 1, (size_t)n, f);
    if (strcmp(path, "-") != 0)
        fclose(f);

    buf[r] = '\0';

    if (out_len)
        *out_len = r;

    return buf;
}
// ============== main ==============
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <path-to-js> [--printTree]\n       Use '-' to read from stdin.\n", argv[0]);
        return 2;
    }
    int printTree = (argc >= 3 && strcmp(argv[2], "--print-tree") == 0) ? 2 : 0;

    printTree = (argc >= 3 && strcmp(argv[2], "--printTree") == 0) ? 2 : 0;

    size_t src_len = 0;
    char *src = read_entire_file(argv[1], &src_len);

    RP_ParseRes res = transpile(src, src_len, printTree);
    if (res.transpiled)
        fwrite(res.transpiled, 1, strlen(res.transpiled), stdout);

    if (res.err)
    {
        if (res.err && res.transpiled)
        {
            char *p = src + res.pos;
            char *s = p, *e = p, *fe = src + src_len;

            fprintf(stderr, "Transpiler Parse Error (line %d)\n", res.line_num);
            while (s >= src && *s != '\n')
                s--;
            s++;
            while (e <= fe && *e != '\n')
                e++;
            fprintf(stderr, "%.*s\n", (int)(e - s), s);
            while (s < p)
            {
                fputc(' ', stderr);
                s++;
            }
            fputc('^', stderr);
            fputc('\n', stderr);
            free(res.transpiled);
            free(src);

            return (1);
        }
    }
    // Cleanup
    if (res.transpiled)
        free(res.transpiled);
    free(src);
    return 0;
}
#endif


