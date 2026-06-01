"noTranspile";
/* node-compat re-export: require('assert/strict').
   In node, this is the strict-mode variant of assert.  Our base
   assert is already strict-mode by default, so it's a direct alias. */
module.exports = require('rampart-nodeshim').assert;
