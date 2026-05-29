/* node-compat shim for `require('constants')`.
 *
 * Legacy top-level module (deprecated since Node 6.3, but still
 * resolvable forever).  It re-exports the constants now properly
 * homed under `fs.constants`, `os.constants` (errno + signals +
 * dlopen + priority) and `crypto.constants`, in a single flat
 * namespace.  fs-extra and a handful of other older packages still
 * use it for `F_OK` / `R_OK` / `S_IF*` mask access.
 *
 * This module pulls the live constants out of fs/os/crypto so it
 * stays in sync with whatever those modules expose, rather than
 * duplicating the values here.
 */
'use strict';

/* Plain {} so .hasOwnProperty / .toString / etc. still work — fs-extra
 * calls constants.hasOwnProperty('SOME_FLAG') as a feature-test guard. */
const out = {};

function mergeFlat(src) {
    if (!src || typeof src !== 'object') return;
    for (const key of Object.keys(src)) {
        const val = src[key];
        if (val !== null && typeof val === 'object') {
            /* Nested namespace (e.g. os.constants.errno, .signals,
               .dlopen, .priority) — flatten one level into the
               top-level namespace, matching node's legacy shape. */
            for (const k2 of Object.keys(val)) {
                if (typeof val[k2] !== 'object') out[k2] = val[k2];
            }
        } else {
            out[key] = val;
        }
    }
}

try { mergeFlat(require('fs').constants); }     catch (_) {}
try { mergeFlat(require('os').constants); }     catch (_) {}
try { mergeFlat(require('crypto').constants); } catch (_) {}

module.exports = out;
