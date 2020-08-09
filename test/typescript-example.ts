"use babel:{ filename: 'typesc.ts', presets: ['typescript','env'], retainLines:true }";

function foo(bar: string) {
    return bar + ' baz';
}

let str: string=foo("bar");
console.log( str );

let x: number = 6;

//babel does not type check https://babeljs.io/docs/en/babel-plugin-transform-typescript#caveats
x="hi";
