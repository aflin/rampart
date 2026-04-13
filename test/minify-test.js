#!/usr/bin/env rampart
"use transpilerGlobally"

rampart.globalize(rampart.utils);

var _nfailed = 0;

function testMinify(name, test)
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
    printf("testing minify  - %-50s - ", name);
    if (test)
        printf("passed\n");
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        _nfailed++;
    }
    if (error) console.log(error);
}

/* helper: minify and eval, return the result of calling fn_name() */
var _minRunCounter = 0;
function minifyAndRun(src, fn_name, args) {
    var minified = rampart.utils.minify(src);
    var tmpfile = "/tmp/_mintest_" + (_minRunCounter++) + ".js";
    var wrapper = minified + "\nmodule.exports = " + fn_name + ".apply(null, " + JSON.stringify(args || []) + ");";
    fprintf(tmpfile, "%s", wrapper);
    var result = require(tmpfile);
    try { rmFile(tmpfile); } catch(e) {}
    try { rmFile(tmpfile.replace('.js', '.transpiled.js')); } catch(e) {}
    return result;
}

/* ========================
   Basic whitespace removal
   ======================== */

testMinify("whitespace removal", function() {
    var src = "function   foo(  )  {   return   1  ;  }";
    var min = rampart.utils.minify(src);
    return min === "function foo(){return 1;}";
});

testMinify("variable declarations", function() {
    var src = "var x = 1;\nvar y = 2;\nvar z = x + y;";
    var min = rampart.utils.minify(src);
    return min === "var x=1;var y=2;var z=x+y;";
});

testMinify("comment stripping", function() {
    var src = "var x = 1; /* comment */ var y = 2; // line comment\nvar z = 3;";
    var min = rampart.utils.minify(src);
    return min.indexOf("comment") === -1 && min.indexOf("//") === -1;
});

/* ========================
   Variable mangling
   ======================== */

testMinify("mangle function params", function() {
    var src = "function foo(longParam) { return longParam + 1; }";
    var min = rampart.utils.minify(src);
    // longParam should be renamed to something short
    return min.indexOf("longParam") === -1 && min.indexOf("foo") !== -1;
});

testMinify("mangle local vars", function() {
    var src = "function foo() { var longVariable = 42; return longVariable; }";
    var min = rampart.utils.minify(src);
    return min.indexOf("longVariable") === -1;
});

testMinify("globals not renamed", function() {
    var src = "var myGlobal = 1; console.log(myGlobal);";
    var min = rampart.utils.minify(src);
    return min.indexOf("myGlobal") !== -1 && min.indexOf("console") !== -1;
});

testMinify("mangle correctness", function() {
    var src = "function test() { var longName = 42; return longName; }";
    return minifyAndRun(src, "test") === 42;
});

/* ========================
   Scope analysis: var vs let/const
   ======================== */

testMinify("var hoisting", function() {
    var src = "function test() { if (true) { var hoisted = 1; } return hoisted; }";
    return minifyAndRun(src, "test") === 1;
});

testMinify("let block scoping", function() {
    var src = "function test() { var x = 'outer'; if (true) { let x = 'inner'; } return x; }";
    return minifyAndRun(src, "test") === "outer";
});

testMinify("let shadow in block", function() {
    // Check that minified output has different mangled names for outer var and inner let
    var src = "function test() { var x = 'outer'; if (true) { let x = 'inner'; console.log(x); } return x; }";
    return minifyAndRun(src, "test") === "outer";
});

testMinify("const block scoping", function() {
    // Verify nested let scopes produce correct output
    var src = "function test() { let a = 1; { let a = 2; } return a; }";
    return minifyAndRun(src, "test") === 1;
});

testMinify("for loop var leaks", function() {
    var src = "function test() { for (var i = 0; i < 3; i++) {} return i; }";
    return minifyAndRun(src, "test") === 3;
});

testMinify("for loop let contained", function() {
    var src = "function test() { var r = []; for (let i = 0; i < 3; i++) { r.push(i); } return r.length; }";
    return minifyAndRun(src, "test") === 3;
});

/* ========================
   Closures and nested functions
   ======================== */

testMinify("closure reference", function() {
    var src = "function test() { var secret = 42; var inner = function() { return secret; }; return inner(); }";
    return minifyAndRun(src, "test") === 42;
});

testMinify("nested function scopes", function() {
    var src = "function test(x) { function inner(y) { return x + y; } return inner(10); }";
    return minifyAndRun(src, "test", [5]) === 15;
});

testMinify("variable shadowing", function() {
    var src = "function test() { var x = 1; function inner() { var x = 2; return x; } return x + inner(); }";
    return minifyAndRun(src, "test") === 3;
});

/* ========================
   Property access preserved
   ======================== */

testMinify("dot property not renamed", function() {
    var src = "function test(obj) { var val = obj.property; return val; }";
    var min = rampart.utils.minify(src);
    return min.indexOf(".property") !== -1;
});

testMinify("bracket property preserved", function() {
    var src = 'function test(obj) { var val = obj["key"]; return val; }';
    var min = rampart.utils.minify(src);
    return min.indexOf('"key"') !== -1;
});

/* ========================
   Property shorthand
   ======================== */

testMinify("object literal shorthand", function() {
    var src = "function test() { var longName = 1; var obj = { longName }; return obj.longName; }";
    return minifyAndRun(src, "test") === 1;
});

testMinify("multiple shorthand props", function() {
    var src = "function test() { var alpha = 1, beta = 2; return { alpha, beta }; }";
    var result = minifyAndRun(src, "test");
    return result.alpha === 1 && result.beta === 2;
});

/* ========================
   Destructuring
   ======================== */

testMinify("object destructuring", function() {
    var src = "function test() { var obj = {a:1, b:2}; var {a, b} = obj; return a + b; }";
    return minifyAndRun(src, "test") === 3;
});

testMinify("destructuring with rename", function() {
    var src = "function test() { var obj = {name: 'hello'}; var {name: myName} = obj; return myName; }";
    return minifyAndRun(src, "test") === "hello";
});

testMinify("destructuring with defaults", function() {
    var src = "function test(opts) { var {name = 'default', age: years = 0} = opts; return name + years; }";
    return minifyAndRun(src, "test", [{}]) === "default0" &&
           minifyAndRun(src, "test", [{name: "hi", age: 5}]) === "hi5";
});

testMinify("array destructuring", function() {
    var src = "function test() { var arr = [10, 20]; var [a, b] = arr; return a + b; }";
    return minifyAndRun(src, "test") === 30;
});

testMinify("destructuring in for-of", function() {
    var src = "function test() { var pairs = [{k:'a',v:1},{k:'b',v:2}]; var r = []; for (var {k,v} of pairs) { r.push(k+v); } return r.join(','); }";
    return minifyAndRun(src, "test") === "a1,b2";
});

/* ========================
   for-in / for-of variables
   ======================== */

testMinify("for-in var renamed", function() {
    var src = "function test(obj) { var result = []; for (var key in obj) { result.push(key); } return result; }";
    var min = rampart.utils.minify(src);
    // key should be renamed
    return min.indexOf("key") === -1 && minifyAndRun(src, "test", [{a:1, b:2}]).sort().join(",") === "a,b";
});

testMinify("for-of var renamed", function() {
    var src = "function test(arr) { var sum = 0; for (var val of arr) { sum += val; } return sum; }";
    var min = rampart.utils.minify(src);
    return min.indexOf("val") === -1 && minifyAndRun(src, "test", [[1,2,3]]) === 6;
});

/* ========================
   eval and with safety
   ======================== */

testMinify("eval prevents renaming", function() {
    var src = 'function test() { var secret = 42; return eval("secret"); }';
    var min = rampart.utils.minify(src);
    // secret must NOT be renamed
    return min.indexOf("secret") !== -1;
});

testMinify("with prevents renaming", function() {
    var src = "function test(obj) { var localVar = 1; with (obj) { return localVar; } }";
    var min = rampart.utils.minify(src);
    return min.indexOf("localVar") !== -1;
});

/* ========================
   ASI (automatic semicolon insertion)
   ======================== */

testMinify("ASI - no semicolons in source", function() {
    var src = "function test() {\n  var a = 1\n  var b = 2\n  return a + b\n}";
    return minifyAndRun(src, "test") === 3;
});

testMinify("ASI - return on own line", function() {
    var src = "function test() {\n  return\n  42\n}";
    // return + newline = ASI, so function returns undefined
    return minifyAndRun(src, "test") === undefined;
});

/* ========================
   Template literals
   ======================== */

testMinify("template literal with var", function() {
    var src = 'function test() { var greeting = "hello"; var target = "world"; return `${greeting} ${target}!`; }';
    var min = rampart.utils.minify(src);
    // vars should be renamed inside template
    return min.indexOf("greeting") === -1 && min.indexOf("target") === -1 &&
           minifyAndRun(src, "test") === "hello world!";
});

testMinify("rampart sprintf template", function() {
    var src = 'function test() { var val = 3.14159; return `${%3.2f:val}`; }';
    var min = rampart.utils.minify(src);
    return min.indexOf("val") === -1 || min.indexOf("%3.2f") !== -1;
});

testMinify("rampart quoted sprintf template", function() {
    var src = 'function test() { var html = "<b>hi</b>"; return `${"<pre>%H</pre>":html}`; }';
    var min = rampart.utils.minify(src);
    return min.indexOf("%H") !== -1;
});

testMinify("triple backtick raw string", function() {
    var src = 'var path = ```c:\\Program Files\\test```;';
    var min = rampart.utils.minify(src);
    return min.indexOf("```") !== -1 && min.indexOf("c:\\Program") !== -1;
});

/* ========================
   Getter/setter
   ======================== */

testMinify("getter and setter", function() {
    var src = 'function test() { var obj = { get name() { return this._n; }, set name(v) { this._n = v; } }; obj.name = "hello"; return obj.name; }';
    return minifyAndRun(src, "test") === "hello";
});

/* ========================
   Rest parameters
   ======================== */

testMinify("rest params", function() {
    var src = "function test(first, ...others) { return first + others.length; }";
    return minifyAndRun(src, "test", [10, 20, 30]) === 12;
});

/* ========================
   Arrow functions
   ======================== */

testMinify("arrow function", function() {
    var src = "function test() { var add = (a, b) => a + b; return add(1, 2); }";
    return minifyAndRun(src, "test") === 3;
});

testMinify("arrow function with body", function() {
    var src = "function test() { var mul = (a, b) => { return a * b; }; return mul(3, 4); }";
    return minifyAndRun(src, "test") === 12;
});

/* ========================
   Class declarations
   ======================== */

testMinify("class with methods", function() {
    var src = "function test() { class Foo { constructor(v) { this.v = v; } get() { return this.v; } } return new Foo(99).get(); }";
    return minifyAndRun(src, "test") === 99;
});

/* ========================
   Try/catch
   ======================== */

testMinify("catch param renamed", function() {
    var src = "function test() { try { throw 42; } catch(caught) { return caught; } }";
    var min = rampart.utils.minify(src);
    return min.indexOf("caught") === -1 && minifyAndRun(src, "test") === 42;
});

/* ========================
   arguments object
   ======================== */

testMinify("arguments preserved", function() {
    var src = "function test() { return arguments[0] + arguments[1]; }";
    return minifyAndRun(src, "test", [10, 20]) === 30;
});

/* ========================
   Computed property names
   ======================== */

testMinify("computed property", function() {
    var src = 'function test() { var key = "hello"; var obj = { [key]: 1 }; return obj[key]; }';
    return minifyAndRun(src, "test") === 1;
});

/* ========================
   typeof undeclared
   ======================== */

testMinify("typeof undeclared", function() {
    var src = "function test() { return typeof undeclaredVariable; }";
    return minifyAndRun(src, "test") === "undefined";
});

/* ========================
   Regex
   ======================== */

testMinify("regex preserved", function() {
    var src = 'function test() { var str = "hello world"; return str.replace(/world/, "earth"); }';
    return minifyAndRun(src, "test") === "hello earth";
});

/* ========================
   Operator spacing
   ======================== */

testMinify("plus-plus spacing", function() {
    var src = "function test(a, b) { return a + +b; }";
    return minifyAndRun(src, "test", [1, "2"]) === 3;
});

testMinify("typeof spacing", function() {
    var src = 'function test(a) { return typeof a; }';
    return minifyAndRun(src, "test", ["hello"]) === "string";
});

/* ========================
   Many variables (exhaust single-char names)
   ======================== */

testMinify("54 variables in one scope", function() {
    var parts = [];
    var refs = [];
    for (var i = 0; i < 54; i++) {
        parts.push("var v" + i + "=" + i);
        refs.push("v" + i);
    }
    var src = "function test() { " + parts.join(";") + "; return " + refs.join("+") + "; }";
    // sum of 0..53 = 1431
    return minifyAndRun(src, "test") === 1431;
});

/* ========================
   Idempotency
   ======================== */

testMinify("idempotent (double minify)", function() {
    var src = "function test() { var longVarName = 42; return longVarName; }";
    var pass1 = rampart.utils.minify(src);
    var pass2 = rampart.utils.minify(pass1);
    return pass1 === pass2;
});

/* ========================
   Error handling
   ======================== */

testMinify("non-string throws", function() {
    var threw = false;
    try { rampart.utils.minify(123); } catch(e) { threw = true; }
    return threw;
});

/* ========================
   Real-world: minify a non-trivial chunk
   ======================== */

testMinify("minify reduces size", function() {
    var src = readFile({file: process.script, returnString: true});
    var min = rampart.utils.minify(src);
    return min.length < src.length;
});

/* ========================
   Done
   ======================== */

printf("\nminify tests complete: ");
if (_nfailed)
    printf("%d FAILED\n", _nfailed);
else
    printf("all passed\n");

process.exit(_nfailed ? 1 : 0);
