/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT

   To build the test
   cc -g -DTEST -o transpiler -Ilib/include/ transpiler.c \
       tree-sitter-javascript/src/parser.c tree-sitter-javascript/src/scanner.c \
       lib/src/lib.c
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "transpiler.h"
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


polyfills allpolys[] = {
    // stolen from babel.  Babel, like this prog is MIT licensed - see https://github.com/babel/babel/blob/main/LICENSE
    {
        "_TrN_Sp.__spreadO = function(target) {function ownKeys(object, enumerableOnly){var keys = Object.keys(object);if (Object.getOwnPropertySymbols){var symbols = Object.getOwnPropertySymbols(object);if (enumerableOnly)symbols = symbols.filter(function(sym) {return Object.getOwnPropertyDescriptor(object, sym).enumerable;});keys.push.apply(keys, symbols);}return keys;}function _defineProperty(obj, key, value){if (key in obj){Object.defineProperty(obj, key, {value : value, enumerable : true, configurable : true, writable : true});}else{obj[key] = value;}return obj;}for (var i = 1; i < arguments.length; i++){var source = arguments[i] != null ? arguments[i] : {};if (i % 2){ownKeys(Object(source), true).forEach(function(key) {_defineProperty(target, key, source[key]);});}else if (Object.getOwnPropertyDescriptors){Object.defineProperties(target, Object.getOwnPropertyDescriptors(source));}else{ownKeys(Object(source)).forEach(function(key) {Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(source, key));});}}return target;};_TrN_Sp.__spreadA = function(target, arr) {if (arr instanceof Array)return target.concat(arr);function _nonIterableSpread(){throw new TypeError(\"Invalid attempt to spread non-iterable instance. In order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");}function _unsupportedIterableToArray(o, minLen){if (!o)return;if (typeof o === \"string\")return _arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor);n = o.constructor.name;if (n === \"Map\" || n === \"Set\")return Array.from(o);if (n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n))return target.concat(_arrayLikeToArray(o, minLen));}function _iterableToArray(iter){if (typeof Symbol !== \"undefined\" && Symbol.iterator in Object(iter))return target.concat(Array.from(iter));}function _arrayLikeToArray(arr, len){if (len == null || len > arr.length)len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++){arr2[i] = arr[i];}return target.concat(arr2);}function _arrayWithoutHoles(arr){if (Array.isArray(arr))return target.concat(_arrayLikeToArray(arr));}return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread();};_TrN_Sp._arrayConcat = function(items){var self = this;items.forEach(function(item) {self.push(item);});return this;};_TrN_Sp._newArray = function() {Object.defineProperty(Array.prototype, '_addchain', {value: _TrN_Sp._arrayConcat,writable: true,configurable: true,enumerable: false});Object.defineProperty(Array.prototype, '_concat', {value: Array.prototype._addchain,writable: true,configurable: true,enumerable: false});return [];};_TrN_Sp._objectAddchain = function(key, value) {if (typeof key == 'object'){Object.assign(this, key)}else{this[key] = value;}return this;};_TrN_Sp._newObject = function() {Object.defineProperty(Object.prototype, '_addchain', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});Object.defineProperty(Object.prototype, '_concat', {value: _TrN_Sp._objectAddchain,writable: true,configurable: true,enumerable: false});return {};};",
        0, (uint32_t)SPREAD_PF },
    {
        "_TrN_Sp._typeof=function(obj) {\"@babel/helpers - typeof\";if (typeof Symbol === \"function\" && typeof Symbol.iterator === \"symbol\") {_TrN_Sp._typeof = function(obj) {return typeof obj;};} else {_TrN_Sp._typeof = function(obj) {return obj && typeof Symbol === \"function\" && obj.constructor === Symbol && obj !== Symbol.prototype ? \"symbol\" : typeof obj;};}return _TrN_Sp._typeof(obj);};_TrN_Sp._getRequireWildcardCache=function() {if (typeof WeakMap !== \"function\") return null;var cache = new WeakMap();_TrN_Sp._getRequireWildcardCache=function(){return cache;};return cache;};_TrN_Sp._interopRequireWildcard=function(obj) {if (obj && obj.__esModule) {return obj;}if (obj === null || _TrN_Sp._typeof(obj) !== \"object\" && typeof obj !== \"function\") {return { \"default\": obj };}var cache = _TrN_Sp._getRequireWildcardCache();if (cache && cache.has(obj)) {return cache.get(obj);}var newObj = {};var hasPropertyDescriptor = Object.defineProperty && Object.getOwnPropertyDescriptor;for (var key in obj) {if (Object.prototype.hasOwnProperty.call(obj, key)) {var desc = hasPropertyDescriptor ? Object.getOwnPropertyDescriptor(obj, key) : null;if (desc && (desc.get || desc.set)) {Object.defineProperty(newObj, key, desc);} else {newObj[key] = obj[key];}}}newObj[\"default\"] = obj;if (cache) {cache.set(obj, newObj);}return newObj;};_TrN_Sp._interopDefault=function(m){if(typeof m =='object' && m.__esModule){return m.default}return m;};",
        0, (uint32_t)IMPORT_PF },
    {
        "_TrN_Sp.typeof =function(obj) {'@babel/helpers - typeof';if (typeof Symbol === 'function' && typeof Symbol.iterator === 'symbol') {_typeof = function _typeof(obj) {return typeof obj;};} else {_typeof = function _typeof(obj) {return obj && typeof Symbol === 'function' &&obj.constructor === Symbol && obj !== Symbol.prototype ?'symbol' :typeof obj;};}return _typeof(obj);}; _TrN_Sp.inherits =function(subClass, superClass) {if (typeof superClass !== 'function' && superClass !== null) {throw new TypeError('Super expression must either be null or a function');}subClass.prototype = Object.create(superClass && superClass.prototype,{constructor: {value: subClass, writable: true, configurable: true}});if (superClass) _TrN_Sp.setPrototypeOf(subClass, superClass);}; _TrN_Sp.setPrototypeOf =function(o, p) {_setPrototypeOf = Object.setPrototypeOf || function _setPrototypeOf(o, p) {o.__proto__ = p;return o;};return _setPrototypeOf(o, p);}; _TrN_Sp.createSuper =function(Derived) {var hasNativeReflectConstruct = _TrN_Sp.isNativeReflectConstruct();return function _createSuperInternal() {var Super = _TrN_Sp.getPrototypeOf(Derived), result;result = Super.apply(this, arguments);return _TrN_Sp.possibleConstructorReturn(this, result);};}; _TrN_Sp.possibleConstructorReturn =function(self, call) {if (call && (_typeof(call) === 'object' || typeof call === 'function')) {return call;}return _TrN_Sp.assertThisInitialized(self);}; _TrN_Sp.assertThisInitialized =function(self) {if (self === void 0) {throw new ReferenceError('this hasn\\'t been initialised - super() hasn\\'t been called');}return self;}; _TrN_Sp.isNativeReflectConstruct =function() {if (typeof Reflect === 'undefined' || !Reflect.construct) return false;if (Reflect.construct.sham) return false;if (typeof Proxy === 'function') return true;try {Date.prototype.toString.call(Reflect.construct(Date, [], function() {}));return true;} catch (e) {return false;}}; _TrN_Sp.getPrototypeOf =function(o) {_getPrototypeOf = Object.setPrototypeOf ?Object.getPrototypeOf :function _getPrototypeOf(o) {return o.__proto__ || Object.getPrototypeOf(o);};return _getPrototypeOf(o);}; _TrN_Sp.classCallCheck =function(instance, Constructor) {if (!(instance instanceof Constructor)) {throw new TypeError('Cannot call a class as a function');}}; _TrN_Sp.defineProperties =function(target, props) {for (var i = 0; i < props.length; i++) {var descriptor = props[i];descriptor.enumerable = descriptor.enumerable || false;descriptor.configurable = true;if ('value' in descriptor) descriptor.writable = true;Object.defineProperty(target, descriptor.key, descriptor);}}; _TrN_Sp.createClass =function(Constructor, protoProps,staticProps) {if (protoProps) _TrN_Sp.defineProperties(Constructor.prototype, protoProps);if (staticProps) _TrN_Sp.defineProperties(Constructor, staticProps);return Constructor;};",
        0, (uint32_t)CLASS_PF  },
    {
        "_TrN_Sp.slicedToArray=function (arr, i) {return _TrN_Sp.arrayWithHoles(arr) || _TrN_Sp.iterableToArrayLimit(arr, i) || _TrN_Sp.unsupportedIterableToArray(arr, i) || _TrN_Sp.nonIterableRest();};_TrN_Sp.nonIterableRest=function(){throw new TypeError(\"Invalid attempt to destructure non-iterable instance.\\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.\");};_TrN_Sp.unsupportedIterableToArray=function(o, minLen) {if (!o) return;if (typeof o === \"string\") return _TrN_Sp.arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === \"Object\" && o.constructor) n = o.constructor.name;if (n === \"Map\" || n === \"Set\") return Array.from(o);if (n === \"Arguments\" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n)) return _TrN_Sp.arrayLikeToArray(o, minLen);};_TrN_Sp.arrayLikeToArray=function(arr, len) {if (len == null || len > arr.length) len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++) {arr2[i] = arr[i];}return arr2;};_TrN_Sp.iterableToArrayLimit=function(arr, i){if (typeof Symbol === \"undefined\" || !(Symbol.iterator in Object(arr))) return;var _arr = [];var _n = true;var _d = false;var _e = undefined;try {for (var _i = arr[Symbol.iterator](), _s; !(_n = (_s = _i.next()).done); _n = true) {_arr.push(_s.value);if (i && _arr.length === i) break;}} catch (err) {_d = true;_e = err;} finally {try {if (!_n && _i[\"return\"] != null) _i[\"return\"]();} finally {if (_d) throw _e;}}return _arr;};_TrN_Sp.arrayWithHoles=function(arr) {if (Array.isArray(arr)) return arr;};",
        0, (uint32_t)FOROF_PF  },
    {
        "_TrN_Sp.asyncGeneratorStep = function(gen, resolve, reject, _next, _throw, key, arg) {try{var info = gen[key](arg);var value = info.value;}catch (error){reject(error);return;}if (info.done){resolve(value);}else{Promise.resolve(value).then(_next, _throw);}};_TrN_Sp.asyncToGenerator = function(fn) {return function() {var self = this, args = arguments;return new Promise(function(resolve, reject) {var gen = fn.apply(self, args);function _next(value){_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, \"next\", value);}function _throw(err){_TrN_Sp.asyncGeneratorStep(gen, resolve, reject, _next, _throw, \"throw\", err);}_next(undefined);});};};_TrN_Sp.regeneratorRuntime = (function () {function mark(genFn) { return genFn; }function wrap(innerFn) {var context = {prev: 0,next: 0,sent: void 0,done: false,rval: void 0,stop: function () { this.done = true; return this.rval; }};return {next: function (arg) {var prevNext = context.next;context.sent = arg;var value = innerFn(context);if (context.done || context.next === \"end\") {return { value: context.rval, done: true };}if (context.next === prevNext) {context.done = true;context.rval = value;return { value: context.rval, done: true };}return { value: value, done: false };},throw: function (err) { throw err; }};}return { mark: mark, wrap: wrap };})();",
        0, (uint32_t)ASYNC_PF  },

    // from https://www.npmjs.com/package/promise-polyfill MIT license included in this dir.
    // the delete global.Promise is for rampart.thread reload.
    {
        "delete global.Promise;(function(e,t){\"object\"==typeof exports&&\"undefined\"!=typeof module?t():\"function\"==typeof define&&define.amd?define(t):t()})(0,function(){\"use strict\";function e(e){var t=this.constructor;return this.then(function(n){return t.resolve(e()).then(function(){return n})},function(n){return t.resolve(e()).then(function(){return t.reject(n)})})}function t(e){return new this(function(t,n){function r(e,n){if(n&&(\"object\"==typeof n||\"function\"==typeof n)){var f=n.then;if(\"function\"==typeof f)return void f.call(n,function(t){r(e,t)},function(n){o[e]={status:\"rejected\",reason:n},0==--i&&t(o)})}o[e]={status:\"fulfilled\",value:n},0==--i&&t(o)}if(!e||\"undefined\"==typeof e.length)return n(new TypeError(typeof e+\" \"+e+\" is not iterable(cannot read property Symbol(Symbol.iterator))\"));var o=Array.prototype.slice.call(e);if(0===o.length)return t([]);for(var i=o.length,f=0;o.length>f;f++)r(f,o[f])})}function n(e,t){this.name=\"AggregateError\",this.errors=e,this.message=t||\"\"}function r(e){var t=this;return new t(function(r,o){if(!e||\"undefined\"==typeof e.length)return o(new TypeError(\"Promise.any accepts an array\"));var i=Array.prototype.slice.call(e);if(0===i.length)return o();for(var f=[],u=0;i.length>u;u++)try{t.resolve(i[u]).then(r)[\"catch\"](function(e){f.push(e),f.length===i.length&&o(new n(f,\"All promises were rejected\"))})}catch(c){o(c)}})}function o(e){return!(!e||\"undefined\"==typeof e.length)}function i(){}function f(e){if(!(this instanceof f))throw new TypeError(\"Promises must be constructed via new\");if(\"function\"!=typeof e)throw new TypeError(\"not a function\");this._state=0,this._handled=!1,this._value=undefined,this._deferreds=[],s(e,this)}function u(e,t){for(;3===e._state;)e=e._value;0!==e._state?(e._handled=!0,f._immediateFn(function(){var n=1===e._state?t.onFulfilled:t.onRejected;if(null!==n){var r;try{r=n(e._value)}catch(o){return void a(t.promise,o)}c(t.promise,r)}else(1===e._state?c:a)(t.promise,e._value)})):e._deferreds.push(t)}function c(e,t){try{if(t===e)throw new TypeError(\"A promise cannot be resolved with itself.\");if(t&&(\"object\"==typeof t||\"function\"==typeof t)){var n=t.then;if(t instanceof f)return e._state=3,e._value=t,void l(e);if(\"function\"==typeof n)return void s(function(e,t){return function(){e.apply(t,arguments)}}(n,t),e)}e._state=1,e._value=t,l(e)}catch(r){a(e,r)}}function a(e,t){e._state=2,e._value=t,l(e)}function l(e){2===e._state&&0===e._deferreds.length&&f._immediateFn(function(){e._handled||f._unhandledRejectionFn(e._value)});for(var t=0,n=e._deferreds.length;n>t;t++)u(e,e._deferreds[t]);e._deferreds=null}function s(e,t){var n=!1;try{e(function(e){n||(n=!0,c(t,e))},function(e){n||(n=!0,a(t,e))})}catch(r){if(n)return;n=!0,a(t,r)}}n.prototype=Error.prototype;var d=setTimeout;f.prototype[\"catch\"]=function(e){return this.then(null,e)},f.prototype.then=function(e,t){var n=new this.constructor(i);return u(this,new function(e,t,n){this.onFulfilled=\"function\"==typeof e?e:null,this.onRejected=\"function\"==typeof t?t:null,this.promise=n}(e,t,n)),n},f.prototype[\"finally\"]=e,f.all=function(e){return new f(function(t,n){function r(e,o){try{if(o&&(\"object\"==typeof o||\"function\"==typeof o)){var u=o.then;if(\"function\"==typeof u)return void u.call(o,function(t){r(e,t)},n)}i[e]=o,0==--f&&t(i)}catch(c){n(c)}}if(!o(e))return n(new TypeError(\"Promise.all accepts an array\"));var i=Array.prototype.slice.call(e);if(0===i.length)return t([]);for(var f=i.length,u=0;i.length>u;u++)r(u,i[u])})},f.any=r,f.allSettled=t,f.resolve=function(e){return e&&\"object\"==typeof e&&e.constructor===f?e:new f(function(t){t(e)})},f.reject=function(e){return new f(function(t,n){n(e)})},f.race=function(e){return new f(function(t,n){if(!o(e))return n(new TypeError(\"Promise.race accepts an array\"));for(var r=0,i=e.length;i>r;r++)f.resolve(e[r]).then(t,n)})},f._immediateFn=\"function\"==typeof setImmediate&&function(e){setImmediate(e)}||function(e){d(e,0)},f._unhandledRejectionFn=function(e){void 0!==console&&console&&console.warn(\"Possible Unhandled Promise Rejection:\",e)};var p=function(){if(\"undefined\"!=typeof self)return self;if(\"undefined\"!=typeof window)return window;if(\"undefined\"!=typeof global)return global;throw Error(\"unable to locate global object\")}();\"function\"!=typeof p.Promise?p.Promise=f:(p.Promise.prototype[\"finally\"]||(p.Promise.prototype[\"finally\"]=e),p.Promise.allSettled||(p.Promise.allSettled=t),p.Promise.any||(p.Promise.any=r))});",
        0, (uint32_t)PROMISE_PF},
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
        // printf("replaced:\n'%s'\n", out);

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

static inline int vappendf(char **bufp, const char *fmt, va_list ap)
{
    if (!bufp || !fmt)
        return -1;

    const char *old = *bufp;
    size_t oldlen = 0;
    if (old)
    {
        /* must be a valid NUL-terminated string */
        oldlen = strlen(old);
    }

    /* Determine required length for the new formatted chunk */
    va_list ap_count;
    va_copy(ap_count, ap);
    int need = vsnprintf(NULL, 0, fmt, ap_count);
    va_end(ap_count);
    if (need < 0)
    {
        return -1; /* formatting error */
    }

    /* Allocate a new buffer safely (do NOT use realloc yet).
       We only free the old buffer after successful formatting. */
    size_t newsize = oldlen + (size_t)need + 1; /* +1 for NUL */
    char *newbuf = NULL;
    REMALLOC(newbuf, newsize);

    /* Preserve old content (if any) */
    if (oldlen)
    {
        memcpy(newbuf, old, oldlen);
    }
    newbuf[oldlen] = '\0';

    /* Format into the tail */
    va_list ap_write;
    va_copy(ap_write, ap);
    int wrote = vsnprintf(newbuf + oldlen, (size_t)need + 1, fmt, ap_write);
    va_end(ap_write);

    if (wrote < 0)
    {
        /* Shouldn't happen in conforming C99 for supported encodings,
           but handle defensively. */
        free(newbuf);
        return -1;
    }

    /* Success: install the new buffer */
    if (old)
        free((void *)old);
    *bufp = newbuf;
    return wrote; /* number of chars appended (no NUL) */
}

/* Varargs front-end *
static inline int appendf(char **bufp, const char *fmt, ...) {
    int r;
    va_list ap;
    va_start(ap, fmt);
    r = vappendf(bufp, fmt, ap);
    va_end(ap);
    return r;
}
*/
/* Convenience API: returns new pointer (or NULL on error). Original 'buf' remains owned by caller. */
static inline char *appendf(char *buf, size_t *len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *tmp = buf;
    int r = vappendf(&tmp, fmt, ap);
    va_end(ap);
    if (r < 0)
    {
        if (len)
            *len = 0;
        return NULL;
    }
    if (len)
        *len = r;
    return tmp;
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
static void binds_add(Bindings *b, const char *name, size_t nlen, const char *repl)
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
    b->len++;
}
static void binds_free(Bindings *b)
{
    for (size_t i = 0; i < b->len; i++)
    {
        free(b->a[i].name);
        free(b->a[i].repl);
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
                if (!ts_node_is_null(left) && strcmp(ts_node_type(left), "identifier") == 0)
                {
                    size_t ns = ts_node_start_byte(left), ne = ts_node_end_byte(left);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s[%d]", base, idx);
                    binds_add(out, src + ns, ne - ns, buf);
                }
                idx++;
                last_was_comma_or_open = 0;
            }
            else
            {
                return 0; // nested not supported
            }
        }
        return 1;
    }
    else if (strcmp(pt, "object_pattern") == 0)
    {
        uint32_t c = ts_node_child_count(pattern);
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
                if (ts_node_is_null(key) || ts_node_is_null(val) || strcmp(ts_node_type(val), "identifier") != 0)
                    return 0;
                if (strcmp(ts_node_type(key), "property_identifier") == 0 ||
                    strcmp(ts_node_type(key), "identifier") == 0)
                {
                    size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
                    size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
                    size_t klen = ke - ks;
                    char *repl = NULL;
                    REMALLOC(repl, strlen(base) + 1 + klen + 1);
                    sprintf(repl, "%s.%.*s", base, (int)klen, src + ks);
                    binds_add(out, src + vs, ve - vs, repl);
                    free(repl);
                }
                else
                    return 0;
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
            }
            else
                return 0;
        }
        return 1;
    }
    return 0;
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
                    add_edit(&tmp, ns - es, ne - es, b->a[i].repl, claimed);
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

/* =======================  helpers (namespaced esm_)  ======================= */

/* tiny string builder */
typedef struct
{
    char *buf;
    size_t len, cap;
} esm_sb;
static void esm_sb_init(esm_sb *b)
{
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}
static void esm_sb_free(esm_sb *b)
{
    free(b->buf);
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}
static void esm_sb_grow(esm_sb *b, size_t need)
{
    size_t req = b->len + need + 1;
    if (req <= b->cap)
        return;
    size_t cap = b->cap ? b->cap * 2 : 1024;
    while (cap < req)
        cap <<= 1;
    b->buf = (char *)realloc(b->buf, cap);
    b->cap = cap;
}
static void esm_sb_putn(esm_sb *b, const char *s, size_t n)
{
    esm_sb_grow(b, n);
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}
static void esm_sb_puts(esm_sb *b, const char *s)
{
    esm_sb_putn(b, s, strlen(s));
}

static char *esm_dup_range(const char *s, size_t a, size_t b)
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

/* count commas for array index calculation */
static size_t esm_count_commas_between(const char *src, size_t a, size_t b)
{
    size_t c = 0;
    for (size_t i = a; i < b; i++)
    {
        if (src[i] == ',')
            c++;
    }
    return c;
}

/* append exports for a CSV of identifiers */
static void esm_append_exports_for_csv(esm_sb *out, const char *csv)
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
            esm_sb_puts(out, " exports.");
            esm_sb_putn(out, p, (size_t)(e - p));
            esm_sb_puts(out, " = ");
            esm_sb_putn(out, p, (size_t)(e - p));
            esm_sb_puts(out, ";");
        }
        p = q ? (q + 1) : NULL;
    }
}

/* collect bound identifiers from binding patterns (object/array/default/rest) */
static void esm_collect_pattern_names(TSNode node, const char *src, esm_sb *csv)
{
    const char *t = ts_node_type(node);

    if (strcmp(t, "identifier") == 0 || strcmp(t, "shorthand_property_identifier_pattern") == 0)
    {
        if (csv->len)
        {
            esm_sb_puts(csv, ",");
        }
        size_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
        esm_sb_putn(csv, src + s, e - s);
        return;
    }
    if (strcmp(t, "rest_pattern") == 0)
    {
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++)
        {
            esm_collect_pattern_names(ts_node_named_child(node, i), src, csv);
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
            esm_collect_pattern_names(left, src, csv);
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
                    esm_collect_pattern_names(val, src, csv);
                }
            }
            else
            {
                esm_collect_pattern_names(ch, src, csv);
            }
        }
        return;
    }
    if (strcmp(t, "array_pattern") == 0)
    {
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++)
        {
            esm_collect_pattern_names(ts_node_named_child(node, i), src, csv);
        }
        return;
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++)
    {
        esm_collect_pattern_names(ts_node_named_child(node, i), src, csv);
    }
}

/* for object rest: append excluded top-level key names to a CSV of quoted strings */
static void esm_append_excluded_key_node(esm_sb *excluded_csv, TSNode prop, const char *src)
{
    const char *pt = ts_node_type(prop);

    if (strcmp(pt, "pair_pattern") == 0 || strcmp(pt, "pair") == 0)
    {
        TSNode key = ts_node_child_by_field_name(prop, "key", 3);
        if (!ts_node_is_null(key))
        {
            if (excluded_csv->len)
            {
                esm_sb_puts(excluded_csv, ",");
            }
            size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
            esm_sb_puts(excluded_csv, "\"");
            esm_sb_putn(excluded_csv, src + ks, ke - ks);
            esm_sb_puts(excluded_csv, "\"");
        }
        return;
    }
    if (strcmp(pt, "shorthand_property_identifier_pattern") == 0)
    {
        if (excluded_csv->len)
        {
            esm_sb_puts(excluded_csv, ",");
        }
        size_t ks = ts_node_start_byte(prop), ke = ts_node_end_byte(prop);
        esm_sb_puts(excluded_csv, "\"");
        esm_sb_putn(excluded_csv, src + ks, ke - ks);
        esm_sb_puts(excluded_csv, "\"");
        return;
    }
    if (strcmp(pt, "object_assignment_pattern") == 0)
    {
        TSNode left = ts_node_child_by_field_name(prop, "left", 4);
        if (!ts_node_is_null(left))
        {
            if (excluded_csv->len)
            {
                esm_sb_puts(excluded_csv, ",");
            }
            size_t ks = ts_node_start_byte(left), ke = ts_node_end_byte(left);
            esm_sb_puts(excluded_csv, "\"");
            esm_sb_putn(excluded_csv, src + ks, ke - ks);
            esm_sb_puts(excluded_csv, "\"");
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
                    esm_sb post;
                    esm_sb_init(&post);
                    if (is_default_export)
                    {
                        /* default: function f(...){} ; module.exports = f; */
                        esm_sb_puts(&post, "; module.exports = ");
                        esm_sb_putn(&post, src + is, ie - is);
                        esm_sb_puts(&post, ";");
                    }
                    else
                    {
                        /* named: function f(...){} ; exports.f = f; */
                        esm_sb_puts(&post, "; exports.");
                        esm_sb_putn(&post, src + is, ie - is);
                        esm_sb_puts(&post, " = ");
                        esm_sb_putn(&post, src + is, ie - is);
                        esm_sb_puts(&post, ";");
                    }
                    add_edit(edits, decl_e, decl_e, post.buf, claimed); /* insert AFTER function */
                    esm_sb_free(&post);

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
            esm_sb out;
            esm_sb_init(&out);
            esm_sb_puts(&out, "exports.__esModule=true;exports.default = ");
            esm_sb_putn(&out, src + vs, ve - vs);
            esm_sb_puts(&out, ";");
            add_edit(edits, ns, ne, out.buf, claimed); /* replace whole export statement */
            esm_sb_free(&out);
            return 1;
        }

        /* Case C: export default <any non-identifier expression or anonymous fn/class>;
           => var __default__ = <expr>; module.exports = __default__; */
        if (!ts_node_is_null(val))
        {
            if (overlaps)
                return 1;
            size_t vs = ts_node_start_byte(val), ve = ts_node_end_byte(val);
            char *body = esm_dup_range(src, vs, ve);
            esm_sb out;
            esm_sb_init(&out);
            esm_sb_puts(&out, "var __default__ = ");
            esm_sb_puts(&out, body);
            esm_sb_puts(&out, "; exports.default = __default__;");
            add_edit(edits, ns, ne, out.buf, claimed); /* replace whole export statement */
            esm_sb_free(&out);
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
                esm_sb pre;
                esm_sb_init(&pre);
                esm_sb_puts(&pre, "exports.");
                esm_sb_putn(&pre, src + is, ie - is);
                esm_sb_puts(&pre, " = ");
                esm_sb_putn(&pre, src + is, ie - is);
                esm_sb_puts(&pre, "; ");
                add_edit(edits, decl_s, decl_s, pre.buf, claimed); // insertion
                esm_sb_free(&pre);
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
                esm_sb pre;
                esm_sb_init(&pre);
                esm_sb_puts(&pre, "exports.");
                esm_sb_putn(&pre, src + is, ie - is);
                esm_sb_puts(&pre, " = ");
                esm_sb_putn(&pre, src + is, ie - is);
                esm_sb_puts(&pre, "; ");
                add_edit(edits, decl_s, decl_s, pre.buf, claimed); // insertion
                esm_sb_free(&pre);
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
                            char *val = esm_dup_range(src, vs, ve);

                            esm_sb out;
                            esm_sb_init(&out);
                            esm_sb names_csv;
                            esm_sb_init(&names_csv);
                            esm_sb excl;
                            esm_sb_init(&excl);

                            esm_sb_puts(&out, "var __tmpD0 = ");
                            esm_sb_puts(&out, val);
                            esm_sb_puts(&out, ";");

                            uint32_t nprops = ts_node_named_child_count(nameNode);
                            for (uint32_t i = 0; i < nprops; i++)
                            {
                                TSNode prop = ts_node_named_child(nameNode, i);
                                const char *pt = ts_node_type(prop);
                                if (strcmp(pt, "rest_pattern") == 0)
                                    continue;

                                esm_append_excluded_key_node(&excl, prop, src);

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
                                        esm_sb_puts(&out, " var ");
                                        esm_sb_putn(&out, src + is, ie - is);
                                        esm_sb_puts(&out, " = __tmpD0.");
                                        esm_sb_putn(&out, src + ks, ke - ks);
                                        esm_sb_puts(&out, ";");
                                        if (names_csv.len)
                                        {
                                            esm_sb_puts(&names_csv, ",");
                                        }
                                        esm_sb_putn(&names_csv, src + is, ie - is);
                                    }
                                    else if (strcmp(vt, "object_pattern") == 0)
                                    {
                                        esm_sb inner;
                                        esm_sb_init(&inner);
                                        esm_collect_pattern_names(valpat, src, &inner);
                                        size_t ks = ts_node_start_byte(key), ke = ts_node_end_byte(key);
                                        char *keytxt = esm_dup_range(src, ks, ke);
                                        const char *p = inner.buf;
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
                                                esm_sb_puts(&out, " var ");
                                                esm_sb_putn(&out, p, (size_t)(e - p));
                                                esm_sb_puts(&out, " = __tmpD0.");
                                                esm_sb_puts(&out, keytxt);
                                                esm_sb_puts(&out, ".");
                                                esm_sb_putn(&out, p, (size_t)(e - p));
                                                esm_sb_puts(&out, ";");
                                                if (names_csv.len)
                                                {
                                                    esm_sb_puts(&names_csv, ",");
                                                }
                                                esm_sb_putn(&names_csv, p, (size_t)(e - p));
                                            }
                                            p = q ? (q + 1) : NULL;
                                        }
                                        esm_sb_free(&inner);
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
                                        esm_sb_puts(&out, " var ");
                                        esm_sb_putn(&out, src + ls, le - ls);
                                        esm_sb_puts(&out, " = (__tmpD0.");
                                        esm_sb_putn(&out, src + ks, ke - ks);
                                        esm_sb_puts(&out, " === undefined ? ");
                                        esm_sb_putn(&out, src + rs, re - rs);
                                        esm_sb_puts(&out, " : __tmpD0.");
                                        esm_sb_putn(&out, src + ks, ke - ks);
                                        esm_sb_puts(&out, ");");
                                        if (names_csv.len)
                                        {
                                            esm_sb_puts(&names_csv, ",");
                                        }
                                        esm_sb_putn(&names_csv, src + ls, le - ls);
                                    }
                                    continue;
                                }

                                if (strcmp(pt, "shorthand_property_identifier_pattern") == 0)
                                {
                                    size_t is = ts_node_start_byte(prop), ie = ts_node_end_byte(prop);
                                    esm_sb_puts(&out, " var ");
                                    esm_sb_putn(&out, src + is, ie - is);
                                    esm_sb_puts(&out, " = __tmpD0.");
                                    esm_sb_putn(&out, src + is, ie - is);
                                    esm_sb_puts(&out, ";");
                                    if (names_csv.len)
                                    {
                                        esm_sb_puts(&names_csv, ",");
                                    }
                                    esm_sb_putn(&names_csv, src + is, ie - is);
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
                                        esm_sb_puts(&out, " var ");
                                        esm_sb_putn(&out, src + is, ie - is);
                                        esm_sb_puts(&out, " = (__tmpD0.");
                                        esm_sb_putn(&out, src + is, ie - is);
                                        esm_sb_puts(&out, " === undefined ? ");
                                        esm_sb_putn(&out, src + rs, re - rs);
                                        esm_sb_puts(&out, " : __tmpD0.");
                                        esm_sb_putn(&out, src + is, ie - is);
                                        esm_sb_puts(&out, ");");
                                        if (names_csv.len)
                                        {
                                            esm_sb_puts(&names_csv, ",");
                                        }
                                        esm_sb_putn(&names_csv, src + is, ie - is);
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
                                    esm_sb_puts(&out, " var ");
                                    esm_sb_putn(&out, src + is, ie - is);
                                    esm_sb_puts(
                                        &out,
                                        " = (function(o,e){var t={},k; for(k in o){ if(Object.prototype.hasOwnProperty.call(o,k) && ");
                                    if (excl.len)
                                    {
                                        esm_sb_puts(&out, "e.indexOf(k)<0");
                                    }
                                    else
                                    {
                                        esm_sb_puts(&out, "true");
                                    }
                                    esm_sb_puts(&out, ") t[k]=o[k]; } return t; })(__tmpD0, [");
                                    esm_sb_puts(&out, excl.buf);
                                    esm_sb_puts(&out, "]);");
                                    if (names_csv.len)
                                    {
                                        esm_sb_puts(&names_csv, ",");
                                    }
                                    esm_sb_putn(&names_csv, src + is, ie - is);
                                }
                            }

                            esm_append_exports_for_csv(&out, names_csv.buf);
                            add_edit(edits, ns, ne, out.buf, claimed);
                            esm_sb_free(&out);
                            esm_sb_free(&names_csv);
                            esm_sb_free(&excl);
                            free(val);
                            return 1;
                        }

                        /* ---------- array pattern ---------- */
                        if (strcmp(nameType, "array_pattern") == 0)
                        {
                            if (overlaps)
                                return 1;
                            size_t vs = ts_node_start_byte(initNode), ve = ts_node_end_byte(initNode);
                            char *val = esm_dup_range(src, vs, ve);

                            esm_sb out;
                            esm_sb_init(&out);
                            esm_sb names_csv;
                            esm_sb_init(&names_csv);

                            esm_sb_puts(&out, "var __tmpA0 = ");
                            esm_sb_puts(&out, val);
                            esm_sb_puts(&out, ";");

                            size_t pat_start = ts_node_start_byte(nameNode);
                            size_t after_bracket = pat_start + 1;

                            uint32_t nelems = ts_node_named_child_count(nameNode);
                            for (uint32_t j = 0; j < nelems; j++)
                            {
                                TSNode el = ts_node_named_child(nameNode, j);
                                const char *et = ts_node_type(el);

                                size_t es = ts_node_start_byte(el);
                                size_t idx = esm_count_commas_between(src, after_bracket, es);

                                if (strcmp(et, "identifier") == 0)
                                {
                                    size_t is = ts_node_start_byte(el), ie = ts_node_end_byte(el);
                                    esm_sb_puts(&out, " var ");
                                    esm_sb_putn(&out, src + is, ie - is);
                                    esm_sb_puts(&out, " = __tmpA0[");
                                    char buf[32];
                                    snprintf(buf, sizeof(buf), "%zu", idx);
                                    esm_sb_puts(&out, buf);
                                    esm_sb_puts(&out, "];");
                                    if (names_csv.len)
                                    {
                                        esm_sb_puts(&names_csv, ",");
                                    }
                                    esm_sb_putn(&names_csv, src + is, ie - is);
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
                                    esm_sb_puts(&out, " var ");
                                    esm_sb_putn(&out, src + ls, le - ls);
                                    esm_sb_puts(&out, " = (__tmpA0[");
                                    char buf[32];
                                    snprintf(buf, sizeof(buf), "%zu", idx);
                                    esm_sb_puts(&out, buf);
                                    esm_sb_puts(&out, "] === undefined ? ");
                                    esm_sb_putn(&out, src + rs, re - rs);
                                    esm_sb_puts(&out, " : __tmpA0[");
                                    esm_sb_puts(&out, buf);
                                    esm_sb_puts(&out, "]);");
                                    if (names_csv.len)
                                    {
                                        esm_sb_puts(&names_csv, ",");
                                    }
                                    esm_sb_putn(&names_csv, src + ls, le - ls);
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
                                        esm_sb_puts(&out, " var ");
                                        esm_sb_putn(&out, src + is, ie - is);
                                        esm_sb_puts(&out, " = Array.prototype.slice.call(__tmpA0, ");
                                        char buf[32];
                                        snprintf(buf, sizeof(buf), "%zu", idx);
                                        esm_sb_puts(&out, buf);
                                        esm_sb_puts(&out, ");");
                                        if (names_csv.len)
                                        {
                                            esm_sb_puts(&names_csv, ",");
                                        }
                                        esm_sb_putn(&names_csv, src + is, ie - is);
                                    }
                                    continue;
                                }

                                if (strcmp(et, "object_pattern") == 0)
                                {
                                    esm_sb inner;
                                    esm_sb_init(&inner);
                                    esm_collect_pattern_names(el, src, &inner);
                                    const char *p = inner.buf;
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
                                            esm_sb_puts(&out, " var ");
                                            esm_sb_putn(&out, p, (size_t)(e - p));
                                            esm_sb_puts(&out, " = __tmpA0[");
                                            char buf[32];
                                            snprintf(buf, sizeof(buf), "%zu", idx);
                                            esm_sb_puts(&out, buf);
                                            esm_sb_puts(&out, "].");
                                            esm_sb_putn(&out, p, (size_t)(e - p));
                                            esm_sb_puts(&out, ";");
                                            if (names_csv.len)
                                            {
                                                esm_sb_puts(&names_csv, ",");
                                            }
                                            esm_sb_putn(&names_csv, p, (size_t)(e - p));
                                        }
                                        p = q ? (q + 1) : NULL;
                                    }
                                    esm_sb_free(&inner);
                                    continue;
                                }
                            }

                            esm_append_exports_for_csv(&out, names_csv.buf);
                            add_edit(edits, ns, ne, out.buf, claimed);
                            esm_sb_free(&out);
                            esm_sb_free(&names_csv);
                            free(val);
                            return 1;
                        }
                    }
                }
            }

            /* fallback: keep lexical declaration text + export collected names */
            size_t ds = ts_node_start_byte(decl), de = ts_node_end_byte(decl);
            char *decl_txt = esm_dup_range(src, ds, de);
            esm_sb names;
            esm_sb_init(&names);
            for (uint32_t i = 0; i < ndecls; i++)
            {
                TSNode vd = ts_node_named_child(decl, i);
                if (strcmp(ts_node_type(vd), "variable_declarator") != 0)
                    continue;
                TSNode nm = ts_node_child_by_field_name(vd, "name", 4);
                if (ts_node_is_null(nm))
                    continue;
                esm_collect_pattern_names(nm, src, &names);
            }
            esm_sb out;
            esm_sb_init(&out);
            esm_sb_puts(&out, decl_txt);
            esm_append_exports_for_csv(&out, names.buf);
            add_edit(edits, ns, ne, out.buf, claimed);
            esm_sb_free(&out);
            esm_sb_free(&names);
            free(decl_txt);
            return 1;
        }

        /* export variable_declaration (just in case) */
        if (strcmp(dt, "variable_declaration") == 0)
        {
            if (overlaps)
                return 1;
            size_t ds = ts_node_start_byte(decl), de = ts_node_end_byte(decl);
            char *decl_txt = esm_dup_range(src, ds, de);
            esm_sb names;
            esm_sb_init(&names);
            uint32_t n = ts_node_named_child_count(decl);
            for (uint32_t i = 0; i < n; i++)
            {
                TSNode d = ts_node_named_child(decl, i);
                if (strcmp(ts_node_type(d), "variable_declarator") != 0)
                    continue;
                TSNode nm = ts_node_child_by_field_name(d, "name", 4);
                if (ts_node_is_null(nm))
                    continue;
                esm_collect_pattern_names(nm, src, &names);
            }
            esm_sb out;
            esm_sb_init(&out);
            esm_sb_puts(&out, decl_txt);
            esm_append_exports_for_csv(&out, names.buf);
            add_edit(edits, ns, ne, out.buf, claimed);
            esm_sb_free(&out);
            esm_sb_free(&names);
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
            mod = esm_dup_range(src, ms, me);
        }

        esm_sb out;
        esm_sb_init(&out);
        char tmp[24];
        tmp[0] = '\0';

        if (mod)
        {
            snprintf(tmp, sizeof(tmp), "__tmpExp0");
            esm_sb_puts(&out, "var ");
            esm_sb_puts(&out, tmp);
            esm_sb_puts(&out, " = require(");
            esm_sb_puts(&out, mod);
            esm_sb_puts(&out, "); ");
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

            esm_sb_puts(&out, "exports.");
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                esm_sb_putn(&out, src + as, ae - as); /* alias */
            }
            else
            {
                esm_sb_putn(&out, src + ls, le - ls); /* same-name export */
            }
            esm_sb_puts(&out, " = ");
            if (mod)
            {
                esm_sb_puts(&out, tmp);
                esm_sb_puts(&out, ".");
            }
            esm_sb_putn(&out, src + ls, le - ls); /* local or tmp.prop */
            esm_sb_puts(&out, ";");
        }

        add_edit(edits, ns, ne, out.buf, claimed);
        esm_sb_free(&out);
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
            char *mod = esm_dup_range(src, ms, me);
            esm_sb out;
            esm_sb_init(&out);
            esm_sb_puts(&out, "var __tmpExp = require(");
            esm_sb_puts(&out, mod);
            esm_sb_puts(&out,
                        "); for (var k in __tmpExp) { if (k === \"default\") continue; exports[k] = __tmpExp[k]; }");
            add_edit(edits, ns, ne, out.buf, claimed);
            esm_sb_free(&out);
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
            char *txt = esm_dup_range(src, s, e);
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
    char *out = NULL;
    char buf[32];

    TSNode spec = find_child_type(named_imports, "import_specifier", &pos);
    if (ts_node_is_null(spec))
        return 0;

    size_t mod_s = ts_node_start_byte(string_frag), mod_e = ts_node_end_byte(string_frag);

    /* temp module binding */
    sprintf(buf, "__tmpModImp%u", tmpn);
    out = appendf(NULL, NULL, "var %s=require(\"%.*s\");", buf, (int)(mod_e - mod_s), src + mod_s);

    /* each specifier: var <aliasOrName> = tmp.<name>; */
    while (!ts_node_is_null(spec))
    {
        TSNode local = ts_node_child_by_field_name(spec, "name", 4);
        TSNode alias = ts_node_child_by_field_name(spec, "alias", 5);

        if (ts_node_is_null(local))
        {
            /* fallback: use raw slice */
            size_t s = ts_node_start_byte(spec), e = ts_node_end_byte(spec);
            out = appendf(out, NULL, "var %.*s=%s.%.*s;", (int)(e - s), src + s, buf, (int)(e - s), src + s);
        }
        else
        {
            size_t ls = ts_node_start_byte(local), le = ts_node_end_byte(local);
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                out = appendf(out, NULL, "var %.*s=%s.%.*s;", (int)(ae - as), src + as, /* alias binding on LHS */
                              buf, (int)(le - ls), src + ls);                           /* local name on RHS from tmp */
            }
            else
            {
                out = appendf(out, NULL, "var %.*s=%s.%.*s;", (int)(le - ls), src + ls, /* same name */
                              buf, (int)(le - ls), src + ls);
            }
        }

        pos++;
        spec = find_child_type(named_imports, "import_specifier", &pos);
    }

    add_edit_take_ownership(edits, start, end, out, claimed);
    tmpn++;
    return 1;
}

static int do_namespace_import(EditList *edits, const char *src, TSNode snode, TSNode namespace_import,
                               TSNode string_frag, size_t start, size_t end, uint32_t *polysneeded, RangeList *claimed)
{
    char *out = NULL;
    TSNode id = find_child_type(namespace_import, "identifier", NULL);

    if (ts_node_is_null(id))
        return 0;

    size_t mod_name_end = ts_node_end_byte(string_frag), mod_name_start = ts_node_start_byte(string_frag),
           id_end = ts_node_end_byte(id), id_start = ts_node_start_byte(id);

    // var math = _interopRequireWildcard(require("math"));
    out = appendf(NULL, NULL, "var %.*s=_TrN_Sp._interopRequireWildcard(require(\"%.*s\"));", (id_end - id_start),
                  src + id_start, (mod_name_end - mod_name_start), src + mod_name_start);

    add_edit_take_ownership(edits, start, end, out, claimed);
    *polysneeded |= IMPORT_PF;
    return 1;
}

static int do_default_import(EditList *edits, const char *src, TSNode snode, TSNode default_ident, TSNode string_frag,
                             size_t start, size_t end, RangeList *claimed)
{
    char *out = NULL;
    size_t mod_s = ts_node_start_byte(string_frag), mod_e = ts_node_end_byte(string_frag);
    size_t id_s = ts_node_start_byte(default_ident), id_e = ts_node_end_byte(default_ident);

    /* With our export lowering, default import is the entire module.exports */
    out = appendf(NULL, NULL, "var %.*s=_TrN_Sp._interopDefault(require(\"%.*s\"));", (int)(id_e - id_s), src + id_s,
                  (int)(mod_e - mod_s), src + mod_s);

    add_edit_take_ownership(edits, start, end, out, claimed);
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
    char *out = appendf(NULL, NULL, "var %s=require(\"%.*s\");", tbuf, (int)(mod_e - mod_s), src + mod_s);

    /* bind default: var def = __tmp;  (we lower default export to module.exports) */
    out = appendf(out, NULL, "var %.*s=%s.default;", (int)(id_e - id_s), src + id_s, tbuf);

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
            out = appendf(out, NULL, "var %.*s=%s.%.*s;", (int)(e - s), src + s, tbuf, (int)(e - s), src + s);
        }
        else
        {
            size_t ls = ts_node_start_byte(local), le = ts_node_end_byte(local);
            if (!ts_node_is_null(alias))
            {
                size_t as = ts_node_start_byte(alias), ae = ts_node_end_byte(alias);
                out = appendf(out, NULL, "var %.*s=%s.%.*s;", (int)(ae - as), src + as, /* alias */
                              tbuf, (int)(le - ls), src + ls);                          /* local */
            }
            else
            {
                out = appendf(out, NULL, "var %.*s=%s.%.*s;", (int)(le - ls), src + ls, /* same name */
                              tbuf, (int)(le - ls), src + ls);
            }
        }

        pos++;
        spec = find_child_type(named_imports, "import_specifier", &pos);
    }

    add_edit_take_ownership(edits, start, end, out, claimed);
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

    // require(""); == 12
    char *out = NULL;
    REMALLOC(out, 13 + slen);
    snprintf(out, slen + 13, "require(\"%.*s\");", (int)slen, src + sstart);
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
        size_t _L = strlen(_s);                                                                                        \
        memcpy(out + j, _s, _L);                                                                                       \
        j += _L;                                                                                                       \
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
        }
    }

    char *rep = NULL;
    if (did && !is_block)
    {
        char *rew = rewrite_concise_body_with_bindings(src, body, &binds, claimed);
        size_t cap = 64 + strlen(temp) + strlen(rew) + 1;

        REMALLOC(rep, cap);
        snprintf(rep, cap, "function (%s) { return %s; }", temp, rew);
        free(rew);
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
        size_t decl_cap = 16;
        char *decl = NULL;
        int w;
        size_t k;

        for (size_t i = 0; i < binds.len; i++)
            decl_cap += strlen(binds.a[i].name) + strlen(binds.a[i].repl) + 4;

        REMALLOC(decl, decl_cap);

        size_t dj = 0;
        memcpy(decl + dj, "var ", 4);
        dj += 4;
        for (size_t i = 0; i < binds.len; i++)
        {
            size_t nlen = strlen(binds.a[i].name), rlen = strlen(binds.a[i].repl);
            memcpy(decl + dj, binds.a[i].name, nlen);
            dj += nlen;
            decl[dj++] = '=';
            memcpy(decl + dj, binds.a[i].repl, rlen);
            dj += rlen;
            if (i + 1 < binds.len)
            {
                decl[dj++] = ',';
                decl[dj++] = ' ';
            }
        }
        decl[dj++] = ';';
        decl[dj++] = ' ';
        decl[dj] = 0;

        size_t cap = 64 + (size_t)strlen(temp) + body_len + strlen(decl) + 1;

        REMALLOC(rep, cap);

        w = snprintf(rep, cap, "function (%s) ", temp);
        k = (size_t)w;
        memcpy(rep + k, bsrc, brace);
        k += brace;
        memcpy(rep + k, decl, strlen(decl));
        k += strlen(decl);
        memcpy(rep + k, bsrc + brace, body_len - brace);
        k += (body_len - brace);
        rep[k] = '\0';
        free(decl);
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
            // printf("%d child elem of spread\n", cnt2);
            for (j = 0; j < cnt2; j++)
            {
                TSNode gkid = ts_node_child(kid, j);
                // printf("looking at '%.*s'\n", (ts_node_end_byte(gkid) - ts_node_start_byte(gkid)),
                // src+ts_node_start_byte(gkid));
                if (strcmp(ts_node_type(gkid), "identifier") == 0)
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
                if (strcmp(ts_node_type(gkid), "identifier") == 0)
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

// ===== ES5 Unicode regex transformer (reintroduced) =====
typedef struct
{
    char *s;
    size_t len;
    size_t cap;
} SBuf;
static void sb_init(SBuf *b)
{
    b->cap = 256;
    b->len = 0;
    b->s = (char *)malloc(b->cap);
    b->s[0] = '\0';
}
static void sb_grow(SBuf *b, size_t need)
{
    if (b->len + need + 1 > b->cap)
    {
        while (b->len + need + 1 > b->cap)
            b->cap = (b->cap < 4096 ? b->cap * 2 : b->cap + need + 1024);
        b->s = (char *)realloc(b->s, b->cap);
    }
}
static void sb_putc(SBuf *b, char c)
{
    sb_grow(b, 1);
    b->s[b->len++] = c;
    b->s[b->len] = '\0';
}
static void sb_puts(SBuf *b, const char *s)
{
    size_t n = strlen(s);
    sb_grow(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}
static void sb_putsn(SBuf *b, const char *s, size_t n)
{
    sb_grow(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}
static void sb_put_u4(SBuf *b, unsigned u)
{
    char tmp[7];
    snprintf(tmp, sizeof(tmp), "\\u%04X", u & 0xFFFFu);
    sb_puts(b, tmp);
}
static void sb_free(SBuf *b)
{
    if (!b)
        return;
    if (b->s)
        free(b->s);
    b->s = NULL;
    b->len = 0;
    b->cap = 0;
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
    if (strstr(t, "function") || strcmp(t, "arrow_function") == 0 || strstr(t, "class"))
        return;
    uint32_t c = ts_node_child_count(node);
    for (uint32_t i = 0; i < c; i++)
        _collect_awaits_shallow(ts_node_child(node, i), out);
}
static void _sb_put_slice(SBuf *b, const char *src, size_t s, size_t e)
{
    if (e > s)
    {
        sb_grow(b, e - s);
        memcpy(b->s + b->len, src + s, e - s);
        b->len += e - s;
        b->s[b->len] = 0;
    }
}
static void _sb_put_slice_trim_trailing_ws(SBuf *b, const char *src, size_t s, size_t e)
{
    while (e > s && (src[e - 1] == ' ' || src[e - 1] == '\t' || src[e - 1] == '\r' || src[e - 1] == '\n'))
        e--;
    _sb_put_slice(b, src, s, e);
}

// Lower a statement containing 0..N awaits into state-machine steps
static void _emit_stmt_async_lower(SBuf *dst, const char *src, size_t ss, size_t se, TSNode stmt_node,
                                   int *p_next_label)
{
    _AsyncNodeVec av = {0};
    _collect_awaits_shallow(stmt_node, &av);
    if (av.len == 0)
    {
        _sb_put_slice(dst, src, ss, se);
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
    SBuf acc;
    sb_init(&acc);
    for (size_t k = 0; k < av.len; k++)
    {
        TSNode aw = av.a[k];
        TSNode arg = ts_node_child_by_field_name(aw, "argument", 8);
        if (ts_node_is_null(arg))
            arg = ts_node_named_child(aw, 0);
        *p_next_label += 3;
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%d", *p_next_label);
        sb_puts(dst, "_context.next = ");
        sb_puts(dst, tmp);
        sb_puts(dst, "; return (");
        _sb_put_slice(dst, src, ts_node_start_byte(arg), ts_node_end_byte(arg));
        sb_puts(dst, ");");
        sb_puts(dst, "\n  case ");
        sb_puts(dst, tmp);
        sb_puts(dst, ":");
        size_t aws = ts_node_start_byte(aw), awe = ts_node_end_byte(aw);
        _sb_put_slice(&acc, src, cursor, aws);
        sb_puts(&acc, "_context.sent");
        cursor = awe;
    }
    _sb_put_slice(&acc, src, cursor, se);
    sb_puts(dst, acc.s);
    if (acc.len == 0 || acc.s[acc.len - 1] != ';')
        sb_puts(dst, ";");
    sb_free(&acc);
}

// Build the body: return _TrN_Sp.regeneratorRuntime.wrap(function
// _callee$(_context){while(1){switch(_context.prev=_context.next){case 0: ... }} , _callee);
static char *_build_regenerator_switch_body(const char *src, TSNode body)
{
    SBuf out;
    sb_init(&out);
    int next_label = 0;
    sb_puts(
        &out,
        "return _TrN_Sp.regeneratorRuntime.wrap(function _callee$(_context){while(1){switch(_context.prev=_context.next){case 0:");
    const char *bt = ts_node_type(body);
    if (strcmp(bt, "statement_block") == 0)
    {
        uint32_t sc = ts_node_named_child_count(body);
        for (uint32_t i = 0; i < sc; i++)
        {
            TSNode stmt = ts_node_named_child(body, i);
            size_t ss = ts_node_start_byte(stmt), se = ts_node_end_byte(stmt);
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
            static int joined = 0;
            if (!has_await && next_has_await && i == 0)
            {
                sb_puts(&out, "\n  ");
                _sb_put_slice_trim_trailing_ws(&out, src, ss, se);
                joined = 1;
            }
            else if (has_await)
            {
                if (!joined)
                    sb_puts(&out, "\n  ");
                _emit_stmt_async_lower(&out, src, ss, se, stmt, &next_label);
                joined = 1;
            }
            else
            {
                sb_puts(&out, "\n  ");
                _sb_put_slice_trim_trailing_ws(&out, src, ss, se);
                joined = 0;
            }
        }
    }
    else
    {
        TSNode expr = body;
        size_t ss = ts_node_start_byte(expr), se = ts_node_end_byte(expr);
        SBuf tmp;
        sb_init(&tmp);
        _emit_stmt_async_lower(&tmp, src, ss, se, expr, &next_label);
        if (strstr(tmp.s, "_context.next") == NULL)
        {
            sb_puts(&out, "\n  return ");
            sb_puts(&out, tmp.s);
            sb_puts(&out, ";");
        }
        else
        {
            sb_puts(&out, "\n  ");
            sb_puts(&out, tmp.s);
        }
        sb_free(&tmp);
    }
    int end_label = next_label + 3;
    char etmp[24];
    snprintf(etmp, sizeof(etmp), "%d", end_label);
    sb_puts(&out, "case ");
    sb_puts(&out, etmp);
    sb_puts(&out, ":case \"end\":return _context.stop();}}}, _callee);");
    return out.s;
}
static void _append_params_sig(SBuf *out, const char *src, TSNode func_like)
{
    TSNode params = ts_node_child_by_field_name(func_like, "parameters", 10);
    if (!ts_node_is_null(params)) {
        _sb_put_slice(out, src, ts_node_start_byte(params), ts_node_end_byte(params));
        return;
    }
    TSNode param = ts_node_child_by_field_name(func_like, "parameter", 9);
    if (!ts_node_is_null(param)) {
        sb_puts(out, "(");
        _sb_put_slice(out, src, ts_node_start_byte(param), ts_node_end_byte(param));
        sb_puts(out, ")");
        return;
    }
    sb_puts(out, "()");
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
    SBuf out;
    sb_init(&out);
    sb_puts(&out, "function ");
    if (!ts_node_is_null(name))
        _sb_put_slice(&out, src, ns, ne);
    else
        sb_puts(&out, fallback);
    sb_puts(&out, "() { return _");
    if (!ts_node_is_null(name))
        _sb_put_slice(&out, src, ns, ne);
    else
        sb_puts(&out, fallback);
    sb_puts(&out, ".apply(this, arguments); };");
    sb_puts(&out, "function _");
    if (!ts_node_is_null(name))
        _sb_put_slice(&out, src, ns, ne);
    else
        sb_puts(&out, fallback);
    sb_puts(&out, "() {_");
    if (!ts_node_is_null(name))
        _sb_put_slice(&out, src, ns, ne);
    else
        sb_puts(&out, fallback);
    sb_puts(&out, " = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function _callee");
    _append_params_sig(&out, src, node);
    sb_puts(&out, " {");
    char *wrap = _build_regenerator_switch_body(src, body);
    if (!wrap)
    {
        sb_free(&out);
        return NULL;
    }
    sb_puts(&out, wrap);
    free(wrap);
    sb_puts(&out, "}));return _");
    if (!ts_node_is_null(name))
        _sb_put_slice(&out, src, ns, ne);
    else
        sb_puts(&out, fallback);
    sb_puts(&out, ".apply(this, arguments);}");
    return out.s;
}
static char *_emit_async_expr_replacement(const char *src, TSNode node)
{
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (ts_node_is_null(body))
        return NULL;
    SBuf out;
    sb_init(&out);
    sb_puts(&out, "(function(){var _ref = _TrN_Sp.asyncToGenerator(_TrN_Sp.regeneratorRuntime.mark(function _callee");
_append_params_sig(&out, src, node);
sb_puts(&out, " {");
    char *wrap = _build_regenerator_switch_body(src, body);
    if (!wrap)
    {
        sb_free(&out);
        return NULL;
    }
    sb_puts(&out, wrap);
    free(wrap);
    sb_puts(&out, "})); return function(){ return _ref.apply(this, arguments); };})()");
    return out.s;
}
static int _is_async_function_like(TSNode node)
{
    const char *t = ts_node_type(node);
    if (!(strcmp(t, "function_declaration") == 0 || strcmp(t, "function_expression") == 0 ||
          strcmp(t, "function") == 0 || strcmp(t, "arrow_function") == 0))
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
    else
        rep = _emit_async_expr_replacement(src, node);
    if (!rep)
        return 0;

    if (rl_overlaps(claimed, ns, ne, "rewrite_async_await_to_regenerator"))
        return 0;

    // preserve line count
    size_t orig_nl = 0, rep_nl = 0;
    for (size_t i = ns; i < ne; i++)
        if (src[i] == '\n')
            orig_nl++;
    for (char *p = rep; *p; ++p)
        if (*p == '\n')
            rep_nl++;
    if (rep_nl < orig_nl)
    {
        size_t add = orig_nl - rep_nl;
        size_t L = strlen(rep);
        char *tmp = (char *)realloc(rep, L + add + 1);
        if (tmp)
        {
            rep = tmp;
            memset(rep + L, '\n', add);
            rep[L + add] = 0;
        }
    }
    add_edit_take_ownership(edits, ns, ne, rep, claimed);
    return 1;
}
// === End async/await pass ===

// let/const -> var (token edit)
static void collect_ids_from_pattern(const char *src, TSNode name_node, SBuf *params, SBuf *args)
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
                sb_puts(params, ",");
                sb_puts(args, ",");
            }
            sb_putsn(params, src + ns, ne - ns);
            sb_putsn(args, src + ns, ne - ns);
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
                                       int overlaps)
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
            ret = 1;
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
            // Only when this decl is the initializer
            TSNode init = ts_node_child_by_field_name(parent, "initializer", 11);
            if (!ts_node_is_null(init) && ts_node_start_byte(init) == ts_node_start_byte(lexical_decl))
            {
                // Collect identifiers from all declarators (destructuring supported)
                SBuf params;
                sb_init(&params);
                SBuf args;
                sb_init(&args);
                uint32_t nchild = ts_node_child_count(lexical_decl);
                for (uint32_t k = 0; k < nchild; k++)
                {
                    TSNode dec = ts_node_child(lexical_decl, k);
                    if (strcmp(ts_node_type(dec), "variable_declarator") == 0)
                    {
                        TSNode name = ts_node_child_by_field_name(dec, "name", 4);
                        if (!ts_node_is_null(name))
                            collect_ids_from_pattern(src, name, &params, &args);
                    }
                }

                if (params.len > 0)
                {
                    TSNode body = ts_node_child_by_field_name(parent, "body", 4);
                    if (!ts_node_is_null(body))
                    {
                        size_t bs = ts_node_start_byte(body);
                        size_t be = ts_node_end_byte(body);
                        int is_block = (strcmp(ts_node_type(body), "statement_block") == 0);

                        SBuf pref;
                        sb_init(&pref);
                        SBuf suff;
                        sb_init(&suff);
                        sb_puts(&pref, "(function(");
                        sb_putsn(&pref, params.s ? params.s : "", params.len);
                        sb_puts(&pref, "){ ");
                        sb_puts(&suff, " })( ");
                        sb_putsn(&suff, args.s ? args.s : "", args.len);
                        sb_puts(&suff, " );");

                        if (is_block)
                        {
                            add_edit(edits, bs + 1, bs + 1, pref.s, claimed);
                            add_edit(edits, be - 1, be - 1, suff.s, claimed);
                        }
                        else
                        {
                            add_edit(edits, bs, bs, pref.s, claimed);
                            add_edit(edits, be, be, suff.s, claimed);
                        }
                        sb_free(&pref);
                        sb_free(&suff);
                    }
                }

                sb_free(&params);
                sb_free(&args);
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
                    SBuf pref;
                    sb_init(&pref);
                    SBuf suff;
                    sb_init(&suff);
                    sb_puts(&pref, "(function(){ ");
                    sb_puts(&suff, " }());");
                    add_edit(edits, bs + 1, bs + 1, pref.s, claimed);
                    add_edit(edits, be - 1, be - 1, suff.s, claimed);
                    sb_free(&pref);
                    sb_free(&suff);
                }
            }

            // Top-level program: wrap the whole file if this decl is top-level and not already wrapped.
            if (strcmp(ptype, "program") == 0)
            {
                TSNode prog = parent;
                size_t ps = ts_node_start_byte(prog);
                size_t pe = ts_node_end_byte(prog);
                // Heuristic: only if file doesn't already start with "(function(" or "(function(){"
                int already = 0;
                size_t head = (pe - ps) > 32 ? 32 : (pe - ps);
                if (head > 0)
                {
                    if (memcmp(src + ps, "(function(", 10) == 0)
                        already = 1;
                    else if (memmem(src + ps, head, "(function(){", 12))
                        already = 1;
                }
                if (!already)
                {
                    add_edit(edits, ps, ps, "(function(){\n", claimed);
                    add_edit(edits, pe, pe, "\n}());", claimed);
                }
            }
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

    if (strcmp(lt, "lexical_declaration") == 0)
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

    // Only implement array_pattern for now (object supported elsewhere in transpiler)
    const char *pt = ts_node_type(pattern);
    if (strcmp(pt, "array_pattern") != 0)
        return 0;

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

    SBuf out;
    sb_init(&out);

    // _loop declaration
    sb_puts(&out, "var _loop = function _loop() { var _pairs$_i = _TrN_Sp.slicedToArray(_pairs[_i], ");
    char numbuf[32];
    snprintf(numbuf, sizeof(numbuf), "%d", N);
    sb_puts(&out, numbuf);
    sb_puts(&out, "), ");

    // bindings: a = _pairs$_i[0], b = _pairs$_i[1];
    for (size_t k = 0; k < alen; k++)
    {
        if (k)
            sb_puts(&out, ",");
        sb_puts(&out, arr[k].name);
        sb_puts(&out, " = _pairs$_i[");
        char ibuf[32];
        snprintf(ibuf, sizeof(ibuf), "%d", arr[k].index);
        sb_puts(&out, ibuf);
        sb_puts(&out, "]");
    }
    sb_puts(&out, "; ");

    // body content
    if (is_block)
    {
        // insert inner of block
        sb_putsn(&out, src + bs + 1, (be - 1) - (bs + 1));
        sb_puts(&out, " }; ");
    }
    else
    {
        sb_putsn(&out, src + bs, be - bs);
        sb_puts(&out, "; }; ");
    }

    // for loop header using array length
    sb_puts(&out, "for (var _i = 0, _pairs = ");
    sb_putsn(&out, src + rs, re - rs);
    sb_puts(&out, "; _i < _pairs.length; _i++) { _loop(); }");

    // Replace entire for-of statement
    add_edit_take_ownership(edits, fs, fe, out.s, claimed);

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

static const char *BIG_DOT =
    "(?:[\\0-\\t\\x0B\\f\\x0E-\\u2027\\u202A-\\uD7FF\\uE000-\\uFFFF]|[\\uD800-\\uDBFF][\\uDC00-\\uDFFF]|[\\uD800-\\uDBFF](?![\\uDC00-\\uDFFF])|(?:[^\\uD800-\\uDBFF]|^)[\\uDC00-\\uDFFF])";

static void append_astral_range(SBuf *out, unsigned long a, unsigned long z)
{
    unsigned ha, la, hz, lz;
    cp_to_surrogates_ul(a, &ha, &la);
    cp_to_surrogates_ul(z, &hz, &lz);

    if (ha == hz)
    {
        sb_puts(out, "(?:");
        sb_put_u4(out, ha);
        sb_putc(out, '[');
        sb_put_u4(out, la);
        sb_putc(out, '-');
        sb_put_u4(out, lz);
        sb_putc(out, ']');
        sb_putc(out, ')');
        return;
    }

    /* first block: ha [la-DFFF] */
    sb_puts(out, "(?:");
    sb_put_u4(out, ha);
    sb_putc(out, '[');
    sb_put_u4(out, la);
    sb_putc(out, '-');
    sb_put_u4(out, 0xDFFFu);
    sb_putc(out, ']');
    sb_putc(out, ')');

    /* middle blocks: [ha+1 - hz-1][DC00-DFFF] */
    if (ha + 1 <= hz - 1)
    {
        sb_putc(out, '|');
        sb_puts(out, "(?:");
        sb_putc(out, '[');
        sb_put_u4(out, ha + 1);
        sb_putc(out, '-');
        sb_put_u4(out, hz - 1);
        sb_putc(out, ']');
        sb_putc(out, '[');
        sb_put_u4(out, 0xDC00u);
        sb_putc(out, '-');
        sb_put_u4(out, 0xDFFFu);
        sb_putc(out, ']');
        sb_putc(out, ')');
    }

    /* last block: hz [DC00-lz] */
    sb_putc(out, '|');
    sb_puts(out, "(?:");
    sb_put_u4(out, hz);
    sb_putc(out, '[');
    sb_put_u4(out, 0xDC00u);
    sb_putc(out, '-');
    sb_put_u4(out, lz);
    sb_putc(out, ']');
    sb_putc(out, ')');
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

    SBuf bmp;
    sb_init(&bmp);
    SBuf astral;
    sb_init(&astral);

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
                    sb_put_u4(&bmp, (unsigned)pend_cp);                                                                \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    if (astral.len)                                                                                    \
                        sb_putc(&astral, '|');                                                                         \
                    append_astral_range(&astral, pend_cp, pend_cp);                                                    \
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
                    sb_putc(&bmp, '-');
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
                    sb_put_u4(&bmp, (unsigned)a);
                    sb_putc(&bmp, '-');
                    sb_put_u4(&bmp, (unsigned)b);
                }
                else
                {
                    if (a < 0x10000UL)
                        a = 0x10000UL;
                    if (astral.len)
                        sb_putc(&astral, '|');
                    append_astral_range(&astral, a, b);
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
                        sb_putc(&bmp, '-');
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
                            sb_put_u4(&bmp, (unsigned)a);
                            sb_putc(&bmp, '-');
                            sb_put_u4(&bmp, (unsigned)b);
                        }
                        else
                        {
                            if (a < 0x10000UL)
                                a = 0x10000UL;
                            if (astral.len)
                                sb_putc(&astral, '|');
                            append_astral_range(&astral, a, b);
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
                sb_putc(&bmp, '\\');
                sb_putc(&bmp, s[savep]);
                p = savep + 1;
                esc = 0;
                continue;
            }
        }
    }

    EMIT_PENDING();
    if (have_dash)
    {
        sb_putc(&bmp, '-');
        have_dash = 0;
    }

    SBuf out;
    sb_init(&out);
    if (!neg)
    {
        if (astral.len == 0)
        {
            sb_putc(&out, '[');
            sb_putsn(&out, bmp.s, bmp.len);
            sb_putc(&out, ']');
        }
        else
        {
            sb_puts(&out, "(?:");
            if (bmp.len)
            {
                sb_putc(&out, '[');
                sb_putsn(&out, bmp.s, bmp.len);
                sb_putc(&out, ']');
                sb_putc(&out, '|');
            }
            sb_putsn(&out, astral.s, astral.len);
            sb_putc(&out, ')');
        }
    }
    else
    {
        sb_puts(&out, "(?:");
        sb_puts(&out, "[^\\uD800-\\uDFFF");
        if (bmp.len)
            sb_putsn(&out, bmp.s, bmp.len);
        sb_putc(&out, ']');
        sb_putc(&out, '|');
        if (astral.len)
        {
            sb_puts(&out, "(?!");
            sb_putsn(&out, astral.s, astral.len);
            sb_putc(&out, ')');
        }
        sb_puts(&out, "(?:[\\uD800-\\uDBFF][\\uDC00-\\uDFFF])");
        sb_putc(&out, ')');
    }
    free(bmp.s);
    free(astral.s);
    *i = p;
    return out.s;
}

static char *regex_u_to_es5_pattern(const char *in, size_t len)
{
    SBuf out;
    sb_init(&out);
    int esc = 0;
    for (size_t i = 0; i < len;)
    {
        char c = in[i];
        if (!esc)
        {
            if (c == '\\')
            {
                esc = 1;
                sb_putc(&out, '\\');
                i++;
                continue;
            }
            if (c == '[')
            {
                char *cls = rewrite_class_es5(in, len, &i);
                if (!cls)
                {
                    free(out.s);
                    return NULL;
                }
                sb_puts(&out, cls);
                free(cls);
                continue;
            }
            if (c == '.')
            {
                sb_puts(&out, BIG_DOT);
                i++;
                continue;
            }
            sb_putc(&out, c);
            i++;
            continue;
        }
        else
        {
            size_t j = i + 1;
            if (in[i] == 'u' && j < len && in[j] == '{')
            {
                out.len--;
                out.s[out.len] = '\0';
                unsigned long cp = 0;
                int hv;
                j++;
                while (j < len && in[j] != '}')
                {
                    hv = hexv((unsigned char)in[j++]);
                    if (hv < 0)
                    {
                        free(out.s);
                        return NULL;
                    }
                    cp = (cp << 4) + hv;
                    if (cp > 0x10FFFFUL)
                    {
                        free(out.s);
                        return NULL;
                    }
                }
                if (j >= len || in[j] != '}')
                {
                    free(out.s);
                    return NULL;
                }
                j++;
                if (cp <= 0xFFFFUL)
                {
                    sb_put_u4(&out, (unsigned)cp);
                }
                else
                {
                    unsigned hi, lo;
                    cp_to_surrogates_ul(cp, &hi, &lo);
                    sb_put_u4(&out, hi);
                    sb_put_u4(&out, lo);
                }
                i = j;
                esc = 0;
                continue;
            }
            else
            {
                sb_putc(&out, in[i]);
                i++;
                esc = 0;
                continue;
            }
        }
    }
    return out.s;
}

/* === ES6 class -> ES5 function/prototype rewrite (minimal) ===
   Handles:
     class C { constructor(...) { ... } m(...) { ... } static s(...) { ... } }
     class C extends B { ... }  with super(...) in constructor
   Limitations:
     - No private fields, no decorators, no computed property names, no getters/setters, no async/generator methods.
     - `super.foo(...)` inside methods is not handled (only bare `super(...)` in constructor).
*/

/* Helper: emit ES5 constructor + proto + methods into SBuf out.
   mode: 0=declaration (function C... + stmts), 1=expression (IIFE that returns C)
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
static void es5_emit_class_core(SBuf *out, const char *src, const char *cname, size_t cname_len, int has_super,
                                size_t sups, size_t supe, TSNode body)
{
    /* ——— gather constructor and methods ——— */
    int ctor_found = 0;
    TSNode ctor_params = {{0}};
    TSNode ctor_body = {{0}};
    uint32_t n = ts_node_child_count(body);

    // Buckets for methods
    SBuf proto_arr;
    sb_init(&proto_arr); // will hold " {key:'x',value:function x(){...}},..."
    SBuf static_arr;
    sb_init(&static_arr); // same for statics

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
        // detect "static" modifier (tree-sitter exposes a named child "static" token)
        for (uint32_t j = 0, cn = ts_node_child_count(mth); j < cn; j++)
        {
            TSNode ch = ts_node_child(mth, j);
            if (ts_node_is_named(ch))
                continue;
            size_t ss = ts_node_start_byte(ch), se = ts_node_end_byte(ch);
            if (se > ss && strncmp(src + ss, "static", 6) == 0)
            {
                is_static = 1;
                break;
            }
        }

        if (is_ctor)
        {
            ctor_found = 1;
            ctor_params = params;
            ctor_body = mb;
            continue;
        }

        // Only simple identifiers for key (matches your example)
        if (ts_node_is_null(nname) || strcmp(ts_node_type(nname), "property_identifier") != 0)
            continue;

        size_t ks = ts_node_start_byte(nname), ke = ts_node_end_byte(nname);
        size_t ps = ts_node_is_null(params) ? 0 : ts_node_start_byte(params);
        size_t pe = ts_node_is_null(params) ? 0 : ts_node_end_byte(params);
        size_t bs = ts_node_is_null(mb) ? 0 : ts_node_start_byte(mb);
        size_t be = ts_node_is_null(mb) ? 0 : ts_node_end_byte(mb);

        SBuf *bucket = is_static ? &static_arr : &proto_arr;
        // comma if needed
        if (bucket->len)
            sb_puts(bucket, ",");
        // {key:'name',value:function name(){...}}
        sb_puts(bucket, "{key:'");
        sb_putsn(bucket, src + ks, ke - ks);
        sb_puts(bucket, "',value:function ");
        sb_putsn(bucket, src + ks, ke - ks);
        if (ps && pe)
            sb_putsn(bucket, src + ps, pe - ps);
        else
            sb_puts(bucket, "()");
        sb_puts(bucket, " ");
        if (bs && be)
            sb_putsn(bucket, src + bs, be - bs);
        else
            sb_puts(bucket, "{}");
        sb_puts(bucket, "}");
    }

    /* ——— open wrapper ——— */
    if (!has_super)
    {
        sb_puts(out, "var ");
        sb_putsn(out, cname, cname_len);
        sb_puts(out, " = (function() {");
    }
    else
    {
        sb_puts(out, "var ");
        sb_putsn(out, cname, cname_len);
        sb_puts(out, " = (function(_Super) {_TrN_Sp.inherits(");
        sb_putsn(out, cname, cname_len);
        sb_puts(out, ", _Super);var _super = _TrN_Sp.createSuper(");
        sb_putsn(out, cname, cname_len);
        sb_puts(out, ");");
    }

    /* ——— constructor ——— */
    sb_puts(out, "  function ");
    sb_putsn(out, cname, cname_len);

    if (ctor_found && !ts_node_is_null(ctor_params))
    {
        size_t ps = ts_node_start_byte(ctor_params), pe = ts_node_end_byte(ctor_params);
        sb_putsn(out, src + ps, pe - ps);
    }
    else
    {
        sb_puts(out, "()"); // synthesize if missing
    }
    sb_puts(out, " {");

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
                sb_puts(out, "var _this;_TrN_Sp.classCallCheck(this, ");
                sb_putsn(out, cname, cname_len);
                sb_puts(out, ");");

                /* _this = _super.call(this, <args>); */
                sb_puts(out, "_this = _super.call(this, ");
                if (call_rp > args_s)
                    sb_putsn(out, b + args_s, call_rp - args_s);
                sb_puts(out, ");");

                /* Copy remainder of ctor body after the super(...) statement’s semicolon */
                size_t after = call_rp + 1; /* position after ')' */
                if (after < blen && b[after] == ';')
                    after++; /* swallow trailing ';' if any */
                if (after < blen)
                    sb_putsn(out, b + after, blen - after);

                /* Ensure the constructor returns _this */
                sb_puts(out, "return _this;");
            }
            else
            {
            NO_SUPER_REWRITE:
                sb_puts(out, "_TrN_Sp.classCallCheck(this, ");
                sb_putsn(out, cname, cname_len);
                sb_puts(out, ");");
                if (blen)
                    sb_putsn(out, b, blen);
            }
        }
        else
        {
            sb_puts(out, "var _this;_TrN_Sp.classCallCheck(this, ");
            sb_putsn(out, cname, cname_len);
            sb_puts(out, ");return _this;");
        }
    }
    else
    {
        // no extends
        sb_puts(out, "_TrN_Sp.classCallCheck(this, ");
        sb_putsn(out, cname, cname_len);
        sb_puts(out, ");");
        if (ctor_found && !ts_node_is_null(ctor_body))
        {
            size_t bs = ts_node_start_byte(ctor_body), be = ts_node_end_byte(ctor_body);
            sb_putsn(out, src + bs, be - bs);
        }
    }
    sb_puts(out, "};");

    /* ——— _TrN_Sp.createClass(Name, [proto], [static]) ——— */
    sb_puts(out, "_TrN_Sp.createClass(");
    sb_putsn(out, cname, cname_len);
    sb_puts(out, ",[");
    sb_puts(out, proto_arr.s);
    sb_puts(out, "],[");
    sb_puts(out, static_arr.s);
    sb_puts(out, "]);");

    /* ——— return + close wrapper ——— */
    sb_puts(out, "return ");
    sb_putsn(out, cname, cname_len);
    sb_puts(out, ";");

    if (!has_super)
    {
        sb_puts(out, "})();");
    }
    else
    {
        sb_puts(out, "})(");
        sb_putsn(out, src + sups, supe - sups);
        sb_puts(out, ");");
    }

    sb_free(&proto_arr);
    sb_free(&static_arr);
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

    SBuf out;
    sb_init(&out);
    es5_emit_class_core(&out, src, nameptr, namelen, has_super, sups, supe, body);
    char *rep = out.s;
    add_edit_take_ownership(edits, cs, ce, rep, claimed);
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

    SBuf out;
    sb_init(&out);
    // emit the same var Name = function(){...}(); but as an expression we only need the IIFE value.
    // So we generate the same code and then reference the Name immediately.
    es5_emit_class_core(&out, src, nameptr, namelen, has_super, sups, supe, body);

    // Replace with just the identifier, because the class expression should yield the constructor.
    // The var/IIFE we just emitted must be inserted *before* and we return the name here.
    // Simpler approach: replace the class expression node with (function(){...return Name;}())
    // However, to keep parity with your target, we reuse the emitted var but wrap as expression:
    // For safety in expressions, emit directly as an IIFE returning the constructor:
    //   (function(){ ...; return Name; }())
    // If you prefer the "var" form only for declarations, keep the simple IIFE here:
    // We'll do that:
    SBuf expr;
    sb_init(&expr);
    sb_puts(&expr, "(function(){");
    sb_puts(&expr, out.s); // defines var Name = function(){...}();
    sb_puts(&expr, "return ");
    sb_putsn(&expr, nameptr, namelen);
    sb_puts(&expr, ";}())");

    add_edit_take_ownership(edits, cs, ce, expr.s, claimed);
    sb_free(&out); // expr now owns the final buffer

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
        // for (a of X)
        name = left;
        is_decl = false;
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
    char ibuf[32], xbuf[32];
    make_fresh_forof_names(ibuf, sizeof ibuf, xbuf, sizeof xbuf);

    // Build replacement
    SBuf out;
    sb_init(&out);

    sb_puts(&out, "for (var ");
    sb_puts(&out, ibuf);
    sb_puts(&out, " = 0, ");
    sb_puts(&out, xbuf);
    sb_puts(&out, " = ");
    sb_putsn(&out, src + rs, re - rs);
    sb_puts(&out, "; ");
    sb_puts(&out, ibuf);
    sb_puts(&out, " < ");
    sb_puts(&out, xbuf);
    sb_puts(&out, ".length; ");
    sb_puts(&out, ibuf);
    sb_puts(&out, "++) {");

    if (is_decl)
    {
        sb_puts(&out, "var ");
        sb_putsn(&out, src + ns, ne - ns);
        sb_puts(&out, " = ");
        sb_puts(&out, xbuf);
        sb_puts(&out, "[");
        sb_puts(&out, ibuf);
        sb_puts(&out, "]; ");
    }
    else
    {
        sb_putsn(&out, src + ns, ne - ns);
        sb_puts(&out, " = ");
        sb_puts(&out, xbuf);
        sb_puts(&out, "[");
        sb_puts(&out, ibuf);
        sb_puts(&out, "]; ");
    }

    // splice body
    if (is_block)
    {
        // copy inner of the block (without braces)
        sb_putsn(&out, src + bs + 1, (be - 1) - (bs + 1));
    }
    else
    {
        sb_putsn(&out, src + bs, be - bs);
    }
    sb_puts(&out, "}");

    // Replace the whole for-of node
    size_t fs = ts_node_start_byte(forof);
    size_t fe = ts_node_end_byte(forof);
    add_edit_take_ownership(edits, fs, fe, out.s, claimed);
    out.s = NULL;
    sb_free(&out);
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

RP_ParseRes transpiler_rewrite_pass(EditList *edits, const char *src, size_t src_len, TSNode root,
                                    uint32_t *polysneeded, int *unresolved)
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
                         strcmp(nt, "function_expression") == 0 || strcmp(nt, "arrow_function") == 0))
        {
            handled = rewrite_async_await_to_regenerator(edits, src, n, &claimed, overlaps);
            if (handled)
                *polysneeded |= ASYNC_PF;
        }

        if (!handled && strcmp(nt, "arrow_function") == 0)
        {
            handled = rewrite_arrow_function_node(edits, src, n, &claimed, overlaps);
        }
        if (!handled && strcmp(nt, "variable_declaration") == 0)
        {
            handled = rewrite_var_function_expression_defaults(edits, src, n, &claimed, overlaps);
        }
        if (!handled && (strcmp(nt, "function_declaration") == 0 || strcmp(nt, "function") == 0 ||
                         strcmp(nt, "function_expression") == 0 || strcmp(nt, "generator_function_declaration") == 0 ||
                         strcmp(nt, "generator_function") == 0 || strcmp(nt, "generator_function_expression") == 0))
        {
            handled = rewrite_function_like_default_params(edits, src, n, &claimed, overlaps);
            if (!handled)
                handled = rewrite_function_rest(edits, src, n, &claimed, overlaps);
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
            handled = rewrite_lexical_declaration(edits, src, n, &claimed, overlaps);
        }

        if (!handled && strcmp(nt, "array") == 0)
        {
            handled = rewrite_array_spread(edits, src, n, 0, &claimed, polysneeded, overlaps);
        }
        if (!handled && strcmp(nt, "object") == 0)
        {
            handled = rewrite_array_spread(edits, src, n, 1, &claimed, polysneeded, overlaps);
        }

        /* just need the polyfill if we see this */
        if (strcmp(nt, "identifier") == 0)
        {
            size_t start = ts_node_start_byte(n), end = ts_node_end_byte(n);
            if (strncmp("Promise", src + start, end - start) == 0)
                *polysneeded |= PROMISE_PF;
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

static RP_ParseRes transpile_code(const char *src, size_t src_len, int printTree, int track_polys)
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

        res = transpiler_rewrite_pass(&edits, src, src_len, root, &polysneeded, &unresolved);
        res.errmsg = NULL;

        if (edits.len || polysneeded)
        {
            uint32_t polysneed_not_added = polysneeded & ~polysdone;

            res.transpiled = apply_edits(src, src_len, &edits, polysneed_not_added);
            polysdone |= polysneeded;

            res.altered = 1;

            if (res.err)
            {
                const char *p = src + res.pos, *s = p, *e = p, *fe = src + src_len, *ple = NULL, *pls = NULL;

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
                    res.errmsg = appendf(NULL, NULL, "%.*s%s\n", (int)(ple - pls), pls, bline);
                }
                s++;
                while (e <= fe && *e != '\n')
                    e++;
                res.errmsg = appendf(res.errmsg, NULL, "%.*s\n", (int)(e - s), s);
                res.errmsg = appendf(res.errmsg, NULL, "%*s", 1 + (p - s), "^");
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
    return transpile_code(src, src_len, printTree, 1);
}

RP_ParseRes transpile_standalone(const char *src, size_t src_len, int printTree)
{
    return transpile_code(src, src_len, printTree, 0);
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
        fprintf(stderr, "Usage: %s <path-to-js> [--print-ast]\n       Use '-' to read from stdin.\n", argv[0]);
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
