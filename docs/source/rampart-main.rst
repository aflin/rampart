Introduction to Rampart
-----------------------

Preface
~~~~~~~

Acknowledgement
"""""""""""""""

Rampart uses the `Duktape Javascript Engine <https://duktape.org>`_. Duktape is an 
embeddable JavaScript engine, with a focus on portability and compact footprint.
The developers of Rampart are extremely grateful for the excellent api and
ease of use of this library.

License
"""""""
Duktape and the Core Rampart program are MIT licensed.


What does it do?
""""""""""""""""
Rampart uses a low memory footprint JavaScript interpreter to bring together
several high performance tools and useful utilities for use in Web
and information management applications.  At its core is the Duktape
javascript library and added to it is a SQL database, full text search
engine, a fast and innovative NOSQL Mmap database, a fast multithreaded 
webserver, client functionality via the Curl and crypto functions via
Openssl.  It attempts to provide performance, maximum flexibility and 
ease of use through the marriage of C code and JavaScript scripting.



Features
~~~~~~~~

Core features of Duktape
""""""""""""""""""""""""

A partial list of Duktape features:

* Partial support for ECMAScript 2015 (E6) and ECMAScript 2016 (E7), see Post-ES5 feature status and kangax/compat-table
* ES2015 TypedArray and Node.js :green:`Buffer` bindings
* CBOR bindings
* Encoding API bindings based on the WHATWG Encoding Living Standard
* performance.now()
* Built-in regular expression engine
* Built-in Unicode support
* Combined reference counting and mark-and-sweep garbage collection with finalization
* Property virtualization using a subset of ECMAScript ES2015 Proxy object
* Bytecode dump/load for caching compiled functions

See full list `Here <https://duktape.org>`_

Rampart additions
"""""""""""""""""

In addition to the standard features in Duktape JavaScript, Rampart adds the
following:

* Standard module support for ``C`` and ``Javascript`` modules via the
  ``require()`` function.

* File and C-functions utilities such as ``printf``, ``fseek``, and ``exec``.

* Included ``C`` modules (``rampart-ramis``, ``rampart-curl``, ``rampart-sql``, ``rampart-server`` and
  ``rampart-crypto``).

* Minimal event loop using ``libevent2``

* ECMA 2015, 2016, 2017 and 2018 support using the `Babel <https://babeljs.io/>`_
  JavaScript transpiler.

* Full Text Search and SQL databasing via ``rampart-sql``.

* Fast NOSQL database via ``rampart-ramis``.

* Multithreaded http(s) server from libevhtp via ``rampart-server``.

* http, ftp, etc. client functionality via ``rampart-curl``.

* Cryptography functions from Openssl via ``rampart-crypto``.

Rampart philosophy
~~~~~~~~~~~~~~~~~~
Though Rampart supports ``setTimeout()`` (and other async functions via
babel), the functions added to `Duktape <https://duktape.org>`_ 
via modules as well as the built-in functions are syncronous.  Raw javascript
execution is more memory efficient, but far slower than with, e.g., node.js.
However, the functionality and speed of the available C functions provide
comparable efficacy and are a viable alternative to 
`LAMP <https://en.wikipedia.org/wiki/LAMP_(software_bundle)>`_, 
`MEAN <https://en.wikipedia.org/wiki/MEAN_(solution_stack)>`_ or other
stacks, all in a single product, while consuming considerably less resources
than the aforementioned.

Rampart Global Variable and Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two global variables beyond what is provided by Duktape:
``rampart`` and ``process``.

rampart.globalize
"""""""""""""""""

Put all or named properties of an :green:`Object` in the global namespace.  

.. code-block:: javascript

    rampart.globalize(var_obj [, prop_names]);

+------------+----------------+---------------------------------------------------+
|Argument    |Type            |Description                                        |
+============+================+===================================================+
|var_obj     |:green:`Object` | The path to the directory containing the database |
+------------+----------------+---------------------------------------------------+
|prop_names  |:green:`Array`  | optional :green:`Array` of property names to be   |
|            |                | put into the global namespace.  If specified, only|
|            |                | the named properties will be exported.            |
+------------+----------------+---------------------------------------------------+


Return value: 
   ``undefined``.

Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);
   printf("rampart.utils.* are now global vars!\n");

   /* or */

  rampart.globalize(rampart.utils, ["printf"]);
  printf("only printf is a global var\n");

rampart.utils
"""""""""""""

Utility functions are provided by the global ``rampart.utils`` :green:`Object`.
These functions bring file io and other functionality to Duktape javascript.

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
   :green:`Number`. The length in characters of the printed string.

Standard formats:  Most of the normal flags and formats are respected.
See standard formats and flags from
`printf(3) <https://man7.org/linux/man-pages/man3/printf.3.html>`_.

Extended (non-standard) formats:

   * ``%s`` - corresponding argument is treated as a :green:`String`
     (converted/coerced if necessary).

   * ``%S`` - same as ``%s`` except an error is thrown if corresponding argument is
     not a :green:`String`.

   * ``%J`` - print :green:`Object` as JSON.  An optional width (i.e.
     ``printf("%4J", obj);``) may be given which will print with new lines and 
     indentation of the specified amount (equivalent to 
     ``printf("%s", JSON.stringify(obj, null, 4) );``). 

   * ``%B`` - print contents of a :green:`Buffer` as is.

   * ``%U`` - url encode (or if ``!`` flag present, decode) a :green:`String`. 

Example:

.. code-block:: javascript

   var uenc = "a+url+encoded+string.+%27%23%24%3f%27";
   rampart.utils.printf("Encoded: %s\nDecoded: %!U\n", uenc, uenc);
   /* expected output:
   Encoded: a+url+encoded+string.+%27%23%24%3f%27
   Decoded: a url encoded string. '#$?'
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
   :green:`Number`. The length in characters of the printed string.

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
|            |                | "seek_cur" - measure offsef from current position |
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

   printf("%B", out);
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
|            |                 | of bytes to read at a time. If total number of    |
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

    var nbytes = rampart.utils.frwrite(handle, data [, max_bytes]);

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

   var data = rampart.utils.hexify(hexstring);

Return Value:
   :green:`Buffer`.  Each two character hex representation converted to a
   byte in the binary string.


Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var s=sprintf("%c%c%c%c",0xF0, 0x9F, 0x98, 0x8A);

   printf("0x%s\n", hexify(s) );
   printf("%B\n", dehexify(hexify(s)) );

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

*  If ``json`` was used, numeric values will be preserves as :green:`Numbers`.

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


Where ``optsObj`` is an :green:`Object` with the key ``filename`` and optional keys
``offset``, ``length`` and/or ``retString``.


+------------+-----------------+-----------------------------------------------------+
|Argument    |Type             |Description                                          |
+============+=================+=====================================================+
|filename    |:green:`String`  | Path to the file to be read                         |
+------------+-----------------+-----------------------------------------------------+
|offsetPos   |:green:`Number`  | If positive, offset from beginning of file          |
|            |                 +-----------------------------------------------------+
|            |                 | If negative, offset from end of file                |
+------------+-----------------+-----------------------------------------------------+
|rLength     |:green:`Number`  | If greater than zero, amount in bytes to be read.   |
|            |                 +-----------------------------------------------------+
|            |                 |Otherwise, position from end of file to stop reading.|
+------------+-----------------+-----------------------------------------------------+
|return_str  |:green:`Boolean` | If not set, or ``false``, return a :green:`Buffer`. |
|            |                 +-----------------------------------------------------+
|            |                 | If ``true``, return contents as a :green:`String`.  |
|            |                 | May be truncated if file contains null characters.  |
+------------+-----------------+-----------------------------------------------------+

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
   rampart.utils.printf("'%s'\n", rampart.utils.trim(str));
   /* expected output:
   'a line of text'
   */

readLine
''''''''

Read a text file line-by-line.

Usage:

.. code-block:: javascript

   var rl = readLine(file);
   var line=rl.next();

Where ``file`` is a :green:`String` (name of file to be read) and return :green:`Object`
contains the property ``next``, a function to retrieve and return the next
line of text in the file.

Return Value:
   :green:`Object`.  Property ``next`` of the return :green:`Object` is a function which
   retrieves and returns the next line of text in the file.  After the last
   line of ``file`` is returned, subsequent calls to ``next`` will return
   ``null``.

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
      "dev":     Number,
      "ino":     Number,
      "mode":    Number,
      "nlink":   Number,
      "uid":     Number,
      "gid":     Number,
      "rdev":    Number,
      "size":    Number,
      "blksize": Number,
      "blocks":  Number,
      "atime":   Date,
      "mtime":   Date,
      "ctime":   Date
      "isBlockDevice":     function,
      "isCharacterDevice": function,
      "isDirectory":       function,
      "isFIFO":            function,
      "isFile":            function,
      "isSocket":          function

   }

See `stat (2) <https://man7.org/linux/man-pages/man2/stat.2.html>`_ for
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
      immediately.  ``stdout`` and ``stderr`` below will be set to ``null``.

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

   * ``timedOut`` - :green:`Boolean`.  Set true if program was killed after
     ``timeout`` milliseconds elapsed.

   * ``pid`` - :green:`Number`. Process id of the executed command.

shell
'''''

Execute :green:`String` in a bash shell. Equivalent to 
``rampart.utils.exec("bash", "-c", shellcmd);``.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.shell(shellcmd);

Where ``shellcmd`` is a :green:`String` containing the command and arguments to be
passed to bash.

Return Value:
   Same as `exec`_\ ().

Example:

.. code-block:: javascript

   var ret = rampart.utils.shell('echo -n "hello"'); 
   console.log(JSON.stringify(ret, null, 3)); 

   /* expected output:
   {
      "stdout": "hello",
      "stderr": "",
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
   :green:`Boolean`.  ``true`` if signal successfully sent.  ``false`` if there was
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


mkdir
'''''

Create a directory.

Usage:

.. code-block:: javascript

   rampart.utils.mkdir(path [, mode]);

Where ``path`` is a :green:`String`, the directory to be created and ``mode`` is a
:green:`Number` or :green:`String`, the octal permissions mode. Any parent directories which
do not exist will also be created.  Throws error if lacking permissions or
if another error encountered.

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
optional :green:`Boolean`, which if ``true``, parent directories explicitely present in
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
lacking permissions or if another error encountered.

Note that ``mode`` is normally given as an octal.  As such it can be, e.g.,
``0755`` (octal number) or ``"755"`` (:green:`String` representation of an octal
number), but ``755``, as a decimal number may not work as intended.

Return Value: 
   ``undefined``.

touch
'''''

Create an empty file, or update the access time stamp of an existing file.

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
  will only update the time stamp, and will no create non-existing
  ``file``.

* ``setAccess`` is a :green:`Boolean` (default ``true``).  Whether to update
  access time stamp of file.

* ``setModify`` is a :green:`Boolean` (default ``true``).  Whether to update
  modification time stamp of file.

* ``referenceFile`` is a :green:`String`.  If specified, the named file's access and
  modification time stamps will be used rather than the current time/date.

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

getpid
''''''

Get the process id of the current process.

Usage:

.. code-block:: javascript

   var pid = rampart.utils.getpid();

Return Value:
   :green:`Number`. The pid of the current process.

getppid
'''''''

Get the process id of the parent of the current process.

Usage:

.. code-block:: javascript

   var ppid = rampart.utils.getppid();

Return Value:
   :green:`Number`. The pid of the parent process.

rampart.import
""""""""""""""

csvFile
'''''''

The csvFile :green:`Function` imports csv data from a file.  It takes a 
:green:`String` containing a file name and optionally
an :green:`Object` of options and/or a callback
:green:`Function`.  The parameters may be specified in any order.

Usage: 

.. code-block:: javascript

    var res = rampart.import.csvFile(filename [, options] [, callback]);

+--------------+------------------+---------------------------------------------------+
|Argument      |Type              |Description                                        |
+==============+==================+===================================================+
|filename      |:green:`String`   | The csv file to import                            |
+--------------+------------------+---------------------------------------------------+
|options       |:green:`Object`   | Options *described below*                         |
+--------------+------------------+---------------------------------------------------+
|callback      |:green:`Function` | a function to handle data one row at a time.      |
+--------------+------------------+---------------------------------------------------+

filename:
    The name of the csv file to be opened;

options:
    The ``options`` :green:`Object` may contain any of the following.

      * ``stripLeadingWhite`` -  :green:`Boolean` (default ``true``):
        Remove leading whitespace characters from cells.

      * ``stripTrailingWhite`` - :green:`Boolean` (default ``true``): Remove
        trailing whitespace characters from cells.

      * ``doubleQuoteEscape`` -  :green:`Boolean` (default ``false``):
        ``""`` within strings is used to embed ``"`` characters.

      * ``singleQuoteNest`` -  :green:`Boolean` (default ``true``): Strings
        may be bounded by ``'`` pairs and ``"`` characters within are ignored.

      * ``backslashEscape`` -  :green:`Boolean` (default ``true``):
        Characters preceded by '\\' are translated and escaped.

      * ``allEscapes`` -  :green:`Boolean` (default ``true``): All ``\``
        escape sequences known by the 'C' compiler are translated, if
        ``false`` only backslash, single quote, and double quote are escaped.

      * ``europeanDecimal``  -  :green:`Boolean` (default ``false``):
        Numbers like ``123 456,78`` will be parsed as ``123456.78``.

      * ``tryParsingStrings`` -  :green:`Boolean` (default ``false``): Look
        inside quoted strings for dates and numbers to parse, if ``false``
        anything quoted is a string.

      * ``delimiter`` - :green:`String` (default ``","``):  Use the first
        character of string as a column delimiter (e.g ``\t``).

      * ``timeFormat`` -  :green:`String` (default ``"%Y-%m-%d %H:%M:%S"``):
        Set the format for parsing a date/time. See manpage for 
        `strptime() <https://man7.org/linux/man-pages/man3/strptime.3p.html>`_.

      * ``returnType``-  :green:`String` (default ``"array"``, optionally
        ``"object"``): Whether to
        return an :green:`Array` or an :green:`Object` for each row.

      * ``hasHeaderRow`` - -  :green:`Boolean` (default ``false``): Whether
        to treat the first row as column names. If ``false``, the first row
        is imported as csv data and the column names will
        default to ``col_1, col_2, ..., col_n``.

      * ``normalize`` - :green:`Boolean` (default ``false``): If ``true``,
        examine each column in the parsed CSV object to find the majority
        type of that column.  It then casts all the members of that column
        to the majority type, or set it to ``null`` if it is
        unable to do so. If ``false``, each cell is individually normalized.

      * ``includeRawString`` :green:`Boolean` (default ``false``): if
        ``true``, return each cell as an object containing 
	``{value: normalized value, raw: originalString}``.  If false, each
	cell value is the primative normalized value.

callback:
   A :green:`Function` taking as parameters (``result_row``, ``index``, ``columns``).
   The callback is executed once for each row in the csv file:

   * ``result_row``: (:green:`Array`/:green:`Object`): depending on the setting of ``returnType``
     in ``Options`` above, a single row is passed to the callback as an
     :green:`Object` or an :green:`Array`.

   * ``index``: (:green:`Number`) The ordinal number of the current search result.

   * ``columns``: an :green:`Array` corresponding to the column names or
     aliases selected and returned in results.
   
.. _returnval:

Return Value:
	:green:`Number`/:green:`Object`.

        With no callback, an :green:`Object` is returned.  The :green:`Object` contains
	three or four key/value pairs.  
	
	Key: ``results``; Value: an :green:`Array` of :green:`Arrays`. 
	Each outer :green:`Array` corresponds to a row in the csv file
	and eac inner :green:`Array` corresponds to the columns in that row.
	If ``returnType`` is set to ``"object"``, an :green:`Array` of
	:green:`Objects` with keys set to the corresponding column names 
	and the values set to the corresponding column values  of the
	imported row.
	
	Key: ``rowCount``; Value: a :green:`Number` corresponding to the number of rows
	returned.

	Key:  ``columns``; Value: an :green:`Array` corresponding to the column names or
	aliases selected and returned in results.

	With a callback, the return value is set to number of rows in the
        csv file (not including the Header if ``hasHeaderRow`` is ``true``).

Note: In the callback, the loop can be cancelled at any point by returning
``false``.  The return value (number of rows) will still be the total number
of rows in the csv file.

csv
'''

Usage:

.. code-block:: javascript

    var res = rampart.import.csv(csvData [, options] [, callback]);


Same as `csvFile`_\ () except instead of a file name, a :green:`String` or :green:`Buffer` containing
the csv data is passed as a parameter.

Example:

.. code-block:: javascript

   var csvdata = 
   "column 1, column 2, column 3, column 4\n"+
   "1.0, val2, val3, val4\n" +
   "valx, val5, val6, value 7\n";

   /* no callback */
   console.log( 
     JSON.stringify(
       rampart.import.csv(csvdata, 
           {
               hasHeaderRow: true, 
               normalize: true
           }
       ),null,3
     )
   );

   /* with callback */
   var rows=rampart.import.csv(
      csvdata, 
      {
         hasHeaderRow: true,
         normalize: true,
         returnType:'object', 
         includeRawString:true
      },
      function(res,i,col){
           console.log(i,res,col);
      }
   );

   console.log("rows:", rows);

   /* expected output:
   {
      "results": [
         [
            1,
            "val2",
            "val3",
            "val4"
         ],
         [
            null,
            "val5",
            "val6",
            "value 7"
         ]
      ],
      "columns": [
         "column 1",
         "column 2",
         "column 3",
         "column 4"
      ],
      "rowCount": 2
   }
   0 {"column 1":{value:1,raw:"1.0"},"column 2":{value:"val2",raw:"val2"},"column 3":{value:"val3",raw:"val3"},"column 4":{value:"val4",raw:"val4"}} ["column 1","column 2","column 3","column 4"]
   1 {"column 1":{value:null,raw:"valx"},"column 2":{value:"val5",raw:"val5"},"column 3":{value:"val6",raw:"val6"},"column 4":{value:"value 7",raw:"value 7"}} ["column 1","column 2","column 3","column 4"]
   rows: 2
   */


Process Global Variable and Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


The ``process`` global variable contains the following properties:

exit
""""

The exit function terminates the execution of the current script.

Usage:

.. code-block:: javascript

   process.exit([exitcode]);

Where the optional ``exitcode`` is a :green:`Number`, the status that Rampart returns
to its parent (default: ``0``);

env
"""

The value of ``process.env`` is an :green:`Object` containing properties and values
corresponding to the environment variables available to Rampart upon
execution.

argv
""""

The value of ``process.argv`` is an :green:`Array` of the arguments passed to rampart
upon execution.  The first member is always the name of the rampart
executable.  The second is usually the filename of the script provided on
the command line.  However if flags are present (arguments starting with
``-``), the script name may be a later argument.  Subsequent members occur
in the order they were given on the command line.

scriptPath
""""""""""

The value of ``process.scriptPath`` is a :green:`String` containing the
canonical path (directory) in which the currently executing script can be
found (e.g.  if ``rampart /path/to/my/script.js`` is run,
``process.scriptPath`` will be ``/path/to/my``).

Additional Global Variables and Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Other global variables are provided by the Duktape JavaScript engine and
include:

* `Duktape <https://duktape.org/guide.html#builtin-duktape>`_
* `CBOR <https://duktape.org/guide.html#builtin-cbor>`_
* `TextEncoder <https://duktape.org/guide.html#builtin-textencoder>`_
* `TextDecoder <https://duktape.org/guide.html#builtin-textdecoder>`_
* `performance <https://duktape.org/guide.html#builtin-performance>`_

For more information, see the `Duktape Guide <https://duktape.org/guide.html>`_

Also added to Rampart is the ``setTimeout()`` function.  It is considered
experimental and is mainly included to support asynchronous functions in 
`ECMAScript 2015+ and Babel.js`_ functions.

ECMAScript 2015+ and Babel.js
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Babel Acknowledgement
"""""""""""""""""""""

Rampart experimentally uses `Babel.js <https://babeljs.io/>`_ to support a
greater breath of javascript syntax and functionality.  Babel.js is a
toolchain that converts ECMAScript 2015+ (and optionally TypeScript) code
into a version of JavaScript compatible with Duktape.  The authors of
Rampart are extremely grateful to the 
`Babel development team <https://babeljs.io/team>`_.

Babel License
"""""""""""""

Babel.js is 
`MIT licensed <https://github.com/babel/babel/blob/main/LICENSE>`_. 

Usage
"""""

A slightly modified version of babel.js (currently babel-standalone v
7.11.1) and the associated collection of polyfills (babel-polyfill.js) are
included in the Rampart distribution.  To use ECMA 2015+ features of
JavaScript, simply include the following at the beginning of the script:

.. code-block:: javascript

   "use babel"

Note that the ``"use babel"`` string should be the first JavaScript text in
the script.  However it may come after any comments or a hash-bang line.  It
also should be the only text on the line, other than an optional comment. 

Example:

.. code-block:: javascript

   #!/usr/local/bin/rampart
   // above is ignored by rampart.

   /* My first ECMA 2015 Script using Rampart/Duktape/Babel */

   "use babel" /* a comment on this line is ok */

   console.log(`a multiline string
   using backticks is much easier than
   using 
   console.log( 
                "string\\n" +
                "string2\\n"
              );
   `);

The ``"use babel"`` directive optionally takes a ``:`` followed by babel
options.  Without options ``"use babel"`` is equivalent to 
``"use babel:{ presets: ['env'], retainLines: true }"``.  See 
`babel documentation <https://babeljs.io/docs/en/babel-preset-env>`_ 
for more information on possible options.

A simple example in 
`TypeScript <https://www.typescriptlang.org/docs/handbook/typescript-in-5-minutes.html>`_:

.. code-block:: javascript

   /* note that filename is required for 'typescript'
      and that 'env' is also included to allow for ECMA 2015+  */

   "use babel:{ filename: 'myfile.ts', presets: ['typescript','env'], retainLines: true }"

   interface Point {
     x: number;
     y: number;
   }

   function printPoint(p: Point) {
     console.log(`${p.x}, ${p.y}`);
   }

   // prints "12, 26"
   const point = { x: 12, y: 26 };
   printPoint(point);

Note that babel does not actually do any type checking.  See
`this caveat <https://babeljs.io/docs/en/babel-plugin-transform-typescript#caveats>`_.

For a list of tested and supported syntax, see the 
``/usr/local/rampart/tests/babel-test.js`` file.

How it works
""""""""""""

When the ``"use babel"`` string is found, Rampart automatically loads
babel.js and uses it to transpile the script into JavaScript compatible with
the Duktape JavaScript engine.  A cache copy of the transpiled script will
be saved in the same directory, and will be named by removing ``.js`` from
the original script name and replacing it with ``.babel.js``.  Thus if, e.g.,
the original script was named ``myfile.js``, the transpiled version will be
named ``myfile.babel.js``.

When the original script is run again, Rampart will check the date on the
script, and if it was not modified after the creation of the ``*.babel.js``
file, the transpile stage will be skipped and the cached, transpiled script
will be run directly.

Caveats
"""""""

For a complicated script, the transpile stage can be very slow.  However if
the script has not changed since last run, the execution speed will be
normal as the cached/transpiled code will be used and thus no traspiling
will occur.

Also note that nearly all Rampart functions are synchronous, and therefore
will be executed before any babel transpiled asynchronous code
regardless of its position in the script.  This is normal behavior for
JavaScript, but may be counterintuitive if coming from, e.g. Node.js
where most functions are asynchronous.

As an example, the following code produces the same output in Rampart and
Node.js.

.. code-block:: javascript

   "use babel" /* ignored in node */

   function resolveme() {
     return new Promise(resolve => {

       setTimeout(() => {
         console.log("**I'm async in a Timeout!!**");
       },5);

       resolve("**I'm async!!**");

     });
   }

   async function asyncCall() {
     const result = await resolveme();
     console.log(result);
   }

   asyncCall();

   console.log(
   `a multiline string
   using backticks`
   );

   /* expect output:
   a multiline string
   using backticks
   **I'm async!!**
   **I'm async in a Timeout!!**
   */

