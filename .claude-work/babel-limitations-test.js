#!/usr/bin/env rampart
/* "use babel" must be on the first line after comments and any optional #! above */
"use babel:{ presets: ['env'],retainLines:true }"

/* Test the known tree-sitter transpiler limitations against Babel
   to see if Babel handles them better. */

rampart.globalize(rampart.utils);

var _asyncQueue = [];
var _asyncRunning = false;
var _drainAsync = function() {
    if (_asyncRunning || _asyncQueue.length === 0) return;
    _asyncRunning = true;
    var item = _asyncQueue.shift();
    item.promise.then(function(result) {
        printf("testing babel-limit - %-48s - ", item.name);
        if (result)
            printf("passed\n");
        else
        {
            printf(">>>>> FAILED <<<<<\n");
        }
        _asyncRunning = false;
        _drainAsync();
    }).then(null, function(e) {
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
        } catch(e) {
            error = e;
            test = false;
        }
    }
    if (test && typeof test === 'object' && typeof test.then === 'function') {
        _asyncQueue.push({name: name, promise: test});
        _drainAsync();
        return;
    }
    printf("testing babel-limit - %-48s - ", name);
    if (test)
        printf("passed\n");
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if (error) console.log(error);
    }
}

/* ===================================================================
   1. const is not read-only (tree-sitter converts const -> var)
   =================================================================== */

testFeature("const is read-only", function() {
    const x = 42;
    try {
        // In native ES2015+, this should throw TypeError.
        // If const -> var, it silently succeeds.
        eval('x = 99');
        return false; // should not reach here
    } catch(e) {
        return x === 42;
    }
});

/* ===================================================================
   2. let/const at top level do not attach to global
   =================================================================== */

testFeature("top-level let not on global", function() {
    let unique = "sentinel_" + Math.random().toString(36).slice(2);
    (0, eval)('let _BblTmpVar = "' + unique + '";');
    var onGlobal = Object.prototype.hasOwnProperty.call(global, "_BblTmpVar");
    return onGlobal === false;
});

/* ===================================================================
   3. Destructuring with await
   =================================================================== */

testFeature("destructuring + await", function() {
    async function fetchPair() {
        const {a, b} = await Promise.resolve({a: 1, b: 2});
        return a + b;
    }
    return fetchPair().then(function(v) { return v === 3; });
});

/* ===================================================================
   4. await inside loops
   =================================================================== */

testFeature("await inside for loop", function() {
    async function loopAwait() {
        var sum = 0;
        for (var i = 1; i <= 3; i++) {
            sum += await Promise.resolve(i);
        }
        return sum;
    }
    return loopAwait().then(function(v) { return v === 6; });
});

testFeature("await inside while loop", function() {
    async function whileAwait() {
        var results = [];
        var i = 0;
        while (i < 3) {
            results.push(await Promise.resolve(i));
            i++;
        }
        return results;
    }
    return whileAwait().then(function(v) {
        return JSON.stringify(v) === "[0,1,2]";
    });
});

/* ===================================================================
   5. yield inside loops
   =================================================================== */

testFeature("yield inside for loop", function() {
    function* range(start, end) {
        for (let i = start; i < end; i++) {
            yield i;
        }
    }
    var results = [];
    var gen = range(0, 5);
    var step;
    while (!(step = gen.next()).done) {
        results.push(step.value);
    }
    return JSON.stringify(results) === "[0,1,2,3,4]";
});

testFeature("yield inside while loop", function() {
    function* countdown(n) {
        while (n > 0) {
            yield n;
            n--;
        }
    }
    var results = [];
    for (var v of countdown(3)) {
        results.push(v);
    }
    return JSON.stringify(results) === "[3,2,1]";
});

/* ===================================================================
   6. BigInt literals and operators
   =================================================================== */

testFeature("BigInt literals", function() {
    try {
        var x = eval('123n');
        return typeof x === 'bigint';
    } catch(e) {
        return false;
    }
});

/* ===================================================================
   7. Intl (Internationalization API)
   =================================================================== */

testFeature("Intl.Collator", function() {
    try {
        var collator = new Intl.Collator("de");
        return typeof collator.compare === "function";
    } catch(e) {
        return false;
    }
});

/* ===================================================================
   Summary
   =================================================================== */

setTimeout(function() {
    printf("\nBabel limitation test complete.\n");
    printf("Tests that pass here but fail in tree-sitter transpiler\n");
    printf("indicate areas where Babel provides better support.\n");
}, 200);
