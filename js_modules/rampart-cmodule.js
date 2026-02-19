
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
    var i, ch;

    for (i = start; i < code.length; i++) {
        ch = code[i];
        if (ch === '{') {
            depth++;
        } else if (ch === '}') {
            depth--;
            if (depth === 0) {
                // Found matching closing brace
                var extractedText = code.substring(start, i + 1);
                var leftOver = code.substring(0, start) + code.substring(i + 1);
                var error;
                if(leftOver.indexOf('}')!==-1)
                    error="Unbalanced or extra '}'"
                return { error: error, mainfunc: extractedText, leftover: leftOver };
            }
        }
    }

    // If unbalanced braces
    return { error: "unbalanced '{}'", mainfunc: null, leftover: code };
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
            break;
        }
    }
    return ret;
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

    var sofile = process.scriptPath + '/' + name+'.so';

    if(!support)
        support='';
    if(!libs)
        libs='';

    // check and compile only once per invocation of current script
    if( require.cache[sofile] && require.cache[sofile].exports) {
        return require.cache[sofile].exports;
    }

    if(getType(prog)!='String' || !prog.length)
        throw new Error("cmodule - argument exportFunction cannot be empty");

    var cfile  = process.scriptPath + '/' + name+'.c';
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
        libs = '-L' + process.installPath + ' -lrampart' + (libs ? ' ' + libs : '');

    var compiler = rampart.buildCC || 'cc';
    var eline = sprintf('%s -o %s %s %s %s', compiler, sofile, allflags, cfile, libs);

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
