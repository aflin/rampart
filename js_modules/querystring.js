"noTranspile";
/* node-compat re-export: makes `require('querystring')` work.
   Legacy module (deprecated in favor of URLSearchParams but still
   widely used). */
module.exports = require('rampart-nodeshim').querystring;
