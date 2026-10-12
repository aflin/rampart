The rampart-totext module
=========================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-totext module uses the
`libdeflate <https://github.com/ebiggers/libdeflate>`_ library for
decompressing ZIP-based document formats (DOCX, PPTX, XLSX, ODT, ODP,
ODS, EPUB).  The developers of Rampart extend our thanks to the author
for this fast and portable decompression library.

For email and mbox files, the module uses the RFC 5322 and MIME parsing
layers of the
`libetpan <https://github.com/dinhvh/libetpan>`_ library, vendored as a
subset.  The developers of Rampart extend our thanks to its authors.

For HTML text extraction, the module relies on the
:ref:`rampart-html module <rampart-html:The rampart-html module>` and
for Markdown conversion, the
:ref:`rampart-cmark module <rampart-cmark:The rampart-cmark module>`.

For PDF conversion, the module optionally uses the
`pdftotext <https://www.xpdfreader.com/pdftotext-man.html>`_ utility
from the Xpdf or Poppler utilities package.  For legacy Microsoft Word
``.doc`` conversion, it optionally uses the
`catdoc <https://www.wagner.pp.ru/~vitus/software/catdoc/>`_ utility on
Linux and FreeBSD, or the built-in ``textutil`` command on macOS.

Image files, and PDF pages that carry no text layer (scans), are read
with optical character recognition through the
:ref:`rampart-ocr module <rampart-langtools:The rampart-ocr module>`
when a reader has been supplied with `setOcr`_.  Scanned PDF pages are
rasterized for it with Poppler's ``pdftoppm``.

License
~~~~~~~

The rampart-totext module is released under the MIT license.

The `libetpan <https://github.com/dinhvh/libetpan>`_ library is released
under the BSD 3-Clause license.

The `libdeflate <https://github.com/ebiggers/libdeflate>`_ library is
released under the
`MIT License <https://github.com/ebiggers/libdeflate/blob/master/COPYING>`_\ .

What does it do?
~~~~~~~~~~~~~~~~

The rampart-totext module extracts plain text from a variety of document
formats.  It is designed for use with search engines and semantic search,
where the primary goal is to retrieve all textual content from a
document while preserving paragraph boundaries.  Formatting such as
bold, italic, font changes and indentation are discarded — only the
text content and paragraph structure are retained.

How does it work?
~~~~~~~~~~~~~~~~~

The module identifies the file format by inspecting the content of the
file (magic bytes, document structure) and falls back to the file
extension when the content is ambiguous.  Based on the detected type, it
applies the appropriate extraction method:

*  **Built-in C converters** handle plain text, XML/Docbook, LaTeX, RTF,
   and troff/man page formats directly.

*  **Rampart module converters** use the ``rampart-html`` and
   ``rampart-cmark`` modules (via internal JavaScript evaluation) for
   HTML and Markdown formats.

*  **ZIP-based formats** (DOCX, PPTX, XLSX, ODT, ODP, ODS, EPUB) are
   decompressed in C using libdeflate, and the extracted XML or HTML
   content is then parsed for text.

*  **External tool converters** invoke ``pdftotext`` for PDF files and
   ``catdoc`` or ``textutil`` for legacy ``.doc`` files.  If the
   required external tool is not installed, an error is thrown.

*  **Images and scanned PDFs** go through a rampart-ocr reader, if one
   has been set with `setOcr`_.  PNG, JPEG, TIFF (including multi-page),
   GIF, BMP, PNM, PSD and HDR files are read in full.  For a PDF,
   ``pdftotext`` runs first and only the pages that yield no usable text
   are rasterized (``pdftoppm``) and read, so a document mixing born-digital
   and scanned pages comes out whole, and a PDF that already carries an
   OCR text layer is never re-read.  Without a reader, image files and
   PDFs that contain nothing but images throw an error saying so; every
   other PDF converts as before.

*  **Gzip-compressed files** are transparently decompressed before
   processing.  This is particularly useful for man pages, which are
   typically stored as ``.1.gz``, ``.2.gz``, etc. on the filesystem.
   After decompression, the ``.gz`` extension is stripped and the inner
   file format is detected normally.  Any supported format may be
   gzip-compressed.

*  **Unknown formats** are scanned for readable ASCII/UTF-8 text
   chunks.  Significant runs of text are extracted as paragraphs,
   allowing partial text recovery even from unrecognized binary formats.

*  **Source code and structured text** files (e.g. ``.csv``, ``.json``,
   ``.py``, ``.c``, ``.js``) are recognized by extension and returned
   as-is without whitespace normalization, preserving their original
   formatting.


Supported Formats
-----------------

The following table lists all supported formats, their detection method,
and any external dependencies.

.. list-table::
   :header-rows: 1
   :widths: 15 15 35 35

   * - Format
     - Extensions
     - Detection
     - Dependencies
   * - Plain Text
     - ``.txt``
     - Fallback (extension only)
     - None
   * - Source / Config
     - ``.csv``, ``.json``, ``.py``, ``.c``, ``.js``, etc.
     - Fallback (extension only)
     - None
   * - HTML
     - ``.html``, ``.htm``
     - ``<!DOCTYPE html>``, ``<html``
     - ``rampart-html``
   * - Markdown
     - ``.md``, ``.markdown``
     - Heuristic (headings, fenced divs, attributes)
     - ``rampart-cmark``, ``rampart-html``
   * - XML / Docbook
     - ``.xml``, ``.docbook``
     - ``<?xml``, docbook root elements
     - None
   * - LaTeX
     - ``.tex``, ``.latex``
     - ``\section{``, ``\href{``, ``\documentclass``, etc.
     - None
   * - RTF
     - ``.rtf``
     - ``{\rtf``
     - None
   * - Man Page
     - ``.1`` through ``.9``
     - ``.\"``, ``'.\"``, ``.TH``, ``.SH``
     - None
   * - DOCX
     - ``.docx``
     - ZIP with ``word/`` entries
     - None (libdeflate is built-in)
   * - PPTX
     - ``.pptx``
     - ZIP with ``ppt/`` entries
     - None (libdeflate is built-in)
   * - XLSX
     - ``.xlsx``
     - ZIP with ``xl/`` entries
     - None (libdeflate is built-in)
   * - ODT
     - ``.odt``
     - ZIP with mimetype ``application/vnd.oasis.opendocument.text``
     - None (libdeflate is built-in)
   * - ODP
     - ``.odp``
     - ZIP with mimetype ``application/vnd.oasis.opendocument.presentation``
     - None (libdeflate is built-in)
   * - ODS
     - ``.ods``
     - ZIP with mimetype ``application/vnd.oasis.opendocument.spreadsheet``
     - None (libdeflate is built-in)
   * - EPUB
     - ``.epub``
     - ZIP with mimetype ``application/epub+zip``
     - ``rampart-html`` (libdeflate is built-in)
   * - PDF
     - ``.pdf``
     - ``%PDF-``
     - ``pdftotext`` (external); for scanned pages also ``pdftoppm``
       and a rampart-ocr reader (see `setOcr`_)
   * - Legacy Word (.doc)
     - ``.doc``
     - OLE2 magic bytes (``\xD0\xCF\x11\xE0``)
     - ``catdoc`` (Linux/FreeBSD) or ``textutil`` (macOS)
   * - PNG, JPEG, GIF, BMP, PSD, HDR
     - ``.png``, ``.jpg``, ``.jpeg``, ``.gif``, ``.bmp``, ``.psd``, ``.hdr``
     - Magic bytes
     - a rampart-ocr reader (see `setOcr`_)
   * - TIFF (multi-page)
     - ``.tif``, ``.tiff``
     - ``II*``, ``MM*`` (and BigTIFF)
     - a rampart-ocr reader (see `setOcr`_)
   * - PNM
     - ``.pnm``, ``.ppm``, ``.pgm``, ``.pbm``
     - ``P1`` .. ``P6`` header
     - a rampart-ocr reader (see `setOcr`_)
   * - Email
     - ``.eml``, ``.emlx``
     - RFC 5322 header block with a ``Received``, ``Message-ID``,
       ``Return-Path`` or ``MIME-Version`` header
     - None (libetpan is built in)
   * - Mailbox
     - ``.mbox``, ``.mbx``
     - a ``From`` line (the word, a space, then a sender) followed by a header block
     - None (libetpan is built in)
   * - MHTML
     - ``.mht``, ``.mhtml``
     - MIME ``multipart/related``
     - ``rampart-html`` (libetpan is built in)


External Dependencies
---------------------

Most formats are handled entirely within the module.  Two formats
require external command-line utilities:

pdftotext
~~~~~~~~~

The ``pdftotext`` utility is used to extract text from PDF files.  It
is available in the ``poppler-utils`` or ``xpdf-utils`` package on most
systems:

*  **Debian/Ubuntu**: ``sudo apt install poppler-utils``
*  **Fedora/RHEL**: ``sudo dnf install poppler-utils``
*  **macOS**: ``brew install poppler``
*  **FreeBSD**: ``pkg install poppler-utils``

If ``pdftotext`` is not installed and a PDF file is passed to
``convertFile()`` or ``convert()``, an error will be thrown.

catdoc
~~~~~~

The ``catdoc`` utility is used to extract text from legacy Microsoft
Word ``.doc`` files on Linux and FreeBSD.  On macOS, the built-in
``textutil`` command is used instead.

*  **Debian/Ubuntu**: ``sudo apt install catdoc``
*  **Fedora/RHEL**: ``sudo dnf install catdoc``
*  **FreeBSD**: ``pkg install catdoc``
*  **macOS**: No installation required (``textutil`` is built-in).

If neither ``catdoc`` nor ``textutil`` is available and a ``.doc`` file
is passed to ``convertFile()`` or ``convert()``, an error will be thrown.

rampart-ocr and pdftoppm
~~~~~~~~~~~~~~~~~~~~~~~~

Optical character recognition is done by a reader from the
:ref:`rampart-ocr module <rampart-langtools:The rampart-ocr module>`,
which is part of the separately installed ``rampart-langtools`` package.
The reader is supplied with `setOcr`_; nothing is loaded until then.

Scanned PDF pages are rasterized with ``pdftoppm``, which ships in the
same package as ``pdftotext`` (``poppler-utils``; see above).  The
scanned-page check uses ``pdfimages`` from that package as well.

If an image, or a PDF that contains only images, is converted with no
reader set, an error is thrown that says which call sets one.


Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()``
function:

.. code-block:: javascript

    var totext = require("rampart-totext");


Functions
---------

The rampart-totext module exports four functions: ``convertFile()``,
``convert()``, ``identify()`` and ``setOcr()``.


convertFile
~~~~~~~~~~~

Extract plain text from a document file on disk.

Usage:

.. code-block:: javascript

    var text = totext.convertFile(filename[, details]);

        /* or, streaming one document at a time */

    var count = totext.convertFile(filename[, details], callback);

Where:

*  ``filename`` is a :green:`String`, the path to the file to convert.
   If the file is gzip-compressed (e.g. ``myfile.1.gz``), it will be
   transparently decompressed before conversion.

*  ``details`` is an optional :green:`Boolean` or :green:`Object`.
   If ``true`` or ``{details: true}`` is passed, the function returns
   an :green:`Object` instead of a :green:`String` (see below).

   The :green:`Object` form may also carry ``ocr``, which controls
   optical character recognition for this call:

   *  a reader from ``rampart-ocr.init()`` — use it for this call,
      instead of whatever `setOcr`_ established;

   *  ``"auto"`` (the default) — read image files, and only those PDF
      pages that have no usable text layer;

   *  ``"always"`` — read every PDF page, ignoring any text layer.  For
      PDFs whose embedded text layer is a poor legacy OCR job;

   *  ``true`` — build the default reader on first need, as
      ``setOcr(true)`` does.

   For email and mbox input, the :green:`Object` form also accepts:

   *  ``prefer`` — ``"text"`` (the default) or ``"html"``: which half of
      a ``multipart/alternative`` body to keep.  Exactly one is kept —
      keeping both would put every word of the message into the index
      twice.

   *  ``attachments`` — a :green:`Boolean`, default ``true``: whether to
      convert attachments.  When ``false``, each attachment still appears
      in ``documents`` with its ``title`` and ``mimeType``, but its
      ``text`` is empty.

   *  ``maxAttachment`` — a :green:`Number`, **unlimited by default**: an
      attachment larger than this once decoded is listed but not converted.
      Set it only to save time on large attachments deliberately; there is
      no memory reason to (see `Large attachments`_).

Return Value:
   By default, a :green:`String` containing the extracted plain text.
   Multi-page input (a PDF, a multi-page TIFF) has its pages separated
   by form feeds (``"\f"``), whether the text came from a text layer or
   from recognition.

   If ``details`` is set, an :green:`Object` with the following
   properties:

   *  ``text`` — a :green:`String`, the extracted plain text.

   *  ``mimeType`` — a :green:`String`, the MIME type of the detected
      input format (e.g. ``"text/html"``,
      ``"application/vnd.openxmlformats-officedocument.wordprocessingml.document"``,
      ``"image/tiff"``).  For unknown formats, the MIME type is
      ``"application/octet-stream"``.

   *  ``title`` — a :green:`String`, a title for the document, suitable for
      a Title column in a database.  It is **derived**, not simply copied:
      the first of the document's own title (``dc:title``, an HTML
      ``<title>``, a PDF ``Title``, a man page's ``.TH`` name) or, failing
      that, the basename of the file.  It is therefore effectively always
      present for ``convertFile()``.  It may be absent for ``convert()``,
      where there is no filename to fall back on and the content may carry
      no title of its own.

   *  ``metaData`` — an :green:`Object`, always present and possibly empty,
      holding what the document itself declared.  One schema is used for
      every format, so a consumer keying off these names does not need to
      know what kind of file it was:

      ``title``, ``author``, ``subject``, ``description``, ``keywords``,
      ``language``, ``created``, ``modified``

      A key appears only when the format supplied it, so its presence is
      meaningful.  Format-specific extras may appear alongside — a man page
      also reports ``section``, ``source`` and ``manual``, an EPUB reports
      ``publisher``.  Note that ``metaData.title`` is only what the document
      claimed, while ``title`` above is the derived value.

   *  ``documents`` — an :green:`Array`, **always** present and **always**
      holding at least one entry.  A file that yields a single document
      gives an Array of one, so a caller writes the same code either way.
      Each entry is an :green:`Object` with ``text`` and, where they apply,
      ``mimeType``, ``title``, ``metaData``, ``ocr``, ``pages``, ``charset``
      and ``charsetSource``.

      ``text`` is always the concatenation of the documents' text, joined
      with a single space::

          details.text === details.documents.map(function(d){return d.text}).join(" ")

      and is byte for byte what ``convertFile()`` returns without
      ``details``.

   *  ``charset`` — a :green:`String`, the encoding the file's bytes were
      decoded **from** before conversion (e.g. ``"UTF-8"``,
      ``"windows-1252"``).  Present only for the formats whose text comes
      from the file's own bytes — plain text, source, HTML, XML, Markdown,
      LaTeX, RTF and man pages.  A PDF or a DOCX has no source encoding to
      report, and neither property appears for one.

   *  ``charsetSource`` — a :green:`String`, how that encoding was
      determined: ``"bom"`` (a byte-order mark), ``"declared"`` (an HTML
      ``<meta charset>`` or XML ``encoding=``), ``"utf-8"`` (the bytes are
      valid UTF-8), or ``"assumed"`` (neither declared nor valid UTF-8, so
      a single-byte encoding was assumed).

   *  ``ocr`` — a :green:`Boolean`, always present, whether the text of
      ``documents[0]`` came from optical character recognition.

      ``ocr``, ``pages``, ``charset`` and ``charsetSource`` at the top level
      describe the **primary document** — ``documents[0]`` — and are the
      same objects as the ones on that entry, not copies.  For a file that
      yields a single document, which is every format listed above, that is
      simply the document.

   *  ``pages`` — present when ``ocr`` is ``true``: an :green:`Array`
      with one entry per recognized page, each the :green:`Object`
      that
      :ref:`reader.readText() <rampart-langtools:reader.readText()>`
      returned (``lines`` with their boxes and confidence scores, and
      ``text``), with ``page`` set to the page's position in the
      document, counting from ``0``.  For a PDF, only the pages that
      were actually recognized appear here.

*  ``callback`` is an optional :green:`Function`.  When given, each document
   is passed to it as it is produced and released before the next one is
   built, so nothing accumulates and memory stays flat however large the
   input.  See `Streaming with a callback`_ below.

Example:

.. code-block:: javascript

    var totext = require("rampart-totext");

    /* convert an HTML file to text */
    var text = totext.convertFile("/path/to/document.html");
    console.log(text);

    /* convert a DOCX file with details */
    var result = totext.convertFile("/path/to/report.docx", true);
    console.log(result.mimeType);  // "application/vnd.openxmlformats-..."
    console.log(result.text);

    /* convert a gzipped man page */
    var text = totext.convertFile("/usr/share/man/man1/ls.1.gz");
    console.log(text);

    /* convert a PDF (requires pdftotext) */
    try {
        var text = totext.convertFile("/path/to/paper.pdf");
        console.log(text);
    } catch(e) {
        console.log("PDF conversion failed:", e.message);
    }


convert
~~~~~~~

Extract plain text from in-memory document content.  This function
behaves the same as ``convertFile()`` but takes a :green:`String` or
:green:`Buffer` of document content instead of a filename.

Usage:

.. code-block:: javascript

    var text = totext.convert(content[, details]);

Where:

*  ``content`` is a :green:`String` or :green:`Buffer` containing the
   document data to convert.  If the data is gzip-compressed, it will
   be transparently decompressed before conversion.

*  ``details`` is an optional :green:`Boolean` or :green:`Object`,
   with the same behavior as in ``convertFile()``.

Return Value:
   Same as ``convertFile()``.

   Note: since no filename is available, file type detection relies
   entirely on content inspection.  For ambiguous formats (e.g. plain
   text vs. Markdown), the content heuristic determines the type.

   For PDF and legacy ``.doc`` formats, the content is passed to the
   external tool via standard input.  Rasterizing scanned PDF pages for
   recognition needs a file, so in that case the content is written to
   a temporary file for the duration of the call.

Example:

.. code-block:: javascript

    var totext = require("rampart-totext");
    rampart.globalize(rampart.utils);

    /* convert a buffer read from a file */
    var buf = readFile("/path/to/document.docx");
    var text = totext.convert(buf);
    console.log(text);

    /* convert with details */
    var result = totext.convert(buf, {details: true});
    console.log(result.mimeType);  // "application/vnd.openxmlformats-..."

    /* convert an HTML string directly.  Note that when converting content
       rather than a named file, the format is detected from the content
       itself, so the string must carry a document-level signature such as
       <!DOCTYPE html> or <html>.  A bare fragment cannot be identified and
       is returned unchanged. */
    var html = "<html><body><h1>Hello</h1><p>World</p></body></html>";
    var text = totext.convert(html);
    console.log(text);  // "Hello\n\nWorld\n\n"


identify
~~~~~~~~

Identify the file format of a document without converting it.

Usage:

.. code-block:: javascript

    var type = totext.identify(filename);

    /* or */

    var type = totext.identify(buffer);

Where:

*  ``filename`` is a :green:`String`, the path to the file to identify.

*  ``buffer`` is a :green:`Buffer` containing the document data.

   If the data is gzip-compressed, it will be transparently
   decompressed before identification.

Return Value:
   A :green:`String`, one of the following type names:

   ``"text"``, ``"plaintext"``, ``"html"``, ``"markdown"``, ``"xml"``,
   ``"latex"``, ``"rtf"``, ``"man"``, ``"pdf"``, ``"docx"``, ``"pptx"``,
   ``"xlsx"``, ``"odt"``, ``"odp"``, ``"ods"``, ``"epub"``, ``"doc"``,
   ``"png"``, ``"jpeg"``, ``"tiff"``, ``"gif"``, ``"bmp"``, ``"pnm"``,
   ``"psd"``, ``"hdr"``, ``"email"``, ``"mbox"``, ``"mhtml"``, or
   ``"unknown"``.

   The file type is determined primarily by inspecting the content.
   If the content is ambiguous, the file extension is used as a
   fallback (when a filename is provided).

Example:

.. code-block:: javascript

    var totext = require("rampart-totext");
    rampart.globalize(rampart.utils);

    var type = totext.identify("myfile.docx");
    console.log(type);  // "docx"

    var buf = readFile("presentation.pptx");
    console.log(totext.identify(buf));  // "pptx"


setOcr
~~~~~~

Supply the reader used for optical character recognition of image
files and scanned PDF pages.

Usage:

.. code-block:: javascript

    totext.setOcr(reader);

    /* or */

    totext.setOcr(true);
    totext.setOcr(options);

    /* or */

    totext.setOcr(false);

Where:

*  ``reader`` is the :green:`Object` returned by
   :ref:`rampart-ocr.init() <rampart-langtools:ocr.init>`.  This is the
   usual form: you choose the model, GPU and thread settings.

*  ``true`` or ``options`` asks the module to build a reader itself the
   first time one is needed, resolving the models through rampart-models
   and downloading them on first use, with ``options`` passed to
   ``rampart-ocr.init()``.  ``true`` is the same as ``{}``.

   That reader includes the **layout model** as well as the OCR set, so
   multi-column scans are read down their columns rather than across
   them.  It is what makes the difference between a two-column page
   coming back in order and coming back interleaved.  The cost is the
   download: 145 MB on first use rather than 21 MB.

   *  ``{layout: false}`` opts out, fetching only the 21 MB OCR set.
      Use it when the corpus is known to be single-column.  A page with
      columns will then be read across them.

   *  A failed or declined layout download is not fatal: you get a
      working reader that cannot order columns, rather than a failed
      conversion.

   The reader uses the GPU when rampart-onnx has one and the CPU
   otherwise.  rampart-ocr's own default is a single thread; pass
   ``{threads: 0}`` to use every core on a CPU.

*  ``false`` (or nothing) removes the reader.

Return Value:
   ``undefined``.

The reader is stored on the module :green:`Object` itself.  A
:green:`Function` called on that object — ``totext.convertFile(...)``
rather than a detached ``var cf = totext.convertFile`` — finds it, and
so does a thread that received the object as a copied global.  A fresh
``require("rampart-totext")`` inside a thread yields a new module
:green:`Object` with no reader; call ``setOcr()`` on that one, or pass
``{ocr: reader}`` per call.

Example:

.. code-block:: javascript

    var totext = require("rampart-totext");
    var ocr    = require("rampart-ocr");
    var models = require("rampart-models");

    /* the simple form: models fetched on first use, layout included */
    totext.setOcr(true);

    /* or build the reader yourself */
    var paths = models.ocrGet("ppocr-v5");
    paths.layout = models.ocrGet("ppocr-layout").layout;
    totext.setOcr(ocr.init(paths, {threads: 0}));

    /* a scanned, multi-page TIFF */
    var text = totext.convertFile("/scans/deposition-0042.tif");

    /* a PDF: text pages come from pdftotext, scanned pages are read */
    var res = totext.convertFile("/scans/production.pdf", {details: true});
    if (res.ocr)
        console.log("recognized pages:", res.pages.map(function(p){ return p.page; }));


Streaming with a callback
-------------------------

Without a callback, ``details`` returns every document in ``documents`` and
the whole extracted text again in ``text``, so all of it is held at once.
That is convenient for a report and unsuitable for a large mailbox.  Passing
a :green:`Function` streams instead: one document is built, handed to the
callback and released before the next is built.

.. code-block:: javascript

    var n = totext.convertFile("archive.mbox", function(doc) {
        /* doc has exactly the shape of a documents[] entry */
        db.insert({ title: doc.title, body: doc.text, from: doc.metaData.from });
    });

    /* the same three forms are accepted, and convert() takes them too */
    totext.convertFile(f, callback);                    /* details implied */
    totext.convertFile(f, true, callback);
    totext.convertFile(f, {prefer:"html"}, callback);
    totext.convert(buffer, callback);

Where:

*  The **return value** is a :green:`Number`: how many documents were passed
   to the callback.  There is no ``text`` and no ``documents`` — everything
   is in the object handed to the callback.

*  Returning ``false`` from the callback **stops the conversion**.  Nothing
   further is parsed, decoded or converted, and the return value is the
   number of documents actually delivered.  For a file that yields a single
   document, returning ``false`` has no effect: there is nothing left to
   stop.

*  The callback is invoked **at least once** for any file, so a caller writes
   one loop whatever it was given.  A ``.docx`` calls it once; an mbox calls
   it once per message and once per attachment, in the same order
   ``documents`` would have held them.

*  Joining the ``text`` of everything the callback received with a single
   space reproduces exactly what ``convertFile()`` returns without
   ``details``.

*  ``details`` is redundant alongside a callback, since document objects are
   delivered either way; ``convertFile(f, false, callback)`` is not an error.

*  An exception thrown by the callback propagates out of ``convertFile()``.

.. _Large attachments:

Large attachments
-----------------

Attachments that will not be converted — a video, an archive, anything over
an explicit ``maxAttachment`` — are **never decoded into memory**.  The
decision is taken from the encoded size and the declared type before any
memory is spent, and anything not ruled out that way has a few kilobytes
decoded so its real type can be identified from the bytes.  Such parts still
appear, with their ``title`` and ``mimeType`` and an empty ``text``, so
nothing is hidden from the caller.

An attachment that *will* be converted and is larger than 4 MB decoded is not
held in ordinary memory either.  It is decoded in pieces into a temporary
file, which is mapped and unlinked immediately, so it never appears in the
filesystem and needs no cleanup.  This matters because ordinary heap memory
cannot be reclaimed by the kernel when there is no swap, whereas a file-backed
mapping can: the difference is between a conversion that gets slower under
memory pressure and one that is killed by it.  Converting a message with a
200 MB attachment:

.. code-block:: text

                              peak unreclaimable memory
      held in memory                   200 MB
      decoded to a mapping               1 MB

Under a 128 MB limit with swap disabled, the first is killed and the second
completes.  The temporary file is created only when an attachment actually
exceeds the threshold, so ordinary mail never touches the disk, and it is
reused for the rest of the call rather than recreated per attachment.

The temporary file is placed in ``TMPDIR``, falling back to ``/tmp`` and then
``/var/tmp``.  Directories on a memory-backed filesystem (``tmpfs``) are
skipped, since spilling to one would defeat the purpose; if every candidate is
memory-backed, conversion proceeds in ordinary memory instead.

Output Format
-------------

The ``convertFile()`` and ``convert()`` functions return plain text
formatted for search indexing and semantic analysis:

*  **Paragraph separation** — Block-level elements (headings,
   paragraphs, list items, table cells) are separated by double
   newlines (``\n\n``).

*  **Whitespace normalization** — For document formats (HTML, DOCX,
   RTF, etc.), consecutive spaces, tabs and single newlines within a
   paragraph are collapsed to a single space.  For source code and
   structured text files (``.csv``, ``.json``, ``.py``, etc.), the
   original formatting is preserved.

*  **Trimming** — Leading and trailing whitespace is removed from the
   output.  Note that this applies to the ``xml``, ``latex``, ``rtf`` and
   plain-text paths; the HTML and Markdown converters may leave trailing
   newlines in place.

*  **Entity decoding** — HTML and XML entities (e.g. ``&amp;``,
   ``&#8220;``, ``&nbsp;``) are decoded to their Unicode equivalents.

*  **Text that is not visible but is still text** — For HTML, Markdown and
   EPUB, the ``alt`` text of images and the ``content`` of
   ``<meta name="description">`` and ``<meta name="keywords">`` are
   extracted along with the visible text.  These are prose someone wrote
   about the document, which is what a search index wants.  Addresses are
   not: an ``<a href>`` or an ``<img src>`` is discarded, and only the
   visible link text is kept.

*  **Formatting removal** — Bold, italic, font changes, colors,
   indentation, and other visual formatting are discarded.  List numbering
   is generated formatting and is not emitted: an ordered list contributes
   its items' text, not "1.", "2.", "3.".

*  **Tag stripping** — All markup tags (HTML, XML, RTF control words,
   LaTeX commands, troff macros) are removed.  An inline tag that is
   stripped will leave a space to prevent adjacent words from being
   concatenated.


Notes on Specific Formats
-------------------------

DOCX
~~~~

The module extracts text from the main document body
(``word/document.xml``).  The actual document path is resolved by
parsing ``_rels/.rels``, so non-standard paths (e.g.
``word/document2.xml``) are handled correctly.  Headers, footers,
footnotes, endnotes, and comments stored in separate XML files within
the ZIP archive are not currently extracted.

PPTX
~~~~

The module iterates over all slide XML files
(``ppt/slides/slide1.xml``, ``slide2.xml``, etc.) in the ZIP archive
and extracts text from each.  Text from all slides is concatenated
with paragraph breaks between slides.

XLSX
~~~~

The module reads the workbook part for the sheet names and their order,
then walks each worksheet cell by cell, in reading order.  Cells holding
strings are resolved through the shared string table
(``xl/sharedStrings.xml``), inline strings are taken as they stand, and
numeric cells are emitted as their values — a spreadsheet is mostly not
strings, and numbers never appear in the string table.

Cells whose format marks them as dates are converted from the serial
number a workbook actually stores to an ISO date, so a cell displaying
``2026-03-14`` is extracted as ``2026-03-14`` rather than as ``46095``.

Each sheet is introduced by its name, rows are separated by newlines and
the cells within a row by tabs, so a value stays beside the label it
belongs to.  Formulas contribute their last computed result, not their
source; cells holding an error value contribute nothing.

ODT / ODP / ODS
~~~~~~~~~~~~~~~~

The module extracts text from ``content.xml``, which in the ODF format
contains the main document body as well as headers, footers, footnotes,
and annotations.  All three OpenDocument formats (text, presentation,
spreadsheet) use the same ``content.xml`` structure.  The module is
compatible with ODF versions 1.0 through 1.3 and files produced by
OpenOffice, LibreOffice, and other ODF-compliant applications.

EPUB
~~~~

The module reads ``META-INF/container.xml`` to find the OPF package, and
concatenates the content documents in **spine order** — the order the
book is meant to be read in — resolving each ``itemref`` through the
manifest.  Documents that are not in the spine, such as the navigation
document, are left out.  If the package cannot be read, the module falls
back to concatenating every ``.xhtml``, ``.html`` and ``.htm`` file in
the archive, in the order the archive stores them.  The extracted HTML
is then processed using the ``rampart-html`` module.

Markdown
~~~~~~~~

The module preprocesses the Markdown source to remove Pandoc extensions
(fenced div markers ``:::``, attribute spans ``{.class #id}``) before
passing the content to ``rampart-cmark`` for conversion to HTML and
then to ``rampart-html`` for text extraction.  Standard CommonMark
syntax is fully supported.

PDF
~~~

PDF text extraction is delegated to the external ``pdftotext`` utility,
which is invoked with UTF-8 encoding.  When using ``convertFile()``,
the filename is passed directly to ``pdftotext``.  When using
``convert()`` with a buffer, the content is passed via standard input.

The quality of the extracted text depends on the PDF's internal
structure — PDFs created from text documents generally produce excellent
results, while scanned documents (image-only PDFs) will produce no text
output.

Email and mbox
~~~~~~~~~~~~~~

An ``.eml`` file is one RFC 5322 message; an mbox holds many, separated by
``From`` lines (the word followed by a space) at the start of a line.  Either way the module walks the
MIME tree and produces **one entry in** ``documents`` **per part**: the
message body, then each attachment, then each nested forwarded message.

*  A ``multipart/alternative`` body contributes exactly one document — the
   ``text/plain`` half by default, the ``text/html`` half with
   ``{prefer:"html"}``.

*  **Attachments are converted, not merely listed.**  An attachment's
   decoded bytes go back through the same identification and conversion
   used for a file on disk, so a PDF, DOCX, ODT, EPUB or image attachment
   yields its text exactly as that file would — including optical
   character recognition, when a reader has been set with `setOcr`_.  The
   declared ``Content-Type`` is deliberately ignored in favour of
   inspecting the bytes, because many mailers label every attachment
   ``application/octet-stream``; the filename is used only as a hint.

*  ``Content-Transfer-Encoding`` (``base64``, ``quoted-printable``) is
   decoded, each part is converted from its own declared ``charset``, and
   header values are decoded from RFC 2047 encoded-words, so a
   ``Subject`` of ``=?utf-8?B?...?=`` is reported as text.

*  ``metaData`` carries ``subject``, ``from``, ``to``, ``cc``, ``date``
   and ``messageId``.  An attachment that has no metadata of its own
   refers to its message's, so in an mbox an attachment can be traced back
   to the message it arrived in.

*  A ``multipart/signed`` message converts normally — the signature part
   is skipped.  A ``multipart/encrypted`` one cannot be read without keys,
   and is recorded as a document with no text rather than having its
   base64 dumped into the output.

*  An attachment that needs a converter which is not installed does not
   fail the message: that part is listed with empty text and the rest of
   the mail converts.

Apple Mail's ``.emlx`` wrapper (a byte count, then the message) and MHTML
``.mht``/``.mhtml`` files (a web page saved as ``multipart/related``) are
read by the same code.

Note that detection of an email without a helpful extension is a
heuristic, not a signature: a header block is required **and** at least one
header that does not occur in ordinary prose (``Received``,
``Message-ID``, ``Return-Path``, ``MIME-Version``).  A ``.txt`` file that
merely begins ``Subject:`` remains plain text.

Legacy Word (.doc)
~~~~~~~~~~~~~~~~~~

The legacy ``.doc`` format (OLE2 Compound Document) is a proprietary
binary format.  Text extraction is delegated to ``catdoc`` on Linux
and FreeBSD, or to ``textutil`` on macOS.  When using ``convert()``
with a buffer, the content is passed to the tool via standard input.
