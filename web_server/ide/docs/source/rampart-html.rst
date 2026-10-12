The rampart-html module
=======================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-html module uses the
`Tidy-HTML5 <http://www.html-tidy.org/>`_ library
to parse, fix-up, and manipulate HTML formatted documents.
The developers of Rampart are extremely grateful for the excellent api and ease
of use of the `Tidy-HTML5 <http://www.html-tidy.org/>`_ library.

License
~~~~~~~

The rampart-html module is released under the MIT license.
The `Tidy-HTML5 <http://www.html-tidy.org/>`_ library is
`licensed <https://github.com/htacg/tidy-html5/blob/next/README/LICENSE.md>`_
with a `zlib <https://opensource.org/licenses/Zlib>`_\ -like license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-html module provides methods to import, parse, alter and
pretty-print HTML documents.  It is meant to be familiar to those with
experience using jQuery, but has different syntax, fewer functions and more
restrictions.

How does it work?
~~~~~~~~~~~~~~~~~

After the module is loaded, an HTML document may be created or parsed by
passing a :green:`String` or :green:`Buffer` to the ``newDocument``
function.  The parsed document may then be manipulated using functions
to select, add or delete specific elements.
It then may be written to a new string using the ``prettyPrint`` function.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var html = require("rampart-html");

Configuring and Parsing
-----------------------

Loading the rampart-html module returns an object with the property/function
``newDocument()``.  This function can be used to create a blank HTML
document or import and parse from an existing one.

newDocument
~~~~~~~~~~~

Usage:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument([document_text][, options]);

where:

* ``document_text`` is a :green:`String` or :green:`Buffer` containing the
  HTML text to be parsed. It also can be an :green:`Object` produced by
  `toObj`_ below. If not specified or a blank string,
  an empty document with the ``html``, ``head``, ``title`` and
  ``body`` tags in place will be created.

* ``options`` is an optional :green:`Object` of key/value pairs that correspond
  to the `standard command line <https://api.html-tidy.org/tidy/tidylib_api_5.6.0/tidy_quickref.html>`_
  options for `Tidy-HTML5 <http://www.html-tidy.org/>`_\ .
  Some of these options apply to the rules for parsing the document, while
  others apply to the appearance of the document (such as indentation and
  line wrapping) when it is output is `prettyPrint`_\ ed.  Other options
  that deal with file i/o are ignored.  Options with a ``-`` may also be
  given as camelCase.

  A small subset of these options include:

  *  ``indent`` - a :green:`Boolean`, whether to add indentation and word
     wrapping to the output when using `prettyPrint`_\ .

  *  ``indent-spaces`` (aka ``indentSpaces``) - a :green:`Number` indicating
     the number of spaces to indent the output when using `prettyPrint`_ and
     ``indent`` above is set ``true``.  The default is ``2``.

  *  ``wrap`` - a :green:`Number` indicating the maximum length of a
     line of text or HTML used when using `prettyPrint`_ and ``indent``
     above is set ``true``.  The default is ``68``.  There may be some
     circumstances where it is not possible to wrap a line.

  *  ``drop-empty-elements`` (aka ``dropEmptyElements``) - a
     :green:`Boolean`, whether to drop empty elements.  **In Rampart** the
     default is ``false``.

  *  ``tidy-mark`` (aka ``tidyMark``) - a :green:`Boolean`, whether to insert
     a ``meta`` tag in the head of the document indicating that the
     `Tidy-HTML5 <http://www.html-tidy.org/>`_ library was used to process
     the document.  **In Rampart** the default is ``false``.

  *  ``vertical-space`` - a :green:`Boolean` or a :green:`String`, whether
     to add some extra empty lines for readability.  The default is
     ``false``.  If set to ``"auto"`` nearly all newline characters will be
     eliminated.

  *  See the `HTML Tidy Options Quick Reference
     <https://api.html-tidy.org/tidy/tidylib_api_5.6.0/tidy_quickref.html>`_
     for more options.

Return Value:
  An *html object* with all the functions for manipulating the HTML
  document.  In addition, this will be the *root html object*.

objToHtml
~~~~~~~~~

Take the output of `toObj`_ and produce html text.

Usage:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument(mydocText);

   /* manipulate document here */

   var htmlJSON = JSON.stringify(mydoc.toObj());

   /* save the json in a file */
   rampart.utils.fprintf("/path/to/my_html.json", '%s', htmlJSON);

   /* **** in another script **** */

   var html = require("rampart-html");
   /* load json, convert to object and then convert to text/html */
   var htmlText = html.objToHtml( rampart.utils.readFile("/path/to/my_html.json") );

   /* htmlText = "<!DOCTYPE html><html><head><title>...</html>" */

Note:
   The same can be performed in the browser by using JavaScript similar to
   this:

.. code-block:: javascript

   /* distinguish between a plain object and an array
      and make output similar to rampart.utils.getType()    */
   function getType(v) {

       if(typeof v == 'object')
       {
           if(v instanceof Array)
               return "Array";
           return "Object"
       }
       var ret = typeof v;
       return ret.charAt(0).toUpperCase() + ret.slice(1);
   }

   var singletons = [
           "br",
           "input",
           "link",
           "meta",
           "!doctype",
           "col",
           "area",
           "base",
           "param",
           "track",
           "wbr",
           "keygen"
   ];

   function objToHtmlArr(obj, txtarr) {
       var a, i=0, len, issingleton=false;

       if(!txtarr)
           txtarr=[];

       if(getType(obj) != "Object")
           return [];

       if(obj.type)
       {
           if(obj.type !='document') {
               issingleton = singletons.includes(obj.type.toLowerCase());
               txtarr.push('<' + obj.type);

               if(obj.attributes) {
                   if (getType(obj.attributes) == "Object"){
                       a = obj.attributes;
                       for (key in a) {
                           txtarr.push(' ' + key + '="' + a[key].replace(/"/g,'&quot;')+'"' )
                       }
                   } else if (getType(obj.attributes) == "Array") {
                       a = obj.attributes;
                       len = a.length;
                       for(;i<len;i++){
                           txtarr.push(' ' + a[i]);
                       }
                   }
               }
               txtarr.push('>');
           }
       }

       if(obj.contents && getType(obj.contents)=='Array') {
           a=obj.contents;
           len=a.length;
           for(i=0;i<len;i++){
               if(getType(a[i])=='String')
                   txtarr.push(a[i]);
               else if (getType(a[i])=='Object')
                   txtarr = objToHtmlArr(a[i],txtarr);
           }
       }

       if(obj.type && obj.type !='document' && !issingleton)
           txtarr.push("</"+obj.type+'>');
       return txtarr;
   }

   function objToHtml(obj){
       var i=0,ret=[];

       if(getType(obj)=="Array") {
           for(;i<obj.length;i++)
               ret=objToHtmlArr(obj[i],ret);
       } else if (getType(obj) == "Object"){
           ret=objToHtmlArr(obj[i],ret);
       }
       return ret.join('');
   }


Manipulating the HTML
---------------------

The html object
~~~~~~~~~~~~~~~

An *html object* is an :green:`Object` which contains an opaque list of elements in
the HTML document parsed with `newDocument`_ above.
An element is a single parsed HTML tag (such as "``<br />``")
with links to its descendant elements and/or plain text content, if any.

The *root html object* is the :green:`Object` returned from
`newDocument`_\ .  It is identical to other *html objects*, except that it
contains only one element (the document root).

In addition to the *root html object*, new ones can be created.
A new list of elements is returned in an *html object* when they are
selected, detached, moved, copied or have their attributes and
classes changed with the functions below.

Each *html object* created from any other *html object* will refer
to elements in the same document.  These elements represent the actual
content in the HTML document, and if manipulated, will change the contents
returned from any other *html object* derived from the same *root html
object*.

Additionally, the *html object* includes the ``length`` property (number of elements in
the list).


destroy
~~~~~~~

Destroy and release resources used by a document created with  ``newDocument()``.

Usage:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument([document_text][, options]);

    mydoc.destroy();

NOTE:
   ``destroy()`` may be used from the *root html object* or any list of elements
   produced from functions below (any dependent *html object*).
   Calling destroy will invalidate the *root html object* and any lists
   created from it (see ``Manipulating the HTML`` below).



Selecting Elements
~~~~~~~~~~~~~~~~~~

findTag
"""""""

Find all the elements that are descendants of the current list of elements
which have a given tag name and return a new *html object*.  If no elements
are found, a *html object* with an empty list of elements is returned.

Usage:

.. code-block:: javascript

    var list = doc.findTag(tagname);

Where ``tagname`` is the name of the HTML tag of the element to be selected
(e.g.  "div").

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument(document_text, options);

    /* get a list of all the divs in the document */
    var alldivs = mydoc.findTag("div");

findAttr
""""""""

Find all the elements that are descendants of the current list
of elements which have a given attribute and return a new *html object*.

Usage:

.. code-block:: javascript

    var list = doc.findAttr(attrname);

Where ``attrname`` is the name of the attribute in the element to be selected (e.g. "id").

Additionally:

   * ``attrname`` can specify a value by using ``"attr=val"`` syntax.
   * whitespace is ignored (e.g. ``"attr = val "``.
   * globs may be used at the beginning or end (but not both) of ``val``
     (e.g ``"id=my_id_*"`` or ``id=*_val``)
   * quotes are respected (e.g. ``"id='my val'"`` or ``'id="my val"'``)
   * quotes can be escaped (e.g. ``"id='john\\'s msg'"``). Note the double
     backslash.  It is required for the JavaScript string to pass a single
     backslash.
   * Quotes, backslashes and globs are also available in `filterAttr`_ and
     `hasAttr`_\ .

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument(document_text, options);

    /* get a list of all the elements with a href in the element */
    var allhrefs = mydoc.findAttr("href");

    /* get a list consisting of the element(s) with the attr 'id = "maindiv"` */
    var maindiv = mydoc.findAttr("id=maindiv");

findClass
"""""""""

Find all the elements that are descendants of the current list of elements
which belong to the named class and return a new *html object*.

Usage:

.. code-block:: javascript

    var list = doc.findClass(classname);

Where ``classname`` is the name of the HTML tag to be selected
(e.g. if an element has the attribute ``class="foo1 bar2"``, ``classname``
of ``bar2`` would select the element).

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument(document_text, options);

    /* get a list of all the elements in the document
       which belong to the "foo1" class              */
    var alldivs = mydoc.findClass("foo1");

Output from Elements
~~~~~~~~~~~~~~~~~~~~

getElement
""""""""""

Return an :green:`Array` of :green:`Strings` containing the opening tag for
each of the given elements in the *html object*.  No children are returned.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div class="myclass">one</div><div>two</div>' +
        '<div class="myclass">three</div><div>four</div>' +
        '<div class="myclass">five</div>'
    );

    var mytags = doc.findTag("div").getElement();

    rampart.utils.printf("%3J\n", mytags);

    /* expected output:

    [
       "<div class=\"myclass\">",
       "<div>",
       "<div class=\"myclass\">",
       "<div>",
       "<div class=\"myclass\">"
    ]

    */


getElementName
""""""""""""""

Return an :green:`Array` of :green:`Strings` containing the opening tag
**name** for each of the given elements in the *html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div class="myclass">one</div><div>two</div>' +
        '<div class="myclass">three</div><div>four</div>' +
        '<div class="myclass">five</div>'
    );

    var mytags = doc.findTag("div").getElementName();

    rampart.utils.printf("%3J\n", mytags);

    /* expected output:

    [
       "div",
       "div",
       "div",
       "div",
       "div"
    ]

    */



getAttr
"""""""

Return an :green:`Array` of :green:`Strings` containing the attribute value of the provided
attribute name for each of the given elements in the *html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div title="div1">one</div><div>two</div>' +
        '<div title="div3">three</div><div>four</div>' +
        '<div title="div5">five</div>'
    );

    var mytags = doc.findTag("div").getAttr('title');

    rampart.utils.printf("%3J\n", mytags);

    /* expected output:

    [
       "div1",
       "",
       "div3",
       "",
       "div5"
    ]

    */


getAllAttr
""""""""""

Return an :green:`Array` of :green:`Objects`, each containing attribute name/value pairs,
for each of the given elements in the *html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div title="div1">one</div><div class="myclass">two</div>' +
        '<div title="div3" class="myclass">three</div><div>four</div>' +
        '<div title="div5">five</div>'
    );

    var mytags = doc.findTag("div").getAllAttr();

    rampart.utils.printf("%3J\n", mytags);

    /* expected output:

    [
       {
          "title": "div1"
       },
       {
          "class": "myclass"
       },
       {
          "title": "div3",
          "class": "myclass"
       },
       {},
       {
          "title": "div5"
       }
    ]

    */

toObj
"""""

Return an :green:`Array` of :green:`Objects` representing the elements
in the *html object* or the entire document if the object is the *root html
object*.

Example:

.. code-block:: javascript

   var html = require("rampart-html");

   var doc = html.newDocument(
       '<div title="div1">one</div><div>two</div>' +
       '<div title="div3">three</div><div>four</div>' +
       '<div title="div5">five <span>six</span></div>'
   );

   var mytags = doc.findTag("div").toObj();

   rampart.utils.printf("%3J\n", mytags);

   /* expected output:
   [
      {
         "type": "div",
         "attributes": {
            "title": "div1"
         },
         "contents": [
            "one"
         ]
      },
      {
         "type": "div",
         "contents": [
            "two"
         ]
      },
      {
         "type": "div",
         "attributes": {
            "title": "div3"
         },
         "contents": [
            "three"
         ]
      },
      {
         "type": "div",
         "contents": [
            "four"
         ]
      },
      {
         "type": "div",
         "attributes": {
            "title": "div5"
         },
         "contents": [
            "five ",
            {
               "type": "span",
               "contents": [
                  "six"
               ]
            }
         ]
      }
   ]
   */

   rampart.utils.printf("%3J\n", doc.toObj());

   /* expected output:
   [
      {
         "type": "document",
         "contents": [
            {
               "type": "!DOCTYPE",
               "attributes": [
                  "html"
               ]
            },
            {
               "type": "html",
               "contents": [
                  {
                     "type": "head",
                     "contents": [
                        {
                           "type": "title"
                        }
                     ]
                  },
                  {
                     "type": "body",
                     "contents": [
                        {
                           "type": "div",
                           "attributes": {
                              "title": "div1"
                           },
                           "contents": [
                              "one"
                           ]
                        },
                        {
                           "type": "div",
                           "contents": [
                              "two"
                           ]
                        },
                        {
                           "type": "div",
                           "attributes": {
                              "title": "div3"
                           },
                           "contents": [
                              "three"
                           ]
                        },
                        {
                           "type": "div",
                           "contents": [
                              "four"
                           ]
                        },
                        {
                           "type": "div",
                           "attributes": {
                              "title": "div5"
                           },
                           "contents": [
                              "five ",
                              {
                                 "type": "span",
                                 "contents": [
                                    "six"
                                 ]
                              }
                           ]
                        }
                     ]
                  }
               ]
            }
         ]
      }
   ]

   */

Note:
   The return :green:`Object` can be used as the input for html.\ `newDocument`_ or
   html.\ `objToHtml`_ above.

Text and HTML Output
~~~~~~~~~~~~~~~~~~~~

toHtml
""""""

Return an :green:`Array` of :green:`Strings`, each string the HTML of each of the given
elements and their children.

.. skip this.  the concatenate is dangerous as <span> will get newlines
    Usage:

    .. code-block:: javascript

        var tags = hobj.toHtml([options]);

    Where:

    * ``tags`` is the return value.

    * ``hobj`` is an *html object* with 0 or more elements.

    *  ``options`` is an :green:`Object` which can have one setting:

        * ``{concatenate: true}`` - if set, ``tags`` will be returned as a
          :green:`String` consisting of the concatenated output from
          each element.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div title="div1">one</div><div>two</div>' +
        '<div title="div3">three</div><div>four</div>' +
        '<div title="div5">five <span>six</span></div>'
    );

    var mytags = doc.findTag("div").toHtml();

    rampart.utils.printf("%3J\n", mytags);

    /* expected output:

    [
       "<div title=\"div1\">one</div>",
       "<div>two</div>",
       "<div title=\"div3\">three</div>",
       "<div>four</div>",
       "<div title=\"div5\">five <span>six</span></div>"
    ]

    */


toText
""""""

Return an :green:`Array` of :green:`Strings` (or optionally a concatenated
:green:`String`), each string being the plain text extracted from each of
the given elements and their children.  By default, ``toText()`` attempts to
extract only visible text.  Options below allow for some formatting and
other relevant text to be returned as well.

Usage:

.. code-block:: javascript

    var tags = hobj.toText([options]);

Where:

* ``tags`` is the return value.

* ``hobj`` is an *html object* with 0 or more elements.

*  ``options`` is an :green:`Object` with the following setting:

    * ``concatenate`` - a :green:`Boolean` if true, the function will return
      a :green:`String` consisting of the concatenated output from each given
      element.  Default is ``false``.

    * ``metaDescription`` - a :green:`Boolean` if true, text from the
      ``content`` of an existing ``<meta name="description" content="text">``
      will also be output.  Default is ``false``.

    * ``metaKeywords`` - a :green:`Boolean` if true, text from the
      ``content`` of an existing ``<meta name="keywords" content="text">``
      will also be output.  Default is ``false``.

    * ``enumerateLists`` - a :green:`Boolean` if true, text in ``<li>`` tags
      will be indented and prepended with an asterisk ``*`` for unordered lists
      (``<ul>``) or a sequential number followed by a period (e.g. ``1.``) for ordered
      lists (``<ol>``). Text inside ``<dl>/<dt>/<dd>`` tags will also be indented.
      Default is ``false``.

    * ``titleText`` - a :green:`Boolean` if true, text from any element
      which contains a ``title`` attribute will also be output.
      Default is ``false``.

    * ``showHRTags``- a :green:`Boolean` if true, ``<hr>`` will be replaced
      with ``\n---\n`` instead of ``\n``.  Default is ``false``.

    * ``aLinks`` - a :green:`Boolean` if true, the ``href`` value from
      ``<a>`` tags will be output after the enclosed text in parentheses
      in markdown style.  Default is ``false``.

    * ``imgAltText`` - a :green:`Boolean` if true, alt text from images
      will also be output.  Default is ``false``.

    * ``imgLinks`` - a :green:`Boolean` if true, the ``src`` value from
      ``<img>`` tags will be output after the enclosed text in parentheses
      in markdown style.  Default is ``false``.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<meta name="description" content="my awesome story as told by me">' +
        '<meta name="keywords" content="awesome adventure love happiness redemption">' +
        '<title>My Awesome Story</title>' +
        '<h1>Table of Contents</h1><ol><li>Chapter 1</li><li>Chapter 2</li></ol>' +
        '<h2 title="Chapter 1">I was born</h2><img src="myimage.jpg" alt="me as a baby" title="My Baby Pic">' +
        '<div>I was born a poor ...</div>' +
        '<h2 title="Chapter 2">I left home</h2><img src="myimage2.jpg" alt="me at 21">' +
        '<div>I got a job guessing weights at <a title="The Carnival Website" href="http://example.com/">a carnival</a>...</div>'
    );

    console.log("___DEFAULT___");
    console.log(doc.toText()[0]);
    console.log("-----------\n\n___WITH_EXTRAS___");
    console.log(doc.toText({
        metaDescription:true,
        metaKeywords: true,
        enumerateLists: true,
        aLinks:true,
        titleText:true,
        showHRTags:true,
        imgLinks:true,
        imgAltText:true
    })[0]);

    /* expected output:
    ___DEFAULT___
    My Awesome Story

    Table of Contents

    Chapter 1
    Chapter 2

    I was born

    I was born a poor ...

    I left home

    I got a job guessing weights at a carnival ...

    -----------

    ___WITH_EXTRAS___
    description: my awesome story as told by me
    keywords: awesome adventure love happiness redemption

    My Awesome Story

    Table of Contents

       1. Chapter 1
       2. Chapter 2

    Chapter 1
    I was born

    ![me as a baby](myimage.jpg "My Baby Pic")
    I was born a poor ...

    Chapter 2
    I left home

    ![me at 21](myimage2.jpg)
    I got a job guessing weights at [a carnival](http://example.com/ "The Carnival Website")...

    */

prettyPrint
"""""""""""

Format and output a :green:`String` of the *first* element in the list of
elements. If used with the *root html object*, it will output the entire
document.

Usage:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument();

    var output = mydoc.prettyPrint({formattingOptions});

    /* or */

    var output = mydoc.prettyPrint(indentSpaces, wrap);

Where

   * `formattingOptions` is an :green:`Object` - the same formatting options from `newDocument`_
     above.

   * `indentSpaces` is a :green:`Number` - number of spaces to be used for
     indentation.

   * `wrap` is a :green:`Number` - minimum number of characters to print before
     wrapping a line. Line length may exceed this value but will break at
     the first opportunity when line length exceeds it.

Note:
   Setting options in ``prettyPrint`` overrides the options set in `newDocument`_
   for that call only.  A subsequent ``prettyPrint()`` with no options reverts
   to the options given to `newDocument`_\ .

   Note also that ``indent`` and ``indentSpaces`` are independent here: passing
   ``{indent: true}`` to ``prettyPrint`` without an ``indentSpaces`` indents by
   zero, rather than falling back to the default of ``2`` described under
   `newDocument`_\ .  Give ``indentSpaces`` explicitly when setting ``indent``
   on a ``prettyPrint`` call.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var mydoc = html.newDocument();

    var output = mydoc.prettyPrint(2,80);

    console.log(output);

    /* expected output:

    <!DOCTYPE html>
    <html>
      <head>
        <title></title>
      </head>
      <body>
      </body>
    </html>

    */

    mydoc = html.newDocument(
        '<title>My Page</title><h1>Welcome to my page</h2>',
        { indent: true }
    );

    output = mydoc.prettyPrint({indent:true, indentSpaces:2});

    console.log(output);

    /* expected output:
    <!DOCTYPE html>
    <html>
      <head>
        <title>
          My Page
        </title>
      </head>
      <body>
        <h1>
          Welcome to my page
        </h1>
      </body>
    </html>
    */

Traversing HTML tree
~~~~~~~~~~~~~~~~~~~~

next
""""

Given the current list of elements, return a new *html object* with a list
consisting of the next sibling element of each, if one exists.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div class="myclass">one</div><div>two</div>' +
        '<div class="myclass">three</div><div>four</div>' +
        '<div class="myclass">five</div>'
    );

    var mydivs = doc.findClass("myclass");

    var nextdivs = mydivs.next();

    console.log(nextdivs.toHtml());

    /* expected output:

    ["<div>two</div>","<div>four</div>"]

    */

prev
""""

Given the current list of elements, return a new *html object* with a list
consisting of the previous sibling element of each, if one exists.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<span>one</span><div class="myclass">two</div>' +
        '<span>three</span><div class="myclass">four</div>'
    );

    var mydivs = doc.findClass("myclass");

    var prevels = mydivs.prev();

    console.log(prevels.toHtml());

    /* expected output:

    ["<span>one</span>","<span>three</span>"]

    */

children
""""""""

Given the current list of elements, return a new *html object* with a list
consisting of the direct descendant elements of each, if any exists.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div class="myclass"><span>one</span><span>two</span></div>' +
        '<div class="myclass"><span>three</span><span>four</span></div>'
    );

    var mydivs = doc.findClass("myclass");

    var children = mydivs.children();

    console.log(children.toHtml());

    /* expected output:

    ["<span>one</span>","<span>two</span>","<span>three</span>","<span>four</span>"]

    */


parent
""""""

Given the current list of elements, return a new *html object* with a list
consisting of the direct ancestor elements of each, if any exists.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div class="myclass"><span>one</span><span>two</span></div>' +
        '<div class="myclass"><span>three</span><span>four</span></div>'
    );

    var myspans = doc.findTag("span");

    var parents = myspans.parent();

    rampart.utils.printf("%3J\n", parents.toHtml());

    /* expected output:

    [
       "<div class=\"myclass\"><span>one</span><span>two</span></div>",
       "<div class=\"myclass\"><span>three</span><span>four</span></div>"
    ]

    */

Note that even though there are four elements in ``myspans``, ``parent()``,
like all *html object* functions, it returns a unique list.

getDocument
"""""""""""

Given an *html object* return the *root html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    /* var doc is our root object */
    var doc = html.newDocument(
        '<span>one</span><div class="myclass">two</div>' +
        '<span>three</span><div class="myclass">four</div>'
    );

    var spans = doc.findTag("span");

    /* demonstrate that getDocument returns the root object */
    console.log( (doc == spans.getDocument()) );

    /* expected output:

    true

    */

In the above example, the `prettyPrint`_ function could be accessed from
``spans`` with the following: ``spans.getDocument().prettyPrint()``.

Manipulating the List of Elements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

filterTag
"""""""""

Reduce the current list of elements to only include elements which have a
given tag name and return a new *html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div  class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div>four</div>'
    );

    var els = doc.findTag('body').children();

    var divs = els.filterTag('div');

    console.log(divs.toHtml());

    /* expected output:

    ["<div class=\"myclass\">one</div>","<div>four</div>"]

    */

filterAttr
""""""""""

Reduce the current list of elements to only include elements which have a
given attribute and return a new *html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div id="mydiv" class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div>four</div>'
    );

    var els = doc.findTag('body').children();

    var mydiv = els.filterAttr('id=mydiv');

    console.log(mydiv.toHtml());

    /* expected output:

    ["<div id=\"mydiv\" class=\"myclass\">one</div>"]

    */

filterClass
"""""""""""

Reduce the current list of elements to only include elements which belong to
a given class and return a new *html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div id="mydiv" class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div>four</div>'
    );

    var els = doc.findTag('body').children();

    els = els.filterClass('myclass');

    console.log(els.toHtml());

    /* expected output:

    ["<div id=\"mydiv\" class=\"myclass\">one</div>","<span class=\"myclass\">three</span>"]

    */


slice
"""""

Reduce the current list of elements to only include a subset of the list
and return a new *html object*. Arguments are the same as
`Array.slice <https://www.w3schools.com/jsref/jsref_slice_array.asp>`_

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<span>zero</span><span>one</span><span>two</span>' +
        '<span>three</span><span>four</span><span>five</span>'
    );

    var els = doc.findTag('body').children();

    els = els.slice(2,4);

    console.log(els.toHtml());

    /* expected output:

    ["<span>two</span>","<span>three</span>"]

    */


eq
""

Reduce the current list of elements to only include a single element
at the given index and return a new *html object*.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<span>zero</span><span>one</span><span>two</span>' +
        '<span>three</span><span>four</span><span>five</span>'
    );

    var els = doc.findTag('body').children();

    var el = els.eq(2);

    console.log(els.toHtml());

    /* expected output:

    ["<span>two</span>"]

    */

add
"""

Add to the given list of elements the elements in the provided *html object*
or :green:`String` and return a new *html object*.

Usage:

.. code-block:: javascript

    var newlist = oldlist.add(additions);

Where

* ``newlist`` is the returned *html object*.
* ``oldlist`` is the *html object* with an array of elements to be appended.
* ``additions`` is an *html object* or a :green:`String` of text or HTML
  to be added to the list.

Note:
  Additions made to list with ``add()`` are detached and will not be a part
  of the output of `prettyPrint` unless one of `append`_\ , `prepend`_\, `before`_\ ,
  or `after`_ is used to insert the list into the document.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<span>one</span><span>two</span><span>three</span>' +
        '<div>four</div><div>five</div><div>six</div>'
    );

    var spans = doc.findTag('span');

    var divs = doc.findTag('div');

    var newlist = spans.add(divs);

    newlist = newlist.add("<div>seven</div><div>eight</div>");

    rampart.utils.printf("%3J\n", newlist.toHtml());

    /* expected output:

    [
       "<span>one</span>",
       "<span>two</span>",
       "<span>three</span>",
       "<div>four</div>",
       "<div>five</div>",
       "<div>six</div>",
       "<div>seven</div>",
       "<div>eight</div>"
    ]

    */


Testing Elements
~~~~~~~~~~~~~~~~

hasTag
""""""

Test each element in the current list for a tag name. Returns an array of
:green:`Booleans`, one for each element.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div  class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div>four</div>'
    );

    var els = doc.findTag('body').children();

    var isdiv = els.hasTag('div');

    console.log(isdiv);

    /* expected output:

    [true,false,false,true]

    */



hasAttr
"""""""

Test each element in the current list for the presence of an attribute.
Returns an :green:`Array` of :green:`Booleans`, one for each element.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div id="someid" class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div id="myid">four</div>'
    );

    var els = doc.findTag('body').children();

    var hasanid = els.hasAttr('id');
    var hasmyid = els.hasAttr('id=myid');

    console.log(hasanid);
    console.log(hasmyid);

    /* expected output:

    [true,false,false,true]
    [false,false,false,true]

    */

hasClass
""""""""
Test each element in the current list for a tag name. Returns an
:green:`Array` of :green:`Booleans`, one for each element.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div  class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div id="myid">four</div>'
    );

    var els = doc.findTag('body').children();

    var hasmyclass = els.hasClass('myclass');

    console.log(hasmyclass);

    /* expected output:

    [true,false,true,false]

    */

Manipulating Elements
~~~~~~~~~~~~~~~~~~~~~

attr
""""

Given a list of elements, change the named attribute to the specified value,
or, if not present add the attribute with the specified value.  Returns
itself.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div title="My Div">one</div><span>two</span>' +
        '<span>three</span><div title="Another Div">four</div>'
    );

    var els = doc.findTag('body').children();

    els.attr("title", "I'm an element");

    rampart.utils.printf("%3J\n", els.toHtml());

    /* expected output:

    [
       "<div title=\"I'm an element\">one</div>",
       "<span title=\"I'm an element\">two</span>",
       "<span title=\"I'm an element\">three</span>",
       "<div title=\"I'm an element\">four</div>"
    ]

    */


removeAttr
""""""""""

Given a list of elements, remove the named attribute.  If not present no
changes are made.  Returns itself.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div title="My Div">one</div><span title>two</span>' +
        '<span>three</span><div title="Another Div">four</div>'
    );

    var els = doc.findTag('body').children();

    els.removeAttr("title");

    rampart.utils.printf("%3J\n", els.toHtml());

    /* expected output:

    [
       "<div>one</div>",
       "<span>two</span>",
       "<span>three</span>",
       "<div>four</div>"
    ]

    */



addClass
""""""""

Given a list of elements, add the named class to each.
Returns itself.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div  class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div id="myid">four</div>'
    );

    var els = doc.findTag('body').children();

    els.addClass('mycolor');

    rampart.utils.printf("%3J\n", els.toHtml());

    /* expected output:

    [
       "<div class=\"myclass mycolor\">one</div>",
       "<span class=\"mycolor\">two</span>",
       "<span class=\"myclass mycolor\">three</span>",
       "<div class=\"mycolor\" id=\"myid\">four</div>"
    ]

    */


removeClass
"""""""""""

Given a list of elements, remove the named class from each.
Returns itself.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div  class="myclass">one</div><span>two</span>' +
        '<span class="myclass">three</span><div id="myid">four</div>'
    );

    var els = doc.findTag('body').children();

    els.removeClass('myclass');

    rampart.utils.printf("%3J\n", els.toHtml());

    /* expected output:

    [
       "<div class=\"\">one</div>",
       "<span>two</span>",
       "<span class>three</span>",
       "<div id=\"myid\">four</div>"
    ]

    */

detach
""""""

Detach the list of elements from the document and return a new *html object*
with the detached elements.

See `append`_ below for an example.

delete
""""""

Same as detach, but no new list is created and nothing is returned (returns
``undefined``).

append
""""""

Append the provided list of elements to each of the given elements as
child(ren) of the given elements.

Usage:

.. code-block:: javascript

    var newlist = oldlist.attach(elems);

Where

* ``newlist`` is a new *html object* containing the elements from
  ``oldlist``.

* ``oldlist`` contains the given elements to append.

* ``elems`` is an *html object* or a :green:`String`  of text or HTML
  to be appended to the given elements.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div>one</div><span>two</span>' +
        '<span>three</span><span>four</span>' +
        '<div>five</div>'
    );

    /* find all spans and detach them from the document */
    var spans = doc.findTag('span').detach();

    var divs = doc.findTag('div');

    /* add the spans back to document as children of the divs */
    var newlist = divs.append(spans);

    /* add some text to the divs */

    newlist = divs.append("...");

    rampart.utils.printf("%3J\n", newlist.toHtml());

    /* expected output:

    [
       "<div>one<span>two</span><span>three</span><span>four</span>...</div>",
       "<div>five<span>two</span><span>three</span><span>four</span>...</div>"
    ]

    */

prepend
"""""""

Similar to `append`_ above, except that the provided elements are added to
the beginning of the list of children of the given elements.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div>one</div><span>two</span>' +
        '<span>three</span><span>four</span>' +
        '<div>five</div>'
    );

    /* find all spans and detach them from the document */
    var spans = doc.findTag('span').detach();

    var divs = doc.findTag('div');

    /* add a space before the contents of each span.
       Note also that '&nbsp;' is used as leading white space
       is automatically trimmed.
    */

    spans = spans.prepend("&nbsp;");

    /* add the spans back to document as children of the divs
       BUT before any existing children                      */

    var newlist = divs.prepend(spans);

    rampart.utils.printf("%3J\n", newlist.toHtml());

    /* expected output:

    [

    "<div><span>&nbsp;two</span><span>&nbsp;three</span><span>&nbsp;four</span>one</div>",
    "<div><span>&nbsp;two</span><span>&nbsp;three</span><span>&nbsp;four</span>five</div>"
    ]

    */


after
"""""

Place the provided list of elements **after** each of the given elements.
Return a new *html object* with the given elements, each followed by the
provided elements.

Usage:

.. code-block:: javascript

    var newlist = oldlist.after(elems);

Where

* ``newlist`` is a new *html object* containing the elements from
  ``oldlist`` and ``elems``.

* ``oldlist`` contains the given elements which provide a reference for placement.

* ``elems`` is an *html object* or a :green:`String`  of text or HTML
  to be placed after the given elements.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div>one</div>' +
        '<div>two</div>',
        {indent: true}
    );

    var divs = doc.findTag('div');

    newlist = divs.after('<span>3</span><span>4</span>');

    rampart.utils.printf("%3J\n", newlist.toHtml());

    console.log(doc.prettyPrint());

    /* expected output:

    [
       "<div>one</div>",
       "<span>3</span>",
       "<span>4</span>",
       "<div>two</div>",
       "<span>3</span>",
       "<span>4</span>"
    ]
    <!DOCTYPE html>
    <html>
      <head>
        <title></title>
      </head>
      <body>
        <div>
          one
        </div><span>3</span><span>4</span>
        <div>
          two
        </div><span>3</span><span>4</span>
      </body>
    </html>

    */

before
""""""

Similar to `after`_ except the provided list of elements are placed **before** each of the given elements.

Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div>one</div>' +
        '<div>two</div>',
        {indent: true}
    );

    var divs = doc.findTag('div');

    newlist = divs.before('<span>3</span><span>4</span>');

    rampart.utils.printf("%3J\n", newlist.toHtml());

    console.log(doc.prettyPrint());

    /* expected output:

    [
       "<span>3</span>",
       "<span>4</span>",
       "<div>one</div>",
       "<span>3</span>",
       "<span>4</span>",
       "<div>two</div>"
    ]
    <!DOCTYPE html>
    <html>
      <head>
        <title></title>
      </head>
      <body>
        <span>3</span><span>4</span>
        <div>
          one
        </div><span>3</span><span>4</span>
        <div>
          two
        </div>
      </body>
    </html>

    */

replace
"""""""

Similar to `after`_ and `before`_ above, except that the given elements are
replaced.


Example:

.. code-block:: javascript

    var html = require("rampart-html");

    var doc = html.newDocument(
        '<div>one</div>' +
        '<div>two</div>',
        {indent: true}
    );

    var divs = doc.findTag('div');

    newlist = divs.replace('<span>3</span><span>4</span>');

    rampart.utils.printf("%3J\n", newlist.toHtml());

    console.log(doc.prettyPrint());

    /* expected output:

    [
       "<span>3</span>",
       "<span>4</span>",
       "<span>3</span>",
       "<span>4</span>"
    ]
    <!DOCTYPE html>
    <html>
      <head>
        <title></title>
      </head>
      <body>
        <span>3</span><span>4</span><span>3</span><span>4</span>
      </body>
    </html>

    */











