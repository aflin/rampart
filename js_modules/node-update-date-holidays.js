#!/usr/bin/env node
/*
 * node-update-date-holidays.js
 *
 * Rebuild the vendored `rampart-date-holidays.js` from the latest (or a
 * pinned) npm `date-holidays` release, test it, and — with your go-ahead —
 * replace the copy rampart actually loads (the one in process.modulesPath).
 *
 * This is a NODE script, not a rampart script: it needs npm + esbuild to
 * download and bundle the package.  It shells out to the rampart binary
 * only for the transpile step (`rampart -t`, currently the only way to
 * emit transpiled output) and to run the test suite.
 *
 * It is meant to be run two ways:
 *   - by the maintainer when cutting a new release, and
 *   - by a user who just wants to pull the latest holiday data.
 *
 * Pipeline:
 *   1. find the rampart binary and ask it where modulesPath is
 *   2. compare the installed module's version with npm's latest;
 *      if they match, say so and stop (nothing is built)
 *   3. npm install + esbuild-bundle the package into one CommonJS file
 *      whose module.exports IS the Holidays constructor
 *   4. run it through `rampart -t` -> duktape-safe "noTranspile" bundle
 *   5. run the holiday test suite against the freshly built module and
 *      print the results
 *   6. report "All N tests passed" / "M of N tests failed", then (on a
 *      clean pass) ask whether to replace the module at its full path
 *   7. clean up every temporary artifact, in all cases
 *
 * Usage:
 *   node js_modules/node-update-date-holidays.js [options]
 *     --version X.Y.Z   pin a version instead of npm "latest"
 *     --minify          minify the bundle; --no-minify to keep it readable
 *                       (if neither is given you are asked, before testing)
 *     --force           rebuild even if already up to date
 *     --yes             replace without the interactive prompt (for scripts)
 *     --quiet           less chatter
 *
 * Env overrides: RAMPART_BIN (path to the rampart binary to use).
 */

'use strict';

const fs       = require('fs');
const os       = require('os');
const path     = require('path');
const readline = require('readline');
const { execFileSync } = require('child_process');

const MODULE_NAME  = 'rampart-date-holidays.js';
const NOTRANSPILE  = '"noTranspile";';

/* ---------- args ---------- */
const USAGE =
'Update the vendored rampart-date-holidays.js from npm.\n' +
'\n' +
'Usage: node node-update-date-holidays.js [options]\n' +
'\n' +
'Checks npm for a newer date-holidays release; if there is one, it downloads,\n' +
'bundles, transpiles and tests it, then asks before replacing the module that\n' +
'rampart loads (the copy in process.modulesPath). Nothing is changed without\n' +
'your confirmation, and all temporary files are cleaned up.\n' +
'\n' +
'Options:\n' +
'  --version X.Y.Z   build a specific version instead of npm "latest"\n' +
'  --minify          minify the module; --no-minify keeps it readable\n' +
'                    (if neither is given, you are asked before the build)\n' +
'  --force           rebuild even when already up to date\n' +
'  --yes, -y         replace without the confirmation prompt (for scripts)\n' +
'  --quiet           less output\n' +
'  --help, -h        show this message\n' +
'\n' +
'Env: RAMPART_BIN   path to the rampart binary to use (default: on PATH)\n';

let VERSION = 'latest', FORCE = false, YES = false, QUIET = false, MINIFY = null;
for (let i = 2; i < process.argv.length; i++) {
    const a = process.argv[i];
    if      (a === '--help' || a === '-h') { process.stdout.write(USAGE); process.exit(0); }
    else if (a === '--force')   FORCE = true;
    else if (a === '--yes' || a === '-y') YES = true;
    else if (a === '--quiet')   QUIET = true;
    else if (a === '--minify')    MINIFY = true;
    else if (a === '--no-minify') MINIFY = false;
    else if (a === '--version') VERSION = process.argv[++i];
    else { console.error('unknown arg: ' + a + '\n'); process.stdout.write(USAGE); process.exit(2); }
}

function log(m)  { if (!QUIET) console.log(m); }
function step(m) { if (!QUIET) console.log('\n==> ' + m); }
// Throw (don't exit) so main()'s finally still cleans up the temp dir.
function die(m)  { const e = new Error(m); e.clean = true; throw e; }
function sh(cmd, args, opts) {
    return execFileSync(cmd, args,
        Object.assign({ stdio: ['ignore', 'pipe', 'pipe'] }, opts)).toString();
}
function pretty(n) { return (n / 1048576).toFixed(2) + ' MB'; }

/* Bootstrap a rampart, then let rampart tell us every path we need — no
 * paths are hardcoded here.  Bootstrap order: $RAMPART_BIN, then a `./rampart`
 * in the current directory (so running from a build dir like build/src updates
 * that build's copy), then a bare `rampart` (a PATH lookup).  From whichever
 * runs we read process.installPathExec (the canonical binary) and
 * process.modulesPath. */
function resolveRampart() {
    const cands = [];
    if (process.env.RAMPART_BIN) cands.push(process.env.RAMPART_BIN);
    const local = path.join(process.cwd(), 'rampart');
    if (fs.existsSync(local)) cands.push(local);     // e.g. run from build/src
    cands.push('rampart');
    for (const bin of cands) {
        try {
            const out = execFileSync(bin,
                ['-gc', 'rampart.utils.printf("%s\\n%s", process.installPathExec, process.modulesPath)'],
                { stdio: ['ignore', 'pipe', 'ignore'] }).toString();
            const [exec, modulesPath] = out.split('\n');
            if (modulesPath) return { bin: exec || bin, modulesPath };
        } catch (e) { /* try next */ }
    }
    die('could not run rampart — put it on PATH or set RAMPART_BIN');
}

/* Pull "(v3.26.11)" out of the vendored file's header, if present. */
function installedVersion(file) {
    if (!fs.existsSync(file)) return null;
    const head = fs.readFileSync(file, 'utf8').slice(0, 4096);
    const m = head.match(/date-holidays node module \(v([0-9][0-9A-Za-z.\-]*)\)/);
    return m ? m[1] : null;
}

/* Reuse the existing license/header verbatim (keeps the CC BY-SA + ISC
 * attribution intact); only bump the version.  Fall back to a minimal
 * header if the current module can't be read. */
function buildHeader(file, newVersion) {
    let header = null;
    try {
        const cur = fs.readFileSync(file, 'utf8');
        const open = cur.indexOf('/*'), close = cur.indexOf('*/', open);
        if (open >= 0 && close >= 0) header = cur.slice(open, close + 2);
    } catch (e) { /* fall through to default */ }
    if (!header) {
        header =
            '/*\n' +
            '    This is the date-holidays node module (v' + newVersion + '), bundled with esbuild\n' +
            '    and transpiled for duktape using the rampart transpiler.\n\n' +
            '    https://github.com/commenthol/date-holidays\n\n' +
            '    Holiday data: CC BY-SA 3.0.  All other code: ISC, (c) commenthol.\n' +
            '    *See end of file for licenses of bundled software.\n' +
            '*/';
    }
    return header.replace(/\(v[0-9][0-9A-Za-z.\-]*\)/, '(v' + newVersion + ')');
}

/* The self-contained holiday test suite (a rampart script).  It requires
 * the freshly built module directly — no almanac .so, no network — and
 * mirrors the holiday assertions from almanac-test.js. */
function testScript(modulePath) {
    return NOTRANSPILE + '\n' +
'var Holidays = require(' + JSON.stringify(modulePath) + ');\n' +
'var pass = 0, fail = 0;\n' +
'function t(name, fn) {\n' +
'    var ok = false, err = null;\n' +
'    try { ok = !!fn(); } catch (e) { ok = false; err = e; }\n' +
'    if (ok) { pass++; rampart.utils.printf("  %-46s passed\\n", name); }\n' +
'    else   { fail++; rampart.utils.printf("  %-46s FAILED%s\\n", name, err ? (" (" + err + ")") : ""); }\n' +
'}\n' +
't("constructor works", function(){ return typeof new Holidays() === "object"; });\n' +
't("constructor with country", function(){ return typeof new Holidays("US") === "object"; });\n' +
't("getCountries returns object", function(){ var r = new Holidays().getCountries(); return r && r.US && r.DE && r.FR; });\n' +
't("getStates for US", function(){ var r = new Holidays().getStates("US"); return r && r.CA && r.NY && r.TX; });\n' +
't("getRegions for US/LA", function(){ var r = new Holidays().getRegions("US","LA"); return r && r.NO; });\n' +
't("init and getHolidays", function(){ var h = new Holidays(); h.init("US"); var r = h.getHolidays(2024); return Array.isArray(r) && r.length > 5; });\n' +
't("New Years Day present", function(){ var r = new Holidays("US").getHolidays(2024).filter(function(h){return h.name==="New Year\'s Day";}); return r.length===1 && r[0].date.indexOf("2024-01-01")===0; });\n' +
't("Independence Day present", function(){ var r = new Holidays("US").getHolidays(2024).filter(function(h){return h.name==="Independence Day";}); return r.length===1 && r[0].date.indexOf("2024-07-04")===0; });\n' +
't("Christmas Day present", function(){ var r = new Holidays("US").getHolidays(2024).filter(function(h){return h.name==="Christmas Day";}); return r.length===1 && r[0].date.indexOf("2024-12-25")===0; });\n' +
't("holiday has required fields", function(){ var h = new Holidays("US").getHolidays(2024)[0]; return h.date && h.start && h.end && h.name && h.type; });\n' +
't("isHoliday positive (Jul 4)", function(){ var r = new Holidays("US").isHoliday(new Date("2024-07-04T05:00:00Z")); return r && r[0] && r[0].name==="Independence Day"; });\n' +
't("isHoliday negative (Mar 15)", function(){ return new Holidays("US").isHoliday(new Date("2024-03-15T05:00:00Z")) === false; });\n' +
't("different country (DE)", function(){ var r = new Holidays("DE").getHolidays(2024); return Array.isArray(r) && r.length > 5; });\n' +
't("state level (US/LA/NO Mardi Gras)", function(){ var r = new Holidays("US","LA","NO").getHolidays(2016).filter(function(h){return h.name==="Mardi Gras";}); return r.length===1; });\n' +
't("getLanguages", function(){ var l = new Holidays("US").getLanguages(); return Array.isArray(l) && l.indexOf("en")!==-1; });\n' +
't("setLanguages changes names", function(){ var hd = new Holidays("DE"); hd.setLanguages("en"); var a = hd.getHolidays(2024).filter(function(h){return h.name==="New Year\'s Day";}); hd.setLanguages("de"); var b = hd.getHolidays(2024).filter(function(h){return h.name==="Neujahr";}); return a.length===1 && b.length===1; });\n' +
't("getTimezones", function(){ var tz = new Holidays("US").getTimezones(); return Array.isArray(tz) && tz.length>0; });\n' +
't("setTimezone", function(){ var hd = new Holidays("US"); hd.setTimezone("America/New_York"); var r = hd.getHolidays(2024); var s = (typeof r[0].start==="string")?r[0].start:r[0].start.toISOString(); return r.length>0 && s.indexOf("T05:00:00")!==-1; });\n' +
't("getDayOff", function(){ return new Holidays("US").getDayOff()==="sunday"; });\n' +
't("setHoliday custom", function(){ var hd = new Holidays("US"); hd.setHoliday("03-15",{name:"Company Day",type:"observance"}); var r = hd.getHolidays(2024).filter(function(h){return h.name==="Company Day";}); return r.length===1 && r[0].date.indexOf("2024-03-15")===0; });\n' +
't("init reinitializes", function(){ var hd = new Holidays("US"); var us = hd.getHolidays(2024).map(function(h){return h.name;}); hd.init("DE"); var de = hd.getHolidays(2024).map(function(h){return h.name;}); return us.indexOf("Independence Day")!==-1 && de.indexOf("Independence Day")===-1; });\n' +
'rampart.utils.printf("\\n__RESULT__ pass=%d fail=%d\\n", pass, fail);\n';
}

function ask(question) {
    return new Promise(function (resolve) {
        const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
        rl.question(question, function (answer) { rl.close(); resolve(answer.trim()); });
    });
}

/* ---------- main ---------- */
async function main() {
    const { bin: RAMPART, modulesPath } = resolveRampart();
    const target = path.join(modulesPath, MODULE_NAME);
    log('rampart binary : ' + RAMPART);
    log('modulesPath    : ' + modulesPath);
    log('target module  : ' + target);

    /* 2. version check BEFORE building anything --------------------------- */
    step('checking versions');
    const current = installedVersion(target);
    let latest;
    try { latest = sh('npm', ['view', 'date-holidays' + (VERSION === 'latest' ? '' : '@' + VERSION), 'version']).trim().split('\n').pop(); }
    catch (e) { die('could not query npm for date-holidays version: ' + (e.stderr ? e.stderr.toString().trim() : e.message)); }
    const wanted = (VERSION === 'latest') ? latest : VERSION;
    log('  installed: ' + (current || '(none)') + '   available: ' + wanted);

    if (!FORCE && current && current === wanted) {
        log('\nrampart-date-holidays module is up to date (v' + current + ').');
        return;
    }

    /* decide minify up front, so the artifact we test is the one we ship -- */
    let minify = MINIFY;
    if (minify === null) {
        if (process.stdin.isTTY) {
            const ans = await ask('Minify the bundle (smaller file, less readable)? [y/N] ');
            minify = /^y(es)?$/i.test(ans);
        } else {
            minify = false;
            log('(non-interactive; not minifying — pass --minify to force)');
        }
    }
    log('minify: ' + (minify ? 'yes' : 'no'));

    /* everything below happens inside a temp dir we always clean up ------- */
    const work = fs.mkdtempSync(path.join(os.tmpdir(), 'dh-vendor-'));
    try {
        /* 3. install + bundle --------------------------------------------- */
        step('installing date-holidays@' + wanted + ' and esbuild (npm)');
        fs.writeFileSync(path.join(work, 'package.json'),
            JSON.stringify({ name: 'dh-vendor', version: '0.0.0', private: true }) + '\n');
        sh('npm', ['install', '--no-audit', '--no-fund', '--loglevel=error',
                   'date-holidays@' + wanted, 'esbuild'], { cwd: work });
        const resolved = JSON.parse(fs.readFileSync(
            path.join(work, 'node_modules', 'date-holidays', 'package.json'), 'utf8')).version;

        step('bundling with esbuild');
        const entryPath  = path.join(work, 'entry.js');
        const bundlePath = path.join(work, 'bundle.js');
        fs.writeFileSync(entryPath, "module.exports = require('date-holidays');\n");
        const esbuild = require(path.join(work, 'node_modules', 'esbuild'));
        // Always bundle readable here: `rampart -t` is pathologically slow on
        // minified, megabyte-long lines.  Minification (if requested) happens
        // AFTER transpile, on the ES5 output, below.
        esbuild.buildSync({
            entryPoints: [entryPath], bundle: true, platform: 'node',
            format: 'cjs', target: 'esnext', legalComments: 'eof',
            outfile: bundlePath, logLevel: 'silent',
        });
        log('  bundle: ' + pretty(fs.statSync(bundlePath).size));

        /* 4. transpile (+ smoke test) ------------------------------------- *
         * Drive transpilation with a "use transpilerGlobally" directive on
         * the loader rather than `-t`: the bare `-t` flag forces
         * functionSources ON (it takes the global-fn-sources default of 1),
         * which embeds every function's ORIGINAL source — inside template
         * literals — bloating the file ~40% AND producing nested quoting
         * that duktape's parser rejects once minified.  functionSources:false
         * drops all that; nothing here needs Function.prototype.toString to
         * return original source.  The directive transpiles the whole require
         * closure, so the required bundle is transpiled the same way and its
         * bundle.transpiled.js cache is written next to it.                 */
        step('transpiling (functionSources off)');
        fs.writeFileSync(path.join(work, 'loader.js'),
            '"use transpilerGlobally: {functionSources:false}";\n' +
            "var H = require('./bundle.js');\n" +
            "var us = new H('US').getHolidays(2024);\n" +
            "rampart.utils.printf('SMOKE ctor=%s US2024=%d\\n', typeof H, us.length);\n");
        const smoke = sh(RAMPART, ['loader.js'], { cwd: work });
        const sm = smoke.match(/SMOKE ctor=function US2024=(\d+)/);
        if (!sm || parseInt(sm[1], 10) <= 0) die('smoke test failed: ' + smoke.trim());
        const transpiled = path.join(work, 'bundle.transpiled.js');
        if (!fs.existsSync(transpiled)) die('transpiler produced no output (bundle.transpiled.js)');

        /* assemble the vendored artifact ---------------------------------- */
        let body = fs.readFileSync(transpiled, 'utf8');
        if (body.startsWith(NOTRANSPILE)) body = body.slice(NOTRANSPILE.length);
        if (minify) {
            step('minifying transpiled output');
            // Whitespace + identifier minify only; minifySyntax stays OFF so no
            // ES2015 syntax (e.g. arrow functions) is re-introduced into the
            // ES5 output duktape must run.  legalComments:eof keeps the bundled
            // license attribution block at the end of the file.
            const before = Buffer.byteLength(body);
            body = esbuild.transformSync(body, {
                loader: 'js', minifyWhitespace: true, minifyIdentifiers: true,
                minifySyntax: false, legalComments: 'eof',
            }).code;
            // No need to hand-check syntax: the test run below loads the module
            // natively under duktape (noTranspile), which rejects any ES2015
            // syntax outright — so a bad minify fails the tests and never ships.
            log('  ' + pretty(before) + ' -> ' + pretty(Buffer.byteLength(body)));
        }
        const candidate = path.join(work, MODULE_NAME);
        fs.writeFileSync(candidate,
            NOTRANSPILE + '\n' + buildHeader(target, resolved) + '\n' + body.replace(/^\s*\n/, ''));
        log('  built v' + resolved + (minify ? ' (minified)' : '') +
            '  (' + pretty(fs.statSync(candidate).size) + ')');

        /* 5. run the holiday test suite against the candidate ------------- */
        step('testing the rebuilt module');
        fs.writeFileSync(path.join(work, 'dh-test.js'), testScript(candidate));
        let out;
        try { out = sh(RAMPART, ['dh-test.js'], { cwd: work }); }
        catch (e) { out = (e.stdout ? e.stdout.toString() : '') + (e.stderr ? e.stderr.toString() : ''); }
        // show the per-test lines (everything except the machine-readable result line)
        out.split('\n').filter(function (l) { return l && l.indexOf('__RESULT__') < 0; })
           .forEach(function (l) { log(l); });

        const rm = out.match(/__RESULT__ pass=(\d+) fail=(\d+)/);
        const passed = rm ? parseInt(rm[1], 10) : 0;
        const failed = rm ? parseInt(rm[2], 10) : -1;
        const total  = passed + (failed > 0 ? failed : 0);

        /* 6. verdict + prompt --------------------------------------------- */
        if (failed !== 0) {
            console.log('\n' + (failed > 0 ? (failed + ' of ' + total + ' tests failed') : 'test run did not complete') +
                        ' — the module will NOT be replaced.');
            process.exitCode = 1;
            return;
        }
        console.log('\nAll ' + passed + ' tests passed.');

        let replace = YES;
        if (!replace) {
            if (!process.stdin.isTTY) {
                log('(non-interactive; run with --yes to replace) — leaving the current module in place.');
            } else {
                const ans = await ask('Replace the current module at\n  ' + target + '\nwith the new v' + resolved + '? [y/N] ');
                replace = /^y(es)?$/i.test(ans);
            }
        }
        if (replace) {
            const tmp = target + '.new';
            fs.copyFileSync(candidate, tmp);
            fs.renameSync(tmp, target);          // atomic swap, no backup file left behind
            console.log('Replaced ' + target + ' with date-holidays v' + resolved + '.');
        } else {
            console.log('Left the current module unchanged.');
        }
    } finally {
        fs.rmSync(work, { recursive: true, force: true });   // 7. always clean up
    }
}

main().catch(function (e) {
    if (e && e.clean) console.error('ERROR: ' + e.message);
    else console.error(e && e.stack ? e.stack : e);
    process.exit(1);
});
