
var topfmt = `%s
%s
static duk_ret_t _rp_makec_func_%s(duk_context *ctx)
%s`;

var bottomfmt = `
duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_c_function(ctx, _rp_makec_func_%s, DUK_VARARGS);
    return 1;
}
`;

function extractBlock(code) {
    var start = code.indexOf('{');
    if (start === -1) return { extractedText: null, leftOver: code };

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
                return { mainfunc: extractedText, leftover: leftOver };
            }
        }
    }

    // If unbalanced braces
    return { mainfunc: null, leftover: code };
}

function makeCModule(name, prog, support, flags, libs) {
    var stat=rampart.utils.stat, sprintf=rampart.utils.sprintf,
        exec=rampart.utils.exec, getType=rampart.utils.getType; 

    if(getType(name)!='String' || !name.length)
        throw new Error("First argument (Name) cannot be empty");

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
        throw new Error("Program cannot be empty");

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
    var mainfunc = parts.mainfunc[0];
    var leftover = parts.leftover.trim();

    if(!mainfunc || mainfunc.length != 1)
        throw new Error("second argument (mainFunc) must have a single function block '{...}'");


    if(leftover.length)
        throw new Error("second argument (mainFunc) must have a single function block '{...}' and may include '#include ...' lines.\nFound illegal text:\n\"" + leftover +
            "\"\nHint: if that's valid c, put it in the third argument (supportCode)");

    var allincludes = sprintf('%s#include "%s/include/rampart.h"\n', includes, process.installPath);

    var progOut = sprintf(topfmt, allincludes, support, name, prog) + 
                  sprintf(bottomfmt,name,name);

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
                return require(sofile);
            }
        }
        try {
            rampart.utils.rmFile(sofile);
        } catch(e) {
            throw new Error(sprintf("cmodule - couldn't remove file %s: %s", sofile, e.message));
        }
    }

    rampart.utils.fprintf(cfile, '%s', progOut);

    var isapple = !! rampart.buildPlatform.match(/Darwin/i);

    var eline, allflags;
    var baresoname = sofile.split('/').pop();

    if(isapple)
        allflags='-Wall -dynamiclib -Wl,-headerpad_max_install_names -undefined dynamic_lookup -install_name '+baresoname;
    else
        allflags='-Wall -fPIC -shared -Wl,-soname,'+baresoname;

    if(flags)
        allflags += ' '+flags;
    var eline = sprintf('cc -o %s %s %s %s', sofile, allflags, cfile, libs);

    var res = exec("sh", "-c", eline);
    if(res.exitStatus !== 0)
        throw new Error("Failed to build:\n" + res.stderr + res.stdout);

    var ret = require(sofile);
    ret.build=res;

    return ret;
}



module.exports=makeCModule;
