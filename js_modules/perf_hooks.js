"noTranspile";
/* node-compat re-export: makes `require('perf_hooks')` work.
   Returns {performance, PerformanceEntry, PerformanceMark,
   PerformanceMeasure, constants}. */
module.exports = require('rampart-nodeshim').perf_hooks;
