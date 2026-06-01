"noTranspile";
/* node-compat re-export: makes `require('console')` work.
   The default export is the global console (which has been enhanced by
   rampart-console.c with time/table/group/etc.) with the Console class
   attached as a .Console property. */
module.exports = require('rampart-nodeshim').console;
