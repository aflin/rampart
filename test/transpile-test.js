#!/usr/bin/env rampart

rampart.globalize(rampart.utils);

import * as math from "math";

import { sum, pi } from "math";

// Default + named (multi-line; spacing/comments) + alias usage
import defPayload, {
  f,             // named function
  aa, bee,       // re-exported aliases of a, b
  q, r, e, u,    // from object pattern
  rest,          // object rest
  x, y, tail     // array pattern
} from "./export-module.js";

// Namespace import to ensure star-import path works too
import * as M from "./export-module.js";

// Separate named import with `as` alias (exercises aliasing in import specifiers)
import { aa as AA, bee as Bee } from "./export-module.js";

// Another default-only import to ensure multiple default imports from same module work
import dp from "./export-module.js";

// ---------- Tests ----------

// Sanity: default export (identifier case)
testFeature("Default export object tag", function() {
  return defPayload && defPayload.tag === "OK";
});
testFeature("Default export object version", function() {
  return defPayload && defPayload.version === 1;
});
testFeature("Second default import equals first", function() {
  return dp === defPayload;
});

// Named function with default param (should be downleveled by transpiler)
testFeature("Named function export with default param (f())", function() {
  return f() === 2;
});
testFeature("Named function export with default param (f(10))", function() {
  return f(10) === 11;
});

// Re-exported aliases
testFeature("Re-export alias aa === 1", function() {
  return aa === 1;
});
testFeature("Re-export alias bee === 2", function() {
  return bee === 2;
});

// Direct names from object destructuring export
testFeature("Object destructuring export q === 10", function() {
  return q === 10;   // from sourceObj.p
});
testFeature("Object destructuring export r === 20", function() {
  return r === 20;
});
testFeature("Nested destructuring export e === 5", function() {
  return e === 5;    // from sourceObj.nested.e
});
testFeature("Defaulted binding u === 99", function() {
  return u === 99;
});
testFeature("Object rest keys are ['s','t']", function() {
  return rest && Array.isArray(Object.keys(rest)) &&
         Object.keys(rest).sort().join(",") === "s,t";
});

// From array destructuring export (hole, default, rest)
testFeature("Array destructuring", function() {
  return x === 1;
});
testFeature("Array destructuring (default not used)", function() {
  return y === 3;
});
testFeature("Array rest tail === [4,5]", function() {
  return Array.isArray(tail) && tail.length === 2 && tail[0] === 4 && tail[1] === 5;
});

// Namespace import mirrors named/default
testFeature("Namespace import mirrors aa", function() {
  return M.aa === aa;
});
testFeature("Namespace import mirrors default", function() {
  return M.default === defPayload;
});

// Aliased named imports
testFeature("Aliased import AA === aa", function() {
  return AA === aa;
});
testFeature("Aliased import Bee === bee", function() {
  return Bee === bee;
});

function testFeature(name,test)
{
    var error=false;
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    printf("testing transpile - %-48s - ", name);
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}

var evens=[2,4,6,8];
var odds  = evens.map(v => v + 1);
testFeature("arrow expression bodies",(
    odds[0]==3 && odds[1]==5 &&odds[2]==7 && odds[3]==9
));

var nums=[];
for (var i=1; i<21; i++)
    nums[i]=i; 
var fives=[];
nums.forEach(v => {
   if (v % 5 === 0)
       fives.push(v);
});
testFeature("arrow statement bodies",(
    fives[0]==5 && fives[1]==10 &&fives[2]==15 && fives[3]==20
));

function altest(){
    this.nums=nums;
    this.fives=[];
    this.nums.forEach((v) => {
        if (v % 5 === 0)
            this.fives.push(v)
    })
    testFeature("arrow lexical this",(
        this.fives[0]==5 && this.fives[1]==10 && this.fives[2]==15 && this.fives[3]==20
    ));
}
var test={altest:altest};
test.altest();

function addDef (x, y = 7, z = 42) {
    return x + y + z;
}
testFeature("default parameters", function(){
    return addDef(1) === 50
});

var obj2,x1 = 1,y1 = 1;
testFeature("transpile eval code", function () {
  eval("obj2={ x1, y1 }");
  return obj2.x1 && obj2.y1;
});

function f2 (x, y, ...a) {
    testFeature("rest parameters 1",
        (a[0]=="hello"&& a[1]===true && a[2]==7) 
    );     

    return (x + y) * a.length;
}
testFeature("rest parameters 2",
    f2(1, 2, "hello", true, 7) === 9
);


var str = "foo";
var c = [ ...str ];
testFeature("spread operator array",
    c[0]=='f' && c[1]=='o' && c[2]=='o' 
);

testFeature("advanced spread and shorthand", function(){
    var o={a:1,b:2};
    var o2={c:3,d:4};
    var x =5;
    var z="zee";

    var y = { ...o, x:x,z  };
    var o3 = { x, z };
    var o4 = { ...o2, x, ...o};

    var a = [1,2];

    var y1 = [ ...z, x, a  ];
    var y2 = [ x, ...z, a  ];
    var y3 = [ ...a, x, ...z  ];

    /* node produces this
    { a: 1, b: 2, x: 5, z: 'zee' }
    { x: 5, z: 'zee' }
    { c: 3, d: 4, x: 5, a: 1, b: 2 }
    [ 'z', 'e', 'e', 5, [ 1, 2 ] ]
    [ 5, 'z', 'e', 'e', [ 1, 2 ] ]
    [ 1, 2, 5, 'z', 'e', 'e' ]
    */
    return (
        y.a==1 && y.b==2 && y.x==5 & y.z=="zee"&
        o3.x==5 && o3.z=="zee" &&
        o4.c==3 && o4.d==4 && o4.x==5 && o4.a==1 && o4.b==2 &&
        y1[0]=='z' && y1[1]=='e' && y1[2]=='e' && y1[3]==5 && y1[4][0]==1 && y1[4][1]==2 &&
        y2[0]==5 && y2[1]=='z' && y2[2]=='e' && y2[3]=='e' && y2[4][0]==1 && y2[4][1]==2 &&
        y3[0]==1 && y3[1]==2 &&y3[2]==5 &&  y3[3]=='z' && y3[4]=='e' && y3[5]=='e'   
    )

});

testFeature("template literals",function(){ 
    var lit=`${fives[0]} times ${fives[1]} = ${fives[0] * fives[1]}`;
    return lit=="5 times 10 = 50"
});

var tag=(s)=> s;
testFeature("template literals (tag)",function(){
    return tag`this`=="this";
});
//`

/*
testFeature("String.raw",
    String.raw`this\nthat`=="this\\nthat"
);
*/

testFeature("bin and oct literal",function(){ 
    return 0b111110111 === 503 && 0o767 === 503;
});

testFeature("unicode literals",function(){ return(
    "𠮷".length === 2 && "𠮷".match(/./u)[0].length === 2 && "𠮷" === "\uD842\uDFB7" && "𠮷" === "\u{20BB7}" && "𠮷".codePointAt(0) == 0x20BB7
)});

/*
testFeature("RE sticky matching",function(){
    var re = new RegExp('foo', 'y');
    return re.test("foo bar") && !re.test("foo bar")
});
*/

testFeature("property shorthand",function(){
    var x = 1, y = 1;
    var obj = { x, y };
    return obj.x && obj.y;
});

testFeature("computed property names",function(){

    let obj = {
        foo: "bar",
        [ "foo" + (()=> "2")() ]: 42
    };
    return obj.foo2==42;
});

testFeature("method properties",function(){
    var obj={
        s(x) { return x;},
        m(x) { return x*x;},
        a(x) { return x+x;}
    }
    
    return obj.s(2)==2 && obj.m(3)==9 && obj.a(3)==6;
});

testFeature("array find/findIndex",function(){
    var arr=[ 1, 3, 4, 2 ];
    return arr.find(x => x > 3) == 4 && arr.findIndex(x => x > 3) == 2;
});

// Default + named (multi-line; spacing/comments) + alias usage
import defPayload, {
  f,             // named function
  aa, bee,       // re-exported aliases of a, b
  q, r, e, u,    // from object pattern
  rest,          // object rest
  x, y, tail     // array pattern
} from "./export-module.js";

// Namespace import to ensure star-import path works too
import * as M from "./export-module.js";

// Separate named import with `as` alias (exercises aliasing in import specifiers)
import { aa as AA, bee as Bee } from "./export-module.js";

// Another default-only import to ensure multiple default imports from same module work
import dp from "./export-module.js";

// ---------- Tests ----------

// Sanity: default export (identifier case)
testFeature("Default export object tag", function() {
  return defPayload && defPayload.tag === "OK";
});
testFeature("Default export object version", function() {
  return defPayload && defPayload.version === 1;
});
testFeature("Second default import equals first", function() {
  return dp === defPayload;
});

// Named function with default param (should be downleveled by transpiler)
testFeature("Named function export with default param (f())", function() {
  return f() === 2;
});
testFeature("Named function export with default param (f(10))", function() {
  return f(10) === 11;
});

// Re-exported aliases
testFeature("Re-export alias aa === 1", function() {
  return aa === 1;
});
testFeature("Re-export alias bee === 2", function() {
  return bee === 2;
});

// Direct names from object destructuring export
testFeature("Object destructuring export q === 10", function() {
  return q === 10;   // from sourceObj.p
});
testFeature("Object destructuring export r === 20", function() {
  return r === 20;
});
testFeature("Nested destructuring export e === 5", function() {
  return e === 5;    // from sourceObj.nested.e
});
testFeature("Defaulted binding u === 99", function() {
  return u === 99;
});
testFeature("Object rest keys are ['s','t']", function() {
  return rest && Array.isArray(Object.keys(rest)) &&
         Object.keys(rest).sort().join(",") === "s,t";
});

// From array destructuring export (hole, default, rest)
testFeature("Array destructuring", function() {
  return x === 1;
});
testFeature("Array destructuring (default not used)", function() {
  return y === 3;
});
testFeature("Array rest tail === [4,5]", function() {
  return Array.isArray(tail) && tail.length === 2 && tail[0] === 4 && tail[1] === 5;
});

// Namespace import mirrors named/default
testFeature("Namespace import mirrors aa", function() {
  return M.aa === aa;
});
testFeature("Namespace import mirrors default", function() {
  return M.default === defPayload;
});

// Aliased named imports
testFeature("Aliased import AA === aa", function() {
  return AA === aa;
});
testFeature("Aliased import Bee === bee", function() {
  return Bee === bee;
});

// Basic class with constructor param is preserved
testFeature("class: function keeps constructor params", function () {
  class Person {
    constructor(name) { this.name = name; }
    greet() { return `Hello, I'm ${this.name}`; }
  }
  const p = new Person("Alice");
  return p.name === "Alice" && p.greet() === "Hello, I'm Alice" && Person.length === 1;
});

// Class with no explicit constructor → synthesized empty constructor
testFeature("class without constructor still constructs", function () {
  class A {}
  const a = new A();
  return a instanceof A && typeof a === "object" && A.length === 0;
});

// Instance vs. static methods
testFeature("instance vs static methods behave correctly", function () {
  class Counter {
    constructor(n) { this.n = n; }
    inc() { this.n += 1; return this.n; }
    static zero() { return new Counter(0); }
  }
  const c = Counter.zero();
  const n1 = c.inc();
  const n2 = c.inc();
  return n1 === 1 && n2 === 2 && typeof Counter.zero === "function" && !("zero" in Counter.prototype);
});

// Inheritance: extends + super(...) in constructor
testFeature("extends + super(...) initializes base fields", function () {
  class Animal {
    constructor(name) { this.name = name; }
    who() { return this.name; }
  }
  class Dog extends Animal {
    constructor(name, breed) { super(name); this.breed = breed; }
    speak() { return this.who() + " says woof"; }
    static kind() { return "canine"; }
  }
  const d = new Dog("Fido", "mutt");
  return d instanceof Dog &&
         d instanceof Animal &&
         d.name === "Fido" &&
         d.breed === "mutt" &&
         d.speak() === "Fido says woof" &&
         Dog.kind() === "canine";
});

// Template literals inside methods get concatenated
testFeature("template literal inside method is concatenated", function () {
  class P { constructor(n){ this.n = n; } msg(){ return `n=${this.n}`; } }
  return new P(7).msg() === "n=" + 7;
});

// Class expression (named)
testFeature("class expression (named) works", function () {
  const Box = class Box {
    constructor(v) { this.v = v; }
    get() { return this.v; }
  };
  const b = new Box(42);
  return b.get() === 42 && b instanceof Box;
});

// Class expression (anonymous)
testFeature("class expression (anonymous) works", function () {
  const C = class { constructor(x){ this.x = x; } val(){ return this.x; } };
  const c = new C(5);
  return c.val() === 5 && c instanceof C;
});

// Default parameter in constructor
testFeature("constructor default parameter preserved", function () {
  class User {
    constructor(name = "anon") { this.name = name; }
  }
  return new User().name === "anon" && new User("Zed").name === "Zed";
});

/*/ Computed method name on prototype (edge)
testFeature("computed method name on prototype", function () {
  const key = "go";
  class M {
    constructor() { this.hit = 0; }
    [key]() { this.hit++; return this.hit; }
  }
  const m = new M();
  return typeof m.go === "function" && m.go() === 1 && m.go() === 2;
});

// Static computed method name (edge)
testFeature("computed static method name", function () {
  const S = "make";
  class Factory {
    static [S](x) { return { x }; }
  }
  const o = Factory.make(9);
  return o && o.x === 9 && typeof Factory.make === "function";
});
*/

// Ensure prototype chain is set up (constructor property)
testFeature("prototype.constructor points back to function", function () {
  class T {}
  return T.prototype.constructor === T;
});

// Multiple classes in one scope (no collisions)
testFeature("multiple classes coexist without collisions", function () {
  class A { constructor(v){ this.v=v; } getV(){ return this.v; } }
  class B { constructor(v){ this.v=v*2; } getV(){ return this.v; } }
  const a = new A(3), b = new B(3);
  return a.getV() === 3 && b.getV() === 6 && !(a instanceof B) && !(b instanceof A);
});


/*
testFeature("method properties with generator",function(){
    var obj={
        *g(x) { yield(x);yield(x*x);}
    }
    var gen=obj.g(2);
//    console.log(gen.next().value, gen.next().value);

    return gen.next().value==2 && gen.next().value==4;
});

testFeature("array matching",function(){
    var list = [ 1, 2, 3 ];
    var [ a, , b ] = list;
    [ b, a ] = [ a, b ];
    return a==3 && b==1;
});

testFeature("object matching shorthand",function(){
    var obj={a:"a", bOuter:{bInner:"b"}, c:"c"};
    var {a, bOuter:{bInner:b}, c} = obj;
    return a=="a" & b=="b" && c=="c";
});

testFeature("matching with defaults",function(){
    var obj = { a: 1 };
    var list = [ 1 ];
    var { a, b = 2 } = obj;
    var [ x, y = 2 ] = list;
    return a==1 && b==2 && x==1 && y==2;
});


testFeature("parameter context matching",function(){
    var f = ([a,b])=> a*b;
    var g = ({a:a,b:b})=>a*b;
    
    return f([2,3])==6 && g({a:3,b:4})==12;
});

testFeature("fail-soft destructuring",function(){
    var list = [ 7, 42 ];
    var [ a = 1, b = 2, c = 3, d ] = list;
    return a === 7 && b === 42 && c === 3 && d === undefined;
});


testFeature("import/export",function(){
    return 6.283186==math.sum(math.pi, math.pi) && 6.283186==sum(pi, pi);
});

/* TODO: export defaults http://es6-features.org/#DefaultWildcard */
/*
testFeature("class defintions and inheritance",function(){
    class mult {
        constructor (x, y) {
            this.x=x;
            this.y=y;
        }
        mult () {
            return this.x*this.y;
        }
    }
    class multAndAdd extends mult {
        constructor (x,y,z)
        {
            super(x,y);
            this.z=z;
        }
        multadd () {
            return this.mult(this.x,this.y) + this.z;
        }            
    }

    var m = new mult(3,4)
    var ma = new multAndAdd(3,4,6);
    return 12 == m.mult() && 18 == ma.multadd();
});

testFeature("inheritance from expressions", function(){
    var aggregation = (baseClass, ...mixins) => {
        let base = class _Combined extends baseClass {
            constructor (...args) {
                super(...args);
                mixins.forEach((mixin) => {
                    mixin.prototype.initializer.call(this);
                });
            }
        };
        let copyProps = (target, source) => {
            Object.getOwnPropertyNames(source)
                .concat(Object.getOwnPropertySymbols(source))
                .forEach((prop) => {
                if (prop.match(/^(?:constructor|prototype|arguments|caller|name|bind|call|apply|toString|length)$/))
                    return
                Object.defineProperty(target, prop, Object.getOwnPropertyDescriptor(source, prop))
            })
        }
        mixins.forEach((mixin) => {
            copyProps(base.prototype, mixin.prototype);
            copyProps(base, mixin);
        });
        return base;
    };

    class Colored {
        initializer ()     { this._color = "white"; }
        get color ()       { return this._color; }
        set color (v)      { this._color = v; }
    }

    class ZCoord {
        initializer ()     { this._z = 0; }
        get z ()           { return this._z; }
        set z (v)          { this._z = v; }
    }

    class Shape {
        constructor (x, y) { this._x = x; this._y = y; }
        get x ()           { return this._x; }
        set x (v)          { this._x = v; }
        get y ()           { return this._y; }
        set y (v)          { this._y = v; }
    }

    class Rectangle extends aggregation(Shape, Colored, ZCoord) {}

    var rect = new Rectangle(7, 42);
    rect.z     = 1000;
    rect.color = "red";
    return rect.x==7 && rect.y==42 && rect.z==1000 && rect.color=="red";
});

/* TODO: http://es6-features.org/#SymbolType and http://es6-features.org/#GlobalSymbols */
/*testFeature("iterator and for of",function(){
    let fibonacci = {
        [Symbol.iterator]() {
            let pre = 0, cur = 1;
            return {
               next () {
                   [ pre, cur ] = [ cur, pre + cur ];
                   return { done: false, value: cur };
               }
            };
        }
    }
    var n;
    for (n of fibonacci) {
        if (n > 1000)
            break;
    }

    return n==1597;
});

testFeature("generator function/iterator protocol",function(){
    let fibonacci = {
        *[Symbol.iterator]() {
            let pre = 0, cur = 1;
            for (;;) {
                [ pre, cur ] = [ cur, pre + cur ];
                yield cur;
            }
        }
    }
    var n;
    for (n of fibonacci) {
        if (n > 1000)
            break;
    }
    return n==1597;
});

testFeature("generator function direct use",function(){
    var res=0;

    function* range (start, end, step) {
        while (start < end) {
            yield start;
            start += step;
        }
    }

    for (let i of range(0, 10, 2)) {
        res+=i;
    }
    return res==20;
});

testFeature("generator matching",function(){
    let fibonacci = function* (numbers) {
        let pre = 0, cur = 1;
        while (numbers-- > 0) {
            [ pre, cur ] = [ cur, pre + cur ];
            yield cur;
        }
    };

    let numbers = [ ...fibonacci(1000) ];
    var sum2 = numbers.reduce(function(a, b){
        return a + b;
    }, 0);

    return (sum2==1.8412729310978296e+209);
});

// http://es6-features.org/#GeneratorControlFlow
// duktape doesn't support setTimeout

// http://es6-features.org/#GeneratorMethods incomplete. TODO: write own

testFeature("set data structure",function(){
    let s = new Set();
    var sum=0
    s.add(2).add(4).add(2);
    for (let key of s.values()) // insertion order
        sum+=key;

    return s.has(2) === true && s.size === 2 && sum==6;
});

testFeature("map data structure",function(){
    let m = new Map();
    let s = Symbol();
    m.set("hello", 42);
    m.set(s, 34);
    return m.get(s) === 34 && m.size === 2;
});


testFeature("weak-link data structures",function(){
    let isMarked     = new WeakSet();
    let attachedData = new WeakMap();

    class Node {
        constructor (id)   { this.id = id;                  }
        mark        ()     { isMarked.add(this);            }
        unmark      ()     { isMarked.delete(this);         }
        marked      ()     { return isMarked.has(this);     }
        set data    (data) { attachedData.set(this, data);  }
        get data    ()     { return attachedData.get(this); }
    }

    let foo = new Node("foo");
    var ret = JSON.stringify(foo) === '{"id":"foo"}';
    foo.mark();
    foo.data = "bar";
    ret = ret && foo.data === "bar";
    ret = ret && JSON.stringify(foo) === '{"id":"foo"}';

    ret = ret && isMarked.has(foo)     === true;
    ret = ret && attachedData.has(foo) === true
    foo = null  /* remove only reference to foo */
/*
    ret = ret && attachedData.has(foo) === false
    ret = ret && isMarked.has(foo)     === false
    return ret;
});

testFeature("typed arrays",function(){
    class Example {
        constructor (buffer = new ArrayBuffer(24)) {
            this.buffer = buffer;
        }
        set buffer (buffer) {
            this._buffer    = buffer;
            this._id        = new Uint32Array (this._buffer,  0,  1);
            this._username  = new Uint8Array  (this._buffer,  4, 16);
            this._amountDue = new Float32Array(this._buffer, 20,  1);
        }
        get buffer ()     { return this._buffer;       }
        set id (v)        { this._id[0] = v;           }
        get id ()         { return this._id[0];        }
        set username (v)  { 
            for (var i = 0, strLen = v.length; i < strLen; i++)
                this._username[i] = v.charCodeAt(i);
        }
        get username ()   { return this._username[0];  }
        set amountDue (v) { this._amountDue[0] = v;    }
        get amountDue ()  { return this._amountDue[0]; }
    }

    let example = new Example()
    example.id = 7
    example.username = "John Doe"
    example.amountDue = 42.0
    var mysum=0;

    var checkarray=new Uint8Array (example._buffer);

    for (var i=0;i<checkarray.length;i++)
        mysum=checkarray[i];
    return mysum==66;
});
testFeature("object property assignment",function(){
    var dest = { quux: 0 };
    var src1 = { foo: 1, bar: 2 };
    var src2 = { foo: 3, baz: 4 };
    Object.assign(dest, src1, src2);
    return dest.quux === 0 && dest.foo  === 3 && dest.bar  === 2 && dest.baz  === 4;
});

testFeature("string repeat",function(){
    var r=" ".repeat(4) + "foo".repeat(3);
    return r=="    foofoofoo";
});

testFeature("string searching",function(){
    return "hello".startsWith("ello", 1) && "hello".endsWith("hell", 4) && 
    "hello".includes("ell") && "hello".includes("ell", 1) && ! "hello".includes("ell", 2);
});

testFeature("number type checking",function(){
    return Number.isNaN(42) === false && Number.isNaN(NaN) === true && Number.isFinite(Infinity) === false &&
    Number.isFinite(-Infinity) === false && Number.isFinite(NaN) === false && Number.isFinite(123) === true;
});

testFeature("number safety checking",function(){
    return Number.isSafeInteger(42) === true && Number.isSafeInteger(9007199254740992) === false;
});

testFeature("number comparison",function(){
    return ! (0.1 + 0.2 === 0.3) &&
    Math.abs((0.1 + 0.2) - 0.3) < Number.EPSILON;
});

testFeature("number truncation",function(){
    return Math.trunc(42.7) == 42 && Math.trunc( 0.1) == 0 && Math.trunc(-0.1) == 0;
});

testFeature("number sign determination",function(){
    return Math.sign(7) == 1 && Math.sign(0) == 0 && 
    Math.sign(-0) == -0 && Math.sign(-7) == -1 && isNaN(Math.sign(NaN));
});

// http://es6-features.org/#PromiseUsage
// http://es6-features.org/#PromiseUsage
// anything with setTimeout will fail in duktape

testFeature("proxying",function(){
    let target = {
        foo: "Welcome, foo"
    };
    let proxy = new Proxy(target, {
        get (receiver, name) {
            return name in receiver ? receiver[name] : `Hello, ${name}`;
        }
    });
    return proxy.foo   === "Welcome, foo" && proxy.world === "Hello, world";
});

testFeature("reflection",function(){
    let obj = { a: 1 };
    let s=Symbol("c");
    Object.defineProperty(obj, "b", { value: 2 });
    obj[s] = 3;
    var r=Reflect.ownKeys(obj); // [ "a", "b", Symbol(c) ]
    return r[0] == "a" && r[1] == "b" && r[2] == s;
});

/*
// TODO: find a working polyfill
testFeature("Internationalization",function(){
    var list = [ "ä", "a", "z" ];
    var l10nDE = new Intl.Collator("de");
    var l10nSV = new Intl.Collator("sv");
    l10nDE.compare("ä", "z") === -1;
    l10nSV.compare("ä", "z") === +1;
    console.log(list.sort(l10nDE.compare)); // [ "a", "ä", "z" ]
    console.log(list.sort(l10nSV.compare)); // [ "a", "z", "ä" ]
    return true;
});
*/
/*
//https://www.freecodecamp.org/news/here-are-examples-of-everything-new-in-ecmascript-2016-2017-and-2018-d52fa3b5a70e/
testFeature("2016 - array includes/indexOf",function(){
    const arr = [1, 2, 3, 4, NaN];

    return arr.includes(3) && arr.includes(NaN) && arr.indexOf(NaN)==-1;
});

testFeature("2016 - exponentiation",function(){
    return 7**2 == 49;
});

testFeature("2017 - Object.values()",function(){
    const x={ a:1, b:2, c:3 };
    var sum2=0;
    
    Object.values(x).forEach(v => sum2+=v);
    return sum2==6;
});

testFeature("2017 - Object.entries()",function(){
    const x={ a:1, b:2, c:3 };
    var sum2=0;
    var cat="";

    for (let [key, value] of Object.entries(x)){
        sum2+=value;
        cat+=key;
    }
    return sum2==6 && cat == "abc";
});

testFeature("2017 - padding",function(){
    return "x".padStart(5)=="    x" && "1".padEnd(6,"0") == "100000";
});

testFeature("2017 - Object.getOwnPropertyDescriptors",function(){
    var Car = {
     name: 'BMW',
     price: 1000000,
     set discount(x) {
      this.d = x;
     },
     get discount() {
      return this.d;
     },
    };
    const ElectricCar2 = Object.defineProperties({}, Object.getOwnPropertyDescriptors(Car));

    return Object.getOwnPropertyDescriptor(ElectricCar2, 'discount') != undefined;
});

testFeature("2017 - trailing comma",function(){
    function person( name, age, ) {}; //no error thrown
    return true;
});

//2017 -async await require setTimeout, won't work in duktape


testFeature("2018 - tagged literal replacement",function(){
    function greet(hardCodedPartsArray, ...replacementPartsArray) {
        
    let str = '';
     hardCodedPartsArray.forEach((string, i) => {
      if (i < replacementPartsArray.length) {
       str += `${string} ${replacementPartsArray[i] || ''}`;
      } else {
       str += `${string} ${timeGreet()}`; //<-- append Good morning/afternoon/evening here
      }
     });
     return str;
    }

    const firstName = 'Raja';
    const greetings = greet`Hello ${firstName}!`;//`

    function timeGreet() {
        return 'Good Day!';
    }

    return greetings == "Hello  Raja! Good Day!";
});

try {
  function msgAfterTimeout (msg, msg2, timeout) {
      return new Promise((resolve, reject) => {
          setTimeout(() => resolve(`${msg}${msg2}`), timeout)
      })
  }
  msgAfterTimeout("Foo", "", 10).then(
    (msg) => msgAfterTimeout(msg, "Bar", 10)
  ).then((msg) => {
      testFeature("Async - Promises/setTimeout", msg=="FooBar");
  });
} catch (e) {
  testFeature("Async - Promises/setTimeout", false);
}

try {
  function resolveAfter2ms() {
    return new Promise(resolve => {
      setTimeout(() => {
        resolve('resolved');
      }, 2);
    });
  }

  async function asyncCall() {
    const result = await resolveAfter2ms();
    testFeature("Async - Promises/async function", result=="resolved");
  }

  asyncCall();

} catch (e) {
  testFeature("Async - Promises/async function", false);
}

//TODO: finish 2018
setTimeout(function(){
  printf("NOT TESTED:  RE sticky flag and Internationalization\n");
},100);
*/