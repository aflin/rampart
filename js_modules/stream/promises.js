"noTranspile";
/* node-compat re-export: require('stream/promises').
   Wraps stream.pipeline / stream.finished in Promise-returning form
   (the underlying impls live on require('stream')). */
var stream = require('rampart-nodeshim').stream;

function pipelinePromise() {
    var streams = Array.prototype.slice.call(arguments);
    return new Promise(function(resolve, reject) {
        streams.push(function(err) { err ? reject(err) : resolve(); });
        stream.pipeline.apply(null, streams);
    });
}
function finishedPromise(s, opts) {
    return new Promise(function(resolve, reject) {
        stream.finished(s, opts || {}, function(err) { err ? reject(err) : resolve(); });
    });
}

module.exports = {
    pipeline: pipelinePromise,
    finished: finishedPromise
};
