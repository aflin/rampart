/* node-compat re-export: makes `require('url')` work.
   NOTE: rampart already ships js_modules/rampart-url.js (a different,
   rampart-native URL utility set).  This file shadows ONLY the
   bare-name `require('url')` -- `require('rampart-url')` and
   `load.url` are unaffected. */
module.exports = require('rampart-nodeshim').url;
