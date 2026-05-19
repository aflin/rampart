/* node-compat re-export: makes `require('zlib')` work.
   Backed by libdeflate via rampart.utils.{gzip,gunzip,...}.
   Stream classes throw ENOSYS (libdeflate is one-shot only). */
module.exports = require('rampart-nodeshim').zlib;
