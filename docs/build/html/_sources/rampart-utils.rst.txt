rampart.utils
"""""""""""""

Utility functions are provided by the global ``rampart.utils`` :green:`Object`.
These functions bring file io and other functionality to Duktape JavaScript.

`fprintf`_ (), `fseek`_\ (), `rewind`_\ (), `ftell`_\ (), `fflush`_\ (),
`fread`_\ () and `fwrite`_\ () take a filehandle, which may be obtained
using `fopen`_\ (), or by using one of the following:

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

   * ``%U`` - url encode (or if ``!`` flag present, decode) a :green:`String`. 

   * ``%H`` - html encode (or if ``!`` flag present, decode) a :green:`String`. 

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

Example:

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
    "</html>\n"+

    /* remove leading white space */
    rampart.utils.printf("%!-.1000P", html);

    /* expected output
    <html>
    <body>
    <div>
    content
    </div>
    </body>
    </html>
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

fopen
'''''

Open a filehandle for use with `fprintf`_\ (), `fclose`_\ (), `fseek`_\ (),
`rewind`_\ (), `ftell`_\ (), `fflush`_\ () `fread`_\ () and `fwrite`_\ ().

Return Value:
   :green:`Object`. The opened filehandle.

Usage:

.. code-block:: javascript

   var handle = rampart.utils.fopen(filename, mode);

Where ``filename`` is a :green:`String` containing the file to be opened and mode is
a :green:`String` (one of the following):

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

fclose
''''''

Close a previously opened handle :green:`Object` opened with `fopen`_\ ().

Example:

.. code-block:: javascript

   var handle = rampart.utils.fopen("/tmp/out.txt", "a");
   ...
   rampart.utils.fclose(handle);

fprintf
'''''''

Same as ``printf()`` except output is sent to the file provided by
a :green:`String` or filehandle :green:`Object` opened and returned from `fopen`_\ ().

Usage:

.. code-block:: javascript

   var filename = "/home/user/myfile.txt";

   var output = rampart.utils.fopen(filename, mode);
   rampart.utils.fprintf(output, fmt, ...);
   rampart.utils.fclose(output);

   /* or */

   var output = filename;
   rampart.utils.fprintf(output, [, append], fmt, ...); 
   /* file is automatically closed after function returns */
   
Where:

* ``output`` may be a :green:`String` (a filename), or an :green:`Object` returned from `fopen`_\ ().

* ``fmt`` is a :green:`String`, a `printf`_\ () format.

* ``append`` is an optional :green:`Boolean` - if ``true`` append instead of
  overwrite an existing file.

Return Value:
   :green:`Number`. The length in bytes of the printed string.

Example:

.. code-block:: javascript

   var handle = fopen("/tmp/out.txt", "w+");
   fprintf(handle, "A number: %d\n", 123);
   fclose(handle);

   /* OR */

   fprintf("/tmp/out.txt", "A number: %d\n", 456); /* implicit fclose */

fseek
'''''

Set file position for file operations.

Usage:

.. code-block:: javascript

   rampart.utils.fseek(handle, offset, whence);

+------------+----------------+---------------------------------------------------+
|Argument    |Type            |Description                                        |
+============+================+===================================================+
|handle      |:green:`Object` | A handle opened with `fopen`_\ ()                 |
+------------+----------------+---------------------------------------------------+
|offset      |:green:`Number` | offset in bytes from whence                       |
+------------+----------------+---------------------------------------------------+
|whence      |:green:`String` | "seek_set" - measure offset from start of file    |
+            +                +---------------------------------------------------+
|            |                | "seek_cur" - measure offset from current position |
+            +                +---------------------------------------------------+
|            |                | "seek_end" - measure offset from end of file.     |
+------------+----------------+---------------------------------------------------+

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

   printf("%s", out);
   /* expect output: "abcdef" */

   fclose(handle);


rewind
''''''

Set the file position to the beginning of the file.  It is equivalent to:

.. code-block:: javascript

   fseek(handle, 0, "seek_set")

Usage:

.. code-block:: javascript

   rewind(handle);

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

Read data from a handle opened with `fopen`_\ () or ``stdin``.

Usage:

.. code-block:: javascript

    rampart.utils.fread(handle [, chunk_size [, max_size]]);

+------------+-----------------+---------------------------------------------------+
|Argument    |Type             |Description                                        |
+============+=================+===================================================+
|handle      |:green:`Object`  | A handle opened with `fopen`_\ ()                 |
+------------+-----------------+---------------------------------------------------+
|chunk_size  |:green:`Number`  | Initial size of return :green:`Buffer` and number |
|            |                 | of bytes to read at a time. If the total number of|
|            |                 | bytes read is greater, the buffer grows as needed.|
|            |                 | If total bytes read is less, the returned buffer  |
|            |                 | will be reduced in size to match. Default is 4096 |
|            |                 | if not specified.                                 |
+------------+-----------------+---------------------------------------------------+
|max_size    |:green:`Number`  | Maximum number of bytes to read.  Unlimited if    |
|            |                 | not specified.                                    |
+------------+-----------------+---------------------------------------------------+

Return Value:
    :green:`Buffer`. Contents set to the read bytes.

fwrite
''''''

Write data to handle opened with `fopen`_\ () or ``stdout``/``stderr``.

Usage:

.. code-block:: javascript

    var nbytes = rampart.utils.fwrite(handle, data [, max_bytes]);

+------------+-----------------+---------------------------------------------------+
|Argument    |Type             |Description                                        |
+============+=================+===================================================+
|handle      |:green:`Object`  | A handle opened with `fopen`_\ ()                 |
+------------+-----------------+---------------------------------------------------+
|data        |:green:`Buffer`/ | The data to be written.                           |
|            |:green:`String`  |                                                   |
+------------+-----------------+---------------------------------------------------+
|max_bytes   |:green:`Number`  | Maximum number of bytes to write. :green:`Buffer`/|
|            |                 | :green:`String` length if not specified.          |
+------------+-----------------+---------------------------------------------------+

Return Value:
    :green:`Number`. Number of bytes written.


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

stringToBuffer
''''''''''''''

Performs a byte-for-byte copy of :green:`String` into a :green:`Buffer`.  
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

See `Duktape documentation <https://wiki.duktape.org/howtobuffers2x>`_ for
more information on different types of :green:`Buffers`.

Return Value:
   :green:`Buffer`.  Contents of :green:`String`/:green:`Buffer` copied to a new :green:`Buffer` :green:`Object`.

bufferToString
''''''''''''''

Performs a 1:1 copy of the contents of a :green:`Buffer` to a :green:`String`.

See ``duk_buffer_to_string()`` in the
`Duktape documentation <https://wiki.duktape.org/howtobuffers2x#buffer-to-string-conversion>`_

Usage:

.. code-block:: javascript

   var str = rampart.utils.bufferToString(data);

Where data is a :green:`Buffer` :green:`Object`.

Return Value:
   :green:`String`.  Contents of :green:`Buffer` copied to a new :green:`String`.

objectToQuery
'''''''''''''

Convert an :green:`Object` of key/value pairs to a :green:`String` suitable for use as a query
string in an HTTP request.

Usage:

.. code-block:: javascript

   var qs = rampart.utils.objectToQuery(kvObj [, arrayOpt]);

Where ``kvObj`` is an :green:`Object` containing the key/value pairs and ``arrayOpt``
controls how :green:`Array` values are treated, and is
one of the following:

   * ``repeat`` - default value if not specified.  Repeat the key in the
     query string with each value from the array.  Example:
     ``{key1: ["val1", "val2"]}`` becomes ``key1=val1&key1=val2``.

   * ``bracket`` - similar to repeat, except url encoded ``[]`` is appended
     to the keys.  Example: ``{key1: ["val1", "val2"]}`` becomes
     ``key1%5B%5D=val1&key1%5B%5D=val2``.

   * ``comma`` - One key with corresponding values separated by a ``,``
     (comma).  Example: ``{key1: ["val1", "val2"]}`` becomes
     ``key1=val1,val2``.

   * ``json`` - encode array as JSON.  Example: 
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

*  All primitive values will be converted to :green:`Strings`.

*  If ``repeat`` or ``bracket`` was used to create the 
   query string, all values will be returned as strings (even if an :green:`Array` of
   :green:`Numbers` was given to `objectToQuery`_\ ().

*  If ``comma`` was used to create the query string, no separation of comma
   separated values will occur and the entire value will be returned as a :green:`String`.

*  If ``json`` was used, numeric values will be preserved as :green:`Numbers`.

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
       rampart.utils.printf("qToO(\n     '%s'\n    ) = \n%s\n", qs, JSON.stringify(qsobj,null,3));
   } 

   /* expected output:
   qToO(
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
   qToO(

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
   qToO(
        'key1=null&key2=1,2,3&key3=val1,val2'
       ) = 
   {
      "key1": "null",
      "key2": "1,2,3",
      "key3": "val1,val2"
   }
   qToO(
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


readFile
''''''''

Read the contents of a file.

Usage:

.. code-block:: javascript

   var contents = rampart.utils.readFile({
      file: filename
      [, offset: offsetPos]
      [, length: rLength]
      [, retString: return_str]
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
      filename:  "/tmp/file.txt",
      offset:    10, 
      length:    -6, 
      retString: true
   });

   /* or var txt = rampart.utils.readFile("/tmp/file.txt", 10, -6, true); */

   rampart.utils.printf("'%s'\n", txt);

   /* expected output:
   'text'
   */


trim
''''

Remove whitespace characters from beginning and end of a :green:`String`.

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

readLine
''''''''

Read a text file line-by-line.

Usage:

.. code-block:: javascript

   var rl = rampart.utils.readLine(file);
   var line=rl.next();

Where ``file`` is a :green:`String` (name of file to be read) and return :green:`Object`
contains the property ``next``, a :green:`Function` to retrieve and return the next
line of text in the file.

Return Value:
   :green:`Object`.  Property ``next`` of the return :green:`Object` is a
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

stat
''''

Return information on a file.

Usage:

.. code-block:: javascript

   var st = stat(file);

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
      "isBlockDevice":     Boolean,
      "isCharacterDevice": Boolean,
      "isDirectory":       Boolean,
      "isFIFO":            Boolean,
      "isFile":            Boolean,
      "isSocket":          Boolean
   }

See `stat (2) <https://man7.org/linux/man-pages/man2/stat.2.html>`_ for the
meaning of each property.  The ``is*()`` functions return ``true`` if the
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
   Same as `stat`_\ () with the addition of the property/function
   ``isSymbolicLink()`` to test whether the file is a symbolic link.

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

   *  ``timeout`` - :green:`Number`: Maximum amount of time in milliseconds before
      the process is automatically killed.

   *  ``killSignal`` - :green:`Number`. If timeout is reached, use this signal 

   *  ``background`` - :green:`Boolean`.  Whether to execute detached and return
      immediately.  ``stdout`` and ``stderr`` below will be set to ``null``.  Any ``timeout`` 
      value is ignored.

   *  ``stdin`` - :green:`String` or :green:`Buffer`. If specified, the content is piped to the 
      command as stdin.

*  ``argn`` - :green:`String`/:green:`Number`/:green:`Object`/:green:`Boolean`/:green:`Null` - Arguments to be passed to
   ``command``.  Non-Strings are converted to a :green:`String` (e.g. "true", "null",
   "42" or for :green:`Object`, the equivalent of ``JSON.stringify(obj)``).

Return Value:
   :green:`Object`.  Properties as follows:

   * ``stdout`` - :green:`String`. Output of command if ``background`` is not set ``false``. 
     Otherwise ``null``.

   * ``stderr`` - :green:`String`. stderr output of command if ``background`` is not set ``false``.
     Otherwise ``null``.

   * ``exitStatus`` - :green:`Number`.  The returned exit status of the command.

   * ``timedOut`` - :green:`Boolean`.  Set true if the program was killed after
     ``timeout`` milliseconds has elapsed.

   * ``pid`` - :green:`Number`. Process id of the executed command.

shell
'''''

Execute :green:`String` in a bash shell. Equivalent to 
``rampart.utils.exec("bash", "-c", shellcmd);``.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.shell(shellcmd[, options]);

Where ``shellcmd`` is a :green:`String` containing the command and arguments to be
passed to bash and ``options`` are the same as specified for `exec`_\ .

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

kill
''''

Terminate a process or send a signal.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.kill(pid [, signal]);

Where ``pid`` is a :green:`Number`, the process id of process to be sent a signal and
``signal`` is a :green:`Number`, the signal to send.  If ``signal`` is not specified,
``15`` (``SIGTERM``) is used.  See manual page for kill(1) for a list of
signals, which may vary by platform.  Setting ``signal`` to ``0`` sends no
signal, but checks for the existence of the process identified by ``pid``.

Return Value:
   :green:`Boolean`.  ``true`` if the signal was successfully sent.  ``false`` if there was
   an error or process does not exist.

Example:

.. code-block:: javascript

   var ret = rampart.utils.exec("sleep", "100", {background:true});
   var pid=ret.pid;

   if (rampart.utils.kill(pid,0)) {
       console.log("process is still running");
       rampart.utils.kill(pid);
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
number), but ``755``, as a decimal number may not work as intended.



Return Value:
   ``undefined``.

rmdir
'''''

Remove an empty directory.

Usage:

.. code-block:: javascript

   rampart.utils.rmdir(path [, recurse]);

Where ``path`` is a :green:`String`, the directory to be removed and ``recurse`` is an
optional :green:`Boolean`, which if ``true``, parent directories explicitly present in
``path`` will also be removed.  Throws an error if the directory cannot be
removed (.e.g., not empty or lacking permission).

Return Value:
   ``undefined``.

Example:

.. code-block:: javascript

   /* make the following directories in the 
      current working directory             */
   rampart.utils.mkdir("p1/p2/p3",0755);

   /* remove the directories recursively */
   rampart.utils.rmdir("p1/p2/p3", true);



readdir
'''''''

Get listing of directory files.

Usage:

.. code-block:: javascript

   var files = rampart.utils.readdir(path [, showhidden]);

Where ``path`` is a :green:`String`, the directory whose content will be listed and
``showhidden`` is a :green:`Boolean`, which if ``true``, files or directories
beginning with ``.`` (hidden files) will be included in the return value.

Return Value: 
   :green:`Array`.  An :green:`Array` of :green:`Strings`, each filename in the directory.


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

Return Value:
   ``undefined``.

rmFile
''''''

Delete a file.

Usage:

.. code-block:: javascript

   rampart.utils.rmFile(filename);

Where ``filename`` is a :green:`String`, the name of the file to be removed.

Return Value:
   ``undefined``.

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

chmod
'''''

Change the file mode bits of a file or directory.

Usage:

.. code-block:: javascript

   rampart.utils.chmod(path [, mode]);

Where ``path`` is a :green:`String`, the file or directory upon which to be operated
and ``mode`` is a :green:`Number` or :green:`String`, the octal permissions mode.  Any parent
directories which do not exist will also be created.  Throws error if
lacking permissions or if another error was encountered.

Note that ``mode`` is normally given as an octal.  As such it can be, e.g.,
``0755`` (octal number) or ``"755"`` (:green:`String` representation of an octal
number), but ``755``, as a decimal number may not work as intended.

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
   A green:`String`, the canonical form of the path.

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

* ``setAccess`` is a :green:`Boolean` (default ``true``).  Whether to update
  access timestamp of file.

* ``setModify`` is a :green:`Boolean` (default ``true``).  Whether to update
  modification timestamp of file.

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

getType
'''''''

Get the type of variable. A simplified but more specific version of
``typeof``.

Usage:

.. code-block:: javascript

    var type = rampart.utils.getType(myvar);

Return Value:
  A :green:`String`, one of ``String``, ``Array``, ``Number``, ``Function``,
  ``Boolean``, ``Buffer`` (any buffer type), ``Nan``, ``Null``, ``Undefined``,
  ``Date`` or ``Object``.
