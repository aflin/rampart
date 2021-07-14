The rampart-cmark module
==============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-cmark module uses 
`cmark library <https://github.com/commonmark/cmark>`_
The authors of Rampart extend our thanks to 
`the authors and contributers <https://github.com/commonmark/cmark/graphs/contributors>`_
to this library.

License
~~~~~~~

The cmark library is licensed under a
`2-clause BSD License <https://github.com/commonmark/cmark/blob/master/COPYING>`_
and includes other licenses therein.

The rampart-cmark module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-cmark module processes 
`CommonMark Markdown <https://commonmark.org/>`_ and converts it to HTML.


How does it work?
~~~~~~~~~~~~~~~~~

The rampart-cmark module exports a single function which takes as its input, a
markdown document and options.  It returns the document formatted in HTML.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var cmark = require("rampart-cmark");



Main Function
-------------

The rampart-cmark module exports a single function, ``toHtml()``.

toHtml
~~~~~~

    The ``toHtml`` function takes one or two arguments: The 
    markdown document to be translated and optionally an :green:`Object`
    of options.
    
    Usage:

    .. code-block:: javascript
    
        var cmark = require("rampart-cmark");
        
        var html = cmark.toHtml(markdown[, options]); 

    Where:
    
    * ``markdown`` is a :green:`String`, the text formatted in CommonMark
      Markdown.

    * ``options`` is an :green:`Object` with the following optional
      properties (all properties default to ``false`` if not set):

        * ``hardBreaks`` - A :green:`Boolean`. If ``true`` render softbreak
    	  elements as hard line breaks.

    	* ``unsafe`` - A :green:`Boolean`. If ``true``  Render  raw  HTML
          and unsafe links (javascript:, vbscript:, file:, and data:, except
          for image/png, image/gif, image/jpeg, or image/webp mime
       	  types). By default, raw HTML is replaced by a placeholder HTML
          comment. Unsafe links are replaced by empty strings.

        * ``noBreaks`` - A :green:`Boolean`. If ``true`` Render softbreak
          elements as spaces.

        * ``smart`` - A :green:`Boolean`. If ``true`` Convert straight
          quotes to curly, ``---`` to em dashes, ``--`` to en dashes.

    Return Value:
        A :green:`String` - The document converted to HTML.

Example
-------

.. code-block:: javascript

    var cmark = require("rampart-cmark");

    var out = cmark.toHtml(`
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

    console.log(out);

    /* expected output
    <h1 data-sourcepos="2:1-4:0">This is an H1</h1>
    <h2 data-sourcepos="5:1-7:71">This is an H2</h2>
    <blockquote data-sourcepos="7:1-12:52">
    <p data-sourcepos="7:3-9:72">This is a blockquote with two paragraphs. Lorem
    ipsum dolor sit amet,<br />
    consectetuer adipiscing elit. Aliquam hendrerit mi posuere lectus.<br />
    Vestibulum enim wisi, viverra nec, fringilla in, laoreet vitae, risus.</p>
    <p data-sourcepos="11:3-12:52">Donec sit amet nisl. Aliquam semper ipsum sit
    amet velit. Suspendisse<br />
    id – sem “consectetuer” libero luctus adipiscing.</p>
    </blockquote>
    */
