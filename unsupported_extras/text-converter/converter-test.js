rampart.globalize(rampart.utils);

var converter = require('rampart-converter.js');

var convert = new converter();


function testFeature(name,test)
{
    var error=false;
    printf("testing converter   - %-46s - ", name);
    fflush(stdout);
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}

var txt, files;
try {
    files = readDir("./convtest/");
} catch(e) {
    testFeature("test files available in ./convtest", false);
}

testFeature("test files available in ./convtest", files.length > 0);

for (var i=0; i<files.length; i++) {
    var file = "./convtest/" + files[i];
    txt = convert.convertFile(file);
    txt = txt.split(/\n+/);
    testFeature(file, txt.length>35);
}