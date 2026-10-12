The rampart-cmark module
========================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-cmark module uses the
`cmark-gfm library <https://github.com/github/cmark-gfm>`_\ , GitHub's fork
of the `cmark library <https://github.com/commonmark/cmark>`_ that adds
support for GitHub Flavored Markdown (GFM) extensions.
The authors of Rampart extend our thanks to
`the authors and contributors <https://github.com/github/cmark-gfm/graphs/contributors>`_
to this library.

License
~~~~~~~

The cmark-gfm library is licensed under a
`2-clause BSD License <https://github.com/github/cmark-gfm/blob/master/COPYING>`_
and includes other licenses therein.

The rampart-cmark module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-cmark module processes
`CommonMark Markdown <https://commonmark.org/>`_ and converts it to HTML.
As of Rampart v0.6.3, the module uses the
`cmark-gfm <https://github.com/github/cmark-gfm>`_ library and supports
`GitHub Flavored Markdown <https://github.github.com/gfm/>`_ extensions
including tables, strikethrough, autolinks, task lists, and tag filtering.
Rampart v0.6.2 and earlier use the original
`cmark <https://github.com/commonmark/cmark>`_ library without GFM extensions.


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
      properties:

        **Rendering Options** (default to ``false``):

        * ``hardBreaks`` - A :green:`Boolean`. If ``true`` render softbreak
    	  elements as hard line breaks.

    	* ``unsafe`` - A :green:`Boolean`. If ``true``  render  raw  HTML
          and unsafe links (javascript:, vbscript:, file:, and data:, except
          for image/png, image/gif, image/jpeg, or image/webp mime
       	  types). By default, raw HTML is replaced by a placeholder HTML
          comment. Unsafe links are replaced by empty strings.

        * ``noBreaks`` - A :green:`Boolean`. If ``true`` render softbreak
          elements as spaces.

        * ``smart`` - A :green:`Boolean`. If ``true`` convert straight
          quotes to curly, ``---`` to em dashes, ``--`` to en dashes.

        * ``sourcePos`` -  A :green:`Boolean`. If ``true`` embed
          source position information in tags as attributes named
          ``data-sourcepos``.

        **GitHub Flavored Markdown Extensions** (default to ``true``):

        All GFM extensions are enabled by default.  Set any extension to
        ``false`` to disable it.

        * ``table`` - A :green:`Boolean`.  Enables
          `GFM table syntax <https://github.github.com/gfm/#tables-extension->`_\ .
          Pipe-delimited tables with a header separator row are rendered as
          HTML ``<table>`` elements with column alignment support.

        * ``strikethrough`` - A :green:`Boolean`.  Enables
          GFM strikethrough.  Text wrapped in ``~~double tildes~~`` is
          rendered as ``<del>`` elements.

        * ``autolink`` - A :green:`Boolean`.  Enables
          GFM autolinks.  Bare URLs and email addresses in the text are
          automatically converted to hyperlinks.

        * ``tagfilter`` - A :green:`Boolean`.  Enables
          GFM tag filtering.  Certain raw HTML tags that are potentially
          dangerous (such as ``<textarea>``, ``<style>``, ``<xmp>``,
          ``<iframe>``, ``<noembed>``, ``<noframes>``, ``<script>``,
          ``<plaintext>``) are escaped in the output even when ``unsafe``
          is enabled.

        * ``tasklist`` - A :green:`Boolean`.  Enables
          GFM task lists.  List items beginning with ``[ ]`` or ``[x]``
          are rendered with HTML checkbox inputs.

    Return Value:
        A :green:`String` - The document converted to HTML.

Examples
--------

Basic CommonMark
~~~~~~~~~~~~~~~~

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

GitHub Flavored Markdown Extensions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    var cmark = require("rampart-cmark");

    var out = cmark.toHtml(`
    # Project Status

    | Task          | Status  |
    |---------------|---------|
    | ~~Old task~~  | Done    |
    | New task      | Pending |

    ## Checklist

    - [x] Write the code
    - [x] Review changes
    - [ ] Deploy to production

    Visit https://example.com for details.
    `,
        {
            table: true,
            strikethrough: true,
            autolink: true,
            tasklist: true,
            smart: true
        }
    );

    console.log(out);

    /* expected output:
    <h1>Project Status</h1>
    <table>
    <thead>
    <tr>
    <th>Task</th>
    <th>Status</th>
    </tr>
    </thead>
    <tbody>
    <tr>
    <td><del>Old task</del></td>
    <td>Done</td>
    </tr>
    <tr>
    <td>New task</td>
    <td>Pending</td>
    </tr>
    </tbody>
    </table>
    <h2>Checklist</h2>
    <ul>
    <li><input type="checkbox" checked="" disabled="" /> Write the code</li>
    <li><input type="checkbox" checked="" disabled="" /> Review changes</li>
    <li><input type="checkbox" disabled="" /> Deploy to production</li>
    </ul>
    <p>Visit <a href="https://example.com">https://example.com</a> for details.</p>
    */

Client-Side Rendering
---------------------

The ``rampart-cmark`` module handles server-side Markdown-to-HTML conversion.
For full client-side rendering comparable to GitHub's appearance, you may wish
to include additional libraries for features that are handled in the browser
rather than by the Markdown parser itself.

Math Rendering — MathJax
~~~~~~~~~~~~~~~~~~~~~~~~

`MathJax <https://www.mathjax.org/>`_ is what GitHub uses for rendering
LaTeX math expressions.  By default MathJax 3 only recognizes ``\(...\)``
for inline math.  To also enable ``$...$`` (single-dollar) inline math,
add a configuration block before loading the library:

.. code-block:: html

    <script>
        MathJax = {
            tex: { inlineMath: [['$', '$'], ['\\(', '\\)']] }
        };
    </script>
    <script src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js">
    </script>

Display math using ``$$...$$`` works without any extra configuration.

Syntax Highlighting — highlight.js
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

`highlight.js <https://highlightjs.org/>`_ provides automatic syntax
highlighting for fenced code blocks.  The ``github.min.css`` theme closely
matches GitHub's appearance (``github-dark.min.css`` is available for dark
mode).

.. code-block:: html

    <link rel="stylesheet"
      href="https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11/build/styles/github.min.css">
    <script
      src="https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11/build/highlight.min.js">
    </script>
    <script>hljs.highlightAll();</script>

GitHub-Style Alerts
~~~~~~~~~~~~~~~~~~~

GitHub-style alerts (``[!NOTE]``, ``[!TIP]``, ``[!IMPORTANT]``,
``[!WARNING]``, ``[!CAUTION]``) are not part of any standard and no CDN
library exists for them.  A small client-side snippet can replicate the
behavior:

.. code-block:: javascript

    document.querySelectorAll('blockquote').forEach(function(bq) {
        var first = bq.querySelector('p:first-child');
        if (!first) return;
        var match = first.textContent.match(
            /^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]/
        );
        if (!match) return;
        var kind = match[1].toLowerCase();
        bq.className += ' markdown-alert markdown-alert-' + kind;
        first.className += ' markdown-alert-title';
        first.textContent = kind.charAt(0).toUpperCase() + kind.slice(1);
    });

Style with CSS to match GitHub's colors for each alert type.

Tables and General GitHub Styling — github-markdown-css
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

`github-markdown-css <https://github.com/sindresorhus/github-markdown-css>`_
is a community-maintained stylesheet that closely replicates GitHub's Markdown
rendering appearance, including tables, code blocks, blockquotes, and more.
Wrap your content in a ``<div class="markdown-body">`` for it to apply.

.. code-block:: html

    <link rel="stylesheet"
      href="https://cdn.jsdelivr.net/npm/github-markdown-css/github-markdown.min.css">

Full Minimal Page
~~~~~~~~~~~~~~~~~

Putting it all together, a minimal page that renders ``cmark-gfm`` output
with GitHub-like styling:

.. code-block:: html

    <!DOCTYPE html>
    <html>
    <head>
        <link rel="stylesheet"
          href="https://cdn.jsdelivr.net/npm/github-markdown-css/github-markdown.min.css">
        <link rel="stylesheet"
          href="https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11/build/styles/github.min.css">
        <script
          src="https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11/build/highlight.min.js">
        </script>
        <script>
            MathJax = {
                tex: { inlineMath: [['$', '$'], ['\\(', '\\)']] }
            };
        </script>
        <script
          src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js">
        </script>
        <script>
            document.addEventListener('DOMContentLoaded', function() {
                hljs.highlightAll();
                // GitHub-style alerts
                document.querySelectorAll('blockquote').forEach(function(bq) {
                    var first = bq.querySelector('p:first-child');
                    if (!first) return;
                    var match = first.textContent.match(
                        /^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]/
                    );
                    if (!match) return;
                    var kind = match[1].toLowerCase();
                    bq.className += ' markdown-alert markdown-alert-' + kind;
                    first.className += ' markdown-alert-title';
                    first.textContent =
                        kind.charAt(0).toUpperCase() + kind.slice(1);
                });
            });
        </script>
    </head>
    <body>
        <div class="markdown-body">
            <!-- your cmark-gfm HTML output here -->
        </div>
    </body>
    </html>

This covers the vast majority of what GitHub renders, with no server-side
processing beyond the ``cmark-gfm`` HTML output from ``toHtml()``.
