"noTranspile";
/* node-compat re-export: makes `require('https')` work.
   Same implementation as http; default protocol is https:// and the
   default Agent's defaultPort is 443.  Wraps rampart-curl. */
module.exports = require('rampart-nodeshim').https;
