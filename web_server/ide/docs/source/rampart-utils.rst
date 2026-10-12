rampart.utils
=============

Utility functions are provided in the global ``rampart.utils`` :green:`Object`.
These functions bring file io and other functionality to Duktape JavaScript.


License
"""""""

The modified ``printf`` code used in the rampart utilities and in
:ref:`the Rampart Server Module <rampart-server:The rampart-server HTTP module>`
is provided by
`Marco Paland <https://github.com/mpaland/printf>`_ under the
`MIT License <https://github.com/mpaland/printf/blob/master/LICENSE>`_\ .

The ``%!H`` HTML escape decoding is provided by
`Christoph Gärtner <https://bitbucket.org/cggaertner/cstuff/src/master/entities.c>`_
under the  `Boost License <https://www.boost.org/users/license.html>`_\ .

The utilities are included in Rampart JavaScript and as such are licensed under
the same MIT license.

General Utilities
"""""""""""""""""

printf
''''''

Print a formatted string to stdout.  Provides C-like
`printf(3) <https://man7.org/linux/man-pages/man3/printf.3.html>`_
functionality in JavaScript.


Usage:

.. code-block:: javascript

   rampart.utils.printf(fmt, ...)

Return Value:
   :green:`Number`. The length in bytes of the printed string.

Standard formats:  Most of the normal flags and formats are respected.
See standard formats and flags from
`printf(3) <https://man7.org/linux/man-pages/man3/printf.3.html>`_.

Extended (non-standard) formats:

   * ``%s`` - corresponding argument is treated as a :green:`String`
     (converted/coerced if necessary; :green:`Objects` are converted the
     same as for ``%J`` and :green:`Buffers`
     are printed as is).

   * ``%S`` - same as ``%s`` except an error is thrown if the corresponding argument is
     not a :green:`String`.

   * ``%J`` - print :green:`Object` as JSON.  An optional width (i.e.
     ``printf("%4J", obj);``) may be given which will print with new lines and
     indentation of the specified amount. Thus ``printf("%4J", obj);`` is
     equivalent to ``printf("%s", JSON.stringify(obj, null, 4) );``.

      * if ``!`` flag is present, a safe version of JSON will be printed where
        any references to inner :green:`Objects` are marked.  See second
        example below.  Note: if ``!`` is omitted but printing would fail
        because of cyclic references, then ``!`` is implied.  Also this flag 
        will print any methods of an object which would otherwise be hidden
        with values such as ``myfunc: {"_ecmascript_func": true}``.

   * ``%B`` - print contents of a :green:`Buffer` or :green:`String` as
     base64.

      * If ``!`` flag present, it decodes a :green:`Buffer` or
        :green:`String` containing base64 (throws an error if not valid
        base64).

      * If a width is given (e.g. ``%80B``), a newline will be printed
        after every ``width`` characters.

      * If the ``-`` flag is present and ``!`` is not present, the output
        will be a modified url safe base64 (using ``-`` and ``_`` in place
        of ``+`` and ``/``).

      * If the ``0`` flag is given (e.g. ``%0B`` or ``%-080B``), and ``!``
        is not present, the output will not be padded with ``=`` characters.

   * ``%U`` - url encode (or if ``!`` flag present, decode) a :green:`String`
     or :green:`Buffer`.  Without the ``!`` flag, :green:`Objects` are
     converted to JSON first.

   * ``%H`` - html encode (or if ``!`` flag present, decode) a :green:`String`
     or :green:`Buffer`.  Unlike ``%U`` above, :green:`Objects` are not
     accepted and will throw an error; convert with ``JSON.stringify()``
     first if needed.

   * ``%P`` - pretty print a :green:`String` or :green:`Buffer`.  Expects
     text with white space.  Format is ``%[!][-][i][.w]P`` where:

     * ``i`` is the optional level of indentation.  Each output line will be indented
       by this amount.  Default is ``0``.  If ``0``, the indent level for
       each paragraph will match the indentation of the first line of the corresponding
       paragraph in the input text (number of spaces at beginning of the paragraph).

     * ``-`` when used with the ``!`` flag optionally sets indentation to 0
       on all lines regardless of ``i`` or leading white space on first line.

     * ``.w`` where ``w`` is the optional length of each line (default ``80`` if not
       specified).

     * ``!`` specifies, if present, that newlines are not converted to spaces (but text
       after newlines is still indented).  In all cases, a double newline
       ("\\n\\n") is considered a separator of paragraphs and is respected.

   * ``%w`` - a shortcut format for ``%!-.wP`` - where ``w`` is effectively unlimited.
     Remove all leading white space from each line and don't wrap lines.

   * ``%C`` - like ``%c`` but prints multi-byte character.  Example:

     ``rampart.utils.printf("%C", 0xf09f9983);`` prints ``🙃``.

     Requires a number, 1-4 bytes (``0``-``4294967295``, or ``0x0``-``0xffffffff``).

   * ``%M`` - print multiline string.  Corresponding argument must be an :green:`Array`.  This
     prints each member of the array without scrolling the terminal when called multiple
     times.

     Example:

    .. code-block:: javascript

        for (var i=0,j=0; i<20; i++,j+=10) {
            rampart.utils.printf("%M", [`line 1: ${i}`, `line 2: ${j}`]);
            rampart.utils.sleep(0.2)
        }
        rampart.utils.printf("done\n");
   

    

Extended (non-standard) flags:

    * ``a`` - For all formats, accent output with color, if on a terminal
      that supports colors and using ``printf(...)`` or ``fprintf(stderr,...)``.
      Colors must be provided in a :green:`String` before the
      parameter that will be colorized.  Format for the color parameter is
      ``foregroundColor[, backgroundColor[, [option]]]`` where:

        * ``ForegroundColor`` is the color of the text.

        * ``backgroundColor`` is the background color.

        * ``option`` is ``blink`` or ``flashing`` for blinking text.
        
      Color is specified in a css compatible ``rgb(x, x, x)``
      format, by providing a `named css color <https://en.wikipedia.org/wiki/Web_colors>`_ 
      or by providing a `named terminal color <https://plumbum.readthedocs.io/en/latest/colorlib.html#color-support>`_\ .

    * ``^`` - same as ``a`` except 16 color mode is forced.

    * ``A`` - same as ``a`` except 256 color mode is forced.

    * ``@`` - same as ``a`` except truecolor mode is forced.

    * ``,`` - For base-10 numeric formats (``%d``, ``%i``, ``%u``, ``%lld``,
      ``%f``, etc.), group the integer portion of the number with commas as
      thousands separators.  For floating point formats, only the digits before
      the decimal point are grouped.  Always a comma, independent of locale.

    * ``'`` - The standard C grouping flag: same as ``,`` but uses the current
      locale's separator (nothing under the ``C`` locale).

    Both flags also work with :green:`BigInt` values.

    Example:

    .. code-block:: javascript

        rampart.utils.printf("%,d\n",   123456789098765);
        rampart.utils.printf("%,lld\n", 123456789098765);
        rampart.utils.printf("%,f\n",   123456789098765.0987);

    Output:

    .. code-block:: text

        123,456,789,098,765
        123,456,789,098,765
        123,456,789,098,765.093750

    Although ``%d`` denotes a 32-bit integer, a :green:`Number` whose value
    exceeds the 32-bit range is automatically promoted to a 64-bit integer
    (as if ``%lld`` had been given) so the correct value is printed rather than
    a truncated one.  The same promotion applies to ``%u``, ``%x``, ``%o`` and
    ``%b``.

:green:`BigInt` arguments:

    All numeric formats accept a :green:`BigInt`:

    * For the integer formats (``%d``, ``%i``, ``%u``, ``%x``, ``%X``, ``%o``,
      ``%b``, with or without an ``l``/``ll`` length modifier) the full,
      arbitrary-precision value is printed.  Width, zero/space padding, the
      sign flags (``+``/space), the ``#`` prefix and the ``,`` grouping flag
      (base 10) are all honored.

    * For the floating point and exponential formats (``%f``, ``%e``, ``%g``
      and their uppercase variants) the value is formatted directly from the
      :green:`BigInt`\ 's exact digits — there is no conversion to a
      :green:`Number` and therefore no precision loss, even at high precision
      (e.g. ``%.40e``) or for magnitudes above 2\ :sup:`53`.

    * ``%s`` prints the :green:`BigInt`\ 's decimal string.

    * ``%J`` prints ``{}``, matching ``JSON.stringify`` which cannot serialize
      a :green:`BigInt`.

    .. code-block:: javascript

        var big = BigInt("123456789012345678901234567890");
        rampart.utils.printf("%,d\n", big);
        // 123,456,789,012,345,678,901,234,567,890

Color with ``%H`` format:

    * Using ``%aH`` or any of the forced versions (``^``, ``A`` or ``@``) will perform the same operation as
      with other format codes.  In addition, it will wrap the text in a span with the foreground and background
      colors set as specified.  It will also add the ``option`` as a class name to the span.

    Example:

    .. code-block:: javascript
    
        rampart.utils.printf("%aH", "green, white, myclassname", "Text in a span");

    Output would be as follows:

        :green:`<span class="rp-color myclassname" style="color:Green; background-color:White;">Text in a span</span>`

    If using ``sprintf`` or if not printing to the terminal, output would be as follows:

        <span class="rp-color myclassname" style="color:Green; background-color:White;">Text in a span</span>
    
    If the plus flag is included (``%+aH``), rgb colors will be converted to the closest named css color.

    .. code-block:: javascript

        rampart.utils.sprintf("%+aH", "rgb(222,128,44)", "Text in a span");

    The return value would be as follows:
    
        <span class="rp-color" style="color:Peru;">Text in a span</span>

Colors with ``%J`` format:

    * Using ``%aJ`` or any of the forced versions (``^``, ``A`` or ``@``), takes as the color argument a number (palette number)
      rather than a foreground and background color.  There are currently four palettes, numbered ``0-3``.  Anything not a number
      or outside of that range will default to ``0``.

    .. code-block:: javascript
    
        var myObj = {Number:1.23, Bool:true, String:"Hello World!", Null: null};
        rampart.utils.printf("%a3J\n", 0, myObj);
        rampart.utils.printf("%a3J\n", 1, myObj);
 
    .. raw:: html

       <div class="highlight json-custom"><pre style=padding:0.6em;">
       {
          <span class="nt0">"Number"</span>: <span class="n0">1.23</span>,
          <span class="nt0">"Bool"</span>: <span class="b0">true</span>,
          <span class="nt0">"String"</span>: <span class="s0">"Hello World!"</span>,
          <span class="nt0">"Null"</span>: <span class="N0">null</span>
       }
       {
          <span class="nt1">"Number"</span>: <span class="n1">1.23</span>,
          <span class="nt1">"Bool"</span>: <span class="b1">true</span>,
          <span class="nt1">"String"</span>: <span class="s1">"Hello World!"</span>,
          <span class="nt1">"Null"</span>: <span class="N1">null</span>
       }
       </pre></div>

    If palette option is omitted, and ``myObj`` is not a :green:`Number`, palette ``0`` will be assumed:

    .. code-block:: javascript

        rampart.utils.printf("%a3J\n", myObj); //palette 0 assumed

Basic printf example:

.. code-block:: javascript

   var uenc = "a+url+encoded+string.+%27%23%24%3f%27";

   rampart.utils.printf("Encoded: %s\nDecoded: %!U\n", uenc, uenc);

   /* expected output:
   Encoded: a+url+encoded+string.+%27%23%24%3f%27
   Decoded: a url encoded string. '#$?'
   */

   var getty = "Four score and seven years ago our fathers\n" +
            "brought forth on this continent, a new nation,\n" +
            "conceived in Liberty, and dedicated to the proposition\n" +
            "that all men are created equal."

   rampart.utils.printf("%5.40P\n", getty);
   /* or
        rampart.utils.printf("%*.*P\n", 5, 40, getty);
   */

   /* expected output:
        Four score and seven years ago our
        fathers brought forth on this
        continent, a new nation, conceived
        in Liberty, and dedicated to the
        proposition that all men are
        created equal.
   */

    var html =
    "<html>\n"+
    "  <body>\n"+
    "    <div>\n"+
    "      content\n"+
    "    </div>\n"+
    "  </body>\n"+
    "</html>\n";

    /* remove leading white space */
    /* rampart.utils.printf("%!-.1000P", html); */
    /* or more simply as */
    rampart.utils.printf("%w", html);

    /* expected output
    <html>
    <body>
    <div>
    content
    </div>
    </body>
    </html>
    */

Example printing JSON with cyclic references:

.. code-block:: javascript

   var x = {a:{c:1},b:{} };
   x.b.a = x.a;

   // without "!", it is printed as normal
   rampart.utils.printf("%3J\n", x);

   /* expected results
      {
         "a": {
            "c": 1
         },
         "b": {
            "a": {
               "c": 1
            }
         }
      }
   */

   // with "!" all self references are printed
   rampart.utils.printf("%!3J\n", x);

   /* expected results:
      {
         "a": {
            "c": 1
         },
         "b": {
            "a": {
               "_cyclic_ref": "$.a"
            }
         }
      }
   */

   // add cyclic ref
   x.x_ref = x;

   // with a cyclic ref present, "!" is implied
   rampart.utils.printf("%3J\n", x);

   /* expected results:
      {
         "a": {
            "c": 1
         },
         "b": {
            "a": {
               "_cyclic_ref": "$.a"
            }
         },
         "x_ref": {
            "_cyclic_ref": "$"
         }
      }
   */

sprintf
'''''''

Same as ``printf()`` except a :green:`String` is returned

Return Value:
   :green:`String`. The formatted string.

bprintf
'''''''

Same as ``sprintf()`` except a :green:`Buffer` is returned.

Return Value:
   :green:`Buffer`.  The formatted string as a :green:`Buffer`.

abprintf
''''''''

Same as ``bprintf()`` except a provided :green:`Buffer` is resized and appended.

Usage:

.. code-block:: javascript

    var newBuf = abprintf(oldbuf[, start], fmt, ...);

Where:

    * ``oldbuf`` is a :green:`Buffer` - the :green:`Buffer` to be appended.

    * ``start`` is an optional :green:`Number`.  ``oldbuf`` is first truncated
      to this length and the formatted output is then appended (so any bytes of
      ``oldbuf`` at and after ``start`` are discarded).  For example, if
      ``oldbuf`` holds ``"hello"``, ``abprintf(oldbuf, 2, "XX")`` yields
      ``"heXX"`` (note that the first argument must be a :green:`Buffer`; a
      :green:`String` will throw).  Default is the end of
      ``oldbuf`` (nothing truncated -- a plain append).  May be a negative
      number, signifying how many bytes from the end of ``oldbuf`` to truncate
      before appending.

    * ``fmt, ...`` - A format :green:`String` and optional format
      parameters.

Return Value:
   :green:`Buffer`.  The formatted string appended to ``oldbuf`` as a dynamic :green:`Buffer`.

Note:
    The :green:`Buffer` ``oldbuf`` will be altered if it is a dynamic
    buffer.  Otherwise, it will be copied and ``oldbuf`` remains unaltered.

hexify
''''''

Convert data to a hex string.

Usage:

.. code-block:: javascript

   var hexstring = rampart.utils.hexify(data [, upper]);

Where ``data`` is the string of bytes (:green:`String` or :green:`Buffer`)
to be converted and ``upper`` is an optional :green:`Boolean`, which if
``true`` prints using upper-case ``A-F``.

Return Value:
   :green:`String`. Each byte in data is converted to its two character hex representation.

Example:  See `dehexify`_ below.

dehexify
''''''''

Convert a hex string to a string of bytes.

Usage:

.. code-block:: javascript

   var data = rampart.utils.dehexify(hexstring);

Return Value:
   :green:`Buffer`.  Each two character hex representation converted to a
   byte in the binary string.


Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var s=sprintf("%c%c%c%c",0xF0, 0x9F, 0x98, 0x8A);

   printf("0x%s\n", hexify(s) );
   printf("%s\n", dehexify(hexify(s)) );

   /* expected output:
   0xf09f988a
   😊
   */

stringToNumber
''''''''''''''

Convert various plain English :green:`Strings` to a :green:`Number` or 
:green:`Number` range.

Usage:

.. code-block:: javascript

   var buf = rampart.utils.stringToNumber(nstr [, retObj ]);

Where ``nstr`` is a :green:`String` and optional ``retObj`` is a :green:`Boolean`.

Examples:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var res = stringToNumber("five");
   // res = 5

   res = stringToNumber("three and a half");
   // res = 3.5

   res = stringToNumber("four score and seven");
   // res = 87

   res = stringToNumber("five dozen");
   // res = 60

   res = stringToNumber("a gazillion");
   // res = NaN

   res = stringToNumber("five dozen", true);
   /* res = {
         "value": 60,
         "op": "=",
         "rem": ""
      }  */

   res = stringToNumber("five dozen cookies",true);
   /* res = {
         "value": 60,
         "op": "=",
         "rem": "cookies"
      }  */

   res = stringToNumber("less than twenty",true);
   /* res = {
         "value": 20,
         "op": "<",
         "rem": ""
      }  */

   res = stringToNumber("less than twenty greater than one half is our range",true);
   /* res = {
         "value": 20,
         "min": 0.5,
         "max": 20,
         "rem": "is our range"
      }
      note: range results carry min/max in place of op  */


stringToBuffer
''''''''''''''

Performs a byte-for-byte copy of a :green:`String` into a :green:`Buffer`.
Also convert one :green:`Buffer` to a :green:`Buffer` of another type.
See ``duk_to_buffer()`` in the
`Duktape documentation <https://wiki.duktape.org/howtobuffers2x#string-to-buffer-conversion>`_

Usage:

.. code-block:: javascript

   var buf = rampart.utils.stringToBuffer(data [, buftype ]);

Where ``data`` is a :green:`String` or :green:`Buffer` and ``buftype`` is one of the following
:green:`Strings`:

   * ``"fixed"`` - returned :green:`Buffer` is a "fixed" :green:`Buffer`.
   * ``"dynamic"`` - returned :green:`Buffer` is a "dynamic" :green:`Buffer`.

If no ``buftype`` is given and ``data`` is a :green:`Buffer`, the same type of :green:`Buffer`
is returned.  If no ``buftype`` is given and ``data`` is a :green:`String`, a "fixed"
:green:`Buffer` is returned.

See `Duktape documentation <https://wiki.duktape.org/howtobuffers2x>`__ for
more information on different types of :green:`Buffers`.

Return Value:
   :green:`Buffer`.  Contents of :green:`String`/:green:`Buffer` copied to a new :green:`Buffer` :green:`Object`.

bufferToString
''''''''''''''

Copies the contents of a :green:`Buffer` into a :green:`String`, converting
the bytes to UTF-8 if they are not already valid UTF-8.

Usage:

.. code-block:: javascript

   var str = rampart.utils.bufferToString(data [, charset]);

Where ``data`` is a :green:`Buffer` :green:`Object` and the optional
``charset`` is a :green:`String` naming the encoding of the bytes (or an
:green:`Object` of the form ``{charset: "windows-1252"}``).

If ``charset`` is given, the bytes are decoded as that encoding.  If it is
not, the bytes are examined: a byte order mark or valid UTF-8 is used as
is, and anything else is decoded as ``windows-1252``.  Bytes which cannot
be decoded are replaced with the Unicode replacement character (``U+FFFD``).

.. note::

   A :green:`String` in Rampart holds UTF-8.  Copying bytes into one
   without checking them produces a :green:`String` which looks normal but
   throws ``internal error`` from the first :green:`String` operation that
   touches it, such as ``replace()``, naming neither the cause nor the
   offending text.  A single stray byte in a large document is enough.
   Converting on the way in avoids this.  Text which is already valid
   UTF-8, including plain ASCII, is copied unchanged.

   If the exact bytes are wanted rather than text, do not use this
   function: keep the :green:`Buffer`.

Return Value:
   :green:`String`.  Contents of :green:`Buffer`, as UTF-8.

toUtf8
''''''

Converts a :green:`Buffer` (or :green:`String`) of text in any supported
encoding to UTF-8, with control over how errors are handled and what is
reported.

`bufferToString`_\ () above is the short form of this and is usually what
is wanted.  Use ``toUtf8()`` when the encoding needs to be reported, when
undecodable input should raise an error rather than be replaced, or when
the text arrives in pieces.

Usage:

.. code-block:: javascript

   var str = rampart.utils.toUtf8(data [, charset]);
   var res = rampart.utils.toUtf8(data [, options]);

Where ``data`` is a :green:`Buffer` or :green:`String`, ``charset`` is a
:green:`String` naming the encoding of the bytes, and ``options`` is an
:green:`Object` with any of:

   * ``charset`` - :green:`String`.  The encoding of ``data``.  Any label
     from the `WHATWG Encoding Standard <https://encoding.spec.whatwg.org/>`_
     is accepted -- ``"windows-1252"``, ``"iso-8859-15"``, ``"koi8-r"``,
     ``"shift_jis"``, ``"gb18030"``, ``"big5"`` and so on.  If omitted, the
     encoding is determined as described under `bufferToString`_\ ().

   * ``fatal`` - :green:`Boolean`.  If ``true``, throw an error when a byte
     cannot be decoded instead of replacing it with ``U+FFFD``.
     Default ``false``.

   * ``stream`` - :green:`Boolean`.  If ``true``, an incomplete multi-byte
     sequence at the end of ``data`` is left unconverted and its length
     reported as ``tail``, so the remaining bytes may be carried into the
     next call.  Without this, a trailing partial sequence is treated as an
     error.  Default ``false``.

   * ``details`` - :green:`Boolean`.  If ``true``, return an :green:`Object`
     describing the conversion rather than the converted text alone.

Example:

.. code-block:: javascript

   var raw = rampart.utils.readFile("/path/to/notes.txt");

   /* text, converted if it needed it */
   var text = rampart.utils.toUtf8(raw);

   /* the same, with a report of what was done */
   var res = rampart.utils.toUtf8(raw, {details: true});
   /*  {
          text:    "...",
          charset: "WINDOWS-1252",
          source:  "assumed",
          repairs: 18,
          tail:    0
       }  */

   /* an HTTP response, where the server said what the encoding is */
   var text = rampart.utils.toUtf8(body, {charset: "iso-8859-9"});

Return Value:
   :green:`String` with the converted text, or, with ``details``, an
   :green:`Object` with the properties:

   * ``text`` - :green:`String`.  The converted text.
   * ``charset`` - :green:`String`.  The encoding it was decoded as.
   * ``source`` - :green:`String`.  How that encoding was arrived at.  One
     of ``"bom"``, ``"declared"`` (given by the caller), ``"utf-8"``
     (already valid) or ``"assumed"``.
   * ``repairs`` - :green:`Number`.  Count of undecodable bytes replaced.
   * ``tail`` - :green:`Number`.  Bytes of an incomplete final sequence left
     unconverted, when ``stream`` is set.

detectCharset
'''''''''''''

Reports the encoding of a :green:`Buffer` without converting it.

Usage:

.. code-block:: javascript

   var info = rampart.utils.detectCharset(data [, charset]);

Where ``data`` is a :green:`Buffer` or :green:`String` and the optional
``charset`` is a :green:`String` naming an encoding the caller already
believes to be correct, which is reported back if it is a recognised label.

Example:

.. code-block:: javascript

   var info = rampart.utils.detectCharset(rampart.utils.readFile("doc.txt"));
   /*  {charset: "UTF-8", source: "utf-8", ascii: true}  */

Return Value:
   :green:`Object` with the properties:

   * ``charset`` - :green:`String`.  The encoding the bytes appear to be in.
   * ``source`` - :green:`String`.  How that was arrived at, as for
     `toUtf8`_\ ().
   * ``ascii`` - :green:`Boolean`.  ``true`` if the bytes are 7-bit ASCII,
     which is valid in every encoding listed here and needs no conversion.

.. note::

   Encodings are not equally distinguishable.  A byte order mark is
   decisive, and multi-byte encodings such as ``shift_jis`` or ``gb18030``
   have distinctive byte patterns.  The single-byte encodings of Western
   and Central Europe -- ``windows-1252``, ``iso-8859-1``, ``iso-8859-15``
   and their relatives -- occupy the same byte ranges and differ only in
   which character each byte maps to, so they cannot be told apart by
   inspection.  Text which is not valid UTF-8 and carries no mark is
   reported as ``windows-1252``, which is a superset of ``iso-8859-1``
   across the printable range.

   Where the encoding is known from elsewhere -- an HTTP ``Content-Type``
   header, a ``<meta charset>`` declaration, a MIME part -- pass it in
   rather than relying on inspection.

objectToQuery
'''''''''''''

Convert an :green:`Object` of key/value pairs to a :green:`String` suitable for use as a query
string in an HTTP request.

Usage:

.. code-block:: javascript

   var qs = rampart.utils.objectToQuery(kvObj [, arrayOpt]);

Where ``kvObj`` is an :green:`Object` containing the key/value pairs and ``arrayOpt``
controls how :green:`Array` values are treated. A :green:`String`,
one of the following:

   * ``"repeat"`` - default value if not specified.  Repeat the key in the
     query string with each value from the array.  Example:
     ``{key1: ["val1", "val2"]}`` becomes ``key1=val1&key1=val2``.

   * ``"bracket"`` - similar to repeat, except url encoded ``[]`` is appended
     to the keys.  Example: ``{key1: ["val1", "val2"]}`` becomes
     ``key1%5B%5D=val1&key1%5B%5D=val2``.

   * ``"comma"`` - One key with corresponding values separated by a ``,``
     (comma).  Example: ``{key1: ["val1", "val2"]}`` becomes
     ``key1=val1,val2``.

   * ``"json"`` - encode array as JSON.  Example:
     ``{key1: ["val1", "val2"]}`` becomes
     ``key1=%5b%22val1%22%2c%22val2%22%5d``.

Note that the values ``null`` and ``undefined`` will be translated as the
:green:`Strings` ``"null"`` and ``"undefined"`` respectively.  Also values which
themselves are :green:`Objects` will be converted to JSON.

queryToObject
'''''''''''''

Convert a query string to an :green:`Object`.  Reverses the process, with caveats, of
`objectToQuery`_\ ().

Usage:

.. code-block:: javascript

   var kvObj = rampart.utils.queryToObject(qs);

Caveats:

*  All primitive values will be converted to :green:`Strings` unless
   ``json`` was used.

*  If ``repeat`` or ``bracket`` was used to create the
   query string, all values will be returned as strings (even if an :green:`Array` of
   :green:`Numbers` was given to `objectToQuery`_\ ().

*  If ``comma`` was used to create the query string, no separation of comma
   separated values will occur and the entire value will be returned as a :green:`String`.

*  If ``json`` was used, numeric values will be preserved as :green:`Numbers`.

*  If the query string contains object like notation (e.g.
   ``"myvar[mykey]=myval&myvar[mykey2]=myval2"``), it will be converted into
   an :green:`Object` (``{myvar: {mykey:"myval", mykey2:"myval2"} }``).

Example:

.. code-block:: javascript

   var obj= {
     key1: null,
     key2: [1,2,3],
     key3: ["val1","val2"]
   }

   var type = [ "repeat", "bracket", "comma", "json" ];

   for (var i=0; i<4; i++) {
       var qs = rampart.utils.objectToQuery(obj, type[i] );
       var qsobj = rampart.utils.queryToObject(qs);
       rampart.utils.printf("queryToObject(\n     '%s'\n    ) = \n%3J\n", qs, qsobj);
   }

   /* expected output:
   queryToObject(
        'key1=null&key2=1&key2=2&key2=3&key3=val1&key3=val2'
       ) =
   {
      "key1": "null",
      "key2": [
         "1",
         "2",
         "3"
      ],
      "key3": [
         "val1",
         "val2"
      ]
   }
   queryToObject(

   'key1=null&key2%5B%5D=1&key2%5B%5D=2&key2%5B%5D=3&key3%5B%5D=val1&key3%5B%5D=val2'
       ) =
   {
      "key1": "null",
      "key2": [
         "1",
         "2",
         "3"
      ],
      "key3": [
         "val1",
         "val2"
      ]
   }
   queryToObject(
        'key1=null&key2=1,2,3&key3=val1,val2'
       ) =
   {
      "key1": "null",
      "key2": "1,2,3",
      "key3": "val1,val2"
   }
   queryToObject(
        'key1=null&key2=%5b1%2c2%2c3%5d&key3=%5b%22val1%22%2c%22val2%22%5d'
       ) =
   {
      "key1": "null",
      "key2": [
         1,
         2,
         3
      ],
      "key3": [
         "val1",
         "val2"
      ]
   }
   */


urlComponents
'''''''''''''

Parse a URL into its component parts, following the
`WHATWG URL specification <https://url.spec.whatwg.org/>`_\ .

Usage:

.. code-block:: javascript

   var parts = rampart.utils.urlComponents(url [, base]);

Where:

* ``url`` is a :green:`String`, the URL to parse.  It may be absolute, or
  relative if ``base`` is supplied.

* ``base`` is an optional :green:`String`, an absolute URL against which a
  relative ``url`` is resolved.

Return Value:
   An :green:`Object` of :green:`String` properties (plus two rampart
   extensions), or :green:`undefined` if the URL cannot be parsed:

   * ``protocol`` - the scheme including the trailing colon (``"https:"``).
   * ``username`` / ``password`` - credentials, or ``""``.
   * ``host`` - hostname with ``:port`` if present (``"example.com:8443"``).
   * ``hostname`` - host without the port.
   * ``port`` - the explicit port as a :green:`String`, or ``""`` if none.
   * ``numericPort`` - *(rampart extension)* the port as a :green:`Number`:
     the explicit port if set, otherwise the default port for the scheme,
     otherwise :green:`null`.
   * ``pathname`` - the path (``"/a/b"``).
   * ``search`` - the query string including the leading ``?``, or ``""``.
   * ``hash`` - the fragment including the leading ``#``, or ``""``.
   * ``href`` - the full, normalized URL.
   * ``origin`` - scheme + host, or the :green:`String` ``"null"`` for
     schemes with an opaque origin.
   * ``hostUnicode`` - *(rampart extension)* the Unicode (display) form of an
     internationalized hostname; equals ``hostname`` for ASCII hosts.

Example:

.. code-block:: javascript

   rampart.utils.urlComponents("https://user:pw@Example.COM:8443/a/b?x=1#frag");
   /* returns:
   {
      protocol:    "https:",
      username:    "user",
      password:    "pw",
      host:        "example.com:8443",
      hostname:    "example.com",
      port:        "8443",
      numericPort: 8443,
      pathname:    "/a/b",
      search:      "?x=1",
      hash:        "#frag",
      href:        "https://user:pw@example.com:8443/a/b?x=1#frag",
      origin:      "https://example.com:8443",
      hostUnicode: "example.com"
   } */

   /* a relative URL resolved against a base */
   rampart.utils.urlComponents("../c?z=9", "https://example.com/a/b/").href;
   /* "https://example.com/a/c?z=9" */


absUrl
''''''

Resolve one or more relative URLs against an absolute base URL.

Usage:

.. code-block:: javascript

   var abs = rampart.utils.absUrl(base, rel [, asComponents]);

Where:

* ``base`` is a :green:`String`, the absolute URL to resolve against.

* ``rel`` is a :green:`String`, or an :green:`Array` of :green:`Strings`,
  the relative URL(s) to resolve.

* ``asComponents`` is an optional :green:`Boolean`.  If truthy, each result
  is returned as a components :green:`Object` (as from `urlComponents`_\ ())
  instead of an ``href`` :green:`String`.

Return Value:
   * :green:`undefined` if ``base`` cannot be parsed.
   * If ``rel`` is a :green:`String`: the resolved ``href`` (or components
     :green:`Object`), or :green:`undefined` if ``rel`` cannot be parsed.
   * If ``rel`` is an :green:`Array`: an :green:`Array` of results, with any
     unparseable entry returned as :green:`undefined`.

Example:

.. code-block:: javascript

   rampart.utils.absUrl("https://example.com/docs/guide/", "../api/x.html");
   /* "https://example.com/docs/api/x.html" */

   rampart.utils.absUrl("https://example.com/a/b/", ["c.html", "../d.html"]);
   /* [ "https://example.com/a/b/c.html", "https://example.com/a/d.html" ] */


toASCII
'''''''

Convert an internationalized domain name (IDN) to its ASCII (Punycode) form,
per IDNA.  Already-ASCII domains are returned unchanged.  Throws if the input
is not a valid domain.

Usage:

.. code-block:: javascript

   var ascii = rampart.utils.toASCII(domain);

Example:

.. code-block:: javascript

   rampart.utils.toASCII("bücher.de");   /* "xn--bcher-kva.de" */
   rampart.utils.toASCII("plain.com");   /* "plain.com" */


toUnicode
'''''''''

Convert a domain name to its Unicode (display) form, reversing IDNA Punycode
encoding.  Throws if the input is not a valid domain.

Usage:

.. code-block:: javascript

   var unicode = rampart.utils.toUnicode(domain);

Example:

.. code-block:: javascript

   rampart.utils.toUnicode("xn--bcher-kva.de");   /* "bücher.de" */


getchar
'''''''

Get one or more characters from ``stdin``.

Usage:

.. code-block:: javascript

   var instr = rampart.utils.getchar([nchar]);

Where ``nchar`` is an optional :green:`number`, the number of characters
to read from ``stdin``.  The default is ``1``.

Return Value:
   A :green:`String` of length ``nchars``.

Note:
   If ``stdin`` is from an interactive terminal, execution
   will be paused until ``nchar`` chars are input.  Unlike
   ``fread(stdin);`` :ref:`below <rampart-utils:fread>`, the terminal will be
   set to return characters without waiting for a newline.

readFile
''''''''

Read the contents of a file.

Usage:

.. code-block:: javascript

   var contents = rampart.utils.readFile({
      file: filename
      [, offset: offsetPos]
      [, length: rLength]
      [, returnString: return_str]
   });

   /* or */

   var contents = rampart.utils.readFile(filename [, offsetPos [, rLength]] [, return_str]);


Where values ``filename`` and optional values
``offsetPos``, ``rLength`` and/or ``return_str`` are:


+------------+-----------------+--------------------------------------------------------------+
|Argument    |Type             |Description                                                   |
+============+=================+==============================================================+
|filename    |:green:`String`  | Path to the file to be read                                  |
+------------+-----------------+--------------------------------------------------------------+
|offsetPos   |:green:`Number`  | If positive, start position to read from beginning of file.  |
|            |                 +--------------------------------------------------------------+
|            |                 | If negative, start position to read from end of file.        |
+------------+-----------------+--------------------------------------------------------------+
|rLength     |:green:`Number`  | If greater than zero, amount in bytes to be read.            |
|            |                 +--------------------------------------------------------------+
|            |                 | If 0 or negative, position from end of file to stop reading. |
+------------+-----------------+--------------------------------------------------------------+
|return_str  |:green:`Boolean` | If not set, or ``false``, return a :green:`Buffer`.          |
|            |                 +--------------------------------------------------------------+
|            |                 | If ``true``, return contents as a :green:`String`.           |
|            |                 | May be truncated if the file contains null characters.       |
+------------+-----------------+--------------------------------------------------------------+

Return Value:
   :green:`Buffer` or :green:`String`.  The contents of the file.

Example:

.. code-block:: javascript

   rampart.utils.fprintf("/tmp/file.txt","This is a text file\n");

   var txt = rampart.utils.readFile({
      file:         "/tmp/file.txt",
      offset:       10,
      length:       -6,
      returnString: true
   });

   /* or var txt = rampart.utils.readFile("/tmp/file.txt", 10, -6, true); */

   rampart.utils.printf("'%s'\n", txt);

   /* expected output:
   'text'
   */

Note:
    If ``return_str`` is ``true`` and ``offsetPos`` and/or ``rLength`` are
    set, the returned :green:`String` may be shortened to ensure that the
    return value is a valid UTF-8 string.  If that behavior is not desired,
    returning a :green:`Buffer` and converting to a string with
    `sprintf`_\ () will bypass the UTF-8 character/byte boundary check.
    Note that `bufferToString`_\ () will NOT bypass it: a sequence cut
    short at the end of the :green:`Buffer` is replaced with ``U+FFFD``,
    since a :green:`String` ending mid character cannot be used safely.


writeFile
'''''''''

Write the contents of a :green:`String` or :green:`Buffer` to a file in one
call.  Convenience wrapper around `fopen`_\ () + `fwrite`_\ () + `fclose`_\ ().
Truncates an existing file by default.

Usage:

.. code-block:: javascript

   rampart.utils.writeFile(path, data [, options]);

Where:

* ``path`` is a :green:`String`, the path to the file to be written.

* ``data`` is a :green:`String` or :green:`Buffer` (any buffer type, including
  :green:`Uint8Array`).

* ``options`` is an optional :green:`Object` with the following properties:

  * ``mode`` -- :green:`Number` or :green:`String` (octal).  File permissions
    applied via ``fchmod`` after the bytes are written.  If unset, the
    system ``umask`` determines mode.  See `chmod`_\ () for octal notation.

  * ``flag`` -- :green:`String`, the ``fopen`` mode to use.  Defaults to
    ``"w"`` (truncate).  Pass ``"a"`` to append, or use `appendFile`_\ ()
    below for the common append case.

Return Value:
   ``undefined``.  Throws on I/O error.

Example:

.. code-block:: javascript

   rampart.utils.writeFile("/tmp/note.txt", "hello\n");
   rampart.utils.writeFile("/tmp/key.bin", new Uint8Array([1,2,3]), {mode: 0o600});

appendFile
''''''''''

Append the contents of a :green:`String` or :green:`Buffer` to a file.  Creates
the file if it does not exist.  Equivalent to ``writeFile(path, data,
{flag: "a"})``.

Usage:

.. code-block:: javascript

   rampart.utils.appendFile(path, data [, options]);

Arguments are the same as `writeFile`_\ () above; the ``flag`` option, if
provided, overrides the default of ``"a"``.

Return Value:
   ``undefined``.  Throws on I/O error.


trim
''''

Remove whitespace characters from the beginning and end of a :green:`String`.

Usage:

.. code-block:: javascript

   var trimmed = rampart.utils.trim(str);

Where ``str`` is a :green:`String`.

Return Value:
   :green:`String`. ``str`` with whitespace removed from beginning and end.

Example:

.. code-block:: javascript

   var str = "\n a line of text \n";
   rampart.utils.printf("'%s'", rampart.utils.trim(str));
   /* expected output:
   'a line of text'
   */

minify
''''''

Minify JavaScript source code by removing unnecessary whitespace, stripping
comments, and mangling (shortening) local variable names.  Global variables
and object property names are preserved.

Usage:

.. code-block:: javascript

   var minified = rampart.utils.minify(src);

Where ``src`` is a :green:`String` containing JavaScript source code.

Return Value:
   :green:`String`. The minified JavaScript code.

Example:

.. code-block:: javascript

   var src = "function add(first, second) {\n    return first + second;\n}";
   var min = rampart.utils.minify(src);
   /* expected output:
   "function add(a,b){return a+b;}"
   */

stat
''''

Return information on a file.

Usage:

.. code-block:: javascript

   var st = rampart.utils.stat(file);

Where ``file`` is a :green:`String` (name of file).

Return Value:
   :green:`Boolean`/:green:`Object`. ``false`` if file does not exist.  Otherwise an :green:`Object` with the following
   properties:

.. code-block:: javascript

   {
      "dev":               Number,
      "ino":               Number,
      "mode":              Number,
      "nlink":             Number,
      "uid":               Number,
      "gid":               Number,
      "rdev":              Number,
      "size":              Number,
      "blksize":           Number,
      "blocks":            Number,
      "atime":             Date,
      "mtime":             Date,
      "ctime":             Date,
      "readable":          Boolean,
      "writable":          Boolean,
      "executable":        Boolean,
      "owner":             String,
      "group":             String,
      "isBlockDevice":     Boolean,
      "isCharacterDevice": Boolean,
      "isDirectory":       Boolean,
      "isFIFO":            Boolean,
      "isFile":            Boolean,
      "isSocket":          Boolean,
      "permissions":       String  /* i.e. "-rw-r--r--" */
   }

See `stat (2) <https://man7.org/linux/man-pages/man2/stat.2.html>`_ for the
meaning of each property.  The ``is*`` :green:`Booleans` are set to ``true`` if the
corresponding file property is true.

Example:

.. code-block:: javascript

   var st = rampart.utils.stat("/tmp/file.txt");

   if(st) {
      /* print file mode as octal number */
      rampart.utils.printf("%o\n", st.mode & 0777)
   } else {
      console.log("file /tmp.file.txt does not exist");
   }
   /* expected output: 644 */

lstat
'''''

Same as `stat`_\ () except if ``file`` is a link, return information about the link itself.

Return Value:
   Same as `stat`_\ () with the addition of the property
   ``isSymbolicLink`` which is set ``true`` if the file is a symbolic link.
   ``readable`` and ``writable`` refer to the link, not the target.

exists
''''''

Test whether a path resolves to anything accessible.  Convenience wrapper
that returns ``false`` for missing paths and inaccessible parents (the
errors `stat`_\ () would swallow into a ``false`` return) without forcing
callers to examine the stat result.

Usage:

.. code-block:: javascript

   var ok = rampart.utils.exists(path);

Where ``path`` is a :green:`String`.

Return Value:
   :green:`Boolean`.  ``true`` if the path exists and is reachable,
   ``false`` otherwise.

statVfs
'''''''

Return filesystem statistics for the volume containing ``path`` -- block
sizes, free space, inode counts and similar values reported by
`statvfs (3) <https://man7.org/linux/man-pages/man3/statvfs.3.html>`_.

Usage:

.. code-block:: javascript

   var info = rampart.utils.statVfs(path);

Where ``path`` is a :green:`String`, any path on the volume of interest.

Return Value:
   :green:`Object` with the following properties:

.. code-block:: javascript

   {
      "bsize":      Number,  /* filesystem block size                 */
      "frsize":     Number,  /* fragment size                          */
      "blocks":     Number,  /* total fragments                        */
      "bfree":      Number,  /* free fragments                         */
      "bavail":     Number,  /* free fragments available to non-root   */
      "files":      Number,  /* total inodes                           */
      "ffree":      Number,  /* free inodes                            */
      "favail":     Number,  /* free inodes available to non-root      */
      "fsid":       Number,  /* filesystem ID                          */
      "flag":       Number,  /* mount flags                            */
      "namemax":    Number,  /* maximum filename length                */
      "totalBytes": Number,  /* convenience: frsize * blocks           */
      "freeBytes":  Number,  /* convenience: frsize * bfree            */
      "availBytes": Number   /* convenience: frsize * bavail           */
   }

Throws if the path cannot be resolved.

exec
''''

Run an executable file.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.exec(command [, options] [,arg1, arg2, ..., argn] );

Where:

*  ``command`` - :green:`String`. An absolute path to an executable or the name of
   an executable that may be found in the current ``PATH`` environment variable.

*  ``options`` - :green:`Object`. Containing the following properties:

   *  ``timeout`` - :green:`Number`. Maximum amount of time in milliseconds before
      the process is automatically killed.  Valid if ``background`` is unset
      or ``false``.

   *  ``killSignal`` - :green:`Number`. If timeout is reached, use this
      signal.  Valid if ``background`` is unset
      or ``false`` and a ``timeout`` value is set.

   *  ``background`` - :green:`Boolean`.  Whether to execute detached and return
      immediately.  If ``true``, ``stdout`` and ``stderr`` below will be set to ``null``.  Any ``timeout``
      value is ignored.

   *  ``env`` - :green:`Object`. Key/value pairs to be used as environment variables for the executed process.
      Default, if not provided is :ref:`process.env <rampart-main:env>`.  An empty :green:`Object`
      (``{}``) removes all environment variables.

   *  ``appendEnv`` - :green:`Boolean`.  If ``false`` (the default),
      only the environment variables given in ``env`` will be available.  If
      ``true``, variables provided in ``env`` will be appended to :ref:`process.env <rampart-main:env>`.
      Duplicate keys in :ref:`process.env <rampart-main:env>` are replaced with the value from ``env``.

   *  ``stdin`` - :green:`String` or :green:`Buffer`.  If specified, the content
      of the :green:`String` or :green:`Buffer` is piped to the command as stdin.

   *  ``returnBuffer`` - :green:`Boolean`.  Whether content is returned in a
      :green:`Buffer` rather than a :green:`String`.  Useful for capturing
      binary data output.

   *  ``args`` - :green:`Array`.  An array of arguments to be passed to the
      executable.  If arguments are also given as positional parameters to
      ``exec()``, the positional arguments come first, followed by the
      arguments from this :green:`Array`.

   *  ``changeDirectory`` - :green:`String`.  Change the working directory
      to value before executing.

   *  ``cd`` - Alias for ``changeDirectory`` .

*  ``argn`` - :green:`String`/:green:`Number`/:green:`Object`/:green:`Boolean`/:green:`Null` - Arguments to be passed to
   ``command``.  Non-Strings are converted to a :green:`String` (e.g. "true", "null",
   "42" or for :green:`Object`, the equivalent of ``JSON.stringify(obj)``).

Return Value:
   :green:`Object`.  Properties as follows:

   * ``stdout`` - :green:`String`. Output of command if ``background`` is not set ``true``.
     Otherwise ``null``.

   * ``stderr`` - :green:`String`. stderr output of command if ``background`` is not set ``true``.
     Otherwise ``null``.

   * ``exitStatus`` - :green:`Number`.  The returned exit status of the command.

   * ``timedOut`` - :green:`Boolean`.  Set true if the program was killed after
     ``timeout`` milliseconds has elapsed.

   * ``pid`` - :green:`Number`. Process id of the executed command.

getenv
''''''

Return the value of an environment variable as currently set in the
process's environment.

Usage:

.. code-block:: javascript

   var value = rampart.utils.getenv(name);

Where ``name`` is a :green:`String` containing the environment variable name.

Return Value:
   :green:`String` containing the current value of the variable, or
   ``undefined`` if the variable is not set.

How this differs from :ref:`process.env <rampart-main:env>`:

   :ref:`process.env <rampart-main:env>` is a :green:`Object` populated
   with a *snapshot* of the environment at the time rampart started.
   Reads from ``process.env`` return values from that snapshot; writes
   modify only the snapshot and are not visible to native code that
   calls ``getenv(3)`` (such as a library loaded via ``require()``),
   nor are they inherited by child processes spawned with
   `exec`_\ ().

   ``rampart.utils.getenv`` consults the live OS-level process
   environment.  It sees values set later via
   `setenv`_\ () (whether by this function family or by C code in a
   loaded module), and it does not see writes made only to the
   ``process.env`` :green:`Object`.

Example:

.. code-block:: javascript

   process.env.HELLO = "from-js-dict";
   rampart.utils.setenv("HELLO", "from-os-env");

   rampart.utils.printf("process.env.HELLO       = %s\n",
                        process.env.HELLO);                /* from-js-dict  */
   rampart.utils.printf("rampart.utils.getenv()  = %s\n",
                        rampart.utils.getenv("HELLO"));    /* from-os-env   */
   rampart.utils.printf("child shell sees        = %s\n",
                        rampart.utils.exec("/bin/sh","-c","echo $HELLO").stdout);
                                                           /* from-os-env\n */

setenv
''''''

Set an environment variable in the process's live OS-level environment.
Wraps POSIX `setenv(3) <https://man7.org/linux/man-pages/man3/setenv.3.html>`_.

Usage:

.. code-block:: javascript

   rampart.utils.setenv(name, value [, overwrite]);

Where:

*  ``name`` - :green:`String`. The variable name.  Must be non-empty and
   may not contain ``=``.

*  ``value`` - :green:`String`. The value to assign.

*  ``overwrite`` - :green:`Boolean`. Optional. If ``true`` (the default)
   and the variable is already set, the existing value is replaced.
   If ``false``, an existing value is left untouched.

Return Value:
   :green:`Undefined`.  Throws on error (e.g. invalid name, allocation
   failure).

Notes:
   The value is visible to subsequent ``rampart.utils.getenv()`` calls,
   to native code that calls ``getenv(3)``, and to child processes
   spawned with `exec`_\ ().  Writes to :ref:`process.env
   <rampart-main:env>` do *not* have these properties; use ``setenv``
   when something outside the JS runtime needs to read the value.

   A common use case is preparing an environment variable that a
   native module reads from its library constructor.  For example,
   ``MAGICK_CONFIGURE_PATH`` must be set before ``require("rampart-graphicsmagick")``
   triggers the load of ``libGraphicsMagick``; ``rampart-gm.js`` does
   exactly that via ``rampart.utils.setenv`` before the ``require()``.

Example:

.. code-block:: javascript

   rampart.utils.setenv("MY_VAR", "hello");
   rampart.utils.printf("%s\n", rampart.utils.getenv("MY_VAR"));
   /* expected output:
   hello
   */

   /* overwrite=false: leave a caller-supplied value in place */
   rampart.utils.setenv("MY_VAR", "ignored", false);
   rampart.utils.printf("%s\n", rampart.utils.getenv("MY_VAR"));
   /* expected output:
   hello
   */

unsetenv
''''''''

Remove an environment variable from the process's live OS-level
environment.  Wraps POSIX `unsetenv(3) <https://man7.org/linux/man-pages/man3/unsetenv.3.html>`_.

Usage:

.. code-block:: javascript

   rampart.utils.unsetenv(name);

Where ``name`` is a :green:`String`.  Must be non-empty and may not
contain ``=``.

Return Value:
   :green:`Undefined`.  Throws on error.

Notes:
   Removing a variable affects ``rampart.utils.getenv()``, native code
   that calls ``getenv(3)``, and child processes spawned by `exec`_\ ().
   It does not modify :ref:`process.env <rampart-main:env>`; if you
   want both views cleared, delete the property from ``process.env``
   as well.

Example:

.. code-block:: javascript

   rampart.utils.setenv("TEMP_VAR", "x");
   rampart.utils.unsetenv("TEMP_VAR");
   rampart.utils.printf("%s\n", rampart.utils.getenv("TEMP_VAR") || "(unset)");
   /* expected output:
   (unset)
   */

shell
'''''

Execute :green:`String` in a shell.  Equivalent to
``rampart.utils.exec(shell, "-c", shellcmd);``, where ``shell`` is the
first of these that exists: ``$SHELL``, ``/bin/bash``, ``/bin/sh``.  If
none is found, an error is thrown.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.shell(shellcmd[, options]);

Where ``shellcmd`` is a :green:`String` containing the command and arguments to be
passed to the shell and ``options`` are the same as specified for `exec`_\ .

Since ``$SHELL`` takes precedence, a command that relies on ``bash``
features may fail for a user whose shell is ``dash``, ``zsh`` or similar.
Where the shell matters, name it: ``exec("/bin/bash", "-c", shellcmd)``.

Return Value:
   Same as `exec`_\ ().

Example:

.. code-block:: javascript

   var ret = rampart.utils.shell('echo -n "hello"; echo "hi" 1>&2;');
   console.log(JSON.stringify(ret, null, 3));

   /* expected output:
   {
      "stdout": "hello",
      "stderr": "hi\n",
      "timedOut": false,
      "exitStatus": 0,
      "pid": 24658
   }
   */

fork
''''

Fork the current process.

Usage:

.. code-block:: javascript

   var pid = rampart.utils.fork([pipe, pipe2, ..., pipeX]);

   if(pid == -1)
      rampart.utils.fprintf(rampart.utils.stderr, "error forking\n");

   if(pid) {
      //parent
   } else {
      //child
   }

Where ``pipeX`` is one or several pipes created with `newPipe`_ below.

Return Value:
   A :green:`Number` - The pid of the child in the parent process, ``0`` in
   the child process and ``-1`` if there is an error and fork failed.

Note:
    ``fork`` will throw an error if there are any threads running at the
    time of the fork, either from ``rampart.thread`` or ``rampart-server``.
    Threads, however, can be created after the fork in either the child
    or parent process.

newPipe
'''''''

Create a bi-directional pipe for passing variables between processes created
with `fork`_ above.

Usage:

.. code-block:: javascript

   var pipe = rampart.utils.newPipe();

   var pid = fork(pipe);

Return Value:
   An :green:`Object` of :green:`Functions`:

   * ``write(data)`` - write to the pipe, where data is any variable which
     can be serialized using ``CBOR``.  Return value is the number of bytes
     written. Note: writes may block if the pipe is full until the reading
     process reads with one of the two read functions below.  Throws an
     error if pipe has been closed.

   * ``read([function])``  - perform a blocking read of data sent from
     another process using ``write()`` above.  If a function is provided
     (i.e. ``function(value, error){}``) the value or error will be passed
     to that callback (with the other being undefined).  Return value will
     be undefined. If no function is provided, the return value will be an
     :green:`Object` with either ``value`` or ``error`` set.

   * ``onRead(function)`` - same as ``read``, except that a
     :green:`Function` is required, the call is non-blocking and the
     callback :green:`Function` will be called in the event loop each time
     data is available.  On error, the pipe will close and the event will be
     removed.

   * ``close()`` - close the pipe.  Any further reads or writes from either
     process will produce or throw an error.

Example:

.. code-block:: javascript

   var pipe = rampart.utils.newPipe();

   // fork and set the pipe for parent and child processes
   var pid = fork(pipe);

   if(pid ==-1) {
      rampart.utils.fprintf(rampart.utils.stderr, "error piping\n");
      process.exit(1);
   }

   if(pid) {
      //parent

      pipe.write("My first message");
      pipe.write("My second message");

   } else {
      //child

      var msg = pipe.read();
      if(msg.error)
         rampart.utils.fprintf(rampart.utils.stderr, "error reading- %s\n", msg.error);
      else
         rampart.utils.printf("msg = '%s'\n", msg.value);

      //run non-blocking in event loop
      pipe.onRead(function(val,err) {
         if(err)
            rampart.utils.fprintf(rampart.utils.stderr, "read event: error reading- %s\n", err);
         else
            rampart.utils.printf("read event: msg = '%s'\n", val);
      });

   }

daemon
''''''

Same as `fork`_ above, except it double forks, detaches and creates its own session.  Thus the child process
will continue to run after the parent and the controlling terminal exit.

forkpty
'''''''

Run an executable file in a pseudo-terminal with unbuffered IO.  IO is
performed asynchronously in the event loop of the current thread.

Usage:

.. code-block:: javascript

   var pty = rampart.utils.forkpty(command [, options] [,arg1, arg2, ..., argn] );

Where:

*  ``command`` - :green:`String`. An absolute path to an executable or the name of
   an executable that may be found in the current ``PATH`` environment variable.

*  ``options`` - :green:`Object`. Containing the following properties:

   *  ``env`` - :green:`Object`. Key/value pairs to be used as environment variables for the executed process.
      Default, if not provided is :ref:`process.env <rampart-main:env>`.  An empty :green:`Object`
      (``{}``) removes all environment variables.

   *  ``appendEnv`` - :green:`Boolean`.  If ``false`` (the default),
      only the environment variables given in ``env`` will be available.  If
      ``true``, variables provided in ``env`` will be appended to :ref:`process.env <rampart-main:env>`.
      Duplicate keys in :ref:`process.env <rampart-main:env>` are replaced with the value from ``env``.

*  ``argn`` - :green:`String`/:green:`Number`/:green:`Object`/:green:`Boolean`/:green:`Null` - Arguments to be passed to
   ``command``.  Non-Strings are converted to a :green:`String` (e.g. "true", "null",
   "42" or for :green:`Object`, the equivalent of ``JSON.stringify(obj)``).

Return Value:
   :green:`Object`.  Properties as follows:

   * ``read`` - :green:`Function`.  Read data from the stdout of the executed
     process.

      .. code-block:: javascript

         pty.read([buffersize [, maxread]] [, retstring]);

      Where: ``buffersize`` defaults to ``4096``, ``maxread`` defaults to
      unlimited and ``retstring`` (default ``false`` for
      :green:`Buffer`) is a :green:`Boolean` - whether the return
      contents should be converted to :green:`String`.

   * ``write`` - :green:`Function`. Write data to the stdin of the executed
     process.

     .. code-block:: javascript

        pty.write([buffer|string]);

   * ``resize`` - :green:`Function`. Set a new size for the pseudo-terminal.

       .. code-block:: javascript

          pty.resize(width, height);

      Where: ``width`` and ``height`` are :green:`Numbers` - number of
      character rows and columns.


   * ``on`` - :green:`Function`:  Two events are currently allowed:
     ``"data"`` and ``"close"``.  If, ``"data"`` is specified, when new data
     is available to be read, the provided callback function will be called.
     If ``"close"`` is specified, the provided callback function will be
     called when the process exits.

     .. code-block:: javascript

	pty.on(['data'|'close'], callback);

     A ``data`` callback is expected to consume the data by calling
     ``pty.read()``.  If a callback returns without reading anything, the
     ``data`` event is paused and no further ``data`` callbacks are made
     until ``pty.read()`` is called again, at which point it resumes.
     Nothing is lost: the unread bytes stay in the pseudo-terminal and are
     returned by that next ``read()``.  A callback that throws also reads
     nothing, and so pauses the event in the same way.

     An error thrown inside a ``data`` or ``close`` callback is reported on
     stderr as ``Error in forkpty 'data' callback:`` followed by a stack
     trace.  It does not stop the script.

   If there is an initial error executing ``command``, ``forkpty()`` will throw an
   error.  When the command exits, the functions in the return object will be deleted.
   Therefore a check should be run before accessing any functions in case the pty has
   closed:

   .. code-block:: javascript

      var pty = rampart.utils.forkpty(command [, options] [,arg1, arg2, ..., argn] );

      if(pty.write)
         pty.write(msg);
      else
         do_cleanup();

   This applies to ``pty.read()`` as well: a ``data`` event may arrive
   just as the child exits, so guard with ``if(pty.read)`` inside the
   handler too.  Note that on that final event the guard will still pass -
   ``read()`` is removed only once it has detected that the child is gone -
   so the return value should be checked as well:

   .. code-block:: javascript

      pty.on('data', function() {
         if(pty.read) {
            var data = pty.read(4096, true);
            if(data)            /* undefined on the last event */
               process_data(data);
         }
      });

   An example for using ``forkpty()`` with websockets to run a terminal in
   a web browser can be found
   `here <https://github.com/aflin/rampart/tree/main/unsupported_extras/forkpty-term>`_\ .


kill
''''

Terminate a process or send a signal.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.kill(pid [, signal[, throwOnError]]);

Where:
   * ``pid`` is a :green:`Number`, the process id of process which will
     receive the signal.
   * ``signal`` is a :green:`Number`, or :green:`String`, the signal to send.
     If ``signal`` is not specified, ``15`` (``SIGTERM``) is used.  See manual
     page for kill(1) for a list of signals, which may vary by platform.  Setting
     ``signal`` to ``0`` sends no signal, but checks for the existence of the
     process identified by ``pid``. ``signal`` may also be a :green:`String`, a well
     known signal such as ``"SIGTERM"`` or ``"SIGUSR1"``.

   * ``throwOnError`` - :green:`Boolean` - whether to throw an error with a specified
     reason upon failure.  Default is ``false``.

Return Value:
   :green:`Boolean`.  ``true`` if the signal was successfully sent.  If ``throwOnError``
   is not ``true``, will return ``false`` if there was an error or process does not exist.

Example:

.. code-block:: javascript

   var ret = rampart.utils.exec("sleep", "100", {background:true});
   var pid=ret.pid;

   if (rampart.utils.kill(pid,0)) {
       console.log("process is still running");
       rampart.utils.kill(pid);
       rampart.utils.sleep(0.2);
       if( rampart.utils.kill(pid,0) == 0 )
          console.log("and now is dead");
   } else
       console.log("not running");
   /* expected output:
      process is still running
      and now is dead
   */


getcwd
''''''

Return the current working directory as a :green:`String`.

Usage:

.. code-block:: javascript

   rampart.utils.getcwd();

Return Value:
   A :green:`String`, the current working directory of the script.

chdir
'''''

Change the current working directory.

Usage:

.. code-block:: javascript

   rampart.utils.chdir(path);

Where ``path`` is a :green:`String`, the location of the new working
directory.  This command throws an error if it fails to change to the
specified directory.

Return Value:
   ``undefined``.

mkdir
'''''

Create a directory.

Usage:

.. code-block:: javascript

   rampart.utils.mkdir(path [, mode]);

Where ``path`` is a :green:`String`, the directory to be created and ``mode`` is a
:green:`Number` or :green:`String`, the octal permissions mode. Any parent directories which
do not exist will also be created.  Throws error if lacking permissions or
if another error was encountered.

Note that ``mode`` is normally given as an octal.  As such it can be, e.g.,
``0755`` (octal number) or ``"755"`` (:green:`String` representation of an octal
number), but ``755``, as a decimal number will give the octal ``01363``,
which is likely not what was intended.



Return Value:
   ``undefined``.

mkdTemp
'''''''

Create a uniquely-named directory using `mkdtemp (3)
<https://man7.org/linux/man-pages/man3/mkdtemp.3.html>`_.  Six random
characters are appended to the supplied prefix and a new directory is
created with mode ``0700`` (owner only).

Usage:

.. code-block:: javascript

   var dir = rampart.utils.mkdTemp(prefix);

Where ``prefix`` is a :green:`String` -- the full path leading up to the
random suffix.  For example, ``"/tmp/build-"`` produces a directory like
``/tmp/build-AbC123``.

Return Value:
   :green:`String`.  The full path of the newly created directory.  Throws
   on failure (most commonly because the parent directory does not exist
   or is not writable).

rmdir
'''''

Remove an empty directory.

Usage:

.. code-block:: javascript

   rampart.utils.rmdir(path [, recurse]);

Where ``path`` is a :green:`String`, the directory to be removed and ``recurse`` is an
optional :green:`Boolean`, which if ``true``, parent directories explicitly present in
``path`` will also be removed.  Throws an error if the directory cannot be
removed (e.g., not empty or lacking permission).

Note that ``recurse`` removes parent directories, not children -- it does not
delete the contents of ``path``.  Take care when combining it with an absolute
path: every empty parent directory up the tree is removed in turn, stopping
only where removal fails (which then throws).  Prefer a relative path, or omit
``recurse`` and remove the levels you intend explicitly.

Return Value:
   ``undefined``.

Example:

.. code-block:: javascript

   /* make the following directories in the
      current working directory             */
   rampart.utils.mkdir("p1/p2/p3",0755);

   /* remove the directories recursively */
   rampart.utils.rmdir("p1/p2/p3", true);



readDir
'''''''

Get listing of directory files.

Usage:

.. code-block:: javascript

   var files = rampart.utils.readDir(path [, showhidden]);

   /* readdir (all lowercase) is an equivalent alias */
   var files = rampart.utils.readdir(path [, showhidden]);

Where ``path`` is a :green:`String`, the directory whose content will be listed and
``showhidden`` is a :green:`Boolean`, which if ``true``, files or directories
beginning with ``.`` (hidden files) will be included in the return value.
Note that this includes the ``.`` and ``..`` directory entries themselves.

Return Value:
   :green:`Array`.  An :green:`Array` of :green:`Strings`, each filename in the directory.


walkDir
'''''''

Recursively walk a directory tree, calling a JavaScript :green:`Function`
for each entry encountered.  Used internally by `cp`_\ () and `rm`_\ ();
also useful directly for custom traversals.

Usage:

.. code-block:: javascript

   rampart.utils.walkDir(dir, callback [, options]);

Where:

* ``dir`` is a :green:`String`, the path to descend.

* ``callback`` is a :green:`Function` ``callback(path, type, depth)`` invoked
  per entry:

  * ``path`` -- :green:`String`, the entry's full path (relative to ``dir``
    if ``dir`` was relative).

  * ``type`` -- :green:`String`, one of ``"file"``, ``"dir"``, ``"symlink"``,
    or ``"other"``.

  * ``depth`` -- :green:`Number`, depth from ``dir``.  The root passed in has
    depth ``0``.

  Returning ``false`` from the callback stops the walk (rampart's standard
  callback-abort convention).

* ``options`` is an optional :green:`Object`:

  * ``followLinks`` -- :green:`Boolean`, default ``false``.  If ``true``,
    symlinks are followed during the descent.

  * ``postOrder`` -- :green:`Boolean`, default ``false``.  If ``true``,
    directory entries are emitted *after* their contents (depth-first
    leaves-then-branches).  Useful for safe recursive deletion.

Return Value:
   ``undefined``.

Example:

.. code-block:: javascript

   /* Build an array of all .js files under the cwd */
   var jsFiles = [];
   rampart.utils.walkDir(".", function(p, t) {
      if (t === "file" && /\.js$/.test(p)) jsFiles.push(p);
   });

glob
''''

Expand a wildcard pattern to a list of matching paths.  Supports ``*`` (any
chars within a component), ``?`` (single char), ``[abc]`` / ``[a-z]`` /
``[!abc]`` character classes, and ``**`` as a component meaning "any depth
of subdirectories".  No brace expansion.

Usage:

.. code-block:: javascript

   var matches = rampart.utils.glob(pattern [, options]);

Where:

* ``pattern`` is a :green:`String`, either relative or absolute.  Absolute
  patterns start the search at ``/``; relative patterns start at ``options.cwd``
  or the current working directory.

* ``options`` is an optional :green:`Object`:

  * ``cwd`` -- :green:`String`, the base directory for relative patterns.
    Default ``"."``.

  * ``dot`` -- :green:`Boolean`, default ``false``.  If ``true``, dotfiles
    are included in matches.

Return Value:
   :green:`Array` of :green:`Strings`, the matching paths (may be empty).

Example:

.. code-block:: javascript

   /* All .js files anywhere below ./src.
      Returned paths are prefixed with the cwd as given, so this yields
      e.g. "./src/main.js", not "main.js".  An absolute cwd likewise
      yields absolute paths. */
   var src = rampart.utils.glob("**/*.js", {cwd: "./src"});

   /* Absolute pattern */
   var logs = rampart.utils.glob("/var/log/*.log");


zipList
'''''''

Read a zip archive's central directory and return a description of every
entry.  Does not extract, decompress or modify anything.

Usage:

.. code-block:: javascript

   var entries = rampart.utils.zipList(zipPath);

Where ``zipPath`` is a :green:`String`, the path to a zip file on disk.

Return Value:
   :green:`Object`.  Keys are entry names (as stored in the archive,
   so directory entries end with ``"/"``).  Each value is an
   :green:`Object` with these properties:

   ============== ================================================================
   Property       Description
   ============== ================================================================
   size           :green:`Number`. Uncompressed size in bytes.
   compressedSize :green:`Number`. Stored size in bytes.
   method         :green:`Number`. Compression method (``0`` = stored, ``8`` = deflate).
   crc32          :green:`Number`. CRC-32 of the uncompressed bytes.
   mode           :green:`Number`. Unix mode bits (file type + permissions).
   mtime          :green:`Date`. Modification time (omitted if the entry has none).
   isFile         :green:`Boolean`. ``true`` for regular files.
   isDirectory    :green:`Boolean`. ``true`` for directory entries.
   isSymbolicLink :green:`Boolean`. ``true`` for symlink entries.
   permissions    :green:`String`. ``ls -l``-style mode string, e.g. ``"-rw-r--r--"``.
   ============== ================================================================

Throws if the path is not a readable zip archive.

.. code-block:: javascript

   var e = rampart.utils.zipList("/tmp/release.zip");
   // { "README.md": { size: 1024, isFile: true, ... },
   //   "lib/":      { size: 0,    isDirectory: true, ... },
   //   "lib/x.js":  { size: 5120, isFile: true, ... },
   //   ... }

   for (var name in e)
       if (e[name].isFile)
           console.log(name, e[name].size, "bytes");


zipGet
''''''

Read one entry from a zip archive into a :green:`Buffer`.  The entry is
decompressed if it was stored with deflate.

Usage:

.. code-block:: javascript

   var buf = rampart.utils.zipGet(zipPath, entryName);

Where ``zipPath`` is a :green:`String` (path to the zip file on disk) and
``entryName`` is a :green:`String` naming an entry as it appears in
``zipList()``\'s output.  Symbolic-link entries are followed; the returned
buffer is the contents of the link's final target inside the archive.

Return Value:
   :green:`Buffer`.  The uncompressed contents of the named entry.

Throws if the zip cannot be opened, the entry is missing, the entry uses
an unsupported compression method, or a symlink chain cannot be resolved.

.. code-block:: javascript

   var b = rampart.utils.zipGet("/tmp/release.zip", "README.md");
   console.log(rampart.utils.bufferToString(b));


zipExtract
''''''''''

Extract some or all of a zip archive's entries to a destination directory
on disk.  File modes, modification times and symbolic links are preserved.

Usage:

.. code-block:: javascript

   var n = rampart.utils.zipExtract(zipPath, destPath [, entries]);

Where:

   * ``zipPath`` -- :green:`String`. The zip file on disk.
   * ``destPath`` -- :green:`String`. The destination directory.  It and any
     intermediate directories implied by entry names will be created as
     needed.
   * ``entries`` -- :green:`Array` of :green:`Strings`, optional.  If
     omitted (or ``undefined`` / ``null``), every entry is extracted.
     If supplied, only entries whose name matches one of the listed
     names is extracted.  Names ending in ``"/"`` match every entry in
     that directory tree; bare names match exactly.

Return Value:
   :green:`Number`.  The count of entries actually written to disk.  Directory
   entries in the archive are not counted; only files (and symlinks, if
   extracted) contribute to the total, so the number may be lower than the
   entry count reported by `zipList`_\ ().

Notes:

   * Path traversal is blocked: an entry name containing ``..`` segments
     that would resolve outside ``destPath`` is rejected.
   * Symlinks are extracted as real symlinks (``symlink(target, path)``);
     the link target is taken verbatim from the archive without further
     validation.
   * ``chmod`` and ``utimensat`` failures during metadata fixup are
     silently ignored so a bulk extract is not aborted by a single
     unprivileged operation.

.. code-block:: javascript

   // unpack the entire archive
   rampart.utils.zipExtract("/tmp/release.zip", "/opt/myapp/");

   // unpack just two specific files
   rampart.utils.zipExtract("/tmp/release.zip", "/tmp/cfg/",
                            ["README.md", "etc/default.conf"]);

   // unpack everything under "templates/"
   rampart.utils.zipExtract("/tmp/release.zip", "/var/www/", ["templates/"]);

.. note::

    The bundle feature exposes three additional functions --
    ``rampart.utils.payloadList()``, ``rampart.utils.payloadGet(name)`` and
    ``rampart.utils.payloadExtract(destDir [, name|nameArray])`` -- which
    operate on the zip archive appended to ``rampart`` itself rather than
    on a zip file on disk.  See
    :ref:`Single-File Bundles <rampart-extras:Single-File Bundles>`.


copyFile
''''''''

Make a copy of a file.

Usage:

.. code-block:: javascript

   rampart.utils.copyFile({src: source, dest: destination [, overwrite: overWrite]});

   /* or */

   rampart.utils.copyFile(source, destination [, overWrite]);

Where ``source`` is a :green:`String`, the file to be copied, ``destination`` is a
:green:`String`, the name of the target file and optional ``overWrite`` is a :green:`Boolean`
which if ``true`` will overwrite ``destination`` if it exists.

For directory trees, see `cp`_\ () below.

Return Value:
   ``undefined``.

cp
''

Copy a file or, with ``{recursive: true}``, an entire directory tree.
Symbolic links are preserved unless ``dereference`` is set.

Usage:

.. code-block:: javascript

   rampart.utils.cp(src, dest [, options]);

Where:

* ``src`` is a :green:`String`, the source file or directory.

* ``dest`` is a :green:`String`, the destination path.

* ``options`` is an optional :green:`Object`:

  * ``recursive`` -- :green:`Boolean`, default ``false``.  Required when
    ``src`` is a directory; throws otherwise.

  * ``dereference`` -- :green:`Boolean`, default ``false``.  If ``true``,
    follow symlinks (copy the target's contents).  If ``false``, replicate
    the link as a link.

  * ``preserveTimestamps`` -- :green:`Boolean`, default ``false``.  If
    ``true``, copy ``atime`` and ``mtime`` from each source entry.

  * ``force`` -- :green:`Boolean`, default ``false``.  If ``true``, ignore
    missing-destination errors silently when running with
    ``errorOnExist: true``.

  * ``errorOnExist`` -- :green:`Boolean`, default ``false``.  If ``true``,
    fail with ``EEXIST`` rather than silently overwriting an existing
    destination file (uses ``O_EXCL`` internally).

Return Value:
   ``undefined``.  Throws on the first error encountered (a partial copy
   may have already been written).

Example:

.. code-block:: javascript

   /* Single file */
   rampart.utils.cp("conf.json", "conf.json.bak");

   /* Recursive deep copy */
   rampart.utils.cp("/src/proj", "/backup/proj", {
      recursive: true,
      preserveTimestamps: true
   });

rmFile
''''''

Delete a single file.

Usage:

.. code-block:: javascript

   rampart.utils.rmFile(filename);

Where ``filename`` is a :green:`String`, the name of the file to be removed.

For removing directory trees, see `rm`_\ () below.

Return Value:
   ``undefined``.

rm
''

Remove a file or, with ``{recursive: true}``, an entire directory tree
(``rm -rf`` semantics).  Walks children before parents so directories are
empty by the time they are removed.

Usage:

.. code-block:: javascript

   rampart.utils.rm(path [, options]);

Where:

* ``path`` is a :green:`String`, the file or directory to remove.

* ``options`` is an optional :green:`Object`:

  * ``recursive`` -- :green:`Boolean`, default ``false``.  Required when
    ``path`` is a non-empty directory.

  * ``force`` -- :green:`Boolean`, default ``false``.  If ``true``, missing
    paths are silently ignored (rather than throwing ``ENOENT``).

Return Value:
   ``undefined``.

Example:

.. code-block:: javascript

   rampart.utils.rm("/tmp/build", {recursive: true, force: true});

link
''''

Create a hard link.

Usage:

.. code-block:: javascript

   rampart.utils.link({src: sourceName, target: targetName});

   /* or */

   rampart.utils.link(sourceName, targetName);

Where ``sourceName`` is the existing file and ``targetName`` is the name of
the to-be-created link.

Return Value:
   ``undefined``.

symlink
'''''''
Create a soft (symbolic) link.

Usage:

.. code-block:: javascript

   rampart.utils.symlink({src: sourceName, target: targetName});

   /* or */

   rampart.utils.symlink(sourceName, targetName);

Where ``sourceName`` is the existing file and ``targetName`` is the name of
the to-be-created symlink.

Return Value:
   ``undefined``.

readLink
''''''''

Read the target path of a symbolic link.  The link target string is
returned verbatim (it is *not* resolved to an absolute path -- use
`realPath`_\ () for that).

Usage:

.. code-block:: javascript

   var target = rampart.utils.readLink(path);

Where ``path`` is a :green:`String`, the symlink itself.

Return Value:
   :green:`String`, the link's target.  Throws if ``path`` does not exist
   or is not a symbolic link (``EINVAL``).

chmod
'''''

Change the file mode bits of a file or directory.

Usage:

.. code-block:: javascript

   rampart.utils.chmod(path [, mode]);

Where ``path`` is a :green:`String`, the file or directory upon which to be operated
and ``mode`` is a :green:`Number` or :green:`String`, the octal permissions mode.
Throws error if lacking permissions or if another error was encountered.

Note that ``mode`` is normally given as an octal.  As such it can be, e.g.,
``0755`` (octal number) or ``"755"`` (:green:`String` representation of an octal
number), but ``755``, as a decimal number will likely not work as intended.

Return Value:
   ``undefined``.

realPath
''''''''

Find the canonical form of a file system path.  The path or file must exist.

Usage:

.. code-block:: javascript

   rampart.utils.realPath(path);

Where ``path`` is a :green:`String`, not necessarily in canonical form.

Return Value:
   A :green:`String`, the canonical form of the path.  Throws an error if path does not exist.

truncate
''''''''

Truncate or extend a regular file to a given size in bytes.  Extending a
file beyond its current size creates a hole; reading the hole returns NUL
bytes.

Usage:

.. code-block:: javascript

   rampart.utils.truncate(path, length);

Where ``path`` is a :green:`String` and ``length`` is a :green:`Number`
(non-negative).

Return Value:
   ``undefined``.  Throws if the file cannot be opened/modified.

See also `truncate (2)
<https://man7.org/linux/man-pages/man2/truncate.2.html>`_.  For truncating
via an open file handle, see `fh.ftruncate`_\ () in the File Handle
Utilities section, or `ftruncateFd`_\ () in the POSIX File Descriptor I/O
section.

touch
'''''

Create an empty file, or update the access timestamp of an existing file.

Usage:

.. code-block:: javascript

   rampart.utils.touch(file);

   /* or */

   rampart.utils.touch({
      path: file
      [, nocreate: noCreate]
      [, setaccess: setAccess]
      [, setmodify: setModify]
      [, reference: referenceFile]
   });

Where:

* ``file`` is a :green:`String`, the name of the file upon which to operate,

* ``noCreate`` is a :green:`Boolean` (default ``false``) which, if ``true``
  will only update the timestamp, and will not create a non-existing
  ``file``.

* ``setAccess`` is a :green:`Boolean` (default ``true``), a :green:`Date Object`,
  or a :green:`Number` (seconds since unix epoch).  Update access time of
  the file to specified date or current date if ``true``.  Do not update if
  ``false``.

* ``setModify`` is a :green:`Boolean` (default ``true``), a :green:`Date Object`,
  or a :green:`Number` (seconds since unix epoch).  Update modification time of
  the file to specified date or current date if ``true``.  Do not update if
  ``false``.

* ``referenceFile`` is a :green:`String`.  If specified, the named file's access and
  modification timestamps will be used rather than the current time/date.

Return Value:
   ``undefined``.

rename
''''''

Rename or move a file.

Usage:

.. code-block:: javascript

   rampart.utils.rename(source, destination);

Where ``source`` is a :green:`String`, the file to be renamed or moved, ``destination`` is a
:green:`String`, the name of the target file.

Return Value:
   ``undefined``.

chown
'''''

Change the owning user and/or group of a file or directory.  Follows
symbolic links (the target gets the new ownership); use `lchown`_\ ()
to operate on the link itself.

Usage:

.. code-block:: javascript

   rampart.utils.chown(path [, user] [, group]);

   /* or */

   rampart.utils.chown({path: path [, user: user] [, group: group]});

Where:

* ``path`` is a :green:`String`, the file or directory to modify.

* ``user`` is a :green:`String` (account name, looked up via
  ``getpwnam(3)``) or a :green:`Number` (uid).  Omit -- or pass a
  negative number in positional form -- to leave the user unchanged.

* ``group`` is a :green:`String` (group name, looked up via
  ``getgrnam(3)``) or a :green:`Number` (gid).  Omit to leave the
  group unchanged.

Return Value:
   ``undefined``.  Throws if the path does not exist, the named user/
   group is unknown, or the caller lacks permission (typically only
   root may change ``user``).

Example:

.. code-block:: javascript

   /* By name */
   rampart.utils.chown("/var/log/myapp", "www-data", "adm");

   /* By id */
   rampart.utils.chown("/srv/data", 1000, 1000);

   /* Change group only, leave user alone */
   rampart.utils.chown({path: "/srv/data", group: "deploy"});

lchown
''''''

Change ownership of a symbolic link itself, *not* the link's target.
Compare to ``chmod``/``chown`` which follow symlinks.  Useful for
backup/restore tools and any code that needs to faithfully reproduce
filesystem state.

Usage:

.. code-block:: javascript

   rampart.utils.lchown(path, user, group);

Where:

* ``path`` is a :green:`String`, the symlink to modify.

* ``user`` and ``group`` are each a :green:`String` (name -- looked up
  via ``getpwnam``/``getgrnam``) or a :green:`Number` (uid/gid).  A
  negative number leaves that side unchanged.

Return Value:
   ``undefined``.  Throws if the symlink does not exist or the caller
   lacks permission.

lchmod
''''''

Change permission bits on a symbolic link itself, where the platform
supports it.

Usage:

.. code-block:: javascript

   rampart.utils.lchmod(path, mode);

Where ``mode`` follows the same conventions as `chmod`_\ () (integer
like ``0o644`` or octal string like ``"644"``).

Platform notes:
   * macOS, FreeBSD: changes the symlink's own mode (uses ``lchmod(2)``).
   * Linux: the kernel has no syscall to alter a symlink's mode bits
     (and they aren't honored anyway).  This function throws with an
     ``ENOSYS``-style message for actual symlinks, but falls through to
     a plain `chmod`_\ () for non-symlink paths, so portable callers
     don't have to special-case the not-a-symlink case.

Return Value:
   ``undefined``.

lUtimes
'''''''

Set access and modification timestamps on a symbolic link itself.
Like `touch`_\ () but operates on the link rather than its target.

Usage:

.. code-block:: javascript

   rampart.utils.lUtimes(path, atime, mtime);

   /* or */

   rampart.utils.lUtimes(path, {setaccess: atime, setmodify: mtime});

Where:

* ``path`` is a :green:`String`, the symlink to modify.

* ``atime`` / ``mtime`` are each a :green:`Number` (seconds since the
  unix epoch) or a :green:`Date`.  Omitted timestamps default to the
  current values on the link.

Internally uses ``utimensat(…, AT_SYMLINK_NOFOLLOW)`` on Linux and
``lutimes(3)`` elsewhere.

Return Value:
   ``undefined``.

sleep
'''''

Pause execution for specified number of seconds.

Usage:

.. code-block:: javascript

   rampart.utils.sleep(seconds);

Where ``seconds`` is a :green:`Number`.  Seconds may be a fraction of seconds.
Internally `nanosleep <https://man7.org/linux/man-pages//man2/nanosleep.2.html>`_
is used.

Example:

.. code-block:: javascript

   /* wait 1.5 seconds */
   rampart.utils.sleep(1.5);

tmpDir
''''''

Return the system temporary directory.  Honors the standard environment
variables ``TMPDIR``, ``TMP`` and ``TEMP`` in that order; falls back to
``/tmp`` on POSIX systems.  The returned path is *not* validated to
exist -- callers should create whatever subdirectory they need (often via
`mkdTemp`_\ ()).

Usage:

.. code-block:: javascript

   var tmp = rampart.utils.tmpDir();

Return Value:
   :green:`String`.  Trailing slashes are stripped.

homeDir
'''''''

Return the current user's home directory.  Uses the ``HOME`` environment
variable when set; otherwise calls ``getpwuid(geteuid())`` to look it up
in ``/etc/passwd``.

Usage:

.. code-block:: javascript

   var home = rampart.utils.homeDir();

Return Value:
   :green:`String`, the home directory.  Throws only if neither lookup
   succeeds (extremely rare).

getType
'''''''

Get the type of variable. A simplified but more specific version of
``typeof``.

Usage:

.. code-block:: javascript

    var type = rampart.utils.getType(myvar);

Return Value:
  A :green:`String`, one of ``String``, ``Array``, ``Number``, ``Function``,
  ``Boolean``, ``Buffer`` (any buffer type), ``Nan``, ``Null``,
  ``Undefined``, ``Date``, ``Vector``, ``Filehandle`` or ``Object`` (excluding any of the other types of
  :green:`Objects` such as ``Null``, ``Array``, ``Function``, ``Vector`` or ``Filehandle``) .

timezone
''''''''

Retrieve system timezone information.

Usage:

.. code-block:: javascript

   var tz = rampart.utils.timezone([directory]);

Where ``directory`` is an optional directory with timezone information.  Default
is ``"/usr/share/zoneinfo"``.

Return Value:
  An :green:`Object` with the following functions: ``findZone()``, ``findAbbr()`` and ``dump()``.

* ``tz.findZone(tzname)`` - Return an :green:`Object` with timezone information.  If the timezone 
  does not exist, returns ``undefined``.

* ``tz.findAbbr(abbrname)`` - Return an :green:`Object` with a list of timezones that match the given
  abbreviation.  If the abbreviation does not exist, returns ``undefined``.

* ``tz.dump()`` - Return an :green:`Object` with the entire database organized by timezones and 
  abbreviations.


Example:

.. code-block:: javascript

   var tz = rampart.utils.timezone();

   var pstZones = tz.findAbbr("PST");
   /*  ambiguous     => whether there are zones in "entries" with differing offsets
       zoneAbbrIndex => where in the "abbreviations" section of the zone info
                        below "PST" is found
   {
      "ambiguous": true,
      "entries": [
         {
            "offset": -28800,
            "offsetString": "-8:00",
            "zoneName": "America/Bahia_Banderas",
            "zoneAbbrIndex": 5
         },
         {
            "offset": -28800,
            "offsetString": "-8:00",
            "zoneName": "America/Boise",
            "zoneAbbrIndex": 2
         },
         ...
      ]
   }
   */   

   var LAZone = tz.findZone("America/Los_Angeles");
   /*
   {
      "name": "America/Los_Angeles",
      "abbreviations": [
         {
            "Abbreviation": "LMT",
            "UTCOffset": -28378,
            "isDST": false
         },
         {
            "Abbreviation": "PDT",
            "UTCOffset": -25200,
            "isDST": true
         },
         {
            "Abbreviation": "PST",
            "UTCOffset": -28800,
            "isDST": false
         },
         {
            "Abbreviation": "PWT",
            "UTCOffset": -25200,
            "isDST": true
         },
         {
            "Abbreviation": "PPT",
            "UTCOffset": -25200,
            "isDST": true
         },
         {
            "Abbreviation": "PST",
            "UTCOffset": -28800,
            "isDST": false
         }
      ],
      "transitions": [
         {
            "transitionDate": "1901-12-13T20:45:52.000Z",
            "transition": {
               "Abbreviation": "PST",
               "UTCOffset": -28800,
               "isDST": false
            }
         },
         {
            "transitionDate": "1918-03-31T10:00:00.000Z",
            "transition": {
               "Abbreviation": "PDT",
               "UTCOffset": -25200,
               "isDST": true
            }
         },
         ...
      ]
   }
   */

dateFmt
'''''''

Format a date :green:`String`.

Usage:

.. code-block:: javascript

    var datestr = rampart.utils.dateFmt(format[, date][, tz])

Where:

   * ``format`` is a `strftime <https://linux.die.net/man/3/strftime>`_ style format
     :green:`String`.

   * ``date`` is an optional date as a :green:`String`, :green:`Number` (seconds since 1970-01-01),
     or a :green:`Date`.  The default value is the current time.  A :green:`String` date is parsed
     automatically (the same parser used by `autoScanDate`_\ ), trying the following formats in order:

.. code-block:: none

    "%Y-%m-%d %H:%M:%S %z"
    "%A %B %d %H:%M:%S %Y %z"
    "%Y-%m-%d %H:%M:%S"
    "%A %B %d %H:%M:%S %Y"
    "%Y-%m-%dT%H:%M:%S"
    "%c"

   * ``tz`` is an optional :green:`Number` or :green:`Boolean` selecting the time zone of the
     output:

       *  A :green:`Number` is a time zone offset in **hours** from UTC; the time is shown shifted
          to that offset (e.g. ``-8`` for US Pacific Standard Time, ``0`` for UTC).

       *  A :green:`Boolean` ``true`` formats the date in the system's **local** time; ``false``
          (the default) formats in **UTC/GMT**.

Return Value:
   The formatted date as a :green:`String`.

Note:

   *  By default the output is in **UTC/GMT**.  Pass ``true`` as the third argument for local time,
      or a :green:`Number` for a specific UTC offset (in hours).

   *  Millisecond notation in the string in the form of ``.123`` or ``.123Z`` is disregarded.

   *  If year or year/month/day formats are missing, the current year or date respectively is assumed.

   *  If a :green:`String` ``date`` includes a numeric ``%z`` style zone (e.g. ``-0500``), it is
      converted from that zone before being formatted in the output zone.

   *  The ``%Z`` format outputs the zone abbreviation but does not itself change the output zone.

Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   printf( "%s\n%s\n%s\n%s\n%s\n",
       dateFmt("%Y-%m-%d %H:%M:%S", "1999-12-31 23:59:59 -0000"),
       dateFmt("%Y-%m-%d %H:%M:%S", "1999-12-31 23:59:59 -0500"),
       dateFmt("%Y-%m-%dT%H:%M:%S", 946713599),
       dateFmt("%Y-%m-%dT%H:%M:%S", 946713599, -8),
       dateFmt("%c", "Mon Jul 26 12:00:01 2021")
   );

   /* Expected output (UTC/GMT, since no local/offset is requested except line 4):
   1999-12-31 23:59:59
   2000-01-01 04:59:59
   2000-01-01T07:59:59
   1999-12-31T23:59:59
   Mon 26 Jul 2021 12:00:01 PM GMT
   */

scanDate
''''''''

Scan a date :green:`String` and return a JavaScript date.

Usage:

.. code-block:: javascript

   var mydate = rampart.utils.scanDate(dateString[, default_offset][, input_format]);

Where:

   * ``dateString`` is the same as ``date`` (as a :green:`String`) in `dateFmt`_ above.

   * ``default_offset`` is the time zone offset in seconds to use if not provided in ``dateString``.
     The default is ``0`` (UTC).

   * ``input_format`` is the same as in `dateFmt`_ above.

Return Value:
   A JavaScript :green:`Date`.


autoScanDate
''''''''''''

Attempt to match a date from a :green:`String` using various formats.

Usage:

.. code-block:: javascript

    var dateRes = autoScanDate(dateString);


Return Value: An :green:`Object`.

   If no timezone offset or abbreviation is present, the return object has 
   the following properties:
   
   * ``date``          - a JavaScript :green:`Date`.
   * ``offset``        - timezone offset (in this case ``0`` for GMT).
   * ``endIndex``      - last character position in ``dateString`` of the match.
   * ``matchedFormat`` - the format that was successfully matched.

   If a timezone offset is present, offset will be set to that timezone and 
   and GMT will be returned with the offset set.

   If a timezone abbreviation is present and valid, offset will be set to 
   the best matching timezone (as sorted by distance from the system timezone).
   Also present is ``dates`` for all the possible timezones which match the 
   abbreviation, filtered by the validity of the Abbreviation (i.e. PST vs PDT) 
   on the given date.

   If ``dateString`` could not be parsed, ``Null`` is returned.

Example:


.. code-block:: javascript

   var dateRes = autoScanDate("Jan 5 03:20 pm 2002");
   /*
   {
      "date": "2002-01-05T15:20:00.000Z",
      "offset": 0,
      "endIndex": 19,
      "matchedFormat": "%b %e %I:%M %p %Y"
   }
   */

   dateRes=autoScanDate("Jan 5 03:20 pm 2002 -0800");
   /*
   {
      "date": "2002-01-05T23:20:00.000Z",
      "offset": -28800,
      "endIndex": 25,
      "matchedFormat": "%b %e %I:%M %p %Y %z"
   }
   */

   dateRes=autoScanDate("Jan 5 03:20 pm 2002 PST");
   /* "dates" is sorted by distance from current timezone offset
      "ambiguous" is true because Manila has a timezone abbv "PST"
      "date" is set to the first record in "dates"
   {
      "ambiguous": true,
      "dates": {
         "America/Dawson": "2002-01-05T23:20:00.000Z",
         "America/Fort_Nelson": "2002-01-05T23:20:00.000Z",
         "America/Metlakatla": "2002-01-05T23:20:00.000Z",
         "America/Ensenada": "2002-01-05T23:20:00.000Z",
         "America/Santa_Isabel": "2002-01-05T23:20:00.000Z",
         "America/Tijuana": "2002-01-05T23:20:00.000Z",
         "America/Los_Angeles": "2002-01-05T23:20:00.000Z",
         ...
         "Asia/Manila": "2002-01-05T07:20:00.000Z",
         "posix/Asia/Manila": "2002-01-05T07:20:00.000Z",
         "right/Asia/Manila": "2002-01-05T07:20:00.000Z"
      },
      "date": "2002-01-05T23:20:00.000Z",
      "offset": -28800,
      "endIndex": 23,
      "matchedFormat": "%b %e %I:%M %p %Y %Z"
   }
   */

   dateRes=autoScanDate("Aug 5 03:20 pm 2002 PST");
   /* note that most timezones that would match PST are observing
      PDT in August, so they are excluded.
   {
      "ambiguous": true,
      "dates": {
         "America/Metlakatla": "2002-08-05T23:20:00.000Z",
         "posix/America/Metlakatla": "2002-08-05T23:20:00.000Z",
         "right/America/Metlakatla": "2002-08-05T23:20:00.000Z",
         "Asia/Manila": "2002-08-05T07:20:00.000Z",
         "posix/Asia/Manila": "2002-08-05T07:20:00.000Z",
         "right/Asia/Manila": "2002-08-05T07:20:00.000Z"
      },
      "date": "2002-08-05T23:20:00.000Z",
      "offset": -28800,
      "endIndex": 23,
      "matchedFormat": "%b %e %I:%M %p %Y %Z"
   }
   */

use
'''

Shortcut and alternative for importing modules with :ref:`require <rampart-main:Using the require Function to Import Modules>`\ .

Usage:

.. code-block:: javascript

    rampart.globalize(rampart.utils);//put utils in the global namespace

    var Sql = use.sql; //same as var Sql = require("rampart-sql");

The ``use`` :green:`Object` is a proxy object which uses the property name referenced (here ``"sql"``) and searches for
a module named ``"rampart-sql"``.  Failing that it will search for a module named ``"sql"``.  It will then call
the :ref:`require <rampart-main:Using the require Function to Import Modules>` function to import and return that value.
If no module can be found, it will throw an error.

Return Value:
    The exported module.

load
''''

Same as `use`_ above except that the property name is also put in the global namespace.

Example:

.. code-block:: javascript

    rampart.globalize(rampart.utils);//put utils in the global namespace

    load.curl;  //same as global.curl = require("rampart-curl");

    var res = curl.fetch("http...");

Note:
    The file name of the module must be lowercase, while the variable name may be
    mixed case.  Example: ``load.Sql;`` is equivalent to
    ``global.Sql=require("rampart-sql");``.

Caveat:
    This cannot be used to load a module whose name contains illegal JavaScript variable name characters. Thus,
    ``load["my@mod"]`` will not work since ``'@'`` is not legal in javaScript even though it is legal in a file name.
    However ``'-'`` and ``'.'`` characters will be replaced with ``'_'``.  Thus, ``load["rampart-curl.so"]`` will
    load the Curl Module and put it in the global namespace similar to ``var rampart_curl_so = require("rampart-curl.so")``.

errorConfig
'''''''''''

Configure the format of reported errors.

Usage:

.. code-block:: javascript

    rampart.utils.errorConfig(options);

   /* or */

   rampart.utils.errorConfig(simple, lines);

Where:

* ``options`` is an :green:`Object` with the properties ``simple`` and
  ``lines``.

* ``simple`` is a :green:`Boolean` (default ``false``) - whether to reduce
  the verbosity of the stack trace.

* ``lines`` is a :green:`Number` (default ``0``) - the number of lines of
  the source code surrounding the error to print.  If greater than ``0`` and
  an even number, it will be incremented up to the next odd number.
  
Examples:

.. code-block:: javascript

   /* default settings */
   rampart.utils.errorConfig(false,0);
    
   function myfunc(myvar) {
      console.log(myvar.x);
   }

   myfunc();

   /* expected output
      TypeError: cannot read property 'x' of undefined
          at [anon] (/usr/local/src/rampart/src/duktape/core/duktape.c:60539) internal
          at myfunc (myscript.js:4)
          at global (myscript.js:7) preventsyield
   */

.. code-block:: javascript

   /* simple stack */
   rampart.utils.errorConfig({simple:true,lines:0});

   function myfunc(myvar) {
      console.log(myvar.x);
   }

   myfunc();

   /* expected output
      TypeError: cannot read property 'x' of undefined
          at myfunc (myscript.js:4)
          at global (myscript.js:7)
   */


.. code-block:: javascript

   /* simple stack and 3 lines */
   rampart.utils.errorConfig({simple:true,lines:3});

   function myfunc(myvar) {
      console.log(myvar.x);
   }

   myfunc();

   /* expected output
      TypeError: cannot read property 'x' of undefined
          at myfunc (myscript.js:4)
          at global (myscript.js:7)

      File: myscript.js
      line 3:    |function myfunc(myvar) {
      line 4: -> |    console.log(myvar.x);
      line 5:    |}
   */

deepCopy
''''''''

Make a deep copy of one or more :green:`Objects`.

Usage:

.. code-block:: javascript

   var target = rampart.utils.deepCopy([appendArrays [, copyBuffers]], target, obj1[, obj2, obj3, ...]);

Where:

    * ``appendArrays`` - a :green:`Boolean`, whether to append an :green:`Array` with the same key
      instead of replace it with the source :green:`Array`.  Default is ``false``.

    * ``copyBuffers`` - a :green:`Boolean`, whether to copy the full binary contents of a :green:`Buffer`
      rather than its reference.  Default is ``true``.

    * ``target`` - an :green:`Object` into which the subsequent :green:`Object` parameters will be copied.

    * ``objn``- Source :green:`Objects` to copy from, with later :green:`Objects` overwriting duplicate keys in earlier ones.


Example:

.. code-block:: javascript

    var target, 
        source1 = {
            account: {
                firstName: "John"
            },
            links: [
                'http://example.com/jsmith1.html'
            ]
        },
        source2 = {
            account: {
                lastName: "Smith"
            },
            links: [
                'http://example.com/jsmith_about.html'
            ]
        };

    target = rampart.utils.deepCopy({}, source1, source2);
    rampart.utils.printf("%3J\n", target);
    /* expected output:
        {
           "account": {
              "firstName": "John",
              "lastName": "Smith"
           },
           "links": [
              "http://example.com/jsmith_about.html"
           ]
        }
    */

    // true == append the "links" array
    target = rampart.utils.deepCopy(true, {}, source1, source2);
    rampart.utils.printf("%3J\n", target);
    /* expected output:
        {
           "account": {
              "firstName": "John",
              "lastName": "Smith"
           },
           "links": [
              "http://example.com/jsmith1.html",
              "http://example.com/jsmith_about.html"
           ]
        }
    */


eventCallback
'''''''''''''

Register a callback :green:`Function` to catch warnings or errors produced by Rampart's event loop (libevent2).

Usage:

.. code-block:: javascript

   rampart.utils.eventCallback(function(level,msg){ /* handle or report here */ });

Where:

    * ``level`` - a :green:`String`, one of ``"msg"``, ``"warn"`` or ``"error"``.
    * ``msg`` - a :green:`String`, the message from libevent2.

Note:
    In normal usage, this function should not be necessary.  If used, the callback function must
    not call any asynchronous functions.  See `the libevent2 reference <https://libevent.org/libevent-book/Ref1_libsetup.html>`_
    for more information.
    

getScopeVars
''''''''''''

Inspect variables visible at the current call scope.  This function can
return all variables grouped by scope type, or look up a single variable
by name.

Usage:

.. code-block:: javascript

   var scopes = rampart.utils.getScopeVars();

   /* or */

   var result = rampart.utils.getScopeVars(varname);

Where ``varname`` is an optional :green:`String` — the name of a single
variable to look up.

**Mode 1: No arguments** — returns an :green:`Object` with all variables
grouped by scope:

.. code-block:: javascript

   var scopes = rampart.utils.getScopeVars();
   /* scopes = {
        local:   { x: 1, y: "hello" },
        closure: { outerVar: 42 },
        global:  { ... }
      }
   */

The returned :green:`Object` has the following properties:

+------------+------------------+----------------------------------------------------------------+
|Property    |Type              |Description                                                     |
+============+==================+================================================================+
|local       |:green:`Object`   | Variables declared in the calling function (``var``            |
|            |                  | declarations and formal parameters), with current values.      |
+------------+------------------+----------------------------------------------------------------+
|closure     |:green:`Object`   | Variables from enclosing function scopes (parent declarative   |
|            |                  | environments).  If multiple closure scopes exist, they are     |
|            |                  | merged with inner scopes taking priority.                      |
+------------+------------------+----------------------------------------------------------------+
|with        |:green:`Object`   | Present only if a ``with`` statement scope exists.  Contains   |
|            |                  | the ``with`` target object.                                    |
+------------+------------------+----------------------------------------------------------------+
|global      |:green:`Object`   | The global object.                                             |
+------------+------------------+----------------------------------------------------------------+

**Mode 2: String argument** — looks up a single variable by name across
all scopes (innermost first), returning an :green:`Object` describing
where it was found:

.. code-block:: javascript

   var result = rampart.utils.getScopeVars("myVar");
   /* result = { value: 42, scope: "closure" }
      or undefined if not found */

+------------+------------------+----------------------------------------------------------------+
|Property    |Type              |Description                                                     |
+============+==================+================================================================+
|value       |any               | The current value of the variable.                             |
+------------+------------------+----------------------------------------------------------------+
|scope       |:green:`String`   | The scope in which the variable was found: ``"local"``,        |
|            |                  | ``"closure"``, ``"with"``, or ``"global"``.                    |
+------------+------------------+----------------------------------------------------------------+

Return Value:
   :green:`Object` or ``undefined``.  In Mode 1, always returns an
   :green:`Object`.  In Mode 2, returns an :green:`Object` if the
   variable was found, or ``undefined`` if it was not found in any scope.

Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var globalVar = "gval";

   function outer() {
       var x = 10;

       function inner() {
           var y = 20;

           /* inspect all scopes */
           var scopes = getScopeVars();
           printf("local y = %d\n", scopes.local.y);       // 20
           printf("closure x = %d\n", scopes.closure.x);   // 10

           /* look up a single variable */
           var info = getScopeVars("x");
           printf("x = %d (from %s scope)\n", info.value, info.scope);
           // x = 10 (from closure scope)

           var missing = getScopeVars("noSuchVar");
           printf("missing = %s\n", typeof missing);        // undefined
       }
       inner();
   }
   outer();

**collapse()** — The :green:`Object` returned by ``getScopeVars()`` (Mode 1)
has a ``collapse()`` method that merges all scopes into a single flat
:green:`Object`.  Scopes are applied outermost-first (global, then with,
then closure, then local), so inner scopes shadow outer ones — matching
JavaScript's actual variable resolution order.

Variables added via ``rampart.localize()`` are included in the local scope.

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var globalVar = "gval";

   function outer() {
       var x = 10;

       function inner() {
           var y = 20;

           var scopes = getScopeVars();
           var all = scopes.collapse();

           printf("y = %d\n", all.y);             // 20 (local)
           printf("x = %d\n", all.x);             // 10 (closure)
           printf("globalVar = %s\n", all.globalVar); // "gval" (global)

           /* local shadows global */
           var rampart = "shadowed";
           all = getScopeVars().collapse();
           printf("rampart = %s\n", all.rampart);  // "shadowed"
       }
       inner();
   }
   outer();

Return Value:
   :green:`Object`.  A flat :green:`Object` with ``{name: value}`` entries
   for every variable visible at the point of the ``getScopeVars()`` call.

repl
''''

Create an interactive line editor backed by the same line-editing engine
used by the built-in Rampart REPL.  Provides prompt, history, and
multi-line editing (Ctrl-X toggle).

Usage:

.. code-block:: javascript

   var r = rampart.utils.repl(options);

   /* or */

   var r = rampart.utils.repl(prompt);

Where ``options`` is an :green:`Object` with the following optional keys
(or ``prompt`` is a :green:`String` shortcut equivalent to passing
``{prompt: prompt}``):

+----------------+-----------------+--------------------------------------------------------------+
|Argument        |Type             |Description                                                   |
+================+=================+==============================================================+
|prompt          |:green:`String`  | Text drawn before each input line. Default is ``""``.        |
+----------------+-----------------+--------------------------------------------------------------+
|history         |:green:`Number`  | Maximum number of history entries kept in memory.            |
|                |                 | Default is ``1024``.                                         |
+----------------+-----------------+--------------------------------------------------------------+
|historyFile     |:green:`String`  | Path to a history file. If the file exists, its contents     |
|                |                 | are loaded into history (appended to anything already in     |
|                |                 | memory) when ``repl()`` is called. The file is written       |
|                |                 | after every successful line returned by ``.next()``.         |
|                |                 | If the file does not yet exist it is created on first save.  |
+----------------+-----------------+--------------------------------------------------------------+
|redrawOnResume  |:green:`Boolean` | If ``true`` (default), on Ctrl-Z resume the screen content   |
|                |                 | visible just before suspend is restored by replaying a       |
|                |                 | captured copy of everything the REPL had written to          |
|                |                 | ``stdout``. If ``false``, only the prompt and current edit   |
|                |                 | line are redrawn at the cursor's current row (the behavior   |
|                |                 | prior to the capture-and-replay feature being added).        |
|                |                 |                                                              |
|                |                 | Capturing works by routing ``stdout`` through a pipe for the |
|                |                 | rest of the process, so while it is ``true`` the process's   |
|                |                 | file descriptor 1 is not a terminal (rampart's own color     |
|                |                 | detection and forked children are unaffected). Set ``false`` |
|                |                 | to leave ``stdout`` untouched.                               |
+----------------+-----------------+--------------------------------------------------------------+
|objectMode      |:green:`Boolean` | If ``true``, ``.next()`` returns an :green:`Object` with     |
|                |                 | ``{text, status[, signal]}`` instead of the legacy           |
|                |                 | :green:`String` / :green:`null` / :green:`false` return      |
|                |                 | values. Required to distinguish a wake (see                  |
|                |                 | ``rampart.utils.repl.interrupt()`` below) from a Ctrl-C      |
|                |                 | cancel. Default ``false`` for backwards compatibility.       |
+----------------+-----------------+--------------------------------------------------------------+

Return Value:
   :green:`Object`.  A handle with the following methods:

+------------+--------------------------------------------------------------------------+
|Method      |Description                                                               |
+============+==========================================================================+
|next()      | Block, draw the prompt, and return the next line typed by the user.      |
|            |                                                                          |
|            | **Legacy mode** (default — ``objectMode: false``): the return value      |
|            | distinguishes the three end-of-input cases:                              |
|            |                                                                          |
|            | * :green:`String` — a line of input (may be empty if the user just       |
|            |   pressed Enter).                                                        |
|            | * :green:`false` — Ctrl-C was pressed at an empty prompt, **or** the     |
|            |   read was woken via ``rampart.utils.repl.interrupt()``. The two cases   |
|            |   are indistinguishable in legacy mode; use ``objectMode`` if you need   |
|            |   to tell them apart.                                                    |
|            | * :green:`null` — Ctrl-D / EOF / read failure. Typically treated as      |
|            |   "exit the REPL".                                                       |
|            |                                                                          |
|            | **objectMode** (when ``objectMode: true`` was passed to ``repl()``):     |
|            | the return value is always an :green:`Object`:                           |
|            |                                                                          |
|            | * ``{text: <string>, status: 'ok'}`` — a line of input.                  |
|            | * ``{text: '',       status: 'cancel'}`` — Ctrl-C at an empty prompt.    |
|            | * ``{text: '',       status: 'eof'}`` — Ctrl-D / EOF / read failure.     |
|            | * ``{text: '',       status: 'wake', signal: <string>}`` — the read was  |
|            |   woken by ``rampart.utils.repl.interrupt(payload)``; ``signal`` carries |
|            |   the bytes that were on the pipe at wake time (empty string if          |
|            |   ``interrupt()`` was called with no argument).                          |
|            |                                                                          |
|            | In both modes, Ctrl-C pressed while a line *has content* is handled      |
|            | internally: linenoise prints ``^C``, the line buffer is dropped, a       |
|            | fresh prompt is drawn on the next row, and ``.next()`` keeps blocking    |
|            | — it does **not** return.                                                |
+------------+--------------------------------------------------------------------------+
|refresh()   | Redraw the current prompt and edit buffer at the cursor's current        |
|            | position. Use this after any output has been written to the terminal     |
|            | while ``.next()`` is blocked — for example, a timer callback or a        |
|            | worker thread streaming output. Calling ``refresh()`` after the          |
|            | external output completes restores the prompt visibly on a fresh line.   |
|            |                                                                          |
|            | Safe to call from a different thread than the one in ``.next()``,        |
|            | provided no other writer is concurrently writing to ``stdout`` at the    |
|            | same moment. linenoise's state (prompt text, edit buffer, cursor row     |
|            | within the buffer) is not modified while ``.next()`` is parked in        |
|            | ``read()``, so the cross-thread refresh sees consistent data.            |
+------------+--------------------------------------------------------------------------+
|close()     | Restore terminal modes and drop the handle's methods. Call when you no   |
|            | longer plan to use this repl.                                            |
+------------+--------------------------------------------------------------------------+

Module-level methods (called on the ``rampart.utils.repl`` :green:`Function`
itself, not on an instance):

+------------------------+--------------------------------------------------------------+
|Method                  |Description                                                   |
+========================+==============================================================+
|getHistory()            | Return the current in-memory history as an :green:`Array`    |
|                        | of :green:`Strings`, oldest first.                           |
+------------------------+--------------------------------------------------------------+
|replaceHistory(arr)     | Replace the in-memory history with ``arr`` (an               |
|                        | :green:`Array` of :green:`Strings`). Existing entries are    |
|                        | discarded.                                                   |
+------------------------+--------------------------------------------------------------+
|appendHistory(arr)      | Append the contents of ``arr`` (an :green:`Array` of         |
|                        | :green:`Strings`) to the end of the in-memory history        |
|                        | without clearing existing entries.                           |
+------------------------+--------------------------------------------------------------+
|refresh()               | Same as the instance ``refresh()`` method, but callable      |
|                        | without holding a handle to the repl. Useful from worker     |
|                        | threads or library code that does not have access to the     |
|                        | repl object created by main.                                 |
+------------------------+--------------------------------------------------------------+
|interrupt([payload])    | Wake any in-flight ``.next()`` read by writing to an         |
|                        | internal pipe that linenoise polls alongside ``stdin``.      |
|                        | The optional ``payload`` :green:`String` is delivered to     |
|                        | the woken ``.next()`` call as the ``signal`` field of its    |
|                        | result :green:`Object` (objectMode) — used to route on       |
|                        | *why* the wake happened. Calling ``interrupt()`` with no     |
|                        | argument (or ``""``) yields ``signal: ""``.                  |
|                        |                                                              |
|                        | Safe to call from any thread. If no ``.next()`` is currently |
|                        | blocked, the payload is buffered and consumed by the next    |
|                        | ``.next()`` call.                                            |
|                        |                                                              |
|                        | In legacy (non-objectMode) ``.next()``, a wake comes back as |
|                        | :green:`false` — indistinguishable from Ctrl-C at empty      |
|                        | prompt. Use ``objectMode`` if your caller needs to act on    |
|                        | the wake (e.g. to read shared state put by the interrupter)  |
|                        | versus a user cancel.                                        |
+------------------------+--------------------------------------------------------------+

Note:
   linenoise's history is process-global. ``getHistory``,
   ``replaceHistory`` and ``appendHistory`` operate on that single global
   buffer regardless of which ``repl()`` instance is active. They share
   the same buffer with the built-in Rampart REPL if rampart is started
   interactively.

Example (legacy mode):

.. code-block:: javascript

   /* Echo lines; reload history from disk, then prepend a seed at the
    * start of each session so the user has a few commands to up-arrow
    * to as separators. The file accumulates every line typed across
    * sessions. */

   var r = rampart.utils.repl({
       prompt: "echo> ",
       historyFile: "/tmp/echo.hist"
   });

   rampart.utils.repl.appendHistory([
       "/* new session */",
       "help",
       "quit"
   ]);

   while (true) {
       var line = r.next();
       if (line === null) break;                 /* Ctrl-D / EOF */
       if (line === false) {                     /* Ctrl-C at empty prompt */
           rampart.utils.printf("(ignored — use Ctrl-D to exit)\n");
           continue;
       }
       rampart.utils.printf("you said: %s\n", line);
   }

   r.close();

   rampart.utils.printf("history: %J\n", rampart.utils.repl.getHistory());

Example (objectMode with ``interrupt()``):

.. code-block:: javascript

   /* Echo lines on the main thread while a worker thread fires an
    * approval prompt every 5 seconds. The wake interrupts the
    * .next() read; the caller routes on the `signal` field, asks
    * the user a single-key question with getchar(1), and resumes
    * the REPL — all without ever yielding the linenoise instance
    * to another reader. */

   rampart.globalize(rampart.utils);

   function workerMain() {
       rampart.globalize(rampart.utils);
       while (true) {
           sleep(5);
           rampart.utils.repl.interrupt('approve');
       }
   }
   var w = new rampart.thread();
   w.exec(workerMain, null, function(){});

   var r = repl({prompt: "echo> ", objectMode: true});

   while (true) {
       var res = r.next();
       if (res.status === 'eof')    break;
       if (res.status === 'cancel') { printf("(Ctrl-D to exit)\n"); continue; }
       if (res.status === 'wake') {
           if (res.signal === 'approve') {
               printf("[approval] allow? [y/n]: ");
               fflush({stream: "stdout"});
               var ch = (getchar(1) || '').toLowerCase();
               printf("%s\n", ch);
               printf(ch === 'y' ? "[approved]\n" : "[denied]\n");
           }
           continue;
       }
       /* status === 'ok' */
       printf("you said: %s\n", res.text);
   }
   r.close();

Keybindings:
   The same keys as the built-in REPL are honored:

   +--------------------+------------------------------------------------------+
   |Key                 |Action                                                |
   +====================+======================================================+
   |Up / Down arrow     |Scroll through history.                               |
   +--------------------+------------------------------------------------------+
   |Ctrl-A              |Move to beginning of line.                            |
   +--------------------+------------------------------------------------------+
   |Ctrl-C              |If line is empty: ``.next()`` returns :green:`false`  |
   |                    |(legacy) or ``{status: 'cancel'}`` (objectMode). The  |
   |                    |caller decides whether to treat this as cancel, exit, |
   |                    |or both (e.g. on second press).                       |
   +--------------------+------------------------------------------------------+
   |Ctrl-C              |If line is not empty: clear the line buffer and       |
   |                    |redraw a fresh prompt on the next row. ``.next()``    |
   |                    |does not return and keeps reading.                    |
   +--------------------+------------------------------------------------------+
   |Ctrl-D              |Quit (if line is empty) — ``.next()`` returns         |
   |                    |:green:`null` (legacy) or ``{status: 'eof'}``         |
   |                    |(objectMode).                                         |
   +--------------------+------------------------------------------------------+
   |Ctrl-D              |Delete to right (if line is not empty).               |
   +--------------------+------------------------------------------------------+
   |Ctrl-D              |Submit line (if in multi-line mode).                  |
   +--------------------+------------------------------------------------------+
   |Ctrl-E              |Move to the end of line.                              |
   +--------------------+------------------------------------------------------+
   |Ctrl-K              |Delete from current to the end of line.               |
   +--------------------+------------------------------------------------------+
   |Ctrl-L              |Clear and refresh screen.                             |
   +--------------------+------------------------------------------------------+
   |Ctrl-T              |Swap current character with previous.                 |
   +--------------------+------------------------------------------------------+
   |Ctrl-U              |Delete current line.                                  |
   +--------------------+------------------------------------------------------+
   |Ctrl-W              |Delete previous word.                                 |
   +--------------------+------------------------------------------------------+
   |Ctrl-X              |Toggle multi-line editing mode (prompt color changes  |
   |                    |while it is on).                                      |
   +--------------------+------------------------------------------------------+
   |Ctrl-Z              |Suspend and drop to shell.                            |
   +--------------------+------------------------------------------------------+
   |Ctrl-Left / Alt-B   |Move one word to the left.                            |
   +--------------------+------------------------------------------------------+
   |Ctrl-Right / Alt-F  |Move one word to the right.                           |
   +--------------------+------------------------------------------------------+
   |Alt-Backspace       |Delete previous word.                                 |
   +--------------------+------------------------------------------------------+

   Input is UTF-8: the cursor moves and deletes by whole character (including
   combining marks and emoji sequences), and double-width characters and
   color escape sequences in the prompt are accounted for when positioning
   the cursor.  Text pasted into a terminal that supports bracketed paste is
   inserted verbatim, tabs and newlines included.

File Handle Utilities
"""""""""""""""""""""

The functions `fprintf`_ (), `fseek`_\ (), `rewind`_\ (), `ftell`_\ (), `fflush`_\ (),
`fread`_\ (), `fgets`_\ (), `fwrite`_\ (), and `readLine`_\ () take a filehandle, which may be obtained
using `fopen`_\ () or `fopenBuffer`_\ ().

In addition, every handle returned by `fopen`_\ () carries the methods
`fh.fstat`_\ (), `fh.fsync`_\ (), `fh.fdatasync`_\ (), `fh.ftruncate`_\ (),
`fh.fchmod`_\ (), `fh.fchown`_\ (), `fh.fUtimes`_\ (), and `fh.fileNo`_\ () --
described in the *Handle stat/sync/truncate/chmod/chown/utimes* subsection
near the end of this section.  For raw integer-fd I/O (with ``O_EXCL``,
``pread``/``pwrite``, ``fsync``-style durability and event-loop
integration), see the separate
`POSIX File Descriptor I/O`_ section that follows.


Calling Methods:
   The above listed functions (functions which take filehandles) may be called using one of
   two alternative syntaxes.

   .. code-block:: javascript

      var handle = rampart.utils.fopen(filename, mode);

      rampart.utils.fprintf(handle, fmt, ...);

      /* or */

      handle.fprintf(fmt, ...);

   The return value for each of the file handle functions is the same for either
   syntax, with the exception that `fseek`_\ (), `rewind`_\ () and `fflush`_\ ()
   return undefined in the first syntax and ``handle`` in the second.

   Below, only the first syntax is documented.

Pre-opened file handles:
   rampart.utils.stdin:
      A handle that corresponds to the UNIX standard in stream.

   rampart.utils.stdout:
      A handle that corresponds to the UNIX standard out stream.

   rampart.utils.stderr:
      A handle that corresponds to the Unix standard error stream.

   rampart.utils.accessLog:
      A handle that corresponds to the ``accessLog`` file option in ``server.start()`` for the
      ``rampart-server`` module.  If not specified, or not loaded, same as
      ``rampart.utils.stdout``.

   rampart.utils.errorLog:
      A handle that corresponds to the ``errorLog`` file option in ``server.start()`` for the
      ``rampart-server`` module.  If not specified, or not loaded, same as
      ``rampart.utils.stderr``.

   The ``rampart.utils.stdin`` handle includes in its properties the `fread`_\ (), `fgets`_\ () and `readLine`_\ () functions
   while the other four include the `fprintf`_\ (), `fflush`_\ () and `fwrite`_\ () functions.
   Example:

   .. code-block:: javascript

      var line, inf = rampart.utils.stdin.readLine();

      while ( line = inf.next() )
         rampart.utils.stdout.fprintf("%s", line); //same as rampart.utils.printf


fopen
'''''

Open a filehandle for use with `fprintf`_\ (), `fclose`_\ (), `fseek`_\ (),
`rewind`_\ (), `ftell`_\ (), `fflush`_\ () `fread`_\ (), `fgets`_\ (), `fwrite`_\ () and
`readLine`_\ ().

Return Value:
   :green:`Object`. An object which opaquely contains the opened file handle along with
   the above functions.

Usage:

.. code-block:: javascript

   var handle = rampart.utils.fopen(filename, mode[, stdRedir]);

Where ``filename`` is a :green:`String` containing the file to be opened.

Where ``mode`` is a :green:`String` (one of the following):

*  ``"r"`` - Open text file for reading.  The stream is positioned at the
   beginning of the file.

*  ``"r+"`` - Open for reading and writing.  The stream is positioned at the
   beginning of the file.

*  ``"w"`` - Truncate file to zero length or create text file for writing.
   The stream is positioned at the beginning of the file.

*  ``"w+"`` - Open for reading and writing.  The file is created if it does
   not exist, otherwise it is truncated.  The stream is positioned at the
   beginning of the file.

*  ``"a"`` - Open for appending (writing at end of file).  The file is
   created if it does not exist.  The stream is positioned at the end of the
   file.

*  ``"a+"`` - Open for reading and appending (writing at end of file).  The
   file is created if it does not exist.  The initial file position for reading
   is at the beginning of the file, but output is always appended to the end of the
   file.

Where optional ``stdRedir`` is one of ``rampart.utils.stdin``, ``rampart.utils.stdout`` 
or ``rampart.utils.stderr`` and the mode is set appropriately (``"r"`` for stdin, ``"w"``
or ``a`` for stdout/stderr; no ``"+"`` allowed).  Any data read or written to one of the
``std`` filehandles will be redirected to the newly opened file.  When the filehandle
is closed, the ``std`` filehandle will be restored to its previous value.

fopenBuffer
'''''''''''

Open a filehandle that writes to a dynamically sized opaque buffer
for use with `fprintf`_\ (), `fclose`_\ (), `fseek`_\ (),
`rewind`_\ (), `ftell`_\ (), `fflush`_\ () `fread`_\ (), `fgets`_\ (), `fwrite`_\ () and
`readLine`_\ ().

Return Value:
   :green:`Object`. An object which opaquely contains the opened file handle along with
   the above functions.

Usage:

.. code-block:: javascript

   var handle = rampart.utils.fopenBuffer([chunkSize][, stdRedir]);

Where ``chunkSize`` is a :green:`Number` (default is ``4096``), amount of memory to allocate each
time the buffer is resized.  When the filehandle is closed, the buffer will
be sized to fit the data written, if necessary.

Where optional ``stdRedir`` is one of ``rampart.utils.stdout`` or
``rampart.utils.stderr``.  Any data written to one of the ``std``
filehandles will be redirected to the newly opened filehandle and placed in
the buffer.  When the filehandle is closed, the ``std`` filehandle will be
restored to its previous value.

Return Value:
   :green:`Object`. An object which opaquely contains the opened file handle along with
   the same functions as in ``fopen()`` above, as well as ``destroy()``, ``getBuffer()`` and ``getString()``
   functions, which will delete the backing data or copy the backing data and return the corresponding 
   JavaScript type.

Note:
   Calling ``fclose()`` will close the file handle, but the backing buffer is still available for use with
   ``getBuffer()`` and ``getString()``.  Calling ``destroy()`` will close the file handle if still open
   and delete the backing buffer.  There is no finalizer on the returned object, so it is important
   to call ``destroy()`` when it is no longer needed.  Also note that the ``fopenBuffer()`` return object can be
   used in several threads at the same time, so long as it hasn't been destroyed in any thread.  Attempting to 
   use a destroyed ``fopenBuffer()`` object will throw an error.

Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var fh = fopenBuffer(stdout);

   // prints to buffer
   printf("line 1\n");
   // also prints to buffer
   console.log("line 2");

   //stdout restored after close
   fclose(fh); // or fh.fclose()

   printf("file handle closed\n");
   // this goes to the terminal like expected
   printf("%s", fh.getString() );

   // manual cleanup is necessary
   fh.destroy();

   /* expected output"

         file handle closed
         line 1
         line 2
   */


fclose
''''''

Close a previously opened handle :green:`Object` opened with `fopen`_\ () or
`fopenBuffer`_\ ().

Example:

.. code-block:: javascript

   var handle = rampart.utils.fopen("/tmp/out.txt", "a");

   ...

   rampart.utils.fclose(handle);

     /* or */

   handle.fclose();

Return Value:
   ``undefined``.

fprintf
'''''''

Same as `printf`_\ () except output is sent to the file provided by
a :green:`String` or filehandle :green:`Object` opened and returned from `fopen`_\ ().

Usage:

.. code-block:: javascript

   var filename = "/home/user/myfile.txt";

   var output = rampart.utils.fopen(filename, mode);
   rampart.utils.fprintf(output, fmt, ...);
   rampart.utils.fclose(output);

   /* or */

   var output = filename;
   var outputLen = rampart.utils.fprintf(output, [, append], fmt, ...);
   /* file is automatically closed after function returns */

Where:

* ``output`` may be a :green:`String` (a file name), or an :green:`Object` returned from `fopen`_\ ().

* ``fmt`` is a :green:`String`, a `printf`_\ () format.

* ``append`` is an optional :green:`Boolean` only used when output is a filename- if ``true`` 
  the file will be appended instead of overwritten.

Return Value:
   A :green:`Number`. The length in bytes of the printed string.

Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var handle = fopen("/tmp/out.txt", "w+");
   fprintf(handle, "A number: %d\n", 123);
   fclose(handle);

   /* OR */

   fprintf("/tmp/out.txt", "A number: %d\n", 123); /* implicit fclose */

fseek
'''''

Set file position for file operations.

Usage:

.. code-block:: javascript

   rampart.utils.fseek(handle, offset[, whence]);

+------------+----------------+----------------------------------------------------------+
|Argument    |Type            |Description                                               |
+============+================+==========================================================+
|handle      |:green:`Object` | A handle opened with `fopen`_\ ()                        |
+------------+----------------+----------------------------------------------------------+
|offset      |:green:`Number` | offset in bytes from whence                              |
+------------+----------------+----------------------------------------------------------+
|whence      |:green:`String` | "seek_set" - measure offset from start of file (default) |
+            +                +----------------------------------------------------------+
|            |                | "seek_cur" - measure offset from current position        |
+            +                +----------------------------------------------------------+
|            |                | "seek_end" - measure offset from end of file.            |
+------------+----------------+----------------------------------------------------------+

Return Value:
   ``undefined``

Example

.. code-block:: javascript

   rampart.globalize(rampart.utils,
     ["fopen","printf","fprintf","fseek","fread"]);

   var handle = fopen("/tmp/out.txt", "w+");

   fprintf(handle, "123def");

   fseek(handle, 0, "seek_set");

   fprintf(handle, "abc");

   fseek(handle, 0, "seek_set");

   var out=fread(handle);

   printf("'%s'\n", out);
   /*
   expect output:
   'abcdef'
   */

   fclose(handle);


rewind
''''''

Set the file position to the beginning of the file.  It is equivalent to:

.. code-block:: javascript

   rampart.utils.fseek(handle, 0, "seek_set")

Usage:

.. code-block:: javascript

   rampart.utils.rewind(handle);

Return Value:
   ``undefined``

ftell
'''''

Obtain the current value of the file position for the handle opened with
`fopen`_\ ().

Usage:

.. code-block:: javascript

   var pos = rampart.utils.ftell(handle);

Return Value:
   :green:`Number`. Current position of ``handle``.


fflush
''''''

For output file handles opened with `fopen`_\ (), or for
``stdout``/``stderr``/``accessLog``/``errorLog``, ``fflush()`` forces a
write of buffered data.

Usage:

.. code-block:: javascript

    rampart.utils.fflush(handle);

Return Value:
   ``undefined``

Example:

.. code-block:: javascript

   /* normally a flush happens automatically
      when a '\n' is printed.  Since we are using
      '\r', flush manually                        */

   for (var i=0; i< 10; i++) {
      rampart.utils.printf("doing #%d\r", i);
      rampart.utils.fflush(rampart.utils.stdout);
      rampart.utils.sleep(1);
   }

   rampart.utils.printf("blast off!!!\n");

fread
'''''

Read data from a file, handle opened with `fopen`_\ () or the pre-opened handle ``stdin``.

Usage:

.. code-block:: javascript

    var data = rampart.utils.fread([handle|file] [, max_size [, chunk_size [,returnString]]]);

+------------+-----------------+---------------------------------------------------+
|Argument    |Type             |Description                                        |
+============+=================+===================================================+
|handle      |:green:`Object`  | A handle opened with `fopen`_\ ()                 |
+------------+-----------------+---------------------------------------------------+
|file        |:green:`String`  | A filename -- file will be auto opened and closed |
+------------+-----------------+---------------------------------------------------+
|max_size    |:green:`Number`  | Maximum number of bytes to read.  Unlimited if    |
|            |                 | not specified.                                    |
+------------+-----------------+---------------------------------------------------+
|chunk_size  |:green:`Number`  | Initial size of return :green:`Buffer` and number |
|            |                 | of bytes to read at a time. If the total number of|
|            |                 | bytes read is greater, the buffer grows as needed.|
|            |                 | If total bytes read is less, the returned buffer  |
|            |                 | will be reduced in size to match. Default is 4096 |
|            |                 | if not specified.                                 |
+------------+-----------------+---------------------------------------------------+
|returnString|:green:`Boolean` | Whether return value is returned as a             |
|            |                 | :green:`String`.  Default is ``false``.           |
+------------+-----------------+---------------------------------------------------+

Return Value:
    A :green:`Buffer` or a :green:`String` if ``returnString`` is ``true``.

fgets
'''''

Usage:

.. code-block:: javascript

    var data = rampart.utils.fgets([handle|file] [, options] [, max_size]);


Read data from file, up to ``max_size`` bytes (default ``1``), stopping at
and including the first ``\n`` or the end of the file.  Note that with the
default ``max_size`` a single byte is returned; to read a full line, pass a
``max_size`` larger than the longest expected line (or use `readLine`_\ ()).

If ``options`` is included, it must be an :green:`Object`, where if set to ``{"echo":false}``, and reading from
``stdin``, echoing typed characters on the terminal will be disabled (e.g., for entering
passwords).

Return Value:
    A :green:`String`, or ``null`` at end of file.

fwrite
''''''

Write data to a file, a handle opened with `fopen`_\ () or a pre-opened
output handle (``stdout``/``stderr``/``accessLog``/``errorLog``).  If using
a handle, the start of the write will be the current position based on how
the file was opened and whether any seeks have been performed.  If using a
file name, the ``append`` parameter will determine whether the file is
appended or truncated.

Usage:

.. code-block:: javascript

    var nbytes = rampart.utils.fwrite([handle|file], data [, max_bytes][, append]);

+------------+-----------------+---------------------------------------------------+
|Argument    |Type             |Description                                        |
+============+=================+===================================================+
|handle      |:green:`Object`  | A handle opened with `fopen`_\ ()                 |
+------------+-----------------+---------------------------------------------------+
|file        |:green:`String`  | A filename -- file will be auto opened and closed |
+------------+-----------------+---------------------------------------------------+
|data        |:green:`Buffer`/ | The data to be written.                           |
|            |:green:`String`  |                                                   |
+------------+-----------------+---------------------------------------------------+
|max_bytes   |:green:`Number`  | Maximum number of bytes to write. :green:`Buffer`/|
|            |                 | :green:`String` length if not specified.          |
+------------+-----------------+---------------------------------------------------+
|append      |:green:`Boolean` | If opened with ``file`` instead of ``handle``,    |
|            |                 | whether to append the file.  Default is ``false``,|
|            |                 | in which case the file will be truncated.         |
+------------+-----------------+---------------------------------------------------+

Return Value:
    A :green:`Number`. Number of bytes written.

readLine
''''''''

Read a text file line-by-line.

Usage:

.. code-block:: javascript

   var rl = rampart.utils.readLine(file);
   var line = rl.next();

Where ``file`` is a :green:`String` (name of file to be read) or a file handle opened
with `fopen`_\ () or ``rampart.utils.stdin``. It returns a :green:`Object`
that contains the property ``next`` which is :green:`Function` to retrieve and return the next
line of text in the opened file.

Return Value:
   An :green:`Object`.  Property ``next`` of the return :green:`Object` is a
   :green:`Function` which retrieves and returns the next line of text in
   the file.  After the last line of ``file`` is returned, subsequent calls
   to ``next`` will return ``null``.

Example:

.. code-block:: javascript

    var rl = rampart.utils.readLine("./myfile.txt");
    var i = 0;
    var line, firstline, lastline;

    while ( (line=rl.next()) ) {
        if(i==0)
            firstline = rampart.utils.trim(line);
        i++;
        lastline = line;
    }
    rampart.utils.printf("%s\n%s\n", firstline, lastline);

    /* expected output: first and last line of file "./myfile.txt" */


Handle stat/sync/truncate/chmod/chown/utimes
""""""""""""""""""""""""""""""""""""""""""""

The following methods are attached to every handle returned by
`fopen`_\ ().  They operate on the open file via its underlying file
descriptor and are the buffered-stdio counterpart to the raw
fd-keyed functions in the next section.  Each method is invoked as
``handle.METHOD(args)``.

fh.fstat
''''''''

Return file status for the open handle.  Equivalent to calling
`stat`_\ () on the path the handle was opened with, but works on the
underlying file descriptor -- useful when the file may have been
renamed or unlinked after opening.  Note that buffered writes are not
reflected in the reported size until `fflush`_\ () (or a close) pushes
them to the file descriptor.

Usage:

.. code-block:: javascript

   var st = handle.fstat();

Return Value:
   :green:`Object`, same shape as `stat`_\ ().

fh.fsync
''''''''

Flush stdio buffers and then call ``fsync(2)`` on the underlying file
descriptor, blocking until all modified data and metadata are written
to durable storage.  Required by the atomic write/rename/fsync pattern.

Usage:

.. code-block:: javascript

   handle.fsync();

Return Value:
   ``undefined``.  Throws on I/O error.

fh.fdatasync
''''''''''''

Like `fh.fsync`_ but only flushes file data (not metadata that doesn't
affect future reads, such as access times).  On macOS, falls back to
``fsync`` because the platform lacks ``fdatasync``.

Usage:

.. code-block:: javascript

   handle.fdatasync();

Return Value:
   ``undefined``.

fh.ftruncate
''''''''''''

Truncate or extend the open file to ``length`` bytes.  Stdio buffers are
flushed first.

Usage:

.. code-block:: javascript

   handle.ftruncate(length);

Where ``length`` is a non-negative :green:`Number`.

Return Value:
   ``undefined``.

See also `truncate`_\ () for the path-based equivalent.

fh.fchmod
'''''''''

Change the file mode bits on the open handle.  Same mode semantics as
`chmod`_\ () -- accepts a :green:`Number` or octal :green:`String`.

Usage:

.. code-block:: javascript

   handle.fchmod(mode);

Return Value:
   ``undefined``.

fh.fchown
'''''''''

Change the owning user and/or group on the open handle.  Either argument
may be a numeric id or a name string.  Negative numbers leave that side
unchanged.

Usage:

.. code-block:: javascript

   handle.fchown(user, group);

Return Value:
   ``undefined``.

fh.fUtimes
''''''''''

Set access and modification timestamps on the open handle.  Accepts
either positional ``(atime, mtime)`` arguments, where each is a
:green:`Number` (seconds since epoch) or :green:`Date`, or an options
object matching `touch`_\ ()'s style.

Usage:

.. code-block:: javascript

   handle.fUtimes(atime, mtime);

   /* or */

   handle.fUtimes({setaccess: atime, setmodify: mtime});

Return Value:
   ``undefined``.

fh.fileNo
'''''''''

Return the integer file descriptor backing the handle, for use with the
fd-keyed functions in the next section, or with system calls that take a
``fd`` argument.

Usage:

.. code-block:: javascript

   var fd = handle.fileNo();

Return Value:
   :green:`Number`.  ``-1`` if the handle is not backed by a real fd
   (rare; happens for `fopenBuffer`_\ () handles).


POSIX File Descriptor I/O
"""""""""""""""""""""""""

The functions in this section operate on raw POSIX integer file
descriptors rather than stdio handles.  They expose primitives that the
buffered `fopen`_\ () interface cannot:

* Atomic create-or-fail via ``O_EXCL`` (lock files, atomic temp files
  without TOCTOU races).
* Positional I/O (`pread`/`pwrite` equivalents) for safe multi-threaded
  access to a single file.
* Direct durability primitives (`fsyncFd`_\ (), `fdatasyncFd`_\ ()).
* Non-blocking I/O on FIFOs/sockets/pipes via ``O_NONBLOCK``.
* Integration with the rampart event loop (libevent ``event_new`` takes
  an integer fd).
* The classic "write to tmp, fsync, rename, fsync(dir)" atomic-write
  pattern for safe state-saving.

For text-oriented buffered reading/writing, `fopen`_\ () remains the
better choice.  Use these when you need durability, atomicity,
non-blocking semantics, or event-loop integration.

Open flag constants (rampart.utils.O)
'''''''''''''''''''''''''''''''''''''

The ``rampart.utils.O`` object holds integer constants for the
``flags`` parameter of `open`_\ ().  Names mirror the POSIX
``<fcntl.h>`` macros with the ``O_`` prefix dropped --
``O_RDONLY`` is ``rampart.utils.O.RDONLY``, and so on.  The short
namespace keeps call sites readable when several flags are OR'd
together (typical usage is ``u.O.WRONLY | u.O.CREAT | u.O.EXCL``).

Combine with bitwise OR.  Platform-specific constants are only
present when supported by the host.

================ ===================================================================
Constant         Meaning
================ ===================================================================
``RDONLY``       Open for reading only.
``WRONLY``       Open for writing only.
``RDWR``         Open for reading and writing.
``CREAT``        Create the file if it does not exist.
``EXCL``         With ``CREAT``, fail if the file already exists (atomic create).
``TRUNC``        If the file exists, truncate to zero length on open.
``APPEND``       All writes are appended to the end of the file.
``NONBLOCK``     Open in non-blocking mode (matters for FIFOs/sockets).
``CLOEXEC``      Close the descriptor on ``exec(3)`` -- avoids leaking fds into children.
``NOFOLLOW``     Refuse to open if the final path component is a symbolic link.
``SYNC``         Each ``write`` blocks until data + metadata reach durable storage.
``DSYNC``        Each ``write`` blocks until data (not metadata) reaches durable storage.
``DIRECTORY``    Fail unless the path resolves to a directory (when supported).
``NOCTTY``       Do not let an opened terminal device become the controlling tty.
``DIRECT``       (Linux only)  Bypass the page cache.
``NOATIME``      (Linux only)  Do not update ``atime`` on read.
``SEEK_SET``     ``lseek`` whence: from start of file (also ``0``).
``SEEK_CUR``     ``lseek`` whence: from current position (also ``1``).
``SEEK_END``     ``lseek`` whence: from end of file (also ``2``).
================ ===================================================================

open
''''

Open a file by path, returning an integer file descriptor.  Equivalent to
`open (2) <https://man7.org/linux/man-pages/man2/open.2.html>`_.

Usage:

.. code-block:: javascript

   var fd = rampart.utils.open(path, flags [, mode]);

   /* or */

   var fd = rampart.utils.open({path: path, flags: flags [, mode: mode]});

Where:

* ``path`` is a :green:`String`.

* ``flags`` is an integer bitmask built from the `Open flag constants (rampart.utils.O)`_.

* ``mode`` is an optional :green:`Number` or octal :green:`String` --
  permissions applied when ``O.CREAT`` is set and the file is newly
  created.  Defaults to ``0644``.  Modified by the process umask.

Return Value:
   :green:`Number`, the integer file descriptor.  Throws on failure.

Example:

.. code-block:: javascript

   var O = rampart.utils.O;

   /* Atomic create-or-fail (classic lock file pattern) */
   var fd = rampart.utils.open("/var/run/myapp.lock",
                               O.WRONLY|O.CREAT|O.EXCL|O.CLOEXEC, 0o600);

close
'''''

Close an integer file descriptor.

Usage:

.. code-block:: javascript

   rampart.utils.close(fd);

Return Value:
   ``undefined``.

read
''''

Read bytes from an open file descriptor.  Performs a single ``read(2)``
syscall (or ``pread`` when ``position`` is given) and returns whatever
bytes were obtained -- which may be fewer than ``length``.

Usage:

.. code-block:: javascript

   var buf = rampart.utils.read(fd, length [, position]);

Where:

* ``fd`` is the integer file descriptor.

* ``length`` is a :green:`Number`, the maximum number of bytes to read.

* ``position`` is an optional :green:`Number`.  If supplied, ``pread`` is
  used and the descriptor's offset is NOT advanced.  Useful for
  multi-threaded random-access reads of one open file.

Return Value:
   :green:`Buffer`.  Will be zero-length if EOF has been reached at the
   read position; will be shorter than ``length`` if the kernel returned
   a short read.

write
'''''

Write bytes to an open file descriptor.  Performs a single ``write(2)``
syscall (or ``pwrite`` when ``position`` is given) and returns the number
of bytes accepted -- which may be fewer than the input length on a short
write.

Usage:

.. code-block:: javascript

   var n = rampart.utils.write(fd, data [, position]);

Where:

* ``fd`` is the integer file descriptor.

* ``data`` is a :green:`String` or :green:`Buffer`.

* ``position`` is an optional :green:`Number`.  If supplied, ``pwrite`` is
  used and the descriptor's offset is NOT advanced.

Return Value:
   :green:`Number`, bytes written.  Callers performing large writes should
   loop until all bytes are accounted for.

lseek
'''''

Reposition the file offset of an open file descriptor.

Usage:

.. code-block:: javascript

   var pos = rampart.utils.lseek(fd, offset [, whence]);

Where:

* ``offset`` is a :green:`Number`, signed byte offset.

* ``whence`` is an optional :green:`String` (``"SEEK_SET"``, ``"SEEK_CUR"``,
  ``"SEEK_END"``) or the equivalent integer (``0`` / ``1`` / ``2``;
  also exposed as ``rampart.utils.O.SEEK_SET`` etc. -- see
  `Open flag constants (rampart.utils.O)`_).  Defaults to
  ``"SEEK_SET"``.

Return Value:
   :green:`Number`, the new file offset.

fstatFd
'''''''

Return file status for an open integer file descriptor.

Usage:

.. code-block:: javascript

   var st = rampart.utils.fstatFd(fd);

Return Value:
   :green:`Object`, same shape as `stat`_\ ().

fsyncFd
'''''''

Call ``fsync(2)`` on the descriptor.  Blocks until both data and metadata
reach durable storage.

Usage:

.. code-block:: javascript

   rampart.utils.fsyncFd(fd);

fdatasyncFd
'''''''''''

Like `fsyncFd`_ but only flushes data (not metadata).  On macOS, falls
back to ``fsync`` because the platform lacks ``fdatasync``.

Usage:

.. code-block:: javascript

   rampart.utils.fdatasyncFd(fd);

ftruncateFd
'''''''''''

Truncate or extend the file referenced by ``fd`` to ``length`` bytes.

Usage:

.. code-block:: javascript

   rampart.utils.ftruncateFd(fd, length);

fchmodFd
''''''''

Change permission bits on the open file descriptor.  Mode argument has
the same shape as `chmod`_\ ().

Usage:

.. code-block:: javascript

   rampart.utils.fchmodFd(fd, mode);

fchownFd
''''''''

Change owner and/or group on the open file descriptor.  Either argument
may be a name :green:`String` or a numeric id; negative numbers leave
that side unchanged.

Usage:

.. code-block:: javascript

   rampart.utils.fchownFd(fd, user, group);

futimesFd
'''''''''

Set access and modification timestamps on the open file descriptor.
Accepts either positional ``(atime, mtime)`` arguments or an options
object identical to `fh.fUtimes`_\ ()'s.

Usage:

.. code-block:: javascript

   rampart.utils.futimesFd(fd, atime, mtime);

   /* or */

   rampart.utils.futimesFd(fd, {setaccess: atime, setmodify: mtime});

Example: durable atomic write
'''''''''''''''''''''''''''''

The "write to tmp, fsync, rename" pattern -- needed whenever a partial
write would corrupt important state -- requires `open`_\ () with
``O.EXCL`` for the lock-free create and `fsyncFd`_\ () for the
durability barrier.  Neither is available through `fopen`_\ ().

.. code-block:: javascript

   var u = rampart.utils, O = u.O;

   function atomicWrite(path, data) {
      var tmp = path + ".tmp." + process.getpid();
      var fd  = u.open(tmp, O.WRONLY|O.CREAT|O.EXCL|O.TRUNC, 0o644);
      try {
         /* Loop until all bytes are written -- write() may return short. */
         var off = 0, total = data.length;
         while (off < total) {
            off += u.write(fd, data.slice ? data.slice(off) : data);
         }
         u.fsyncFd(fd);     /* the data is now durable */
      } finally {
         u.close(fd);
      }
      u.rename(tmp, path);  /* atomic on POSIX -- old or new, never partial */
   }

   atomicWrite("/etc/myapp/state.json", JSON.stringify({n: 42}));


File Watching
"""""""""""""

Watch a file or directory for changes and invoke a JavaScript
:green:`Function` when something happens.  On Linux, the kernel's
`inotify (7) <https://man7.org/linux/man-pages/man7/inotify.7.html>`_
interface is wired into rampart's libevent loop -- events arrive
synchronously without polling overhead.  Other platforms automatically
fall back to a polling backend that stats the path at a configurable
interval.

Watchers integrate into the same event loop that drives `setTimeout`,
`setInterval`, the `forkpty`_ pseudo-terminal, and (when used)
``rampart-server`` -- so callbacks fire on the current thread alongside
other JavaScript work without blocking it.

watch
'''''

Begin watching a path and return a watcher object.

Usage:

.. code-block:: javascript

   var w = rampart.utils.watch(path, callback);

   /* or */

   var w = rampart.utils.watch(path, options, callback);

   /* or */

   var w = rampart.utils.watch(options, callback);

Where:

* ``path`` is a :green:`String`, the file or directory to watch.

* ``callback`` is a :green:`Function` ``callback(event)`` invoked for each
  filesystem event.  Returning ``false`` from the callback closes the
  watcher.

* ``options`` is an optional :green:`Object`:

  * ``path`` -- :green:`String`, only meaningful when the path is being
    supplied via the options object instead of as a positional argument.

  * ``poll`` -- :green:`Boolean`, default ``false``.  If ``true``, force
    the polling backend even on Linux.  Useful for files on filesystems
    where inotify is unreliable (NFS, some FUSE mounts).

  * ``interval`` -- :green:`Number`, default ``1000``.  Polling interval
    in milliseconds.  Ignored under inotify.

The ``event`` :green:`Object` passed to the callback has the following
properties:

============ ========================================================
Property     Description
============ ========================================================
``type``     :green:`String`: ``"change"``, ``"create"``, ``"delete"``,
             or ``"rename"``.
``path``     :green:`String`: full path of the entry that changed.  For
             a directory watch, this is the path of the affected child
             (under inotify; the polling backend reports the watched
             path itself).
``isDir``    :green:`Boolean`: ``true`` if the entry is a directory.
============ ========================================================

Return Value:
   :green:`Object`, the watcher.  Properties:

   * ``path``     -- :green:`String`, the watched path.
   * ``backend``  -- :green:`String`, ``"inotify"`` or ``"polling"``.
   * ``close()``  -- :green:`Function`, stop watching (idempotent).

Example:

.. code-block:: javascript

   /* Live-reload a config file */
   var w = rampart.utils.watch("/etc/myapp/config.json", function(ev) {
      if (ev.type === "change" || ev.type === "create")
         reloadConfig();
   });

   /* Stop watching after the first event */
   var w2 = rampart.utils.watch("/tmp/marker", function(ev) {
      console.log("first event:", ev.type);
      return false;   /* equivalent to w2.close() */
   });

   /* Force the polling backend with a custom interval */
   var w3 = rampart.utils.watch({path: "/mnt/nfs/file", poll: true, interval: 500},
                                 function(ev) { ... });


Limitations
^^^^^^^^^^^

* **No recursive watching.**  Only the named path is watched; for a
  directory, only its immediate contents trigger events.  Recursion can
  be implemented in user code by walking the tree (`walkDir`_\ ()) and
  installing a watcher per directory.

* **No native kqueue backend.**  On macOS and the BSDs, the polling
  backend is used.

* **No event debouncing.**  Bursts of rapid changes produce a burst of
  callbacks; the caller should debounce in JavaScript if needed.

* **Polling fidelity.**  The polling backend compares ``mtime``, ``size``
  and ``inode`` between ticks; changes that leave all three identical
  (e.g. an in-place write of identical-size content within the same
  wall-clock second) will be missed.


Compression and Checksums
"""""""""""""""""""""""""

One-shot DEFLATE-family compression and decompression via the
`libdeflate <https://github.com/ebiggers/libdeflate>`_ library, plus the
two checksums it provides (CRC-32 and Adler-32).  All functions are
synchronous and throw on error; no streaming API is exposed (libdeflate
is one-shot only -- pass the full input as a single :green:`String` or
:green:`Buffer`).

For node-style asynchronous variants, plus the legacy stream classes
(``zlib.Deflate``, ``zlib.Gzip``, etc.), see ``zlib`` under
:ref:`rampart-nodeshim Module <rampart-extras:rampart-nodeshim Module>`.
The synchronous functions below are the implementation those wrappers
delegate to.

gzip
''''

Compress data into the gzip container format (RFC 1952).

Usage:

.. code-block:: javascript

   var compressed = rampart.utils.gzip(data [, level]);

Where:

* ``data`` is a :green:`String` or :green:`Buffer`.

* ``level`` is an optional :green:`Number` -- compression level from
  ``1`` (fastest) to ``12`` (best ratio).  Defaults to ``6``.  Values
  outside the range are clamped.

Return Value:
   :green:`Buffer` -- the gzip-compressed bytes (including the gzip
   header and footer).

Example:

.. code-block:: javascript

   var u = rampart.utils;
   var bytes = u.gzip("Hello, World!");
   u.writeFile("hello.gz", bytes);

gunzip
''''''

Decompress gzip-format data.

Usage:

.. code-block:: javascript

   var raw = rampart.utils.gunzip(data);

Where ``data`` is a :green:`String` or :green:`Buffer` containing gzip
data (must start with the magic bytes ``0x1F 0x8B``).

Return Value:
   :green:`Buffer` -- the decompressed bytes.

Throws:
   * ``"bad compressed data"`` if the input is not valid gzip.
   * If the decompressed output would exceed 256 MB (zip-bomb guard);
     compose your own loop using `inflate`_\ () if you genuinely need
     larger output.

deflate
'''''''

Compress data into the zlib container format (RFC 1950 -- a deflate
stream wrapped with a 2-byte header and a trailing Adler-32 checksum).

Usage:

.. code-block:: javascript

   var compressed = rampart.utils.deflate(data [, level]);

Where ``data`` and ``level`` are as for `gzip`_\ () above.

Return Value:
   :green:`Buffer` -- the zlib-wrapped compressed bytes.

inflate
'''''''

Decompress zlib-format data.

Usage:

.. code-block:: javascript

   var raw = rampart.utils.inflate(data);

Where ``data`` is a :green:`String` or :green:`Buffer` containing zlib
data.

Return Value:
   :green:`Buffer` -- the decompressed bytes.

Throws if the input is not valid zlib data or if the decompressed
output would exceed 256 MB.

deflateRaw
''''''''''

Compress data into raw DEFLATE format (RFC 1951 -- no header, no footer,
no checksum).  Useful when an outer protocol already provides framing
and integrity checks (e.g., PNG's IDAT, ZIP file entries).

Usage:

.. code-block:: javascript

   var compressed = rampart.utils.deflateRaw(data [, level]);

Where ``data`` and ``level`` are as for `gzip`_\ ().

Return Value:
   :green:`Buffer` -- the raw deflate bytes.

inflateRaw
''''''''''

Decompress raw DEFLATE-format data.

Usage:

.. code-block:: javascript

   var raw = rampart.utils.inflateRaw(data);

Where ``data`` is a :green:`String` or :green:`Buffer` containing raw
deflate data.

Return Value:
   :green:`Buffer` -- the decompressed bytes.

Throws on bad data or >256 MB output.

crc32
'''''

Compute the CRC-32 checksum (IEEE 802.3 polynomial -- the same used by
gzip and ZIP).

Usage:

.. code-block:: javascript

   var sum = rampart.utils.crc32(data [, seed]);

Where:

* ``data`` is a :green:`String` or :green:`Buffer`.

* ``seed`` is an optional :green:`Number`, the initial CRC-32 state.
  Defaults to ``0``.  Pass a previous CRC-32 value here to extend the
  checksum across multiple chunks of input.

Return Value:
   :green:`Number` -- the 32-bit unsigned CRC.

Example:

.. code-block:: javascript

   var u = rampart.utils;
   u.crc32("hello");                // -> 0x3610A686
   /* incremental over two chunks */
   var c = u.crc32("hel");
   c = u.crc32("lo", c);
   /* c === u.crc32("hello") */

adler32
'''''''

Compute the Adler-32 checksum (RFC 1950 -- the checksum used inside the
zlib container).  Faster than CRC-32 but with weaker collision
resistance; appropriate for accidental-corruption detection, not for
cryptographic integrity.

Usage:

.. code-block:: javascript

   var sum = rampart.utils.adler32(data [, seed]);

Where:

* ``data`` is a :green:`String` or :green:`Buffer`.

* ``seed`` is an optional :green:`Number`, the initial state.  Defaults
  to ``1`` (the standard Adler-32 starting value -- *not* ``0``).  Pass
  a previous Adler-32 value here to extend across chunks.

Return Value:
   :green:`Number` -- the 32-bit unsigned Adler-32.


Rand, Hash and HyperLogLog
""""""""""""""""""""""""""

Included in rampart.utils are several non-cryptographic functions which have been optimized for speed and ease of use.

Note that the `rand`_\ () and `hash`_\ () functions are not of cryptographic quality.  For cryptographic quality hashes
and random numbers, see :ref:`The Rampart-Crypto Module <rampart-crypto:preface>`.

rand
''''

Generate a random number using a fast, non-cryptographic random number generator.

Usage:

.. code-block:: javascript

   var rn = rampart.utils.rand([min, max]);

      /* or */

   var rn = rampart.utils.rand(max);

Where ``min`` is the floor and ``max``
is the ceiling (EXCLUSIVE) of the range of the random number to produce.
If not provided, ``min`` and ``max`` default to ``0.0`` and
``1.0`` respectively.

Return Value:
   A :green:`Number` - the generated random number.

Note that if srand has not been called before use, the random number generator
will be automatically seeded.

irand
'''''

Generate a random integer using a fast, non-cryptographic random number generator.

Usage:

.. code-block:: javascript

   var rn = rampart.utils.irand([min, max]);

      /* or */

   var rn = rampart.utils.irand(max);

      /* or */

   rampart.utils.irand([max[min,max]],callback);

Where ``min`` is the floor and ``max``
is the ceiling (INCLUSIVE) of the range of the random integers to produce.
If not provided, ``min`` and ``max`` default to ``0`` and
``99`` respectively.

If provided, ``callback`` is a :green:`Function` ``callback(r,i)`` where
``r`` is the random integer and ``i`` is the loop count. The :green:`Function`
will be called repeatedly until it returns ``false``.

Return Value:
   A :green:`Number` - the generated random integer as a number. If
   a function is provided, returns ``undefined``.

Note that if `srand`_ has not been called before use, the random number generator
will be automatically seeded.

Note also because of JavaScript :green:`Number` precision, the maximum and
minimum ``max`` or ``min`` that may be provided is ``9007199254740991`` and
``-9007199254740991`` respectively.

gaussrand
'''''''''

The ``gaussrand([sigma])`` function returns a random :green:`Number` using a
fast, non-cryptographic random number generator and based on
a normal distribution centered at zero (``0.0``), where ``sigma`` is one
standard deviation.  ``sigma`` is optional, defaulting to ``1.0``.

normrand
''''''''

The ``normrand([scale])`` function returns a random :green:`Number` using a
fast, non-cryptographic random number generator and based on
a normal distribution centered at zero (``0.0``) and clamped between ``-scale``
and ``scale``.

Similar to the `gaussrand`_ above.  It is equivalent to:

.. code-block:: javascript

    var nrand = scale * rampart.utils.gaussrand(1.0)/5.0;

    if(nrand>scale)
        nrand=scale;
    else if (nrand < -scale)
        nrand = -scale;


With a ``scale`` of ``1.0`` (the default), the distribution of numbers has a
standard deviation of ``0.2``.


srand
'''''

Seed the random number generator for use with the random functions above.

Usage:

.. code-block:: javascript

   rampart.utils.srand([random_num]);

Where ``random_num`` is an optional number to seed the random number generator.  If not specified, a number will
be derived by reading ``/dev/urandom``.

hash
''''

Calculate the hash of data.

Usage:

.. code-block:: javascript

   var myhash = rampart.utils.hash(data,options);

Where ``data`` is the data from which the hash is calculated and options is
an :green:`Object` with the following optional properties:

* ``type`` - the type of hash to be calculated. A :green:`String`, one of:

   * ``"murmur"`` - A 64 bit hash using the `murmur` algorithm.

   * ``"city"`` - A 64 bit hash using the `city` algorithm.

   * ``"city128"`` - A 128 bit hash using the `city` algorithm.  This is the default if not specified.

   * ``"both"`` - A 192 bit hash -- the ``city128`` hash concatenated with the ``murmur`` hash.

* ``function`` - Alias for ``type``.

* ``returnBuffer`` - a :green:`Boolean`, if ``true``, the hash will be returned as the binary value of the hash
  in a a :green:`Buffer`.  If ``false`` (the default), the return value will be a :green:`String` - a hex encoded representation
  of the hash.

Return Value:
   A :green:`String` or :green:`Buffer` - the computed hash.

hll
'''

The ``hll`` function calculates a count of unique items based on Rampart's own
`hyperloglog <https://en.wikipedia.org/wiki/HyperLogLog>`_ algorithm. It allocates and uses
a 16384 byte buffer to calculate a distinct count of items added.

Usage:

.. code-block:: javascript

   var myhll = new rampart.utils.hll(name);

      /* or */

   var myhll = new rampart.utils.hll(name, hllBufferData);

      /* or */

   var myhll = new rampart.utils.hll(name [, hllBufferData], merge_hll1 [, merge_hll2, ...]);

Where:

* ``name`` is an arbitrary :green:`String`.  It may be called again with the same ``name``
  in order to retrieve the same `hll` :green:`Object`.

* ``hllBufferData`` is a :green:`Buffer` - The raw `hll` buffer to initialize the new
  `hll` :green:`Object` with data previously extracted using
  :ref:`getBuffer <rampart-utils:hll.getBuffer>` below.

* ``merge_hll1``, ``merge_hll2``, etc. are `hll` :green:`Objects` created with ``new rampart.utils.hll(name)``
  to be merged into the new (blank) return `hll` :green:`Object` in the same manner as
  :ref:`merge <rampart-utils:hll.merge>` below.

Return Value:
   An opaque `hll` :green:`Object` containing the following functions: ``add``, ``addFile``, ``count``, ``merge``,
   and ``getBuffer``.

Note that an `hll` can be referred to from different threads in the
:ref:`Rampart Server <rampart-server:The rampart-server HTTP module>` or inside :ref:`Rampart threads <rampart-thread:Rampart Thread Functions>`. Each
thread may specify the same `hll` by using the same name.  In addition, the below
functions are thread-safe.


hll.add
'''''''

Add a value or values to the `hll`_\ .

Usage:

.. code-block:: javascript

   var myhll = new rampart.utils.hll(name);

   myhll.add(value);

Where ``value`` is a :green:`String`, :green:`Buffer` or an array of :green:`Strings` and/or :green:`Buffers`.
A :green:`Buffer` value would typically come from, e.g., `readFile`_ or
`stringToBuffer`_\ .

Return Value:
   The `hll` :green:`Object`.

hll.addFile
'''''''''''

Add values to the `hll`_ from a file, with each value on a separate line.

.. code-block:: javascript

   var myhll = new rampart.utils.hll(name);

   myhll.addFile(file [, delim] );

Where
   * ``file`` is a :green:`String` (name of file to be read) or a file handle opened
     with with `fopen`_\ () or ``rampart.utils.stdin``.

   * ``delim`` is an optional :green:`String`, the first character of which is used
     as a line separator.  The default value is ``"\n"``.

Return Value:
   The `hll` :green:`Object`.

hll.count
'''''''''

Get a current estimate count of distinct items added to the `hll`_\ .

Usage:

.. code-block:: javascript

   var myhll = new rampart.utils.hll(name);

   /* add items */
   ...

   var mycount = myhll.count();


Return Value:
   A :green:`Number`, the estimated number of distinct items added to the `hll`_\ .

hll.merge
'''''''''

Merge one or more `hll` files into the current `hll` in order to calculate an estimate of the number of distinct
items of the union.

Usage:

.. code-block:: javascript

   var mergedHll = myhll.merge(myhll2 [, myhll3, ...]);


Where ``myhll2``, ``myhll3``, etc. are `hlls` created with ``new rampart.utils.hll`` above.

Return Value:
   The `hll` :green:`Object` merged and updated with the provided `hlls`.

hll.getBuffer
'''''''''''''

Get the raw `hll` buffer as a JavaScript :green:`Buffer`, which may be used to save
the `hll` to disk using a command such as `fwrite`_\ () above.

Usage:

.. code-block:: javascript

   var myhll = new rampart.utils.hll(name);

   /* add items */
   ...

   var hllbuf = myhll.getBuffer();

Return Value:
   A :green:`Buffer` 16384 bytes in length.

