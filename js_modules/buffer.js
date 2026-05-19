/* node-compat re-export: makes `require('buffer')` work.
   Returns the buffer module: {Buffer, Blob, File, atob, btoa,
   SlowBuffer, INSPECT_MAX_BYTES, kMaxLength, ...}.  The Buffer
   class itself is also a global. */
module.exports = require('rampart-nodeshim').buffer;
