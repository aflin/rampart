/* node-compat re-export: makes `require('stream')` work.
   Implementation (Readable/Writable/Duplex/Transform/PassThrough +
   pipeline/finished + WHATWG interop) lives in rampart-nodeshim and
   adapts WHATWG Streams from rampart-whatwg. */
module.exports = require('rampart-nodeshim').stream;
