/*   **************  RAMPART TEXT CONVERTER **************** */
/*
    The text converter uses several GNU licensed utilities to 
    convert various document formats to plain text.

    The following programs/modules should be installed and available before usage:

        * pandoc - for docx, odt, markdown, rtf, latex, epub and docbook
        * catdoc (linux/freebsd) or textutil (macos) - for doc
        * pdftotext from the xpdf utils - for pdfs
        * man - for man files (if not available, pandoc will be used)
        * file - to identify file types
        * head - for linux optimization identifying files
        * gunzip - to decompress any gzipped document
        * the rampart-html module for html and as a helper for pandoc conversions

    Minimally, pandoc and file must be available for this module to load.

    The following file formats are supported (if appropriate program
    above is available):

        docx, doc, odt, markdown, rtf, latex, epub, docbook, pdf & man
        Also files identified as txt (text/plain) will be returned as is.

    Usage:

    var converter = require("rampart-converter.js");
    var convert = new converter(defaultOptions);

    where "defaultOptions" is Undefined or an Object of command line flags for each
    of the converting programs.  Example to only include the first two pages
    for a pdf (pdftotext) and to convert a docx (pandoc) to markdown instead
    of to text:

    var convert = new converter({
        pdftotext: {f:1, l:2},
        pandoc :   {t: 'markdown'}
    });

    To convert a document:

    var txt = convertFile('/path/to/my/file.ext', options);
        or
    var txt = convert(myFileBufferOrString, options);

    where "options' overrides the defaultOptions above and 
    is either of:

        1) same format as defaultOptions above - 
           {pdftotext: {f:1, l:2}}
                  or
        2) options for the utility to be used:
           {f:1, l:2}

    Full example:

        var converter=require('rampart-converter.js');

        // options optionally as defaults
        //var c = new convert({
        //    pandoc : { 't': 'markdown' },
        //    pdftotext: {f:1, l:2}
        //});

        var convert = new converter();

        //options per invocation
        var ptxt = convert.convertFile('convtest/test.pdf', {pdftotext: {f:1, l:2}});
        var dtxt = convert.convertFile('convtest/test.docx', { pandoc : { 't': 'markdown' }});

        // OR - alternative format for options:
        var ptxt = convert.convertFile('convtest/test.pdf', {f:1, l:2});
        var dtxt = convert.convertFile('convtest/test.docx', { 't': 'markdown' });

        rampart.utils.printf("%s\n\n%s\n", ptxt, dtxt);

    Command Line usage:

        > rampart /path/to/rampart-converter.js /path/to/document.ext

*/

var html=require("rampart-html");

// utils we will need
var exec      = rampart.utils.exec,
    shell     = rampart.utils.shell,
    printf    = rampart.utils.printf, 
    getType   = rampart.utils.getType,
    readFile  = rampart.utils.readFile,
    rmFile    = rampart.utils.rmFile,
    sprintf   = rampart.utils.sprintf,
    fprintf   = rampart.utils.fprintf;

var convapps = ['pandoc','pdftotext','catdoc','man'];

var extapps = {
    catdoc    : exec("which","catdoc").stdout.trim(),
    textutil  : exec("which","textutil").stdout.trim(),
    pandoc    : exec("which","pandoc").stdout.trim(),
    pdftotext : exec("which","pdftotext").stdout.trim(),
    file      : exec("which","file").stdout.trim(),
    gunzip    : exec("which","gunzip").stdout.trim(),
    head      : exec("which","head").stdout.trim(),
    man       : exec("which","man").stdout.trim()
}

var docApp = 'catdoc';
var platform='linux';
//macos converts .doc files using textutil
if(/Darwin/.test(rampart.buildPlatform)) {
    docApp='textutil'
    platform='macos';
} else if (/FreeBSD/.test(rampart.buildPlatform)) {
    platform='freebsd'
}

//from = -f for pandoc, also used for extension for temp files
var textMimeMap = {
    "text/html"             : {app:'internal', from:'html'},
    "text/plain"            : {app:'internal', from:'text'},
    'application/msword'    : {app:docApp, from:'doc'},
    'application/pdf'       : {app:'pdftotext', from:'pdf'},
    'application/vnd.openxmlformats-officedocument.wordprocessingml.document'
                            : {app:'pandoc', from: 'docx'},
    'application/vnd.oasis.opendocument.text'
                            : {app:'pandoc', from: 'odt'},
    'text/rtf'              : {app:'pandoc', from: 'rtf'},
    'text/x-tex'            : {app:'pandoc', from: 'latex'},
    'application/epub+zip'  : {app:'pandoc', from: 'epub'},
    'text/xml'              : {app:'pandoc', from: 'docbook'},
    'text/markdown'         : {app:'pandoc', from: 'markdown'},
    'text/troff'            : {app:'man', from:'1'}
}

for (var key in extapps)
    if(!extapps[key].length)
        delete extapps[key];

if(!extapps.file)
    throw new Error("convert: command line utility 'file' not found");

if(!extapps.pandoc)
    throw new Error("convert: command line utility 'pandoc' not found");

if(!extapps.man)
    textMimeMap['text/troff']= {app:'pandoc', from: 'man'};

var pandocInputTypes = exec(extapps.pandoc,'--list-input-formats').stdout.split(/[\s\n]+/gm);

if(pandocInputTypes[pandocInputTypes.length-1]=='')
    pandocInputTypes.length--;

for (var k in textMimeMap) {
    var m = textMimeMap[k];
    if( m.app=='pandoc' && ! pandocInputTypes.includes(m.from) )
        m.unsupported=true;
}

var aptopts={};

function getHelpOutput(command) {
    var res = exec(command, '-h');

    if(command == extapps.pdftotext && res.exitStatus==99) //on macos
        res.exitStatus=0;

    if(res.exitStatus)
        return null;

    if(res.stderr.length > res.stdout.length)
        return res.stderr;

    return res.stdout;
}

function getopts() {
    var res, manopts, pdopts, cdopts, p2topts, tuopts;
    try {
        res = getHelpOutput(extapps.pandoc);
        if(!res)
            throw new Error("could not parse pandoc command line options");
        pdopts= res.match(/\-[^ \=\[]+/g);
        for (var i=0; i<pdopts.length; i++) {
            pdopts[i]=pdopts[i].replace(/^[\-]+/g,'').replace(/\n+/g,'');
        }
    } catch(e) { throw new Error("could not parse pandoc command line options"); }
    aptopts.pandoc=pdopts;

    if(extapps.catdoc)
        aptopts.catdoc = ['v', 'u', '8', 'b', 't', 'a', 'w', 'x', 'l', 'V', 'm', 's', 'd', 'f'];

    if(extapps.pdftotext)
    {
        try {
            res = getHelpOutput(extapps.pdftotext);

            if(!res)
                throw new Error("could not parse pdftotext command line options");
            p2topts= res.match(/\-[^ \=\[]+/g);
            for (var i=0; i<p2topts.length; i++) {
                p2topts[i]=p2topts[i].replace(/^[\-]+/g,'').replace(/\n+/g,'');
            }
        } catch(e) { throw new Error("could not parse pdftotext command line options"); }
        aptopts.pdftotext=p2topts;
    }

    if(extapps.textutil) {
        try {
            res = getHelpOutput(extapps.textutil);
            if(!res)
                throw new Error("could not parse textutil command line options");
            tuopts = res.match(/\-[a-zA-Z0-9]+/g);
            for (var i=0; i<tuopts.length; i++) {
                tuopts[i]=tuopts[i].replace(/^[\-]+/g,'').replace(/\n+/g,'');
            }
        } catch(e) { throw new Error("2 could not parse textutil command line options"); }
        aptopts.textutil=tuopts;
    }

    if(extapps.man) {
        /* parsing man opts is difficult between platforms and is not needed
        try {
            res = getHelpOutput(extapps.man);
            if(!res)
                throw new Error("could not parse man command line options");
            manopts = res.match(/\-{1,2}[a-zA-Z0-9]+/g);
            for (var i=0; i<manopts.length; i++) {
                manopts[i]=manopts[i].replace(/[\-\n]+/g,'');
            }
        } catch(e) { throw new Error("2 could not parse man command line options"); }
        */
        aptopts.man=[];
    }
}
getopts();

function optsToArray(opts, cmd) {

    if(getType(opts) != "Object")
        return [];

    var keys=Object.keys(opts),
        l=keys.length,
        ret=[];
    for(var i=0; i<l; i++) {
        var opt=keys[i], val=opts[opt];
        // for opts without values - ie {number-sections:true}
        if(getType(val) == 'Boolean') {
            if(val) {
                if(opt.charAt(0)=='-')
                    ret.push(opt);
                else //pandoc and man long opts need "--"
                    ret.push( ( (cmd=='pandoc' || cmd=='man') && opt.length>1?'--':'-') + opt );
            }

        // opts with values for pandoc and man are "-o val" or "--opt=val"
        } else if (cmd=='pandoc' || cmd=='man') {

            //substitute to and write with -t for simplicity in check in pandoc_exec below
            if(cmd=='pandoc' && (opt == 'write' || opt == 'to'))
                opt='t';

            if(opt.length==1) {
                ret.push('-' + opt );
                ret.push( sprintf('%s',val) );
            } else {
                ret.push(sprintf("--%s=%s",opt,val));
            }

        // opts with values for others just have "-opt val"
        } else {
            ret.push('-' + opt );
            ret.push( sprintf('%s',val) );
        }
    }
    return ret;
}

function doexec(opts,inputf) {
    var tmpfile, res;
    if(inputf) {
        if(platform != 'linux') {
            tmpfile = `/tmp/rp_tmpfile-${process.getpid()}-${rampart.thread.getCurrentId()}.${inputf.ext}`
            fprintf(tmpfile, '%s', inputf.contents);
            for (var i=0;i<opts.length;i++) {
                if(opts[i]=='-') {
                    opts[i]=tmpfile;
                    break;
                }
            }
            if(i==opts.length)
                opts.push(tmpfile);
        } else {
            opts.push({stdin:inputf.contents});
        }
    }

    try {
        res = exec.apply(null, opts);
    } catch(e) {
        if(tmpfile)
            rmFile(tmpfile);
        throw(e);
    }

    if(tmpfile)
        rmFile(tmpfile);

    if(res.exitStatus)
        throw new Error(sprintf("%s returned exitCode %d - %s - %s", opts[0], res.exitStatus, res.stderr,res.stdout) );

    return res.stdout;
}

function markdownProbability(text) {
  var markdownPatterns = [
    { pattern: /^#{1,6}\s.+/m, weight: 2 },           // Headings
    { pattern: /!\[.*?\]\(.*?\)/, weight: 5 },        // Images
    { pattern: /\[.*?\]\(.*?\)/, weight: 4 },         // Links
    { pattern: /`{1,3}[\s\S]*?`{1,3}/, weight: 1 },   // Code
    { pattern: /\*\*.*?\*\*/, weight: 1 },            // Bold text
    { pattern: /__.*?__/, weight: 1 },                // Bold text alternative
    { pattern: /\*[^*\n]*\*/, weight: 0.5 },          // Italic text
    { pattern: /_[^_\n]*_/, weight: 0.5 },            // Italic text alternative
    { pattern: /^>.+/m, weight: 1 },                  // Blockquotes
    { pattern: /^[\*\-+]\s+.+/m, weight: 1 },         // Unordered lists
    { pattern: /^[0-9]+\.\s+.+/m, weight: 1 },        // Ordered lists
    { pattern: /^-{3,}$/m, weight: 1 },               // Horizontal rules
    { pattern: /^```[\s\S]*?^```/m, weight: 2 },      // Fenced code blocks
  ];

  var matchedWeight = 0;
  var totalWeight = 0;

  for (var i = 0; i < markdownPatterns.length; i++) {
    var pattern = markdownPatterns[i].pattern, weight = markdownPatterns[i].weight;
    totalWeight += weight;
    if (pattern.test(text)) {
      matchedWeight += weight;
    }
  }

  return matchedWeight / totalWeight;
}

function getFileType(file) {
    var exarr, stdin, mime, res, inputf;
    if(file.file) {
        exarr = [ extapps.file, '-b', '--mime-type', '-'];
        inputf={contents:file.file, ext:'tmp'}
    } else {
        exarr = [ extapps.file, '-b', '--mime-type', file.filename ];
    }

    mime=doexec(exarr,inputf).trim();
    if(mime == 'application/gzip') {
        if( (platform=='linux' && !extapps.head) || !extapps.gunzip)
        {
            throw new Error(
                sprintf
                (
                    "convert: cannot unzip file '%s', command line utilities not found:%s%s", 
                    file.filename, 
                    extapps.head  ?'':' head',
                    extapps.gunzip  ?'':' gunzip'
                )
            );
        }
        //first do a quick check that the uncompressed file type is supported
        if(platform!='linux') {
            //gunzip on macos insists on the whole file
            if(file.file)
                res = shell(`${extapps.gunzip} -c -k | ${extapps.file} -b --mime-type -`, {stdin:file.file});
            else
                res = shell(`${extapps.gunzip} -c -k ${file.filename} | ${extapps.file} -b --mime-type -`);
        } else {
            if(file.file)
                res = shell(`${extapps.head} -c 2048 | ${extapps.gunzip} -c -k | ${extapps.file} -b --mime-type -`, {stdin:file.file});
            else
                res = shell(`${extapps.head} -c 2048 ${file.filename} | ${extapps.gunzip} -c -k | ${extapps.file} -b --mime-type -`);
        }
        if(res.exitStatus)
            throw new Error(`Error decompressing file '${file.filename}' while checking type`);

        mime=res.stdout.trim();

        // now can we handle that mime?
        if(!textMimeMap[mime] || textMimeMap[mime].unsupported )
            throw new Error(`convert: mime type '${mime}' of compressed file is not supported`);

        if(file.file) {
            file.file = doexec([extapps.gunzip, '-c', '-k'], {contents:file.file,ext:'gz'});
        } else {
            file.file = doexec([extapps.gunzip, '-c', '-k', file.filename]);
            file.origFilename=file.filename;
            file.filename='-';
        }
        /* skip temp file.  text shouldn't be so large that it will cause problems (hopefully)
        } else {
            var ext = file.filename.match(/([^\.]+)\.gz/);
            if(ext.length>1)
                ext='.' + ext[1];
            else
                ext='';

            var tmpfile = `tmpfile-${process.getpid()}-${rampart.thread.getCurrentId()}${ext}`
            res=shell(`${extapps.zcat} ${file.filename} > ${tmpfile}`);
            if(res.exitStatus)
                throw new Error(`Error decompressing file '${file.filename}' in preperation for conversion`);
            file.filename=tmpfile;
            file.isTempFile=true;
        }
        */
        return getFileType(file);
    }

    //check for markdown, if we have filename, check extension.
    if(mime == 'text/plain') {
        if( 
            file.filename.match(/\.markdown$/) || file.filename.match(/\.md$/) ||
            (file.origFilename && ( file.origFilename.match(/\.markdown.gz$/) || file.origFilename.match(/\.md.gz$/) ))
        )
            mime = 'text/markdown';
        else if (file.file) {
            var f=file.file;
            if(getType(f) == 'Buffer')
                f=sprintf('%s',f);
            var prob = markdownProbability(f);
            if(prob > 0.3)
                mime = 'text/markdown';
        }
    }
    if(!textMimeMap[mime] || textMimeMap[mime].unsupported )
        throw new Error(`convert: mime type '${mime}' is not supported`);

    return mime;
}

/* given a filename or a string/buffer of file contents, 
 * return object with contents or filename:
 *   file contents -> { file: infile, filename: '-' }
 *   file name     -> { file: undefined, filename: infile}
 */
function getFile(infile, type) {
    var filename='-';
    var file, ftype, ret;
    var intype = getType(infile);
    // if already in proper format
    if(intype=='Object' && infile.filename)
    {
        if( !infile.file && filename != '-')
            throw new Error("convert: input must be a file (Buffer or String) or a filename (String)");
        if(!infile.mime)
            infile.mime=getFileType(infile);
        return(infile);
    }
    if(intype == "String")
    {
        if(type=='file'|| type=='filename') {
            filename=infile;
        } else if (type=='memory' || type =='contents' || type=='content') {
            file=infile;
        } else {
            //auto check type:
            var len = infile.length;
            if(len <= 4096)
            {
                //check for newlines - it might be a text file starting with / or ./
                if( infile.indexOf('\n') != -1)
                    //treat it as file data
                    file=infile;
                //check for '/' or './' or '../', and if so, treate as file name
                else if(
                    ( infile.charAt(0) == '/' ) || 
                    ( infile.charAt(0)=='.' && infile.charAt(1) == '/' ) ||
                    ( infile.charAt(0)=='.' && infile.charAt(1) == '.' && infile.charAt(2) == '/')
                )
                    filename=infile;
                // if under filename limit, stat to see if it exists
                else if (infile.length <= 256) {
                    if(rampart.utils.stat(infile))
                        filename=infile;
                    else
                        file=infile;
                } else
                    file=infile;
            } else file=infile;
        }
    } else if (intype=='Buffer') 
        file=infile;
    else
        throw new Error("convert: input must be a file (Buffer or String) or a filename (String)");

    ret = { file: file, filename: filename };

    ret.mime = getFileType(ret);

    return ret;
}

var toTextDefaults = {
    //concatenate: true,
    metaDescription: true,
    metaKeywords:    true,
    titleText:       true,
    aLinks:          true,
//    imgLinks:        true
    imgAltText:      true
} 

function htmlToText(ht,opts) {
    var ret, doc = html.newDocument(ht);

    if(!opts) opts=toTextDefaults;

    ret = doc.toText(opts)[0];

    doc.destroy();
    return ret;
}

function internal_exec(file, optarr, mm) {
    var from = mm ? mm.from : false;
    var infile;
    if(file.filename == '-')
        infile=file.file;
    else
        infile=readFile(file.filename);

    if(from=='html')
        return htmlToText(infile);
    // else is already text
    return sprintf('%s',infile);
}

function pandoc_exec(file, optarr, mm) {
    var tohtml=false, inputf;
    var from = mm ? mm.from : false;
    if(!optarr)
        optarr=[];
    optarr.unshift(extapps.pandoc);

    if(from) {
        optarr.push("-f");
        optarr.push(from);
    }
    // unfortunately I know of no way to turn off columns in "--to=plain" mode.  Easiest
    // thing to do is export to html and extract text using rampart-html
    if( !optarr.includes('-t') )
    {
        optarr.push("-t");
        optarr.push("html");
        tohtml=true;
    }
    optarr.push(file.filename);
    if(file.filename == '-') {
        inputf = {contents:file.file, ext:mm.from}
    }
    var ret = doexec(optarr,inputf);

    if(tohtml)
        ret=htmlToText(ret,{});

    return ret;
}

function pdftotext_exec(file, optarr) {
    var inputf;
    if(!optarr)
        optarr=[];

    optarr.unshift(extapps.pdftotext);
    if(file.filename == '-')
    {
        inputf={contents:file.file, ext:'pdf'}    
    }
    optarr.push(file.filename);

    optarr.push('-');
    return doexec(optarr,inputf);
}

function mantotext_exec(file, optarr) {
    var inputf;
    if(!optarr)
        optarr=[];

    optarr.unshift(extapps.man);

    if(file.filename == '-') {
        inputf={contents:file.file, ext:'.1'}
        if(platform == 'linux') {
            optarr.push('-l');
        }
        optarr.push('-');
    }
    else if (file.filename.indexOf('/')==-1)
        optarr.push("./"+file.filename);
    else
        optarr.push(file.filename);

    var ret = doexec(optarr,inputf);

    return ret;
}

function catdoc_exec(file, optarr) {
    if(!optarr)
        optarr=[];

    optarr.unshift(extapps.catdoc);

    if(file.filename == '-')
        optarr.push({stdin:file.file});
    else
        optarr.push(file.filename);

    return doexec(optarr);
}

function textutil_exec(file, optarr) {
    if(!optarr)
        optarr=[];

    optarr.unshift(extapps.textutil);
    optarr.push("-stdout");
    if( !optarr.includes('-convert') )
    {
        optarr.push("-convert");
        optarr.push("txt");
    }

    if(file.filename == '-')
    {
        optarr.push("-stdin");
        optarr.push({stdin:file.file});
    } else
        optarr.push(file.filename);

    return doexec(optarr);
}

var toTextExecFn = {};

//pandoc is required
toTextExecFn.pandoc = pandoc_exec;

if(extapps.pdftotext) {
    toTextExecFn.pdftotext = pdftotext_exec;
} else {
    toTextExecFn.pdftotext = function() {
        throw new Error("conversion requires that the xpdf tool pdftotext be installed");
    }
}

if(extapps.textutil) {
    toTextExecFn.textutil = textutil_exec;
} else {
    toTextExecFn.textutil = function() {
        throw new Error("conversion requires that the textutil command line utility be installed");
    }
}

if(extapps.catdoc) {
    toTextExecFn.catdoc = catdoc_exec;
} else {
    toTextExecFn.catdoc = function() {
        throw new Error("conversion requires that the catdoc command line utility be installed");
    }
}

toTextExecFn.man = mantotext_exec;
toTextExecFn.internal = internal_exec;

var builtinDefaults = {
    pdftotext: {
        enc:'UTF-8'
    }
}


function _convert(fileNameOrCont, opts, defaults, type) {
    if(!opts) opts={};

    var fullopts = opts;
    var mimemap, optarr;
    var file=getFile(fileNameOrCont,type);
    mimemap = textMimeMap[file.mime];

    if(defaults[mimemap.app]) {
        if(opts[mimemap.app])
            opts = opts[mimemap.app];
        fullopts = rampart.utils.deepCopy({}, defaults[mimemap.app], opts);
    } else {
        if(opts[mimemap.app])
            fullopts=opts[mimemap.app];
    }
    optarr = optsToArray(fullopts, mimemap.app);

    return toTextExecFn[mimemap.app](file, optarr, mimemap);
}

function checkopts(opts) {
    var i=0, j, keys = Object.keys(opts);

    for(;i<keys.length;i++) {
        var k=keys[i];
        if(!convapps.includes(k))
            throw new Error(sprintf("Options for command line converter '%s' not supported (must be one of %J)\n", k, convapps));

        var validOpts = aptopts[k];
        if(!validOpts)
            throw new Error(sprintf("Options for command line converter '%s' not supported (command line util missing)",k));

        var v=opts[k];
        if(getType(v) != 'Object')
            throw new Error(sprintf("Option '%s' must be an Object (key/vals for command line util '%s')",k,k));
        optKeys=Object.keys(v);

        for(j=0;j<optKeys.length;j++){
            //remove '\-+' from key
            var opt = optKeys[j].replace(/^\-+/,'');
            // replace --opt with opt
            if(opt != optKeys[j]) {
                v[opt]=v[optKeys[j]];
                delete v[optKeys[j]];
            }

            // validOpts.length : skip man check
            if(validOpts.length && !validOpts.includes(opt))
                throw new Error(sprintf("%s-%s is not a valid option for command line util '%s'",opt.length>1?'-':'',opt, k));
        }
    }
}

function Converter(defaultOpts) {

    if(getType(defaultOpts) == 'Object')
        defaultOpts = rampart.utils.deepCopy({}, builtinDefaults, defaultOpts);
    else
        defaultOpts = rampart.utils.deepCopy({}, builtinDefaults);

    checkopts(defaultOpts);
    this.opts=defaultOpts;
    this.convert = function (data, extraOpts) {
        return _convert(data, extraOpts, this.opts, 'memory');
    }
    this.convertFile = function (filename, extraOpts) {
        return _convert(filename, extraOpts, this.opts, 'file');
    }
}

if(module && module.exports)
    module.exports=Converter;
else {

    var args = process.argv;

    var converter = new Converter();

    //var f=rampart.utils.readFile(args[2]);
    //var res = converter.convert(f);
    var res=converter.convertFile(args[2]);

    printf("%s", res);
}
