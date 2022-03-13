/* make printf et. al. global */
rampart.globalize(rampart.utils);

var cmark = require("rampart-cmark");
var html  = require("rampart-html");

function testFeature(name,test)
{
    var error=false;
    printf("testing %-40s - ", name);
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




testFeature("Cmark: Basic Functionality - H1", function() {
  var out = cmark.toHtml(`
This is an H1
=============
  `);
  return ( trim(out) == '<h1>This is an H1</h1>');
});

var htmltxt = cmark.toHtml(`
This is an H1
=============

This is an H2
-------------

> This is a blockquote with two paragraphs. Lorem ipsum dolor sit amet,
> consectetuer adipiscing elit. Aliquam hendrerit mi posuere lectus.
> Vestibulum enim wisi, viverra nec, fringilla in, laoreet vitae, risus.
>
> Donec sit amet nisl. Aliquam semper ipsum sit amet velit. Suspendisse
> id -- sem "consectetuer" libero luctus adipiscing.
`,
    {
        sourcePos: true,
        hardBreaks: true,
        smart: true
    }
);

var doc = html.newDocument(htmltxt, {indent:true});

doc.findTag('p').addClass("myclass");
doc.findTag('p').eq(0).attr("id","para_1");
doc.findTag('p').eq(1).attr("id","para_2");

//console.log(doc.prettyPrint());

testFeature("Html: Add class and attribute", function() {
  var el = doc.findAttr('id=para_1');
  var el2 = doc.findAttr('id=para_2');

  return el.length == 1 && el2.length == 1;
});

testFeature("Html: Append element, convert to text", function() {
  var text;
  doc.findTag('body').append('<h3>heading3</h3>');

  text=doc.findTag('h3').toText();

  return text[0]=='heading3';
});

testFeature("Html: Delete element", function() {
  var el;
  
  doc.findTag('h3').delete();
  
  el = doc.findTag('h3');
  
  return el.length==0;
});


