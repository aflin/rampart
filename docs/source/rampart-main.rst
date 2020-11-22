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
* ES2015 TypedArray and Node.js Buffer bindings
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
Though rampart supports ``setTimeout()`` (and other async functions via
babel), the functions added to `Duktape <https://duktape.org>`_ 
via modules as well as the built-in functions are syncronous.  Raw javascript
execution is more memory efficient, but far slower than with, e.g., node.js.
However, the functionality and speed of the available C functions provide
comparable efficacy and are a viable alternative to 
`LAMP <https://en.wikipedia.org/wiki/LAMP_(software_bundle)>`_, 
`MEAN <https://en.wikipedia.org/wiki/MEAN_(solution_stack)>`_ or other
stacks, all in a single product.

Rampart Global Variable and Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two global variables beyond what is provided by Duktape:
``rampart`` and ``process``.

rampart.globalize
"""""""""""""""""

Put all or named properties of an object in the global namespace.  

.. code-block:: javascript

    rampart.globalize(var_obj [, prop_names]);

+------------+------------+---------------------------------------------------+
|Argument    |Type        |Description                                        |
+============+============+===================================================+
|var_obj     |Object      | The path to the directory containing the database |
+------------+------------+---------------------------------------------------+
|prop_names  |Array       | optional array of property names to be put into   |
|            |            | the global namespace.  If specified, only the     |
|            |            | named properties will be exported.                |
+------------+------------+---------------------------------------------------+


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

Utility functions are provided by the global ``rampart.utils`` object.
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

Print formatted string to stdout.  Provides C-like 
`printf(3) <https://man7.org/linux/man-pages/man3/printf.3.html>`_ 
functionality in javascript.

Usage:

.. code-block:: javascript

   rampart.utils.printf(fmt, ...)
   
Return Value:
   Number. The length in characters of the printed string.

Standard formats:  Most of the normal flags and formats are respected.
See standard formats and flags from
`printf(3) <https://man7.org/linux/man-pages/man3/printf.3.html>`_.

Extended (non-standard) formats:

   * ``%s`` - corresponding argument is treated as a string (converted if
     necessary)

   * ``%S`` - same as ``%s`` except an error is thrown if corresponding argument is
     not a String.

   * ``%J`` - print objects as JSON.

   * ``%B`` - print contents of a buffer as is.

   * ``%U`` - url encode (or if ``!`` flag present, decode) a string. 

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

Same as ``printf()`` except a string is returned

Return Value:
   String. The formatted string.

bprintf
'''''''

Same as ``sprintf()`` except a buffer is returned.

Return Value:
   Buffer.  The formatted string as a buffer.

fopen
'''''

Open a filehandle for use with `fprintf`_\ (), `fclose`_\ (), `fseek`_\ (),
`rewind`_\ (), `ftell`_\ (), `fflush`_\ () `fread`_\ () and `fwrite`_\ ().

Return Value:
   Object. The opened filehandle.

Usage:

.. code-block:: javascript

   var handle = rampart.utils.fopen(filename, mode);

Where ``filename`` is a String containing the file to be opened and mode is
a String (one of the following):

*  ``r`` - Open text file for reading.  The stream is positioned at the
   beginning of the file.

*  ``r+`` - Open for reading and writing.  The stream is positioned at the
   beginning of the file.

*  ``w`` - Truncate file to zero length or create text file for writing. 
   The stream is positioned at the beginning of the file.

*  ``w+`` - Open for reading and writing.  The file is created if it does
   not exist, otherwise it is truncated.  The stream is positioned at the
   beginning of the file.

*  ``a`` - Open for appending (writing at end of file).  The file is
   created if it does not exist.  The stream is positioned at the end of the
   file.

*  ``a+`` - Open for reading and appending (writing at end of file).  The
   file is created if it does not exist.  The initial file position for reading
   is at the beginning of the file, but output is always appended to the end of the
   file.

fclose
''''''

Close a previously opened handle object opened with `fopen`_\ ().

Example:

.. code-block:: javascript

   var handle = rampart.utils.fopen("/tmp/out.txt", "a");
   ...
   rampart.utils.fclose(handle);

fprintf
'''''''

Same as ``printf()`` except output is sent to the file provided by
a String or filehandle Object opened and returned from `fopen`_\ ().

Usage:

.. code-block:: javascript

   var output = fopen(filename, mode);
   rampart.utils.fprintf(output, fmt, ...);

   /* or */

   var output = filename;
   rampart.utils.fprintf(output, [, append], fmt, ...); 

   
Where:

* ``output`` may be a String (a filename), or an Object returned from `fopen`_\ ().

* ``fmt`` is a String, a `printf`_\ () format.

* ``append`` is an optional Boolean - if ``true`` append instead of
  overwrite an existing file.

Return Value:
   Number. The length in characters of the printed string.

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

+------------+------------+---------------------------------------------------+
|Argument    |Type        |Description                                        |
+============+============+===================================================+
|handle      |Object      | A handle opened with `fopen`_\ ()                 |
+------------+------------+---------------------------------------------------+
|offset      |Number      | offset in bytes from whence                       |
+------------+------------+---------------------------------------------------+
|whence      |String      | "seek_set" - measure offset from start of file    |
+            +            +---------------------------------------------------+
|            |            | "seek_cur" - measure offsef from current position |
+            +            +---------------------------------------------------+
|            |            | "seek_end" - measure offset from end of file.     |
+------------+------------+---------------------------------------------------+

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
   Number. Current position of ``handle``.


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

+------------+------------+---------------------------------------------------+
|Argument    |Type        |Description                                        |
+============+============+===================================================+
|handle      |Object      | A handle opened with `fopen`_\ ()                 |
+------------+------------+---------------------------------------------------+
|chunk_size  |Number      | Initial size of return buffer and number of bytes |
|            |            | to read at a time. If total number of bytes read  |
|            |            | is greater, the buffer grows as needed. If total  |
|            |            | of bytes read is less, the returned buffer will be|
|            |            | reduced in size to match. 4096 if not specified.  |
+------------+------------+---------------------------------------------------+
|max_size    |Number      | Maximum number of bytes to read.  Unlimited if    |
|            |            | not specified.                                    |
+------------+------------+---------------------------------------------------+

Return Value:
    Buffer. Contents set to the read bytes.

fwrite
''''''

Write data to handle opened with `fopen`_\ () or ``stdout``/``stderr``.

Usage:

.. code-block:: javascript

    var nbytes = rampart.utils.frwrite(handle, data [, max_bytes]);

+------------+------------+---------------------------------------------------+
|Argument    |Type        |Description                                        |
+============+============+===================================================+
|handle      |Object      | A handle opened with `fopen`_\ ()                 |
+------------+------------+---------------------------------------------------+
|data        |Buffer/     | The data to be written.                           |
|            |String      |                                                   |
+------------+------------+---------------------------------------------------+
|max_bytes   |Number      | Maximum number of bytes to write. Buffer/String   |
|            |            | length if not specified.                          |
+------------+------------+---------------------------------------------------+

Return Value:
    Number. Number of bytes written.


hexify
''''''

Convert data to a hex string.

Usage:

.. code-block:: javascript

   var hexstring = rampart.utils.hexify(data [, upper]);

Where ``data`` is the string of bytes to be converted and ``upper`` is a
Boolean, which if true prints using upper-case ``A-F``.

Return Value:
   String. Each byte in data is converted to its two character hex representation.

Example:  See `dehexify`_ below.

dehexify
''''''''

Convert a hex string to a string of bytes.

Usage:

.. code-block:: javascript

   var data = rampart.utils.hexify(hexstring);

Return Value:
   Buffer.  Each two character hex representation converted to binary.


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

Performs a byte-for-byte copy of string into a buffer.  
Also convert one Buffer to a buffer of another type.
See ``duk_to_buffer()`` in the 
`duktape documentation <https://wiki.duktape.org/howtobuffers2x#string-to-buffer-conversion>`_

Usage:

.. code-block:: javascript

   var buf = rampart.utils.stringToBuffer(data [, buftype ]);

Where ``data`` is a String or Buffer and ``buftype`` is one of the following
Strings:

   * ``"fixed"`` - returned buffer is a "fixed" buffer.
   * ``"dynamic"`` - returned buffer is a "dynamic" buffer.

If no ``buftype`` is given and ``data`` is a Buffer, the same type of buffer
is returned.  If no ``buftype`` is given and ``data`` is a String, a "fixed"
buffer is returned.

See `duktape documentation <https://wiki.duktape.org/howtobuffers2x>`_ for
more information on different types of buffers.

Return Value:
   Buffer.  Contents of String/Buffer copied to a new Buffer Object.

bufferToString
''''''''''''''

Performs a 1:1 copy of the contents of a Buffer to a String.

See ``duk_buffer_to_string()`` in the
`duktape documentation <https://wiki.duktape.org/howtobuffers2x#buffer-to-string-conversion>`_

Usage:

.. code-block:: javascript

   var str = rampart.utils.bufferToString(data);

Where data is a Buffer Object.

Return Value:
   String.  Contents of Buffer copied to a new String.

objectToQuery
'''''''''''''

Convert an object of key/value pairs to a string suitable for use as a query
string in an HTTP request.

Usage:

.. code-block:: javascript

   var qs = rampart.utils.objectToQuery(kvObj [, arrayOpt]);

Where ``kvObj`` is an Object containing the key/value pairs and ``arrayOpt``
controls how Array values are treated, and is
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

   * ``json`` - encode array as a JSON object.  Example: 
     ``{key1: ["val1", "val2"]}`` becomes
     ``key1=%5b%22val1%22%2c%22val2%22%5d``.

Note that the values ``null`` and ``undefined`` will be translated as the
strings ``"null"`` and ``"undefined"`` respectively.  Also values which
themselves are Objects will be converted to JSON.

queryToObject
'''''''''''''

Convert a query string to an object.  Reverses the process, with caveats, of
`objectToQuery`_\ ().

Usage:

.. code-block:: javascript

   var kvObj = rampart.utils.queryToObject(qs);

Caveats:

*  All primitive values will be converted to strings.

*  If ``repeat`` or ``bracket`` was used to create the 
   query string, all values will be returned as strings (even if an Array of
   Numbers was given to `objectToQuery`_\ ().

*  If ``comma`` was used to create the query string, no separation of comma
   separated values will occur and the entire value will be returned as a String.

*  If ``json`` was used, numeric values will be preserves as Numbers.

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


Where ``optsObj`` is an object with the key ``filename`` and optional keys
``offset``, ``length`` and/or ``retString``.


+------------+------------+-----------------------------------------------------+
|Argument    |Type        |Description                                          |
+============+============+=====================================================+
|filename    |String      | Path to the file to be read                         |
+------------+------------+-----------------------------------------------------+
|offsetPos   |Number      | If positive, offset from beginning of file          |
|            |            +-----------------------------------------------------+
|            |            | If negative, offset from end of file                |
+------------+------------+-----------------------------------------------------+
|rLength     |Number      | If greater than zero, amount in bytes to be read.   |
|            |            +-----------------------------------------------------+
|            |            |Otherwise, position from end of file to stop reading.|
+------------+------------+-----------------------------------------------------+
|return_str  |Boolean     | If not set, or ``false``, return a Buffer.          |
|            |            +-----------------------------------------------------+
|            |            | If ``true``, return contents as a String.           |
|            |            | May be truncated if file contains nulls.            |
+------------+------------+-----------------------------------------------------+

Return Value:
   Buffer or String.  The contents of the file.

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

Remove whitespace characters from beginning and end of a String.

Usage:

.. code-block:: javascript

   var trimmed = rampart.utils.trim(str);

Where ``str`` is a String.

Return Value:
   String. ``str`` with whitespace removed from beginning and end.

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

Where ``file`` is a String (name of file to be read) and return object
contains the property ``next``, a function to retrieve and return the next
line of text in the file.

Return Value:
   Object.  Property ``next`` of the return Object is a function which
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

Where ``file`` is a String (name of file).

Return Value:
   Boolean/Object. ``false`` if file does not exist.  Otherwise an Object with the following
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
   ``isSymbolicLink`` to test whether the file is a symbolic link.

exec
''''

Run an executable file.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.exec(command [, options] [,arg1, arg2, ..., argn] );

Where:

*  ``command`` - String. An absolute path to an executable or the name of
   an executable that may be found in the current ``PATH`` environment variable.

*  ``options`` - Object. Containing the following properties:

   *  ``timeout`` - Number: Maximum amount of time in milliseconds before
      the process is automatically killed.

   *  ``killSignal`` - Number. If timeout is reached, use this signal 

   *  ``background`` - Boolean.  Whether to execute detached and return
      immediately.  ``stdout`` and ``stderr`` below will be set to null.

*  ``argn`` - String/Number/Object/Boolean/Null - Arguments to be passed to
   ``command``.  Non-Strings are converted to a String (e.g. "true", "null",
   "42" or for Object, the equivalent of ``JSON.stringify(obj)``).

Return Value:
   Object.  Properties as follows

   * ``stdout`` - String. Output of command if ``background`` is not set ``false``. 
     Otherwise ``null``.

   * ``stderr`` - String. stderr output of command if ``background`` is not set ``false``.
     Otherwise ``null``.

   * ``exitStatus`` - Number.  The returned exit status of the command.

   * ``timedOut`` - Boolean.  Set true if program was killed after
     ``timeout`` milliseconds elapsed.

   * ``pid`` - Number. Process id of the executed command.

shell
'''''

Execute String in a bash shell. Equivalent to 
``rampart.utils.exec("bash", "-c", shellcmd);``.

Usage:

.. code-block:: javascript

   var ret = rampart.utils.shell(shellcmd);

Where ``shellcmd`` is a String containing the command and arguments to be
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

Where ``pid`` is a Number, the process id of process to be sent a signal and
``signal`` is a Number, the signal to send.  If ``signal`` is not specified,
``15`` (``SIGTERM``) is used.  See manual page for kill(1) for a list of
signals, which may vary by platform.  Setting ``signal`` to ``0`` sends no
signal, but checks for the existence of the process identified by ``pid``.

Return Value:
   Boolean.  ``true`` if signal successfully sent.  ``false`` if there was
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

Where ``path`` is a String, the directory to be created and ``mode`` is a
Number or String, the octal permissions mode. Any parent directories which
do not exist will also be created.  Throws error if lacking permissions or
if another error encountered.

Note that ``mode`` is normally given as an octal.  As such it can be, e.g.,
``0755`` (octal number) or ``"755"`` (string representation of an octal
number), but ``755``, as a decimal number may not work as intended.



Return Value:
   ``undefined``.

rmdir
'''''

Remove an empty directory.

Usage:

.. code-block:: javascript

   rampart.utils.rmdir(path [, recurse]);

Where ``path`` is a String, the directory to be removed and ``recurse`` is an
optional Boolean, which if ``true``, parent directories explicitely present in
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

Where ``path`` is a String, the directory whose content will be listed and
``showhidden`` is a Boolean, which if ``true``, files or directories
beginning with ``.`` (hidden files) will be included in the return value.

Return Value: 
   Array.  An Array of Strings, each filename in the directory.


copyFile
''''''''

Make a copy of a file.

Usage:

.. code-block:: javascript

   rampart.utils.copyFile({src: source, dest: destination [, overwrite: overWrite]});

   /* or */

   rampart.utils.copyFile(source, destination [, overWrite]);

Where ``source`` is a String, the file to be copied, ``destination`` is a
String, the name of the target file and optional ``overWrite`` is a Boolean
which if ``true`` will overwrite ``destination`` if it exists.

Return Value:
   ``undefined``.

rmFile
''''''

Delete a file.

Usage:

.. code-block:: javascript

   rampart.utils.rmFile(filename);

Where ``filename`` is a String, the name of the file to be removed.

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

Where ``path`` is a String, the file or directory upon which to be operated
and ``mode`` is a Number or String, the octal permissions mode.  Any parent
directories which do not exist will also be created.  Throws error if
lacking permissions or if another error encountered.

Note that ``mode`` is normally given as an octal.  As such it can be, e.g.,
``0755`` (octal number) or ``"755"`` (string representation of an octal
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

* ``file`` is a String, the name of the file upon which to operate, 

* ``noCreate`` is a Boolean (default ``false``) which, if ``true``
  will only update the time stamp, and will no create non-existing
  ``file``.

* ``setAccess`` is a Boolean (default ``true``).  Whether to update
  access time stamp of file.

* ``setModify`` is a Boolean (default ``true``).  Whether to update
  modification time stamp of file.

* ``referenceFile`` is a String.  If specified, the named file's access and
  modification time stamps will be used rather than the current time/date.

Return Value:
   ``undefined``.

rename
''''''

Rename or move a file.

Usage:

.. code-block:: javascript

   rampart.utils.rename(source, destination);

Where ``source`` is a String, the file to be renamed or moved, ``destination`` is a
String, the name of the target file.

Return Value:
   ``undefined``.

sleep
'''''

Pause execution for specified number of seconds.

Usage:

.. code-block:: javascript

   rampart.utils.sleep(seconds);

Where ``seconds`` is a Number.  Seconds may be a fraction of seconds. 
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
   Number. The pid of the current process.

getppid
'''''''

Get the process id of the parent of the current process.

Usage:

.. code-block:: javascript

   var ppid = rampart.utils.getppid();

Return Value:
   Number. The pid of the parent process.

Process Global Variable and Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


The ``process`` global variable contains the following properties:

exit
""""

The exit function terminates the execution of the current script.

Usage:

.. code-block:: javascript

   process.exit([exitcode]);

Where the optional ``exitcode`` is a Number, the status that rampart returns
to its parent (default 0);

env
"""

The value of ``process.env`` is an Object containing properties and values
corresponding to the environment variables available to Rampart upon
execution.

argv
""""

The value of ``process.argv`` is an Array of the arguments passed to rampart
upon execution.  The first member is always the name of the rampart
executable.  The second is usually the filename of the script provided on
the command line.  However if flags are present (arguments starting with
``-``), the script name may be a later argument.  Subsequent members occur
in the order they were given on the command line.

scriptPath
""""""""""

The value of ``process.scriptPath`` is a String containing the path
(directory) in which the currently executing script can be found (e.g.
if ``rampart /path/to/my/script.js`` is run, ``process.scriptPath`` will
be ``/path/to/my/``).

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

