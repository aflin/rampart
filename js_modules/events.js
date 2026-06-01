"noTranspile";
/* node-compat re-export: makes `require('events')` work.
   Returns the EventEmitter class (with once/getEventListeners/etc.
   as statics, per node's convention). */
module.exports = require('rampart-nodeshim').events;
