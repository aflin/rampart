/* node-compat stub for `require('http2')`.
 *
 * rampart-nodeshim doesn't implement HTTP/2.  This stub exists so that
 * libraries which `require('http2')` at module load time (axios,
 * undici, etc.) can finish loading; any attempt to actually USE
 * HTTP/2 throws a clear error.
 *
 * What's populated:
 *   - `connect()` / `createServer()` / `createSecureServer()` throw
 *     `ERR_HTTP2_NOT_SUPPORTED` so the failure mode is obvious.
 *   - `constants` carries the standard HTTP/2 pseudo-header names
 *     and a small subset of status codes — many libs (axios in
 *     particular) destructure these at function-definition time
 *     without ever calling the function.
 *
 * If a real HTTP/2 client is needed later, this file becomes the
 * shim entry point.
 */
'use strict';

function notSupported(name) {
    return function () {
        var err = new Error('HTTP/2 is not supported by rampart-nodeshim (called ' + name + ')');
        err.code = 'ERR_HTTP2_NOT_SUPPORTED';
        throw err;
    };
}

module.exports = {
    /* Constants — string pseudo-headers + a small subset of status
       codes that npm libraries commonly destructure at definition
       time.  Values match Node's `http2.constants`. */
    constants: {
        HTTP2_HEADER_AUTHORITY:               ':authority',
        HTTP2_HEADER_METHOD:                  ':method',
        HTTP2_HEADER_PATH:                    ':path',
        HTTP2_HEADER_PROTOCOL:                ':protocol',
        HTTP2_HEADER_SCHEME:                  ':scheme',
        HTTP2_HEADER_STATUS:                  ':status',
        HTTP2_HEADER_ACCEPT_ENCODING:         'accept-encoding',
        HTTP2_HEADER_CONTENT_ENCODING:        'content-encoding',
        HTTP2_HEADER_CONTENT_LENGTH:          'content-length',
        HTTP2_HEADER_CONTENT_TYPE:            'content-type',
        HTTP2_HEADER_COOKIE:                  'cookie',
        HTTP2_HEADER_HOST:                    'host',
        HTTP2_HEADER_USER_AGENT:              'user-agent',
        HTTP_STATUS_OK:                       200,
        HTTP_STATUS_NO_CONTENT:               204,
        HTTP_STATUS_MOVED_PERMANENTLY:        301,
        HTTP_STATUS_FOUND:                    302,
        HTTP_STATUS_NOT_MODIFIED:             304,
        HTTP_STATUS_BAD_REQUEST:              400,
        HTTP_STATUS_UNAUTHORIZED:             401,
        HTTP_STATUS_FORBIDDEN:                403,
        HTTP_STATUS_NOT_FOUND:                404,
        HTTP_STATUS_INTERNAL_SERVER_ERROR:    500,
        NGHTTP2_NO_ERROR:                     0,
        NGHTTP2_PROTOCOL_ERROR:               1,
        NGHTTP2_INTERNAL_ERROR:               2
    },
    connect:             notSupported('http2.connect'),
    createServer:        notSupported('http2.createServer'),
    createSecureServer:  notSupported('http2.createSecureServer'),
    getDefaultSettings:  function () { return {}; },
    getPackedSettings:   notSupported('http2.getPackedSettings'),
    getUnpackedSettings: notSupported('http2.getUnpackedSettings')
};
