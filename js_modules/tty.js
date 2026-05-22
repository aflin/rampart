/* node-compat re-export: makes `require('tty')` work.
   v1 covers isatty + ReadStream/WriteStream query/control surface
   (setRawMode, getWindowSize, columns/rows, cursorTo, moveCursor,
   clearLine, clearScreenDown, getColorDepth, hasColors).  Full
   Readable/Writable stream behavior deferred until the `stream`
   module lands. */
module.exports = require('rampart-nodeshim').tty;
