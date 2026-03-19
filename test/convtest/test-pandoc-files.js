rampart.globalize(rampart.utils);

var totext = require("rampart-totext");
var testdir = process.scriptPath + "/";
var passed = 0, failed = 0, errors = [];

/* files that contain no extractable text (images only, formulas, empty styled paragraphs, etc.)
   These should produce empty output — if they don't, something is wrong. */
var expect_empty = {
    "pandoc-odt/formula.odt": true,
    "pandoc-odt/hiddenTextByVariable.odt": true,
    "pandoc-odt/imageRelative.odt": true,
    "pandoc-odt/variable.odt": true,
    "pandoc-odt/image.odt": true,
    "pandoc-odt/horizontalRule.odt": true,
};

function testFile(file, expectedType) {
    var fullpath = testdir + file;
    try {
        var id = totext.identify(fullpath);
        if(id !== expectedType) {
            errors.push(file + ": identified as '" + id + "', expected '" + expectedType + "'");
            failed++;
            return;
        }
        var txt = totext.convertFile(fullpath);
        if(typeof txt !== 'string') {
            errors.push(file + ": convert returned " + typeof txt);
            failed++;
            return;
        }

        if(expect_empty[file]) {
            if(txt.length > 0) {
                errors.push(file + ": expected empty output, got " + txt.length + " bytes");
                failed++;
                return;
            }
            passed++;
            return;
        }

        if(txt.length < 1) {
            errors.push(file + ": empty output");
            failed++;
            return;
        }
        /* check for paragraph separation — only flag if document is long enough
           to reasonably expect multiple paragraphs */
        var has_paras = txt.indexOf("\n\n") >= 0 || txt.length < 5000;
        if(!has_paras) {
            errors.push(file + ": no paragraph breaks (len=" + txt.length + ")");
            failed++;
            return;
        }
        passed++;
    } catch(e) {
        errors.push(file + ": EXCEPTION: " + e.message);
        failed++;
    }
}

/* test all pandoc docx files */
var docx_files = readDir(testdir + "pandoc-docx");
for(var i = 0; i < docx_files.length; i++) {
    if(docx_files[i].match(/\.docx$/))
        testFile("pandoc-docx/" + docx_files[i], "docx");
}

/* test all pandoc odt files */
var odt_files = readDir(testdir + "pandoc-odt");
for(var i = 0; i < odt_files.length; i++) {
    if(odt_files[i].match(/\.odt$/))
        testFile("pandoc-odt/" + odt_files[i], "odt");
}

/* test all pandoc pptx files */
var pptx_files = readDir(testdir + "pandoc-pptx");
for(var i = 0; i < pptx_files.length; i++) {
    if(pptx_files[i].match(/\.pptx$/))
        testFile("pandoc-pptx/" + pptx_files[i], "pptx");
}

printf("Results: %d passed, %d failed out of %d total\n", passed, failed, passed + failed);
if(errors.length > 0) {
    printf("\nFailures:\n");
    for(var i = 0; i < errors.length; i++)
        printf("  %s\n", errors[i]);
}
