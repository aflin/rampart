/* node-compat re-export: makes `require('worker_threads')` work.
   Backed by rampart.thread + rampart.lock + thread.onGet (level-triggered
   message delivery) and thr.terminate() for forcible shutdown. */
module.exports = require('rampart-nodeshim').worker_threads;
