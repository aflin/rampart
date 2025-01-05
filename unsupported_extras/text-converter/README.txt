*   **************  RAMPART TEXT CONVERTER ****************
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

var converter = require("rampart-converter.js");
var convert = new converter();
var txt = convert.convertFile('/path/to/my/file.ext', options);
    or
var txt = convert.convert(myFileBufferOrString, options);

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

Test in this directory:

    > rampart ./converter-test.js
