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

/* Uncomment to enable runtime TDZ + const-reassign checks for top-level
   let/const in each function. Disabled by default: the checks add an
   inline `(function(){throw …})()` at each statically-detected violation
   point, which is small but non-zero runtime cost and may surface
   pre-existing TDZ/const-reassign bugs in code that has been silently
   tolerated. Re-enable when cross-runtime parity with node is required. */
//#define TDZ_RUNTIME_CHECKS 1

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
    /* Same start: apply wider (larger-end) edit first. Needed for the
       fn-source rewriter where an [ns,ns] insert-prefix coexists with a
       [ns,ne] replace (arrow/async) — replace must land before the insert
       so the prefix ends up outside, not inside, the replacement. */
    if (ea->end < eb->end)
        return 1;
    if (ea->end > eb->end)
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
#define FN_SOURCE_PF (1<<9) // _TrN_Sp._fs + Function.prototype.toString override
#define REGEXP_U_PF  (1<<10) // RegExp constructor wrapper that strips `u` flag
#define BARE_REQ_PF  (1<<11) // _TrN_Sp._req — node_modules walk for bare-spec require()
#define JSON_REQ_PF  (1<<12) // _TrN_Sp._reqJson — read+JSON.parse for .json require()
#define DECORATORS_PF (1<<13) // _TrN_Sp._applyDecoratedDescriptor — TC39 Stage 1 legacy decorators

/* toggled from outside via transpile_set_fn_sources(); default on */
static int _tp_fn_sources = 1;
void transpile_set_fn_sources(int on) { _tp_fn_sources = on ? 1 : 0; }

/* Set to the current pass index by transpile_code(). */
static int _tp_pass_idx = 0;

/* On pass >= 1, src begins with the polyfill prefix prepended in pass 0. Any
   function whose range is inside this prefix is part of the polyfills and
   must NOT be wrapped by fn-source. transpile_code() detects the prefix end
   offset in src at the top of each pass and stores it here. */
static size_t _tp_polyfill_prefix_len = 0;


polyfills allpolys[] = {
    // stolen from babel.  Babel, like this prog is MIT licensed - see https://github.com/babel/babel/blob/main/LICENSE
    {
        "_TrN_Sp.__spreadO = function(target) {function ownKeys(object, enumerableOnly){var keys = Object.keys(object);if (Object.getOwnPropertySymbols){var symbols = Object.getOwnPropertySymbols(object);if (enumerableOnly)symbols = symbols.filter(function(sym) {return Object.getOwnPropertyDescriptor(object, sym).enumerable;});keys.push.apply(keys, symbols);}return keys;}function _defineProperty(obj, key, value){if (key in obj){Object.defineProperty(obj, key, {value : value, enumerable : true, configurable : true, writable : true});}else{obj[key] = value;}return obj;}for (var i = 1; i < arguments.length; i++){var source = arguments[i] != null ? arguments[i] : {};if (i % 2){ownKeys(Object(source), true).forEach(function(key) {_defineProperty(target, key, source[key]);});}else if (Object.getOwnPropertyDescriptors){Object.defineProperties(target, Object.getOwnPropertyDescriptors(source));}else{ownKeys(Object(source)).forEach(function(key) {Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(source, key));});}}return target;};_TrN_Sp.__spreadA = function(target, arr) {if (arr instanceof Array)return target.concat(arr);function _nonIterableSpread(){throw new TypeError(\"Invalid attempt to spread non-iterable instance. In order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");}function _unsupportedIterableToArray(o, minLen){if (!o)return;if (typeof o === \"string\")return _arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor);n = o.constructor.name;if (n === \"Map\" || n === \"Set\")return Array.from(o);if (n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n))return target.concat(_arrayLikeToArray(o, minLen));}function _iterableToArray(iter){if (typeof Symbol !== \"undefined\" && Symbol.iterator in Object(iter)){var _it=iter[Symbol.iterator](),_a=[],_s;while(!(_s=_it.next()).done)_a.push(_s.value);return target.concat(_a);}}function _arrayLikeToArray(arr, len){if (len == null || len > arr.length)len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++){arr2[i] = arr[i];}return target.concat(arr2);}function _arrayWithoutHoles(arr){if (Array.isArray(arr))return target.concat(_arrayLikeToArray(arr));}return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread();};_TrN_Sp._arrayConcat = function(items){var self = this;items.forEach(function(item) {self.push(item);});return this;};_TrN_Sp._newArray = function() {Object.defineProperty(Array.prototype, '_addchain', {value: _TrN_Sp._arrayConcat,writable: true,configurable: true,enumerable: false});Object.defineProperty(Array.prototype, '_concat', {value: Array.prototype._addchain,writable: true,configurable: true,enumerable: false});return [];};_TrN_Sp._objectAddchain = function(key, value) {if (typeof key == 'object'){Object.assign(this, key)}else{this[key] = value;}return this;};_TrN_Sp._newObject = function() {Object.defineProperty(Object.prototype, '_addchain', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});Object.defineProperty(Object.prototype, '_concat', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});return {};};",
        0, (uint32_t)SPREAD_PF },
    {
        "_TrN_Sp._typeof=function(obj) {\"@babel/helpers - typeof\";if (typeof Symbol === \"function\" && typeof Symbol.iterator === \"symbol\") {_TrN_Sp._typeof = function(obj) {return typeof obj;};} else {_TrN_Sp._typeof = function(obj) {return obj && typeof Symbol === \"function\" && obj.constructor === Symbol && obj !== Symbol.prototype ? \"symbol\" : typeof obj;};}return _TrN_Sp._typeof(obj);};_TrN_Sp._getRequireWildcardCache=function() {if (typeof WeakMap !== \"function\") return null;var cache = new WeakMap();_TrN_Sp._getRequireWildcardCache=function(){return cache;};return cache;};_TrN_Sp._interopRequireWildcard=function(obj) {if (obj && obj.__esModule) {return obj;}if (obj === null || _TrN_Sp._typeof(obj) !== \"object\" && typeof obj !== \"function\") {return { \"default\": obj };}var cache = _TrN_Sp._getRequireWildcardCache();if (cache && cache.has(obj)) {return cache.get(obj);}var newObj = {};var hasPropertyDescriptor = Object.defineProperty && Object.getOwnPropertyDescriptor;for (var key in obj) {if (Object.prototype.hasOwnProperty.call(obj, key)) {var desc = hasPropertyDescriptor ? Object.getOwnPropertyDescriptor(obj, key) : null;if (desc && (desc.get || desc.set)) {Object.defineProperty(newObj, key, desc);} else {newObj[key] = obj[key];}}}newObj[\"default\"] = obj;if (cache) {cache.set(obj, newObj);}return newObj;};_TrN_Sp._interopDefault=function(m){if(typeof m =='object' && m.__esModule){return m.default}return m;};",
        0, (uint32_t)IMPORT_PF },
    {
        "_TrN_Sp.typeof =function(obj) {'@babel/helpers - typeof';if (typeof Symbol === 'function' && typeof Symbol.iterator === 'symbol') {_typeof = function _typeof(obj) {return typeof obj;};} else {_typeof = function _typeof(obj) {return obj && typeof Symbol === 'function' &&obj.constructor === Symbol && obj !== Symbol.prototype ?'symbol' :typeof obj;};}return _typeof(obj);}; _TrN_Sp.inherits =function(subClass, superClass) {if (typeof superClass !== 'function' && superClass !== null) {throw new TypeError('Super expression must either be null or a function');}subClass.prototype = Object.create(superClass && superClass.prototype,{constructor: {value: subClass, writable: true, configurable: true}});if (superClass) _TrN_Sp.setPrototypeOf(subClass, superClass);}; _TrN_Sp.setPrototypeOf =function(o, p) {_setPrototypeOf = Object.setPrototypeOf || function _setPrototypeOf(o, p) {o.__proto__ = p;return o;};return _setPrototypeOf(o, p);}; _TrN_Sp.createSuper =function(Derived) {var hasNativeReflectConstruct = _TrN_Sp.isNativeReflectConstruct();return function _createSuperInternal() {var Super = _TrN_Sp.getPrototypeOf(Derived), result;result = Super.apply(this, arguments);return _TrN_Sp.possibleConstructorReturn(this, result);};}; _TrN_Sp.possibleConstructorReturn =function(self, call) {if (call && (typeof call === 'object' || typeof call === 'function')) {return call;}return _TrN_Sp.assertThisInitialized(self);}; _TrN_Sp.assertThisInitialized =function(self) {if (self === void 0) {throw new ReferenceError('this hasn\\'t been initialised - super() hasn\\'t been called');}return self;}; _TrN_Sp.isNativeReflectConstruct =function() {if (typeof Reflect === 'undefined' || !Reflect.construct) return false;if (Reflect.construct.sham) return false;if (typeof Proxy === 'function') return true;try {Date.prototype.toString.call(Reflect.construct(Date, [], function() {}));return true;} catch (e) {return false;}}; _TrN_Sp.getPrototypeOf =function(o) {_getPrototypeOf = Object.setPrototypeOf ?Object.getPrototypeOf :function _getPrototypeOf(o) {return o.__proto__ || Object.getPrototypeOf(o);};return _getPrototypeOf(o);}; _TrN_Sp.classCallCheck =function(instance, Constructor) {if (!(instance instanceof Constructor)) {throw new TypeError('Cannot call a class as a function');}}; _TrN_Sp.defineProperties =function(target, props) {for (var i = 0; i < props.length; i++) {var descriptor = props[i];if (descriptor.src && _TrN_Sp._fs) {if (typeof descriptor.value === 'function') _TrN_Sp._fs(descriptor.value, descriptor.src);if (typeof descriptor.get === 'function') _TrN_Sp._fs(descriptor.get, descriptor.src);if (typeof descriptor.set === 'function') _TrN_Sp._fs(descriptor.set, descriptor.src);delete descriptor.src;}descriptor.enumerable = descriptor.enumerable || false;descriptor.configurable = true;if ('value' in descriptor) descriptor.writable = true;Object.defineProperty(target, descriptor.key, descriptor);}}; _TrN_Sp.createClass =function(Constructor, A, B, swapOrder) {var protoProps = swapOrder ? B : A;var staticProps = swapOrder ? A : B;if (protoProps) _TrN_Sp.defineProperties(Constructor.prototype, protoProps);if (staticProps) _TrN_Sp.defineProperties(Constructor, staticProps);return Constructor;}; _TrN_Sp._superGet =function(target, prop, recv) {while (target) {var desc = Object.getOwnPropertyDescriptor(target, prop);if (desc) {if (desc.get) return desc.get.call(recv);return desc.value;}target = Object.getPrototypeOf(target);}return undefined;};",
        0, (uint32_t)CLASS_PF  },
    {
        "_TrN_Sp.slicedToArray=function (arr, i) {return _TrN_Sp.arrayWithHoles(arr) || _TrN_Sp.iterableToArrayLimit(arr, i) || _TrN_Sp.unsupportedIterableToArray(arr, i) || _TrN_Sp.nonIterableRest();};_TrN_Sp.nonIterableRest=function(){throw new TypeError(\"Invalid attempt to destructure non-iterable instance.\\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");};_TrN_Sp.unsupportedIterableToArray=function(o, minLen) {if (!o) return;if (typeof o === \"string\") return _TrN_Sp.arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor) n = o.constructor.name;if (n === \"Map\" || n === \"Set\") return Array.from(o);if (n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n)) return _TrN_Sp.arrayLikeToArray(o, minLen);};_TrN_Sp.arrayLikeToArray=function(arr, len) {if (len == null || len > arr.length) len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++) {arr2[i] = arr[i];}return arr2;};_TrN_Sp.iterableToArrayLimit=function(arr, i){if (typeof Symbol === \"undefined\" || !(Symbol.iterator in Object(arr))) return;var _arr = [];var _n = true;var _d = false;var _e = undefined;try {for (var _i = arr[Symbol.iterator](), _s; !(_n = (_s = _i.next()).done); _n = true) {_arr.push(_s.value);if (i && _arr.length === i) break;}} catch (err) {_d = true;_e = err;} finally {try {if (!_n && _i[\"return\"] != null) _i[\"return\"]();} finally {if (_d) throw _e;}}return _arr;};_TrN_Sp.arrayWithHoles=function(arr) {if (Array.isArray(arr)) return arr;};",
        0, (uint32_t)FOROF_PF  },
    {
        // Debug controls: pending warnings (ON by default, turn off with _TrN_Sp.warnOnLongPending=false)
        // do not warn for merely pending promises.
        // Enable only for debugging "long-pending" promises.
        "_TrN_Sp._wrapLongPending = function(p, label) {if (!_TrN_Sp.warnOnLongPending) return p;try {var id = setTimeout(function () {if (typeof console !== 'undefined' && console && console.warn) {console.warn('Promise still pending after', _TrN_Sp.pendingWarnMs, 'ms', label ? '(' + label + ')' : '');}}, _TrN_Sp.pendingWarnMs);if (p && typeof p['finally'] === 'function') {p['finally'](function () { clearTimeout(id); });} else if (p && typeof p.then === 'function') {p.then(function(){ clearTimeout(id); }, function(){ clearTimeout(id); });}} catch (_e) {}return p;};_TrN_Sp.asyncGeneratorStep = function(gen, resolve, reject, _next, _throw, key, arg) {try {var info = gen[key](arg);var value = info.value;} catch (error) {reject(error);return;}if (info.done) {resolve(value);} else {Promise.resolve(value).then(_next, _throw);}};_TrN_Sp.asyncToGenerator = function(fn) {return function() {var self = this, args = arguments;var __p = new Promise(function(resolve, reject) {var gen = fn.apply(self, args);function _next(value) {_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, 'next', value);}function _throw(err) {_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, 'throw', err);}_next(undefined);});return _TrN_Sp._wrapLongPending(__p, fn && fn.name ? fn.name : 'async');};_TrN_Sp.warnOnLongPending = (_TrN_Sp.warnOnLongPending === undefined ? true: _TrN_Sp.warnOnLongPending);_TrN_Sp.pendingWarnMs = (typeof _TrN_Sp.pendingWarnMs === 'number' && _TrN_Sp.pendingWarnMs >= 0) ? _TrN_Sp.pendingWarnMs : 2000;};_TrN_Sp.regeneratorRuntime = (function() {function mark(genFn) {return genFn;}function wrap(innerFn, outerFn, outerThis) {var _s=void 0,_t=false,_te;var context = {prev: 0,next: 0,done: false,rval: void 0,_ts:null,stop: function() {this.done = true;return this.rval;}};Object.defineProperty(context,'sent',{get:function(){if(_t){_t=false;var e=_te;_te=void 0;throw e;}return _s;},set:function(v){_s=v;},configurable:true});var _iter={next: function(arg) {if (context.done) return {value: undefined, done: true};context.sent = arg;context._y=false;var value;while(true){try{value = innerFn.call(outerThis, context);break;}catch(_e){if(context._catch){context._caught=_e;context.next=context._catch;context._catch=0;continue;}context.done=true;throw _e;}}if (context.done || context.next === 'end') {return {value: context.rval, done: true};}if (!context._y) {context.done = true;context.rval = value;return {value: context.rval, done: true};}return {value: value, done: false};},'throw': function(err) {_t=true;_te=err;return this.next(err);},'return': function(v) {if (context.done) return {value: v, done: true};if (context._ts && context._ts.length) {context.rval = v;context._rret = true;context.next = context._ts[context._ts.length - 1];return this.next();}context.done = true;return {value: v, done: true};}};if(typeof Symbol!=='undefined'&&Symbol.iterator)_iter[Symbol.iterator]=function(){return this;};return _iter;}return {mark: mark, wrap: wrap};})();if(typeof Symbol!=='undefined'&&Symbol.asyncIterator===undefined){try{Symbol.asyncIterator=Symbol('Symbol.asyncIterator');}catch(_e){}}_TrN_Sp._iter=function(x){if(x==null)throw new TypeError('not iterable');if(typeof x.next==='function')return x;if(typeof Symbol!=='undefined'&&Symbol.iterator&&typeof x[Symbol.iterator]==='function')return x[Symbol.iterator]();var i=0;return{next:function(){if(i>=x.length)return{value:undefined,done:true};return{value:x[i++],done:false};}};};_TrN_Sp._asyncIter=function(x){if(x==null)throw new TypeError('not iterable');if(typeof Symbol!=='undefined'&&Symbol.asyncIterator&&typeof x[Symbol.asyncIterator]==='function')return x[Symbol.asyncIterator]();return _TrN_Sp._iter(x);};_TrN_Sp.__await=function(v){return{__await:true,value:v};};_TrN_Sp.__asyncGenerator=function(thisArg,_args,fn){if(!fn||typeof fn.apply!=='function')throw new TypeError('async generator body must be a function');var iter=fn.apply(thisArg,_args||[]);var q=[],running=false;function step(){if(q.length===0){running=false;return;}running=true;var cur=q[0];var r;try{r=iter[cur.key](cur.arg);}catch(e){q.shift();cur.reject(e);return step();}if(r.done){q.shift();cur.resolve({value:r.value,done:true});return step();}var v=r.value;if(v&&v.__await){Promise.resolve(v.value).then(function(rv){q[0]={key:'next',arg:rv,resolve:cur.resolve,reject:cur.reject};step();},function(re){q[0]={key:'throw',arg:re,resolve:cur.resolve,reject:cur.reject};step();});}else{Promise.resolve(v).then(function(rv){q.shift();cur.resolve({value:rv,done:false});step();},function(re){q.shift();cur.reject(re);step();});}}function enqueue(key,arg){return new Promise(function(resolve,reject){q.push({key:key,arg:arg,resolve:resolve,reject:reject});if(!running)step();});}var aiter={next:function(v){return enqueue('next',v);},'throw':function(e){return enqueue('throw',e);},'return':function(v){return enqueue('return',v);}};if(typeof Symbol!=='undefined'&&Symbol.asyncIterator)aiter[Symbol.asyncIterator]=function(){return this;};return aiter;};",
        // old overly verbose version:
        //"_TrN_Sp.asyncGeneratorStep = function(gen, resolve, reject, _next, _throw, key, arg) {try{var info = gen[key](arg);var value = info.value;}catch (error){reject(error);return;}if (info.done){resolve(value);}else{Promise.resolve(value).then(_next, _throw);}};_TrN_Sp.asyncToGenerator = function(fn) {return function() {var self = this, args = arguments;return new Promise(function(resolve, reject) {var gen = fn.apply(self, args);function _next(value){_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, \"next\", value);}function _throw(err){_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, \"throw\", err);}_next(undefined);});};};_TrN_Sp.regeneratorRuntime = (function () {function mark(genFn) { return genFn; }function wrap(innerFn) {var context = {prev: 0,next: 0,sent: void 0,done: false,rval: void 0,stop: function () { this.done = true; return this.rval; }};return {next: function (arg) {var prevNext = context.next;context.sent = arg;var value = innerFn(context);if (context.done || context.next === \"end\") {return { value: context.rval, done: true };}if (context.next === prevNext) {context.done = true;context.rval = value;return { value: context.rval, done: true };}return { value: value, done: false };},throw: function (err) { throw err; }};}return { mark: mark, wrap: wrap };})();",
        0, (uint32_t)ASYNC_PF  },

    // from https://www.npmjs.com/package/promise-polyfill MIT license included in this dir.
    // the delete global.Promise is for rampart.thread reload.
    {
        // set rampart.warnUnhandledPromise=false to silence the
        // "Possible Unhandled Promise Rejection" console warning.
        "delete global.Promise;(function(e, t) {'object' == typeof exports && 'undefined' != typeof module ? t() :'function' == typeof define && define.amd              ? define(t) :t()})(0, function() {'use strict';function e(e) {var t = this.constructor;return this.then(function(n) {return t.resolve(e()).then(function() {return n})},function(n) {return t.resolve(e()).then(function() {return t.reject(n)})})}function t(e) {return new this(function(t, n) {function r(e, n) {if (n && ('object' == typeof n || 'function' == typeof n)) {var f = n.then;if ('function' == typeof f)return void f.call(n,function(t) {r(e, t)},function(n) {o[e] = {status: 'rejected', reason: n}, 0 == --i && t(o)})}o[e] = {status: 'fulfilled', value: n}, 0 == --i && t(o)}if (!e || 'undefined' == typeof e.length)return n(new TypeError(typeof e + ' ' + e +' is not iterable(cannot read property Symbol(Symbol.iterator))'));var o = Array.prototype.slice.call(e);if (0 === o.length) return t([]);for (var i = o.length, f = 0; o.length > f; f++) r(f, o[f])})}function n(e, t) {this.name = 'AggregateError', this.errors = e, this.message = t || ''}function r(e) {var t = this;return new t(function(r, o) {if (!e || 'undefined' == typeof e.length)return o(new TypeError('Promise.any accepts an array'));var i = Array.prototype.slice.call(e);if (0 === i.length) return o(new n([],'All promises were rejected'));for (var f = [], u = 0; i.length > u; u++) try {t.resolve(i[u]).then(r)['catch'](function(e) {f.push(e),f.length === i.length && o(new n(f, 'All promises were rejected'))})} catch (c) {o(c)}})}function o(e) {return !(!e || 'undefined' == typeof e.length)}function i() {}function f(e) {if (!(this instanceof f))throw new TypeError('Promises must be constructed via new');if ('function' != typeof e) throw new TypeError('not a function');this._state = 0, this._handled = !1, this._value = undefined,this._deferreds = [], s(e, this)}function u(e, t) {for (; 3 === e._state;) e = e._value;0 !== e._state ? (e._handled = !0, f._immediateFn(function() {var n = 1 === e._state ? t.onFulfilled : t.onRejected;if (null !== n) {var r;try {r = n(e._value)} catch (o) {return void a(t.promise, o)}c(t.promise, r)} else(1 === e._state ? c : a)(t.promise, e._value)})) :e._deferreds.push(t)}function c(e, t) {try {if (t === e)throw new TypeError('A promise cannot be resolved with itself.');if (t && ('object' == typeof t || 'function' == typeof t)) {var n = t.then;if (t instanceof f) return e._state = 3, e._value = t, void l(e);if ('function' == typeof n)return void s(function(e, t) {return function() {e.apply(t, arguments)}}(n, t), e)}e._state = 1, e._value = t, l(e)} catch (r) {a(e, r)}}function a(e, t) {e._state = 2, e._value = t, l(e)}function l(e) {2 === e._state && 0 === e._deferreds.length && f._immediateFn(function() {e._handled || f._unhandledRejectionFn(e._value)});for (var t = 0, n = e._deferreds.length; n > t; t++) u(e, e._deferreds[t]);e._deferreds = null}function s(e, t) {var n = !1;try {e(function(e) {n || (n = !0, c(t, e))},function(e) {n || (n = !0, a(t, e))})} catch (r) {if (n) return;n = !0, a(t, r)}}n.prototype = Error.prototype;var d = setTimeout;f.prototype['catch'] = function(e) {return this.then(null, e)}, f.prototype.then = function(e, t) {var n = new this.constructor(i);return u(this, new function(e, t, n) {this.onFulfilled = 'function' == typeof e ? e : null,this.onRejected = 'function' == typeof t ? t : null, this.promise = n}(e, t, n)), n}, f.prototype['finally'] = e, f.all = function(e) {return new f(function(t, n) {function r(e, o) {try {if (o && ('object' == typeof o || 'function' == typeof o)) {var u = o.then;if ('function' == typeof u)return void u.call(o, function(t) {r(e, t)}, n)}i[e] = o, 0 == --f && t(i)} catch (c) {n(c)}}if (!o(e)) return n(new TypeError('Promise.all accepts an array'));var i = Array.prototype.slice.call(e);if (0 === i.length) return t([]);for (var f = i.length, u = 0; i.length > u; u++) r(u, i[u])})}, f.any = r, f.allSettled = t, f.resolve = function(e) {return e && 'object' == typeof e && e.constructor === f ? e :new f(function(t) {t(e)})}, f.reject = function(e) {return new f(function(t, n) {n(e)})}, f.race = function(e) {return new f(function(t, n) {if (!o(e)) return n(new TypeError('Promise.race accepts an array'));for (var r = 0, i = e.length; i > r; r++) f.resolve(e[r]).then(t, n)})}, f._immediateFn = (function(){var q=[],s=false;function fl(){var c=q;q=[];s=false;for(var j=0;j<c.length;j++)c[j]();}return function(e){q.push(e);if(!s){s=true;d(fl,0);}};})(), f._unhandledRejectionFn = function(e) {if (typeof console === 'undefined' || !console) return;var warn = (typeof rampart === 'undefined') ? true : (rampart.warnUnhandledPromise !== false);if (warn) console.warn('Possible Unhandled Promise Rejection:', e);};var p = function() {if ('undefined' != typeof self) return self;if ('undefined' != typeof window) return window;if ('undefined' != typeof global) return global;throw Error('unable to locate global object')}();'function' != typeof p.Promise ?p.Promise = f :(p.Promise.prototype['finally'] || (p.Promise.prototype['finally'] = e),p.Promise.allSettled || (p.Promise.allSettled = t),p.Promise.any || (p.Promise.any = r))});_TrN_Sp._pAS=Promise.allSettled;_TrN_Sp._pAn=Promise.any;_TrN_Sp._pF=Promise.prototype['finally'];_TrN_Sp._pP=function(){if(typeof Promise==='function'){if(!Promise.allSettled&&_TrN_Sp._pAS)Promise.allSettled=_TrN_Sp._pAS;if(!Promise.any&&_TrN_Sp._pAn)Promise.any=_TrN_Sp._pAn;if(Promise.prototype&&!Promise.prototype['finally']&&_TrN_Sp._pF)Promise.prototype['finally']=_TrN_Sp._pF;}};",
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
    {
        /* Attach original pre-transpile source to functions and make toString
           return it when present. Note: duktape puts toString/call/apply/bind
           on an internal ecmascript/native function prototype (the actual
           `Object.getPrototypeOf(fn)`), NOT on Function.prototype — so we
           override there. Native/bound functions without __source__ fall
           through to the built-in. */
        "_TrN_Sp._fs=function(fn,src){if(typeof fn==='function'){try{Object.defineProperty(fn,'__source__',{value:src,configurable:true,writable:false,enumerable:false});}catch(e){}}return fn;};"
        "if(!_TrN_Sp._origToString){var _fnp=Object.getPrototypeOf(function(){});_TrN_Sp._origToString=_fnp.toString;_fnp.toString=function(){if(this&&typeof this.__source__==='string')return this.__source__;return _TrN_Sp._origToString.call(this);};}",
        0, (uint32_t)FN_SOURCE_PF },
    {
        /* Wrap RegExp so `new RegExp(pat, 'gu')` / `RegExp(pat, 'u')` strip
           the `u` flag — duktape doesn't accept it. Same hook covers
           library code that builds regex flags dynamically (marked's
           `edit()` helper passes 'gu' through as a parameter). */
        "if(!_TrN_Sp._origRegExp){_TrN_Sp._origRegExp=RegExp;global.RegExp=function(p,f){if(typeof f==='string'&&f.indexOf('u')>=0)f=f.replace(/u/g,'');return this instanceof _TrN_Sp._origRegExp?new _TrN_Sp._origRegExp(p,f):_TrN_Sp._origRegExp(p,f);};global.RegExp.prototype=_TrN_Sp._origRegExp.prototype;}",
        0, (uint32_t)REGEXP_U_PF },
    {
        /* Bare-specifier require: walk up from the calling module's
           directory looking for node_modules/<spec>/. Read package.json
           for "main", fall back to index.js. If nothing resolves, fall
           through to rampart's native require (which checks
           process.modulesPath). The cache (_c) memoises spec→absPath
           lookups so repeated require()s of the same spec are O(1). */
        "_TrN_Sp._req=function(m,s){var c=_TrN_Sp._req._c||(_TrN_Sp._req._c={});var d=(m&&m.path)?m.path:(typeof process!=='undefined'&&process.scriptPath?process.scriptPath:'');var k=d+'|'+s;if(k in c)return require(c[k]);var st=rampart.utils.stat,rf=rampart.utils.readFile;var exts=['','.js','.cjs','.mjs','.json'];while(d&&d.length>1){var pd=d+'/node_modules/'+s;var sp=st(pd);if(sp&&sp.isFile){c[k]=pd;return require(pd);}if(sp&&sp.isDirectory){var pj=pd+'/package.json';var mn=null;if(st(pj)){try{var meta=JSON.parse(rf(pj,{returnString:true}));mn=meta.main||'index.js';}catch(e){mn='index.js';}var p=pd+'/'+mn;if(st(p)){c[k]=p;return require(p);}}var idx=pd+'/index.js';if(st(idx)){c[k]=idx;return require(idx);}var idxc=pd+'/index.cjs';if(st(idxc)){c[k]=idxc;return require(idxc);}}else{for(var ei=1;ei<exts.length;ei++){var pde=pd+exts[ei];if(st(pde)){c[k]=pde;return require(pde);}}}var n=d.lastIndexOf('/');if(n<=0)break;d=d.substring(0,n);}return require(s);};",
        0, (uint32_t)BARE_REQ_PF },
    {
        /* JSON require: rampart's loader treats every spec as JS, so
           `require('./foo.json')` parses the JSON file as JavaScript and
           fails. Resolve the path (relative against module.path, or via
           node_modules walk for bare specs), then JSON.parse the file.
           Results are memoised in _c. */
        "_TrN_Sp._reqJson=function(m,s){var c=_TrN_Sp._reqJson._c||(_TrN_Sp._reqJson._c={});var base=(m&&m.path)?m.path:(typeof process!=='undefined'&&process.scriptPath?process.scriptPath:'');var k=base+'|'+s;if(k in c)return c[k];var st=rampart.utils.stat,rf=rampart.utils.readFile;var p=null;if(s.charAt(0)==='/'){p=s;}else if(s.charAt(0)==='.'){var t=s,b=base;while(t.indexOf('../')===0){t=t.substring(3);var n=b.lastIndexOf('/');if(n<=0)break;b=b.substring(0,n);}if(t.indexOf('./')===0)t=t.substring(2);p=b+'/'+t;}else{var d=base;while(d&&d.length>1){var cand=d+'/node_modules/'+s;if(st(cand)){p=cand;break;}var n=d.lastIndexOf('/');if(n<=0)break;d=d.substring(0,n);}}if(!p||!st(p))throw new Error('JSON module not found: '+s);var v=JSON.parse(rf(p,{returnString:true}));c[k]=v;return v;};",
        0, (uint32_t)JSON_REQ_PF },
    {
        /* TC39 Stage 1 / TypeScript experimentalDecorators / babel legacy
           decorator runtime.  Applies a list of decorators to a method,
           accessor, or field descriptor, in reverse declaration order
           (the spec's evaluation order).  `desc` may be undefined for
           field decorators that have no existing prototype descriptor;
           we synthesize a default { value: undefined } slot for the
           decorators to mutate.  If any decorator returns a non-falsy
           replacement descriptor, that overrides.  Final result is
           installed via Object.defineProperty; for fields with
           initializers, the initializer is invoked with `ctx` as
           `this`. */
        "_TrN_Sp._applyDecoratedDescriptor=function(target,key,decorators,desc,ctx){var d;if(desc){d={};Object.keys(desc).forEach(function(k){d[k]=desc[k];});d.enumerable=!!d.enumerable;d.configurable=!!d.configurable;if('value' in d||d.initializer)d.writable=true;}else{d={enumerable:true,configurable:true,writable:true,value:undefined};}d=decorators.slice().reverse().reduce(function(dd,dec){return dec(target,key,dd)||dd;},d);if(ctx&&d.initializer!==void 0){d.value=d.initializer?d.initializer.call(ctx):void 0;d.initializer=undefined;}if(d.initializer===void 0){Object.defineProperty(target,key,d);d=null;}return d;};",
        0, (uint32_t)DECORATORS_PF },
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
    /* Reserve one extra byte so we can always append a trailing
       newline. Duktape's parser fails with "end of input" when the
       file ends mid-`//` comment with no terminating newline; appending
       `\n` unconditionally avoids that. */
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

    /* Ensure the final output ends with a newline.  Duktape's parser
       rejects files that end inside a `//` line comment with no
       trailing newline ("parse error, end of input").  Source maps
       routinely emit `//# sourceMappingURL=...` as the last line
       without a terminating newline, so without this every transpiled
       file that started with one would fail to load. */
    if (out_len == 0 || out[out_len - 1] != '\n')
    {
        out[out_len++] = '\n';
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

/* Check whether a yield_expression is inside a context our lowering
   actually fails on.  The MVP yield-in-loop lowering covers
   while/do-while/for/for-of plus break/continue and try-finally; this
   function returns 1 only for cases that remain broken so the warning
   stops firing on patterns that actually work. */
static int yield_in_unsupported_context(const char *src, TSNode node)
{
    (void)src;
    (void)node;
    /* All known yield-in-context patterns now have structural lowerings:
         - while/for/do-while/for-of/for-in body
         - if/else branches
         - try/catch/finally (single-level catch with re-throw works,
           nested re-throw is partial)
         - switch with non-fall-through cases
         - break/continue (labelled and unlabelled)
         - return inside try-finally
       Patterns NOT yet covered (rare): switch with fall-through, nested
       try with re-throw from inner catch, async-iterator for-of over
       generator/Set/Map.  These no longer warn pre-emptively — users
       hit clear runtime errors when they trigger. */
    return 0;
}

/* Walk the AST and emit warnings for unsupported patterns.
   This is a read-only scan — no edits, no claimed ranges. */
/* Scan AST for unsupported patterns and warn on stderr (read-only — no edits).
   Does not stop transpilation — the script still runs. */
static void warn_unsupported_patterns(const char *src, TSNode root)
{
    TSTreeCursor cur = ts_tree_cursor_new(root);
    for (;;)
    {
        TSNode n = ts_tree_cursor_current_node(&cur);
        const char *nt = ts_node_type(n);

        if (strcmp(nt, "await_expression") == 0)
        {
            /* await inside loops is now handled by the unified yield/await
               state-machine lowering (_emit_yield_body collects both kinds
               and emits per-iteration cases) — no warning needed.
               Destructuring with await also handled via wrap_paren. */
        }
        else if (strcmp(nt, "yield_expression") == 0)
        {
            /* `yield` in loops works for the common cases (while/for/
               do-while/for-of, with break/continue/return, including
               try-finally).  Only warn for the remaining gaps:
               for-in over object keys, yield in catch handlers, and
               labelled break/continue inside a yield-loop. */
            if (yield_in_unsupported_context(src, n))
            {
                TSPoint p = ts_node_start_point(n);
                fprintf(stderr, "Transpiler warning (line %u): 'yield' in this context "
                        "(for-in / catch handler / labelled loop) is not supported by the "
                        "state-machine lowering. Restructure or use a manual iteration pattern.\n",
                        p.row + 1);
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

    uint32_t pi = 0;
    for (uint32_t i = 0; i < c; i++)
    {
        TSNode p = ts_node_named_child(params, i);
        const char *pt = ts_node_type(p);
        /* Skip comment children — they aren't params and don't count
           toward the argument index. */
        if (strcmp(pt, "comment") == 0)
            continue;
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
        pi++;
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
    {
        free(decls);  /* not handed to add_edit_take_ownership yet */
        return 1;
    }

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
    {
        free(decls);  /* not handed to add_edit_take_ownership yet */
        return 1;
    }

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

            /* Always emit a literal piece between subs (and before the first
               sub) — even if empty. Tagged-template invocation requires the
               invariant `nlits = nexprs + 1`; with `${a}${b}` the parts
               array must be `["", "", ""]`, not `[]`. Without this we'd
               drop the leading/trailing/between-empty literals and the
               tag would see a shorter array than it expects. */
            if (*nl == capL)
            {
                capL = capL ? capL * 2 : 8;
                REMALLOC(*lits, capL * sizeof(Piece));
            }
            (*lits)[(*nl)++] = (Piece){0, cursor, sub_s};
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
    /* Trailing literal — always emit, even if empty, so that
       `nlits == nexprs + 1`. The earlier `if (close_tick > cursor)`
       guard was wrong: a template ending with `${expr}` (close_tick
       == cursor) still needs a final empty string in the parts array. */
    if (*nl == capL)
    {
        capL = capL ? capL * 2 : 8;
        REMALLOC(*lits, capL * sizeof(Piece));
    }
    (*lits)[(*nl)++] = (Piece){0, cursor, close_tick};
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
            /* Comments inside an array pattern appear as named `comment`
               children. Skip them — they aren't bindings. */
            if (strcmp(kt, "comment") == 0)
                continue;
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
            /* Comments inside the pattern appear as named `comment` children.
               Skip them — they aren't bindings. */
            if (strcmp(kt, "comment") == 0)
                continue;
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
    if (strcmp(ts_node_type(node), "variable_declaration") != 0 &&
        strcmp(ts_node_type(node), "lexical_declaration") != 0)
        return 0;

    // Scan ALL declarators first.  If any uses destructuring, we
    // expand every declarator in order — otherwise we'd drop the
    // sibling declarators when we replace the whole statement
    // (e.g. `const x = 1, [a,b] = y, z = 2` used to silently lose
    // `x` and `z`).
    uint32_t dc = ts_node_named_child_count(node);
    int any_destr = 0;
    for (uint32_t di = 0; di < dc && !any_destr; di++) {
        TSNode decl = ts_node_named_child(node, di);
        if (strcmp(ts_node_type(decl), "variable_declarator") != 0) continue;
        TSNode name = ts_node_child_by_field_name(decl, "name", 4);
        if (ts_node_is_null(name)) continue;
        const char *nt = ts_node_type(name);
        if (strcmp(nt, "array_pattern") == 0 || strcmp(nt, "object_pattern") == 0)
            any_destr = 1;
    }
    if (!any_destr) return 0;
    if (overlaps) return 1;

    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);
    rp_string *out = rp_string_new(256);

    for (uint32_t di = 0; di < dc; di++)
    {
        TSNode decl = ts_node_named_child(node, di);
        if (strcmp(ts_node_type(decl), "variable_declarator") != 0)
            continue;
        TSNode name = ts_node_child_by_field_name(decl, "name", 4);
        TSNode val  = ts_node_child_by_field_name(decl, "value", 5);
        if (ts_node_is_null(name))
            continue;
        const char *nt = ts_node_type(name);

        if (strcmp(nt, "array_pattern") == 0 || strcmp(nt, "object_pattern") == 0) {
            if (ts_node_is_null(val)) continue;
            char tmpvar[32];
            snprintf(tmpvar, sizeof(tmpvar), "_TrN_d%u", ++_destr_counter);
            Bindings binds;
            binds_init(&binds);
            if (!collect_flat_destructure_bindings(name, src, tmpvar, &binds)) {
                binds_free(&binds);
                /* Bail out: leave original code intact rather than
                   produce a malformed mix. */
                out = rp_string_free(out);
                return 0;
            }
            size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
            rp_string_puts(out, "var ");
            rp_string_puts(out, tmpvar);
            rp_string_puts(out, " = ");
            rp_string_putsn(out, src + vs, ve - vs);
            rp_string_puts(out, "; ");
            for (size_t i = 0; i < binds.len; i++) {
                rp_string_puts(out, "var ");
                rp_string_puts(out, binds.a[i].name);
                rp_string_puts(out, " = ");
                if (binds.a[i].defval) {
                    rp_string_puts(out, binds.a[i].repl);
                    rp_string_puts(out, " !== undefined ? ");
                    rp_string_puts(out, binds.a[i].repl);
                    rp_string_puts(out, " : ");
                    rp_string_puts(out, binds.a[i].defval);
                } else {
                    rp_string_puts(out, binds.a[i].repl);
                }
                rp_string_puts(out, "; ");
            }
            binds_free(&binds);
        } else {
            /* Plain `name = expr` (or `name` with no initializer). */
            size_t nms = ts_node_start_byte(name), nme = ts_node_end_byte(name);
            rp_string_puts(out, "var ");
            rp_string_putsn(out, src + nms, nme - nms);
            if (!ts_node_is_null(val)) {
                size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
                rp_string_puts(out, " = ");
                rp_string_putsn(out, src + vs, ve - vs);
            }
            rp_string_puts(out, "; ");
        }
    }

    add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
    out = rp_string_free(out);
    return 1;
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
    snprintf(tmpvar, sizeof(tmpvar), "_TrN_d%u", ++_destr_counter);

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

/* Forward decls: defined in the import-rewrite section further down.
   `_imp_modimp_counter` is process-static within this file; we read it
   from the export rewriter to compute the modImp index of an already-
   processed named-import. */
static TSNode _imp_find_program(TSNode n);
static uint32_t _imp_modimp_counter;

/* Walk the program for an import_statement whose named_imports
   contains a binding matching `local` (as alias when present, else
   as remote name).  The export rewriter fires AFTER the import
   rewriter on the same dispatch pass, so the modImp counter has
   already been incremented for every named-imports statement that
   precedes our export in source order.  We count those preceding
   imports (K) and compute the matched import's index as
   `_imp_modimp_counter - (K_before_export - K_within)`.

   Sets *out_idx to the matched import's modImp index and
   *out_remote_start / *out_remote_end to the REMOTE name source
   bytes.  Returns 1 on match, 0 otherwise. */
static int _exp_find_import_binding(TSNode program, TSNode export_node, const char *src,
                                    const char *binding, size_t binding_len,
                                    uint32_t *out_idx,
                                    size_t *out_remote_start,
                                    size_t *out_remote_end)
{
    if (ts_node_is_null(program)) return 0;
    size_t exp_start = ts_node_start_byte(export_node);
    uint32_t cn = ts_node_named_child_count(program);
    /* Two-pass: first count named-imports statements that precede our
       export, then on a second walk find the matching binding and
       compute its absolute modImp index. */
    uint32_t k_before_export = 0;
    int matched = 0;
    uint32_t matched_k = 0;
    size_t matched_rs = 0, matched_re = 0;
    for (uint32_t i = 0; i < cn; i++)
    {
        TSNode stmt = ts_node_named_child(program, i);
        if (strcmp(ts_node_type(stmt), "import_statement") != 0) continue;
        TSNode named = {{0}};
        uint32_t sc = ts_node_named_child_count(stmt);
        for (uint32_t j = 0; j < sc && ts_node_is_null(named); j++)
        {
            TSNode c = ts_node_named_child(stmt, j);
            if (strcmp(ts_node_type(c), "import_clause") == 0)
            {
                uint32_t cc = ts_node_named_child_count(c);
                for (uint32_t k = 0; k < cc; k++)
                {
                    TSNode cc2 = ts_node_named_child(c, k);
                    if (strcmp(ts_node_type(cc2), "named_imports") == 0) { named = cc2; break; }
                }
            }
            else if (strcmp(ts_node_type(c), "named_imports") == 0)
            {
                named = c;
            }
        }
        if (ts_node_is_null(named)) continue;
        if (ts_node_start_byte(stmt) >= exp_start) break;
        /* This statement was processed by do_named_imports — its
           index among preceding statements is k_before_export. */
        if (!matched)
        {
            uint32_t k = ts_node_named_child_count(named);
            for (uint32_t kk = 0; kk < k; kk++)
            {
                TSNode spec = ts_node_named_child(named, kk);
                if (strcmp(ts_node_type(spec), "import_specifier") != 0) continue;
                TSNode local = ts_node_child_by_field_name(spec, "name", 4);
                TSNode alias = ts_node_child_by_field_name(spec, "alias", 5);
                const char *bind_t = NULL; size_t bind_l = 0;
                size_t rem_s = 0, rem_e = 0;
                if (!ts_node_is_null(local))
                {
                    rem_s = ts_node_start_byte(local);
                    rem_e = ts_node_end_byte(local);
                    if (!ts_node_is_null(alias))
                    {
                        bind_t = src + ts_node_start_byte(alias);
                        bind_l = ts_node_end_byte(alias) - ts_node_start_byte(alias);
                    }
                    else
                    {
                        bind_t = src + rem_s;
                        bind_l = rem_e - rem_s;
                    }
                }
                if (bind_l == binding_len && bind_t && memcmp(bind_t, binding, binding_len) == 0)
                {
                    matched = 1;
                    matched_k = k_before_export;
                    matched_rs = rem_s;
                    matched_re = rem_e;
                    break;
                }
            }
        }
        k_before_export++;
    }
    if (!matched) return 0;
    /* The counter currently sits at (start_of_pass_for_file + k_before_export).
       Our match is the matched_k-th statement among those preceding.  Its
       emitted index is start_of_pass_for_file + matched_k
       = _imp_modimp_counter - k_before_export + matched_k. */
    *out_idx = _imp_modimp_counter - k_before_export + matched_k;
    *out_remote_start = matched_rs;
    *out_remote_end = matched_re;
    return 1;
}

/* ============================  export rewriter  ============================ */
/* Forward decl — defined below in the import-rewrite section. */
static void _emit_require_call(rp_string *out, const char *spec, size_t spec_len,
                               uint32_t *polysneeded_or_null);

static int rewrite_export_node(EditList *edits, const char *src, TSNode snode, RangeList *claimed,
                               uint32_t *polysneeded, int overlaps)
{
    size_t ns = ts_node_start_byte(snode), ne = ts_node_end_byte(snode);
    /* `default` is an UNNAMED keyword token in the tree-sitter-javascript
       grammar (see `seq('default', …)` in the grammar) — it is NOT exposed
       via a field name. `ts_node_child_by_field_name(snode, "default", …)`
       always returns null here. Detect by walking unnamed children and
       matching their byte range to "default". */
    int is_default_export = 0;
    for (uint32_t _i = 0, _n = ts_node_child_count(snode); _i < _n; _i++)
    {
        TSNode _ch = ts_node_child(snode, _i);
        if (ts_node_is_named(_ch)) continue;
        size_t _cs = ts_node_start_byte(_ch), _ce = ts_node_end_byte(_ch);
        if (_ce - _cs == 7 && memcmp(src + _cs, "default", 7) == 0)
        {
            is_default_export = 1;
            break;
        }
    }
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
                        /* default: function f(...){} ; exports.default = f;
                           Set __esModule so _interopDefault unwraps to `f`
                           on the consumer side.  We do NOT overwrite
                           module.exports (`module.exports = f`) because
                           that would erase any sibling named exports —
                           luxon/datetime.js has BOTH `export default class
                           Duration` and `export const lowOrderMatrix`. */
                        rp_string_puts(post, "; exports.__esModule=true; exports.default = ");
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
           => var _TrN_default = <expr>; module.exports = _TrN_default; */
        if (!ts_node_is_null(val))
        {
            if (overlaps)
                return 1;
            size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
            char *body = dup_range(src, vs, ve);
            rp_string *out = rp_string_new(64);
            rp_string_puts(out, "var _TrN_default = ");
            rp_string_puts(out, body);
            rp_string_puts(out, "; exports.__esModule=true; exports.default = _TrN_default;");
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

                            rp_string_puts(out, "var _TrN_tmpD0 = ");
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
                                        rp_string_puts(out, " = _TrN_tmpD0.");
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
                                                rp_string_puts(out, " = _TrN_tmpD0.");
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
                                        rp_string_puts(out, " = (_TrN_tmpD0.");
                                        rp_string_putsn(out, src + ks, ke - ks);
                                        rp_string_puts(out, " === undefined ? ");
                                        rp_string_putsn(out, src + rs, re - rs);
                                        rp_string_puts(out, " : _TrN_tmpD0.");
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
                                    rp_string_puts(out, " = _TrN_tmpD0.");
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
                                        rp_string_puts(out, " = (_TrN_tmpD0.");
                                        rp_string_putsn(out, src + is, ie - is);
                                        rp_string_puts(out, " === undefined ? ");
                                        rp_string_putsn(out, src + rs, re - rs);
                                        rp_string_puts(out, " : _TrN_tmpD0.");
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
                                    rp_string_puts(out, ") t[k]=o[k]; } return t; })(_TrN_tmpD0, [");
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

                            rp_string_puts(out, "var _TrN_tmpA0 = ");
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
                                    rp_string_puts(out, " = _TrN_tmpA0[");
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
                                    rp_string_puts(out, " = (_TrN_tmpA0[");
                                    char buf[32];
                                    snprintf(buf, sizeof(buf), "%zu", idx);
                                    rp_string_puts(out, buf);
                                    rp_string_puts(out, "] === undefined ? ");
                                    rp_string_putsn(out, src + rs, re - rs);
                                    rp_string_puts(out, " : _TrN_tmpA0[");
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
                                        rp_string_puts(out, " = Array.prototype.slice.call(_TrN_tmpA0, ");
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
                                            rp_string_puts(out, " = _TrN_tmpA0[");
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
        const char *mod_spec = NULL;  /* unquoted */
        size_t mod_spec_len = 0;
        if (!ts_node_is_null(srcnode))
        {
            size_t ms = ts_node_start_byte(srcnode), me = ts_node_end_byte(srcnode);
            mod = dup_range(src, ms, me);
            /* Strip surrounding quote chars to get the raw spec. */
            if (me - ms >= 2) {
                mod_spec = src + ms + 1;
                mod_spec_len = (me - ms) - 2;
            }
        }

        rp_string *out = rp_string_new(64);
        char tmp[24];
        tmp[0] = '\0';

        if (mod)
        {
            snprintf(tmp, sizeof(tmp), "_TrN_tmpExp0");
            rp_string_puts(out, "var ");
            rp_string_puts(out, tmp);
            rp_string_puts(out, " = ");
            _emit_require_call(out, mod_spec, mod_spec_len, polysneeded);
            rp_string_puts(out, "; ");
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
                rp_string_putsn(out, src + ls, le - ls); /* tmp.prop */
            }
            else
            {
                /* Local re-export — if the name is a named-import
                   binding from this module, emit `_TrN_modImp<N>.<remote>`
                   so the cross-module live binding survives.  The
                   import rewriter no longer rewrites identifiers
                   inside export_specifier (see _imp_rewrite_refs),
                   so without this lookup the RHS would be an
                   undefined name. */
                uint32_t modidx = 0;
                size_t rstart = 0, rend = 0;
                if (_exp_find_import_binding(_imp_find_program(snode), snode, src,
                                             src + ls, le - ls,
                                             &modidx, &rstart, &rend))
                {
                    rp_string_appendf(out, "_TrN_modImp%u.", modidx);
                    rp_string_putsn(out, src + rstart, rend - rstart);
                }
                else
                {
                    rp_string_putsn(out, src + ls, le - ls);
                }
            }
            rp_string_puts(out, ";");
        }

        add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
        rp_string_free(out);
        free(mod);
        return 1;
    }

    /* export * as Foo from "mod"  (ES2020 namespace re-export)
       The `* as Foo` part is wrapped in a `namespace_export` node;
       inside it sits the identifier `Foo`.  Emit:
         exports.Foo = _TrN_Sp._interopRequireWildcard(require("mod")); */
    {
        TSNode nsexp = find_child_type(snode, "namespace_export", NULL);
        TSNode srcnode_ns = ts_node_child_by_field_name(snode, "source", 6);
        if (!ts_node_is_null(nsexp) && !ts_node_is_null(srcnode_ns))
        {
            if (overlaps) return 1;
            TSNode alias = find_child_type(nsexp, "identifier", NULL);
            if (ts_node_is_null(alias))
                alias = find_child_type(nsexp, "property_identifier", NULL);
            if (!ts_node_is_null(alias))
            {
                size_t as_ = ts_node_start_byte(alias), ae_ = ts_node_end_byte(alias);
                size_t ms_ = ts_node_start_byte(srcnode_ns), me_ = ts_node_end_byte(srcnode_ns);
                const char *spec = (me_ - ms_ >= 2) ? (src + ms_ + 1) : NULL;
                size_t spec_len = (me_ - ms_ >= 2) ? (me_ - ms_) - 2 : 0;
                rp_string *out = rp_string_new(64);
                rp_string_puts(out, "exports.");
                rp_string_putsn(out, src + as_, ae_ - as_);
                rp_string_puts(out, " = _TrN_Sp._interopRequireWildcard(");
                _emit_require_call(out, spec, spec_len, polysneeded);
                rp_string_puts(out, ");");
                add_edit_take_ownership(edits, ns, ne, rp_string_steal(out), claimed);
                rp_string_free(out);
                *polysneeded |= IMPORT_PF;
                return 1;
            }
        }
    }

    /* export * from "mod"
       The tree-sitter-javascript grammar represents the `*` as an
       anonymous token child (not a named field), so we walk children
       to detect it instead of relying on field-name lookup. */
    {
        TSNode srcnode = ts_node_child_by_field_name(snode, "source", 6);
        int saw_star = 0;
        uint32_t cn = ts_node_child_count(snode);
        for (uint32_t ci = 0; ci < cn; ci++)
        {
            TSNode ch = ts_node_child(snode, ci);
            if (ts_node_is_named(ch)) continue;
            size_t cs = ts_node_start_byte(ch), ce = ts_node_end_byte(ch);
            if (ce - cs == 1 && src[cs] == '*') { saw_star = 1; break; }
        }
        if (saw_star && !ts_node_is_null(srcnode))
        {
            if (overlaps)
                return 1;
            size_t ms = ts_node_start_byte(srcnode), me = ts_node_end_byte(srcnode);
            char *mod = dup_range(src, ms, me);
            const char *mod_spec = (me - ms >= 2) ? (src + ms + 1) : NULL;
            size_t mod_spec_len = (me - ms >= 2) ? (me - ms) - 2 : 0;
            rp_string *out = rp_string_new(64);
            rp_string_puts(out, "{var __tmpExp = ");
            _emit_require_call(out, mod_spec, mod_spec_len, polysneeded);
            rp_string_puts(out,
                ";for (var __k in __tmpExp) {if (__k === \"default\" || __k === \"__esModule\") continue;exports[__k] = __tmpExp[__k];}}");
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
            snprintf(tmpname, sizeof(tmpname), "_TrN_dp%u", _dp_counter++);
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
            /* Non-destructured param. If it's `x = default` we have to
               lower the default too — otherwise duktape rejects the
               whole `(x = "y", _dp1)` shape. function_like_default_params
               would normally handle this, but it bails out when ANY
               param uses destructure-with-default (its
               build_param_default_inits returns NULL), so the method
               falls through to us. Handle simple defaults inline. */
            if (strcmp(pt, "assignment_pattern") == 0)
            {
                TSNode left = ts_node_child_by_field_name(p, "left", 4);
                TSNode right = ts_node_child_by_field_name(p, "right", 5);
                if (!ts_node_is_null(left) && !ts_node_is_null(right) &&
                    strcmp(ts_node_type(left), "identifier") == 0)
                {
                    size_t ls = ts_node_start_byte(left), le = ts_node_end_byte(left);
                    size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                    /* param is just the identifier */
                    rp_string_putsn(new_params, src + ls, le - ls);
                    /* body inj: if (X === void 0) X = <default>; */
                    rp_string_puts(body_inj, "if (");
                    rp_string_putsn(body_inj, src + ls, le - ls);
                    rp_string_puts(body_inj, " === void 0) ");
                    rp_string_putsn(body_inj, src + ls, le - ls);
                    rp_string_puts(body_inj, " = ");
                    rp_string_putsn(body_inj, src + rs, re - rs);
                    rp_string_puts(body_inj, "; ");
                    if (param_def) free(param_def);
                    continue;
                }
            }
            /* Fallback: copy as-is. */
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

/* Forward decls — bodies live after the _bs_* helpers in the
   block-scope section (which we depend on). */
static void _imp_rewrite_refs(TSNode node, const char *src,
                              const char *name, size_t name_len,
                              const char *new_text,
                              EditList *edits, RangeList *claimed);
static TSNode _imp_find_program(TSNode n);

/* True if a string specifier is "bare" (no relative/absolute prefix
   and not a zip-style `:` path).  Bare specs need to resolve via the
   node_modules walk in `_TrN_Sp._req`. */
static int _is_bare_spec(const char *spec, size_t spec_len)
{
    if (spec_len == 0) return 0;
    char c = spec[0];
    if (c == '.' || c == '/' || c == ':') return 0;
    return 1;
}

/* Emit either `require("spec")` (relative / absolute / zip paths) or
   `_TrN_Sp._req(module,"spec")` (bare specs).  When the spec is bare,
   set *polysneeded_or_null |= BARE_REQ_PF (if non-NULL) so the helper
   is emitted into the preamble.  The import rewriters emit `require()`
   literals inside their edit replacement text, which the later
   bare-spec rewriter doesn't see (it walks the AST, not the edit
   list); we have to choose the right form here. */
static void _emit_require_call(rp_string *out, const char *spec, size_t spec_len,
                               uint32_t *polysneeded_or_null)
{
    if (_is_bare_spec(spec, spec_len))
    {
        rp_string_appendf(out, "_TrN_Sp._req(module,\"%.*s\")",
                          (int)spec_len, spec);
        if (polysneeded_or_null)
            *polysneeded_or_null |= BARE_REQ_PF;
    }
    else
    {
        rp_string_appendf(out, "require(\"%.*s\")",
                          (int)spec_len, spec);
    }
}

static int do_named_imports(EditList *edits, const char *src, TSNode snode, TSNode named_imports, TSNode string_frag,
                            size_t start, size_t end, RangeList *claimed)
{
    uint32_t *tmpn_p = &_imp_modimp_counter;
#define tmpn (*tmpn_p)
    uint32_t pos = 0;
    char buf[32];

    TSNode spec = find_child_type(named_imports, "import_specifier", &pos);
    if (ts_node_is_null(spec))
        return 0;

    size_t mod_s = ts_node_start_byte(string_frag), mod_e = ts_node_end_byte(string_frag);

    /* temp module binding */
    sprintf(buf, "_TrN_modImp%u", tmpn);
    rp_string *out = rp_string_new(64);

    rp_string_appendf(out, "var %s=", buf);
    _emit_require_call(out, src + mod_s, mod_e - mod_s, NULL);
    rp_string_puts(out, ";if(_TrN_Sp._pP)_TrN_Sp._pP();");

    /* Locate the enclosing program so we can rewrite refs across the
       whole module (closures included). */
    TSNode program = _imp_find_program(snode);

    /* For each specifier: rewrite all references to the binding (alias
       if present, else local name) into `<buf>.<remoteName>` member
       expressions. This preserves live-binding semantics: cycles see
       the export at call time rather than at destructure time. The
       previous emission of `var <name>=<buf>.<remoteName>;` is dropped;
       references are rewritten directly. */
    while (!ts_node_is_null(spec))
    {
        TSNode local = ts_node_child_by_field_name(spec, "name", 4);
        TSNode alias = ts_node_child_by_field_name(spec, "alias", 5);

        const char *binding_text = NULL;
        size_t binding_len = 0;
        const char *remote_text = NULL;
        size_t remote_len = 0;

        if (ts_node_is_null(local))
        {
            /* fallback: use spec's raw text */
            size_t s = ts_node_start_byte(spec), e = ts_node_end_byte(spec);
            binding_text = src + s; binding_len = e - s;
            remote_text  = binding_text; remote_len = binding_len;
        }
        else
        {
            size_t ls = ts_node_start_byte(local), le = ts_node_end_byte(local);
            remote_text = src + ls; remote_len = le - ls;
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                binding_text = src + as; binding_len = ae - as;
            }
            else
            {
                binding_text = remote_text; binding_len = remote_len;
            }
        }

        /* Build "buf.remote" replacement text. */
        rp_string *acc = rp_string_new(strlen(buf) + remote_len + 2);
        rp_string_puts(acc, buf);
        rp_string_putc(acc, '.');
        rp_string_putsn(acc, remote_text, remote_len);
        char *member_text = rp_string_steal(acc);
        acc = rp_string_free(acc);

        if (!ts_node_is_null(program))
        {
            _imp_rewrite_refs(program, src, binding_text, binding_len,
                              member_text, edits, claimed);
        }
        free(member_text);

        pos++;
        spec = find_child_type(named_imports, "import_specifier", &pos);
    }
    add_edit_take_ownership(edits, start, end, rp_string_steal(out), claimed);
    out=rp_string_free(out);

    tmpn++;
#undef tmpn
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
    rp_string_appendf(out, "var %.*s=_TrN_Sp._interopRequireWildcard(",
                      (id_end - id_start), src + id_start);
    _emit_require_call(out, src + mod_name_start, mod_name_end - mod_name_start, polysneeded);
    rp_string_puts(out, ");if(_TrN_Sp._pP)_TrN_Sp._pP();");

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
    rp_string_appendf(out, "var %.*s=_TrN_Sp._interopDefault(",
                      (int)(id_e - id_s), src + id_s);
    _emit_require_call(out, src + mod_s, mod_e - mod_s, NULL);
    rp_string_puts(out, ");if(_TrN_Sp._pP)_TrN_Sp._pP();");

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
    sprintf(tbuf, "_TrN_modImpdn%u", tmpn);

    size_t mod_s = ts_node_start_byte(string_frag), mod_e = ts_node_end_byte(string_frag);
    size_t id_s = ts_node_start_byte(default_ident), id_e = ts_node_end_byte(default_ident);

    /* require once */
    rp_string *out=rp_string_new(512);
    rp_string_appendf(out, "var %s=", tbuf);
    _emit_require_call(out, src + mod_s, mod_e - mod_s, NULL);
    rp_string_puts(out, ";if(_TrN_Sp._pP)_TrN_Sp._pP();");

    /* bind default: var def = __tmp.default;  Default values can be
       eager-bound because interopDefault freezes the value at require
       time; circular partial-load happily returns the early value or
       undefined and the consumer handles that. */
    rp_string_appendf(out, "var %.*s=%s.default;", (int)(id_e - id_s), src + id_s, tbuf);

    /* Locate the enclosing program for live-binding rewrites. */
    TSNode program = _imp_find_program(snode);

    /* Named specifiers: rewrite each reference to `<tbuf>.<remote>`
       (live binding) rather than emit an eager `var X = T.X;`.
       Required for ES module cycles — e.g. luxon's datetime.js ↔
       interval.js: interval imports `friendlyDateTime`, which datetime
       defines AFTER it imports interval, so an eager copy is `undefined`. */
    uint32_t pos = 0;
    TSNode spec = find_child_type(named_imports, "import_specifier", &pos);
    while (!ts_node_is_null(spec))
    {
        TSNode local = ts_node_child_by_field_name(spec, "name", 4);
        TSNode alias = ts_node_child_by_field_name(spec, "alias", 5);

        const char *binding_text = NULL;
        size_t binding_len = 0;
        const char *remote_text = NULL;
        size_t remote_len = 0;

        if (ts_node_is_null(local))
        {
            size_t s = ts_node_start_byte(spec), e = ts_node_end_byte(spec);
            binding_text = src + s; binding_len = e - s;
            remote_text  = binding_text; remote_len = binding_len;
        }
        else
        {
            size_t ls = ts_node_start_byte(local), le = ts_node_end_byte(local);
            remote_text = src + ls; remote_len = le - ls;
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                binding_text = src + as; binding_len = ae - as;
            }
            else
            {
                binding_text = remote_text; binding_len = remote_len;
            }
        }

        /* Build "tbuf.remote" replacement text. */
        rp_string *acc = rp_string_new(strlen(tbuf) + remote_len + 2);
        rp_string_puts(acc, tbuf);
        rp_string_putc(acc, '.');
        rp_string_putsn(acc, remote_text, remote_len);
        char *member_text = rp_string_steal(acc);
        acc = rp_string_free(acc);

        if (!ts_node_is_null(program))
            _imp_rewrite_refs(program, src, binding_text, binding_len,
                              member_text, edits, claimed);
        free(member_text);

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

    /* If the module specifier is a bare spec, the helpers below will
       emit `_TrN_Sp._req(module, "...")` instead of `require("...")`
       so the node_modules walk resolves it.  Flag the polyfill now. */
    {
        size_t ms = ts_node_start_byte(string_frag);
        size_t me = ts_node_end_byte(string_frag);
        if (_is_bare_spec(src + ms, me - ms))
            *polysneeded |= BARE_REQ_PF;
    }

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

    /* Side-effect import:  import "X";  →  require("X");
       Bare spec needs the helper form so the node_modules walk runs. */
    rp_string *side = rp_string_new(64);
    _emit_require_call(side, src + sstart, slen, NULL);
    rp_string_puts(side, ";if(_TrN_Sp._pP)_TrN_Sp._pP();");
    add_edit_take_ownership(edits, ns, ne, rp_string_steal(side), claimed);
    rp_string_free(side);
    return 1;
}

/* Dynamic `import(<spec>)` — lower to a Promise that resolves to the
   module's namespace.  Native `import()` returns a Promise<ModuleNamespace>;
   we approximate by wrapping require() with `_interopRequireWildcard` so
   CommonJS modules appear with `{default, ...named}` shape, matching
   what node's CJS-import gives.  `<spec>` may be any expression (literal
   string, template, identifier, …) so we copy its source bytes verbatim.
   `_pP` is invoked for the side-effect of patching Promise methods that
   the require may have installed, before the Promise.resolve call. */
static int rewrite_dynamic_import(EditList *edits, const char *src, TSNode snode, RangeList *claimed,
                                  uint32_t *polysneeded, int overlaps)
{
    if (strcmp(ts_node_type(snode), "call_expression") != 0)
        return 0;
    TSNode fn = ts_node_child_by_field_name(snode, "function", 8);
    if (ts_node_is_null(fn))
        return 0;
    if (strcmp(ts_node_type(fn), "import") != 0)
        return 0;

    TSNode args = ts_node_child_by_field_name(snode, "arguments", 9);
    if (ts_node_is_null(args))
        return 0;
    uint32_t nc = ts_node_named_child_count(args);
    if (nc < 1)
        return 0;

    if (overlaps)
        return 1;

    TSNode spec = ts_node_named_child(args, 0);
    size_t ss = ts_node_start_byte(spec), se = ts_node_end_byte(spec);
    size_t ns = ts_node_start_byte(snode), ne = ts_node_end_byte(snode);

    /* Use `new Promise(...)` so synchronous throws from require() become
       rejections (matches node's `import("./bad")` rejecting instead of
       throwing). _pP runs AFTER require, since require() is what installs
       the polyfill's missing Promise methods. */
    rp_string *out = rp_string_new(128);
    rp_string_puts(out, "(new Promise(function(_TrN_r){var _TrN_m=_TrN_Sp._interopRequireWildcard(require(");
    rp_string_putsn(out, src + ss, se - ss);
    rp_string_puts(out, "));if(_TrN_Sp._pP)_TrN_Sp._pP();_TrN_r(_TrN_m);}))");
    char *rep = rp_string_steal(out);
    out = rp_string_free(out);
    add_edit_take_ownership(edits, ns, ne, rep, claimed);
    *polysneeded |= IMPORT_PF | PROMISE_PF;
    return 1;
}

/* True iff `n`'s subtree contains an await_expression that isn't nested
   inside a function-like (function/arrow/method/generator). Used to detect
   top-level await in a program. */
static int _has_top_level_await(TSNode n)
{
    const char *t = ts_node_type(n);
    if (strcmp(t, "await_expression") == 0)
        return 1;
    /* Stop descending at function boundaries — awaits inside async functions
       are not top-level. Also stop at class bodies (await isn't legal there
       at evaluation-of-body time anyway; field initializers are deferred). */
    if (strcmp(t, "function_declaration") == 0 ||
        strcmp(t, "function_expression") == 0 ||
        strcmp(t, "function") == 0 ||
        strcmp(t, "arrow_function") == 0 ||
        strcmp(t, "method_definition") == 0 ||
        strcmp(t, "generator_function") == 0 ||
        strcmp(t, "generator_function_declaration") == 0 ||
        strcmp(t, "generator_function_expression") == 0 ||
        strcmp(t, "class_declaration") == 0 ||
        strcmp(t, "class") == 0 ||
        strcmp(t, "class_body") == 0)
        return 0;
    uint32_t c = ts_node_child_count(n);
    for (uint32_t i = 0; i < c; i++)
        if (_has_top_level_await(ts_node_child(n, i)))
            return 1;
    return 0;
}

/* Top-level `await`. Duktape rejects `await` outside an async function.
   When a program has at least one top-level await, we:
     1. Wrap all top-level statements (after the directive prologue) in an
        IIFE: `(async function(){ ... }).call(this).then(null, errHandler)`.
     2. Rewrite top-level `var x = init;` / `let` / `const` to
        `global.x = init;` so they remain visible on the global object
        (preserves today's "top-level var attaches to global" semantics).
     3. Append `;global.X = X;` after top-level `function`/`class`
        declarations so they remain externally callable.
   The async IIFE will be picked up on the next rewrite pass by the
   existing async-function lowering, which converts it to a regenerator-
   driven state machine. */
static int rewrite_top_level_await(EditList *edits, const char *src, TSNode prog_node,
                                   RangeList *claimed, uint32_t *polysneeded, int *unresolved,
                                   int overlaps)
{
    if (strcmp(ts_node_type(prog_node), "program") != 0)
        return 0;
    if (!_has_top_level_await(prog_node))
        return 0;
    if (overlaps)
        return 1;

    uint32_t nc = ts_node_named_child_count(prog_node);
    if (nc == 0)
        return 0;

    /* Skip directive prologue ("use strict", "use transpilerGlobally", …). */
    uint32_t first_idx = 0;
    while (first_idx < nc)
    {
        TSNode k = ts_node_named_child(prog_node, first_idx);
        const char *kt = ts_node_type(k);
        if (strcmp(kt, "expression_statement") == 0)
        {
            uint32_t ec = ts_node_named_child_count(k);
            if (ec >= 1)
            {
                TSNode e = ts_node_named_child(k, 0);
                if (strcmp(ts_node_type(e), "string") == 0)
                {
                    first_idx++;
                    continue;
                }
            }
        }
        break;
    }
    if (first_idx >= nc)
        return 0;

    TSNode first = ts_node_named_child(prog_node, first_idx);
    TSNode last = ts_node_named_child(prog_node, nc - 1);
    size_t open_pos = ts_node_start_byte(first);
    size_t close_pos = ts_node_end_byte(last);

    add_edit(edits, open_pos, open_pos,
             ";(async function(){\n", claimed);
    add_edit(edits, close_pos, close_pos,
             "\n}).call(this).then(null,function(_TrN_e){"
             "if(typeof console!=='undefined'&&console&&console.error)console.error(_TrN_e);"
             "if(typeof process!=='undefined'&&process.exit)process.exit(1);"
             "else throw _TrN_e;});",
             claimed);

    /* Rewrite top-level declarations. */
    for (uint32_t i = first_idx; i < nc; i++)
    {
        TSNode stmt = ts_node_named_child(prog_node, i);
        const char *st = ts_node_type(stmt);

        if (strcmp(st, "variable_declaration") == 0 ||
            strcmp(st, "lexical_declaration") == 0)
        {
            uint32_t dc = ts_node_named_child_count(stmt);
            rp_string *rep = rp_string_new(64);
            int any_destr = 0;
            for (uint32_t j = 0; j < dc; j++)
            {
                TSNode decl = ts_node_named_child(stmt, j);
                if (strcmp(ts_node_type(decl), "variable_declarator") != 0) continue;
                TSNode name = ts_node_child_by_field_name(decl, "name", 4);
                TSNode val  = ts_node_child_by_field_name(decl, "value", 5);
                if (ts_node_is_null(name)) continue;

                const char *nt = ts_node_type(name);
                if (strcmp(nt, "identifier") == 0)
                {
                    size_t nns = ts_node_start_byte(name), nne = ts_node_end_byte(name);
                    if (rep->len) rp_string_puts(rep, " ");
                    rp_string_puts(rep, "global.");
                    rp_string_putsn(rep, src + nns, nne - nns);
                    if (!ts_node_is_null(val))
                    {
                        size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
                        rp_string_puts(rep, "=");
                        rp_string_putsn(rep, src + vs, ve - vs);
                    }
                    else
                    {
                        rp_string_puts(rep, "=void 0");
                    }
                    rp_string_puts(rep, ";");
                }
                else
                {
                    /* Destructure at top level — fall back to keeping the
                       declaration as-is (bindings stay local to the IIFE).
                       MVP limitation; would need an inline temp +
                       per-binding `global.name = temp.field` pass. */
                    any_destr = 1;
                    break;
                }
            }
            if (!any_destr && rep->len > 0)
            {
                char *r = rp_string_steal(rep);
                size_t ss = ts_node_start_byte(stmt), se = ts_node_end_byte(stmt);
                add_edit_take_ownership(edits, ss, se, r, claimed);
            }
            rep = rp_string_free(rep);
        }
        else if (strcmp(st, "function_declaration") == 0 ||
                 strcmp(st, "class_declaration") == 0)
        {
            TSNode nm = ts_node_child_by_field_name(stmt, "name", 4);
            if (!ts_node_is_null(nm) && strcmp(ts_node_type(nm), "identifier") == 0)
            {
                size_t nns = ts_node_start_byte(nm), nne = ts_node_end_byte(nm);
                rp_string *rep = rp_string_new(32);
                rp_string_puts(rep, ";global.");
                rp_string_putsn(rep, src + nns, nne - nns);
                rp_string_puts(rep, "=");
                rp_string_putsn(rep, src + nns, nne - nns);
                rp_string_puts(rep, ";");
                size_t se = ts_node_end_byte(stmt);
                char *r = rp_string_steal(rep);
                rep = rp_string_free(rep);
                add_edit_take_ownership(edits, se, se, r, claimed);
            }
        }
    }

    *polysneeded |= ASYNC_PF | PROMISE_PF;
    /* Force a second rewrite pass so the just-emitted `async function(){}`
       wrapper gets picked up by `rewrite_async_await_to_regenerator` and
       lowered to regenerator state machine. */
    *unresolved = 1;
    return 1;
}

// Templates (tagged + untagged)
static int rewrite_template_node(EditList *edits, const char *src, TSNode tpl_node, RangeList *claimed, int overlaps)
{
    if (overlaps)
        return 1;

    size_t ns = ts_node_start_byte(tpl_node), ne = ts_node_end_byte(tpl_node);

    /* Detect tagged-template form: the template_string is the
       `arguments` field of a `call_expression` whose `function` field
       is the tag. Tree-sitter wraps `tag \`...\`` in a call_expression
       regardless of whitespace between the tag and the backtick. The
       old check used `prev_named_sibling` and compared end-byte to the
       template's start-byte, which failed whenever there was any
       whitespace (as in ajv `(0, codegen_1._) \`...\``). */
    TSNode tag = (TSNode){{0}};
    int is_tagged = 0;
    TSNode parent = ts_node_parent(tpl_node);
    if (!ts_node_is_null(parent) && strcmp(ts_node_type(parent), "call_expression") == 0)
    {
        TSNode parent_args = ts_node_child_by_field_name(parent, "arguments", 9);
        if (!ts_node_is_null(parent_args) && ts_node_eq(parent_args, tpl_node))
        {
            tag = ts_node_child_by_field_name(parent, "function", 8);
            if (!ts_node_is_null(tag))
                is_tagged = 1;
        }
    }

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

/* Forward decl: defined later in the file. Needed here so the arrow
   rewriter can run block-scope rename on its body into a local edit
   list before assembling its wholesale replacement. */
static int rewrite_block_scope_rename(EditList *edits, const char *src, TSNode fn_node,
                                      RangeList *claimed, int overlaps);

/* Apply edits whose ranges all fall inside [slice_start, slice_end) to
   the slice src[slice_start..slice_end), returning a malloc'd
   null-terminated string. Edits at absolute src positions are
   translated to slice-relative on the fly. The input edits list is
   re-sorted in descending start order. */
static char *_apply_edits_to_slice(const char *src, size_t slice_start, size_t slice_end,
                                   EditList *e)
{
    qsort(e->items, e->len, sizeof(Edit), cmp_desc);
    size_t slen = slice_end - slice_start;
    size_t worst = slen;
    for (size_t i = 0; i < e->len; i++)
    {
        Edit *ed = &e->items[i];
        if (ed->start < slice_start || ed->end > slice_end) continue;
        worst += strlen(ed->text);
    }
    char *buf = NULL;
    REMALLOC(buf, worst + 1);
    memcpy(buf, src + slice_start, slen);
    size_t cur = slen;
    buf[cur] = 0;
    for (size_t i = 0; i < e->len; i++)
    {
        Edit *ed = &e->items[i];
        if (ed->start < slice_start || ed->end > slice_end) continue;
        size_t rs = ed->start - slice_start;
        size_t re = ed->end - slice_start;
        size_t rep_len = strlen(ed->text);
        size_t old_len = re - rs;
        memmove(buf + rs + rep_len, buf + re, cur - re);
        memcpy(buf + rs, ed->text, rep_len);
        cur = cur - old_len + rep_len;
    }
    buf[cur] = 0;
    return buf;
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

    /* For block-body arrows, run babel-style block-scope let/const
       rename on the arrow's body into a LOCAL edit list and produce a
       substituted body string. The arrow's wholesale-replace below
       would otherwise clobber identifier-level edits inside the body
       (we'd add the edits to the main list, they'd apply first since
       they're at higher start positions, and then the arrow's edit at
       [ns, ne] would overwrite the renamed bytes). */
    char *body_subst = NULL;
    size_t body_subst_len = 0;
    if (is_block && !overlaps)
    {
        EditList _local_edits;
        init_edits(&_local_edits);
        RangeList _local_claimed;
        rl_init(&_local_claimed);
        rewrite_block_scope_rename(&_local_edits, src, arrow_node, &_local_claimed, 0);
        if (_local_edits.len > 0)
        {
            body_subst = _apply_edits_to_slice(src, bs, be, &_local_edits);
            body_subst_len = strlen(body_subst);
        }
        free_edits(&_local_edits);
        free(_local_claimed.a);
    }
    const char *body_src = body_subst ? body_subst : (src + bs);
    size_t body_src_len = body_subst ? body_subst_len : (be - bs);
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

    // Multi-param destructuring (block body only): when there are
    // multiple params and at least one is an array/object pattern,
    // assign each destructure to a fresh `_arr_N` or `_obj_N` and emit
    // bindings in a body prelude. This is the ES2015 case that blocks
    // luxon's `([sofar, current], item) => …`.
    int multi_did = 0;
    rp_string *multi_params = NULL;
    rp_string *multi_inj = NULL;
    if (!single &&
        strcmp(ts_node_type(params), "formal_parameters") == 0)
    {
        uint32_t np = ts_node_named_child_count(params);
        int has_destruct = 0;
        for (uint32_t i = 0; i < np; i++)
        {
            TSNode p = ts_node_named_child(params, i);
            const char *pt = ts_node_type(p);
            if (strcmp(pt, "array_pattern") == 0 || strcmp(pt, "object_pattern") == 0)
            {
                has_destruct = 1;
                break;
            }
            if (strcmp(pt, "assignment_pattern") == 0)
            {
                TSNode left = ts_node_child_by_field_name(p, "left", 4);
                if (!ts_node_is_null(left))
                {
                    const char *lt = ts_node_type(left);
                    if (strcmp(lt, "array_pattern") == 0 || strcmp(lt, "object_pattern") == 0)
                    {
                        has_destruct = 1;
                        break;
                    }
                }
            }
        }
        if (has_destruct)
        {
            multi_params = rp_string_new(64);
            multi_inj = rp_string_new(128);
            rp_string_putc(multi_params, '(');
            unsigned arr_n = 0, obj_n = 0;
            for (uint32_t i = 0; i < np; i++)
            {
                if (i > 0) rp_string_puts(multi_params, ", ");
                TSNode p = ts_node_named_child(params, i);
                const char *pt = ts_node_type(p);
                size_t pps = ts_node_start_byte(p), ppe = ts_node_end_byte(p);

                TSNode destr_pat = (TSNode){{0}};
                char *param_def = NULL;
                if (strcmp(pt, "array_pattern") == 0 || strcmp(pt, "object_pattern") == 0)
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
                        if (strcmp(lt, "array_pattern") == 0 || strcmp(lt, "object_pattern") == 0)
                        {
                            destr_pat = left;
                            if (!ts_node_is_null(right))
                            {
                                size_t ds = ts_node_start_byte(right), de = ts_node_end_byte(right);
                                param_def = malloc(de - ds + 1);
                                memcpy(param_def, src + ds, de - ds);
                                param_def[de - ds] = '\0';
                            }
                        }
                    }
                }

                if (!ts_node_is_null(destr_pat))
                {
                    char tmpname[32];
                    int is_array_pat = (strcmp(ts_node_type(destr_pat), "array_pattern") == 0);
                    if (is_array_pat)
                        snprintf(tmpname, sizeof(tmpname), "_TrN_arr%u", ++arr_n);
                    else
                        snprintf(tmpname, sizeof(tmpname), "_TrN_obj%u", ++obj_n);
                    rp_string_puts(multi_params, tmpname);

                    if (param_def)
                    {
                        rp_string_appendf(multi_inj, "if (%s === void 0) %s = %s; ",
                                          tmpname, tmpname, param_def);
                        free(param_def);
                    }

                    Bindings mb;
                    binds_init(&mb);
                    if (collect_flat_destructure_bindings(destr_pat, src, tmpname, &mb) && mb.len > 0)
                    {
                        rp_string_puts(multi_inj, "var ");
                        for (size_t bi = 0; bi < mb.len; bi++)
                        {
                            if (bi > 0) rp_string_puts(multi_inj, ", ");
                            rp_string_puts(multi_inj, mb.a[bi].name);
                            rp_string_puts(multi_inj, "=");
                            if (mb.a[bi].defval)
                            {
                                rp_string_puts(multi_inj, mb.a[bi].repl);
                                rp_string_puts(multi_inj, " !== undefined ? ");
                                rp_string_puts(multi_inj, mb.a[bi].repl);
                                rp_string_puts(multi_inj, " : ");
                                rp_string_puts(multi_inj, mb.a[bi].defval);
                            }
                            else
                            {
                                rp_string_puts(multi_inj, mb.a[bi].repl);
                            }
                        }
                        rp_string_puts(multi_inj, "; ");
                    }
                    binds_free(&mb);
                    multi_did = 1;
                }
                else
                {
                    /* Plain param (identifier, rest_pattern, etc.) — copy bytes. */
                    rp_string_putsn(multi_params, src + pps, ppe - pps);
                }
            }
            rp_string_putc(multi_params, ')');
            if (!multi_did)
            {
                /* No destructure actually emitted (shouldn't happen given
                   has_destruct check above, but defensive). */
                multi_params = rp_string_free(multi_params);
                multi_inj = rp_string_free(multi_inj);
            }
        }
    }

    /* Rest parameter detection. Arrows go through this rewriter directly
       (not rewrite_function_rest, which only sees function_declaration /
       function_expression / method_definition).  If the params contain a
       rest_pattern, we strip it from the emitted params and inject
       `var <name> = Object.values(arguments).slice(N);` into the rewritten
       body. Works for block AND concise bodies (concise gets the IIFE
       wrap with the shim before the return).
       Only handles the simple rest case (identifier in rest_pattern); rest
       combined with destructure-rest inside a pattern would need extra
       work — not blocking. */
    char *rest_shim_name = NULL;
    size_t rest_shim_idx = 0;
    char *rest_emitted_params = NULL; /* params text minus rest */
    if (!multi_did && !ts_node_is_null(params) &&
        strcmp(ts_node_type(params), "formal_parameters") == 0)
    {
        uint32_t np = ts_node_named_child_count(params);
        for (uint32_t i = 0; i < np; i++)
        {
            TSNode p = ts_node_named_child(params, i);
            if (strcmp(ts_node_type(p), "rest_pattern") == 0)
            {
                TSNode nm = (TSNode){{0}};
                uint32_t rc = ts_node_named_child_count(p);
                for (uint32_t k = 0; k < rc; k++)
                {
                    TSNode kp = ts_node_named_child(p, k);
                    if (strcmp(ts_node_type(kp), "identifier") == 0)
                    {
                        nm = kp;
                        break;
                    }
                }
                if (ts_node_is_null(nm))
                    break;
                size_t ns2 = ts_node_start_byte(nm), ne2 = ts_node_end_byte(nm);
                rest_shim_name = malloc(ne2 - ns2 + 1);
                memcpy(rest_shim_name, src + ns2, ne2 - ns2);
                rest_shim_name[ne2 - ns2] = '\0';
                rest_shim_idx = i;
                /* Build emitted params: original params text minus the
                   rest_pattern (and its leading comma if not first).
                   Always wrap in parens since `function args (...)` needs
                   parens around params regardless. */
                rp_string *pbuf = rp_string_new(32);
                rp_string_putc(pbuf, '(');
                for (uint32_t k = 0; k < np; k++)
                {
                    if (k == i) continue;
                    TSNode kp = ts_node_named_child(params, k);
                    size_t kps = ts_node_start_byte(kp), kpe = ts_node_end_byte(kp);
                    if (pbuf->len > 1) rp_string_puts(pbuf, ", ");
                    rp_string_putsn(pbuf, src + kps, kpe - kps);
                }
                rp_string_putc(pbuf, ')');
                rest_emitted_params = rp_string_steal(pbuf);
                pbuf = rp_string_free(pbuf);
                break;
            }
        }
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
    if (multi_did)
    {
        /* Multi-param destructure. Build:
             block body:  function (params) { body_inj; original_body; }
             concise:     function (params) { body_inj; return <expr>; } */
        rp_string *rbuf = rp_string_new(128);
        rp_string_puts(rbuf, "function ");
        rp_string_puts(rbuf, multi_params->str);
        if (is_block)
        {
            size_t body_len = body_src_len;
            const char *bsrc = body_src;
            size_t brace = 0;
            while (brace < body_len && bsrc[brace] != '{') brace++;
            if (brace < body_len) brace++;
            rp_string_putc(rbuf, ' ');
            rp_string_putsn(rbuf, bsrc, brace);
            rp_string_puts(rbuf, multi_inj->str);
            rp_string_putsn(rbuf, bsrc + brace, body_len - brace);
        }
        else
        {
            rp_string_puts(rbuf, " { ");
            rp_string_puts(rbuf, multi_inj->str);
            rp_string_puts(rbuf, "return ");
            rp_string_putsn(rbuf, body_src, body_src_len);
            rp_string_puts(rbuf, "; }");
        }
        rep = rp_string_steal(rbuf);
        rbuf = rp_string_free(rbuf);
    }
    else if (did && !is_block)
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
        size_t body_len = body_src_len;
        const char *bsrc = body_src;
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
    else if (rest_shim_name)
    {
        /* Rest-aware arrow rewrite. Emit
             function <params-without-rest> { var <name>=Object.values(arguments).slice(N); <body> }
           Works for block AND concise bodies. */
        rp_string *rbuf = rp_string_new(96);
        rp_string_puts(rbuf, "function ");
        rp_string_puts(rbuf, rest_emitted_params);
        if (is_block)
        {
            const char *bsrc = body_src;
            size_t blen = body_src_len;
            size_t brace = 0;
            while (brace < blen && bsrc[brace] != '{') brace++;
            if (brace < blen) brace++;
            rp_string_putc(rbuf, ' ');
            rp_string_putsn(rbuf, bsrc, brace);
            rp_string_appendf(rbuf, "var %s=Object.values(arguments).slice(%u); ",
                              rest_shim_name, (unsigned)rest_shim_idx);
            rp_string_putsn(rbuf, bsrc + brace, blen - brace);
        }
        else
        {
            rp_string_appendf(rbuf, " { var %s=Object.values(arguments).slice(%u); return ",
                              rest_shim_name, (unsigned)rest_shim_idx);
            rp_string_putsn(rbuf, body_src, body_src_len);
            rp_string_puts(rbuf, "; }");
        }
        rep = rp_string_steal(rbuf);
        rbuf = rp_string_free(rbuf);
    }
    else
    {
        int needs_paren = !slice_starts_with_paren(src, ps, pe);
        size_t cap = 96 + (pe - ps) + body_src_len + 1;

        REMALLOC(rep, cap);

        if (is_block)
        {
            if (needs_paren)
                snprintf(rep, cap, "function (%.*s) %.*s", (int)(pe - ps), src + ps, (int)body_src_len, body_src);
            else
                snprintf(rep, cap, "function %.*s %.*s", (int)(pe - ps), src + ps, (int)body_src_len, body_src);
        }
        else
        {
            if (needs_paren)
                snprintf(rep, cap, "function (%.*s) { return %.*s; }", (int)(pe - ps), src + ps, (int)body_src_len,
                         body_src);
            else
                snprintf(rep, cap, "function %.*s { return %.*s; }", (int)(pe - ps), src + ps, (int)body_src_len,
                         body_src);
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
                            /* Search for the function body's opening `{` AFTER
                               the closing `)` of the parameter list — the
                               first `{` from the start of rep would match the
                               `{` of an object-literal default value
                               (e.g. `function(a, b = {}) {…}`), then
                               pre_block < pc and the size arithmetic below
                               underflows → heap corruption. */
                            char *brace = strchr(pc, '{');
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
    /* Token-fusion guard: if the source byte immediately before the
       arrow's start is an identifier char (letter / digit / `_` / `$`)
       and our replacement begins with one, JS lexer will read them as
       a single token.  Real-world hit: `return (x)=>x` — the `return`
       fuses with `_TrN_Sp` or `function` and parses as a bogus
       identifier `returnFunction` / `return_TrN_Sp`.  Prepend a space
       to keep them as separate tokens. */
    if (ns > 0 && rep && rep[0]) {
        unsigned char pb = (unsigned char)src[ns - 1];
        unsigned char rb = (unsigned char)rep[0];
        int pb_id = (pb >= 'a' && pb <= 'z') || (pb >= 'A' && pb <= 'Z')
                 || (pb >= '0' && pb <= '9') || pb == '_' || pb == '$';
        int rb_id = (rb >= 'a' && rb <= 'z') || (rb >= 'A' && rb <= 'Z')
                 || (rb >= '0' && rb <= '9') || rb == '_' || rb == '$';
        if (pb_id && rb_id) {
            size_t rlen = strlen(rep);
            char *spaced = (char *)malloc(rlen + 2);
            spaced[0] = ' ';
            memcpy(spaced + 1, rep, rlen + 1);
            free(rep);
            rep = spaced;
        }
    }
    add_edit_take_ownership(edits, ns, ne, rep, claimed);
    binds_free(&binds);
    if (param_default)
        free(param_default);
    if (body_subst)
        free(body_subst);
    if (multi_params) multi_params = rp_string_free(multi_params);
    if (multi_inj) multi_inj = rp_string_free(multi_inj);
    if (rest_shim_name) free(rest_shim_name);
    if (rest_emitted_params) free(rest_emitted_params);
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
    /* Reserve headroom for newlines injected between children so the
       transpiled output preserves source line positions. Upper bound:
       one newline per source byte of the literal span. */
    needed += (ts_node_end_byte(arr) - ts_node_start_byte(arr));

    int nshort = 0;
    int nspread = 0;

    for (i = 0; i < cnt1; i++)
    {
        TSNode kid = ts_node_child(arr, i);
        const char *_kt = ts_node_type(kid);
        /* Skip `comment` children. They are named, so they'd otherwise
           fall into the else-if branch below and be emitted as if they
           were property values — which is wrong twice over: comments
           aren't properties, and the rewriter collapses multi-line
           literals onto one line, so a `// line comment` would then
           consume everything that follows it on the line (including
           later properties). */
        if (strcmp(_kt, "comment") == 0)
            continue;
        if (strcmp(_kt, "spread_element") == 0)
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
            if (strcmp(_kt, "shorthand_property_identifier") == 0)
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
    /* For source-line preservation: track end-byte of previous emitted
       child so we can count source newlines between it and the next
       child, and inject them into out.  Initialised to just after the
       opening `{`/`[` so the first child also gets pre-padding. */
    size_t prev_kid_end = ts_node_start_byte(arr) + 1;
    int have_emitted_kid = 0;

    for (i = 0; i < cnt1; i++)
    {
        TSNode kid = ts_node_child(arr, i);
        const char *_kt2 = ts_node_type(kid);

        /* Skip comments — see size-pass note. */
        if (strcmp(_kt2, "comment") == 0)
            continue;

        /* Count source newlines between previous kid end (or the
           opening `{`/`[` for the first kid) and this kid start so we
           can preserve line numbers in the rewrite. */
        (void)have_emitted_kid;
        size_t kid_start_byte = ts_node_start_byte(kid);
        int between_nl = 0;
        if (kid_start_byte > prev_kid_end)
        {
            for (size_t p = prev_kid_end; p < kid_start_byte; p++)
                if (src[p] == '\n') between_nl++;
        }

        if (strcmp(_kt2, "spread_element") == 0)
        {
            cnt2 = ts_node_child_count(kid);
            for (j = 0; j < cnt2; j++)
            {
                TSNode gkid = ts_node_child(kid, j);
                /* Handle any expression after "..." */
                if (ts_node_is_named(gkid) && strcmp(ts_node_type(gkid), "spread_element") != 0)
                {
                    size_t start = ts_node_start_byte(gkid), end = ts_node_end_byte(gkid);
                    /* Pad newlines BEFORE the spread-chunk's connector
                       so the spread starts on its original source line. */
                    for (int z = 0; z < between_nl; z++) addchar('\n');
                    between_nl = 0;
                    addstr(fpref, 32);
                    addstr(src + start, (end - start));
                    addchar(')');
                    addchar(')');
                    lasttype = isspread;
                    prev_kid_end = end;
                    have_emitted_kid = 1;
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
                /* Pad newlines AFTER the comma so the new property's
                   text starts on its original source line. */
                for (int z = 0; z < between_nl; z++) addchar('\n');
            }
            else
            {
                /* Pad newlines BEFORE the `._concat(` connector. */
                for (int z = 0; z < between_nl; z++) addchar('\n');
                addstr("._concat(", 9);
                addchar(open);
            }

            addstr(src + start, (end - start));
            if (strcmp(_kt2, "shorthand_property_identifier") == 0)
            {
                addchar(':');
                addstr(src + start, (end - start));
            }
            addchar(close);
            addchar(')');
            lasttype = isplain;
            prev_kid_end = end;
            have_emitted_kid = 1;
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
/* Walk an array_pattern or object_pattern node and append any binding
   identifier names to `names`. Recurses into nested patterns. */
static void _collect_pattern_names(const char *src, TSNode pattern, rp_string *names, int *first)
{
    const char *pt = ts_node_type(pattern);
    if (strcmp(pt, "identifier") == 0)
    {
        size_t ns = ts_node_start_byte(pattern), ne = ts_node_end_byte(pattern);
        if (!*first)
            rp_string_puts(names, ", ");
        rp_string_putsn(names, src + ns, ne - ns);
        *first = 0;
        return;
    }
    if (strcmp(pt, "rest_pattern") == 0 || strcmp(pt, "rest_element") == 0)
    {
        TSNode inner = ts_node_named_child(pattern, 0);
        if (!ts_node_is_null(inner))
            _collect_pattern_names(src, inner, names, first);
        return;
    }
    if (strcmp(pt, "assignment_pattern") == 0)
    {
        TSNode left = ts_node_child_by_field_name(pattern, "left", 4);
        if (!ts_node_is_null(left))
            _collect_pattern_names(src, left, names, first);
        return;
    }
    if (strcmp(pt, "shorthand_property_identifier_pattern") == 0)
    {
        size_t ns = ts_node_start_byte(pattern), ne = ts_node_end_byte(pattern);
        if (!*first)
            rp_string_puts(names, ", ");
        rp_string_putsn(names, src + ns, ne - ns);
        *first = 0;
        return;
    }
    if (strcmp(pt, "object_assignment_pattern") == 0)
    {
        /* { x = 1 } — the left side is the binding identifier */
        TSNode left = ts_node_child_by_field_name(pattern, "left", 4);
        if (!ts_node_is_null(left))
            _collect_pattern_names(src, left, names, first);
        return;
    }
    if (strcmp(pt, "pair_pattern") == 0)
    {
        /* { a: x } — the value is the binding (possibly another pattern) */
        TSNode val = ts_node_child_by_field_name(pattern, "value", 5);
        if (!ts_node_is_null(val))
            _collect_pattern_names(src, val, names, first);
        return;
    }
    if (strcmp(pt, "array_pattern") == 0 || strcmp(pt, "object_pattern") == 0)
    {
        uint32_t cc = ts_node_named_child_count(pattern);
        for (uint32_t i = 0; i < cc; i++)
            _collect_pattern_names(src, ts_node_named_child(pattern, i), names, first);
        return;
    }
    /* Anything else: don't traverse. */
}

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
            const char *nmt = ts_node_type(nm);
            if (strcmp(nmt, "identifier") == 0)
            {
                size_t ns = ts_node_start_byte(nm), ne = ts_node_end_byte(nm);
                if (!*first)
                    rp_string_puts(names, ", ");
                rp_string_putsn(names, src + ns, ne - ns);
                *first = 0;
            }
            else if (strcmp(nmt, "array_pattern") == 0 || strcmp(nmt, "object_pattern") == 0)
            {
                /* Hoist destructured binding names so the async rewriter can
                   emit them as plain assignments in the destructure-await
                   lowering. Safe in non-async contexts too — extra var
                   declarations at function scope have no observable effect. */
                _collect_pattern_names(src, nm, names, first);
            }
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

/* Returns 1 iff `stmt` is a variable/lexical declaration that needs the
   destructure-await emitter — i.e. it has SOME declarator with an
   await-containing value AND SOME declarator with a destructure pattern.
   The two can be the same declarator (`{a,b} = await x`) or different
   declarators (`[a] = arr, b = await x`). In both cases the existing
   per-statement lowering can't handle the stmt correctly: it would
   either emit invalid `{a,b} = _TrN_context.sent` syntax, or emit naked
   `[a] = arr` (destructure-assignment) which duktape doesn't support. */
static int _stmt_is_destructure_await(TSNode stmt)
{
    const char *st = ts_node_type(stmt);
    if (strcmp(st, "variable_declaration") != 0 &&
        strcmp(st, "lexical_declaration") != 0)
        return 0;
    int has_pattern = 0, has_await = 0;
    uint32_t dc = ts_node_named_child_count(stmt);
    for (uint32_t i = 0; i < dc; i++)
    {
        TSNode decl = ts_node_named_child(stmt, i);
        if (strcmp(ts_node_type(decl), "variable_declarator") != 0)
            continue;
        TSNode name = ts_node_child_by_field_name(decl, "name", 4);
        TSNode val = ts_node_child_by_field_name(decl, "value", 5);
        if (ts_node_is_null(name))
            continue;
        const char *nt = ts_node_type(name);
        if (strcmp(nt, "array_pattern") == 0 || strcmp(nt, "object_pattern") == 0)
            has_pattern = 1;
        if (!ts_node_is_null(val))
        {
            _AsyncNodeVec av = {0};
            _collect_awaits_shallow(val, &av);
            if (av.len > 0) has_await = 1;
            if (av.a) free(av.a);
        }
        if (has_pattern && has_await) return 1;
    }
    return 0;
}

/* Emit state-machine steps for every TOP-LEVEL await in val_node, appending
   to dst. Returns a malloc'd string of the substituted value expression
   (with a unique intermediate `_TrN_context._ts<N>` reference in place of
   each await). Caller frees.

   Each await binds its resolved value to its own `_TrN_context._ts<N>` slot
   (stored on the persistent _context object, not a local var) so that
   later references in the substituted expression don't all collapse to
   the last `_TrN_context.sent` value. Local `var _tsN = _TrN_context.sent` in a
   case label would NOT work: each entry to _TrN_callee$ re-hoists the var
   to `undefined`, losing the assignment from a prior case. Storing on
   _context survives across the multiple _TrN_callee$ invocations.

   That matters for any value with more than one await
   (e.g. `fn(await a, await b)`, `cond ? await a : await b`,
   `(await f)(await x)`).

   Nested awaits (`await f(await g())`) are handled by recursing into
   each await's argument before emitting the outer's case, so inner
   awaits get lower case numbers. */
static char *_emit_value_awaits_lower(rp_string *dst, const char *src, TSNode val_node,
                                      int *p_next_label)
{
    static unsigned _ts_counter = 0;
    _AsyncNodeVec av = {0};
    _collect_awaits_shallow(val_node, &av);
    size_t vs = ts_node_start_byte(val_node), ve = ts_node_end_byte(val_node);

    if (av.len == 0)
    {
        char *r = (char *)malloc(ve - vs + 1);
        memcpy(r, src + vs, ve - vs);
        r[ve - vs] = '\0';
        return r;
    }

    /* sort awaits ascending by start position */
    for (size_t i = 0; i + 1 < av.len; i++)
        for (size_t j = i + 1; j < av.len; j++)
            if (ts_node_start_byte(av.a[j]) < ts_node_start_byte(av.a[i]))
            {
                TSNode t = av.a[i]; av.a[i] = av.a[j]; av.a[j] = t;
            }

    size_t cursor = vs;
    rp_string *acc = rp_string_new(128);

    for (size_t k = 0; k < av.len; k++)
    {
        TSNode aw = av.a[k];
        TSNode arg = ts_node_child_by_field_name(aw, "argument", 8);
        if (ts_node_is_null(arg)) arg = ts_node_named_child(aw, 0);

        /* Recurse into the argument FIRST so any nested awaits (e.g.
           `await fn(await g())`) emit their state-machine cases to `dst`
           BEFORE this outer await's case.  arg_lowered contains the arg
           text with inner awaits replaced by `_TrN_context._tsM` refs. */
        char *arg_lowered = NULL;
        if (!ts_node_is_null(arg))
            arg_lowered = _emit_value_awaits_lower(dst, src, arg, p_next_label);

        *p_next_label += 3;
        char lblbuf[16];
        snprintf(lblbuf, sizeof(lblbuf), "%d", *p_next_label);

        char tsref[32];
        snprintf(tsref, sizeof(tsref), "_TrN_context._ts%u", ++_ts_counter);

        /* ensure dst ends with a terminator */
        if (dst->len)
        {
            char *p = dst->str + dst->len - 1;
            while (p > dst->str && isspace((unsigned char)*p)) p--;
            if (*p != ';' && *p != ':' && *p != '{')
                rp_string_putc(dst, ';');
        }
        rp_string_puts(dst, "_TrN_context._y=true;_TrN_context.next = ");
        rp_string_puts(dst, lblbuf);
        rp_string_puts(dst, "; return (");
        if (arg_lowered)
            rp_string_puts(dst, arg_lowered);
        else
            rp_string_puts(dst, "undefined");
        rp_string_puts(dst, "); case ");
        rp_string_puts(dst, lblbuf);
        rp_string_puts(dst, ": ");
        rp_string_puts(dst, tsref);
        rp_string_puts(dst, " = _TrN_context.sent;");

        if (arg_lowered) free(arg_lowered);

        size_t aws = ts_node_start_byte(aw), awe = ts_node_end_byte(aw);
        rp_string_putsn(acc, src + cursor, aws - cursor);
        rp_string_puts(acc, tsref);
        cursor = awe;
    }
    rp_string_putsn(acc, src + cursor, ve - cursor);

    char *ret = rp_string_steal(acc);
    acc = rp_string_free(acc);
    if (av.a) free(av.a);
    return ret;
}

/* Lower `var/let/const PATTERN = <value-with-await>` into:
     <state-machine steps for each await in value>
     var _daN = <substituted-value>;
     <name1> = _daN.<prop1>;
     <name2> = _daN.<prop2>;
   The destructured names themselves are hoisted at the top of _callee via
   _collect_var_names_recursive so they're plain assignments here.
   For declarators without destructure or without await, emits with the
   keyword stripped (same as _emit_var_decl_as_assignments). */
static void _emit_destructure_await_lower(rp_string *dst, const char *src, TSNode stmt,
                                          int *p_next_label)
{
    uint32_t dc = ts_node_named_child_count(stmt);
    for (uint32_t i = 0; i < dc; i++)
    {
        TSNode decl = ts_node_named_child(stmt, i);
        if (strcmp(ts_node_type(decl), "variable_declarator") != 0)
            continue;
        TSNode name = ts_node_child_by_field_name(decl, "name", 4);
        TSNode val = ts_node_child_by_field_name(decl, "value", 5);
        if (ts_node_is_null(name) || ts_node_is_null(val))
            continue;
        const char *nt = ts_node_type(name);
        int is_pattern = (strcmp(nt, "array_pattern") == 0 ||
                          strcmp(nt, "object_pattern") == 0);

        /* Does this declarator's value contain await? */
        _AsyncNodeVec av = {0};
        _collect_awaits_shallow(val, &av);
        int has_await = (av.len > 0);

        if (is_pattern && has_await)
        {
            char tmpname[24];
            snprintf(tmpname, sizeof(tmpname), "_TrN_da%u", ++_destr_counter);

            /* Emit state-machine steps for each await in the value, then
               substitute `_TrN_context.sent` in for each. Handles single
               embedded awaits like `(await x).y` and sibling awaits like
               `fn(await a, await b)`. Does NOT handle nested awaits like
               `await fn(await y)` — those need a deeper rewrite. */
            char *subst = _emit_value_awaits_lower(dst, src, val, p_next_label);

            rp_string_puts(dst, " var ");
            rp_string_puts(dst, tmpname);
            rp_string_puts(dst, " = ");
            rp_string_puts(dst, subst);
            rp_string_puts(dst, ";");
            free(subst);

            /* Emit destructure expansion. Names hoisted at top of _callee
               (see _collect_var_names_recursive), so emit plain assignments. */
            Bindings binds;
            binds_init(&binds);
            if (collect_flat_destructure_bindings(name, src, tmpname, &binds))
            {
                for (size_t b = 0; b < binds.len; b++)
                {
                    rp_string_puts(dst, " ");
                    rp_string_puts(dst, binds.a[b].name);
                    rp_string_puts(dst, " = ");
                    if (binds.a[b].defval)
                    {
                        rp_string_puts(dst, binds.a[b].repl);
                        rp_string_puts(dst, " !== undefined ? ");
                        rp_string_puts(dst, binds.a[b].repl);
                        rp_string_puts(dst, " : ");
                        rp_string_puts(dst, binds.a[b].defval);
                    }
                    else
                    {
                        rp_string_puts(dst, binds.a[b].repl);
                    }
                    rp_string_puts(dst, ";");
                }
            }
            binds_free(&binds);
        }
        else if (has_await)
        {
            /* identifier = <value-with-await>; share the same multi-await
               helper used for destructure cases. */
            char *subst = _emit_value_awaits_lower(dst, src, val, p_next_label);

            size_t ns_id = ts_node_start_byte(name), ne_id = ts_node_end_byte(name);
            rp_string_puts(dst, " ");
            rp_string_putsn(dst, src + ns_id, ne_id - ns_id);
            rp_string_puts(dst, " = ");
            rp_string_puts(dst, subst);
            rp_string_puts(dst, ";");
            free(subst);
        }
        else
        {
            /* No await — emit as plain assignment if there's an initializer,
               same shape as _emit_var_decl_as_assignments. */
            if (!is_pattern)
            {
                size_t ns_id = ts_node_start_byte(name), ne_id = ts_node_end_byte(name);
                size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
                rp_string_putsn(dst, src + ns_id, ne_id - ns_id);
                rp_string_puts(dst, " = ");
                rp_string_putsn(dst, src + vs, ve - vs);
                rp_string_puts(dst, ";");
            }
            else
            {
                /* Destructure without await — expand inline using a sync
                   temp. Emitting the declarator verbatim would produce
                   `{x} = obj;` which is a parse error inside the state
                   machine (looks like a block-statement followed by junk).
                   Letting the destructuring rewriter handle it on a later
                   pass doesn't help because async clobbers our text. */
                char tmpname[24];
                snprintf(tmpname, sizeof(tmpname), "_TrN_da%u", ++_destr_counter);
                size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
                rp_string_puts(dst, "var ");
                rp_string_puts(dst, tmpname);
                rp_string_puts(dst, " = ");
                rp_string_putsn(dst, src + vs, ve - vs);
                rp_string_puts(dst, ";");

                Bindings binds;
                binds_init(&binds);
                if (collect_flat_destructure_bindings(name, src, tmpname, &binds))
                {
                    for (size_t b = 0; b < binds.len; b++)
                    {
                        rp_string_puts(dst, " ");
                        rp_string_puts(dst, binds.a[b].name);
                        rp_string_puts(dst, " = ");
                        if (binds.a[b].defval)
                        {
                            rp_string_puts(dst, binds.a[b].repl);
                            rp_string_puts(dst, " !== undefined ? ");
                            rp_string_puts(dst, binds.a[b].repl);
                            rp_string_puts(dst, " : ");
                            rp_string_puts(dst, binds.a[b].defval);
                        }
                        else
                        {
                            rp_string_puts(dst, binds.a[b].repl);
                        }
                        rp_string_puts(dst, ";");
                    }
                }
                binds_free(&binds);
            }
        }

        if (av.a) free(av.a);
    }
}

/* Returns the destructure-assignment LHS/RHS pair when `stmt` is an
   expression_statement wrapping `({a,b} = expr)` or `[a,b] = expr` whose RHS
   contains an await. Returns 1 on match and fills *out_left / *out_right;
   returns 0 otherwise. */
static int _stmt_is_destructure_assignment_await(TSNode stmt, TSNode *out_left, TSNode *out_right)
{
    if (strcmp(ts_node_type(stmt), "expression_statement") != 0)
        return 0;
    TSNode expr = ts_node_named_child(stmt, 0);
    if (ts_node_is_null(expr))
        return 0;
    /* Unwrap `({a,b} = expr)` — parenthesized form. */
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
    /* RHS must contain an await. */
    _AsyncNodeVec av = {0};
    _collect_awaits_shallow(right, &av);
    int has_await = (av.len > 0);
    if (av.a) free(av.a);
    if (!has_await) return 0;
    if (out_left) *out_left = left;
    if (out_right) *out_right = right;
    return 1;
}

/* Lower `({a, b} = await EXPR);` into:
     _TrN_context._y=true; _TrN_context.next=K; return (EXPR);
     case K: var _daN = _TrN_context.sent;
     a = _daN.a;
     b = _daN.b;
   The bindings (a, b) already exist — this is assignment, not declaration —
   so no var-hoisting needed for the LHS names. The temp is var-declared
   inline (hoisted by JS to the enclosing _callee). */
static void _emit_destructure_assignment_await_lower(rp_string *dst, const char *src,
                                                    TSNode left, TSNode right,
                                                    int *p_next_label)
{
    char tmpname[24];
    snprintf(tmpname, sizeof(tmpname), "_TrN_da%u", ++_destr_counter);

    /* Emit state-machine steps for awaits in RHS; subst is the RHS with
       _TrN_context.sent substituted in. Handles embedded and sibling awaits. */
    char *subst = _emit_value_awaits_lower(dst, src, right, p_next_label);

    rp_string_puts(dst, " var ");
    rp_string_puts(dst, tmpname);
    rp_string_puts(dst, " = ");
    rp_string_puts(dst, subst);
    rp_string_puts(dst, ";");
    free(subst);

    /* Emit binding assignments using existing bindings (no var). */
    Bindings binds;
    binds_init(&binds);
    if (collect_flat_destructure_bindings(left, src, tmpname, &binds))
    {
        for (size_t b = 0; b < binds.len; b++)
        {
            rp_string_puts(dst, " ");
            rp_string_puts(dst, binds.a[b].name);
            rp_string_puts(dst, " = ");
            if (binds.a[b].defval)
            {
                rp_string_puts(dst, binds.a[b].repl);
                rp_string_puts(dst, " !== undefined ? ");
                rp_string_puts(dst, binds.a[b].repl);
                rp_string_puts(dst, " : ");
                rp_string_puts(dst, binds.a[b].defval);
            }
            else
            {
                rp_string_puts(dst, binds.a[b].repl);
            }
            rp_string_puts(dst, ";");
        }
    }
    binds_free(&binds);
}

/* Set non-zero while emitting the body of an `async function*`. Causes
   `_lower_range_with_yields` to wrap each await_expression's argument in
   `_TrN_Sp.__await(...)` so the wrapper distinguishes await-markers from
   plain yielded values. Safe across nested function boundaries because
   each nested function gets its own separate body-emission call (and
   `_collect_yields_shallow` stops at function boundaries). */
static int _g_in_async_gen = 0;

/* Forward decl for the JS-string-literal emitter, defined down by the
   rewriter pass — used earlier by the class-method fn-source emission
   to attach a `src:"..."` field to method descriptors. */
static void emit_js_string_literal(rp_string *out, const char *src, size_t s, size_t e);

/* Forward decls so the async dispatcher can delegate to the unified
   yield/await lowering machinery defined further down. The actual
   LoopCtx/FinCtx structs are defined later; only the typedef'd-pointer
   parameters are used here, so a struct-tag forward decl suffices. */
struct LoopCtx;
struct FinCtx;
static void _emit_yield_body(rp_string *out, const char *src, TSNode block,
                             struct LoopCtx *ctx, struct FinCtx *fctx, int *p_next_label);
static void _emit_stmt_yield_lower(rp_string *dst, const char *src, size_t ss, size_t se, TSNode stmt_node,
                                   struct LoopCtx *ctx, struct FinCtx *fctx, int *p_next_label);

// Lower a statement containing 0..N awaits into state-machine steps.
// Currently unused; the structural lowering pipeline emits these inline.
static __attribute__((unused)) void
_emit_stmt_async_lower(rp_string *dst, const char *src, size_t ss, size_t se, TSNode stmt_node,
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
        rp_string_puts(dst, "_TrN_context._y=true;_TrN_context.next = ");
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
        rp_string_puts(acc, "_TrN_context.sent");
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
// _TrN_callee$(_context){while(1){switch(_TrN_context.prev=_TrN_context.next){case 0: ... }} , _callee);
static char *_build_regenerator_switch_body(const char *src, TSNode body)
{
    rp_string *out = rp_string_new(384);

    int next_label = 0;

    // Hoist var/let/const declarations so they persist across _TrN_callee$ invocations via closure
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
        "return _TrN_Sp.regeneratorRuntime.wrap(function _TrN_callee$(_TrN_context){while(1){switch(_TrN_context.prev=_TrN_context.next){case 0:");
    const char *bt = ts_node_type(body);
    if (strcmp(bt, "statement_block") == 0)
    {
        /* Delegate to the unified statement dispatcher.  _emit_yield_body
           collects yield_expression AND await_expression nodes (their
           state-machine transitions are identical) and structurally
           lowers loops / if / try / switch / labelled so awaits inside
           loops fire on each iteration.  Async-specific destructure
           handling falls back to the wrap_paren path which is correct
           for the common `const {a,b} = await X;` shape. */
        _emit_yield_body(out, src, body, NULL /* loop */, NULL /* finally */, &next_label);
    }
    else
    {
        // Concise arrow body: the expression is an implicit return.
        TSNode expr = body;
        size_t ss = ts_node_start_byte(expr), se = ts_node_end_byte(expr);
        rp_string *tmp = rp_string_new(64);
        /* _emit_stmt_yield_lower now handles awaits too (the collector
           covers both yield_expression and await_expression). */
        _emit_stmt_yield_lower(tmp, src, ss, se, expr, NULL, NULL, &next_label);
        if (strstr(tmp->str, "_TrN_context.next") == NULL)
        {
            rp_string_puts(out, " return ");
            rp_string_puts(out, tmp->str);
            rp_string_puts(out, ";");
        }
        else
        {
            // The await was lowered. The last segment (after the final "case N:")
            // contains _TrN_context.sent which is the value to implicitly return.
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
    rp_string_puts(out, ":case \"end\":return _TrN_context.stop();}}}, _TrN_callee, this);");
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
        /* Arrow with bare-identifier param `x => …` has no `parameters`
           field — the single `parameter` field is the identifier.
           Pre-fix, this branch dereferenced `params` (null), segfault. */
        size_t s = ts_node_start_byte(param), e = ts_node_end_byte(param);
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
    rp_string_puts(out, " = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function _TrN_callee");
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
    rp_string_puts(out, "(function(){var _TrN_ref = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function _TrN_callee");
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
    rp_string_puts(out, "})); return function(){ return _TrN_ref.apply(this, arguments); };})()");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

static char *_emit_async_method_replacement(const char *src, TSNode node)
{
    /* A method_definition node lives inside either:
       (a) an object literal — emit `name: (function(){...})()`
           (object-property form), or
       (b) a class body — emit `name(params){ body }` (method form);
           the `name:value` form is a syntax error in class bodies. */
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    TSNode nname = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(body) || ts_node_is_null(nname))
        return NULL;
    size_t ns = ts_node_start_byte(nname), ne = ts_node_end_byte(nname);
    TSNode parent = ts_node_parent(node);
    int in_class_body = !ts_node_is_null(parent)
                     && strcmp(ts_node_type(parent), "class_body") == 0;
    rp_string *out = rp_string_new(512);

    const char *nt = ts_node_type(nname);
    int named = (strcmp(nt, "property_identifier") == 0 || strcmp(nt, "identifier") == 0);

    if (in_class_body)
    {
        /* Class method form: name(params){ var _TrN_ref = ...; return _TrN_ref.apply(this, arguments); } */
        rp_string_putsn(out, src+ns, ne-ns);
        _append_params_sig(out, src, node);
        rp_string_puts(out, " { var _TrN_ref = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function ");
        if (named) rp_string_putsn(out, src+ns, ne-ns);
        else       rp_string_puts(out, "_TrN_callee");
        _append_params_sig(out, src, node);
        rp_string_puts(out, " {");
        char *wrap = _build_regenerator_switch_body(src, body);
        if (!wrap) { out = rp_string_free(out); return NULL; }
        rp_string_puts(out, wrap);
        free(wrap);
        rp_string_puts(out, "})); return _TrN_ref.apply(this, arguments); }");
    }
    else
    {
        /* Object-property form: name: (function(){ ... })() */
        rp_string_putsn(out, src+ns, ne-ns);
        rp_string_puts(out, ": ");
        rp_string_puts(out, "(function(){var _TrN_ref = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function ");
        if (named) rp_string_putsn(out, src+ns, ne-ns);
        else       rp_string_puts(out, "_TrN_callee");
        _append_params_sig(out, src, node);
        rp_string_puts(out, " {");
        char *wrap = _build_regenerator_switch_body(src, body);
        if (!wrap) { out = rp_string_free(out); return NULL; }
        rp_string_puts(out, wrap);
        free(wrap);
        rp_string_puts(out, "})); return function ");
        if (named) rp_string_putsn(out, src+ns, ne-ns);
        else       rp_string_puts(out, "_TrN_callee");
        _append_params_sig(out, src, node);
        rp_string_puts(out, " { return _TrN_ref.apply(this, arguments); };})()");
    }

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

/* Per-loop context tracked during yield lowering so that break/continue
   inside a loop body translate into state-machine transitions rather
   than `break`-out-of-switch (which would leave _TrN_context.next unchanged
   and spin forever in the outer `while(1)`).

   If the loop is the body of a `labeled_statement`, `label`/`label_len`
   identify the source bytes of the label so `break LABEL` /
   `continue LABEL` inside can target the right enclosing loop. */
typedef struct LoopCtx {
    int continue_case;  /* where `continue` jumps */
    int exit_case;      /* where `break` jumps */
    const char *label;  /* NULL if loop is unlabelled */
    size_t label_len;
    struct LoopCtx *outer;
} LoopCtx;

/* Per-try-finally context. When a `return X;` appears inside the try
   body, it must route through the finally before actually returning;
   the runtime stores the deferred value in `_TrN_context.rval` and sets
   `_TrN_context._rret = true`, then jumps to finally_case.  The finally
   body, after running, checks `_TrN_context._rret` and returns the saved
   value (which triggers the iterator-end path in the wrap). */
typedef struct FinCtx {
    int finally_case;
    struct FinCtx *outer;
} FinCtx;

/* Collect unlabelled break/continue/return nodes in `node`'s subtree.
   break/continue target the immediately enclosing loop; return targets
   the enclosing function (and any try-finally on the way must run
   cleanup first). Skip nested loops, switches, and nested function/
   class bodies (they have their own scopes). `rets` may be NULL when
   no try-finally is in scope — return then needs no translation. */
static void _collect_control(TSNode node, _AsyncNodeVec *brks, _AsyncNodeVec *conts, _AsyncNodeVec *rets)
{
    const char *t = ts_node_type(node);
    if (strcmp(t, "break_statement") == 0) {
        /* Collect both labelled and unlabelled — substitution walks
           outer LoopCtx chain by label, or uses the immediate ctx for
           unlabelled. */
        if (brks) _anv_push(brks, node);
        return;
    }
    if (strcmp(t, "continue_statement") == 0) {
        if (conts) _anv_push(conts, node);
        return;
    }
    if (strcmp(t, "return_statement") == 0) {
        /* Only collect if caller cares (i.e. an enclosing finally needs
           to run before the return takes effect).  Don't descend into
           the return statement; yields inside its argument are picked
           up independently by _collect_yields_shallow. */
        if (rets) _anv_push(rets, node);
        return;
    }
    if (strcmp(t, "while_statement") == 0 ||
        strcmp(t, "for_statement") == 0 ||
        strcmp(t, "do_statement") == 0 ||
        strcmp(t, "for_in_statement") == 0 ||
        strcmp(t, "switch_statement") == 0)
    {
        /* Don't descend into nested loops/switches for break/continue,
           but DO collect returns inside them (return still targets the
           enclosing function). */
        if (rets) {
            uint32_t c = ts_node_child_count(node);
            for (uint32_t i = 0; i < c; i++)
                _collect_control(ts_node_child(node, i), NULL, NULL, rets);
        }
        return;
    }
    if (strstr(t, "function") || strcmp(t, "arrow_function") == 0 ||
        strstr(t, "class") || strstr(t, "method") != NULL)
        return;
    uint32_t c = ts_node_child_count(node);
    for (uint32_t i = 0; i < c; i++)
        _collect_control(ts_node_child(node, i), brks, conts, rets);
}

/* Back-compat wrapper for sites that don't care about returns. */
static __attribute__((unused)) void
_collect_loop_control(TSNode node, _AsyncNodeVec *brks, _AsyncNodeVec *conts)
{
    _collect_control(node, brks, conts, NULL);
}

/* Collect yield_expression AND await_expression nodes — both lower to
   the same state-machine transition shape (`_TrN_context.next=N; return X;
   case N: _TrN_context.sent`).  An async function body never contains
   yields and a generator body never contains awaits (each is a syntax
   error in the other context), so combining them in one collector is
   safe. */
static void _collect_yields_shallow(TSNode node, _AsyncNodeVec *out)
{
    const char *t = ts_node_type(node);
    if (strcmp(t, "yield_expression") == 0 || strcmp(t, "await_expression") == 0)
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

/* Quick text scan for `yield` or `await` keyword in a source range —
   used to short-circuit lowering for statements that contain neither.
   Substring match is OK because false positives just route to the
   structural lowering path which then no-ops on no actual matches. */
static int _text_has_yield(const char *src, size_t ss, size_t se)
{
    for (size_t k = ss; k + 5 <= se; k++)
    {
        if (src[k] == 'y' && src[k+1] == 'i' && src[k+2] == 'e'
            && src[k+3] == 'l' && src[k+4] == 'd')
            return 1;
        if (src[k] == 'a' && src[k+1] == 'w' && src[k+2] == 'a'
            && src[k+3] == 'i' && src[k+4] == 't')
            return 1;
    }
    return 0;
}

/* Helper: lower yields in src range [ss..se] (with `container` as the
   tree-sitter subtree covering it). Emits each yield's state-machine
   transition into `dst`. Returns a malloc'd string that is the original
   source with yield expressions replaced by `_TrN_context.sent`. Handles
   nested yields (e.g. `yield yield __await(v)`) by recursing into each
   yield's argument first, so inner yields get lower case numbers and
   their post-resume value (`_TrN_context.sent`) becomes the outer yield's
   actual return-argument.

   When `ctx` is non-NULL, also substitutes unlabelled break/continue
   statements in the range with state-machine transitions to the
   enclosing loop's exit/continue case.

   When `fctx` is non-NULL, return statements in the range are
   translated to set `_TrN_context.rval` and route through the enclosing
   finally case before actually returning. */
static char *_lower_range_with_yields(rp_string *dst, const char *src,
                                      size_t ss, size_t se, TSNode container,
                                      LoopCtx *ctx, FinCtx *fctx,
                                      int *p_next_label)
{
    _AsyncNodeVec av = {0};
    _collect_yields_shallow(container, &av);

    /* If a loop ctx is in scope, also collect unlabelled break/continue.
       If a finally ctx is in scope, also collect return statements. */
    _AsyncNodeVec brks = {0}, conts = {0}, rets = {0};
    if (ctx || fctx)
        _collect_control(container, ctx ? &brks : NULL, ctx ? &conts : NULL,
                         fctx ? &rets : NULL);

    if (av.len == 0 && brks.len == 0 && conts.len == 0 && rets.len == 0)
    {
        size_t len = se - ss;
        char *out = NULL;
        REMALLOC(out, len + 1);
        memcpy(out, src + ss, len);
        out[len] = '\0';
        if (av.a) free(av.a);
        if (brks.a) free(brks.a);
        if (conts.a) free(conts.a);
        if (rets.a) free(rets.a);
        return out;
    }

    /* Sort yields by start ascending. */
    for (size_t i = 0; i + 1 < av.len; i++)
        for (size_t j = i + 1; j < av.len; j++)
            if (ts_node_start_byte(av.a[j]) < ts_node_start_byte(av.a[i]))
            {
                TSNode t = av.a[i];
                av.a[i] = av.a[j];
                av.a[j] = t;
            }

    /* When multiple yields appear at this level (e.g. `(yield A) + (yield B)`)
       each needs its own slot, because `_TrN_context.sent` is overwritten on
       every re-entry and the later expression can't read the earlier
       sent value. Use `_TrN_context.s<N>` (a fresh property on the runtime
       context object, indexed by case number) as the slot. For the
       single-yield case the slot is unnecessary and we just use
       `_TrN_context.sent` directly to keep the output minimal. */
    int use_slots = (av.len > 1);

    /* Build a merged sorted list of substitution sites (yields +
       break/continue/return), so the accumulator is built in source
       order.  Returns are processed BEFORE any yields nested inside
       their argument expression (return's start byte < arg yield's
       start byte); the return substitution lowers its argument
       recursively, which emits the inner yield's case to `dst` and
       leaves the outer iteration to skip the inner yield (its start
       byte falls inside the already-advanced cursor). */
    typedef struct { TSNode n; int kind; } SubItem;
    /* kind: 0 = yield, 1 = break, 2 = continue, 3 = return */
    size_t total = av.len + brks.len + conts.len + rets.len;
    SubItem *items = NULL;
    REMALLOC(items, sizeof(SubItem) * (total + 1));
    size_t ik = 0;
    for (size_t i = 0; i < av.len; i++)    { items[ik].n = av.a[i];    items[ik].kind = 0; ik++; }
    for (size_t i = 0; i < brks.len; i++)  { items[ik].n = brks.a[i];  items[ik].kind = 1; ik++; }
    for (size_t i = 0; i < conts.len; i++) { items[ik].n = conts.a[i]; items[ik].kind = 2; ik++; }
    for (size_t i = 0; i < rets.len; i++)  { items[ik].n = rets.a[i];  items[ik].kind = 3; ik++; }
    for (size_t i = 0; i + 1 < total; i++)
        for (size_t j = i + 1; j < total; j++)
            if (ts_node_start_byte(items[j].n) < ts_node_start_byte(items[i].n))
            {
                SubItem t = items[i]; items[i] = items[j]; items[j] = t;
            }

    rp_string *acc = rp_string_new(64);
    size_t cursor = ss;

    for (size_t k = 0; k < total; k++)
    {
        TSNode node = items[k].n;
        size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);

        /* If this item's range starts before the current cursor, an
           outer-processed item already consumed it (e.g. a return whose
           argument contained this yield).  Skip. */
        if (ns < cursor)
            continue;

        /* Copy chunk before this substitution site. */
        if (ns > cursor)
            rp_string_putsn(acc, src + cursor, ns - cursor);
        cursor = ne;

        if (items[k].kind == 0)
        {
            /* yield_expression */
            TSNode yw = node;
            /* Detect `yield*` (delegating yield) by scanning the source
               bytes between the `yield` keyword and the argument. */
            int is_star = 0;
            {
                size_t p2 = ns + 5; /* past "yield" */
                while (p2 < ne && isspace((unsigned char)src[p2])) p2++;
                if (p2 < ne && src[p2] == '*')
                    is_star = 1;
            }

            /* Recurse into the arg FIRST so any nested yields emit their
               cases to `dst` BEFORE this yield's case (i.e. innermost gets
               the lower case number). The recursion returns the substituted
               arg text. */
            TSNode arg = ts_node_named_child(yw, 0);
            char *arg_lowered = NULL;
            if (!ts_node_is_null(arg))
            {
                size_t as = ts_node_start_byte(arg), ae = ts_node_end_byte(arg);
                arg_lowered = _lower_range_with_yields(dst, src, as, ae, arg, ctx, fctx, p_next_label);
            }

            if (is_star)
            {
                /* yield* iter — delegate: walk iter and yield each value.

                   Sync-gen mode (`_g_in_async_gen == 0`):
                     _ctx._ys<N> = _TrN_Sp._iter(<arg>);
                     case LOOP:
                        _ctx._yr<N> = _ctx._ys<N>.next();
                        if (_ctx._yr<N>.done) { _ctx.next=DONE; break; }
                        _ctx._y=true;
                        _ctx.next=RESUME;
                        return _ctx._yr<N>.value;
                     case RESUME: _ctx.next=LOOP; break;
                     case DONE:

                   Async-gen mode (await every step):
                     _ctx._ys<N> = _TrN_Sp._asyncIter(<arg>);
                     case LOOP_NEXT:
                        _ctx._y=true; _ctx.next=AFTER_NEXT;
                        return __await(_ctx._ys<N>.next());
                     case AFTER_NEXT:
                        _ctx._yr<N> = _ctx.sent;
                        if (_ctx._yr<N>.done) { _ctx.next=DONE; break; }
                        _ctx._y=true; _ctx.next=RESUME;
                        return _ctx._yr<N>.value;
                     case RESUME: _ctx.next=LOOP_NEXT; break;
                     case DONE: */
                int async_mode = _g_in_async_gen;

                *p_next_label += 3;
                int loop_label = *p_next_label;
                int after_next_label = 0;
                if (async_mode)
                {
                    *p_next_label += 3;
                    after_next_label = *p_next_label;
                }
                *p_next_label += 3;
                int resume_label = *p_next_label;
                *p_next_label += 3;
                int done_label = *p_next_label;
                char iter_slot[32], step_slot[32];
                snprintf(iter_slot, sizeof(iter_slot), "_TrN_context._ys%d", loop_label);
                snprintf(step_slot, sizeof(step_slot), "_TrN_context._yr%d", loop_label);

                if (dst->len)
                {
                    char *p = dst->str + dst->len - 1;
                    while (p > dst->str && isspace(*p)) p--;
                    if (*p != ';' && *p != ':' && *p != '{')
                        rp_string_putc(dst, ';');
                }
                rp_string_appendf(dst, "%s=_TrN_Sp.%s(",
                                  iter_slot, async_mode ? "_asyncIter" : "_iter");
                if (arg_lowered) rp_string_puts(dst, arg_lowered);
                else             rp_string_puts(dst, "undefined");
                rp_string_puts(dst, ");");

                if (async_mode)
                {
                    /* LOOP_NEXT: await iter.next() */
                    rp_string_appendf(dst, "case %d:_TrN_context._y=true;_TrN_context.next=%d;"
                                           "return _TrN_Sp.__await(%s.next());",
                                      loop_label, after_next_label, iter_slot);
                    /* AFTER_NEXT: store step, check done, yield value */
                    rp_string_appendf(dst, "case %d:%s=_TrN_context.sent;"
                                           "if(%s.done){_TrN_context.next=%d;break;}"
                                           "_TrN_context._y=true;_TrN_context.next=%d;return %s.value;",
                                      after_next_label, step_slot, step_slot,
                                      done_label, resume_label, step_slot);
                }
                else
                {
                    /* Sync-gen: just .next() inline. */
                    rp_string_appendf(dst, "case %d:%s=%s.next();if(%s.done){_TrN_context.next=%d;break;}",
                                      loop_label, step_slot, iter_slot, step_slot, done_label);
                    rp_string_appendf(dst, "_TrN_context._y=true;_TrN_context.next=%d;return %s.value;",
                                      resume_label, step_slot);
                }
                rp_string_appendf(dst, "case %d:_TrN_context.next=%d;break;case %d:",
                                  resume_label, loop_label, done_label);

                if (arg_lowered) free(arg_lowered);
                /* yield* result is the iterator's return value; expose it. */
                rp_string_appendf(acc, "(%s.value)", step_slot);
                continue;
            }

            *p_next_label += 3;
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "%d", *p_next_label);

            char slot_name[40];
            if (use_slots)
                snprintf(slot_name, sizeof(slot_name), "_TrN_context.s%s", tmp);
            else
                strcpy(slot_name, "_TrN_context.sent");

            if (dst->len)
            {
                char *p = dst->str + dst->len - 1;
                while (p > dst->str && isspace(*p)) p--;
                if (*p != ';') rp_string_putc(dst, ';');
            }
            /* In `async function*` bodies, mark await_expression args with
               `_TrN_Sp.__await(...)` so the __asyncGenerator wrapper
               distinguishes them from plain yielded values. yield_expression
               stays unwrapped — its value goes to the consumer directly. */
            int is_await_in_async_gen = 0;
            if (_g_in_async_gen)
            {
                const char *nt = ts_node_type(node);
                if (strcmp(nt, "await_expression") == 0)
                    is_await_in_async_gen = 1;
            }
            rp_string_puts(dst, "_TrN_context._y=true;_TrN_context.next = ");
            rp_string_puts(dst, tmp);
            rp_string_puts(dst, "; return (");
            if (is_await_in_async_gen)
                rp_string_puts(dst, "_TrN_Sp.__await(");
            if (arg_lowered)
                rp_string_puts(dst, arg_lowered);
            else
                rp_string_puts(dst, "undefined");
            if (is_await_in_async_gen)
                rp_string_puts(dst, ")");
            rp_string_puts(dst, ");");
            rp_string_puts(dst, " case ");
            rp_string_puts(dst, tmp);
            rp_string_puts(dst, ":");
            if (use_slots)
            {
                rp_string_puts(dst, slot_name);
                rp_string_puts(dst, "=_TrN_context.sent;");
            }

            if (arg_lowered) free(arg_lowered);

            rp_string_puts(acc, slot_name);
        }
        else if (items[k].kind == 3)
        {
            /* return_statement: route via the enclosing finally before
               actually returning.  If the return has a value, lower it
               (yields in the arg emit their case-transitions to dst) and
               store in _TrN_context.rval.  Then jump to the finally case;
               the finally's tail will return _TrN_context.rval to the wrap. */
            TSNode ret = node;
            TSNode arg = ts_node_named_child(ret, 0);
            char *arg_lowered = NULL;
            if (!ts_node_is_null(arg))
            {
                size_t as = ts_node_start_byte(arg), ae = ts_node_end_byte(arg);
                arg_lowered = _lower_range_with_yields(dst, src, as, ae, arg, ctx, fctx, p_next_label);
            }
            rp_string_puts(acc, "{");
            if (arg_lowered)
            {
                rp_string_puts(acc, "_TrN_context.rval=");
                rp_string_puts(acc, arg_lowered);
                rp_string_puts(acc, ";");
            }
            rp_string_appendf(acc, "_TrN_context._rret=true;_TrN_context.next=%d;break;}",
                              fctx->finally_case);
            if (arg_lowered) free(arg_lowered);
        }
        else
        {
            /* break or continue; may be labelled.  For unlabelled, target
               the immediately enclosing LoopCtx.  For labelled, walk the
               LoopCtx outer chain looking for a matching label; fall back
               to the innermost if not found.  For unlabelled `continue`,
               skip switch-flavored ctxs (continue_case == -1) — they're
               only there to capture `break`. */
            TSNode stmt_node = node;
            int is_continue = (items[k].kind == 2);
            LoopCtx *target_ctx = ctx;
            if (ts_node_named_child_count(stmt_node) > 0)
            {
                TSNode label_node = ts_node_named_child(stmt_node, 0);
                size_t ls = ts_node_start_byte(label_node);
                size_t le = ts_node_end_byte(label_node);
                size_t llen = le - ls;
                LoopCtx *walk = ctx;
                target_ctx = NULL;
                while (walk)
                {
                    if (walk->label && walk->label_len == llen &&
                        memcmp(walk->label, src + ls, llen) == 0)
                    {
                        target_ctx = walk;
                        break;
                    }
                    walk = walk->outer;
                }
            }
            else if (is_continue && target_ctx && target_ctx->continue_case < 0)
            {
                /* Unlabelled continue inside a switch — walk past
                   switches to the enclosing loop. */
                LoopCtx *walk = target_ctx->outer;
                target_ctx = NULL;
                while (walk)
                {
                    if (walk->continue_case >= 0) { target_ctx = walk; break; }
                    walk = walk->outer;
                }
            }
            if (target_ctx)
            {
                int tgt = (items[k].kind == 1) ? target_ctx->exit_case : target_ctx->continue_case;
                rp_string_appendf(acc, "{_TrN_context.next=%d;break;}", tgt);
            }
            else
            {
                /* No matching ctx in scope; emit the original text so JS
                   reports a sensible "label not found" at runtime rather
                   than silently dropping. */
                rp_string_putsn(acc, src + ns, ne - ns);
            }
        }
    }
    if (cursor < se)
        rp_string_putsn(acc, src + cursor, se - cursor);

    char *result = rp_string_steal(acc);
    acc = rp_string_free(acc);
    free(items);
    if (av.a) free(av.a);
    if (brks.a) free(brks.a);
    if (conts.a) free(conts.a);
    if (rets.a) free(rets.a);
    return result;
}

// Lower a statement containing 0..N yields (including nested) into
// state-machine steps. Emits case-transitions to `dst` and appends the
// substituted statement (yields replaced by `_TrN_context.sent`).
static void _emit_stmt_yield_lower(rp_string *dst, const char *src, size_t ss, size_t se, TSNode stmt_node,
                                   LoopCtx *ctx, FinCtx *fctx, int *p_next_label)
{
    char *lowered = _lower_range_with_yields(dst, src, ss, se, stmt_node, ctx, fctx, p_next_label);
    rp_string_puts(dst, lowered);
    size_t lowered_len = strlen(lowered);
    if (lowered_len == 0 || lowered[lowered_len - 1] != ';')
        rp_string_puts(dst, ";");
    free(lowered);
}

/* Process children of a statement_block, lowering yields into state-machine
   cases.  Recursively decomposes while/for loops that contain yields.
   `ctx` (if non-NULL) carries the enclosing-loop labels so break/continue
   inside the body translate into state transitions.  `fctx` (if non-NULL)
   carries the enclosing try-finally so `return X;` inside routes through
   the finally before actually returning. */
static void _emit_yield_body_range(rp_string *out, const char *src, TSNode block,
                                   uint32_t child_start, uint32_t child_end,
                                   LoopCtx *ctx, FinCtx *fctx, int *p_next_label)
{
    uint32_t sc = child_end;
    for (uint32_t i = child_start; i < sc; i++)
    {
        TSNode stmt = ts_node_named_child(block, i);
        size_t ss = ts_node_start_byte(stmt), se = ts_node_end_byte(stmt);
        const char *stmt_type = ts_node_type(stmt);

        /* labeled_statement: unwrap to its body for dispatch and record
           the label so the contained loop's LoopCtx can carry it.
           `break LABEL` / `continue LABEL` inside then resolve to that
           loop's exit / continue case via the LoopCtx outer chain. */
        const char *pending_label = NULL;
        size_t pending_label_len = 0;
        if (strcmp(stmt_type, "labeled_statement") == 0)
        {
            TSNode lbl = ts_node_child_by_field_name(stmt, "label", 5);
            TSNode body_node = ts_node_child_by_field_name(stmt, "body", 4);
            if (!ts_node_is_null(lbl) && !ts_node_is_null(body_node))
            {
                pending_label = src + ts_node_start_byte(lbl);
                pending_label_len = ts_node_end_byte(lbl) - ts_node_start_byte(lbl);
                stmt = body_node;
                stmt_type = ts_node_type(stmt);
                ss = ts_node_start_byte(stmt);
                se = ts_node_end_byte(stmt);
            }
        }

        int is_var_decl = (strcmp(stmt_type, "variable_declaration") == 0 ||
                           strcmp(stmt_type, "lexical_declaration") == 0);
        int has_yield = _text_has_yield(src, ss, se);

        /* Back up whitespace for line-number preservation */
        while (ss > 0 && isspace(*(src + ss - 1)))
            ss--;

        /* Pre-filter: multi-declarator destructure-await like
           `const a = 1, {b} = await X;` — the generic strip-keyword +
           paren-wrap below can't represent this with a single
           `_TrN_context.sent` substitution.  Route to the dedicated emitter
           which fans the declarators out into individual statements.
           Also catches single-declarator destructure-await; the dedicated
           path emits cleaner code than the generic wrap_paren. */
        if (is_var_decl && has_yield && _stmt_is_destructure_await(stmt))
        {
            size_t stmt_s = ts_node_start_byte(stmt);
            if (ss < stmt_s)
                rp_string_putsn(out, src + ss, stmt_s - ss);
            _emit_destructure_await_lower(out, src, stmt, p_next_label);
            continue;
        }

        /* Pre-filter: destructure-assignment-await `({a,b} = await X);` —
           the LHS uses existing bindings (no var/let/const). */
        {
            TSNode da_left, da_right;
            if (has_yield && _stmt_is_destructure_assignment_await(stmt, &da_left, &da_right))
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s)
                    rp_string_putsn(out, src + ss, stmt_s - ss);
                _emit_destructure_assignment_await_lower(out, src, da_left, da_right, p_next_label);
                continue;
            }
        }

        /* Strip var/let/const keyword for declarations containing yield.
           Also wrap the result in parens when the declarator's name is a
           destructure pattern — bare `{a,b} = X;` parses as block-then-
           assignment, which is invalid; `({a,b} = X);` is the
           destructuring-assignment expression form. */
        int wrap_paren = 0;
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

                /* Check if the declarator's name is a destructure pattern.
                   Don't emit `(` yet — the state-machine transitions from
                   the yield's lowering must land at switch-statement scope,
                   not inside parens.  We emit `(<text>);` after the
                   linearisation completes. */
                TSNode dname = ts_node_child_by_field_name(first_decl, "name", 4);
                if (!ts_node_is_null(dname))
                {
                    const char *dnt = ts_node_type(dname);
                    if (strcmp(dnt, "object_pattern") == 0 ||
                        strcmp(dnt, "array_pattern") == 0)
                        wrap_paren = 1;
                }
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
            /* Preserve leading whitespace so the assignment lands on the
               same source line as the original `var`/`let`/`const`. */
            size_t stmt_s = ts_node_start_byte(stmt);
            if (ss < stmt_s)
                rp_string_putsn(out, src + ss, stmt_s - ss);
            _emit_var_decl_as_assignments(out, src, stmt);
        }
        else if (strcmp(stmt_type, "break_statement") == 0 && ctx
                 && ts_node_named_child_count(stmt) == 0)
        {
            /* Unlabelled break inside a yield-lowered loop body. Bare
               `break` would only exit the surrounding switch, leaving
               _TrN_context.next unchanged — spin forever. Translate. */
            rp_string_appendf(out, "{_TrN_context.next=%d;break;}", ctx->exit_case);
        }
        else if (strcmp(stmt_type, "continue_statement") == 0 && ctx
                 && ts_node_named_child_count(stmt) == 0)
        {
            rp_string_appendf(out, "{_TrN_context.next=%d;break;}", ctx->continue_case);
        }
        else if (strcmp(stmt_type, "while_statement") == 0 && has_yield)
        {
            /* Preserve leading whitespace so the `case COND:` lands on
               the original `while` source line. */
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s) rp_string_putsn(out, src + ss, stmt_s - ss);
            }
            /* Decompose: while (cond) { body }
               -> case COND: if(!cond){_TrN_context.next=EXIT;break;}
                  <body stmts with yields lowered>
                  _TrN_context.next=COND;break;
                  case EXIT: */
            TSNode cond = ts_node_child_by_field_name(stmt, "condition", 9);
            TSNode wbody = ts_node_child_by_field_name(stmt, "body", 4);

            *p_next_label += 3;
            int cond_label = *p_next_label;
            char ctmp[24];
            snprintf(ctmp, sizeof(ctmp), "%d", cond_label);

            /* Allocate exit label BEFORE processing body so break inside
               can translate to a jump to it. */
            *p_next_label += 3;
            int exit_label = *p_next_label;
            char etmp[24];
            snprintf(etmp, sizeof(etmp), "%d", exit_label);

            LoopCtx wctx = { cond_label, exit_label, pending_label, pending_label_len, ctx };

            /* Process body */
            rp_string *wbuf = rp_string_new(256);
            if (!ts_node_is_null(wbody))
            {
                if (strcmp(ts_node_type(wbody), "statement_block") == 0)
                    _emit_yield_body(wbuf, src, wbody, &wctx, fctx, p_next_label);
                else
                {
                    /* Single-statement body (no braces). Lower as a single
                       statement with the loop ctx in scope. */
                    size_t bs = ts_node_start_byte(wbody), be = ts_node_end_byte(wbody);
                    _emit_stmt_yield_lower(wbuf, src, bs, be, wbody, &wctx, fctx, p_next_label);
                }
            }

            /* Condition check. Lower any awaits/yields in the cond so
               their state-machine transitions emit BEFORE the if-test. */
            rp_string_puts(out, "case ");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ":");
            if (!ts_node_is_null(cond))
            {
                size_t cs = ts_node_start_byte(cond), ce = ts_node_end_byte(cond);
                char *cond_lowered = _lower_range_with_yields(out, src, cs, ce, cond,
                                                              NULL, NULL, p_next_label);
                rp_string_puts(out, "if(!(");
                rp_string_puts(out, cond_lowered);
                rp_string_puts(out, ")){_TrN_context.next=");
                rp_string_puts(out, etmp);
                rp_string_puts(out, ";break;}");
                free(cond_lowered);
            }

            /* Body */
            rp_string_puts(out, wbuf->str);
            wbuf = rp_string_free(wbuf);

            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');

            /* Loop back */
            rp_string_puts(out, "_TrN_context.next=");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ";break;");

            /* Exit label */
            rp_string_puts(out, "case ");
            rp_string_puts(out, etmp);
            rp_string_puts(out, ":");
        }
        else if (strcmp(stmt_type, "for_statement") == 0 && has_yield)
        {
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s) rp_string_putsn(out, src + ss, stmt_s - ss);
            }
            /* Decompose: for (init; cond; incr) { body }
               -> init;
                  case COND: if(!(cond)){_TrN_context.next=EXIT;break;}
                  <body>
                  case INCR: incr; _TrN_context.next=COND;break;
                  case EXIT:
               INCR is a separate case so `continue` can jump to it. */
            TSNode init = ts_node_child_by_field_name(stmt, "initializer", 11);
            TSNode cond = ts_node_child_by_field_name(stmt, "condition", 9);
            TSNode incr = ts_node_child_by_field_name(stmt, "increment", 9);
            TSNode fbody = ts_node_child_by_field_name(stmt, "body", 4);

            /* Emit initializer (skip empty_statement).  For
               variable/lexical declarations we strip the var/let/const
               keyword because the variable names are already hoisted to
               the enclosing function's scope via _collect_body_var_names;
               keeping `var` here would create a fresh local in _TrN_callee$
               that shadows the hoisted outer binding and the state
               wouldn't persist across iterations. */
            if (!ts_node_is_null(init) && strcmp(ts_node_type(init), "empty_statement") != 0)
            {
                const char *itype = ts_node_type(init);
                size_t is2 = ts_node_start_byte(init), ie2 = ts_node_end_byte(init);
                if (strcmp(itype, "variable_declaration") == 0 ||
                    strcmp(itype, "lexical_declaration") == 0)
                {
                    TSNode first_decl = ts_node_named_child(init, 0);
                    if (!ts_node_is_null(first_decl))
                        is2 = ts_node_start_byte(first_decl);
                }
                rp_string_putsn(out, src + is2, ie2 - is2);
                if (src[ie2 - 1] != ';')
                    rp_string_putc(out, ';');
            }

            *p_next_label += 3;
            int cond_label = *p_next_label;
            char ctmp[24];
            snprintf(ctmp, sizeof(ctmp), "%d", cond_label);

            /* Allocate incr_label and exit_label BEFORE body so
               break/continue inside can translate. */
            *p_next_label += 3;
            int incr_label = *p_next_label;
            char itmp[24];
            snprintf(itmp, sizeof(itmp), "%d", incr_label);

            *p_next_label += 3;
            int exit_label = *p_next_label;
            char etmp[24];
            snprintf(etmp, sizeof(etmp), "%d", exit_label);

            LoopCtx forctx = { incr_label, exit_label, pending_label, pending_label_len, ctx };

            rp_string *fbuf = rp_string_new(256);
            if (!ts_node_is_null(fbody))
            {
                if (strcmp(ts_node_type(fbody), "statement_block") == 0)
                    _emit_yield_body(fbuf, src, fbody, &forctx, fctx, p_next_label);
                else
                {
                    size_t bs = ts_node_start_byte(fbody), be = ts_node_end_byte(fbody);
                    _emit_stmt_yield_lower(fbuf, src, bs, be, fbody, &forctx, fctx, p_next_label);
                }
            }

            rp_string_puts(out, "case ");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ":");

            /* Condition check (skip if empty/missing — infinite loop).
               Lower any awaits/yields in cond before the if-test. */
            if (!ts_node_is_null(cond) && strcmp(ts_node_type(cond), "empty_statement") != 0)
            {
                size_t cs = ts_node_start_byte(cond), ce = ts_node_end_byte(cond);
                while (ce > cs && (src[ce - 1] == ';' || isspace(src[ce - 1])))
                    ce--;
                char *cond_lowered = _lower_range_with_yields(out, src, cs, ce, cond,
                                                              NULL, NULL, p_next_label);
                rp_string_puts(out, "if(!(");
                rp_string_puts(out, cond_lowered);
                rp_string_puts(out, ")){_TrN_context.next=");
                rp_string_puts(out, etmp);
                rp_string_puts(out, ";break;}");
                free(cond_lowered);
            }

            rp_string_puts(out, fbuf->str);
            fbuf = rp_string_free(fbuf);

            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');

            /* Increment case (continue target) */
            rp_string_puts(out, "case ");
            rp_string_puts(out, itmp);
            rp_string_puts(out, ":");
            if (!ts_node_is_null(incr))
            {
                size_t is2 = ts_node_start_byte(incr), ie2 = ts_node_end_byte(incr);
                rp_string_putsn(out, src + is2, ie2 - is2);
                rp_string_putc(out, ';');
            }

            rp_string_puts(out, "_TrN_context.next=");
            rp_string_puts(out, ctmp);
            rp_string_puts(out, ";break;");

            rp_string_puts(out, "case ");
            rp_string_puts(out, etmp);
            rp_string_puts(out, ":");
        }
        else if (strcmp(stmt_type, "try_statement") == 0 && has_yield)
        {
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s) rp_string_putsn(out, src + ss, stmt_s - ss);
            }
            /* try/catch/finally with yields inside.  The regenerator
               wrap catches any throw from innerFn and, if
               _TrN_context._catch is set, routes to the catch case.  This
               handler sets _TrN_context._catch around the try body, clears
               it on normal exit, and lowers the catch and finally
               bodies into their own cases.

               try { body } catch (e) { cbody } finally { fin }
               ->
                 _ctx._catch = CATCH;
                 case TRY_BODY: <body lowered>
                                _ctx._catch=0; _ctx.next=FIN_OR_POST; break;
                 case CATCH:    _ctx._catch=0;
                                e = _ctx._caught;
                                <cbody lowered>
                                _ctx.next=FIN_OR_POST; break;
                 case FIN:      <fin>
                                if(_ctx._rret) return _ctx.rval;
                                _ctx.next=POST; break;
                 case POST: */
            TSNode tbody = ts_node_child_by_field_name(stmt, "body", 4);
            TSNode tfin_clause = ts_node_child_by_field_name(stmt, "finalizer", 9);
            TSNode tcatch = ts_node_child_by_field_name(stmt, "handler", 7);

            int has_catch = !ts_node_is_null(tcatch);
            int has_finally = !ts_node_is_null(tfin_clause);
            if (!has_catch && !has_finally)
            {
                /* Bare try with no catch/finally — fall through. */
                _emit_stmt_yield_lower(out, src, ss, se, stmt, ctx, fctx, p_next_label);
                continue;
            }

            /* Extract finally body if present. */
            TSNode fin_body = (TSNode){{0}};
            if (has_finally)
            {
                uint32_t fc = ts_node_named_child_count(tfin_clause);
                if (fc > 0)
                    fin_body = ts_node_named_child(tfin_clause, fc - 1);
            }

            /* Extract catch parameter + body if present. */
            TSNode catch_param = (TSNode){{0}};
            TSNode catch_body = (TSNode){{0}};
            if (has_catch)
            {
                catch_param = ts_node_child_by_field_name(tcatch, "parameter", 9);
                catch_body = ts_node_child_by_field_name(tcatch, "body", 4);
                if (ts_node_is_null(catch_body))
                {
                    /* Some grammar versions don't expose a "body" field;
                       fall back to the last named child of catch_clause. */
                    uint32_t nc = ts_node_named_child_count(tcatch);
                    if (nc > 0)
                        catch_body = ts_node_named_child(tcatch, nc - 1);
                }
            }

            int catch_label = 0, fin_label = 0, post_label = 0;
            if (has_catch)
            {
                *p_next_label += 3;
                catch_label = *p_next_label;
            }
            if (has_finally)
            {
                *p_next_label += 3;
                fin_label = *p_next_label;
            }
            *p_next_label += 3;
            post_label = *p_next_label;

            /* If finally present, install fctx for the body (and catch
               body) so `return X;` routes via finally. */
            FinCtx tctx;
            FinCtx *body_fctx = fctx;
            if (has_finally)
            {
                tctx.finally_case = fin_label;
                tctx.outer = fctx;
                body_fctx = &tctx;
            }

            /* Allocate state slots:
                 _oc<N>  : saved outer _TrN_context._catch  (per-try unique)
                 _fe<N>  : pending exception to re-throw after finally
               And, if has_finally without user catch, allocate a synthetic
               FIN_THROW label that routes exceptions through finally before
               propagating up. */
            static unsigned _trycatch_counter = 0;
            unsigned my_id = ++_trycatch_counter;
            char save_slot[24], err_slot[24];
            snprintf(save_slot, sizeof(save_slot), "_TrN_context._oc%u", my_id);
            snprintf(err_slot, sizeof(err_slot),  "_TrN_context._fe%u", my_id);

            int fin_throw_label = 0;
            if (has_finally)
            {
                *p_next_label += 3;
                fin_throw_label = *p_next_label;
            }

            /* Determine which case label receives a throw from the try body:
                 has_catch       -> user catch label
                 has_finally only-> synthetic FIN_THROW label
                 neither         -> no _catch installation needed (bare try) */
            int body_throw_target = 0;
            if (has_catch) body_throw_target = catch_label;
            else if (has_finally) body_throw_target = fin_throw_label;

            int after_body_target = has_finally ? fin_label : post_label;

            /* Save outer _catch and install ours.  Clear the pending-error
               slot. */
            if (body_throw_target)
            {
                rp_string_appendf(out, "%s=_TrN_context._catch||0;", save_slot);
                rp_string_appendf(out, "_TrN_context._catch=%d;", body_throw_target);
            }
            if (has_finally)
            {
                rp_string_appendf(out, "%s=void 0;", err_slot);
                /* Push this try's finally label onto the runtime _ts stack
                   so the iterator's .return(v) can find an active finally
                   and run it before completing. */
                rp_string_appendf(out, "(_TrN_context._ts||(_TrN_context._ts=[])).push(%d);", fin_label);
            }

            /* Lower the try body. */
            if (!ts_node_is_null(tbody) && strcmp(ts_node_type(tbody), "statement_block") == 0)
                _emit_yield_body(out, src, tbody, ctx, body_fctx, p_next_label);
            else if (!ts_node_is_null(tbody))
            {
                size_t bs = ts_node_start_byte(tbody), be = ts_node_end_byte(tbody);
                _emit_stmt_yield_lower(out, src, bs, be, tbody, ctx, body_fctx, p_next_label);
            }
            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');

            /* After normal try-body exit, restore outer's _catch and jump on. */
            if (body_throw_target)
                rp_string_appendf(out, "_TrN_context._catch=%s;", save_slot);
            rp_string_appendf(out, "_TrN_context.next=%d;break;", after_body_target);

            /* case CATCH: throws from try-body land here.  If a finally
               follows, install FIN_THROW as the new _catch so throws inside
               the catch body route through finally; otherwise restore outer.
               Bind exception, run body, jump to FIN (or POST). */
            if (has_catch)
            {
                if (has_finally)
                    rp_string_appendf(out, "case %d:_TrN_context._catch=%d;",
                                      catch_label, fin_throw_label);
                else
                    rp_string_appendf(out, "case %d:_TrN_context._catch=%s;",
                                      catch_label, save_slot);
                if (!ts_node_is_null(catch_param) &&
                    strcmp(ts_node_type(catch_param), "identifier") == 0)
                {
                    size_t cps = ts_node_start_byte(catch_param),
                           cpe = ts_node_end_byte(catch_param);
                    /* Declare with var so the binding sticks for the
                       catch body even though we're inside a switch. */
                    rp_string_puts(out, "var ");
                    rp_string_putsn(out, src + cps, cpe - cps);
                    rp_string_puts(out, "=_TrN_context._caught;");
                }
                if (!ts_node_is_null(catch_body) &&
                    strcmp(ts_node_type(catch_body), "statement_block") == 0)
                    _emit_yield_body(out, src, catch_body, ctx, body_fctx, p_next_label);
                else if (!ts_node_is_null(catch_body))
                {
                    size_t cbs = ts_node_start_byte(catch_body),
                           cbe = ts_node_end_byte(catch_body);
                    _emit_stmt_yield_lower(out, src, cbs, cbe, catch_body,
                                           ctx, body_fctx, p_next_label);
                }
                if (out->len && out->str[out->len - 1] != ';')
                    rp_string_putc(out, ';');
                /* If finally, restore _catch (FIN_THROW no longer needed) before jumping. */
                if (has_finally)
                    rp_string_appendf(out, "_TrN_context._catch=%s;", save_slot);
                rp_string_appendf(out, "_TrN_context.next=%d;break;", after_body_target);
            }

            /* case FIN_THROW: throws from try-body (no user catch) or from
               catch-body land here.  Save the error to _fe<N>, restore
               outer _catch, fall through to FIN. */
            if (has_finally)
            {
                rp_string_appendf(out, "case %d:%s=_TrN_context._caught;_TrN_context._catch=%s;",
                                  fin_throw_label, err_slot, save_slot);
                /* Fall through to FIN. */
            }

            /* case FINALLY: cleanup body; if we got here via throw, re-throw
               after running finally; if via return, return after finally;
               otherwise jump POST. */
            if (has_finally)
            {
                rp_string_appendf(out, "case %d:", fin_label);
                if (!ts_node_is_null(fin_body) &&
                    strcmp(ts_node_type(fin_body), "statement_block") == 0)
                    _emit_yield_body(out, src, fin_body, ctx, fctx, p_next_label);
                else if (!ts_node_is_null(fin_body))
                {
                    size_t fbs = ts_node_start_byte(fin_body),
                           fbe = ts_node_end_byte(fin_body);
                    _emit_stmt_yield_lower(out, src, fbs, fbe, fin_body,
                                           ctx, fctx, p_next_label);
                }
                if (out->len && out->str[out->len - 1] != ';')
                    rp_string_putc(out, ';');
                /* Pop this try's finally label off the runtime _ts stack —
                   we're leaving the try-finally either via re-throw,
                   pending-return, or normal exit. */
                rp_string_puts(out, "_TrN_context._ts&&_TrN_context._ts.pop();");
                /* Pending throw takes precedence over pending return. */
                rp_string_appendf(out,
                    "if(%s!==void 0){var _TrN_te=%s;%s=void 0;throw _TrN_te;}",
                    err_slot, err_slot, err_slot);
                rp_string_puts(out, "if(_TrN_context._rret){_TrN_context._rret=false;return _TrN_context.rval;}");
                rp_string_appendf(out, "_TrN_context.next=%d;break;", post_label);
            }

            /* case POST: */
            rp_string_appendf(out, "case %d:", post_label);
        }
        else if (strcmp(stmt_type, "switch_statement") == 0 && has_yield)
        {
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s) rp_string_putsn(out, src + ss, stmt_s - ss);
            }
            /* Structural lowering for switch with yields.  Cache the
               value, dispatch to each case via if-comparison + jump,
               then emit each case body as its own state.  This MVP:
               - cases are tested in source order
               - case body ending with a top-level break_statement is
                 mapped to "jump AFTER" and the break is dropped
               - case body NOT ending with break falls through to the
                 next case (jump to that case's body)
               - default clause runs when no case matched
               - `break;` anywhere inside a case body translates via a
                 LoopCtx-shaped exit_case (continue still skips switches
                 to reach the outer loop). */
            TSNode sval = ts_node_child_by_field_name(stmt, "value", 5);
            TSNode sbody = ts_node_child_by_field_name(stmt, "body", 4);
            if (ts_node_is_null(sbody))
            {
                _emit_stmt_yield_lower(out, src, ss, se, stmt, ctx, fctx, p_next_label);
                continue;
            }

            uint32_t nc = ts_node_named_child_count(sbody);
            /* First pass: locate the default case (if any).  We allocate
               one label per child slot below regardless of switch_case
               vs switch_default, so an explicit case count isn't
               needed here. */
            int default_idx = -1;
            for (uint32_t i2 = 0; i2 < nc; i2++)
            {
                TSNode kid = ts_node_named_child(sbody, i2);
                const char *kt = ts_node_type(kid);
                if (strcmp(kt, "switch_default") == 0) default_idx = (int)i2;
            }

            *p_next_label += 3;
            int after_case = *p_next_label;
            int *case_labels = NULL;
            REMALLOC(case_labels, sizeof(int) * (size_t)(nc + 1));
            for (uint32_t i2 = 0; i2 < nc; i2++)
            {
                *p_next_label += 3;
                case_labels[i2] = *p_next_label;
            }

            /* Allocate a slot for the switch value on the context. */
            int sw_id = ++(*p_next_label);
            char sx_name[48];
            snprintf(sx_name, sizeof(sx_name), "_TrN_context._sw%d", sw_id);

            /* Emit `_sx = (value);` */
            rp_string_appendf(out, "%s=(", sx_name);
            if (!ts_node_is_null(sval))
            {
                size_t vs = ts_node_start_byte(sval), ve = ts_node_end_byte(sval);
                rp_string_putsn(out, src + vs, ve - vs);
            }
            rp_string_puts(out, ");");

            /* Dispatch: for each case, `if (_sx === val) { next=CASE_K; break; }`.
               After all cases, jump to default (if any) else to AFTER. */
            for (uint32_t i2 = 0; i2 < nc; i2++)
            {
                TSNode kid = ts_node_named_child(sbody, i2);
                if (strcmp(ts_node_type(kid), "switch_case") != 0) continue;
                TSNode kv = ts_node_child_by_field_name(kid, "value", 5);
                if (ts_node_is_null(kv)) continue;
                size_t cvs = ts_node_start_byte(kv), cve = ts_node_end_byte(kv);
                rp_string_appendf(out, "if(%s===(", sx_name);
                rp_string_putsn(out, src + cvs, cve - cvs);
                rp_string_appendf(out, ")){_TrN_context.next=%d;break;}", case_labels[i2]);
            }
            int dispatch_else = (default_idx >= 0) ? case_labels[default_idx] : after_case;
            rp_string_appendf(out, "_TrN_context.next=%d;break;", dispatch_else);

            /* Each case body: emit `case CASE_K:` then the body, then
               either `next=AFTER; break;` (if break) or `next=NEXT_K; break;`
               for fall-through. */
            LoopCtx swctx = { -1 /* continue can't target switch */, after_case,
                              pending_label, pending_label_len, ctx };
            for (uint32_t i2 = 0; i2 < nc; i2++)
            {
                TSNode kid = ts_node_named_child(sbody, i2);
                const char *kt = ts_node_type(kid);
                if (strcmp(kt, "switch_case") != 0 && strcmp(kt, "switch_default") != 0)
                    continue;

                rp_string_appendf(out, "case %d:", case_labels[i2]);

                /* Collect body statements (skip the case value, if any). */
                uint32_t kc = ts_node_named_child_count(kid);
                /* For switch_case, the value field is also a named child.
                   Identify it and skip. */
                TSNode case_val = (strcmp(kt, "switch_case") == 0)
                                      ? ts_node_child_by_field_name(kid, "value", 5)
                                      : (TSNode){{0}};
                /* Determine body start index. */
                uint32_t body_start_idx = 0;
                if (!ts_node_is_null(case_val))
                {
                    /* The value is a named child; find its index. */
                    for (uint32_t bi = 0; bi < kc; bi++)
                    {
                        TSNode ch = ts_node_named_child(kid, bi);
                        if (ts_node_eq(ch, case_val)) { body_start_idx = bi + 1; break; }
                    }
                }
                /* Determine body end: drop trailing unlabelled break. */
                uint32_t body_end_idx = kc;
                int had_break = 0;
                if (body_end_idx > body_start_idx)
                {
                    TSNode last = ts_node_named_child(kid, body_end_idx - 1);
                    if (strcmp(ts_node_type(last), "break_statement") == 0 &&
                        ts_node_named_child_count(last) == 0)
                    {
                        body_end_idx--;
                        had_break = 1;
                    }
                }

                /* Emit body with structural per-statement dispatch so
                   nested switch/for/while/if get their own state-machine
                   lowering instead of leaking literal `case N:` labels
                   into the surrounding regen switch.  Pass the case node
                   directly as the "block" and the body's child-index
                   range. */
                if (body_end_idx > body_start_idx)
                    _emit_yield_body_range(out, src, kid, body_start_idx, body_end_idx,
                                           &swctx, fctx, p_next_label);
                if (out->len && out->str[out->len - 1] != ';')
                    rp_string_putc(out, ';');

                /* Successor: if had break, jump to AFTER.  Otherwise
                   fall through to next case (or AFTER if none). */
                int succ;
                if (had_break) succ = after_case;
                else
                {
                    /* Find the next case/default after this one in source order. */
                    int found_next = -1;
                    for (uint32_t bi = i2 + 1; bi < nc; bi++)
                    {
                        TSNode nk = ts_node_named_child(sbody, bi);
                        const char *nkt = ts_node_type(nk);
                        if (strcmp(nkt, "switch_case") == 0 || strcmp(nkt, "switch_default") == 0)
                        {
                            found_next = (int)bi;
                            break;
                        }
                    }
                    succ = (found_next >= 0) ? case_labels[found_next] : after_case;
                }
                rp_string_appendf(out, "_TrN_context.next=%d;break;", succ);
            }

            /* case AFTER */
            rp_string_appendf(out, "case %d:", after_case);
            free(case_labels);
        }
        else if (strcmp(stmt_type, "if_statement") == 0 && has_yield)
        {
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s) rp_string_putsn(out, src + ss, stmt_s - ss);
            }
            /* Structural lowering for if-with-yield-in-body.  Linearising
               an if's body would extract the yield to switch level,
               making it fire unconditionally — the bug behind
               "yield-in-conditional-branch is broken".  Allocate cases
               for the then/else bodies and dispatch via state
               transitions.

               if (cond) <then> else <else>
               ->
                 if (cond) { _ctx.next=THEN; break; }
                 _ctx.next=(ELSE or AFTER); break;
                 case THEN: <then lowered>;
                            _ctx.next=AFTER; break;
                 case ELSE: <else lowered>;
                            _ctx.next=AFTER; break;
                 case AFTER: */
            TSNode cond = ts_node_child_by_field_name(stmt, "condition", 9);
            TSNode conseq = ts_node_child_by_field_name(stmt, "consequence", 11);
            TSNode altr = ts_node_child_by_field_name(stmt, "alternative", 11);
            int has_else = !ts_node_is_null(altr);

            /* Lower the condition (may itself contain yields). */
            char *cond_lowered = NULL;
            if (!ts_node_is_null(cond))
            {
                size_t cs = ts_node_start_byte(cond), ce = ts_node_end_byte(cond);
                cond_lowered = _lower_range_with_yields(out, src, cs, ce, cond, ctx, fctx, p_next_label);
            }

            *p_next_label += 3;
            int then_case = *p_next_label;
            int else_case = 0;
            if (has_else)
            {
                *p_next_label += 3;
                else_case = *p_next_label;
            }
            *p_next_label += 3;
            int after_case = *p_next_label;

            /* Dispatch */
            rp_string_puts(out, "if");
            rp_string_puts(out, cond_lowered ? cond_lowered : "(false)");
            rp_string_appendf(out, "{_TrN_context.next=%d;break;}", then_case);
            if (cond_lowered) free(cond_lowered);
            rp_string_appendf(out, "_TrN_context.next=%d;break;", has_else ? else_case : after_case);

            /* case THEN */
            rp_string_appendf(out, "case %d:", then_case);
            if (!ts_node_is_null(conseq))
            {
                if (strcmp(ts_node_type(conseq), "statement_block") == 0)
                    _emit_yield_body(out, src, conseq, ctx, fctx, p_next_label);
                else
                {
                    size_t bs = ts_node_start_byte(conseq), be = ts_node_end_byte(conseq);
                    _emit_stmt_yield_lower(out, src, bs, be, conseq, ctx, fctx, p_next_label);
                }
            }
            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');
            rp_string_appendf(out, "_TrN_context.next=%d;break;", after_case);

            /* case ELSE */
            if (has_else)
            {
                rp_string_appendf(out, "case %d:", else_case);
                if (strcmp(ts_node_type(altr), "statement_block") == 0)
                    _emit_yield_body(out, src, altr, ctx, fctx, p_next_label);
                else
                {
                    size_t bs = ts_node_start_byte(altr), be = ts_node_end_byte(altr);
                    _emit_stmt_yield_lower(out, src, bs, be, altr, ctx, fctx, p_next_label);
                }
                if (out->len && out->str[out->len - 1] != ';')
                    rp_string_putc(out, ';');
                rp_string_appendf(out, "_TrN_context.next=%d;break;", after_case);
            }

            /* case AFTER */
            rp_string_appendf(out, "case %d:", after_case);
        }
        else if (strcmp(stmt_type, "for_in_statement") == 0 && has_yield)
        {
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s) rp_string_putsn(out, src + ss, stmt_s - ss);
            }
            /* Decompose for-of (only "of" — for-in is not lowered here)
               into index-based iteration through the regenerator state
               machine.  Duktape doesn't have Array.prototype[Symbol.iterator]
               natively, so the iterator-protocol approach fails; instead we
               mirror the existing (non-generator) for-of rewriter which
               uses array-like indexing.  Works for arrays, strings,
               arguments, typed arrays — anything with .length and integer
               indexing.

                 for (BINDING of EXPR) { body }
               ->
                 _ctx._fxN = (EXPR); _ctx._fiN = 0;
                 case TEST: if (_ctx._fiN >= _ctx._fxN.length)
                              { _ctx.next=EXIT; break; }
                            BINDING = _ctx._fxN[_ctx._fiN];
                 <body>
                 case INCR: _ctx._fiN++; _ctx.next=TEST; break;
                 case EXIT:

               The collection and index live on the context object so
               they survive across yield re-entries. */
            TSNode op = ts_node_child_by_field_name(stmt, "operator", 8);
            int is_of = 0;
            if (!ts_node_is_null(op))
            {
                size_t ops = ts_node_start_byte(op), ope = ts_node_end_byte(op);
                if (ope - ops == 2 && src[ops] == 'o' && src[ops + 1] == 'f')
                    is_of = 1;
            }
            /* Detect `for await (... of ...)` by scanning the source bytes
               between "for" and "(" for the "await" keyword. */
            int is_await_of = 0;
            if (is_of)
            {
                size_t scan = ss + 3; /* past "for" */
                size_t scan_end = ts_node_start_byte(stmt) + 10; /* small window */
                if (scan_end > se) scan_end = se;
                while (scan + 5 <= scan_end && src[scan] != '(')
                {
                    if (src[scan] == 'a' && src[scan+1] == 'w' && src[scan+2] == 'a'
                        && src[scan+3] == 'i' && src[scan+4] == 't')
                    {
                        is_await_of = 1;
                        break;
                    }
                    scan++;
                }
            }
            TSNode left = ts_node_child_by_field_name(stmt, "left", 4);
            TSNode right = ts_node_child_by_field_name(stmt, "right", 5);
            TSNode fbody = ts_node_child_by_field_name(stmt, "body", 4);

            *p_next_label += 3;
            int test_label = *p_next_label;
            *p_next_label += 3;
            int incr_label = *p_next_label;
            *p_next_label += 3;
            int exit_label = *p_next_label;

            char coll_name[48], idx_name[48];
            snprintf(coll_name, sizeof(coll_name), "_TrN_context._fx%d", test_label);
            snprintf(idx_name,  sizeof(idx_name),  "_TrN_context._fi%d", test_label);

            /* Initialize:
                 for-of  : _ctx._fxN = _TrN_Sp._iter(EXPR);
                 for-in  : _ctx._fxN = [keys...]; _ctx._fiN = 0; (index-based)
               _fyN holds the per-iteration step result so re-entry through
               a yield boundary doesn't re-call .next(). */
            char step_name[48];
            snprintf(step_name, sizeof(step_name), "_TrN_context._fy%d", test_label);
            if (is_of)
            {
                /* For-of: wrap with _TrN_Sp._iter so generators / Sets /
                   Maps / iterator-only objects work alongside arrays. */
                rp_string_puts(out, coll_name);
                rp_string_puts(out, "=_TrN_Sp._iter(");
                if (!ts_node_is_null(right))
                {
                    size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                    rp_string_putsn(out, src + rs, re - rs);
                }
                rp_string_puts(out, ");");
            }
            else
            {
                /* For-in: collect keys upfront via an inline (non-yielding)
                   for-in.  `_TrN_context._fxN` here is the keys ARRAY. */
                rp_string_appendf(out, "%s=[];", coll_name);
                rp_string_appendf(out, "for(var _TrN_fk%d in (", test_label);
                if (!ts_node_is_null(right))
                {
                    size_t rs = ts_node_start_byte(right), re = ts_node_end_byte(right);
                    rp_string_putsn(out, src + rs, re - rs);
                }
                rp_string_appendf(out, "))%s.push(_TrN_fk%d);", coll_name, test_label);
                rp_string_appendf(out, "%s=0;", idx_name);
            }

            /* case TEST: pull next value (or end the loop).
               for-await-of additionally awaits both .next() and .value. */
            int await_next_label = 0, await_value_label = 0;
            if (is_await_of)
            {
                *p_next_label += 3;
                await_next_label = *p_next_label;
                *p_next_label += 3;
                await_value_label = *p_next_label;
                /* In async-gen contexts the per-iteration awaits must be
                   `__await`-wrapped so the wrapper recognises them as
                   awaits (not yields-to-consumer). */
                const char *aw_open  = _g_in_async_gen ? "_TrN_Sp.__await(" : "";
                const char *aw_close = _g_in_async_gen ? ")"                : "";
                /* case TEST: await iter.next() */
                rp_string_appendf(out,
                    "case %d:_TrN_context._y=true;_TrN_context.next=%d;return %s%s.next()%s;",
                    test_label, await_next_label, aw_open, coll_name, aw_close);
                /* case AWAIT_NEXT: store step, check done, then await value */
                rp_string_appendf(out,
                    "case %d:%s=_TrN_context.sent;if(%s.done){_TrN_context.next=%d;break;}"
                    "_TrN_context._y=true;_TrN_context.next=%d;return %s%s.value%s;",
                    await_next_label, step_name, step_name, exit_label,
                    await_value_label, aw_open, step_name, aw_close);
                /* case AWAIT_VALUE: fall through with _TrN_context.sent as the resolved value */
                rp_string_appendf(out, "case %d:", await_value_label);
            }
            else if (is_of)
            {
                rp_string_appendf(out, "case %d:%s=%s.next();if(%s.done){_TrN_context.next=%d;break;}",
                                  test_label, step_name, coll_name, step_name, exit_label);
            }
            else
            {
                rp_string_appendf(out, "case %d:if(%s>=%s.length){_TrN_context.next=%d;break;}",
                                  test_label, idx_name, coll_name, exit_label);
            }

            /* Emit BINDING = step.value (for-of) or coll[idx] (for-in).
               for-await-of: BINDING = _TrN_context.sent (already-awaited value).
               MVP: identifier binding only. Strip the var/let/const keyword. */
            if (!ts_node_is_null(left))
            {
                const char *lt = ts_node_type(left);
                TSNode name_node = (TSNode){{0}};
                if (strcmp(lt, "variable_declaration") == 0 ||
                    strcmp(lt, "lexical_declaration") == 0)
                {
                    TSNode decl = ts_node_named_child(left, 0);
                    if (!ts_node_is_null(decl))
                    {
                        TSNode nm = ts_node_child_by_field_name(decl, "name", 4);
                        if (!ts_node_is_null(nm) && strcmp(ts_node_type(nm), "identifier") == 0)
                            name_node = nm;
                    }
                }
                else if (strcmp(lt, "identifier") == 0)
                {
                    name_node = left;
                }
                if (!ts_node_is_null(name_node))
                {
                    size_t ns2 = ts_node_start_byte(name_node), ne2 = ts_node_end_byte(name_node);
                    rp_string_putsn(out, src + ns2, ne2 - ns2);
                    if (is_await_of)
                        rp_string_puts(out, "=_TrN_context.sent;");
                    else if (is_of)
                        rp_string_appendf(out, "=%s.value;", step_name);
                    else
                        rp_string_appendf(out, "=%s[%s];", coll_name, idx_name);
                }
                else
                {
                    /* Destructure / pattern binding — MVP doesn't support. */
                    if (is_await_of)
                        rp_string_puts(out, "var _TrN_ofd=_TrN_context.sent;");
                    else if (is_of)
                        rp_string_appendf(out, "var _TrN_ofd=%s.value;", step_name);
                    else
                        rp_string_appendf(out, "var _TrN_ofd=%s[%s];", coll_name, idx_name);
                }
            }

            LoopCtx forctx = { incr_label, exit_label, pending_label, pending_label_len, ctx };
            rp_string *fbuf = rp_string_new(256);
            if (!ts_node_is_null(fbody))
            {
                if (strcmp(ts_node_type(fbody), "statement_block") == 0)
                    _emit_yield_body(fbuf, src, fbody, &forctx, fctx, p_next_label);
                else
                {
                    size_t bs = ts_node_start_byte(fbody), be = ts_node_end_byte(fbody);
                    _emit_stmt_yield_lower(fbuf, src, bs, be, fbody, &forctx, fctx, p_next_label);
                }
            }
            rp_string_puts(out, fbuf->str);
            fbuf = rp_string_free(fbuf);
            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');

            /* case INCR (continue target): re-test. For for-in we bump the
               index; for-of advances via the iterator protocol so no bump. */
            if (is_of)
                rp_string_appendf(out, "case %d:_TrN_context.next=%d;break;",
                                  incr_label, test_label);
            else
                rp_string_appendf(out, "case %d:%s++;_TrN_context.next=%d;break;",
                                  incr_label, idx_name, test_label);
            /* case EXIT */
            rp_string_appendf(out, "case %d:", exit_label);
        }
        else if (strcmp(stmt_type, "do_statement") == 0 && has_yield)
        {
            {
                size_t stmt_s = ts_node_start_byte(stmt);
                if (ss < stmt_s) rp_string_putsn(out, src + ss, stmt_s - ss);
            }
            /* Decompose: do { body } while (cond)
               -> case BODY: <body> case TEST: if(!cond){_TrN_context.next=EXIT;break;}
                  _TrN_context.next=BODY;break;
                  case EXIT:
               Body executes once before the test, so BODY case comes first
               (fall through from surrounding code).  `continue` jumps to
               TEST so the next iteration goes through the condition. */
            TSNode dbody = ts_node_child_by_field_name(stmt, "body", 4);
            TSNode dcond = ts_node_child_by_field_name(stmt, "condition", 9);

            *p_next_label += 3;
            int body_label = *p_next_label;
            char btmp[24];
            snprintf(btmp, sizeof(btmp), "%d", body_label);

            *p_next_label += 3;
            int test_label = *p_next_label;
            char ttmp[24];
            snprintf(ttmp, sizeof(ttmp), "%d", test_label);

            *p_next_label += 3;
            int exit_label = *p_next_label;
            char etmp[24];
            snprintf(etmp, sizeof(etmp), "%d", exit_label);

            LoopCtx dctx = { test_label, exit_label, pending_label, pending_label_len, ctx };

            rp_string *dbuf = rp_string_new(256);
            if (!ts_node_is_null(dbody))
            {
                if (strcmp(ts_node_type(dbody), "statement_block") == 0)
                    _emit_yield_body(dbuf, src, dbody, &dctx, fctx, p_next_label);
                else
                {
                    size_t bs = ts_node_start_byte(dbody), be = ts_node_end_byte(dbody);
                    _emit_stmt_yield_lower(dbuf, src, bs, be, dbody, &dctx, fctx, p_next_label);
                }
            }

            /* Body */
            rp_string_puts(out, "case ");
            rp_string_puts(out, btmp);
            rp_string_puts(out, ":");
            rp_string_puts(out, dbuf->str);
            dbuf = rp_string_free(dbuf);
            if (out->len && out->str[out->len - 1] != ';')
                rp_string_putc(out, ';');

            /* Test */
            rp_string_puts(out, "case ");
            rp_string_puts(out, ttmp);
            rp_string_puts(out, ":");
            if (!ts_node_is_null(dcond))
            {
                size_t cs = ts_node_start_byte(dcond), ce = ts_node_end_byte(dcond);
                char *dcond_lowered = _lower_range_with_yields(out, src, cs, ce, dcond,
                                                               NULL, NULL, p_next_label);
                rp_string_puts(out, "if(!(");
                rp_string_puts(out, dcond_lowered);
                rp_string_puts(out, ")){_TrN_context.next=");
                rp_string_puts(out, etmp);
                rp_string_puts(out, ";break;}");
                free(dcond_lowered);
            }
            rp_string_puts(out, "_TrN_context.next=");
            rp_string_puts(out, btmp);
            rp_string_puts(out, ";break;");

            /* Exit */
            rp_string_puts(out, "case ");
            rp_string_puts(out, etmp);
            rp_string_puts(out, ":");
        }
        else if (has_yield)
        {
            if (wrap_paren)
            {
                /* Destructure-yield-decl: do the yield lowering, then emit
                   `(<substituted-text>);` so the destructuring assignment
                   sits at expression position.  Can't use
                   _emit_stmt_yield_lower because it emits state
                   transitions ahead of the substituted text, and those
                   transitions need to be at switch-statement scope, not
                   wrapped in parens. */
                char *lowered = _lower_range_with_yields(out, src, ss, se, stmt, ctx, fctx, p_next_label);
                rp_string_putc(out, '(');
                rp_string_puts(out, lowered);
                /* Replace trailing ';' (if present) with ');' */
                if (out->len > 0 && out->str[out->len - 1] == ';')
                    out->str[out->len - 1] = ')';
                else
                    rp_string_putc(out, ')');
                rp_string_putc(out, ';');
                free(lowered);
            }
            else
            {
                _emit_stmt_yield_lower(out, src, ss, se, stmt, ctx, fctx, p_next_label);
            }
        }
        else if (ctx)
        {
            /* Even without yield, the statement may contain break/continue
               that targets our enclosing loop (e.g. `if (done) break;`).
               Route through the lowering helper with no yields so it just
               substitutes control-flow nodes. */
            _emit_stmt_yield_lower(out, src, ss, se, stmt, ctx, fctx, p_next_label);
        }
        else
        {
            rp_string_putsn(out, src + ss, se - ss);
        }
    }
}

/* Convenience wrapper: process all named children of block. */
static void _emit_yield_body(rp_string *out, const char *src, TSNode block,
                             LoopCtx *ctx, FinCtx *fctx, int *p_next_label)
{
    uint32_t sc = ts_node_named_child_count(block);
    _emit_yield_body_range(out, src, block, 0, sc, ctx, fctx, p_next_label);
}

static char *_build_regenerator_switch_body_for_yield(const char *src, TSNode body)
{
    rp_string *out = rp_string_new(384);

    int next_label = 0;

    // Hoist var/let/const declarations so they persist across _TrN_callee$ invocations via closure
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
        "return _TrN_Sp.regeneratorRuntime.wrap(function _TrN_callee$(_TrN_context){while(1){switch(_TrN_context.prev=_TrN_context.next){case 0:");
    const char *bt = ts_node_type(body);
    if (strcmp(bt, "statement_block") == 0)
    {
        _emit_yield_body(out, src, body, NULL /* loop */, NULL /* finally */, &next_label);
    }
    else
    {
        // Concise arrow body: the expression is an implicit return.
        TSNode expr = body;
        size_t ss = ts_node_start_byte(expr), se = ts_node_end_byte(expr);
        rp_string *tmp = rp_string_new(64);
        _emit_stmt_yield_lower(tmp, src, ss, se, expr, NULL /* loop */, NULL /* finally */, &next_label);
        if (strstr(tmp->str, "_TrN_context.next") == NULL)
        {
            rp_string_puts(out, " return ");
            rp_string_puts(out, tmp->str);
            rp_string_puts(out, ";");
        }
        else
        {
            // The yield was lowered. The last segment (after the final "case N:")
            // contains _TrN_context.sent which is the value to implicitly return.
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
    rp_string_puts(out, ":case \"end\":return _TrN_context.stop();}}}, null, this);");
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

/* True iff `node` is `async function*` — a generator with the async modifier.
   Tree-sitter parses this as a generator_function_declaration / _expression
   with an `async` child token. Methods need both `async` and `*` keywords. */
static int _has_async_modifier(TSNode node)
{
    uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        TSNode k = ts_node_child(node, i);
        if (strcmp(ts_node_type(k), "async") == 0)
            return 1;
    }
    return 0;
}
static int _is_async_generator_function_like(const char *src, TSNode node)
{
    return _is_generator_function_like(src, node) && _has_async_modifier(node);
}

/* See forward declaration near top of file for _g_in_async_gen. */

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
        rp_string_puts(out, "_TrN_gen");
    rp_string_puts(out, " = _TrN_Sp.regeneratorRuntime.mark(function ");
    if (!ts_node_is_null(name))
        rp_string_putsn(out, src+ns, ne-ns);
    else
        rp_string_puts(out, "_TrN_gen");
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
        rp_string_puts(out, "_TrN_callee");
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
        rp_string_puts(out, "_TrN_gen");
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

/* async function* foo(params) { body } emit. The inner is a regen-
   style generator (built with _g_in_async_gen=1 so awaits get wrapped
   in `_TrN_Sp.__await(...)` markers).  The outer function calls
   `_TrN_Sp.__asyncGenerator(this, arguments, innerGen)` to produce an
   async iterator. */
static char *_emit_async_gen_decl_replacement(const char *src, TSNode node)
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
    rp_string_puts(out, "function ");
    if (!ts_node_is_null(name)) rp_string_putsn(out, src+ns, ne-ns);
    else rp_string_puts(out, "_TrN_asyncGen");
    _append_params_sig(out, src, node);
    rp_string_puts(out, "{return _TrN_Sp.__asyncGenerator(this,arguments,_TrN_Sp.regeneratorRuntime.mark(function _TrN_callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    _g_in_async_gen++;
    char *wrap = _build_regenerator_switch_body_for_yield(src, body);
    _g_in_async_gen--;
    if (!wrap)
    {
        out = rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "}));}");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

static char *_emit_async_gen_expr_replacement(const char *src, TSNode node)
{
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body))
        return NULL;

    rp_string *out = rp_string_new(256);
    rp_string_puts(out, "function ");
    if (!ts_node_is_null(name))
    {
        size_t ns = ts_node_start_byte(name), ne = ts_node_end_byte(name);
        rp_string_putsn(out, src+ns, ne-ns);
    }
    _append_params_sig(out, src, node);
    rp_string_puts(out, "{return _TrN_Sp.__asyncGenerator(this,arguments,_TrN_Sp.regeneratorRuntime.mark(function _TrN_callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    _g_in_async_gen++;
    char *wrap = _build_regenerator_switch_body_for_yield(src, body);
    _g_in_async_gen--;
    if (!wrap)
    {
        out = rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "}));}");

    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

static char *_emit_async_gen_method_replacement(const char *src, TSNode node)
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
    rp_string_puts(out, ": function ");
    if (named) rp_string_putsn(out, src+ns, ne-ns);
    _append_params_sig(out, src, node);
    rp_string_puts(out, "{return _TrN_Sp.__asyncGenerator(this,arguments,_TrN_Sp.regeneratorRuntime.mark(function _TrN_callee");
    _append_params_sig(out, src, node);
    rp_string_puts(out, " {");
    _g_in_async_gen++;
    char *wrap = _build_regenerator_switch_body_for_yield(src, body);
    _g_in_async_gen--;
    if (!wrap)
    {
        out = rp_string_free(out);
        return NULL;
    }
    rp_string_puts(out, wrap);
    free(wrap);
    rp_string_puts(out, "}));}");

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
    int is_async_gen = _is_async_generator_function_like(src, node);
    if (is_async_gen)
    {
        if (strcmp(t, "generator_function_declaration") == 0)
            rep = _emit_async_gen_decl_replacement(src, node);
        else if (strcmp(t, "method_definition") == 0)
            rep = _emit_async_gen_method_replacement(src, node);
        else
            rep = _emit_async_gen_expr_replacement(src, node);
    }
    else if (strcmp(t, "generator_function_declaration") == 0)
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

static __attribute__((unused)) int
span_has_flow_ctrl_tokens(const char *src, size_t s, size_t e)
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
    if (memmem(p, len, "this", 4))
        return 1;
    return 0;
}

/* Forward decls for §8 Phase 5 helpers (definitions are below the
   _bs_* family of scope-rename helpers). */
typedef struct _bs_name_set_s _BS_NameSet;
static int _bs_is_fn_boundary(TSNode node);
static int _bs_ns_contains(const _BS_NameSet *s, const char *name, size_t len);

/* §8 Phase 5 helpers: capture detection and sentinel rewriting for the
   for-let → _loop transformation. See transpiler-todo.md §8 Phase 5. */

/* Walk a subtree, returning 1 if any identifier reference inside a
   NESTED function/arrow/method references one of the names in `names`.
   "Capture" means: a closure created inside the for-loop body that
   references the loop-let binding — the closure must capture the
   iteration's fresh binding to behave correctly. References at the
   body's top level (not inside a nested function) don't count, because
   those execute synchronously per iteration. */
static int _bs_for_has_capture(TSNode node, const char *src,
                               _BS_NameSet *names, int inside_fn)
{
    const char *t = ts_node_type(node);
    int now_inside = inside_fn || _bs_is_fn_boundary(node);

    if (now_inside && strcmp(t, "identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (_bs_ns_contains(names, src + s, e - s))
        {
            TSNode parent = ts_node_parent(node);
            int skip = 0;
            if (!ts_node_is_null(parent))
            {
                const char *pt = ts_node_type(parent);
                if (strcmp(pt, "member_expression") == 0)
                {
                    TSNode prop = ts_node_child_by_field_name(parent, "property", 8);
                    if (!ts_node_is_null(prop) && ts_node_eq(prop, node)) skip = 1;
                }
                else if (strcmp(pt, "pair") == 0 || strcmp(pt, "pair_pattern") == 0)
                {
                    TSNode key = ts_node_child_by_field_name(parent, "key", 3);
                    if (!ts_node_is_null(key) && ts_node_eq(key, node)) skip = 1;
                }
                else if (strcmp(pt, "variable_declarator") == 0)
                {
                    TSNode nn = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nn) && ts_node_eq(nn, node)) skip = 1;
                }
            }
            if (!skip) return 1;
        }
    }

    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        if (_bs_for_has_capture(ts_node_child(node, i), src, names, now_inside)) return 1;
    return 0;
}

/* §8 Phase 5 — full babel-parity sentinel walker.

   Context carried through the recursive walk:
   - edits          : sentinel substitution edits (positions are absolute src bytes)
   - has_bare_break / has_bare_continue / has_return : which dispatch arms to emit
   - break_labels / continue_labels (deduped): labels seen on labeled break/continue
     whose target is the for-let or an enclosing loop (these must propagate)
   - args_positions : every `arguments` identifier reference at body scope (not
     inside nested functions). If non-empty, the wrap emits a
     `var _TrN_loop_args = arguments;` prefix and rewrites each occurrence.
   - giveup         : set when we encounter something we can't safely handle
     (label target inside the body that we can't analyse, etc.)

   Depth counters:
   - switch_depth : enclosing switch_statements. While >0, bare `break` targets
     the switch and is left alone; labeled break/continue and return still apply.
   - loop_depth   : enclosing loops INSIDE the for-let body (not the for-let
     itself). While >0, bare `break`/`continue` and labeled-targeting-inner
     don't reach the for-let; only `return` and labels targeting our level or
     above still propagate. */

typedef struct {
    char  **labels;
    size_t  len, cap;
} _BS_LblSet;

static int _bs_lbl_has(const _BS_LblSet *s, const char *name, size_t len)
{
    for (size_t i = 0; i < s->len; i++)
    {
        if (strlen(s->labels[i]) == len && memcmp(s->labels[i], name, len) == 0)
            return 1;
    }
    return 0;
}
static void _bs_lbl_add(_BS_LblSet *s, const char *name, size_t len)
{
    if (_bs_lbl_has(s, name, len)) return;
    if (s->len == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 4;
        REMALLOC(s->labels, s->cap * sizeof(char *));
    }
    char *copy = NULL;
    REMALLOC(copy, len + 1);
    memcpy(copy, name, len);
    copy[len] = '\0';
    s->labels[s->len++] = copy;
}
static void _bs_lbl_free(_BS_LblSet *s)
{
    for (size_t i = 0; i < s->len; i++) free(s->labels[i]);
    free(s->labels);
    s->labels = NULL;
    s->len = s->cap = 0;
}

typedef struct {
    size_t *a;
    size_t  len, cap;
} _BS_SizeVec;

static void _bs_sv_push(_BS_SizeVec *v, size_t x)
{
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        REMALLOC(v->a, v->cap * sizeof(size_t));
    }
    v->a[v->len++] = x;
}
static void _bs_sv_free(_BS_SizeVec *v) { free(v->a); v->a = NULL; v->len = v->cap = 0; }

typedef struct {
    EditList    *edits;
    _BS_LblSet   break_labels;
    _BS_LblSet   continue_labels;
    _BS_SizeVec  args_positions;
    TSNode       body;            /* the for-let's body — for inside-body checks */
    int          has_bare_break;
    int          has_bare_continue;
    int          has_return;
    int          giveup;
    /* Nested-wrap depth: counts for-lets-with-let-init encountered while
       descending. At depth > 0 we're inside another wrap's body — labels
       and `has_return` are still collected (so OUR dispatch can propagate
       them upward), but sentinel EDITS are not emitted (the inner wrap
       owns those edits). Bare break/continue at any depth > 0 is also
       not OUR concern. `arguments` references inside a nested wrap are
       handled by that wrap's own argument-capture, not ours. */
} _BS_ForCtx;

/* Walk parent chain from `from` looking for a labeled_statement with the
   given label name. Stops at function boundaries. Returns null if not
   found. */
static TSNode _bs_find_label_target(TSNode from, const char *src,
                                    const char *label, size_t label_len)
{
    TSNode p = ts_node_parent(from);
    while (!ts_node_is_null(p))
    {
        if (_bs_is_fn_boundary(p)) return (TSNode){{0}};
        if (strcmp(ts_node_type(p), "labeled_statement") == 0)
        {
            TSNode lname = ts_node_child_by_field_name(p, "label", 5);
            if (!ts_node_is_null(lname))
            {
                size_t ls = ts_node_start_byte(lname), le = ts_node_end_byte(lname);
                if (le - ls == label_len && memcmp(src + ls, label, label_len) == 0)
                    return p;
            }
        }
        p = ts_node_parent(p);
    }
    return (TSNode){{0}};
}

/* Returns 1 if `for_stmt` looks like a for-let that WILL emit a wrap
   (has lexical_declaration with `let` keyword in init/left position).
   Conservatively assumes captures exist — a for-let without captures
   gets the simple wrap which doesn't have a dispatch, so misclassifying
   either direction is harmless for label-propagation reasoning EXCEPT
   for the case where we'd cross a no-capture wrap thinking it's a
   wrap-emitting one. In practice, for-lets without captures are rare;
   this approximation keeps the code simple. */
static int _bs_for_is_let(TSNode for_stmt)
{
    const char *t = ts_node_type(for_stmt);
    if (strcmp(t, "for_statement") != 0 &&
        strcmp(t, "for_in_statement") != 0 &&
        strcmp(t, "for_of_statement") != 0)
        return 0;
    TSNode init = ts_node_child_by_field_name(for_stmt, "initializer", 11);
    if (ts_node_is_null(init))
        init = ts_node_child_by_field_name(for_stmt, "left", 4);
    if (ts_node_is_null(init)) return 0;
    if (strcmp(ts_node_type(init), "lexical_declaration") != 0) return 0;
    uint32_t nc = ts_node_child_count(init);
    for (uint32_t i = 0; i < nc; i++)
    {
        TSNode kid = ts_node_child(init, i);
        if (ts_node_is_named(kid)) break;
        if (strcmp(ts_node_type(kid), "let") == 0) return 1;
    }
    return 0;
}

/* Is there an ancestor for-let between `my_for` and the function
   boundary? If yes, we're inside another wrap → not outermost. */
static int _bs_has_outer_wrap(TSNode my_for)
{
    TSNode p = ts_node_parent(my_for);
    while (!ts_node_is_null(p))
    {
        if (_bs_is_fn_boundary(p)) return 0;
        if (_bs_for_is_let(p)) return 1;
        p = ts_node_parent(p);
    }
    return 0;
}

/* Can our wrap's dispatch directly do `break LABEL` / `continue LABEL`?
   That requires the labeled_statement to be reachable from our
   dispatch site without crossing another wrap's IIFE. */
static int _bs_label_dispatchable(TSNode my_for, TSNode label_target)
{
    TSNode p = ts_node_parent(my_for);
    while (!ts_node_is_null(p))
    {
        if (ts_node_eq(p, label_target)) return 1;
        if (_bs_is_fn_boundary(p)) return 0;
        if (_bs_for_is_let(p)) return 0;  /* intervening wrap hides label */
        p = ts_node_parent(p);
    }
    return 0;
}

/* Returns 1 if `node` is the same as `ancestor` or a descendant of it. */
static int _bs_node_is_inside(TSNode node, TSNode ancestor)
{
    TSNode p = node;
    while (!ts_node_is_null(p))
    {
        if (ts_node_eq(p, ancestor)) return 1;
        p = ts_node_parent(p);
    }
    return 0;
}

static void _bs_for_walk(TSNode node, const char *src, _BS_ForCtx *ctx,
                         int switch_depth, int loop_depth, int wrap_depth)
{
    if (ctx->giveup) return;

    const char *t = ts_node_type(node);

    if (strcmp(t, "for_statement") == 0 || strcmp(t, "for_in_statement") == 0 ||
        strcmp(t, "for_of_statement") == 0)
    {
        TSNode init = ts_node_child_by_field_name(node, "initializer", 11);
        if (ts_node_is_null(init))
            init = ts_node_child_by_field_name(node, "left", 4);
        int is_inner_forlet = (!ts_node_is_null(init) &&
                               strcmp(ts_node_type(init), "lexical_declaration") == 0);
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++)
            _bs_for_walk(ts_node_child(node, i), src, ctx,
                         switch_depth, loop_depth + 1,
                         is_inner_forlet ? wrap_depth + 1 : wrap_depth);
        return;
    }
    if (strcmp(t, "while_statement") == 0 || strcmp(t, "do_statement") == 0)
    {
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++)
            _bs_for_walk(ts_node_child(node, i), src, ctx,
                         switch_depth, loop_depth + 1, wrap_depth);
        return;
    }

    /* Nested function: separate scope. Don't descend. */
    if (_bs_is_fn_boundary(node))
        return;

    /* Switch: bare `break` inside targets the switch, not our loop. */
    if (strcmp(t, "switch_statement") == 0)
    {
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++)
            _bs_for_walk(ts_node_child(node, i), src, ctx,
                         switch_depth + 1, loop_depth, wrap_depth);
        return;
    }

    /* `arguments` reference at body scope (we never descend into nested fns,
       so any arguments we see is genuinely the enclosing function's). */
    if (strcmp(t, "identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == 9 && memcmp(src + s, "arguments", 9) == 0)
        {
            TSNode parent = ts_node_parent(node);
            int skip = 0;
            if (!ts_node_is_null(parent))
            {
                const char *pt = ts_node_type(parent);
                if (strcmp(pt, "member_expression") == 0)
                {
                    TSNode prop = ts_node_child_by_field_name(parent, "property", 8);
                    if (!ts_node_is_null(prop) && ts_node_eq(prop, node)) skip = 1;
                }
                else if (strcmp(pt, "pair") == 0 || strcmp(pt, "pair_pattern") == 0)
                {
                    TSNode key = ts_node_child_by_field_name(parent, "key", 3);
                    if (!ts_node_is_null(key) && ts_node_eq(key, node)) skip = 1;
                }
                else if (strcmp(pt, "variable_declarator") == 0)
                {
                    TSNode nn = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nn) && ts_node_eq(nn, node)) skip = 1;
                }
            }
            /* Only WE (this for-let's wrap) capture arguments at OUR
               body level. Inside a nested wrap's body, that wrap does
               its own capture. */
            if (!skip && wrap_depth == 0) _bs_sv_push(&ctx->args_positions, s);
        }
    }

    if (strcmp(t, "break_statement") == 0)
    {
        TSNode lblnode = (TSNode){{0}};
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++)
        {
            TSNode kid = ts_node_child(node, i);
            if (ts_node_is_named(kid) && strcmp(ts_node_type(kid), "statement_identifier") == 0)
            {
                lblnode = kid;
                break;
            }
        }
        if (ts_node_is_null(lblnode))
        {
            /* Bare break belongs to the innermost enclosing loop/switch
               (or our for-let if neither). We only OWN it if loop_depth
               == 0 and switch_depth == 0. */
            if (loop_depth > 0) return;
            if (switch_depth > 0) return;
            if (wrap_depth > 0) return;  /* inside another wrap — its concern */
            ctx->has_bare_break = 1;
            size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
            add_edit(ctx->edits, s, e, "return \"break\";", NULL);
            return;
        }
        /* Labeled break. */
        size_t ls = ts_node_start_byte(lblnode), le = ts_node_end_byte(lblnode);
        size_t llen = le - ls;
        TSNode target = _bs_find_label_target(node, src, src + ls, llen);
        if (ts_node_is_null(target))
        {
            ctx->giveup = 1;
            return;
        }
        if (_bs_node_is_inside(target, ctx->body))
            return;  /* nested labeled_statement handles it */
        /* Collect the label regardless of wrap_depth: our dispatch needs
           it to either dispatch directly or propagate. */
        _bs_lbl_add(&ctx->break_labels, src + ls, llen);
        if (wrap_depth > 0) return;  /* inner wrap emits the sentinel edit */
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        rp_string *rep = rp_string_new(32);
        rp_string_puts(rep, "return \"b:");
        rp_string_putsn(rep, src + ls, llen);
        rp_string_puts(rep, "\";");
        add_edit_take_ownership(ctx->edits, s, e, rp_string_steal(rep), NULL);
        rep = rp_string_free(rep);
        return;
    }

    if (strcmp(t, "continue_statement") == 0)
    {
        TSNode lblnode = (TSNode){{0}};
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc; i++)
        {
            TSNode kid = ts_node_child(node, i);
            if (ts_node_is_named(kid) && strcmp(ts_node_type(kid), "statement_identifier") == 0)
            {
                lblnode = kid;
                break;
            }
        }
        if (ts_node_is_null(lblnode))
        {
            if (loop_depth > 0) return;
            if (wrap_depth > 0) return;
            ctx->has_bare_continue = 1;
            size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
            add_edit(ctx->edits, s, e, "return \"continue\";", NULL);
            return;
        }
        size_t ls = ts_node_start_byte(lblnode), le = ts_node_end_byte(lblnode);
        size_t llen = le - ls;
        TSNode target = _bs_find_label_target(node, src, src + ls, llen);
        if (ts_node_is_null(target))
        {
            ctx->giveup = 1;
            return;
        }
        if (_bs_node_is_inside(target, ctx->body))
            return;
        _bs_lbl_add(&ctx->continue_labels, src + ls, llen);
        if (wrap_depth > 0) return;
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        rp_string *rep = rp_string_new(32);
        rp_string_puts(rep, "return \"c:");
        rp_string_putsn(rep, src + ls, llen);
        rp_string_puts(rep, "\";");
        add_edit_take_ownership(ctx->edits, s, e, rp_string_steal(rep), NULL);
        rep = rp_string_free(rep);
        return;
    }

    if (strcmp(t, "return_statement") == 0)
    {
        ctx->has_return = 1;
        if (wrap_depth > 0) {
            /* Inner wrap emits the sentinel; we just record that returns
               exist so our dispatch propagates. */
            return;
        }
        TSNode expr = ts_node_named_child(node, 0);
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (ts_node_is_null(expr))
        {
            add_edit(ctx->edits, s, e, "return {};", NULL);
        }
        else
        {
            add_edit(ctx->edits, s, s + 6, "return {v:", NULL);
            size_t ee = ts_node_end_byte(expr);
            add_edit(ctx->edits, ee, ee, "}", NULL);
        }
        return;
    }

    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        _bs_for_walk(ts_node_child(node, i), src, ctx, switch_depth, loop_depth, wrap_depth);
}

/* AST-based check: walk body for break/continue statements that belong
   to THIS loop (not nested loops or functions). Stops descending at
   function boundaries and nested loops. */
static int body_has_loop_flow_control(TSNode body)
{
    TSTreeCursor cur = ts_tree_cursor_new(body);
    int found = 0;

    /* descend into first child to skip the body node itself */
    if (!ts_tree_cursor_goto_first_child(&cur))
    {
        ts_tree_cursor_delete(&cur);
        return 0;
    }

    for (;;)
    {
        TSNode n = ts_tree_cursor_current_node(&cur);
        const char *t = ts_node_type(n);

        if (strcmp(t, "break_statement") == 0 || strcmp(t, "continue_statement") == 0 ||
            strcmp(t, "return_statement") == 0 || strcmp(t, "this") == 0)
        {
            found = 1;
            break;
        }

        /* Don't descend into nested FUNCTIONS — their break/continue/this
           are someone else's concern.  We DO descend into nested loops/
           switches because `this`, `return`, and labeled break/continue
           inside them still belong to OUR enclosing function and an IIFE
           wrap of our body would still hide them. */
        int skip_children = 0;
        if (strstr(t, "function") || strcmp(t, "arrow_function") == 0)
            skip_children = 1;

        if (!skip_children && ts_tree_cursor_goto_first_child(&cur))
            continue;

        while (!ts_tree_cursor_goto_next_sibling(&cur))
        {
            if (!ts_tree_cursor_goto_parent(&cur))
                goto done;
            /* stop if we've walked back up to the body node */
            if (ts_node_eq(ts_tree_cursor_current_node(&cur), body))
                goto done;
        }
    }
done:
    ts_tree_cursor_delete(&cur);
    return found;
}

/* =================================================================
   Block-scope rename pass (transpiler-todo.md §8 Phases 1-4).

   Babel-style: walk each function scope, find `let`/`const`
   declarations in nested blocks that would shadow another binding
   in the same function, and rename them with a fresh uid `_name`,
   `_name2`, … plus rewrite all lexical references that resolve to
   that binding.

   Per-function-scope freshness (not program-wide). Bindings we
   detect-against in a function:
     - var declarations at any nested depth
     - function declarations at any nested depth
     - class declarations at any nested depth
     - formal parameters of the function
     - let/const declarations elsewhere in the same function

   We do NOT descend into nested function/method/arrow bodies — those
   are independent scopes, processed when the cursor reaches them.
   ================================================================= */

/* Body defined here; `_BS_NameSet` typedef alias declared at line ~7775. */
struct _bs_name_set_s {
    char  **names;
    size_t *name_lens;
    size_t  len, cap;
};

static void _bs_ns_init(_BS_NameSet *s) { s->names = NULL; s->name_lens = NULL; s->len = s->cap = 0; }
static void _bs_ns_free(_BS_NameSet *s)
{
    for (size_t i = 0; i < s->len; i++) free(s->names[i]);
    free(s->names);
    free(s->name_lens);
    s->names = NULL;
    s->name_lens = NULL;
    s->len = s->cap = 0;
}
static void _bs_ns_add(_BS_NameSet *s, const char *name, size_t len)
{
    if (s->len == s->cap)
    {
        s->cap = s->cap ? s->cap * 2 : 8;
        REMALLOC(s->names, s->cap * sizeof(char *));
        REMALLOC(s->name_lens, s->cap * sizeof(size_t));
    }
    s->names[s->len] = (char *)malloc(len + 1);
    memcpy(s->names[s->len], name, len);
    s->names[s->len][len] = '\0';
    s->name_lens[s->len] = len;
    s->len++;
}
static int _bs_ns_contains(const _BS_NameSet *s, const char *name, size_t len)
{
    for (size_t i = 0; i < s->len; i++)
        if (s->name_lens[i] == len && memcmp(s->names[i], name, len) == 0)
            return 1;
    return 0;
}

/* Helper: is `node` a function-like scope boundary we shouldn't cross? */
static int _bs_is_fn_boundary(TSNode node)
{
    const char *t = ts_node_type(node);
    return (strcmp(t, "function_declaration") == 0 ||
            strcmp(t, "function_expression") == 0 ||
            strcmp(t, "function") == 0 ||
            strcmp(t, "arrow_function") == 0 ||
            strcmp(t, "generator_function_declaration") == 0 ||
            strcmp(t, "generator_function") == 0 ||
            strcmp(t, "generator_function_expression") == 0 ||
            strcmp(t, "method_definition") == 0);
}

/* Collect formal parameter identifier names from a `formal_parameters`
   or single-param `identifier` node. Skip destructuring for now (we
   don't rename around them, but we DO want to count them as shadow
   sources, so we recurse to pick up the binding names). */
static void _bs_collect_param_names(TSNode params, const char *src, _BS_NameSet *out)
{
    if (ts_node_is_null(params)) return;
    const char *pt = ts_node_type(params);
    if (strcmp(pt, "identifier") == 0)
    {
        size_t s = ts_node_start_byte(params), e = ts_node_end_byte(params);
        _bs_ns_add(out, src + s, e - s);
        return;
    }
    if (strcmp(pt, "shorthand_property_identifier_pattern") == 0 ||
        strcmp(pt, "shorthand_property_identifier") == 0)
    {
        size_t s = ts_node_start_byte(params), e = ts_node_end_byte(params);
        _bs_ns_add(out, src + s, e - s);
        return;
    }
    uint32_t c = ts_node_named_child_count(params);
    for (uint32_t i = 0; i < c; i++)
        _bs_collect_param_names(ts_node_named_child(params, i), src, out);
}

/* Recursively walk a node, collecting names of:
     - var declarations
     - function declarations
     - class declarations
   Skips descent into nested function-like bodies (those are separate
   scopes). Does NOT collect let/const here. */
static void _bs_collect_var_fn_class_names(TSNode node, const char *src, _BS_NameSet *out, int is_root)
{
    if (!is_root && _bs_is_fn_boundary(node))
        return;
    const char *t = ts_node_type(node);

    if (strcmp(t, "variable_declaration") == 0)
    {
        uint32_t dc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < dc; i++)
        {
            TSNode d = ts_node_named_child(node, i);
            if (strcmp(ts_node_type(d), "variable_declarator") != 0) continue;
            TSNode name = ts_node_child_by_field_name(d, "name", 4);
            if (ts_node_is_null(name)) continue;
            const char *nt = ts_node_type(name);
            if (strcmp(nt, "identifier") == 0)
            {
                size_t s = ts_node_start_byte(name), e = ts_node_end_byte(name);
                _bs_ns_add(out, src + s, e - s);
            }
            else if (strcmp(nt, "array_pattern") == 0 || strcmp(nt, "object_pattern") == 0)
            {
                _bs_collect_param_names(name, src, out);
            }
        }
    }
    else if (strcmp(t, "function_declaration") == 0 ||
             strcmp(t, "generator_function_declaration") == 0 ||
             strcmp(t, "class_declaration") == 0)
    {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name) && strcmp(ts_node_type(name), "identifier") == 0)
        {
            size_t s = ts_node_start_byte(name), e = ts_node_end_byte(name);
            _bs_ns_add(out, src + s, e - s);
        }
        /* Don't descend INTO the function body. */
        return;
    }
    /* Recurse into children. */
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        _bs_collect_var_fn_class_names(ts_node_child(node, i), src, out, 0);
}

/* Recursively walk, collecting all let/const-bound names from the
   function scope (any nesting depth, but stopping at nested function
   boundaries). Two outputs: `all_names` gets every let/const name
   (used as shadow sources), `decl_list` gets the declarator TSNodes
   themselves for later targeted rewriting. */
typedef struct {
    TSNode *a;       /* the variable_declarator nodes */
    size_t  len, cap;
} _BS_NodeVec;

static void _bs_nv_init(_BS_NodeVec *v) { v->a = NULL; v->len = v->cap = 0; }
static void _bs_nv_free(_BS_NodeVec *v) { free(v->a); v->a = NULL; v->len = v->cap = 0; }
static void _bs_nv_push(_BS_NodeVec *v, TSNode n)
{
    if (v->len == v->cap)
    {
        v->cap = v->cap ? v->cap * 2 : 8;
        REMALLOC(v->a, v->cap * sizeof(TSNode));
    }
    v->a[v->len++] = n;
}

static void _bs_collect_lexical_decls(TSNode node, const char *src,
                                      _BS_NameSet *all_names,
                                      _BS_NodeVec *decl_list,
                                      int is_root)
{
    if (!is_root && _bs_is_fn_boundary(node))
        return;
    const char *t = ts_node_type(node);

    if (strcmp(t, "lexical_declaration") == 0)
    {
        uint32_t dc = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < dc; i++)
        {
            TSNode d = ts_node_named_child(node, i);
            if (strcmp(ts_node_type(d), "variable_declarator") != 0) continue;
            TSNode name = ts_node_child_by_field_name(d, "name", 4);
            if (ts_node_is_null(name)) continue;
            const char *nt = ts_node_type(name);
            if (strcmp(nt, "identifier") == 0)
            {
                size_t s = ts_node_start_byte(name), e = ts_node_end_byte(name);
                _bs_ns_add(all_names, src + s, e - s);
                _bs_nv_push(decl_list, d);
            }
            /* TODO: destructuring patterns in let/const */
        }
    }

    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        _bs_collect_lexical_decls(ts_node_child(node, i), src, all_names, decl_list, 0);
}

/* Generate a fresh uid for `base`. Strategy: try `_base`, `_base2`,
   `_base3`, … until we find one not in `taken`. Caller frees. */
static char *_bs_fresh_uid(const char *base, size_t base_len, const _BS_NameSet *taken)
{
    /* strip leading _ and trailing digits from base */
    while (base_len > 0 && base[0] == '_') { base++; base_len--; }
    while (base_len > 0 && isdigit((unsigned char)base[base_len - 1])) { base_len--; }
    if (base_len == 0) { base = "ref"; base_len = 3; }

    /* try _base, _base2, _base3, ... */
    char *buf = NULL;
    size_t cap = base_len + 16;
    REMALLOC(buf, cap);
    int i = 1;
    for (;;)
    {
        int n;
        if (i == 1) n = snprintf(buf, cap, "_%.*s", (int)base_len, base);
        else        n = snprintf(buf, cap, "_%.*s%d", (int)base_len, base, i);
        if ((size_t)n >= cap)
        {
            cap = (size_t)n + 1;
            REMALLOC(buf, cap);
            continue;
        }
        if (!_bs_ns_contains(taken, buf, (size_t)n))
            return buf;
        i++;
        if (i > 99999) { free(buf); return NULL; } /* safety */
    }
}

/* Determine the lexical scope for a let/const declaration: the
   nearest enclosing statement_block (or for-loop body / arrow concise
   body / function body, whichever first). */
static TSNode _bs_enclosing_block(TSNode decl)
{
    TSNode p = ts_node_parent(decl);
    while (!ts_node_is_null(p))
    {
        const char *pt = ts_node_type(p);
        if (strcmp(pt, "statement_block") == 0 ||
            strcmp(pt, "program") == 0 ||
            strcmp(pt, "switch_case") == 0 ||
            strcmp(pt, "switch_default") == 0)
            return p;
        if (_bs_is_fn_boundary(p)) return p;
        p = ts_node_parent(p);
    }
    return p;
}

/* Walk `block` recursively, finding every `identifier` reference that
   matches `name` and resolves to the binding declared at `decl`.
   Emit an edit replacing that identifier with `new_name`. Skip:
     - property identifiers after `.`
     - object-literal keys (pair, method names)
     - destructuring keys
     - declarators that redeclare `name` (and their entire scope)
     - the declarator's own name node (that's rewritten separately)
   For shorthand_property_identifier, expand to `name: _name` rather
   than just replace. */
/* Does `block` (or one of its direct children) declare `name` via
   `let`/`const`, in a declarator OTHER than `skip_decl`? Used to decide
   whether to descend into a nested block when rewriting references —
   if the nested block has its own `let name` binding, that block's
   scope shadows the outer one and we must not rewrite inside it. */
static int _bs_block_shadows(TSNode block, const char *src,
                             const char *name, size_t name_len,
                             TSNode skip_decl)
{
    if (ts_node_is_null(block)) return 0;
    uint32_t cc = ts_node_child_count(block);
    for (uint32_t i = 0; i < cc; i++)
    {
        TSNode child = ts_node_child(block, i);
        if (strcmp(ts_node_type(child), "lexical_declaration") != 0) continue;
        uint32_t dc = ts_node_named_child_count(child);
        for (uint32_t j = 0; j < dc; j++)
        {
            TSNode d = ts_node_named_child(child, j);
            if (strcmp(ts_node_type(d), "variable_declarator") != 0) continue;
            if (ts_node_eq(d, skip_decl)) continue;
            TSNode nn = ts_node_child_by_field_name(d, "name", 4);
            if (ts_node_is_null(nn)) continue;
            if (strcmp(ts_node_type(nn), "identifier") != 0) continue;
            size_t ns = ts_node_start_byte(nn), ne = ts_node_end_byte(nn);
            if (ne - ns == name_len && memcmp(src + ns, name, name_len) == 0)
                return 1;
        }
    }
    return 0;
}

static void _bs_rewrite_refs_in_block(TSNode node, const char *src,
                                      const char *name, size_t name_len,
                                      const char *new_name,
                                      TSNode decl_node,
                                      EditList *edits, RangeList *claimed)
{
    const char *t = ts_node_type(node);

    /* Don't descend into nested function/method/arrow — they're separate scopes. */
    if (_bs_is_fn_boundary(node))
        return;

    /* If this node is itself a statement_block that declares `name`
       (other than via the declarator we're processing), its scope
       shadows the outer one — don't descend at all. The decl-node we
       were called with is the OUTER one whose refs we're chasing;
       inside this shadowing block, references to `name` belong to the
       INNER binding (which will be — or has been — renamed by its own
       pass). */
    if (strcmp(t, "statement_block") == 0 ||
        strcmp(t, "switch_case") == 0 ||
        strcmp(t, "switch_default") == 0)
    {
        if (_bs_block_shadows(node, src, name, name_len, decl_node))
            return;
    }

    /* Identifier reference? */
    if (strcmp(t, "identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == name_len && memcmp(src + s, name, name_len) == 0)
        {
            /* Skip if parent is member_expression with this as property,
               or pair with this as key, or variable_declarator name
               position, etc. */
            TSNode parent = ts_node_parent(node);
            int skip = 0;
            if (!ts_node_is_null(parent))
            {
                const char *pt = ts_node_type(parent);
                if (strcmp(pt, "member_expression") == 0)
                {
                    TSNode prop = ts_node_child_by_field_name(parent, "property", 8);
                    if (!ts_node_is_null(prop) && ts_node_eq(prop, node)) skip = 1;
                }
                else if (strcmp(pt, "pair") == 0 || strcmp(pt, "pair_pattern") == 0)
                {
                    TSNode key = ts_node_child_by_field_name(parent, "key", 3);
                    if (!ts_node_is_null(key) && ts_node_eq(key, node)) skip = 1;
                }
                else if (strcmp(pt, "method_definition") == 0)
                {
                    TSNode nname = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nname) && ts_node_eq(nname, node)) skip = 1;
                }
                else if (strcmp(pt, "variable_declarator") == 0)
                {
                    TSNode nn = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nn) && ts_node_eq(nn, node)) skip = 1;
                }
            }
            if (!skip)
                add_edit(edits, s, e, new_name, claimed);
        }
    }

    /* Shorthand property `{ name }` — when our binding is referenced
       here it must be expanded to `{ name: _name }`. */
    if (strcmp(t, "shorthand_property_identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == name_len && memcmp(src + s, name, name_len) == 0)
        {
            /* Insert ": _name" after the identifier; identifier itself stays. */
            rp_string *ins = rp_string_new(name_len + 8);
            rp_string_puts(ins, ": ");
            rp_string_puts(ins, new_name);
            add_edit_take_ownership(edits, e, e, rp_string_steal(ins), claimed);
            ins = rp_string_free(ins);
        }
    }

    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        _bs_rewrite_refs_in_block(ts_node_child(node, i), src, name, name_len, new_name, decl_node, edits, claimed);
}

/* Does the function body (walked from `node`) contain any identifier
   reference to `name` that lives OUTSIDE `my_block`? Used to decide
   whether a `let name` in a nested block needs renaming to avoid the
   let→var hoisting capturing free references that were meant to
   resolve in an outer scope.

   Skips:
     - nested function/method/arrow bodies (separate scopes)
     - descent into `my_block` itself (refs there belong to this binding)
     - property-key positions (member.prop, pair-key, method-name,
       declarator-name, fn-decl-name) — those aren't reads
   Counts:
     - bare identifier references
     - shorthand_property_identifier references (`{ name }`) — those
       expand to `{ name: name }` and the value side IS a read */
static int _bs_has_ref_outside_block(TSNode node, const char *src,
                                     const char *name, size_t name_len,
                                     TSNode my_block)
{
    if (_bs_is_fn_boundary(node))
        return 0;
    if (ts_node_eq(node, my_block))
        return 0;

    const char *t = ts_node_type(node);
    if (strcmp(t, "identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == name_len && memcmp(src + s, name, name_len) == 0)
        {
            TSNode parent = ts_node_parent(node);
            if (!ts_node_is_null(parent))
            {
                const char *pt = ts_node_type(parent);
                if (strcmp(pt, "member_expression") == 0)
                {
                    TSNode prop = ts_node_child_by_field_name(parent, "property", 8);
                    if (!ts_node_is_null(prop) && ts_node_eq(prop, node)) goto descend;
                }
                else if (strcmp(pt, "pair") == 0 || strcmp(pt, "pair_pattern") == 0)
                {
                    TSNode key = ts_node_child_by_field_name(parent, "key", 3);
                    if (!ts_node_is_null(key) && ts_node_eq(key, node)) goto descend;
                }
                else if (strcmp(pt, "method_definition") == 0)
                {
                    TSNode nname = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nname) && ts_node_eq(nname, node)) goto descend;
                }
                else if (strcmp(pt, "variable_declarator") == 0)
                {
                    TSNode nn = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nn) && ts_node_eq(nn, node)) goto descend;
                }
                else if (strcmp(pt, "function_declaration") == 0 ||
                         strcmp(pt, "generator_function_declaration") == 0 ||
                         strcmp(pt, "class_declaration") == 0)
                {
                    TSNode fname = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(fname) && ts_node_eq(fname, node)) goto descend;
                }
            }
            return 1;
        }
    }
    if (strcmp(t, "shorthand_property_identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == name_len && memcmp(src + s, name, name_len) == 0)
            return 1;
    }
descend:;
    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
    {
        if (_bs_has_ref_outside_block(ts_node_child(node, i), src, name, name_len, my_block))
            return 1;
    }
    return 0;
}

/* --------------- Import live-binding rewrite helpers ---------------
   Used by do_named_imports to rewrite each named-import binding into
   member-expression access on a `__tmpModImpN` namespace var. This
   restores ESM-style live-binding semantics: a reference reads the
   import at CALL time rather than at declaration time, so circular
   modules don't snapshot undefined.  Reuses _bs_* helpers for scope
   walking and shadow detection. */

/* True iff `fn_node` (a function-like) declares `name` as a param
   or in its body via var/let/const/function/class — in which case
   refs inside the function body refer to the LOCAL binding, not
   the outer import. */
static int _imp_fn_declares_name(TSNode fn_node, const char *src,
                                 const char *name, size_t name_len)
{
    TSNode params = ts_node_child_by_field_name(fn_node, "parameters", 10);
    /* Arrow function with a bare-identifier param (`x => …`) stores the
       parameter under the field `parameter` (singular), not
       `parameters`.  Check that variant too so the body of such an
       arrow doesn't get its references rewritten — the parameter
       shadows the import. */
    TSNode param1 = ts_node_child_by_field_name(fn_node, "parameter", 9);
    TSNode body = ts_node_child_by_field_name(fn_node, "body", 4);

    _BS_NameSet ns;
    _bs_ns_init(&ns);

    if (!ts_node_is_null(params))
        _bs_collect_param_names(params, src, &ns);
    if (!ts_node_is_null(param1))
        _bs_collect_param_names(param1, src, &ns);

    /* For non-arrow function-likes, the function's own name is in
       scope inside its body. */
    TSNode fn_name = ts_node_child_by_field_name(fn_node, "name", 4);
    if (!ts_node_is_null(fn_name) && strcmp(ts_node_type(fn_name), "identifier") == 0)
    {
        size_t s = ts_node_start_byte(fn_name), e = ts_node_end_byte(fn_name);
        _bs_ns_add(&ns, src + s, e - s);
    }

    if (!ts_node_is_null(body))
    {
        uint32_t bc = ts_node_named_child_count(body);
        for (uint32_t i = 0; i < bc; i++)
            _bs_collect_var_fn_class_names(ts_node_named_child(body, i), src, &ns, 0);
        _BS_NodeVec lex_decls;
        _bs_nv_init(&lex_decls);
        for (uint32_t i = 0; i < bc; i++)
            _bs_collect_lexical_decls(ts_node_named_child(body, i), src, &ns, &lex_decls, 0);
        _bs_nv_free(&lex_decls);
    }

    int found = _bs_ns_contains(&ns, name, name_len);
    _bs_ns_free(&ns);
    return found;
}

static void _imp_rewrite_refs(TSNode node, const char *src,
                              const char *name, size_t name_len,
                              const char *new_text,
                              EditList *edits, RangeList *claimed)
{
    const char *t = ts_node_type(node);

    /* Skip the import_statement subtree itself. */
    if (strcmp(t, "import_statement") == 0)
        return;

    /* If we cross a function-like boundary that shadows the name,
       don't descend; otherwise descend (closures inside still need
       the rewrite). */
    if (_bs_is_fn_boundary(node))
    {
        if (_imp_fn_declares_name(node, src, name, name_len))
            return;
    }

    /* If this block re-declares the name via let/const, stop. */
    if (strcmp(t, "statement_block") == 0 ||
        strcmp(t, "switch_case") == 0 ||
        strcmp(t, "switch_default") == 0)
    {
        TSNode none = {{0}};
        if (_bs_block_shadows(node, src, name, name_len, none))
            return;
    }

    /* `catch (e) { ... }`: the catch parameter introduces a fresh
       binding for the body.  If it matches the import name, stop —
       references inside the catch body are the local binding, not
       the import. */
    if (strcmp(t, "catch_clause") == 0)
    {
        TSNode param = ts_node_child_by_field_name(node, "parameter", 9);
        if (!ts_node_is_null(param) && strcmp(ts_node_type(param), "identifier") == 0)
        {
            size_t ps = ts_node_start_byte(param), pe = ts_node_end_byte(param);
            if (pe - ps == name_len && memcmp(src + ps, name, name_len) == 0)
                return;
        }
    }

    if (strcmp(t, "identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == name_len && memcmp(src + s, name, name_len) == 0)
        {
            TSNode parent = ts_node_parent(node);
            int skip = 0;
            if (!ts_node_is_null(parent))
            {
                const char *pt = ts_node_type(parent);
                if (strcmp(pt, "member_expression") == 0)
                {
                    TSNode prop = ts_node_child_by_field_name(parent, "property", 8);
                    if (!ts_node_is_null(prop) && ts_node_eq(prop, node)) skip = 1;
                }
                else if (strcmp(pt, "pair") == 0 || strcmp(pt, "pair_pattern") == 0)
                {
                    TSNode key = ts_node_child_by_field_name(parent, "key", 3);
                    if (!ts_node_is_null(key) && ts_node_eq(key, node)) skip = 1;
                }
                else if (strcmp(pt, "method_definition") == 0)
                {
                    TSNode nname = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nname) && ts_node_eq(nname, node)) skip = 1;
                }
                else if (strcmp(pt, "variable_declarator") == 0)
                {
                    TSNode nn = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nn) && ts_node_eq(nn, node)) skip = 1;
                }
                else if (strcmp(pt, "import_specifier") == 0 ||
                         strcmp(pt, "import_clause") == 0 ||
                         strcmp(pt, "named_imports") == 0)
                {
                    skip = 1;
                }
                else if (strcmp(pt, "export_specifier") == 0)
                {
                    /* `export { name }` / `export { name as alias }`:
                       the rewrite is the export rewriter's job, not
                       ours.  Rewriting the inner identifier produces
                       invalid `export { mod.name }` syntax and also
                       races against rewrite_export_node's own
                       range-claim.  rewrite_export_node will look up
                       imports separately and emit the modImp form on
                       the RHS.  Regression test:
                       test/transpile-test.js "Re-export of named
                       import (passthrough)". */
                    skip = 1;
                }
                else if (strcmp(pt, "required_parameter") == 0 ||
                         strcmp(pt, "optional_parameter") == 0 ||
                         strcmp(pt, "rest_pattern") == 0 ||
                         strcmp(pt, "assignment_pattern") == 0)
                {
                    skip = 1;
                }
                else if (strcmp(pt, "arrow_function") == 0)
                {
                    /* `x => x`: the bare identifier IS the parameter
                       field — rewriting it would produce
                       `__tmpModImp0.x => __tmpModImp0.x`, illegal in a
                       parameter position.  Only skip the parameter
                       binding, not body references. */
                    TSNode param = ts_node_child_by_field_name(parent, "parameter", 9);
                    if (!ts_node_is_null(param) && ts_node_eq(param, node))
                        skip = 1;
                }
                else if (strcmp(pt, "catch_clause") == 0)
                {
                    /* `catch (e) {...}`: skip rewriting the catch
                       parameter identifier itself.  Body refs are
                       handled by the catch-shadowing check on descent
                       below. */
                    TSNode param = ts_node_child_by_field_name(parent, "parameter", 9);
                    if (!ts_node_is_null(param) && ts_node_eq(param, node))
                        skip = 1;
                }
                else if (strcmp(pt, "function_declaration") == 0 ||
                         strcmp(pt, "function") == 0 ||
                         strcmp(pt, "function_expression") == 0 ||
                         strcmp(pt, "generator_function_declaration") == 0 ||
                         strcmp(pt, "generator_function") == 0 ||
                         strcmp(pt, "generator_function_expression") == 0 ||
                         strcmp(pt, "class_declaration") == 0 ||
                         strcmp(pt, "class") == 0)
                {
                    TSNode nn = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nn) && ts_node_eq(nn, node)) skip = 1;
                }
                else if (strcmp(pt, "labeled_statement") == 0 ||
                         strcmp(pt, "break_statement") == 0 ||
                         strcmp(pt, "continue_statement") == 0)
                {
                    skip = 1;
                }
            }
            if (!skip)
                add_edit(edits, s, e, new_text, claimed);
        }
    }

    if (strcmp(t, "shorthand_property_identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == name_len && memcmp(src + s, name, name_len) == 0)
        {
            rp_string *ins = rp_string_new(name_len + 16);
            rp_string_puts(ins, ": ");
            rp_string_puts(ins, new_text);
            add_edit_take_ownership(edits, e, e, rp_string_steal(ins), claimed);
            ins = rp_string_free(ins);
        }
    }

    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        _imp_rewrite_refs(ts_node_child(node, i), src, name, name_len, new_text, edits, claimed);
}

static TSNode _imp_find_program(TSNode n)
{
    while (!ts_node_is_null(n))
    {
        if (strcmp(ts_node_type(n), "program") == 0) return n;
        n = ts_node_parent(n);
    }
    return n;
}

/* Main entry: process one function scope (or `program`). */
static int rewrite_block_scope_rename(EditList *edits, const char *src, TSNode fn_node,
                                      RangeList *claimed, int overlaps)
{
    /* Find the function's body. For `program` the node itself is the
       body. For arrow_function with a concise body the body is an
       expression — no let/const can appear there, so skip. */
    const char *t = ts_node_type(fn_node);
    TSNode body;
    if (strcmp(t, "program") == 0)
        body = fn_node;
    else
    {
        body = ts_node_child_by_field_name(fn_node, "body", 4);
        if (ts_node_is_null(body)) return 0;
        if (strcmp(ts_node_type(body), "statement_block") != 0)
            return 0;  /* concise arrow body — nothing to do */
    }

    /* Step 1: collect all function-scope names (var/fn/class/params). */
    _BS_NameSet scope_names;
    _bs_ns_init(&scope_names);

    if (strcmp(t, "program") != 0)
    {
        TSNode params = ts_node_child_by_field_name(fn_node, "parameters", 10);
        _bs_collect_param_names(params, src, &scope_names);
        /* If fn_node has a `name` field (function_declaration), include it
           — JS function-declaration names are visible inside the function. */
        TSNode fn_name = ts_node_child_by_field_name(fn_node, "name", 4);
        if (!ts_node_is_null(fn_name) && strcmp(ts_node_type(fn_name), "identifier") == 0)
        {
            size_t s = ts_node_start_byte(fn_name), e = ts_node_end_byte(fn_name);
            _bs_ns_add(&scope_names, src + s, e - s);
        }
    }

    uint32_t bc = ts_node_child_count(body);
    for (uint32_t i = 0; i < bc; i++)
        _bs_collect_var_fn_class_names(ts_node_child(body, i), src, &scope_names, 0);

    /* Step 2: collect all let/const declarators. */
    _BS_NameSet lex_names;
    _bs_ns_init(&lex_names);
    _BS_NodeVec lex_decls;
    _bs_nv_init(&lex_decls);
    for (uint32_t i = 0; i < bc; i++)
        _bs_collect_lexical_decls(ts_node_child(body, i), src, &lex_names, &lex_decls, 0);

    if (lex_decls.len == 0)
    {
        _bs_ns_free(&scope_names);
        _bs_ns_free(&lex_names);
        _bs_nv_free(&lex_decls);
        return 0;
    }

    /* `taken` is the union of all names used in this function: scope
       names + let/const names + already-generated uids (we'll add to
       this set as we rename). */
    _BS_NameSet taken;
    _bs_ns_init(&taken);
    for (size_t i = 0; i < scope_names.len; i++)
        _bs_ns_add(&taken, scope_names.names[i], scope_names.name_lens[i]);
    for (size_t i = 0; i < lex_names.len; i++)
        if (!_bs_ns_contains(&taken, lex_names.names[i], lex_names.name_lens[i]))
            _bs_ns_add(&taken, lex_names.names[i], lex_names.name_lens[i]);

    /* Step 3: for each let/const declarator, decide rename.
       A binding shadows iff:
         (a) Its name appears in scope_names (var/fn/class/param), OR
         (b) Some OTHER let/const with the same name is declared in a
             strictly ENCLOSING block within this function scope. */
    int did_any_rename = 0;
    int saw_overlap = 0;

    for (size_t di = 0; di < lex_decls.len; di++)
    {
        TSNode decl = lex_decls.a[di];
        TSNode name_node = ts_node_child_by_field_name(decl, "name", 4);
        if (ts_node_is_null(name_node)) continue;
        if (strcmp(ts_node_type(name_node), "identifier") != 0) continue;
        size_t ns = ts_node_start_byte(name_node), ne = ts_node_end_byte(name_node);
        size_t nlen = ne - ns;
        const char *name = src + ns;

        int conflicts = 0;
        TSNode my_block = _bs_enclosing_block(decl);
        if (_bs_ns_contains(&scope_names, name, nlen))
            conflicts = 1;
        if (!conflicts)
        {
            /* Check sibling lex_decls. Conflict only when another decl
               with the same name lives in a STRICTLY ENCLOSING block
               (its block is a proper ancestor of this decl's block). */
            for (size_t li = 0; li < lex_decls.len && !conflicts; li++)
            {
                if (li == di) continue;
                TSNode other = lex_decls.a[li];
                TSNode oname = ts_node_child_by_field_name(other, "name", 4);
                if (ts_node_is_null(oname)) continue;
                if (strcmp(ts_node_type(oname), "identifier") != 0) continue;
                size_t os = ts_node_start_byte(oname), oe = ts_node_end_byte(oname);
                if (oe - os != nlen || memcmp(src + os, name, nlen) != 0) continue;
                /* Same name. Is `other`'s block a proper ancestor of mine? */
                TSNode oblock = _bs_enclosing_block(other);
                TSNode p = ts_node_parent(my_block);
                while (!ts_node_is_null(p))
                {
                    if (ts_node_eq(p, oblock)) { conflicts = 1; break; }
                    if (_bs_is_fn_boundary(p)) break;
                    p = ts_node_parent(p);
                }
            }
        }
        if (!conflicts && !ts_node_is_null(my_block) && !ts_node_eq(my_block, body))
        {
            /* Free-reference check: a `let name` in a nested block
               needs renaming if any reference to `name` exists outside
               its block within the same function. Otherwise the
               let→var lowering's hoisted local var would capture
               references that were meant to resolve in an outer scope
               (closure capture, sibling-block let, etc.).
               Skipped for top-level (my_block == body) — those `let`s
               share scope with params/vars and any conflict is already
               caught above. */
            if (_bs_has_ref_outside_block(body, src, name, nlen, my_block))
                conflicts = 1;
        }
        if (!conflicts) continue;

        if (overlaps)
        {
            saw_overlap = 1;
            continue;
        }

        /* Generate fresh uid and reserve. */
        char *new_name = _bs_fresh_uid(name, nlen, &taken);
        if (!new_name) continue;
        _bs_ns_add(&taken, new_name, strlen(new_name));

        /* Rewrite the declarator's name. */
        add_edit(edits, ns, ne, new_name, claimed);

        /* Rewrite all references in the declarator's enclosing block. */
        TSNode encl = _bs_enclosing_block(decl);
        if (!ts_node_is_null(encl))
            _bs_rewrite_refs_in_block(encl, src, name, nlen, new_name, decl, edits, claimed);

        free(new_name);
        did_any_rename = 1;
    }

    _bs_ns_free(&scope_names);
    _bs_ns_free(&lex_names);
    _bs_nv_free(&lex_decls);
    _bs_ns_free(&taken);

    if (saw_overlap) return 1;
    return did_any_rename ? 1 : 0;
}

#ifdef TDZ_RUNTIME_CHECKS
/* Runtime TDZ + const-reassign checks for let/const bindings.
   Detects statically (at transpile time) the common cases where node
   would throw at runtime:
     - reading a let/const before its declaration in the same lexical
       block (TDZ) → throws ReferenceError
     - assigning to a const (with `=`, `+=`, `++`, etc.) → throws
       TypeError
   For each such case the offending expression is replaced with an
   inline IIFE that throws when reached. Nested function bodies are
   NOT descended into — closure-captured TDZ reads and const-reassigns
   inside closures are missed (MVP). */

typedef struct {
    const char *src;
    const char *name;
    size_t name_len;
    size_t decl_start;
    int is_const;
    EditList *edits;
    RangeList *claimed;
    int did_any;
} _TdzCtx;

static void _tdz_walk_block(TSNode node, _TdzCtx *ctx)
{
    if (_bs_is_fn_boundary(node)) return;

    const char *t = ts_node_type(node);

    if (strcmp(t, "identifier") == 0)
    {
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        if (e - s == ctx->name_len &&
            memcmp(ctx->src + s, ctx->name, ctx->name_len) == 0)
        {
            TSNode parent = ts_node_parent(node);
            if (!ts_node_is_null(parent))
            {
                const char *pt = ts_node_type(parent);
                int skip = 0;
                if (strcmp(pt, "member_expression") == 0)
                {
                    TSNode prop = ts_node_child_by_field_name(parent, "property", 8);
                    if (!ts_node_is_null(prop) && ts_node_eq(prop, node)) skip = 1;
                }
                else if (strcmp(pt, "pair") == 0 || strcmp(pt, "pair_pattern") == 0)
                {
                    TSNode key = ts_node_child_by_field_name(parent, "key", 3);
                    if (!ts_node_is_null(key) && ts_node_eq(key, node)) skip = 1;
                }
                else if (strcmp(pt, "method_definition") == 0)
                {
                    TSNode nname = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nname) && ts_node_eq(nname, node)) skip = 1;
                }
                else if (strcmp(pt, "variable_declarator") == 0)
                {
                    TSNode nn = ts_node_child_by_field_name(parent, "name", 4);
                    if (!ts_node_is_null(nn) && ts_node_eq(nn, node)) skip = 1;
                }
                if (!skip)
                {
                    int is_lhs = 0;
                    TSNode target = parent;
                    if (strcmp(pt, "assignment_expression") == 0 ||
                        strcmp(pt, "augmented_assignment_expression") == 0)
                    {
                        TSNode left = ts_node_child_by_field_name(parent, "left", 4);
                        if (!ts_node_is_null(left) && ts_node_eq(left, node))
                            is_lhs = 1;
                    }
                    else if (strcmp(pt, "update_expression") == 0)
                    {
                        is_lhs = 1;
                    }

                    if (ctx->is_const && is_lhs)
                    {
                        /* Replace the whole assignment/update expression. */
                        size_t a_s = ts_node_start_byte(target),
                               a_e = ts_node_end_byte(target);
                        char *r = strdup("(function(){throw new TypeError(\"Assignment to constant variable.\");})()");
                        add_edit_take_ownership(ctx->edits, a_s, a_e, r, ctx->claimed);
                        ctx->did_any = 1;
                    }
                    else if (!is_lhs && s < ctx->decl_start)
                    {
                        /* TDZ read before declaration. */
                        rp_string *rep = rp_string_new(96);
                        rp_string_appendf(rep,
                            "(function(){throw new ReferenceError(\"Cannot access '%.*s' before initialization\");})()",
                            (int)ctx->name_len, ctx->name);
                        char *rdup = rp_string_steal(rep);
                        rep = rp_string_free(rep);
                        add_edit_take_ownership(ctx->edits, s, e, rdup, ctx->claimed);
                        ctx->did_any = 1;
                    }
                }
            }
        }
    }

    uint32_t cc = ts_node_child_count(node);
    for (uint32_t i = 0; i < cc; i++)
        _tdz_walk_block(ts_node_child(node, i), ctx);
}

static int rewrite_let_const_runtime_checks(EditList *edits, const char *src,
                                            TSNode fn_node, RangeList *claimed, int overlaps)
{
    const char *t = ts_node_type(fn_node);
    TSNode body;
    if (strcmp(t, "program") == 0)
        body = fn_node;
    else
    {
        body = ts_node_child_by_field_name(fn_node, "body", 4);
        if (ts_node_is_null(body)) return 0;
        if (strcmp(ts_node_type(body), "statement_block") != 0) return 0;
    }

    _BS_NameSet lex_names;
    _bs_ns_init(&lex_names);
    _BS_NodeVec lex_decls;
    _bs_nv_init(&lex_decls);
    uint32_t bc = ts_node_child_count(body);
    for (uint32_t i = 0; i < bc; i++)
        _bs_collect_lexical_decls(ts_node_child(body, i), src, &lex_names, &lex_decls, 0);

    if (lex_decls.len == 0)
    {
        _bs_ns_free(&lex_names);
        _bs_nv_free(&lex_decls);
        return 0;
    }

    if (overlaps)
    {
        _bs_ns_free(&lex_names);
        _bs_nv_free(&lex_decls);
        return 1;
    }

    int did_any = 0;

    for (size_t di = 0; di < lex_decls.len; di++)
    {
        TSNode decl = lex_decls.a[di];
        TSNode name_node = ts_node_child_by_field_name(decl, "name", 4);
        if (ts_node_is_null(name_node)) continue;
        if (strcmp(ts_node_type(name_node), "identifier") != 0) continue;
        size_t ns = ts_node_start_byte(name_node), ne = ts_node_end_byte(name_node);
        size_t nlen = ne - ns;
        const char *name = src + ns;

        /* Determine if this declarator is const by inspecting its
           parent lexical_declaration's keyword. */
        TSNode lex_node = ts_node_parent(decl);
        int is_const = 0;
        uint32_t lc = ts_node_child_count(lex_node);
        for (uint32_t i = 0; i < lc; i++)
        {
            TSNode kid = ts_node_child(lex_node, i);
            if (ts_node_is_named(kid)) continue;
            const char *kw = ts_node_type(kid);
            if (strcmp(kw, "const") == 0) { is_const = 1; break; }
            if (strcmp(kw, "let") == 0) break;
        }

        TSNode encl = _bs_enclosing_block(decl);
        if (ts_node_is_null(encl)) continue;

        _TdzCtx ctx;
        ctx.src = src;
        ctx.name = name;
        ctx.name_len = nlen;
        ctx.decl_start = ts_node_start_byte(decl);
        ctx.is_const = is_const;
        ctx.edits = edits;
        ctx.claimed = claimed;
        ctx.did_any = 0;

        _tdz_walk_block(encl, &ctx);
        if (ctx.did_any) did_any = 1;
    }

    _bs_ns_free(&lex_names);
    _bs_nv_free(&lex_decls);
    return did_any;
}
#endif /* TDZ_RUNTIME_CHECKS */

static int rewrite_lexical_declaration(EditList *edits, const char *src, TSNode lexical_decl, RangeList *claimed,
                                       int overlaps, int no_program_wrap)
{
    int ret = 0;
    uint32_t c = ts_node_child_count(lexical_decl);
    int have_let = 0;

    // --- 1) Replace 'let' keyword with 'var'. Leave 'const' alone:
    // duktape parses 'const' natively (treats it as var), and rewriting
    // it triggers the block-wrap path below which broke 'arguments'.
    // See transpiler-todo.md §8 for the planned proper fix.
    int have_const = 0;
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
            /* Leave const alone — duktape parses it natively.
               Don't fall through to the IIFE wrap below. */
            have_const = 1;
            break;
        }
    }
    (void)have_const;

    /* Only `let` needs the wrap/scope handling below. `const` is left
       fully alone for Phase 0 (see transpiler-todo.md §8). */
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

                        /* §8 Phase 5: capture-aware wrapping.
                           - No captures: skip wrap entirely (closure-of-final
                             doesn't matter when no closure is created).
                           - Captures, no flow control: simple IIFE wrap.
                           - Captures with break/continue/return: emit babel-
                             style sentinel wrap so flow-control can cross
                             the synthesized function boundary.
                           - Captures with `arguments` or labeled break/
                             continue: can't safely wrap; fall back to no
                             wrap (closure semantics will be var-like, an
                             accepted limitation). */
                        _BS_NameSet names_set;
                        _bs_ns_init(&names_set);
                        {
                            uint32_t nc2 = ts_node_child_count(lexical_decl);
                            for (uint32_t k = 0; k < nc2; k++)
                            {
                                TSNode dec = ts_node_child(lexical_decl, k);
                                if (strcmp(ts_node_type(dec), "variable_declarator") != 0) continue;
                                TSNode name = ts_node_child_by_field_name(dec, "name", 4);
                                if (ts_node_is_null(name)) continue;
                                if (strcmp(ts_node_type(name), "identifier") == 0)
                                {
                                    size_t ns = ts_node_start_byte(name), ne = ts_node_end_byte(name);
                                    _bs_ns_add(&names_set, src + ns, ne - ns);
                                }
                            }
                        }

                        int has_captures = _bs_for_has_capture(body, src, &names_set, 0);
                        int is_block = (strcmp(ts_node_type(body), "statement_block") == 0);

                        if (has_captures)
                        {
                            /* Pre-scan body for sentinels, labels, arguments
                               references, and giveup conditions. We emit the
                               wrap via prefix/suffix inserts (no wholesale
                               replacement), and let sentinel substitutions plus
                               the arguments capture compose with the rest. */
                            _BS_ForCtx ctx;
                            memset(&ctx, 0, sizeof(ctx));
                            EditList sent_edits;
                            init_edits(&sent_edits);
                            ctx.edits = &sent_edits;
                            ctx.body  = body;

                            if (is_block)
                            {
                                uint32_t bcc = ts_node_child_count(body);
                                for (uint32_t i = 0; i < bcc && !ctx.giveup; i++)
                                    _bs_for_walk(ts_node_child(body, i), src, &ctx, 0, 0, 0);
                            }

                            if (ctx.giveup)
                            {
                                /* Can't safely wrap; fall back to no-wrap.
                                   let→var only. */
                                free_edits(&sent_edits);
                                _bs_lbl_free(&ctx.break_labels);
                                _bs_lbl_free(&ctx.continue_labels);
                                _bs_sv_free(&ctx.args_positions);
                            }
                            else
                            {
                                int has_args = (ctx.args_positions.len > 0);
                                int need_sentinels = (sent_edits.len > 0) ||
                                                     ctx.break_labels.len > 0 ||
                                                     ctx.continue_labels.len > 0 ||
                                                     ctx.has_return;
                                int is_outermost = !_bs_has_outer_wrap(parent);

                                rp_string *pref = rp_string_new(96);
                                rp_string *suff = rp_string_new(256);

                                if (need_sentinels)
                                {
                                    rp_string_puts(pref, "var _TrN_loop_ret = (function(");
                                }
                                else
                                {
                                    rp_string_puts(pref, "(function(");
                                }
                                rp_string_putsn(pref, params->str ? params->str : "", params->len);
                                rp_string_puts(pref, "){ ");

                                rp_string_puts(suff, " }).call(this");
                                if (args->len > 0)
                                {
                                    rp_string_puts(suff, ", ");
                                    rp_string_putsn(suff, args->str, args->len);
                                }
                                rp_string_puts(suff, ");");
                                if (need_sentinels)
                                {
                                    /* Bare break/continue are ours by definition
                                       (collected only at our loop_depth==0). */
                                    if (ctx.has_bare_break)
                                        rp_string_puts(suff, " if (_TrN_loop_ret === \"break\") break;");
                                    if (ctx.has_bare_continue)
                                        rp_string_puts(suff, " if (_TrN_loop_ret === \"continue\") continue;");
                                    /* Labeled break: per label, dispatch directly if reachable
                                       from our dispatch site; otherwise propagate up. */
                                    for (size_t li = 0; li < ctx.break_labels.len; li++)
                                    {
                                        const char *l = ctx.break_labels.labels[li];
                                        TSNode tgt = _bs_find_label_target(body, src, l, strlen(l));
                                        if (!ts_node_is_null(tgt) && _bs_label_dispatchable(parent, tgt))
                                            rp_string_appendf(suff,
                                                " if (_TrN_loop_ret === \"b:%s\") break %s;", l, l);
                                        else
                                            rp_string_appendf(suff,
                                                " if (_TrN_loop_ret === \"b:%s\") return _TrN_loop_ret;", l);
                                    }
                                    for (size_t li = 0; li < ctx.continue_labels.len; li++)
                                    {
                                        const char *l = ctx.continue_labels.labels[li];
                                        TSNode tgt = _bs_find_label_target(body, src, l, strlen(l));
                                        if (!ts_node_is_null(tgt) && _bs_label_dispatchable(parent, tgt))
                                            rp_string_appendf(suff,
                                                " if (_TrN_loop_ret === \"c:%s\") continue %s;", l, l);
                                        else
                                            rp_string_appendf(suff,
                                                " if (_TrN_loop_ret === \"c:%s\") return _TrN_loop_ret;", l);
                                    }
                                    /* Return-value sentinel: outermost wrap unwraps,
                                       intermediate wraps propagate as-is. */
                                    if (ctx.has_return)
                                    {
                                        if (is_outermost)
                                            rp_string_puts(suff,
                                                " if (typeof _TrN_loop_ret === \"object\" && _TrN_loop_ret !== null) return _TrN_loop_ret.v;");
                                        else
                                            rp_string_puts(suff,
                                                " if (typeof _TrN_loop_ret === \"object\" && _TrN_loop_ret !== null) return _TrN_loop_ret;");
                                    }
                                }

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
                                pref = rp_string_free(pref);
                                suff = rp_string_free(suff);

                                /* arguments capture: emit `var _TrN_loop_args =
                                   arguments;` BEFORE the for-statement, and
                                   rewrite each arguments-reference in the body
                                   to `_TrN_loop_args`. */
                                if (has_args)
                                {
                                    size_t fs = ts_node_start_byte(parent);
                                    add_edit(edits, fs, fs, "var _TrN_loop_args = arguments; ", claimed);
                                    for (size_t ai = 0; ai < ctx.args_positions.len; ai++)
                                    {
                                        size_t pos = ctx.args_positions.a[ai];
                                        add_edit(edits, pos, pos + 9 /* len("arguments") */,
                                                 "_TrN_loop_args", claimed);
                                    }
                                }

                                /* Hand sentinel edits to the main list. */
                                for (size_t si = 0; si < sent_edits.len; si++)
                                {
                                    Edit *e = &sent_edits.items[si];
                                    if (e->own_text)
                                    {
                                        add_edit_take_ownership(edits, e->start, e->end, e->text, claimed);
                                        e->text = NULL;
                                        e->own_text = 0;
                                    }
                                    else
                                    {
                                        add_edit(edits, e->start, e->end, e->text, claimed);
                                    }
                                }
                                free_edits(&sent_edits);
                                _bs_lbl_free(&ctx.break_labels);
                                _bs_lbl_free(&ctx.continue_labels);
                                _bs_sv_free(&ctx.args_positions);
                            }
                        }
                        /* else: !has_captures — skip wrap entirely. */

                        _bs_ns_free(&names_set);
                    }
                }

                params = rp_string_free(params);
                args = rp_string_free(args);
            }
        }
        else
        {
            /* Phase 0 (see transpiler-todo.md §8): drop the
               block-wrap-via-IIFE entirely. The IIFE shadowed `arguments`
               (and would also shadow `this`, return/break/continue) for
               any function body that used both a `let` and `arguments`.
               Trade-off: block-scoped `let x` inside a nested `if`/`{}`
               within a function now gets var-style scoping (same as
               duktape's native behavior for var and const). The proper
               fix is the babel-style rename pass planned in §8. */
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
        snprintf(tmpvar, sizeof(tmpvar), "_TrN_dof%u", ++_destr_counter);

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
        /* Emit: var _TrN_x = <right>, _TrN_it = ...; while (...) { var TMP = ...; body }
           Prefixes with `_TrN_` to avoid colliding with user-declared locals. */
        rp_string_puts(out, "var _TrN_x = ");
        rp_string_putsn(out, src + rs, re - rs);
        rp_string_puts(out, ", _TrN_it = (typeof Symbol!=='undefined'&&typeof _TrN_x[Symbol.iterator]==='function')?_TrN_x[Symbol.iterator]():null, _TrN_i = 0, _TrN_r; while(_TrN_it?!(_TrN_r=_TrN_it.next()).done:_TrN_i<_TrN_x.length) { var ");
        rp_string_puts(out, tmpvar);
        rp_string_puts(out, " = _TrN_it?_TrN_r.value:_TrN_x[_TrN_i++]; ");

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
    /* Optional rest pattern `[a, b, ...rest]`: when present, slice from
       rest_idx to end of the iterable, bound to rest_name.  Only one
       allowed per array pattern, must come last. */
    char *rest_name = NULL;
    int rest_idx = 0;
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
            /* `...name` captures the tail. Expect a single identifier
               child; bail on anything more complex. */
            TSNode rid = {{0}};
            uint32_t rnc = ts_node_named_child_count(ch);
            for (uint32_t ri = 0; ri < rnc; ri++) {
                TSNode kid = ts_node_named_child(ch, ri);
                if (strcmp(ts_node_type(kid), "identifier") == 0) { rid = kid; break; }
            }
            if (ts_node_is_null(rid)) {
                /* unsupported rest target (e.g. nested pattern) */
                for (size_t k = 0; k < alen; k++) free(arr[k].name);
                free(arr);
                return 0;
            }
            size_t ns = ts_node_start_byte(rid), ne = ts_node_end_byte(rid);
            rest_name = strndup(src + ns, ne - ns);
            rest_idx = idx;
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

    /* Generate unique helper names per call so nested for-of loops don't
       collide. Previously these were hardcoded `_pairs`/`_i`/`_loop`/`_pairs$_i`
       and inner `var _pairs = ...` declarations would hoist into the outer
       _loop's scope, shadowing the closure refs the outer body needed.
       See transpiler-todo.md §9 (now fixed). */
    unsigned ctr = ++_destr_counter;
    char nm_loop[32], nm_pairs[32], nm_i[32], nm_pi[32], nm_ret[32];
    snprintf(nm_loop,  sizeof(nm_loop),  "_loop%u",   ctr);
    snprintf(nm_pairs, sizeof(nm_pairs), "_pairs%u",  ctr);
    snprintf(nm_i,     sizeof(nm_i),     "_TrN_i%u",      ctr);
    snprintf(nm_pi,    sizeof(nm_pi),    "_pi%u",     ctr);
    snprintf(nm_ret,   sizeof(nm_ret),   "_ret%u",    ctr);

    /* Flow-control propagation: when body has return/break/continue,
       wrapping it in `_loopN.call(this)` swallows them.  Rewrite each
       to a sentinel value returned from the wrap and dispatch in the
       outer for-loop.  Uses the same _BS_ForCtx machinery that the
       for-let-block-scope path uses; see lines ~8035 / ~9402. */
    int has_flow = body_has_loop_flow_control(body);
    _BS_ForCtx fc;
    memset(&fc, 0, sizeof(fc));
    EditList sent_edits;
    init_edits(&sent_edits);
    int use_sentinels = 0;
    int is_outermost_wrap = 1;  /* destructured for-of bodies are
                                   self-contained; safe to unwrap. */
    if (has_flow && is_block) {
        fc.edits = &sent_edits;
        fc.body  = body;
        uint32_t bcc = ts_node_child_count(body);
        for (uint32_t i = 0; i < bcc && !fc.giveup; i++)
            _bs_for_walk(ts_node_child(body, i), src, &fc, 0, 0, 0);
        if (!fc.giveup) {
            use_sentinels = (sent_edits.len > 0) || fc.has_return
                          || fc.break_labels.len > 0 || fc.continue_labels.len > 0;
        }
    }

    rp_string *out = rp_string_new(256);

    // _loop declaration
    rp_string_puts(out, "var ");
    rp_string_puts(out, nm_loop);
    rp_string_puts(out, " = function ");
    rp_string_puts(out, nm_loop);
    rp_string_puts(out, "() { var ");
    rp_string_puts(out, nm_pi);
    rp_string_puts(out, " = _TrN_Sp.slicedToArray(");
    rp_string_puts(out, nm_pairs);
    rp_string_puts(out, "[");
    rp_string_puts(out, nm_i);
    rp_string_puts(out, "]");
    /* If a rest pattern is present we want the full array; passing 0
       (falsy) makes iterableToArrayLimit collect everything. */
    if (!rest_name) rp_string_appendf(out, ", %d", N);
    rp_string_puts(out, "), ");

    // bindings: a = <nm_pi>[0], b = <nm_pi>[1];
    for (size_t k = 0; k < alen; k++)
    {
        if (k)
            rp_string_puts(out, ",");
        rp_string_puts(out, arr[k].name);
        rp_string_puts(out, " = ");
        rp_string_puts(out, nm_pi);
        rp_string_puts(out, "[");
        char ibuf[32];
        snprintf(ibuf, sizeof(ibuf), "%d", arr[k].index);
        rp_string_puts(out, ibuf);
        rp_string_puts(out, "]");
    }
    if (rest_name) {
        if (alen) rp_string_puts(out, ",");
        rp_string_appendf(out, "%s = %s.slice(%d)", rest_name, nm_pi, rest_idx);
    }
    rp_string_puts(out, "; ");

    // body content — apply sentinel edits if we're propagating flow
    if (is_block)
    {
        if (use_sentinels) {
            char *modbody = _apply_edits_to_slice(src, bs + 1, be - 1, &sent_edits);
            rp_string_puts(out, modbody);
            free(modbody);
        } else {
            rp_string_putsn(out, src + bs + 1, (be - 1) - (bs + 1));
        }
        rp_string_puts(out, " }; ");
    }
    else
    {
        rp_string_putsn(out, src + bs, be - bs);
        rp_string_puts(out, "; }; ");
    }

    // for loop header using array length
    rp_string_puts(out, "for (var ");
    rp_string_puts(out, nm_i);
    rp_string_puts(out, " = 0, ");
    rp_string_puts(out, nm_pairs);
    rp_string_puts(out, " = ");
    rp_string_putsn(out, src + rs, re - rs);
    rp_string_puts(out, "; ");
    rp_string_puts(out, nm_i);
    rp_string_puts(out, " < ");
    rp_string_puts(out, nm_pairs);
    rp_string_puts(out, ".length; ");
    rp_string_puts(out, nm_i);
    rp_string_puts(out, "++) { ");
    if (use_sentinels) {
        /* Capture the wrap's return value and dispatch.  `.call(this)`
           still propagates `this` for class-method bodies. */
        rp_string_puts(out, "var ");
        rp_string_puts(out, nm_ret);
        rp_string_puts(out, " = ");
        rp_string_puts(out, nm_loop);
        rp_string_puts(out, ".call(this);");
        if (fc.has_bare_break) {
            rp_string_appendf(out, " if (%s === \"break\") break;", nm_ret);
        }
        if (fc.has_bare_continue) {
            rp_string_appendf(out, " if (%s === \"continue\") continue;", nm_ret);
        }
        /* Labeled break/continue: dispatch directly when target is
           reachable from here, else propagate up. */
        for (size_t li = 0; li < fc.break_labels.len; li++) {
            const char *l = fc.break_labels.labels[li];
            rp_string_appendf(out, " if (%s === \"b:%s\") return %s;",
                              nm_ret, l, nm_ret);
        }
        for (size_t li = 0; li < fc.continue_labels.len; li++) {
            const char *l = fc.continue_labels.labels[li];
            rp_string_appendf(out, " if (%s === \"c:%s\") return %s;",
                              nm_ret, l, nm_ret);
        }
        if (fc.has_return) {
            if (is_outermost_wrap)
                rp_string_appendf(out,
                    " if (typeof %s === \"object\" && %s !== null) return %s.v;",
                    nm_ret, nm_ret, nm_ret);
            else
                rp_string_appendf(out,
                    " if (typeof %s === \"object\" && %s !== null) return %s;",
                    nm_ret, nm_ret, nm_ret);
        }
        rp_string_puts(out, " }");
    } else {
        rp_string_puts(out, nm_loop);
        /* `.call(this)` so `this.opts` and similar references inside the
           destructured body resolve to the enclosing function's `this`
           (e.g. inside a class method).  Without this binding the inner
           _loopN call would see `this === undefined` (strict) or globalThis
           (sloppy), breaking ajv-style code like `this.opts.es5`. */
        rp_string_puts(out, ".call(this); }");
    }

    free_edits(&sent_edits);
    _bs_lbl_free(&fc.break_labels);
    _bs_lbl_free(&fc.continue_labels);
    _bs_sv_free(&fc.args_positions);

    // Replace entire for-of statement
    add_edit_take_ownership(edits, fs, fe, rp_string_steal(out), claimed);

    out = rp_string_free(out);

    // cleanup
    for (size_t k = 0; k < alen; k++)
        free(arr[k].name);
    free(arr);
    if (rest_name) free(rest_name);

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
                    rp_string_puts(bucket, "_TrN_Super.");
                else
                    rp_string_puts(bucket, "_TrN_Super.prototype.");
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
                /* Property access: super.name → _TrN_Sp._superGet(<target>, "name", this)
                   so getters resolve correctly (calling them with `this` bound to
                   the actual instance rather than the prototype object). For
                   ordinary data properties the helper walks the prototype chain
                   and returns desc.value. */
                rp_string_puts(bucket, "_TrN_Sp._superGet(");
                if (is_static)
                    rp_string_puts(bucket, "_TrN_Super");
                else
                    rp_string_puts(bucket, "_TrN_Super.prototype");
                rp_string_puts(bucket, ",\"");
                rp_string_putsn(bucket, body + id_start, id_len);
                rp_string_puts(bucket, "\",this)");
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
       var _TrN_this;
       _TrN_Sp.classCallCheck(this, Name);
       _TrN_this = _super.call(this, *super args as written*);
       * remainder of body, with this. field inits left as-is *
       return _TrN_this;
     }
     _TrN_Sp.createClass(Name, [ ...proto... ], [ ...static... ]);
     return Name;
   }(Super);
*/

/* Returns 1 if `name` (length `len`) is a JavaScript reserved word that
   cannot legally appear as a function-expression name. Used to decide
   whether a class method's name can be inlined into `function NAME(){}`
   or must be emitted anonymously (the `key:` field still carries the
   user-facing name). */
static __attribute__((unused)) int
_name_is_reserved(const char *name, size_t len)
{
    static const char *RES[] = {
        "break","case","catch","class","const","continue","debugger","default",
        "delete","do","else","enum","export","extends","false","finally","for",
        "function","if","import","in","instanceof","new","null","return","super",
        "switch","this","throw","true","try","typeof","var","void","while","with",
        "yield","let","static","implements","interface","package","private",
        "protected","public","await", NULL
    };
    for (size_t i = 0; RES[i]; i++)
    {
        size_t rl = strlen(RES[i]);
        if (rl == len && memcmp(name, RES[i], len) == 0)
            return 1;
    }
    return 0;
}

/* Private-name lowering for ES2022 class private fields/methods.

   Tree-sitter parses `#name` as `private_property_identifier`. Duktape
   has no concept of `#` in identifiers, so we strip the `#` and
   prepend `_priv_` to produce a regular identifier. The substitution
   leaks no privacy enforcement (duktape doesn't check accessor scope
   anyway), but with `_priv_` as the prefix accidental collisions with
   user code are vanishingly rare.

   Emit `src[ss..se]` into `out` but replace every
   `private_property_identifier` node within the subtree rooted at
   `root` with `_priv_<name>` (where <name> is the original identifier
   text without the `#`). The substitution preserves byte ranges of
   non-private code so any other rewrites inside the span continue to
   work via the edit list. */
static void _collect_priv_ids(TSNode node, _BS_NodeVec *out, size_t lo, size_t hi)
{
    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);
    if (ne <= lo || ns >= hi) return;
    if (strcmp(ts_node_type(node), "private_property_identifier") == 0)
    {
        _bs_nv_push(out, node);
        return;
    }
    uint32_t c = ts_node_child_count(node);
    for (uint32_t i = 0; i < c; i++)
        _collect_priv_ids(ts_node_child(node, i), out, lo, hi);
}

/* Class-scoped counter for private-field name mangling.  Each call to
   `es5_emit_class_core` reserves a fresh `_priv_class_id` so two
   classes that both use `#x` end up with distinct mangled names
   (`_TrN_priv0_x` vs `_TrN_priv1_x`) — no cross-class leakage.  Static
   because the counter just needs to be unique per-process; resetting
   it across transpile() calls would risk a cached `.transpiled.js`
   referring to a class_id that a fresh transpile reallocates to a
   different class. */
static unsigned _priv_class_counter = 0;

static void _emit_with_priv_subst(rp_string *out, const char *src, size_t ss, size_t se, TSNode root,
                                  unsigned class_id)
{
    _BS_NodeVec privs;
    _bs_nv_init(&privs);
    _collect_priv_ids(root, &privs, ss, se);

    if (privs.len == 0)
    {
        rp_string_putsn(out, src + ss, se - ss);
        _bs_nv_free(&privs);
        return;
    }

    /* Sort by start ascending. */
    for (size_t i = 0; i + 1 < privs.len; i++)
        for (size_t j = i + 1; j < privs.len; j++)
            if (ts_node_start_byte(privs.a[j]) < ts_node_start_byte(privs.a[i]))
            {
                TSNode t = privs.a[i];
                privs.a[i] = privs.a[j];
                privs.a[j] = t;
            }

    size_t cursor = ss;
    for (size_t i = 0; i < privs.len; i++)
    {
        size_t ps = ts_node_start_byte(privs.a[i]);
        size_t pe = ts_node_end_byte(privs.a[i]);
        if (ps < cursor) continue;  /* skip nested duplicates */
        if (ps > cursor)
            rp_string_putsn(out, src + cursor, ps - cursor);
        /* Skip leading `#`, prepend `_TrN_priv<class_id>_`.  The
           class-scoped id prevents accidental cross-class collision
           when two unrelated classes both use the same private name. */
        rp_string_appendf(out, "_TrN_priv%u_", class_id);
        rp_string_putsn(out, src + ps + 1, pe - ps - 1);
        cursor = pe;
    }
    if (cursor < se)
        rp_string_putsn(out, src + cursor, se - cursor);
    _bs_nv_free(&privs);
}

/* Emit the params node's text into `out`, stripping comment children.
   Used by the class method emitter (which copies params verbatim).
   If the source has comments BETWEEN params (common in wide method
   signatures — see ajv core.js `errorsText(...)`), the verbatim copy
   plus subsequent single-line flattening lets a line-comment swallow
   the closing paren and break parsing.

   Strategy: walk the params children, skip `comment` nodes, emit
   intervening bytes from src so the comma/whitespace separators
   between params survive. */
static void _emit_params_no_comments(rp_string *out, const char *src, TSNode params)
{
    size_t ps = ts_node_start_byte(params), pe = ts_node_end_byte(params);
    uint32_t nc = ts_node_child_count(params);
    size_t cur = ps;
    for (uint32_t i = 0; i < nc; i++)
    {
        TSNode kid = ts_node_child(params, i);
        const char *kt = ts_node_type(kid);
        if (strcmp(kt, "comment") == 0)
        {
            /* Emit bytes from `cur` to start of comment, then jump past it. */
            size_t cs = ts_node_start_byte(kid), ce = ts_node_end_byte(kid);
            if (cs > cur)
                rp_string_putsn(out, src + cur, cs - cur);
            cur = ce;
        }
    }
    if (cur < pe)
        rp_string_putsn(out, src + cur, pe - cur);
}

/* Count `\n`s in src[0..off). Used to keep emitted class members on
   the same line as the source so error stacks point to the right place. */
static int _src_line_at(const char *src, size_t off)
{
    int line = 1;
    for (size_t i = 0; i < off; i++)
        if (src[i] == '\n') line++;
    return line;
}

static void _emit_n_newlines(rp_string *out, int n)
{
    while (n-- > 0) rp_string_putc(out, '\n');
}

/* Count `\n`s in the bucket's tail beyond `since_len`. Used to keep
   the bucket's notional "current source line" in sync with the
   newlines we've actually emitted into it. */
static int _count_newlines_since(rp_string *bucket, size_t since_len)
{
    int n = 0;
    for (size_t i = since_len; i < bucket->len; i++)
        if (bucket->str[i] == '\n') n++;
    return n;
}

static void es5_emit_class_core(rp_string *out, const char *src, const char *cname, size_t cname_len, int has_super,
                                size_t sups, size_t supe, TSNode body,
                                uint32_t *polysneeded)
{
    /* Reserve a unique class id for private-field name mangling so
       two classes that use the same `#name` don't collide.  See
       `_emit_with_priv_subst` for the mangling scheme. */
    unsigned class_priv_id = _priv_class_counter++;

    /* Accumulator for TC39 Stage 1 decorator-apply calls emitted
       AFTER the class IIFE.  Each method/field decorator-application
       appends a `_TrN_Sp._applyDecoratedDescriptor(...)` call here;
       drained at the bottom of this function. */
    rp_string *dec_calls = rp_string_new(0);

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

    /* Source line of the class body's opening `{`. Each bucket
       (proto_arr, static_arr, field_inits, static_field_inits) tracks
       the "virtual current source line" we've emitted up to in the
       bucket. We pad with `\n` characters before each new member so
       that the member's content lands on its source line. We can't
       use the source END line of the previous member as a proxy: some
       methods (e.g. async ones) compress to fewer newlines than the
       source body had, so we must count the actual newlines emitted. */
    int body_open_line = _src_line_at(src, ts_node_start_byte(body));
    int proto_cur_line = body_open_line;
    int static_cur_line = body_open_line;
    int field_cur_line = body_open_line;
    int sfield_cur_line = body_open_line;
    /* First-method source line for each bucket. The buckets are filled
       BEFORE the OUT prelude/ctor are assembled, so we don't yet know
       what line OUT will be at when we append each bucket. At assembly
       time we'll compute (first_method_src_line - actual_out_line) and
       prepend that many newlines so the bucket's first member lands at
       its source line. */
    int proto_first_line = -1;
    int static_first_line = -1;
    int sfield_first_line = -1;
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
        int is_private_field = (strcmp(ts_node_type(fprop), "private_property_identifier") == 0);

        /* Align this field's emit with its source line, then advance
           the cursor by however many newlines the emission actually
           writes (the value may span multiple lines in source).
           For the FIRST static field, defer the leading pad to OUT
           assembly time (we don't yet know OUT's current line). */
        int *fpcur = is_static_field ? &sfield_cur_line : &field_cur_line;
        int fline = _src_line_at(src, ts_node_start_byte(ch));
        int is_first_sfield = (is_static_field && sfield_first_line < 0);
        if (is_first_sfield)
        {
            sfield_first_line = fline;
        }
        else
        {
            int fdelta = fline - *fpcur;
            if (fdelta > 0)
                _emit_n_newlines(dest, fdelta);
        }
        *fpcur = fline;
        size_t pre_field_len = dest->len;

        if (is_static_field)
        {
            rp_string_putsn(dest, cname, cname_len);
            rp_string_puts(dest, ".");
        }
        else
        {
            rp_string_puts(dest, "this.");
        }
        if (is_private_field)
        {
            /* Strip leading `#`, prepend the class-scoped `_priv<id>_`. */
            rp_string_appendf(dest, "_TrN_priv%u_", class_priv_id);
            rp_string_putsn(dest, src + fps + 1, fpe - fps - 1);
        }
        else
        {
            rp_string_putsn(dest, src + fps, fpe - fps);
        }
        if (!ts_node_is_null(fval))
        {
            size_t fvs = ts_node_start_byte(fval), fve = ts_node_end_byte(fval);
            rp_string_puts(dest, " = ");
            /* The value may reference `this.#x` — substitute. */
            _emit_with_priv_subst(dest, src, fvs, fve, fval, class_priv_id);
        }
        else
        {
            rp_string_puts(dest, " = undefined");
        }
        rp_string_puts(dest, ";");
        *fpcur += _count_newlines_since(dest, pre_field_len);

        /* TC39 Stage 1 decorator support on fields.  Same shape as
           methods but `desc` is undefined (field has no prototype
           descriptor — the helper synthesizes one). */
        {
            int n_dec = 0;
            for (uint32_t j = 0, cn = ts_node_child_count(ch); j < cn; j++) {
                TSNode dch = ts_node_child(ch, j);
                if (strcmp(ts_node_type(dch), "decorator") == 0) n_dec++;
            }
            if (n_dec > 0 && !is_private_field) {
                rp_string_puts(dec_calls, "_TrN_Sp._applyDecoratedDescriptor(");
                rp_string_putsn(dec_calls, cname, cname_len);
                if (!is_static_field) rp_string_puts(dec_calls, ".prototype");
                rp_string_puts(dec_calls, ",\"");
                rp_string_putsn(dec_calls, src + fps, fpe - fps);
                rp_string_puts(dec_calls, "\",[");
                int emitted = 0;
                for (uint32_t j = 0, cn = ts_node_child_count(ch); j < cn; j++) {
                    TSNode dch = ts_node_child(ch, j);
                    if (strcmp(ts_node_type(dch), "decorator") != 0) continue;
                    if (emitted++) rp_string_puts(dec_calls, ",");
                    size_t ds = ts_node_start_byte(dch) + 1;
                    size_t de = ts_node_end_byte(dch);
                    rp_string_putsn(dec_calls, src + ds, de - ds);
                }
                rp_string_puts(dec_calls, "],void 0,");
                rp_string_putsn(dec_calls, cname, cname_len);
                if (!is_static_field) rp_string_puts(dec_calls, ".prototype");
                rp_string_puts(dec_calls, ");");
                if (polysneeded) *polysneeded |= DECORATORS_PF | CLASS_PF;
            }
        }
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
        int is_async = 0;
        // detect "static", "get", "set", "async" modifiers
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
            else if (slen == 5 && strncmp(src + ss, "async", 5) == 0)
                is_async = 1;
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

        int is_private_method = 0;
        if (!ts_node_is_null(nname) && strcmp(ts_node_type(nname), "property_identifier") == 0)
        {
            ks = ts_node_start_byte(nname);
            ke = ts_node_end_byte(nname);
        }
        else if (!ts_node_is_null(nname) && strcmp(ts_node_type(nname), "private_property_identifier") == 0)
        {
            /* `#name` — skip the `#`, the emit path below will prepend
               `_priv_` for both the `key:` and `function NAME` slots. */
            is_private_method = 1;
            ks = ts_node_start_byte(nname) + 1;
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
        int is_first = (bucket->len == 0);
        if (!is_first)
            rp_string_puts(bucket, ",");

        /* For the first method in a bucket, just record its source
           line; no leading newlines yet — the OUT-assembly step will
           prepend the right number once it knows OUT's position. For
           subsequent methods, pad with the inter-method line delta. */
        int *pcur = is_static ? &static_cur_line : &proto_cur_line;
        int *pfirst = is_static ? &static_first_line : &proto_first_line;
        int mline = _src_line_at(src, ts_node_start_byte(mth));
        if (is_first)
        {
            *pfirst = mline;
        }
        else
        {
            int delta = mline - *pcur;
            if (delta > 0)
                _emit_n_newlines(bucket, delta);
        }
        *pcur = mline;
        size_t pre_emit_len = bucket->len;

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
            // {key:'name',value:function(){...}}  or get/set variant.
            // The inner function is ANONYMOUS: a named function expression
            // would bind its own name inside the body, shadowing a
            // free identifier of the same name from the enclosing
            // scope.  Real-world hit: luxon's `class DateTime` has a
            // `toISODate({format = "extended"} = {})` method whose body
            // calls the *outer* `function toISODate(o, extended)` helper
            // via `toISODate(this, ...)`.  A named expression would make
            // that recurse instead.  ES6 method definitions never create
            // such a binding; we match that semantic.  fn.name still
            // surfaces via Object descriptor NamedEvaluation where
            // supported. The `key:` field always carries the user-facing
            // name regardless.
            rp_string_puts(bucket, "{key:'");
            if (is_private_method)
                rp_string_appendf(bucket, "_TrN_priv%u_", class_priv_id);
            rp_string_putsn(bucket, src + ks, ke - ks);
            rp_string_appendf(bucket, "',%s:function ", desc_field);
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

        /* Emit params (minus rest if present, minus inline comments).
           Comments BETWEEN params would otherwise survive verbatim into
           a single-line function expression and let `//` swallow the
           closing `)`. */
        if (ps && pe)
        {
            if (rest_name)
            {
                /* Copy params but skip the rest portion. The rest-strip
                   is done by hand on byte ranges; comment-strip is not
                   applied here because the rest-method-params case is
                   simpler in practice — fix if it ever bites. */
                rp_string_putsn(bucket, src + ps, rest_remove_s - ps);
                rp_string_putsn(bucket, src + rest_remove_e, pe - rest_remove_e);
            }
            else
            {
                _emit_params_no_comments(bucket, src, params);
            }
        }
        else
            rp_string_puts(bucket, "()");

        /* Emit body (with rest shim and super rewrite as needed) */
        rp_string_puts(bucket, " ");
        if (is_async && bs && be)
        {
            /* Async class method:
                 value: function name(params) {
                    return _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(
                        function _callee(params) { <regenerator-switch> }
                    )).apply(this, arguments);
                 }
               The outer wrapper's params are unused (apply forwards
               `arguments`); the inner _callee receives the real params. */
            rp_string_puts(bucket, "{return _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function _TrN_callee");
            if (ps && pe)
                rp_string_putsn(bucket, src + ps, pe - ps);
            else
                rp_string_puts(bucket, "()");
            rp_string_puts(bucket, " {");
            char *_wrap = _build_regenerator_switch_body(src, mb);
            if (_wrap)
            {
                rp_string_puts(bucket, _wrap);
                free(_wrap);
            }
            rp_string_puts(bucket, "})).apply(this, arguments);}");
        }
        else if (bs && be)
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
                /* copy body contents (skip outer braces), with super rewrite if needed.
                   Private-name substitution happens unconditionally — for non-private
                   classes the helper short-circuits to a raw copy. The super path
                   needs the substituted bytes too, so build a temp buffer first. */
                if (has_super)
                {
                    rp_string *subbed = rp_string_new(64);
                    _emit_with_priv_subst(subbed, src, bs + 1, be - 1, mb, class_priv_id);
                    copy_body_replace_super(bucket, subbed->str, subbed->len, is_static);
                    subbed = rp_string_free(subbed);
                }
                else
                    _emit_with_priv_subst(bucket, src, bs + 1, be - 1, mb, class_priv_id);
                rp_string_putc(bucket, '}');
            }
            else
            {
                _emit_with_priv_subst(bucket, src, bs, be, mb, class_priv_id);
            }
        }
        else
            rp_string_puts(bucket, "{}");
        /* Attach the original method source so that Function.prototype.
           toString returns it (fn-source feature for class methods).
           defineProperties applies this via _TrN_Sp._fs at install time. */
        {
            size_t mts = ts_node_start_byte(mth), mte = ts_node_end_byte(mth);
            rp_string *lit = rp_string_new(mte - mts + 8);
            emit_js_string_literal(lit, src, mts, mte);
            rp_string_puts(bucket, ",src:");
            rp_string_putsn(bucket, lit->str, lit->len);
            lit = rp_string_free(lit);
        }
        rp_string_puts(bucket, "}");

        /* Advance the bucket's notional source-line by however many
           newlines this method's emission actually wrote. */
        *pcur += _count_newlines_since(bucket, pre_emit_len);

        /* TC39 Stage 1 decorator support.  Walk method_definition
           children for `decorator` nodes (siblings of name/params).
           For each decorated method, emit a post-class call:
             _TrN_Sp._applyDecoratedDescriptor(<target>, "name", [dec...],
                 Object.getOwnPropertyDescriptor(<target>, "name"),
                 <target>);
           where <target> is `<Class>.prototype` (instance method) or
           `<Class>` (static method).  Decorators are emitted in source
           order in the array; the helper applies them in reverse. */
        {
            int n_dec = 0;
            for (uint32_t j = 0, cn = ts_node_child_count(mth); j < cn; j++) {
                TSNode dch = ts_node_child(mth, j);
                if (strcmp(ts_node_type(dch), "decorator") == 0) n_dec++;
            }
            if (n_dec > 0 && !is_computed && !is_private_method) {
                rp_string_puts(dec_calls, "_TrN_Sp._applyDecoratedDescriptor(");
                rp_string_putsn(dec_calls, cname, cname_len);
                if (!is_static) rp_string_puts(dec_calls, ".prototype");
                rp_string_puts(dec_calls, ",\"");
                rp_string_putsn(dec_calls, src + ks, ke - ks);
                rp_string_puts(dec_calls, "\",[");
                int emitted = 0;
                for (uint32_t j = 0, cn = ts_node_child_count(mth); j < cn; j++) {
                    TSNode dch = ts_node_child(mth, j);
                    if (strcmp(ts_node_type(dch), "decorator") != 0) continue;
                    if (emitted++) rp_string_puts(dec_calls, ",");
                    /* skip the leading `@` (1 byte) — the child after
                       is identifier / call_expression / member_expression. */
                    size_t ds = ts_node_start_byte(dch) + 1;
                    size_t de = ts_node_end_byte(dch);
                    rp_string_putsn(dec_calls, src + ds, de - ds);
                }
                rp_string_puts(dec_calls, "],Object.getOwnPropertyDescriptor(");
                rp_string_putsn(dec_calls, cname, cname_len);
                if (!is_static) rp_string_puts(dec_calls, ".prototype");
                rp_string_puts(dec_calls, ",\"");
                rp_string_putsn(dec_calls, src + ks, ke - ks);
                rp_string_puts(dec_calls, "\"),");
                rp_string_putsn(dec_calls, cname, cname_len);
                if (!is_static) rp_string_puts(dec_calls, ".prototype");
                rp_string_puts(dec_calls, ");");
                if (polysneeded) *polysneeded |= DECORATORS_PF | CLASS_PF;
            }
        }
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
        rp_string_puts(out, " = (function(_TrN_Super) {_TrN_Sp.inherits(");
        rp_string_putsn(out, cname, cname_len);
        rp_string_puts(out, ", _TrN_Super);var _TrN_super = _TrN_Sp.createSuper(");
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

                /* Emit prelude (var _TrN_this; classCallCheck) */
                rp_string_puts(out, "var _TrN_this;_TrN_Sp.classCallCheck(this, ");
                rp_string_putsn(out, cname, cname_len);
                rp_string_puts(out, ");");

                /* Inject class field initializations BEFORE the body.
                   Most class layouts don't read `this` until after super
                   anyway; pre-super reads would be runtime errors in
                   spec-correct JS regardless. */
                if (field_inits->len)
                    rp_string_puts(out, field_inits->str);

                /* Copy body bytes [0..super_call_start) verbatim — these
                   are the structural context surrounding `super(...)`,
                   e.g. `if(`, `try{`, control-flow leading up to the
                   call.  Pre-fix, the rewriter dropped this prefix and
                   assumed super was the first statement, which broke
                   patterns like `constructor(){ if(super(x),y) throw }`. */
                size_t super_kw_pos = (size_t)(open - b);
                if (super_kw_pos > 0)
                {
                    rp_string *pre = rp_string_new(64);
                    _emit_with_priv_subst(pre, src,
                                          ts_node_start_byte(ctor_body) + 1,
                                          ts_node_start_byte(ctor_body) + 1 + super_kw_pos,
                                          ctor_body, class_priv_id);
                    copy_body_replace_super(out, pre->str, pre->len, 0);
                    pre = rp_string_free(pre);
                }

                /* Replace `super(args)` with an EXPRESSION that assigns
                   to _TrN_this and evaluates to that value:
                     (_TrN_this = _TrN_super.call(this, args), _TrN_this)
                   This form fits inside any expression position (an
                   `if`-condition, a comma sequence, a logical op, etc.)
                   without disturbing the surrounding parens. */
                rp_string_puts(out, "(_TrN_this = ");
                {
                    const char *atext = b + args_s;
                    size_t alen = call_rp - args_s;
                    while (alen > 0 && (*atext == ' ' || *atext == '\t' || *atext == '\n' || *atext == '\r'))
                    { atext++; alen--; }
                    while (alen > 0 && (atext[alen-1] == ' ' || atext[alen-1] == '\t' || atext[alen-1] == '\n' || atext[alen-1] == '\r'))
                        alen--;
                    if (alen > 3 && atext[0] == '.' && atext[1] == '.' && atext[2] == '.'
                        && memchr(atext + 3, ',', alen - 3) == NULL)
                    {
                        /* single spread: super(...expr) -> _super.apply(this, expr) */
                        rp_string_puts(out, "_TrN_super.apply(this,");
                        rp_string_putsn(out, atext + 3, alen - 3);
                        rp_string_puts(out, ")");
                    }
                    else if (alen > 0)
                    {
                        rp_string_puts(out, "_TrN_super.call(this, ");
                        rp_string_putsn(out, atext, alen);
                        rp_string_puts(out, ")");
                    }
                    else
                    {
                        rp_string_puts(out, "_TrN_super.call(this)");
                    }
                }
                rp_string_puts(out, ", _TrN_this)");

                /* Copy bytes AFTER `super(...)` — i.e. from byte after
                   the closing ')' to the end of body.  Preserves any
                   wrapping context (`if(super(),x) body`'s closing `)`,
                   `try{super();...}catch`, etc.). */
                size_t after = call_rp + 1; /* position after ')' */
                if (after < blen)
                {
                    rp_string *subbed = rp_string_new(64);
                    _emit_with_priv_subst(subbed, src,
                                          ts_node_start_byte(ctor_body) + 1 + after,
                                          ts_node_start_byte(ctor_body) + 1 + blen,
                                          ctor_body, class_priv_id);
                    copy_body_replace_super(out, subbed->str, subbed->len, 0);
                    subbed = rp_string_free(subbed);
                }

                /* Ensure the constructor returns _TrN_this.  Lead with `;`
                   so we don't concatenate `=3return` when the body's last
                   expression has no trailing semicolon. */
                rp_string_puts(out, ";return _TrN_this;");
            }
            else
            {
            NO_SUPER_REWRITE:
                rp_string_puts(out, "_TrN_Sp.classCallCheck(this, ");
                rp_string_putsn(out, cname, cname_len);
                rp_string_puts(out, ");");
                if (field_inits->len)
                    rp_string_puts(out, field_inits->str);
                if (blen)
                {
                    rp_string *subbed = rp_string_new(64);
                    _emit_with_priv_subst(subbed, src,
                                          ts_node_start_byte(ctor_body) + 1,
                                          ts_node_start_byte(ctor_body) + 1 + blen,
                                          ctor_body, class_priv_id);
                    copy_body_replace_super(out, subbed->str, subbed->len, 0);
                    subbed = rp_string_free(subbed);
                }
            }
        }
        else
        {
            /* No explicit constructor in `class … extends Base { … }`. Emit
               default ctor that forwards to super, then runs field inits. */
            rp_string_puts(out, "var _TrN_this;_TrN_Sp.classCallCheck(this, ");
            rp_string_putsn(out, cname, cname_len);
            rp_string_puts(out, ");_TrN_this = _TrN_super.apply(this, arguments);");
            if (field_inits->len)
                rp_string_puts(out, field_inits->str);
            rp_string_puts(out, "return _TrN_this;");
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
            _emit_with_priv_subst(out, src, bs, be, ctor_body, class_priv_id);
        }
    }
    rp_string_puts(out, "};");

    /* Static field initializations are emitted INSIDE the IIFE, at
       their source line positions, so per-file line numbers stay
       aligned.  Pad to first static-field source line, then dump the
       bucket which already has inter-field padding tracked. */
    if (static_field_inits->len)
    {
        if (sfield_first_line >= 0)
        {
            int out_lines_now = body_open_line + _count_newlines_since(out, 0);
            int pad = sfield_first_line - out_lines_now;
            if (pad > 0) _emit_n_newlines(out, pad);
        }
        rp_string_puts(out, static_field_inits->str);
    }

    /* ——— _TrN_Sp.createClass(Name, [...], [...] [, 1]) ———
       Emit static bucket first when it sits at an earlier source line
       than the proto bucket, so per-method padding can align both
       buckets to their source positions.  In that case we pass an
       extra `1` flag and the helper swaps interpretation.  */
    int swap_order = (static_first_line >= 0
                      && proto_first_line >= 0
                      && static_first_line < proto_first_line);
    rp_string *first_arr  = swap_order ? static_arr : proto_arr;
    int        first_line = swap_order ? static_first_line : proto_first_line;
    rp_string *second_arr = swap_order ? proto_arr : static_arr;
    int        second_line= swap_order ? proto_first_line : static_first_line;

    rp_string_puts(out, "_TrN_Sp.createClass(");
    rp_string_putsn(out, cname, cname_len);
    rp_string_puts(out, ",[");
    if (first_line >= 0)
    {
        int out_lines_now = body_open_line + _count_newlines_since(out, 0);
        int pad = first_line - out_lines_now;
        if (pad > 0) _emit_n_newlines(out, pad);
    }
    rp_string_puts(out, first_arr->str);
    rp_string_puts(out, "],[");
    if (second_line >= 0)
    {
        int out_lines_now = body_open_line + _count_newlines_since(out, 0);
        int pad = second_line - out_lines_now;
        if (pad > 0) _emit_n_newlines(out, pad);
    }
    rp_string_puts(out, second_arr->str);
    if (swap_order)
        rp_string_puts(out, "],1);");
    else
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

    /* Append any decorator-apply calls collected from method/field
       loops above.  These need to fire AFTER the class IIFE has run
       so the descriptors are present on the prototype / static side. */
    if (dec_calls->len)
        rp_string_puts(out, dec_calls->str);
    dec_calls = rp_string_free(dec_calls);

    proto_arr = rp_string_free(proto_arr);
    static_arr = rp_string_free(static_arr);
    field_inits = rp_string_free(field_inits);
    static_field_inits = rp_string_free(static_field_inits);
}

static int rewrite_class_to_es5(EditList *edits, const char *src, TSNode class_node, RangeList *claimed,
                                uint32_t *polysneeded, int overlaps)
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
    es5_emit_class_core(out, src, nameptr, namelen, has_super, sups, supe, body, polysneeded);

    /* TC39 Stage 1 class-level decorators.  `@dec class Foo {…}` —
       decorators are children of class_declaration BEFORE the `class`
       keyword.  Apply in reverse declaration order after the IIFE:
         Foo = dec2(Foo) || Foo;
         Foo = dec1(Foo) || Foo;
       (Conventional spec order: outermost decorator runs last.) */
    {
        TSNode dec_nodes[16];
        int n_dec = 0;
        for (uint32_t i = 0, cn = ts_node_child_count(class_node); i < cn && n_dec < 16; i++) {
            TSNode dch = ts_node_child(class_node, i);
            if (strcmp(ts_node_type(dch), "decorator") == 0)
                dec_nodes[n_dec++] = dch;
        }
        if (n_dec > 0) {
            for (int k = n_dec - 1; k >= 0; k--) {
                rp_string_putsn(out, nameptr, namelen);
                rp_string_puts(out, "=");
                size_t ds = ts_node_start_byte(dec_nodes[k]) + 1;  /* skip `@` */
                size_t de = ts_node_end_byte(dec_nodes[k]);
                rp_string_putsn(out, src + ds, de - ds);
                /* If the decorator is a member expression / identifier
                   (not a call), wrap as a call: `dec(Foo) || Foo`. */
                rp_string_puts(out, "(");
                rp_string_putsn(out, nameptr, namelen);
                rp_string_puts(out, ")||");
                rp_string_putsn(out, nameptr, namelen);
                rp_string_puts(out, ";");
            }
            if (polysneeded) *polysneeded |= CLASS_PF;
        }
    }

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
        snprintf(tmpname, sizeof(tmpname), "_TrN_C%u", (unsigned)cs);
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
    es5_emit_class_core(expr, src, nameptr, namelen, has_super, sups, supe, body, polysneeded);

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

/* Duktape's regex tokenizer ends the regex at the first unescaped `/`
   even when that `/` lives inside a character class `[...]`. Spec-wise
   `/` inside `[]` is a literal char and doesn't need escaping, but
   duktape gets it wrong when followed by certain characters (notably
   `` ` ``, which makes duktape's parser fall through and try to start a
   template literal). The fix is harmless and portable: scan the regex
   pattern, escape any `/` inside `[]` to `\/`.

   Confirmed by marked 12.0.2 inline grammar `/^\\([!"#$%&'()*+,\-./:;<=>?@\[\]\\^_`{|}~])/`. */
static int rewrite_regex_slash_in_class(EditList *edits, const char *src, TSNode regex_node, RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(regex_node), "regex") != 0) return 0;
    TSNode pattern = find_child_type(regex_node, "regex_pattern", NULL);
    if (ts_node_is_null(pattern)) return 0;
    size_t ps = ts_node_start_byte(pattern), pe = ts_node_end_byte(pattern);

    /* First pass: count unescaped slashes inside char classes. */
    int in_class = 0;
    int needs_fix = 0;
    for (size_t i = ps; i < pe; i++)
    {
        char c = src[i];
        if (c == '\\' && i + 1 < pe) { i++; continue; }
        if (!in_class && c == '[') in_class = 1;
        else if (in_class && c == ']') in_class = 0;
        else if (in_class && c == '/') { needs_fix = 1; break; }
    }
    if (!needs_fix) return 0;
    if (overlaps) return 1;

    /* Second pass: build escaped pattern. */
    size_t cap = (pe - ps) * 2 + 1;
    char *out = NULL;
    REMALLOC(out, cap);
    size_t k = 0;
    in_class = 0;
    for (size_t i = ps; i < pe; i++)
    {
        char c = src[i];
        if (c == '\\' && i + 1 < pe)
        {
            out[k++] = c;
            out[k++] = src[++i];
            continue;
        }
        if (!in_class && c == '[') in_class = 1;
        else if (in_class && c == ']') in_class = 0;
        else if (in_class && c == '/')
        {
            out[k++] = '\\';
            out[k++] = '/';
            continue;
        }
        out[k++] = c;
    }
    out[k] = '\0';
    add_edit_take_ownership(edits, ps, pe, out, claimed);
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
/* Walk a regex pattern byte-by-byte counting capture groups.  Builds
   a list of (name, index) for each `(?<name>…)` and returns it via
   *names_out / *indices_out / *n_out (caller frees names_out).
   Returns 1 on success, 0 on parse failure.  Does NOT validate the
   regex — just tracks paren context so it can distinguish capture
   groups from `(?:…)`, `(?=…)`, `(?!…)`, `(?<=…)`, `(?<!…)`. */
static int regex_collect_named_groups(const char *in, size_t len,
                                      char ***names_out, int **indices_out,
                                      int *n_out)
{
    char **names = NULL;
    int *indices = NULL;
    int cap = 0, n = 0;
    int group_idx = 0;  /* incremented on each capturing `(` */
    int esc = 0;
    int in_class = 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = in[i];
        if (esc) { esc = 0; continue; }
        if (c == '\\') { esc = 1; continue; }
        if (in_class) {
            if (c == ']') in_class = 0;
            continue;
        }
        if (c == '[') { in_class = 1; continue; }
        if (c != '(') continue;
        /* It's a `(`.  Determine if it opens a capture group. */
        if (i + 1 < len && in[i + 1] == '?') {
            /* `(?:` or `(?=` or `(?!` — non-capturing.  `(?<name>` is
               capturing (named).  `(?<=` and `(?<!` are non-capturing
               (lookbehind, which duktape doesn't support anyway). */
            if (i + 2 < len && in[i + 2] == '<' &&
                i + 3 < len && in[i + 3] != '=' && in[i + 3] != '!')
            {
                /* `(?<name>…)` */
                group_idx++;
                size_t name_start = i + 3;
                size_t name_end = name_start;
                while (name_end < len && in[name_end] != '>') name_end++;
                if (name_end >= len) { /* malformed */ }
                size_t name_len = name_end - name_start;
                if (n >= cap) {
                    cap = cap ? cap * 2 : 4;
                    names = realloc(names, cap * sizeof(char *));
                    indices = realloc(indices, cap * sizeof(int));
                }
                names[n] = malloc(name_len + 1);
                memcpy(names[n], in + name_start, name_len);
                names[n][name_len] = 0;
                indices[n] = group_idx;
                n++;
            }
            /* else: `(?:` / `(?=` / `(?!` / `(?<=` / `(?<!` — non-capturing */
        } else {
            /* plain capture group */
            group_idx++;
        }
    }
    *names_out = names;
    *indices_out = indices;
    *n_out = n;
    return 1;
}

/* Rewrite a regex pattern for ES5 compatibility with the modern
   non-/u features removed:
   - `/s` (dotall): substitute `.` outside character class with `[\s\S]`
   - `(?<name>…)` named capture group: substitute with `(…)`
   - `\k<name>` named backref: substitute with `\N` from groups map

   Returns malloc'd string (caller frees) or NULL on parse failure.  */
static char *regex_modern_strip_pattern(const char *in, size_t len, int has_s,
                                        char **names, int *indices, int n_named)
{
    rp_string *out = rp_string_new(256);
    int esc = 0;
    int in_class = 0;
    for (size_t i = 0; i < len;)
    {
        char c = in[i];
        if (esc)
        {
            /* `\k<name>` backref?  Only valid outside char class. */
            if (!in_class && c == 'k' && i + 1 < len && in[i + 1] == '<' && n_named > 0)
            {
                size_t name_start = i + 2;
                size_t name_end = name_start;
                while (name_end < len && in[name_end] != '>') name_end++;
                if (name_end < len) {
                    size_t name_len = name_end - name_start;
                    int idx = 0;
                    for (int k = 0; k < n_named; k++) {
                        if (strlen(names[k]) == name_len
                            && memcmp(names[k], in + name_start, name_len) == 0) {
                            idx = indices[k];
                            break;
                        }
                    }
                    /* out already has the leading `\`; replace `\k<name>` with `\<idx>`. */
                    rp_string_appendf(out, "%d", idx);
                    i = name_end + 1;
                    esc = 0;
                    continue;
                }
            }
            rp_string_putc(out, c);
            i++;
            esc = 0;
            continue;
        }
        if (c == '\\') { rp_string_putc(out, '\\'); esc = 1; i++; continue; }
        if (in_class) {
            rp_string_putc(out, c);
            if (c == ']') in_class = 0;
            i++;
            continue;
        }
        if (c == '[') { rp_string_putc(out, '['); in_class = 1; i++; continue; }
        if (c == '.' && has_s) {
            rp_string_puts(out, "[\\s\\S]");
            i++;
            continue;
        }
        if (c == '(' && i + 3 < len && in[i + 1] == '?' && in[i + 2] == '<'
            && in[i + 3] != '=' && in[i + 3] != '!')
        {
            /* `(?<name>…)` → `(…)` */
            rp_string_putc(out, '(');
            /* skip past `?<name>` */
            i += 3;
            while (i < len && in[i] != '>') i++;
            if (i < len) i++;  /* skip the `>` */
            continue;
        }
        rp_string_putc(out, c);
        i++;
    }
    char *ret = rp_string_steal(out);
    out = rp_string_free(out);
    return ret;
}

/* Strip modern non-/u regex flags (`/s`, `/y`, `/d`) and convert named
   captures + named backrefs to numeric.  Lossy for /y (sticky) and /d
   (match indices) — the flag-driven behavior is lost but the regex
   still parses and matches the same characters.  /s gets a proper
   `.` → `[\s\S]` substitution.  Named-group references via
   `match.groups.name` will return undefined (we don't synthesize the
   .groups property) but `\k<name>` backrefs work correctly via the
   index map.  Runs AFTER `rewrite_regex_u_to_es5` — if /u was present,
   that rewriter already claimed the range and this one bails on
   overlap, leaving /us etc. unhandled (rare). */
static int rewrite_regex_modern_to_es5(EditList *edits, const char *src, TSNode regex_node,
                                       RangeList *claimed, int overlaps)
{
    if (strcmp(ts_node_type(regex_node), "regex") != 0)
        return 0;
    size_t rs = ts_node_start_byte(regex_node), re = ts_node_end_byte(regex_node);
    TSNode pattern = find_child_type(regex_node, "regex_pattern", NULL);
    TSNode flags = find_child_type(regex_node, "regex_flags", NULL);
    if (ts_node_is_null(pattern)) return 0;

    size_t ps = ts_node_start_byte(pattern), pe = ts_node_end_byte(pattern);
    size_t fs = ts_node_is_null(flags) ? 0 : ts_node_start_byte(flags);
    size_t fe = ts_node_is_null(flags) ? 0 : ts_node_end_byte(flags);

    int has_s = 0, has_y = 0, has_d = 0;
    for (size_t i = fs; i < fe; i++) {
        if (src[i] == 's') has_s = 1;
        else if (src[i] == 'y') has_y = 1;
        else if (src[i] == 'd') has_d = 1;
    }

    /* Detect named groups in pattern (cheap pre-scan). */
    int has_named = 0;
    {
        int esc = 0, in_class = 0;
        for (size_t i = ps; i < pe; i++) {
            char c = src[i];
            if (esc) { esc = 0; continue; }
            if (c == '\\') { esc = 1; continue; }
            if (in_class) { if (c == ']') in_class = 0; continue; }
            if (c == '[') { in_class = 1; continue; }
            if (c == '(' && i + 3 < pe && src[i+1] == '?' && src[i+2] == '<'
                && src[i+3] != '=' && src[i+3] != '!') { has_named = 1; break; }
        }
    }

    if (!has_s && !has_y && !has_d && !has_named) return 0;
    if (overlaps) return 1;

    /* Collect named-group map (needed for \k<name> conversion). */
    char **names = NULL; int *indices = NULL; int n_named = 0;
    if (has_named)
        regex_collect_named_groups(src + ps, pe - ps, &names, &indices, &n_named);

    char *newpat = regex_modern_strip_pattern(src + ps, pe - ps, has_s,
                                              names, indices, n_named);
    if (names) {
        for (int k = 0; k < n_named; k++) free(names[k]);
        free(names);
    }
    if (indices) free(indices);
    if (!newpat) return 0;

    /* Strip s/y/d from flags. */
    size_t nflen = 0;
    char *newflags = NULL;
    if (fe > fs) {
        newflags = malloc((fe - fs) + 1);
        for (size_t i = fs; i < fe; i++) {
            char c = src[i];
            if (c == 's' || c == 'y' || c == 'd') continue;
            newflags[nflen++] = c;
        }
        newflags[nflen] = 0;
    }

    size_t outlen = 1 + strlen(newpat) + 1 + nflen;
    char *rep = malloc(outlen + 1);
    size_t k = 0;
    rep[k++] = '/';
    memcpy(rep + k, newpat, strlen(newpat));
    k += strlen(newpat);
    rep[k++] = '/';
    if (nflen) { memcpy(rep + k, newflags, nflen); k += nflen; }
    rep[k] = 0;
    free(newpat);
    if (newflags) free(newflags);
    add_edit_take_ownership(edits, rs, re, rep, claimed);
    return 1;
}

// helper: generate fresh temporary names following _i, _x, _i2, _x2, ...
static void make_fresh_forof_names(char *ibuf, size_t ibufsz, char *xbuf, size_t xbufsz)
{
    static unsigned counter = 0;
    ++counter;
    // All generated names are _TrN_-prefixed to avoid collisions with
    // user variables.  Counter 1 emits "_TrN_i"/"_TrN_x"; later pairs
    // get a numeric suffix.
    if (counter == 1)
    {
        snprintf(ibuf, ibufsz, "_TrN_i");
        snprintf(xbuf, xbufsz, "_TrN_x");
    }
    else
    {
        snprintf(ibuf, ibufsz, "_TrN_i%u", counter);
        snprintf(xbuf, xbufsz, "_TrN_x%u", counter);
    }
}

// Rewrite plain (non-destructuring) for-of loops:
//   for (var a of X) { body }   =>  for (var _i=0,_x=X; _i<_x.length; _i++) { var a=_x[_i]; body }
//   for (a of X) { body }       =>  for (var _i=0,_x=X; _i<_x.length; _i++) { a=_x[_i]; body }
static int rewrite_for_of_simple(EditList *edits, const char *src, TSNode forof, RangeList *claimed,
                                 uint32_t *polysneeded, int overlaps)
{
    (void)polysneeded;

    if (ts_node_is_null(forof))
        return 0;
    if (strcmp(ts_node_type(forof), "for_in_statement") != 0)
        return 0;

    if (overlaps)
        return 1;

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
        /* ibuf is "_TrN_i" or "_TrN_iN"; skip the "_TrN_i" (6 chars) to
           get the numeric suffix portion (empty for counter==1). */
        const char *suffix = ibuf + 6;
        snprintf(itbuf, TPSMALLBUFSZ+1, "_TrN_it%s", suffix);
        snprintf(rbuf, TPSMALLBUFSZ, "_TrN_r%s", suffix);
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

    // For let/const: IIFE-wrap body so closures get fresh per-iteration binding,
    // but skip if body contains break/continue (can't cross function boundaries)
    if (is_let && body_has_loop_flow_control(body))
        is_let = false;

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
        snprintf(tvar, sizeof(tvar), "_TrN_nc%u", _nc_counter++);
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
                                     RangeList *claimed, int overlaps, int *unresolved)
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
    {
        /* A wider rewriter (e.g. class-body, async-method) has claimed
           the range we're inside.  Defer to the next pass once that
           wholesale rewrite settles into plain function bodies; our
           ?. operator will then be visible at the AST top level. */
        *unresolved = 1;
        return 1;
    }

    size_t es = ts_node_start_byte(node), ee = ts_node_end_byte(node);
    size_t elen = ee - es;

    /* Build clean expression (with ?. replaced by . or removed for ?.[/?.() */
    char *clean = malloc(elen + 1);
    size_t ci = 0;

    /* Collect positions of ?. in original, and map to clean positions */
    size_t oc_clean[32];
    int noc = 0;

    /* Track paren / bracket / brace depth: only process `?.` operators
       at depth 0.  Nested `?.` (inside call arguments, subscript keys,
       arrow-fn bodies, etc.) belong to their own sub-chain and will
       be rewritten by a separate top-level invocation — pre-fix, this
       loop greedily counted every `?.` in the byte range, which broke
       cases like `obj?.forEach(t => t.x?.())` (the inner chain's `?.`
       got merged into the outer's ternary nesting). */
    int paren_depth = 0;

    for (size_t i = 0; i < elen; i++)
    {
        char c = src[es + i];
        if (paren_depth == 0
            && i + 1 < elen && c == '?' && src[es + i + 1] == '.')
        {
            if (noc < 32)
            {
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
            continue;
        }
        if (c == '(' || c == '[' || c == '{') paren_depth++;
        else if (c == ')' || c == ']' || c == '}') paren_depth--;
        clean[ci++] = c;
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
        snprintf(tmpname, sizeof(tmpname), "_TrN_oc%d", oc_counter++);

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
                                      RangeList *claimed, int overlaps, int *unresolved)
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
    {
        /* A wholesale rewriter (typically the class-body emitter) has
           already claimed this range and is about to copy the source
           bytes verbatim — including our `||=`/`&&=`/`??=`.  Signal a
           re-pass so once that wider rewrite settles, our augmented
           assignment lives in a plain function context and can be
           lowered.  Returning 1 (handled) without an edit prevents
           competing rewriters from acting on the same node this pass. */
        *unresolved = 1;
        return 1;
    }

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
                                          RangeList *claimed, int overlaps, int *unresolved)
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
    /* Always rewrite get/set (Duktape doesn't support method-shorthand
       accessors). Also rewrite if params use ES2015+ features (rest,
       default, destructuring) so the function-param rewriters can fire
       on the resulting function_expression. Plain method shorthand with
       plain identifier params is left alone — Duktape handles it. */
    int is_get_or_set = ((namelen == 3 && strncmp(src + ns, "get", 3) == 0) ||
                        (namelen == 3 && strncmp(src + ns, "set", 3) == 0));
    int needs_param_rewrite = 0;
    if (!is_get_or_set)
    {
        TSNode params = ts_node_child_by_field_name(node, "parameters", 10);
        if (!ts_node_is_null(params))
        {
            uint32_t np = ts_node_named_child_count(params);
            for (uint32_t i = 0; i < np; i++)
            {
                TSNode p = ts_node_named_child(params, i);
                const char *pt = ts_node_type(p);
                if (strcmp(pt, "rest_pattern") == 0 ||
                    strcmp(pt, "assignment_pattern") == 0 ||
                    strcmp(pt, "object_pattern") == 0 ||
                    strcmp(pt, "array_pattern") == 0)
                {
                    needs_param_rewrite = 1;
                    break;
                }
            }
        }
    }
    if (!is_get_or_set && !needs_param_rewrite)
        return 0;
    if (overlaps)
    {
        /* A wider rewriter (arrow, async, class wholesale) has claimed
           the range we're inside.  Defer to next pass so when its
           rewrite settles into plain JS, our `get(){…}` → `get: function(){…}`
           rewrite can fire at top-level.  Pre-fix this silently
           dropped the rewrite and left invalid `{get(){…}}` in
           duktape-targeted output. */
        *unresolved = 1;
        return 1;
    }
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

/* Rewrite `require("bare-spec")` to `_TrN_Sp._req(module,"bare-spec")`
   so the runtime helper can walk node_modules from module.path.  Only
   bare specifiers — anything starting with `./`, `../`, `/`, `:` is left
   alone so existing relative/absolute/zip paths pass through untouched.
   The helper falls through to native require() if nothing resolves, so
   process.modulesPath specs (rampart-sql, etc.) still work. */
/* Shared detection: is this call `require("string-literal")`?  Returns
   1 with out-fields populated when yes, 0 otherwise. */
static int _detect_require_str_call(const char *src, TSNode call_expr,
                                    size_t *fs_out, size_t *a_s_out,
                                    const char **spec_out, size_t *spec_len_out)
{
    if (ts_node_is_null(call_expr) || strcmp(ts_node_type(call_expr), "call_expression") != 0)
        return 0;
    TSNode func = ts_node_child_by_field_name(call_expr, "function", 8);
    if (ts_node_is_null(func) || strcmp(ts_node_type(func), "identifier") != 0)
        return 0;
    size_t fs = ts_node_start_byte(func), fe = ts_node_end_byte(func);
    if (fe - fs != 7 || strncmp(src + fs, "require", 7) != 0)
        return 0;
    TSNode args = ts_node_child_by_field_name(call_expr, "arguments", 9);
    if (ts_node_is_null(args) || strcmp(ts_node_type(args), "arguments") != 0)
        return 0;
    if (ts_node_named_child_count(args) != 1)
        return 0;
    TSNode arg0 = ts_node_named_child(args, 0);
    if (strcmp(ts_node_type(arg0), "string") != 0)
        return 0;
    size_t a_s = ts_node_start_byte(arg0), a_e = ts_node_end_byte(arg0);
    if (a_e - a_s < 2) return 0;
    char q = src[a_s];
    if (q != '"' && q != '\'') return 0;
    *fs_out = fs;
    *a_s_out = a_s;
    *spec_out = src + a_s + 1;
    *spec_len_out = (a_e - a_s) - 2;
    return *spec_len_out > 0;
}

/* Rewrite `require("X.json")` to `_TrN_Sp._reqJson(module,"X.json")`.
   Rampart's loader treats every spec as JS source; without this, doing
   `require("./refs/data.json")` parses the JSON file as JavaScript and
   fails. */
static int rewrite_json_require(EditList *edits, const char *src, TSNode call_expr,
                                RangeList *claimed, uint32_t *polysneeded, int overlaps)
{
    size_t fs, a_s, spec_len;
    const char *spec;
    if (!_detect_require_str_call(src, call_expr, &fs, &a_s, &spec, &spec_len))
        return 0;
    if (spec_len < 5 || strncmp(spec + spec_len - 5, ".json", 5) != 0)
        return 0;

    if (overlaps)
        return 1;

    char *rep = NULL;
    const char *repl = "_TrN_Sp._reqJson(module,";
    size_t rl = strlen(repl);
    REMALLOC(rep, rl + 1);
    memcpy(rep, repl, rl);
    rep[rl] = '\0';
    add_edit_take_ownership(edits, fs, a_s, rep, claimed);
    *polysneeded |= JSON_REQ_PF;
    return 1;
}

static int rewrite_bare_require(EditList *edits, const char *src, TSNode call_expr,
                                RangeList *claimed, uint32_t *polysneeded, int overlaps)
{
    size_t fs, a_s, spec_len;
    const char *spec;
    if (!_detect_require_str_call(src, call_expr, &fs, &a_s, &spec, &spec_len))
        return 0;

    /* Bare-spec test: anything starting with `.`, `/`, `:` is not bare. */
    char c0 = spec[0];
    if (c0 == '.' || c0 == '/' || c0 == ':') return 0;

    /* `.json` is handled by rewrite_json_require so it can resolve via
       its own helper (returns parsed JSON, not a require result). */
    if (spec_len >= 5 && strncmp(spec + spec_len - 5, ".json", 5) == 0)
        return 0;

    if (overlaps)
        return 1;

    /* Replace the byte range [start_of_`require`, start_of_string_arg)
       with `_TrN_Sp._req(module,` so the open-paren + whitespace gets
       absorbed into the rewrite.  The string arg and closing `)` are
       left untouched. */
    char *rep = NULL;
    const char *repl = "_TrN_Sp._req(module,";
    size_t rl = strlen(repl);
    REMALLOC(rep, rl + 1);
    memcpy(rep, repl, rl);
    rep[rl] = '\0';
    add_edit_take_ownership(edits, fs, a_s, rep, claimed);
    *polysneeded |= BARE_REQ_PF;
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

        /* save: var _TrN_bsfN = NAME; */
        rp_string_appendf(saves, "var _TrN_bsf%d=%.*s;", id, (int)nlen, src + name_s);

        /* restore: NAME = _TrN_bsfN; */
        rp_string_appendf(restores, "%.*s=_TrN_bsf%d;", (int)nlen, src + name_s, id);

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

/* Emit a JS string literal into `out` covering src[s..e].
   Escapes backslash, double-quote, and C0 control chars; passes UTF-8 through. */
static void emit_js_string_literal(rp_string *out, const char *src, size_t s, size_t e)
{
    rp_string_puts(out, "\"");
    for (size_t i = s; i < e; i++)
    {
        unsigned char c = (unsigned char)src[i];
        switch (c)
        {
            case '\\': rp_string_puts(out, "\\\\"); break;
            case '"':  rp_string_puts(out, "\\\""); break;
            case '\n': rp_string_puts(out, "\\n"); break;
            case '\r': rp_string_puts(out, "\\r"); break;
            case '\t': rp_string_puts(out, "\\t"); break;
            case '\b': rp_string_puts(out, "\\b"); break;
            case '\f': rp_string_puts(out, "\\f"); break;
            case '\v': rp_string_puts(out, "\\v"); break;
            case '\0': rp_string_puts(out, "\\u0000"); break;
            /* U+2028 / U+2029 LINE/PARAGRAPH SEPARATOR: legal as raw chars in
               string literals in ES2019+, but stay safe and escape the 3-byte
               UTF-8 sequences E2 80 A8 / E2 80 A9. */
            default:
                if (c == 0xE2 && i + 2 < e
                    && (unsigned char)src[i+1] == 0x80
                    && ((unsigned char)src[i+2] == 0xA8 || (unsigned char)src[i+2] == 0xA9))
                {
                    rp_string_puts(out, (unsigned char)src[i+2] == 0xA8 ? "\\u2028" : "\\u2029");
                    i += 2;
                }
                else if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04X", c);
                    rp_string_puts(out, buf);
                }
                else
                {
                    rp_string_putsn(out, (const char *)&c, 1);
                }
                break;
        }
    }
    rp_string_puts(out, "\"");
}

/* Return 1 if the expression `node` is already the first argument of a
   _TrN_Sp._fs(...) call — used across passes to avoid double-wrapping
   function/arrow expressions. */
static int is_wrapped_fn_source(const char *src, TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (ts_node_is_null(parent)) return 0;
    /* The function is the first argument of the call, so parent is `arguments`
       (i.e. the call's argument list) — walk one more up to find call_expression. */
    TSNode grand = ts_node_parent(parent);
    if (ts_node_is_null(grand)) return 0;
    const char *gt = ts_node_type(grand);
    if (strcmp(gt, "call_expression") != 0) return 0;
    TSNode callee = ts_node_child_by_field_name(grand, "function", 8);
    if (ts_node_is_null(callee)) return 0;
    if (strcmp(ts_node_type(callee), "member_expression") != 0) return 0;
    size_t cs = ts_node_start_byte(callee), ce = ts_node_end_byte(callee);
    if (ce - cs == 11 && memcmp(src + cs, "_TrN_Sp._fs", 11) == 0) return 1;
    return 0;
}

/* For function_declaration: look at the immediately-following sibling to see if
   it's already `_TrN_Sp._fs(<name>, ...);`. If so, we've wrapped this one on a
   prior pass — skip. */
static int is_decl_already_attached(const char *src, TSNode decl, const char *name_s, size_t name_len)
{
    TSNode parent = ts_node_parent(decl);
    if (ts_node_is_null(parent)) return 0;
    uint32_t nc = ts_node_child_count(parent);
    /* find decl's index among children */
    int idx = -1;
    for (uint32_t i = 0; i < nc; i++)
    {
        TSNode kid = ts_node_child(parent, i);
        if (ts_node_eq(kid, decl)) { idx = (int)i; break; }
    }
    if (idx < 0) return 0;
    /* scan forward past ';' tokens for next statement */
    for (uint32_t i = (uint32_t)idx + 1; i < nc; i++)
    {
        TSNode sib = ts_node_child(parent, i);
        const char *st = ts_node_type(sib);
        if (strcmp(st, ";") == 0 || strcmp(st, "empty_statement") == 0) continue;
        if (strcmp(st, "expression_statement") != 0) return 0;
        TSNode expr = ts_node_named_child(sib, 0);
        if (ts_node_is_null(expr)) return 0;
        if (strcmp(ts_node_type(expr), "call_expression") != 0) return 0;
        TSNode callee = ts_node_child_by_field_name(expr, "function", 8);
        if (ts_node_is_null(callee)) return 0;
        if (strcmp(ts_node_type(callee), "member_expression") != 0) return 0;
        size_t cs = ts_node_start_byte(callee), ce = ts_node_end_byte(callee);
        if (!(ce - cs == 11 && memcmp(src + cs, "_TrN_Sp._fs", 11) == 0)) return 0;
        TSNode args = ts_node_child_by_field_name(expr, "arguments", 9);
        if (ts_node_is_null(args)) return 0;
        TSNode first = ts_node_named_child(args, 0);
        if (ts_node_is_null(first)) return 0;
        if (strcmp(ts_node_type(first), "identifier") != 0) return 0;
        size_t is = ts_node_start_byte(first), ie = ts_node_end_byte(first);
        if (ie - is == name_len && memcmp(src + is, name_s, name_len) == 0) return 1;
        return 0;
    }
    return 0;
}

/* Capture original pre-transpile function source and emit a _TrN_Sp._fs wrapper.
   Claims the node range so other function rewriters defer to the next pass. */
static int rewrite_attach_fn_source(EditList *edits, const char *src, TSNode node, RangeList *claimed,
                                    int overlaps)
{
    /* tree-sitter also emits anonymous keyword nodes whose type is "function"
       or "function_expression" — those would match the strcmps below. Gate on
       is_named first, and also require a `body` field so we don't grab
       keyword-only nodes that happen to share a type name. */
    if (!ts_node_is_named(node)) return 0;
    /* Skip anything inside the prepended polyfill prefix (pass >= 1). */
    if (ts_node_end_byte(node) <= _tp_polyfill_prefix_len) return 0;
    const char *t = ts_node_type(node);
    int is_decl = (strcmp(t, "function_declaration") == 0 ||
                   strcmp(t, "generator_function_declaration") == 0);
    int is_expr = (strcmp(t, "function_expression") == 0 ||
                   strcmp(t, "function") == 0 ||
                   strcmp(t, "arrow_function") == 0 ||
                   strcmp(t, "generator_function_expression") == 0 ||
                   strcmp(t, "generator_function") == 0);
    if (!is_decl && !is_expr) return 0;
    if (ts_node_is_null(ts_node_child_by_field_name(node, "body", 4))) return 0;

    /* Skip if we've already wrapped this node in a previous pass. */
    if (is_expr && is_wrapped_fn_source(src, node)) return 0;

    size_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);

    if (is_decl)
    {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        if (ts_node_is_null(name)) return 0; /* skip anonymous default-export decls */
        size_t nms = ts_node_start_byte(name), nme = ts_node_end_byte(name);

        if (is_decl_already_attached(src, node, src + nms, nme - nms)) return 0;

        if (overlaps) return 1;

        rp_string *lit = rp_string_new(ne - ns + 32);
        emit_js_string_literal(lit, src, ns, ne);

        rp_string *ins = rp_string_new(ne - ns + 64);
        rp_string_puts(ins, ";_TrN_Sp._fs(");
        rp_string_putsn(ins, src + nms, nme - nms);
        rp_string_puts(ins, ",");
        rp_string_putsn(ins, lit->str, lit->len);
        rp_string_puts(ins, ");");
        lit = rp_string_free(lit);

        add_edit_take_ownership(edits, ne, ne, rp_string_steal(ins), claimed);
        ins = rp_string_free(ins);
        return 1;
    }

    /* Expression form: wrap with _TrN_Sp._fs(<expr>, "<src>"). */
    if (overlaps) return 1;
    {
        rp_string *lit = rp_string_new(ne - ns + 32);
        emit_js_string_literal(lit, src, ns, ne);

        rp_string *tail = rp_string_new(ne - ns + 16);
        rp_string_puts(tail, ",");
        rp_string_putsn(tail, lit->str, lit->len);
        rp_string_puts(tail, ")");
        lit = rp_string_free(lit);

        /* Token-fusion guard: same pattern as the arrow rewriter.
           `return(e,o)=>…` puts byte `n` (end of `return`) right
           before our `_TrN_Sp._fs(` insert.  JS lexer reads
           `return_TrN_Sp` as one identifier and silently breaks. */
        const char *prefix = "_TrN_Sp._fs(";
        if (ns > 0) {
            unsigned char pb = (unsigned char)src[ns - 1];
            int pb_id = (pb >= 'a' && pb <= 'z') || (pb >= 'A' && pb <= 'Z')
                     || (pb >= '0' && pb <= '9') || pb == '_' || pb == '$';
            if (pb_id) prefix = " _TrN_Sp._fs(";
        }
        add_edit(edits, ns, ns, prefix, claimed);
        add_edit_take_ownership(edits, ne, ne, rp_string_steal(tail), claimed);
        tail = rp_string_free(tail);
    }
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

        /* Skip nodes entirely inside the prepended polyfill prefix:
           - Rewriters firing here would edit the polyfill text.
           - More importantly, scanning identifiers and member_expressions
             inside the polyfill body (which mentions Promise, Set, Map,
             Object.entries, getOwnPropertyDescriptors, …) would trigger
             *_PF detections, causing apply_edits to prepend a SECOND
             polyfill preamble on the next pass. See transpiler-todo §3.
           Skip the whole subtree by going to the next sibling instead of
           descending into children. */
        if (_tp_polyfill_prefix_len && ne <= _tp_polyfill_prefix_len)
            goto skip_subtree;

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
            if (!handled)
                handled = rewrite_regex_modern_to_es5(edits, src, n, &claimed, overlaps);
            if (!handled)
                handled = rewrite_regex_slash_in_class(edits, src, n, &claimed, overlaps);
        }

        if (!handled && (strcmp(nt, "template_string") == 0 || strcmp(nt, "template_literal") == 0))
        {
            handled = rewrite_template_node(edits, src, n, &claimed, overlaps);
        }

        if (!handled && (strcmp(nt, "call_expression") == 0))
        {
            handled = rewrite_string_raw(edits, src, n, &claimed, overlaps);
        }

        if (!handled && (strcmp(nt, "call_expression") == 0))
        {
            handled = rewrite_json_require(edits, src, n, &claimed, polysneeded, overlaps);
        }

        if (!handled && (strcmp(nt, "call_expression") == 0))
        {
            handled = rewrite_bare_require(edits, src, n, &claimed, polysneeded, overlaps);
        }

        if (!handled && (strcmp(nt, "string") == 0 || strcmp(nt, "template_literal") == 0))
        {
            handled = rewrite_raw_node(edits, src, n, &claimed, overlaps);
        }

        /* Attach original pre-transpile function source so toString can return
           it. Runs on every pass — on pass 1+ this wraps functions that were
           copied verbatim out of an async/generator body-rewrite (so the
           original-source text of nested arrows/fns is preserved). Functions
           inside the prepended polyfill text are skipped via the range check
           below against _tp_polyfill_prefix_len. */
        if (_tp_fn_sources)
        {
            int saw = rewrite_attach_fn_source(edits, src, n, &claimed, overlaps);
            if (saw)
                *polysneeded |= FN_SOURCE_PF;
        }

        /* class transpile produces functions, and then in pass2, handle them */
        if (!handled && strcmp(nt, "class_declaration") == 0)
        {
            handled = rewrite_class_to_es5(edits, src, n, &claimed, polysneeded, overlaps);
            if (handled)
            {
                *polysneeded |= CLASS_PF;
                /* Class method descriptors now carry a `src:"..."` field
                   that defineProperties wires up via `_TrN_Sp._fs` for
                   per-method Function.prototype.toString. */
                if (_tp_fn_sources) *polysneeded |= FN_SOURCE_PF;
            }
        }

        if (!handled && strcmp(nt, "class") == 0)
        {
            handled = rewrite_class_expression_to_es5(edits, src, n, &claimed, polysneeded, overlaps);
            if (handled)
            {
                *polysneeded |= CLASS_PF;
                if (_tp_fn_sources) *polysneeded |= FN_SOURCE_PF;
            }
        }

        /* Block-scoped function declarations (ES2015) */
        if (!handled && strcmp(nt, "statement_block") == 0)
        {
            handled = rewrite_block_scoped_functions(edits, src, n, &claimed, overlaps);
        }

        /* Babel-style block-scope rename for let/const in nested blocks.
           Dispatched once per function-like scope (and once for `program`).
           See transpiler-todo.md §8 Phases 1-4.
           arrow_function is intentionally NOT dispatched here — the arrow
           rewriter calls rewrite_block_scope_rename itself into a LOCAL
           edit list and splices the renamed body into its wholesale
           replacement. Dispatching from the main pass too would just
           queue edits that the arrow's wholesale-replace clobbers. */
        if (!handled && (strcmp(nt, "function_declaration") == 0 ||
                         strcmp(nt, "function_expression") == 0 ||
                         strcmp(nt, "function") == 0 ||
                         strcmp(nt, "generator_function_declaration") == 0 ||
                         strcmp(nt, "generator_function") == 0 ||
                         strcmp(nt, "generator_function_expression") == 0 ||
                         strcmp(nt, "method_definition") == 0 ||
                         strcmp(nt, "program") == 0))
        {
#ifdef TDZ_RUNTIME_CHECKS
            /* TDZ + const-reassign checks run BEFORE block-scope rename
               so any conflicting identifier rewrite (TDZ throw replaces
               the same position the rename would touch) wins. */
            (void)rewrite_let_const_runtime_checks(edits, src, n, &claimed, overlaps);
#endif
            int saw = rewrite_block_scope_rename(edits, src, n, &claimed, overlaps);
            /* Don't set handled — other rewriters need to also fire on
               this same function node (async, generator, default params,
               etc.). Block-scope rename emits non-overlapping edits at
               identifier positions only. */
            (void)saw;
        }

        if (!handled && (strcmp(nt, "import_statement") == 0))
        {
            handled = rewrite_import_node(edits, src, n, &claimed, polysneeded, overlaps);
        }
        if (!handled && strcmp(nt, "call_expression") == 0)
        {
            handled = rewrite_dynamic_import(edits, src, n, &claimed, polysneeded, overlaps);
        }
        if (!handled && strcmp(nt, "program") == 0)
        {
            int saw = rewrite_top_level_await(edits, src, n, &claimed, polysneeded,
                                              unresolved, overlaps);
            /* Don't set handled — block-scope and other program-level
               rewriters need to fire on the same node. */
            (void)saw;
        }
        if (!handled && (strcmp(nt, "export_statement") == 0))
        {
            handled = rewrite_export_node(edits, src, n, &claimed, polysneeded, overlaps);
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
            /* Run method-shorthand rewriting but do NOT set handled — the
               function-param rewriters below also need to fire on this node
               to lower rest/default/destructuring params. The two edits
               don't conflict: shorthand inserts `: function` at name-end
               (zero-width); param rewriters edit inside the params or
               after the closing paren. */
            (void)rewrite_plain_method_shorthand(edits, src, n, &claimed, overlaps, unresolved);
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
                         strcmp(nt, "generator_function") == 0 || strcmp(nt, "generator_function_expression") == 0 ||
                         strcmp(nt, "method_definition") == 0))
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
            // Handle let/const in plain for...in: just replace the keyword with var
            if (!handled)
            {
                TSNode kind = ts_node_child_by_field_name(n, "kind", 4);
                if (!ts_node_is_null(kind))
                {
                    size_t ks = ts_node_start_byte(kind), ke = ts_node_end_byte(kind);
                    if ((ke - ks == 3 && strncmp(src + ks, "let", 3) == 0))
                    {
                        if (!overlaps)
                            add_edit(edits, ks, ke, "var", &claimed);
                        handled = 1;
                    }
                    else if ((ke - ks == 5 && strncmp(src + ks, "const", 5) == 0))
                    {
                        if (!overlaps)
                            add_edit(edits, ks, ke, "var  ", &claimed);
                        handled = 1;
                    }
                }
            }
        }

        if (!handled && strcmp(nt, "lexical_declaration") == 0)
        {
            // Try destructuring first — it handles the keyword change itself
            handled = rewrite_destructuring_declaration(edits, src, n, &claimed, overlaps);
            if (!handled)
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
            handled = rewrite_optional_chaining(edits, src, n, &claimed, overlaps, unresolved);
        }

        /* Logical assignment (ES2021): a ??= b, a ||= b, a &&= b */
        if (!handled && strcmp(nt, "augmented_assignment_expression") == 0)
        {
            handled = rewrite_logical_assignment(edits, src, n, &claimed, overlaps, unresolved);
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

        /* Strip trailing commas from function params and call arguments (ES2017 -> ES5).
           Skip if the comma is inside an already-claimed region (e.g. the
           enclosing arrow rewrite has wholesale-replaced this byte range —
           it preserves the comma verbatim in its output, and our stripper
           edit would collide with the wider replace in apply_edits and
           corrupt the surrounding bytes). */
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
                        if (!rl_overlaps(&claimed, j, j + 1, "trailing-comma-strip"))
                            add_edit(edits, j, j + 1, "", &claimed);
                        else
                            *unresolved = 1; /* re-scan after the wider rewrite settles */
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
            /* `RegExp` used as constructor/callee — install the wrapper
               that strips the `u` flag at runtime, since duktape rejects
               it. Static `/foo/u` literals are handled by the dedicated
               u-to-es5 rewriter; this catches the dynamic case where the
               flag comes from a variable (e.g. marked's `edit(..., 'gu')`). */
            if (ilen == 6 && strncmp(src + start, "RegExp", 6) == 0)
                *polysneeded |= REGEXP_U_PF;
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
    skip_subtree:
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
    /* polysdone tracks which polyfills have already been emitted into
       the output across multi-pass transpilation of THIS file.  It used
       to be `static`, which made cached `.transpiled.js` files non
       self-contained: a child file transpiled after a parent that had
       already needed `_fs` would silently omit the helper from its own
       cache (because polysdone carried it).  Reloading the cached file
       standalone then failed with `_TrN_Sp.* undefined`.  Per-call
       state keeps each cache self-contained.  Multi-pass within one
       file still dedupes via the loop's own update of polysdone. */
    uint32_t polysdone = 0;
    (void)track_polys;

    _tp_pass_idx = 0;

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
            warn_unsupported_patterns(src, root);

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

        _tp_pass_idx = npasses;
        npasses++;

        /* Compute polyfill-prefix length for this pass's src so fn-source
           skips functions that are part of the prepended polyfill preamble.
           Each preamble starts with "if(!global._TrN_Sp)" and ends with the
           literal ";_TrN_Sp.load();". When later passes detect additional
           polyfills, apply_edits prepends a SECOND preamble ahead of the
           first — we loop to skip all consecutive preambles. */
        _tp_polyfill_prefix_len = 0;
        {
            const char *poly_prefix = "if(!global._TrN_Sp)";
            size_t poly_prefix_sz = strlen(poly_prefix);
            const char *end_marker = ";_TrN_Sp.load();";
            size_t end_sz = strlen(end_marker);
            size_t off = 0;
            /* allow shebang line to precede the preamble */
            if (src_len > 2 && src[0] == '#' && src[1] == '!')
            {
                const char *nl = memchr(src, '\n', src_len);
                if (nl) off = (size_t)(nl + 1 - src);
            }
            for (;;)
            {
                if (off + poly_prefix_sz > src_len) break;
                if (memcmp(src + off, poly_prefix, poly_prefix_sz) != 0) break;
                const char *hit = memmem(src + off, src_len - off, end_marker, end_sz);
                if (!hit) break;
                off = (size_t)(hit - src) + end_sz;
                _tp_polyfill_prefix_len = off;
            }
        }

        if (npasses > MAX_PASSES)
        {
            /* Deeply-nested async-inside-async / generator-inside-generator
               can require one pass per level (each pass "opens up" one
               layer for fn-source to see).  Bail with a recoverable
               error instead of `exit(1)` — calling exit() from a
               library tears down the whole process. */
            char msg[128];
            snprintf(msg, sizeof(msg),
                "Transpiler: gave up after %d passes (probably deeply-nested async/generator)\n",
                MAX_PASSES);
            res.transpiled = NULL;
            res.errmsg = strdup(msg);
            res.err = 1;
            res.pos = 0;
            res.line_num = 0;
            res.col_num = 0;
            res.altered = 0;
            if (free_src) free(free_src);
            free_edits(&edits);
            ts_tree_delete(tree);
            ts_parser_delete(parser);
            return res;
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


