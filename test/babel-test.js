#!./rp
use "babel:{ presets: ['latest'],retainLines:true }"; // use "babel" must be on the first line, or second if #! above, with no spaces or tabs preceeding it"

import * as math from "math";

import { sum, pi } from "math";

rampart.globalize(rampart.cfunc);

function assert(name,test)
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
    printf("testing %-35s - ", name);
    if(test)
        printf("passed\n")
    else
        printf(">>>>> FAILED <<<<<\n");
    if(error) console.log(error);
}

assert("polyfill loaded",
    global._babelPolyfill
);

/* Babel bug? no chrome fails on this as well. 
assert("parse return on separate line", function(){ return
    true;
});
*/

/* from http://es6-features.org/ */
/*
    Copyright © 2015-2017 Ralf S. Engelschall    @engelschall
    Licensed under MIT License.

    heavily modified by aaron @ flin dot org
*/
{
    function foo () { return 1; }
    foo() === 1;
    {
        function foo () { return 2; }
        foo() === 2;
    }
    assert("block scoped func",(
    foo() === 1));
}

var evens=[2,4,6,8];
var odds  = evens.map(v => v + 1);
assert("arrow expression bodies",(
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
assert("arrow statement bodies",(
    fives[0]==5 && fives[1]==10 &&fives[2]==15 && fives[3]==20
));

function altest(){
    this.nums=nums;
    this.fives=[];
    this.nums.forEach((v) => {
        if (v % 5 === 0)
            this.fives.push(v)
    })
    assert("arrow lexical this",(
        this.fives[0]==5 && this.fives[1]==10 && this.fives[2]==15 && this.fives[3]==20
    ));
}
var test={altest:altest};
test.altest();

function f (x, y = 7, z = 42) {
    return x + y + z;
}
assert("default parameters",
    f(1) === 50
);


function f2 (x, y, ...a) {
    return (x + y) * a.length;
}
assert("rest parameters",
    f2(1, 2, "hello", true, 7) === 9
);


var str = "foo";
var c = [ ...str ];
assert("spread operator",
    c[0]=='f' && c[1]=='o' && c[2]=='o' 
);

assert("template literals",function(){ 
    var lit=`${fives[0]} times ${fives[1]} = ${fives[0] * fives[1]}`;
    return lit=="5 times 10 = 50"
});

var tag=(s)=> s;
assert("template literals (tag)",function(){
    return tag`this`=="this";
});
//`

assert("String.raw",
    String.raw`this\nthat`=="this\\nthat"
);

assert("bin and oct literal",function(){ 
    return 0b111110111 === 503 && 0o767 === 503;
});

assert("unicode literals",function(){ return(
    "𠮷".length === 2 && "𠮷".match(/./u)[0].length === 2 && "𠮷" === "\uD842\uDFB7" && "𠮷" === "\u{20BB7}" && "𠮷".codePointAt(0) == 0x20BB7
)});

assert("RE sticky matching",function(){
    var re = new RegExp('foo', 'y');
    return re.test("foo bar") && !re.test("foo bar")
});

assert("property shorthand",function(){
    var x = 1, y = 1;
    var obj = { x, y };
    return obj.x && obj.y;
});

assert("computed property names",function(){

    let obj = {
        foo: "bar",
        [ "foo" + (()=> "2")() ]: 42
    };
    return obj.foo2==42;
});

assert("method properties",function(){
    var obj={
        s(x) { return x;},
        m(x) { return x*x;},
        a(x) { return x+x;}
    }
    
    return obj.s(2)==2 && obj.m(3)==9 && obj.a(3)==6;
});

assert("method properties with generator",function(){
    var obj={
        *g(x) { yield(x);yield(x*x);}
    }
    var gen=obj.g(2);
//    console.log(gen.next().value, gen.next().value);

    return gen.next().value==2 && gen.next().value==4;
});

assert("array matching",function(){
    var list = [ 1, 2, 3 ];
    var [ a, , b ] = list;
    [ b, a ] = [ a, b ];
    return a==3 && b==1;
});

assert("object matching shorthand",function(){
    var obj={a:"a", bOuter:{bInner:"b"}, c:"c"};
    var {a, bOuter:{bInner:b}, c} = obj;
    return a=="a" & b=="b" && c=="c";
});

assert("matching with defaults",function(){
    var obj = { a: 1 };
    var list = [ 1 ];
    var { a, b = 2 } = obj;
    var [ x, y = 2 ] = list;
    return a==1 && b==2 && x==1 && y==2;
});


assert("parameter context matching",function(){
    var f = ([a,b])=> a*b;
    var g = ({a:a,b:b})=>a*b;
    
    return f([2,3])==6 && g({a:3,b:4})==12;
});

assert("fail-soft destructuring",function(){
    var list = [ 7, 42 ];
    var [ a = 1, b = 2, c = 3, d ] = list;
    return a === 7 && b === 42 && c === 3 && d === undefined;
});

/* TODO: export, babel in modules */
assert("import/export",function(){
    return 6.283186==math.sum(math.pi, math.pi) && 6.283186==sum(pi, pi);
});

/* TODO: export defaults http://es6-features.org/#DefaultWildcard */

assert("class defintions and inheritance",function(){
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

assert("inheritance from expressions", function(){
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






























