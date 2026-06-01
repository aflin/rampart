"noTranspile";
/* node-compat re-export: makes `require('module')` work.
   Returns the Module class with builtinModules/createRequire/isBuiltin/
   wrap statics. */
module.exports = require('rampart-nodeshim').module;
