rampart.globalize(rampart.utils);

var args=process.argv
var i=2;


function usage(msg){
    fprintf(stderr, '%s%s', msg?msg:"",
`
Create a c template, makefile and a javascript test file for a new Rampart module.

usage:
    ${args[1]} -h 
        or
    ${args[1]} c_file_name [-f function_args] [-m make_file_name] [-t test_file_name]

where:

    c_file_name     - The c template file to write.

    make_file_name  - The name of the makefile to write (default "Makefile")

    test_file_name  - The name of the JavaScript test file (default c_file_name-test.js)

    function_args   - Create c functions that will be exported to JavaScript. 
                    - May be specified more than once.
                    - Format: cfunc_name:jsfunc_name[:nargs[:input_types]]

    function_args format:

        cfunc_name:  The name of the c function.

        jsfunc_name: The name of the javascript function to export.

        nargs: The number of arguments the javascript can take (-1 for variadic)

        input_types: Require a variable type for javascript options:
            A character for each argument. [n|i|u|s|b|B|o|a|f].
            Corresponding to require 
             [  number|number(as int)|number(as int>-1)|string|
                boolean|buffer|object|array|function             ]

A ready to compile, testable module will be produced if both "nargs" and "input_types" are provided.

`)  ;
    process.exit(1);
}

if(args.length==2)
    usage();

var cfile = sprintf('#include "%s/include/rampart.h"\n\n', process.installPath);
var cfile_name;
var mfile_name = "Makefile";
var tfile_name;
var funcs = [];

while (args[i]) {
    switch(args[i]){
        case '-h':
            usage();
        case "-f":
            i++;
            funcs.push(args[i]);
            break;
        case "-m":
            i++;
            if (args[i] == '-')
                mfile_name=stdout;
            else
                mfile_name=args[i];
            break;
        case "-t":
            i++;
            tfile_name=args[i];
            break;
        default:
            if(cfile_name)
                usage(`unknown option ${args[i]}\n\n`);
            else if (args[i] == '-')
                cfile_name=stdout;
            else if (args[i].charAt(0) == '-')
                usage(`unknown option ${args[i]}\n\n`);
            else
                cfile_name=args[i];
    }
    i++;
}

var modname;


var cfile_alt_name;

if (getType(cfile_name)!='String'){
    cfile_alt_name="myfile.c"
    modname="myfile"
} else {
    if(! /\.c$/.test(cfile_name) )
        cfile_name += '.c'
    modname=cfile_name.replace(/\.[^\.]+$/,'');
    cfile_alt_name=cfile_name;
}

function getresp(def, len) {
    var l = (len)? len: 1;
    var ret = stdin.getchar(l);
    if(ret == '\n')
        return def;
    printf("\n");
    return ret.toLowerCase();
}

if(stat(mfile_name)) {
    printf("file %s exists, overwrite? [y|N]: ", mfile_name);
    fflush(stdout);
    var resp = getresp("n", 1);
    if(resp == 'n') {
        fprintf(stderr, "cannot continue.  Try rerunning with a different makefile name using -m\n");
        process.exit(1);
    }
}

if(cfile_name!=stdout && stat(cfile_name)) {
    printf("file %s exists, overwrite? [y|N]: ", cfile_name);
    fflush(stdout);
    var resp = getresp("n", 1);
    if(resp == 'n') {
        fprintf(stderr, "cannot continue.  Try rerunning with a different name.\n");
        process.exit(1);
    }
}

if(!tfile_name)
    tfile_name=`${modname}-test.js`;

if(stat(tfile_name)) {
    printf("file %s exists, overwrite? [y|N]: ", tfile_name);
    fflush(stdout);
    var resp = getresp("n", 1);
    if(resp == 'n') {
        fprintf(stderr, "cannot continue.  Try rerunning with a different testfile name using -t\n");
        process.exit(1);
    }
}


var tfile=`
rampart.globalize(rampart.utils);

var ${modname} = require("${modname}");

function testFeature(name,test,error)
{
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    printf("testing %-50s - ", name);
    if(test)
        printf("passed\\n")
    else
    {
        printf(">>>>> FAILED <<<<<\\n");
        if(error) printf('%J\\n',error);
        process.exit(1);
    }
    if(error) console.log(error);
}

`;

//[n|i|u|s|b|B|o|a|f]
var js_blanks = {
    n: "10",
    i: "11",
    u: "12",
    s: '"String"',
    b: "true",
    B: 'stringToBuffer("buf")',
    o: '{a:2}',
    a: '[1,2]',
    f: 'function(){}'
}


var initcode=`
/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    /* the return object when var mod=require("${modname}") is called. */
    duk_push_object(ctx);

`;

function makefunction(farg) {
    var fargs = farg.split(':');
    var cfunc=fargs[0];
    var jsfunc=fargs[1];

    var nargs = fargs[2] ? parseInt(fargs[2]):"DUK_VARARGS";
    if (Number.isNaN(nargs))
        usage(`improper format for -f at:\n    ${farg}\n    ${fargs[2]} is not a number\n\n`);

    var argstr = fargs[3]? fargs[3]:"";

    if(!jsfunc || farg.indexOf(' ')!=-1 )
        usage(`improper format for -f at:\n    ${farg}\n\n`);

    var func=`static duk_ret_t ${cfunc}(duk_context *ctx)\n{\n`;

    if(nargs==-1)
        nargs="DUK_VARARGS";

    var nprintargs = (nargs=="DUK_VARARGS")?argstr.length:nargs;

    if( nargs!="DUK_VARARGS" && nargs != nprintargs )
        usage(`too few or too many argument type chars for -f at:\n    ${farg}\n   strlen("${argstr}") != ${nargs}\n`);

    tfile+=`\ntestFeature("${modname}.${jsfunc} basic functionality", function(){
    var lastarg = `;

    var carg;

    var targs=""
    for (var i=0; i<nprintargs; i++) {
        carg = argstr.charAt(i);

        //[n|i|u|s|b|B|o|a|f]
        switch (carg) {
            case 'n':
                func+=`    double js_arg${i+1} = REQUIRE_NUMBER(ctx, ${i}, "${jsfunc}: argument ${i+1} must be a number");\n\n`;
                break;
            case 'i':
                func+=`    int js_arg${i+1} = REQUIRE_INT(ctx, ${i}, "${jsfunc}: argument ${i+1} must be a number");\n\n`;
                break;
            case 'u':
                func+=`    int js_arg${i+1} = REQUIRE_INT(ctx, ${i}, "${jsfunc}: argument ${i+1} must be a positive number");\n\n`;
                break;
            case 's':
                func+=`    const char * js_arg${i+1} = REQUIRE_STRING(ctx, ${i}, "${jsfunc}: argument ${i+1} must be a string");\n\n`;
                break;
            case 'b':
                func+=`    int js_arg${i+1} = REQUIRE_BOOL(ctx, ${i}, "${jsfunc}: argument ${i+1} must be a boolean");\n\n`;
                break;
            case 'B':
                func+=`    void * js_arg${i+1} = REQUIRE_BUFFER_DATA(ctx, ${i}, "${jsfunc}: argument ${i+1} must be a buffer");\n\n`;
                break;
            case 'o':
                func+=`    REQUIRE_PLAIN_OBJECT(ctx, ${i}, "${jsfunc}: argument ${i+1} must be an object");\n    duk_idx_t obj_idx${i+1} = ${i};\n\n`;
                break;
            case 'a':
                func+=`    REQUIRE_ARRAY(ctx, ${i}, "${jsfunc}: argument ${i+1} must be an array");\n    duk_idx_t arr_idx${i+1} = ${i};\n\n`;
                break;
            case 'f':
                func+=`    REQUIRE_FUNCTION(ctx, ${i}, "${jsfunc}: argument ${i+1} must be a function");\n    duk_idx_t func_idx${i+1} = ${i};\n\n`;
                break;
            default:
                usage(`improper format for -f at:\n    ${farg}\n    arg type '${carg}' in "argstr" is unknown type\n`);
        }

        if( i+1 == nprintargs) {
            targs += (i ? ', ' : '') + 'lastarg';
            tfile += `${js_blanks[carg]};\n`;
        }
        else
            targs += (i ? ', ' : '') + js_blanks[carg];
    }

    tfile += `    return lastarg == ${modname}.${jsfunc}(${targs});\n});\n`; 

    func+=`    /* YOUR CODE GOES HERE */\n\n    return 1;\n}\n`

    initcode+=`
    /* js function is mod.${jsfunc} and it calls ${cfunc} */
    duk_push_c_function(ctx, ${cfunc}, ${nargs});
    duk_put_prop_string(ctx, -2, "${jsfunc}");

`   ;
    return func;
}

for (i=0; i<funcs.length; i++) {
    cfile += makefunction(funcs[i]);
}

cfile += initcode + `    return 1;\n}\n`

fprintf(cfile_name, '%s', cfile);

if(funcs.length)
    fprintf(tfile_name, '%s', tfile);
else
    fprintf(stderr, "No functions given, nothing to test. Skipping creation of %s\n", tfile_name);

var makefile = `
CC=cc
OSNAME := \$(shell uname)

CFLAGS=-Wall -g -O2 -std=c99 -I/usr/local/rampart/include 

ifeq (\$(OSNAME), Linux)
	CFLAGS += -fPIC -shared -Wl,-soname,stringfuncs.so
endif
ifeq (\$(OSNAME), Darwin)
	CFLAGS += -dynamiclib -Wl,-headerpad_max_install_names -undefined dynamic_lookup -install_name stringfuncs.so
endif

all: ${modname}.so

${modname}.so: ${cfile_alt_name}
	\$(CC) \$(CFLAGS) -o \$@ \$^

.PHONY: clean

clean:
	rm -f ./*.so

`;
fprintf(mfile_name, '%s', makefile);
