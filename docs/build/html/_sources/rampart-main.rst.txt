Introduction to Rampart
-----------------------

Preface
~~~~~~~

Acknowledgement
"""""""""""""""

Rampart uses the `Duktape JavaScript Engine <https://duktape.org>`_. Duktape is an 
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
JavaScript library and added to it is a SQL database, full text search
engine, a fast and innovative NOSQL Mmap database, a fast multi-threaded 
webserver, client functionality via the Curl and crypto functions via
Openssl.  It attempts to provide performance, maximum flexibility and 
ease of use through the marriage of C code and JavaScript scripting.



Features
~~~~~~~~

Core features of Duktape
""""""""""""""""""""""""

A partial list of Duktape features:

* Partial support for ECMAScript 2015 (E6) and ECMAScript 2016 (E7).
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

* Standard module support for ``C`` and ``JavaScript`` modules via the
  ``require()`` function.

* File and C-functions utilities such as ``printf``, ``fseek``, and ``exec``.

* Included ``C`` modules (``rampart-ramis``, ``rampart-curl``, ``rampart-sql``, ``rampart-server`` 
  ``rampart-html`` and ``rampart-crypto``).

* Minimal event loop using ``libevent2``

* ECMA 2015, 2016, 2017 and 2018 support using the `Babel <https://babeljs.io/>`_
  JavaScript transpiler.

* Full Text Search and SQL databasing via ``rampart-sql``.

* Fast NOSQL database via ``rampart-ramis``.

* Multi-threaded http(s) server from libevhtp via ``rampart-server``.

* http, ftp, etc. client functionality via ``rampart-curl``.

* Cryptography functions from Openssl via ``rampart-crypto``.

* Html parsing and and error correcting via ``rampart-html``. 

Rampart philosophy
~~~~~~~~~~~~~~~~~~
Though Rampart supports ``setTimeout()`` (and other async functions via
babel), the functions added to `Duktape <https://duktape.org>`_ 
via modules as well as the built-in functions are synchronous.  Raw JavaScript
execution is more memory efficient, but far slower than with, e.g., node.js.
However, the functionality and speed of the available C functions provide
comparable efficacy, excellent performance and are a viable alternative to 
`LAMP <https://en.wikipedia.org/wiki/LAMP_(software_bundle)>`_, 
`MEAN <https://en.wikipedia.org/wiki/MEAN_(solution_stack)>`_ or other
stacks, all in a single product, while consuming considerably less resources
than the aforementioned.

Rampart Global Variable and Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart provides global variables beyond what is available in Duktape:
``rampart`` and ``process``, as well as the ``require`` function.  Below is
a listing of these added functions.

rampart.globalize
"""""""""""""""""

Put all or named properties of an :green:`Object` in the global namespace.  

.. code-block:: javascript

    rampart.globalize(var_obj [, prop_names]);

+------------+----------------+-----------------------------------------------------------+
|Argument    |Type            |Description                                                |
+============+================+===========================================================+
|var_obj     |:green:`Object` | The :green:`Object` with the properties to be globalized  |
+------------+----------------+-----------------------------------------------------------+
|prop_names  |:green:`Array`  | optional :green:`Array` of property names to be           |
|            |                | put into the global namespace.  If specified, only        |
|            |                | the named properties will be exported.                    |
+------------+----------------+-----------------------------------------------------------+

Without ``prop_names``, this is equivalent to ``Object.assign(global, var_obj);``.

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

A collection of utility functions.  
See :ref:`this page<rampart-utils:rampart.utils>` 
for full description of functions.

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
        Set the format for parsing a date/time. See man page for 
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
	cell value is the primitive normalized value.

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
    three key/value pairs:

        * Key: ``results`` - Value: an :green:`Array` of :green:`Arrays`. 
          Each outer :green:`Array` corresponds to a row in the csv file
          and each inner :green:`Array` corresponds to the columns in that row.
          If ``returnType`` is set to ``"object"``, an :green:`Array` of
          :green:`Objects` with keys set to the corresponding column names 
          and the values set to the corresponding column values  of the
          imported row.
        
        * Key: ``rowCount`` - Value: a :green:`Number` corresponding to the number of rows returned.

        * Key:  ``columns`` - Value: an :green:`Array` corresponding to the column names or
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

Using the require Function to Import Modules
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Scripts may reference function stored in external files.  These files are
known as modules.  A module is a compiled C program or a JavaScript file
which exports an :green:`Object` or :green:`Function` when the
``require("module-name")`` syntax is used.

Example for the SQL C Module:

.. code-block:: javascript

   var Sql = require("rampart-sql");

This will search the current directory and the rampart modules directories
for a module named ``rampart-sql.so`` or ``rampart-sql.js`` and use the
first one found.  In this case ``rampart-sql.so`` will be found and the SQL
module and its functions will be usable via the named variable ``Sql``.  See,
e.g, :ref:`rampart-sql:Loading the Javascript Module` for full details.

Example creating a JavaScript module
""""""""""""""""""""""""""""""""""""

If you have an often used function, or a function used for serving web pages 
with :ref:`rampart-server:The rampart-server HTTP module`, it can be placed in a
separate file (here the file is named ``times2.js``):

.. code-block:: javascript

   function timestwo (num) {
      return num * 2;
   }

   module.exports=timestwo;

The ``module.exports`` variable is set to the :green:`Object` or
:green:`Function` being exported.

In another script, the exported ``timestwo`` function could be accessed as such:

.. code-block:: javascript

  var x2 = require("times2");
  /* alternatively
    var x2 = require("times2.js");
  */

  var res = x2(5);

  /* res == 10 */

Example creating a C module
"""""""""""""""""""""""""""

A module can also be written in C.  When exporting from C, the module should
return a :green:`Function` or an :green:`Object` which may contain functions
and/or other JavaScript variables.

Example (where filename is ``times3.c``):

.. code-block:: C

   #include "rampart.h"

   static duk_ret_t timesthree(duk_context *ctx)
   {
       double num = duk_get_number_default(ctx, 0, 0.0);

       duk_push_number(ctx, num * 3.0 );

       return 1;
   }


   /* **************************************************
      Initialize module
      ************************************************** */
   duk_ret_t duk_open_module(duk_context *ctx)
   {
     duk_push_c_function(ctx, timesthree, 1);

     return 1;
   }

The following could be compiled with GCC as follows:

``cc -I/usr/local/rampart/include -fPIC -shared -Wl,-soname,times3.so -o times3.so times3.c``

The module could then be imported using the ``require()`` function.

.. code-block:: javascript

   var x3 = require("times3");

   var res = x3(5);

   /* res == 15 */



See `The Duktape API Documentation <https://duktape.org/api.html>`_
for a full listing of functions available.

Module Search Path
""""""""""""""""""

Modules are searched for in the following order:

#. As given.  If ``/path/to/module.js`` is given, the absolute path is checked first.
   If ``path/to/module.js`` or ``module.js`` is given
   ``./path/to/module.js`` or ``./module.js`` is checked
   first. Thus relative paths are checked from the current directory first.

#. In :ref:`process.scriptPath <rampart-main:scriptPath>`\ .

#. In the ``.rampart/modules`` directory of current user's home directory 
   as provided by the ``$HOME`` environment variable.

#. In the "/modules" directory of the ``-DRP_INST_PATH`` path set when Rampart 
   was compiled.  The default is ``/usr/local/rampart/modules``. Or
   preferentially, if set, the path pointed to by the environment variable
   ``$RAMPART_PATH`` + "/modules".

#. In the current working directory. If ``/module.js`` is given, 
   ``./module.js`` is checked.


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
`ECMAScript 2015+ and Babel.js`_\ .

ECMAScript 2015+ and Babel.js
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Babel Acknowledgement
"""""""""""""""""""""

Rampart **experimentally** uses `Babel.js <https://babeljs.io/>`_ to support a
greater breath of JavaScript syntax and functionality.  Babel.js is a
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

   console.log(`a multi-line string
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
script, and if it was not modified after the modification date of the
``*.babel.js`` file, the transpile stage will be skipped and the cached,
transpiled script will be run directly.

Caveats
"""""""

For a complicated script, the transpile stage can be very slow.  However if
the script has not changed since last run, the execution speed will be
normal as the cached/transpiled code will be used and thus no traspiling
will occur.

Though nearly all rampart functions are synchronous, asynchronous code may
also be used with babel.  For example, the following code produces the same
output in Rampart and Node.js.

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

