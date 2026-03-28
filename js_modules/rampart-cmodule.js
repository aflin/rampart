
var topfmt = `/* **************** Rampart C Module: %s%*s******************* *\\

Compiled as:
%s

\\* ************************************************************************** */

%s
%s
static duk_ret_t %s(duk_context *ctx)
%s`;

var bottomfmt = `
duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_c_function(ctx, %s, DUK_VARARGS);
    return 1;
}
`;

function wrapCommand(cmd, width, indent) {
    width = width || 80;
    indent = typeof indent === 'string' ? indent : '  ';

    // Normalize whitespace to single spaces
    var parts = cmd.replace(/\s+/g, ' ').trim().split(' ');
    var lines = [];
    var buf = '';
    var first = true;

    function pushLine(final) {
        var prefix = first ? '' : indent;
        lines.push(prefix + buf + (final ? '' : ' \\'));
        first = false;
        buf = '';
    }

    for (var i = 0; i < parts.length; i++) {
        var word = parts[i];
        var prefixLen = first ? 0 : indent.length;
        var nextLen = prefixLen + (buf ? buf.length + 1 : 0) + word.length;

        // If adding this word would exceed width, flush current buffer
        if (buf && nextLen > width) {
            pushLine(false);
            prefixLen = first ? 0 : indent.length; // recompute after flush
        }

        // If the word itself is longer than the width (with prefix), put it alone
        if (!buf && (prefixLen + word.length) > width) {
            buf = word;
            pushLine(false);
            continue;
        }

        buf += (buf ? ' ' : '') + word;
    }

    if (buf) pushLine(true);

    return lines.join('\n');
}


function extractBlock(code) {
    var start = code.indexOf('{');
    if (start === -1) return { mainfunc: null, leftover: code, error: "No initial '{' found" };

    var depth = 0;
    var i = start;
    var len = code.length;

    while (i < len) {
        var ch = code[i];

        // Skip string literals "..." (respecting \" escapes)
        if (ch === '"') {
            i++;
            while (i < len && code[i] !== '"') {
                if (code[i] === '\\') i++; // skip escaped char
                i++;
            }
            i++; // skip closing "
            continue;
        }

        // Skip character literals '...' (respecting \' escapes)
        if (ch === "'") {
            i++;
            while (i < len && code[i] !== "'") {
                if (code[i] === '\\') i++;
                i++;
            }
            i++;
            continue;
        }

        // Skip block comments /* ... */
        if (ch === '/' && i + 1 < len && code[i + 1] === '*') {
            i += 2;
            while (i + 1 < len && !(code[i] === '*' && code[i + 1] === '/')) i++;
            i += 2;
            continue;
        }

        // Skip line comments // ...
        if (ch === '/' && i + 1 < len && code[i + 1] === '/') {
            while (i < len && code[i] !== '\n') i++;
            continue;
        }

        // Count braces only outside strings and comments
        if (ch === '{') {
            depth++;
        } else if (ch === '}') {
            depth--;
            if (depth === 0) {
                var extractedText = code.substring(start, i + 1);
                var leftOver = code.substring(0, start) + code.substring(i + 1);
                var error;
                // Check leftover for unbalanced braces (also skipping strings/comments)
                if (hasBraceOutsideLiterals(leftOver))
                    error = "Unbalanced or extra '}'";
                return { error: error, mainfunc: extractedText, leftover: leftOver };
            }
        }

        i++;
    }

    return { error: "unbalanced '{}'", mainfunc: null, leftover: code };
}

// Check if a string contains } outside of string literals and comments
function hasBraceOutsideLiterals(code) {
    var i = 0, len = code.length;
    while (i < len) {
        var ch = code[i];
        if (ch === '"') {
            i++;
            while (i < len && code[i] !== '"') {
                if (code[i] === '\\') i++;
                i++;
            }
            i++; continue;
        }
        if (ch === "'") {
            i++;
            while (i < len && code[i] !== "'") {
                if (code[i] === '\\') i++;
                i++;
            }
            i++; continue;
        }
        if (ch === '/' && i + 1 < len && code[i + 1] === '*') {
            i += 2;
            while (i + 1 < len && !(code[i] === '*' && code[i + 1] === '/')) i++;
            i += 2; continue;
        }
        if (ch === '/' && i + 1 < len && code[i + 1] === '/') {
            while (i < len && code[i] !== '\n') i++;
            continue;
        }
        if (ch === '}') return true;
        i++;
    }
    return false;
}

function sanitizeToCName(name) {
    // Replace all non-alphanumeric and non-underscore chars with '_'
    var sanitized = name.replace(/[^a-zA-Z0-9_]/g, '_');

    // If it starts with a digit, prefix it with '_'
    if (/^[0-9]/.test(sanitized)) {
        sanitized = '_' + sanitized;
    }

    return sanitized;
}

function findRampartHeader(hint){
    var ret=null;
    var candidates = [
        process.installPath + '/include/rampart.h', // where it should be
        '../../src/include/rampart.h',              // if we are in the github build/src dir
        '/usr/local/rampart/include/rampart.h',     // where it might be if standard install
        '/usr/local/include/rampart.h'              // where it might be if rampart is in /usr/local/bin
    ]
    if(hint)
    {
        if(hint.indexOf('rampart.h') == -1)
            hint = (hint+'/rampart.h').replace('//','/');
        candidates.unshift(hint);
    }

    for (var i=0; i<candidates.length; i++){
        if(rampart.utils.stat(candidates[i])){
            ret = candidates[i];
            // resolve relative paths to absolute so the #include works
            // regardless of where the .c file is written
            if(ret.charAt(0) !== '/')
                ret = rampart.utils.getcwd() + '/' + ret;
            break;
        }
    }
    return ret;
}

/*
   Find the directory of the module that called cmodule().
   Walks the Error stack to find the first caller that isn't
   rampart-cmodule.js itself, then extracts its directory.
   Falls back to process.scriptPath, then /tmp.
*/
function findCallerDir() {
    var stat = rampart.utils.stat;
    var e = new Error("cmodule_caller_trace");
    var lines = e.stack.split('\n');

    for (var i = 1; i < lines.length; i++) {
        /* Match "at funcname (/absolute/path:line)" or "at /absolute/path:line" */
        var match = lines[i].match(/\(?(\/.+?):\d+\)?/);
        if (match) {
            var filepath = match[1];
            /* Skip cmodule itself */
            if (filepath.indexOf('rampart-cmodule') >= 0) continue;
            var dir = filepath.replace(/\/[^/]+$/, '');
            /* Verify we can write there */
            var dirstat = stat(dir);
            if (dirstat && dirstat.writable) return dir;
        }
    }

    /* Fall back to process.scriptPath */
    if (stat(process.scriptPath) && stat(process.scriptPath).writable)
        return process.scriptPath;

    /* Fall back to /tmp */
    if (stat('/tmp') && stat('/tmp').writable)
        return '/tmp';

    throw new Error("cmodule - cannot find a writable directory for compiled module");
}

function makeCModule(name, prog, support, flags, libs, rpHeaderLoc) {
    var stat=rampart.utils.stat, sprintf=rampart.utils.sprintf,
        exec=rampart.utils.exec, getType=rampart.utils.getType;

    if(getType(name) == 'Object') {
        prog        = name.exportFunction;
        support     = name.supportCode;
        flags       = name.compileFlags;
        libs        = name.libraries;
        rpHeaderLoc = name.rpHeaderLoc;
        name        = name.name;
    }

    if(getType(name)!='String' || !name.length)
        throw new Error("cmodule - argument name cannot be empty");

    var rpheader = findRampartHeader(rpHeaderLoc);

    name=sanitizeToCName(name);

    var sofile = findCallerDir() + '/' + name+'.so';

    if(!support)
        support='';
    if(!libs)
        libs='';

    // check and compile only once per invocation of current script
    if( require.cache[sofile] && require.cache[sofile].exports) {
        return require.cache[sofile].exports;
    }

    // If no source provided, try to load an existing .so from disk
    if(!prog || (getType(prog)=='String' && !prog.length)) {
        if(stat(sofile))
            return require(sofile);
        throw new Error("cmodule - no source provided and no cached module found for '" + name + "' at " + sofile);
    }

    if(getType(prog)!='String')
        throw new Error("cmodule - argument exportFunction must be a string");

    var cfile  = findCallerDir() + '/' + name+'.c';
    var incl = prog.match(/\s*#include [^\n]*\s+/mg)
    var includes = "";

    if(incl)
    {
        incl.forEach(function(line){
            includes += line.trim() + '\n';
            prog = prog.replace(line,'');
        });
    }

    var parts = extractBlock(prog);
    var mainfunc = parts.mainfunc;
    var leftover = parts.leftover.trim();

    if(!mainfunc || !mainfunc.length)
        throw new Error("cmodule - argument exportFunction must have a single function block '{...}'"+(parts.error ? ('\n'+parts.error) : '')  );


    if(leftover.length)
        throw new Error("cmodule - argument exportFunction must have a single function block '{...}' and may include '#include ...' lines.\nFound illegal text:\n\"" + leftover +
            "\"\nHint: if that's valid c, put it in the third argument (supportCode)");

    var allincludes = sprintf('%s#include "%s"\n', includes, rpheader);

    // build the compile line
    var isapple = !! rampart.buildPlatform.match(/Darwin/i);
    var iscygwin = !! rampart.buildPlatform.match(/Msys|Cygwin/i);

    var eline, allflags;
    var baresoname = sofile.split('/').pop();

    if(isapple)
        allflags='-Wall -dynamiclib -Wl,-headerpad_max_install_names -undefined dynamic_lookup -install_name '+baresoname;
    else
        allflags='-Wall -fPIC -shared -Wl,-soname,'+baresoname;

    if(flags)
        allflags += ' '+flags;

    // On Cygwin/MSYS2, the PE linker requires all symbols resolved at
    // link time.  Link against librampart.dll.a for duktape API symbols.
    if(iscygwin)
        libs = "-L'" + process.installPathBin + "' -lrampart" + (libs ? ' ' + libs : '');

    var compiler = rampart.buildCC || 'cc';
    var eline = sprintf("%s -o '%s' %s '%s' %s", compiler, sofile, allflags, cfile, libs);

    var barecfile = cfile.split('/').pop();
    var spacelen = 20-barecfile.length;
    if(spacelen < 1) spacelen=1;
    var progOut = sprintf(topfmt, barecfile, spacelen ,'*', wrapCommand(eline), allincludes, support, name, prog) + 
                  sprintf(bottomfmt,name,name);

    // check for existing .so and .c file.  If progOut is the same as 
    // existing .c, skip compile and load the .so
    var estat = stat(sofile);
    if(estat)
    {
        var existing;
        try {
            existing = readFile(cfile);
        } catch(e){}
        
        if(existing)
        {
            var hashe = rampart.utils.hash(existing);
            var hashp = rampart.utils.hash(progOut);
            if(hashe == hashp)
            {
                var ret = require(sofile);
                ret.rampartCmoduleIsCached=true;
                return ret;
            }
        }

        try {
            rampart.utils.rmFile(sofile);
        } catch(e) {
            throw new Error(sprintf("cmodule - couldn't remove file %s: %s", sofile, e.message));
        }
    }

    // write the file
    try {
        rampart.utils.fprintf(cfile, '%s', progOut);
    } catch(e) {
        throw new Error(sprintf("cmodule - error writing file '%s': %s", e.message)); 
    }

    // On MSYS2/Cygwin, the default sh may be Git's bash which lacks the
    // MSYS2 toolchain environment.  Use MSYS2's own bash with -l to get
    // a proper login shell with the right DLL search paths.
    var compilerDir = compiler.lastIndexOf('/') > 0 ? compiler.substring(0, compiler.lastIndexOf('/')) : '';
    var msysBash = compilerDir ? compilerDir + '/bash' : '';
    var res;
    if (msysBash && stat(msysBash))
        res = exec(msysBash, "-l", "-c", "export MSYSTEM=MSYS && " + eline);
    else
        res = exec("sh", "-c", eline);
    if(res.exitStatus !== 0)
        throw new Error("cmodule - failed to build:\n" + res.stderr + res.stdout);

    var ret = require(sofile);
    ret.build=res;
    ret.compileLine = eline;

    return ret;
}



module.exports=makeCModule;
