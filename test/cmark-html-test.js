/* make printf et. al. global */
rampart.globalize(rampart.utils);

var cmark = require("rampart-cmark");
var html  = require("rampart-html");

function testFeature(name,test)
{
    var error=false;
    printf("testing %-60s - ", name);
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


testFeature("Cmark - Basic Functionality - H1", function() {
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

testFeature("Html - Add class and attribute", function() {
  var el = doc.findAttr('id=para_1');
  var el2 = doc.findAttr('id=para_2');

  return el.length == 1 && el2.length == 1;
});

testFeature("Html - Append element, convert to text and html", function() {
  var text, html;

  doc.findTag('body').append('<h3>heading3</h3>');

  text=doc.findTag('h3').toText();
  html=doc.findTag('h3').toHtml();

  return text[0]=='heading3' && html[0]=='<h3>heading3</h3>';
});

testFeature("Html - Delete element/replace/remove[Attr|Class]", function() {
  var el, el2, el3;
  
  doc.findTag('h3').delete();
  
  el = doc.findTag('h3');

  el2 = doc.findAttr('id=para_2');
  el2.removeAttr(" id ");

  el2.addClass('c1');
  el2.addClass('c2');
  el2.addClass('c3');
  el2.removeClass(' c2 ');

  doc.findTag('h2').replace("<h4>h4 text here</h4>");


  return el.length==0 && !el2.hasAttr('id')[0] && !el2.hasClass('c2')[0]
  && doc.findTag('h2').length==0 && doc.findTag('h4').length==1;
});

testFeature("Html - Find - Class/Attr/Tag", function() {
  var c,a,t;
  
  t=doc.findTag('h1');
  c=doc.findClass(" *yclass	");
  a=doc.findAttr("	id =	*ara_1	");

  return t.length==1 && c.length==2 && a.length==1;
});

testFeature("Html - Pend - append/prepend/before/after/detach-append", function() {
  var el, el2, el3, el4, doc2, txt;

  doc.findTag('body').append('<div id=tdiv>');
  doc.findTag('body').append('<div id=tdiv2>');

  el=doc.findAttr('id = tdiv ');
  //console.log(el.toHtml());
  el.append('<div id=d3>');
  el.prepend('<div id=d1>');
  el2=el.findAttr("id=d3");
  el2.before('<div id=d2>');

  el2.append('<div id=d5>');
  el4 = el2.findAttr('id=d5').detach();
  doc.findAttr('id=tdiv2').append(el4);

  doc2=html.newDocument('<div id=d4>');
  el3=doc2.findTag('div');
  el2.after(el3);
  
  doc2.destroy();

  txt = el.toHtml()[0];

  //"<div id=\"tdiv\"><div id=\"d1\"></div><div id=\"d2\"></div><div id=\"d3\"></div><div id=\"d4\"></div></div>"
  //console.log(doc.prettyPrint());
  return /tdiv.*d1.*d2.*d3.*d4/.test(txt) && doc.findAttr('id = tdiv2 ').children().length==1;

});

testFeature("Html - Get - element/elementname/attr/allAttr", function(){
  var el = doc.findAttr(' id =	tdiv ').children();
  var e = el.getElement();
  var en = el.getElementName();
  var ea = el.getAttr('id');
  var eaa = el.getAllAttr();
  
  //printf("%3J\n", [e,en,ea,eaa]);
  
  return (
    e[0]=="<div id=\"d1\">" &&
    e[1]=="<div id=\"d2\">" &&
    e[2]=="<div id=\"d3\">" &&
    e[3]=="<div id=\"d4\">" &&

    en[0]=='div' &&
    en[1]=='div' &&
    en[2]=='div' &&
    en[3]=='div' &&
 
    ea[0]=='d1' &&
    ea[1]=='d2' &&
    ea[2]=='d3' &&
    ea[3]=='d4' &&

    eaa[0].id=='d1' &&
    eaa[1].id=='d2' &&
    eaa[2].id=='d3' &&
    eaa[3].id=='d4');
});

testFeature("Html - Traversal - prev/next/children/parent", function(){
  var el = doc.findTag('blockquote');
  var sel = el.prev();
  var prev,next,nchildren,parent;
  
  prev = sel.getElementName();

  sel = el.next();
  next = sel.getElementName();
  
  sel = sel.children();
  nchildren=sel.length;

  sel = sel.parent();
  parent = sel.getAttr('id');
  
  //printf("%s, %s, %s, %s\n", prev,next,nchildren,parent);  

  return prev[0]=='h4' && next[0]=='div' && nchildren==4 && parent=='tdiv';
});

var list;

testFeature("Html - Filter/has[Class|Attr|Tag]", function(){
  var els, t1, t2, t3, t4, t5, t6, h1, h2, h3;

  doc.findAttr('id=tdiv').append("<span class='a classy Class1_class'>text</span><span class='a Class2_class classy'>text</span>");

  //console.log(doc.prettyPrint());

  list=doc.findAttr('id=tdiv').children();
  els = list.filterTag("span");
  t1 = (els.length == 2);

  els = list.filterAttr(["id='noexiste'"," id = 'd*' "])
  t2 = (els.length==4);

  els = list.filterClass(['Class2_\\class','*class']);
  h3 = els.hasClass(' classy ');
  h3 = h3[0] && h3[1];
  t3 = (els.length==2);

  els = list.slice(2,4);
  h1 = els.hasTag('div');
  h1 = h1[0] && h1[1];
  t4 = (els.length==2);
  
  els = list.eq(1);
  h2 = els.hasAttr(" id = d2 ")[0];
  t5 = (els.length==1);
  
  els = list.add("<a href>");
  t6 = (els.length == 7) 

  return t1 && t2 && t3 && t4 && t5 && t6 && h1 && h2 && h3;

});



testFeature("Html - destroy invalidates dependent Html Objs", function(){
  var els = list.filterTag("span");
  var ret=true;

  doc.destroy();
  
  try {
    els.toHtml();
    ret=false;
  } catch(e) { ret = ret && true; }

  try {
    list.toHtml();
    ret=false;
  } catch(e) { ret = ret && true; }

  return ret;
});
