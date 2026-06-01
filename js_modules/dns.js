"noTranspile";
/* node-compat re-export: makes `require('dns')` work.
   Wraps rampart.net.Resolver under node's dns API. */
module.exports = require('rampart-nodeshim').dns;
