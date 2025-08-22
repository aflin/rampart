// export-module.js — edge cases for exports (NO classes)

// Sources to destructure from
const sourceObj = {
  p: 10,
  r: 20,
  s: 30,
  t: 40,
  nested: { e: 5 },
  u: undefined
};

const sourceArr = [1, /* hole */, 3, 4, 5];

// Named function export with default param (should be downleveled by your pass)
export function f(n = 1) {
  return n + 1;
}

// Multiple named value exports
export const a = 1, b = 2;

// Object destructuring export with alias, shorthand, nested, default, and rest
export const { p: q, r, nested: { e }, u = 99, ...rest } = sourceObj;

// Array destructuring export with hole, default, and rest
export let [x, , y = 2, ...tail] = sourceArr;

// Local re-exports with aliases (no `from`)
export { a as aa, b as bee };

// Default export (identifier form)
const DEFAULT_PAYLOAD = { tag: "OK", version: 1 };
export default DEFAULT_PAYLOAD;
