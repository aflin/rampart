/* node-compat re-export: makes `require('crypto')` work.
   NOTE: rampart's load.crypto / use.crypto still resolves to
   rampart-crypto (a different API) since the proxy resolver checks
   rampart-X first.  This file only affects `require('crypto')`. */
module.exports = require('rampart-nodeshim').crypto;
