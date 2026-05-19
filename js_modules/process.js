/* node-compat re-export: makes `require('process')` work.
   Returns the process object (same one usually available as the
   `process` global in node). */
module.exports = require('rampart-nodeshim').process;
