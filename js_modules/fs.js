"noTranspile";
/* node-compat re-export: makes `require('fs')` work.
   Implementation lives in rampart-nodeshim. */
module.exports = require('rampart-nodeshim').fs;
