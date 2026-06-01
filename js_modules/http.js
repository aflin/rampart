"noTranspile";
/* node-compat re-export: makes `require('http')` work.
   Client side (request/get/Agent) is real and wraps rampart-curl.
   Server side (createServer) currently throws ERR_NOT_IMPLEMENTED;
   see src/duktape/modules/nodeshim-todo.md §8.1 Phase A for plan. */
module.exports = require('rampart-nodeshim').http;
