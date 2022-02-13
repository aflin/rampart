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

Also included in Rampart is 
`linenoise.c <https://github.com/antirez/linenoise>`_ (under the
`BSD 2 Clause License <https://github.com/antirez/linenoise/blob/master/LICENSE>`_\ ),
`setproctitle.c <https://github.com/msantos/runcron/blob/master/setproctitle.c>`_\ (under
the MIT license) and `whereami.c <https://github.com/gpakosz/whereami>`_ (under the
MIT license or the WTFPLv2).  The developers of Rampart wish to extend their thanks
for the excellent code.

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

* Included ``C`` modules (``rampart-sql``, ``rampart-server``, ``rampart-curl``, 
  ``rampart-crypto``, ``rampart-html``, ``rampart-lmdb``, ``rampart-redis``, 
  ``rampart-cmark`` and ``rampart-robots``).

* Event loop using ``libevent2``.

* ECMA 2015, 2016, 2017 and 2018 support using the `Babel <https://babeljs.io/>`_
  JavaScript transpiler.

* Full Text Search and SQL databasing via ``rampart-sql``.

* Multi-threaded http(s) server from libevhtp_ws via ``rampart-server``.

* http, ftp, etc. client functionality via ``rampart-curl``.

* Cryptography functions from Openssl via ``rampart-crypto``.

* Html parsing and and error correcting via ``rampart-html``. 

* Fast NOSQL database via ``rampart-lmdb``.

* Redis Client via ``rampart-redis``.

* Simple Event functions via `rampart.event`_\ .

* `Extra JavaScript Functionality`_\ .

Rampart philosophy 
~~~~~~~~~~~~~~~~~~ 

Though Rampart supports ``setTimeout()``, `Events <rampart.events>`_ (and
other async functions via, e.g., :ref:`websockets <rampart-server:websockets>`, 
:ref:`rampart-redis <rampart-redis:The rampart-redis module>`, Babel, etc), 
all functions included in the `Duktape <https://duktape.org>`_ JavaScript
engine (as well as most of the functions added to Rampart) are synchronous.  Raw 
JavaScript execution is more memory efficient, but far slower than with, e.g., 
node.js.  However, the functionality and speed of the available C functions provide 
comparable efficacy, excellent performance and are a viable alternative to 
`LAMP <https://en.wikipedia.org/wiki/LAMP_(software_bundle)>`_, 
`MEAN <https://en.wikipedia.org/wiki/MEAN_(solution_stack)>`_ or other stacks, all
in a single product, while consuming considerably less resources than the
aforementioned.

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
|            |                | the named properties will be copied.                      |
+------------+----------------+-----------------------------------------------------------+

Without ``prop_names``, this is equivalent to ``Object.assign(global, var_obj);``.

With ``prop_names``, this is equivalent to ``for (var k in prop_names) global[[prop_names[k]]] = var_obj[[prop_names[k]]];``

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

rampart.event
"""""""""""""

Rampart can execute functions from within its event loop using its own
event-on-trigger syntax.

rampart.event.on()
''''''''''''''''''

Insert a named function to be run upon triggering a named event.  If the named
event does not exist, it will be created.

Usage:

.. code-block:: javascript

   rampart.event.on(eventName, funcName, callback, callbackUserVar);

Where:

   * ``eventName`` is an arbitrary :green:`String` used to identify, trigger
     and remove the event using the `rampart.event.remove()`_ function below.

   * ``funcName`` is an arbitrary :green:`String` used to identify and remove
     the callback function using the `rampart.event.off()`_ function below.

   * ``callback`` is a :green:`Function` to be executed when the event is triggered.
     It is called, when triggered, as such: ``callback(callbackUserVar, callbackTriggerVar)``.

   * ``callbackUserVar`` is an arbitrary variable which will be passed to the ``callback``
     :green:`Function` as its first parameter.

rampart.event.trigger()
'''''''''''''''''''''''

Trigger a named event, calling all the callbacks registered under the given name.

.. code-block:: javascript

   rampart.event.trigger(eventName, callbackTriggerVar);

Where:

   * ``eventName`` is the :green:`String` used when registering the event with `rampart.event.on()`_\ .

   * ``callbackTriggerVar`` is the second parameter passed to the ``callback`` function specified
     when the event and function were registered with `rampart.event.on()`_\ .

   * **Caveat**, the ``callbackTriggerVar`` must be a variable which 
     can be serialized using `CBOR <https://duktape.org/guide.html#builtin-cbor>`_\ .
     Because this function may trigger events that span several threads and Duktape stacks, when
     used with the :ref:`rampart-server <rampart-server:The rampart-server HTTP module>`
     module, special variables such as ``req`` (see: 
     :ref:`The Request Object <rampart-server:The Request Object>`) may contain
     functions and hidden state variables which cannot be moved from stack
     to stack.  In most cases, it will not be limiting since each callback is run on its own thread/stack
     and can take a ``callbackUserVar`` which does not have the above limitations.

rampart.event.off()
'''''''''''''''''''

Remove a named function from the list of functions for the given event.

.. code-block:: javascript

   rampart.event.off(eventName, funcName);

Where:

   * ``eventName`` is a :green:`String`, the ``eventName`` passed to the `rampart.utils.on()`
     function above.

   * ``funcName`` is a :green:`String`, the ``funcName`` passed to the `rampart.utils.on()`
     function above.

rampart.event.remove()
''''''''''''''''''''''

Remove all function from the list of functions for the given event. This effectively
removes the event.

.. code-block:: javascript

   rampart.event.remove(eventName);

Where:

   * ``eventName`` is a :green:`String`, the ``eventName`` passed to the `rampart.utils.on()`
     function above.


Example
'''''''

.. code-block:: javascript

   var usr_var = "I'm a user variable.";

   function myCallback (uservar,triggervar){

       console.log(uservar, "Triggervar = "+triggervar);
       rampart.utils.sleep(0.5);

       if(triggervar>4)
           rampart.event.remove("myev");

       rampart.event.trigger("myev", triggervar+1);
   }

   rampart.event.on("myev", "myfunc", myCallback, usr_var);

   rampart.event.trigger("myev", 1);

   /* expected output:
   I'm a user variable. Triggervar = 1
   I'm a user variable. Triggervar = 2
   I'm a user variable. Triggervar = 3
   I'm a user variable. Triggervar = 4
   I'm a user variable. Triggervar = 5
   */

See also: the :ref:`Echo/Chat Server Example <rampart-server:Example echo/chat server>`.

.. this was moved out.  update new location
    For a more complete example of events using the webserver and websockets,
    see the ``rampart/examples/web_server/modules/wschat.js``
    script.

rampart.include
"""""""""""""""

Include the source of a file in the current script as global code.

Usage:

.. code-block:: javascript

   rampart.include(jsfile);

Where ``jsfile`` is the path of the script to be included.  

If ``jsfile`` is not a absolute path name it will be searched for in the same
manner as with `Module Search Path`_ except that in addition to the 
current directory and the ``process.scriptPath`` directory, it will search in
``/usr/local/rampart/includes/`` and ``~/.rampart/includes/`` rather than the
equivalent ``*/modules/`` paths.

The ``rampart.include`` function is similar to the following code:

.. code-block:: javascript

   var icode = rampart.utils.readFile({file: jsfile, retString:true});
   eval(icode);

With the exception that it:

   * Processes `babel <ECMAScript 2015+ and Babel.js>`_ code.
   * Includes the `Extra JavaScript Functionality`_ described below.
   * Searches for the ``jsfile`` file in a manner similar to 
     the `require <Using the require Function to Import Modules>`_
     function.

Return Value:
``undefined``

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


      * ``includeRawString`` - :green:`Boolean` (default ``false``): if
        ``true``, return each cell as an object 
        containing ``{value: normalized value, raw: originalString}``.  
        If false, each cell value is the primitive normalized value.

      * ``progressFunc`` - :green:`Function`: A function to monitor the progress
        of the passes over the csv data.  It takes as arguments ``function (stage, i)``
        The variable ``stage`` is ``0`` for the initial counting of rows, ``1`` for the parsing
        of the cells in each row and ``2+`` optionally if ``normalize`` is ``true`` for the
        two stages of the analysis of each column in the csv (e.g. ``2`` for column 0 first pass,
        ``3`` for column 0 second pass, etc.).  The variable ``i`` is the row number.

      * ``progressStep`` :green:`Number`: Where number is ``n``, execute
        ``progresFunc`` callback, if provided, for every nth row in each stage.
        

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

The ``process`` global variable has the following properties:

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

installPath
"""""""""""

The value of ``process.installPath`` is a :green:`String` containing the
canonical path (directory) of the rampart install directory. It is derived
from the path of the rampart executable, removing '/bin' from the end of the 
path if exists.  Example: if ``/usr/local/bin/rampart`` is run (and is the
actual location of the executable and not a symlink), ``process.installPath``
will be ``/usr/local``.  However if the executable is in a path that does
not end in ``bin/`` (e.g. ``~/mytestfiles/rampart``), ``process.installPath`` will be the location of the
executable.  ``process.installPath`` is used internally to locate modules
and other files used by rampart. See `Module Search Path`_ below.

scriptPath
""""""""""

The value of ``process.scriptPath`` is a :green:`String` containing the
canonical path (directory) in which the currently executing script can be
found (e.g.  if ``rampart /path/to/my/script.js`` is run,
``process.scriptPath`` will be ``/path/to/my``).

scriptName
""""""""""

The value of ``process.scriptName`` is a :green:`String`, the name of the
currently executing script (e.g.  if ``rampart /path/to/my/script.js`` is 
run, ``process.scriptName`` will be ``script.js``).

script
""""""

The value of ``process.script`` is a :green:`String` containing the
canonical path (file) of the currently executing script
(e.g.  if ``rampart /path/to/my/script.js`` is run,
``process.script`` will be ``/path/to/my/script.js``).

getpid
""""""

Get the process id of the current process.

Usage:

.. code-block:: javascript

   var pid = process.getpid();

Return Value:
   :green:`Number`. The pid of the current process.

getppid
"""""""

Get the process id of the parent of the current process.

Usage:

.. code-block:: javascript

   var ppid = process.getppid();

Return Value:
   :green:`Number`. The pid of the parent process.

setProcTitle
""""""""""""

Set the name of the current process (as seen by the command line
utilities such as ``ps`` and ``top``).

Usage:

.. code-block:: javascript

   process.setProcTitle(newname);

Where ``newname`` is the new name for the current process.

Return Value:
   ``undefined``.

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
e.g, :ref:`The rampart-sql documentation <rampart-sql:Loading the Javascript Module>` 
for full details.

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

Note also that from within a module, the ``module`` object contains some useful
information.  An example module named ``mod.js`` and loaded with the
statement ``require("mod.js")`` will have
``module`` set to a value similar to the following:

.. code-block:: javascript

    {
       "id": "/path/to/my/mod.js",
       "path": "/path/to/my",
       "exports": {},
       "mtime": 1624904227,
       "atime": 1624904227
    }



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

This could be compiled with GCC as follows:

``cc -I/usr/local/rampart/include -fPIC -shared -Wl,-soname,times3.so -o times3.so times3.c``

On MacOs, the following might be used:

``cc -I/usr/local/rampart/include -dynamiclib -undefined dynamic_lookup -install_name times3.so -o times3.so times3.c``

The module could then be imported using the ``require()`` function.

.. code-block:: javascript

   var x3 = require("times3");

   var res = x3(5);

   /* res == 15 */



See `The Duktape API Documentation <https://duktape.org/api.html>`_
for a detailed listing of available Duktape C API functions.

Module Search Path
""""""""""""""""""

Modules are searched for in the following order:

#. As given.  If ``/path/to/module.js`` is given, the absolute path is checked first.
   If ``path/to/module.js`` or ``module.js`` is given
   ``./path/to/module.js`` or ``./module.js`` is checked
   first. Thus relative paths are checked from the current directory first.

#. In :ref:`process.scriptPath <rampart-main:scriptPath>`\ .

#. In the current working directory. If ``/module.js`` is given, 
   ``./module.js`` is checked.

#. In the ``lib/rampart_modules`` subdirectory of :ref:`process.installPath <rampart-main:installPath>`\ .

#. In the ``~/.rampart/lib/rampart_modules`` directory of current user's home directory 
   as provided by the ``$HOME`` environment variable.

#. In the ``lib/rampart_modules`` directory of the ``-DRP_INST_PATH`` path set when Rampart 
   was compiled.  The default is ``/usr/local/rampart/lib/rampart_modules``. Or
   preferentially, if set, the path pointed to by the environment variable
   ``$RAMPART_PATH`` + "/lib/rampart_modules".

#. In :ref:`process.installPath <rampart-main:installPath>`\ .

#. In the ``modules`` subdirectory of :ref:`process.installPath <rampart-main:installPath>`\ .

#. In the ``~/.rampart/modules`` directory of current user's home directory 
   as provided by the ``$HOME`` environment variable.

#. In the ``modules`` directory of the ``-DRP_INST_PATH`` path set when Rampart 
   was compiled.  The default is ``/usr/local/rampart/modules``. Or
   preferentially, if set, the path pointed to by the environment variable
   ``$RAMPART_PATH`` + "/modules".


Extra JavaScript Functionality
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A subset of post ES5 JavaScript syntax is supported when not using
`babel <ECMAScript 2015+ and Babel.js>`_ below.  It is provided
experimentally (unsupported) and is limited in scope. 

Object.values()
"""""""""""""""

Return an :green:`Array` containing the values of an object.

.. code-block:: javascript

   var obj = {
      key1: "val1",
      key2: "val2"
   }

   console.log(Object.values(obj));
   /* expected output:
      ["val1","val2"]              */

Template Literals
"""""""""""""""""

These may be uses much as expected:

.. code-block:: javascript

   var type, color;
   
   var out = `I'm a ${color? color: `black`} ${ type ? `${type} ` : `tea`}pot`;
   /* out = "I'm a black teapot" */
   
   type = "coffee";
   color = "red";
   out = `I'm a ${color? color: `black`} ${ type ? `${type} ` : `tea`}pot`;
   /* out = "I'm a red coffee pot" */   


Tagged Functions
""""""""""""""""

These may be used much as expected:

.. code-block:: javascript

   function aboutMe(strings) {
      var keys = Object.values(arguments).slice(1);
      console.log(strings);
      console.log(keys);
   }

   var name="Francis", age=31;

   aboutMe`My name is ${name} and I am ${age} years old`;
   /* expected output:
      ["My name is "," and I am "," years old"]
      ["Francis",31]
   */


Rest Parameters
"""""""""""""""

Rest Parameter syntax may also be used for arguments to functions.

.. code-block:: javascript

   function aboutMe(strings, ...keys) {
      console.log(strings);
      console.log(keys);
   }

   var name="Francis", age=31;

   aboutMe`My name is ${name} and I am ${age} years old`;
   /* expected output:
      ["My name is "," and I am "," years old"]
      ["Francis",31]
   */


Template Literals and sprintf
"""""""""""""""""""""""""""""

A non-standard shortcut syntax may be used in template literals in place of
:ref:`rampart.utils.sprintf <rampart-utils:sprintf>` by specifying a format
string followed by a colon ``:`` in a substituted variable (``${}``).  If
the string begins with a ``%``, or if the string is quoted with single or
double quotes :ref:`rampart.utils.sprintf <rampart-utils:sprintf>` is
called.

Example:

.. the original javascript


  var myhtml = `
  <div>
      my contents
  </div>
  `;

  /* same as:
  console.log("Here is the html:<br>\n<pre>"+rampart.utils.sprintf("%H",myhtml)+"</pre>");
  */ 
  console.log(`Here is the html:<br>\n<pre>${%H:myhtml}</pre>`);
      
  /* or */
      
  /* same as:
  console.log("Here is the html:<br>\n"+rampart.utils.sprintf("<pre>%H</pre>",myhtml));
  */

  console.log(`Here is the html<br>\n${"<pre>%H</pre>":myhtml}`);

  /* expected output:
  Here is the html:<br>
  <pre>
  &lt;div&gt;
      my contents
  &lt;&#47;div&gt;
  </pre>
  */


.. raw:: html

   <div class="highlight-javascript notranslate"><div class="highlight"><pre><span></span><span class="kd">var</span> <span class="nx">myhtml</span> <span class="o">=</span> <span class="sb">`</span>
   <span class="sb">&lt;div&gt;</span>
   <span class="sb">    my contents</span>
   <span class="sb">&lt;/div&gt;</span>
   <span class="sb">`</span><span class="p">;</span>

   <span class="cm">/* same as:</span>
   <span class="cm">console.log(&quot;Here is the html:&lt;br&gt;\n&lt;pre&gt;&quot;+rampart.utils.sprintf(&quot;%H&quot;,myhtml)+&quot;&lt;/pre&gt;&quot;);</span>
   <span class="cm">*/</span>
   <span class="nx">console</span><span class="p">.</span><span class="nx">log</span><span class="p">(</span><span class="sb">`Here is the html:&lt;br&gt;\n&lt;pre&gt;${</span><span class="nx">%H:myhtml</span></span><span class="sb">}&lt;/pre&gt;`</span><span class="p">);</span>

   <span class="cm">/* or */</span>

   <span class="cm">/* same as:</span>
   <span class="cm">console.log(&quot;Here is the html:&lt;br&gt;\n&quot;+rampart.utils.sprintf(&quot;&lt;pre&gt;%H&lt;/pre&gt;&quot;,myhtml));</span>
   <span class="cm">*/</span>
   <span class="nx">console</span><span class="p">.</span><span class="nx">log</span><span class="p">(</span><span class="sb">`Here is the html&lt;br&gt;\n${</span><span class="nx">&quot;&lt;pre&gt;%H&lt;/pre&gt;&quot;:myhtml</span></span><span class="sb">}`</span><span class="p">);</span>

   <span class="cm">/* expected output:</span>
   <span class="cm">Here is the html:&lt;br&gt;</span>
   <span class="cm">&lt;pre&gt;</span>
   <span class="cm">&amp;lt;div&amp;gt;</span>
   <span class="cm">    my contents</span>
   <span class="cm">&amp;lt;&amp;#47;div&amp;gt;</span>
   <span class="cm">&lt;/pre&gt;</span>
   <span class="cm">*/</span>
   </pre></div></div>


Note that this non-standard syntax is not available when using 
:ref:`babel <babeljs>` below.

setTimeout()
""""""""""""

Also added to Rampart is the ``setTimeout()`` function.  It supports the
asynchronous calling of functions from within Rampart's event loop in the same
manner as ``setTimeout`` in ``node.js`` or a browser such as Firefox or Chrome.

Usage:

.. code-block:: javascript

   var id = setTimeout(callback, timeOut);

Where:

* ``callback`` is a :green:`Function` to be run when the elapsed time is reached.
* ``timeOut`` is the amount of time in milliseconds to wait before the ``callback`` function is called.

Return Value:
    An id which may be used with `clearTimeout()`_\ .

Example:

.. code-block:: javascript

   /* print message after 2 seconds */
   setTimeout(function(){ console.log("Hi from a timeout callback"); }, 2000);

Note that Rampart JavaScript executes all global code before entering its event loop.
Thus if a script uses synchronous functions that take longer than ``timeOut``, the 
``callback`` will be run immediately after the global code is executed. Consider the following:

.. code-block:: javascript

   setTimeout(function(){ console.log("Hi from a timeout callback"); }, 2000);

   rampart.utils.sleep(3);

The ``callback`` function will not be executed until after the sleep
function returns.  At that time, the clock will have expired and the
``setTimeout`` callback will be run immediately.  The net effect is that
``console.log`` will be executed after approximately 3 seconds.

clearTimeout()
""""""""""""""

Clear a pending `setTimeout()`_ timer before it has executed.

Usage:

.. code-block:: javascript

   var id = setTimeout(callback, timeOut);

   clearTimeout(id);

Where:

* ``id`` is the return value from a call to `setTimeout()`_\ .

Return Value:
    ``undefined``

setInterval()
"""""""""""""

Similar to `setTimeout()`_ except it repeats every ``interval`` milliseconds
until cancelled via `clearInterval()`_.

Usage:

.. code-block:: javascript

   var id = setInterval(callback, interval);

Where:

* ``callback`` is a :green:`Function` to be run when the elapsed time is reached.
* ``interval`` is the amount of time in milliseconds between calls to ``callback``.

Return Value:
    An id which may be used with `clearInterval()`_\ .

Example:

.. code-block:: javascript

   var x=0;

   /* print message every second, 10 times */
   var id = setInterval(function(){ 
        x++;
        console.log("loop " + x);
        if(x>9) {
            clearInterval(id);
            console.log("all done");
        }
   }, 1000);

clearInterval()
"""""""""""""""

Clear a pending `setInterval()`_ timer, breaking the loop.

Usage:

.. code-block:: javascript

   var id = setInterval(callback, interval);

   clearInterval(id);

Where:

* ``id`` is the return value from a call to `setInterval()`_\ .

Return Value:
    ``undefined``

setMetronome()
""""""""""""""

Similar to `setInterval()`_ except it repeats every ``interval`` milliseconds
as close to the scheduled time as possible, possibly skipping intervals 
(aims for the absolute value of ``starttime + count * interval`` and skips
if past that time).

Usage:

.. code-block:: javascript

   var id = setMetronome(callback, interval);

Where:

* ``callback`` is a :green:`Function` to be run when the elapsed time is reached.
* ``interval`` is the amount of time in milliseconds between calls to ``callback``.

Return Value:
    An id which may be used with `clearMetronome()`_\ .

Example:

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    var x=0;
    var id=setMetronome(function(){
        var r = Math.random()*2;

        printf("%d %.3f %.3f\n", x++, r, (performance.now()/1000)%100);

        if(x>9)
            clearMetronome(id);

        sleep(r); //sleep a random amount of time between 0 and 2 seconds
    },1000);

    /* Output will be similar to:

    0 0.884 45.759
    1 0.574 46.759
    2 1.737 47.759
    3 0.810 49.759
    4 0.792 50.759
    5 1.616 51.759
    6 1.989 53.759
    7 1.959 55.759
    8 1.275 57.758
    9 0.324 59.760

    NOTE: where the sleep time is greater than 1 second, that
          second is skipped in order to keep the timing.
    */

clearMetronome()
""""""""""""""""

Clear a pending `setMetronome()`_ timer, breaking the loop.

Usage:

.. code-block:: javascript

   var id = setMetronome(callback, interval);

   clearMetronome(id);

Where:

* ``id`` is the return value from a call to `setMetronome()`_\ .

Return Value:
    ``undefined``

NOTE:  
    `clearTimeout()`_, `clearInterval()`_ and `clearMetronome()`_ internally are
    aliases for the same function and will clear whichever id is specified,
    regardless of type.


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

.. _babeljs:

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

Asynchronous code may also be used with babel.  For example, the following code 
produces the same output in Rampart and Node.js.

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

