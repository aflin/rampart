"use strict";function _createForOfIteratorHelper(o, allowArrayLike) {var it;if (typeof Symbol === "undefined" || o[Symbol.iterator] == null) {if (Array.isArray(o) || (it = _unsupportedIterableToArray(o)) || allowArrayLike && o && typeof o.length === "number") {if (it) o = it;var i = 0;var F = function F() {};return { s: F, n: function n() {if (i >= o.length) return { done: true };return { done: false, value: o[i++] };}, e: function e(_e) {throw _e;}, f: F };}throw new TypeError("Invalid attempt to iterate non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.");}var normalCompletion = true,didErr = false,err;return { s: function s() {it = o[Symbol.iterator]();}, n: function n() {var step = it.next();normalCompletion = step.done;return step;}, e: function e(_e2) {didErr = true;err = _e2;}, f: function f() {try {if (!normalCompletion && it["return"] != null) it["return"]();} finally {if (didErr) throw err;}} };}function _unsupportedIterableToArray(o, minLen) {if (!o) return;if (typeof o === "string") return _arrayLikeToArray(o, minLen);var n = Object.prototype.toString.call(o).slice(8, -1);if (n === "Object" && o.constructor) n = o.constructor.name;if (n === "Map" || n === "Set") return Array.from(o);if (n === "Arguments" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n)) return _arrayLikeToArray(o, minLen);}function _arrayLikeToArray(arr, len) {if (len == null || len > arr.length) len = arr.length;for (var i = 0, arr2 = new Array(len); i < len; i++) {arr2[i] = arr[i];}return arr2;}function asyncGeneratorStep(gen, resolve, reject, _next, _throw, key, arg) {try {var info = gen[key](arg);var value = info.value;} catch (error) {reject(error);return;}if (info.done) {resolve(value);} else {Promise.resolve(value).then(_next, _throw);}}function _asyncToGenerator(fn) {return function () {var self = this,args = arguments;return new Promise(function (resolve, reject) {var gen = fn.apply(self, args);function _next(value) {asyncGeneratorStep(gen, resolve, reject, _next, _throw, "next", value);}function _throw(err) {asyncGeneratorStep(gen, resolve, reject, _next, _throw, "throw", err);}_next(undefined);});};}function _typeof(obj) {"@babel/helpers - typeof";if (typeof Symbol === "function" && typeof Symbol.iterator === "symbol") {_typeof = function _typeof(obj) {return typeof obj;};} else {_typeof = function _typeof(obj) {return obj && typeof Symbol === "function" && obj.constructor === Symbol && obj !== Symbol.prototype ? "symbol" : typeof obj;};}return _typeof(obj);}
/* "use babel" must be on the first line after comments and any optional #! above */


/* Test the known tree-sitter transpiler limitations against Babel
                                                                                        to see if Babel handles them better. */

rampart.globalize(rampart.utils);

var _asyncQueue = [];
var _asyncRunning = false;
var _drainAsync = function _drainAsync() {
  if (_asyncRunning || _asyncQueue.length === 0) return;
  _asyncRunning = true;
  var item = _asyncQueue.shift();
  item.promise.then(function (result) {
    printf("testing babel-limit - %-48s - ", item.name);
    if (result)
    printf("passed\n");else

    {
      printf(">>>>> FAILED <<<<<\n");
    }
    _asyncRunning = false;
    _drainAsync();
  }).then(null, function (e) {
    printf("testing babel-limit - %-48s - ", item.name);
    printf(">>>>> FAILED <<<<<\n");
    console.log(e);
    _asyncRunning = false;
    _drainAsync();
  });
};

function testFeature(name, test)
{
  var error = false;
  if (typeof test == 'function') {
    try {
      test = test();
    } catch (e) {
      error = e;
      test = false;
    }
  }
  if (test && _typeof(test) === 'object' && typeof test.then === 'function') {
    _asyncQueue.push({ name: name, promise: test });
    _drainAsync();
    return;
  }
  printf("testing babel-limit - %-48s - ", name);
  if (test)
  printf("passed\n");else

  {
    printf(">>>>> FAILED <<<<<\n");
    if (error) console.log(error);
  }
}

/* ===================================================================
     1. const is not read-only (tree-sitter converts const -> var)
     =================================================================== */

testFeature("const is read-only", function () {
  var x = 42;
  try {
    // In native ES2015+, this should throw TypeError.
    // If const -> var, it silently succeeds.
    eval('x = 99');
    return false; // should not reach here
  } catch (e) {
    return x === 42;
  }
});

/* ===================================================================
       2. let/const at top level do not attach to global
       =================================================================== */

testFeature("top-level let not on global", function () {
  var unique = "sentinel_" + Math.random().toString(36).slice(2);
  (0, eval)('let _BblTmpVar = "' + unique + '";');
  var onGlobal = Object.prototype.hasOwnProperty.call(global, "_BblTmpVar");
  return onGlobal === false;
});

/* ===================================================================
       3. Destructuring with await
       =================================================================== */

testFeature("destructuring + await", function () {function
  fetchPair() {return _fetchPair.apply(this, arguments);}function _fetchPair() {_fetchPair = _asyncToGenerator( /*#__PURE__*/regeneratorRuntime.mark(function _callee() {var _yield$Promise$resolv, a, b;return regeneratorRuntime.wrap(function _callee$(_context) {while (1) {switch (_context.prev = _context.next) {case 0:_context.next = 2;return (
                Promise.resolve({ a: 1, b: 2 }));case 2:_yield$Promise$resolv = _context.sent;a = _yield$Promise$resolv.a;b = _yield$Promise$resolv.b;return _context.abrupt("return",
              a + b);case 6:case "end":return _context.stop();}}}, _callee);}));return _fetchPair.apply(this, arguments);}

  return fetchPair().then(function (v) {return v === 3;});
});

/* ===================================================================
       4. await inside loops
       =================================================================== */

testFeature("await inside for loop", function () {function
  loopAwait() {return _loopAwait.apply(this, arguments);}function _loopAwait() {_loopAwait = _asyncToGenerator( /*#__PURE__*/regeneratorRuntime.mark(function _callee2() {var sum, i;return regeneratorRuntime.wrap(function _callee2$(_context2) {while (1) {switch (_context2.prev = _context2.next) {case 0:
              sum = 0;
              i = 1;case 2:if (!(i <= 3)) {_context2.next = 10;break;}_context2.t0 =
              sum;_context2.next = 6;return Promise.resolve(i);case 6:sum = _context2.t0 += _context2.sent;case 7:i++;_context2.next = 2;break;case 10:return _context2.abrupt("return",

              sum);case 11:case "end":return _context2.stop();}}}, _callee2);}));return _loopAwait.apply(this, arguments);}

  return loopAwait().then(function (v) {return v === 6;});
});

testFeature("await inside while loop", function () {function
  whileAwait() {return _whileAwait.apply(this, arguments);}function _whileAwait() {_whileAwait = _asyncToGenerator( /*#__PURE__*/regeneratorRuntime.mark(function _callee3() {var results, i;return regeneratorRuntime.wrap(function _callee3$(_context3) {while (1) {switch (_context3.prev = _context3.next) {case 0:
              results = [];
              i = 0;case 2:if (!(
              i < 3)) {_context3.next = 11;break;}_context3.t0 =
              results;_context3.next = 6;return Promise.resolve(i);case 6:_context3.t1 = _context3.sent;_context3.t0.push.call(_context3.t0, _context3.t1);
              i++;_context3.next = 2;break;case 11:return _context3.abrupt("return",

              results);case 12:case "end":return _context3.stop();}}}, _callee3);}));return _whileAwait.apply(this, arguments);}

  return whileAwait().then(function (v) {
    return JSON.stringify(v) === "[0,1,2]";
  });
});

/* ===================================================================
       5. yield inside loops
       =================================================================== */

testFeature("yield inside for loop", function () {var _marked = /*#__PURE__*/regeneratorRuntime.mark(
  range);function range(start, end) {var i;return regeneratorRuntime.wrap(function range$(_context4) {while (1) {switch (_context4.prev = _context4.next) {case 0:
            i = start;case 1:if (!(i < end)) {_context4.next = 7;break;}_context4.next = 4;
            return i;case 4:i++;_context4.next = 1;break;case 7:case "end":return _context4.stop();}}}, _marked);}


  var results = [];
  var gen = range(0, 5);
  var step;
  while (!(step = gen.next()).done) {
    results.push(step.value);
  }
  return JSON.stringify(results) === "[0,1,2,3,4]";
});

testFeature("yield inside while loop", function () {var _marked2 = /*#__PURE__*/regeneratorRuntime.mark(
  countdown);function countdown(n) {return regeneratorRuntime.wrap(function countdown$(_context5) {while (1) {switch (_context5.prev = _context5.next) {case 0:if (!(
            n > 0)) {_context5.next = 6;break;}_context5.next = 3;
            return n;case 3:
            n--;_context5.next = 0;break;case 6:case "end":return _context5.stop();}}}, _marked2);}


  var results = [];var _iterator = _createForOfIteratorHelper(
  countdown(3)),_step;try {for (_iterator.s(); !(_step = _iterator.n()).done;) {var v = _step.value;
      results.push(v);
    }} catch (err) {_iterator.e(err);} finally {_iterator.f();}
  return JSON.stringify(results) === "[3,2,1]";
});

/* ===================================================================
       6. BigInt literals and operators
       =================================================================== */

testFeature("BigInt literals", function () {
  try {
    var x = eval('123n');
    return typeof x === 'bigint';
  } catch (e) {
    return false;
  }
});

/* ===================================================================
       7. Intl (Internationalization API)
       =================================================================== */

testFeature("Intl.Collator", function () {
  try {
    var collator = new Intl.Collator("de");
    return typeof collator.compare === "function";
  } catch (e) {
    return false;
  }
});

/* ===================================================================
       Summary
       =================================================================== */

setTimeout(function () {
  printf("\nBabel limitation test complete.\n");
  printf("Tests that pass here but fail in tree-sitter transpiler\n");
  printf("indicate areas where Babel provides better support.\n");
}, 200);