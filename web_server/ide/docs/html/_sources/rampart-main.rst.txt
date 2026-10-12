Introduction to Rampart
-----------------------

Preface
~~~~~~~

Acknowledgement
"""""""""""""""

Rampart uses a `modified <https://github.com/aflin/duktape>`_ version of the
`Duktape JavaScript Engine <https://duktape.org>`_. Duktape is an
embeddable JavaScript engine, with a focus on portability and compact footprint.
The developers of Rampart are extremely grateful for the excellent API and
ease of use of this library.

License
"""""""

Duktape and the Core Rampart program are MIT licensed.  

Also included in Rampart is 
`linenoise.c <https://github.com/antirez/linenoise>`_ (under the
`BSD 2 Clause License <https://github.com/antirez/linenoise/blob/master/LICENSE>`_\ ),
`setproctitle.c <https://github.com/msantos/runcron/blob/master/setproctitle.c>`_ (under
the MIT license) and `whereami.c <https://github.com/gpakosz/whereami>`_ (under the
MIT license or the WTFPLv2).  The developers of Rampart wish to extend their thanks
for the excellent code.

What does it do?
""""""""""""""""

Rampart uses a low memory footprint JavaScript interpreter to bring together
several high performance tools and useful utilities for use in Web and
information management applications.  At its core is the Duktape JavaScript
library and added to it is a SQL database, full text and vector (semantic)
search engines, a memory map NOSQL database, a fast multi-threaded
webserver, client functionality
via the Curl, crypto functions via OpenSSL and more.  It attempts to provide
performance, maximum flexibility and ease of use through the marriage of C
code and JavaScript scripting.



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

See full list `Here <https://duktape.org>`__

Rampart additions
"""""""""""""""""

In addition to the standard features in Duktape JavaScript, Rampart adds the
following:

* Standard module support for ``C`` and ``JavaScript`` modules via the
  ``require()`` function.

* File and C-functions utilities such as ``printf``, ``fseek``, and ``exec``.

* Included ``C`` modules (``rampart-sql``, ``rampart-server``, ``rampart-curl``,
  ``rampart-crypto``, ``rampart-html``, ``rampart-lmdb``, ``rampart-redis``,
  ``rampart-cmark``, ``rampart-net``, ``rampart-python``, ``rampart-robots``,
  ``rampart-almanac``, ``rampart-auth``, ``rampart-totext``, ``rampart-faiss``,
  ``rampart-llamacpp``, ``rampart-iroh``, ``rampart-webview`` and
  ``rampart-gm``).

* Included ``JavaScript`` modules (``rampart-chromeview``,
  ``rampart-cmodule``, ``rampart-date-holidays``, ``rampart-email``,
  ``rampart-llm``, ``rampart-open-meteo``, ``rampart-sqlUpdate`` and
  ``rampart-webserver``).

* Event loop using ``libevent2``.

* ECMA 2015 (ES6) and later language support via the built-in
  `transpiler <ECMAScript 2015+ with transpiler>`_ or
  `Babel <ECMAScript 2015+ and Babel.js>`_.

* Full Text Search, Vector (semantic) Search and SQL databasing via ``rampart-sql``.

* Generic threading, locking and variable sharing via ``rampart.thread``.

* Multi-threaded http(s) server from libevhtp_ws via ``rampart-server``.

* HTTP, FTP, etc. client functionality via ``rampart-curl``.

* Cryptography functions from OpenSSL via ``rampart-crypto``.

* HTML parsing and error correcting via ``rampart-html``. 

* Fast NOSQL database via ``rampart-lmdb``.

* Redis Client via ``rampart-redis``.

* Asynchronous networking and socket functions via ``rampart-net``.

* Python interpreter, running python functions and sharing variables via
  ``rampart-python``

* Simple, cross-thread Event functions via `rampart.event`_\ .

* `Extra JavaScript Functionality`_\ .

* Lazy-loaded **Intl** (ECMA-402) support — full surface
  (``DateTimeFormat``, ``NumberFormat``, ``Collator``, ``Segmenter``,
  etc.) backed by vendored ICU4C; ``rampart-intl.so`` is only loaded on
  first access.  See :ref:`Intl <rampart-main:Intl>`.

* Lazy-loaded **WHATWG / W3C Web Platform APIs** (experimental) —
  ``fetch``, ``URL``, ``Headers``/``Request``/``Response``,
  ``Blob``/``File``, the ``ReadableStream`` family, ``WebSocket``,
  ``XMLHttpRequest``, ``crypto`` (Web Crypto), ``structuredClone``,
  ``EventTarget``, and more, via ``rampart-whatwg.so``.  Partial
  conformance — strongest where no browser DOM is required.  See
  :ref:`WHATWG / W3C Web Platform APIs (experimental) <rampart-main:WHATWG / W3C Web Platform APIs (experimental)>`.

* Optional **Node.js compatibility shim** (experimental) — a subset of
  node's core modules (``fs``, ``path``, ``util``, ``events``, ``url``,
  ``crypto``, ``stream``, ``buffer``, ``process`` extras, etc.) via
  ``require('rampart-nodeshim')``, for porting node-style code.  Prefer
  rampart's native modules for new code.  See
  :ref:`rampart-nodeshim Module <rampart-extras:rampart-nodeshim Module>`.

Rampart philosophy 
~~~~~~~~~~~~~~~~~~ 

Rampart treats JavaScript as a thin orchestration layer over high
performance functions written in C.  The heavy lifting — SQL, full text
and vector (semantic) search, the HTTP(S) server, cryptography,
compression, and document processing — happens in native code, so the
speed of the interpreter itself rarely matters to overall throughput.
Duktape is chosen for exactly this role: it is far more memory efficient
than engines such as V8, and wherever its slower interpretation would
otherwise be a bottleneck, the work has already been handed off to C.

The result is a single, self-contained product — no separate database
server, search cluster, or application tier to deploy and operate.  It is
a viable alternative to stacks such as
`LAMP <https://en.wikipedia.org/wiki/LAMP_(software_bundle)>`_,
`MEAN <https://en.wikipedia.org/wiki/MEAN_(solution_stack)>`_, or a
`Node <https://en.wikipedia.org/wiki/Node.js>`_\ /\ `Express <https://en.wikipedia.org/wiki/Express.js>`_
application paired with a separate search engine, while consuming
considerably fewer resources than any of them.

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

rampart.localize
""""""""""""""""

Put all or named properties of an :green:`Object` into the local scope
of the calling function.  Localized names are accessible as if they were
local variables, but do not pollute the global namespace.  When called
from the global scope, the behavior is identical to `rampart.globalize`_.

.. code-block:: javascript

    rampart.localize(var_obj [, filterOrIgnore [, ignoreConflicts]]);

+------------------+------------------+------------------------------------------------------------+
|Argument          |Type              |Description                                                 |
+==================+==================+============================================================+
|var_obj           |:green:`Object`   | The :green:`Object` with the properties to be localized.   |
+------------------+------------------+------------------------------------------------------------+
|filterOrIgnore    |:green:`Array` or | If an :green:`Array`, only the named properties will be    |
|                  |:green:`Boolean`  | copied (same convention as ``rampart.globalize``).         |
|                  |                  +------------------------------------------------------------+
|                  |                  | If a :green:`Boolean` set ``true``, silently skip          |
|                  |                  | properties that conflict with local variable               |
|                  |                  | declarations.  Default is ``false`` (throw a               |
|                  |                  | ``TypeError`` on conflict).                                |
+------------------+------------------+------------------------------------------------------------+
|ignoreConflicts   |:green:`Boolean`  | When ``filterOrIgnore`` is an :green:`Array`, this         |
|                  |                  | optional third argument controls conflict behavior.        |
|                  |                  | If ``true``, silently skip conflicts.  Default is          |
|                  |                  | ``false`` (throw a ``TypeError``).                         |
+------------------+------------------+------------------------------------------------------------+

Return value:
   ``undefined``.

Note:
   Names that are declared with ``var`` in the calling function are
   register-bound at compile time and cannot be overridden by ``localize``.
   By default, attempting to localize a name that conflicts with a ``var``
   declaration throws a ``TypeError``.  Setting ``ignoreConflicts`` to
   ``true`` causes the conflicting name to be silently skipped while
   still copying non-conflicting names.

Example:

.. code-block:: javascript

   function handleRequest() {
       rampart.localize(rampart.utils);
       printf("rampart.utils.* are now local to this function!\n");
   }

   /* with a filter */
   function handleRequest2() {
       rampart.localize(rampart.utils, ["printf", "sprintf"]);
       printf("only printf and sprintf are local\n");
   }

   /* with ignore conflicts */
   function myFunc() {
       var printf = null; /* local var declaration */
       rampart.localize(rampart.utils, true);
       /* printf stays null (conflict skipped), other utils are localized */
       sprintf("this works: %s\n", "yes");
   }

rampart.utils
"""""""""""""

A collection of utility functions.  
See :ref:`Rampart Utility Functions <rampart-utils:rampart.utils>` 
for full description of functions.

rampart.event
"""""""""""""

Rampart can execute functions from within its event loop using its own
event-on-trigger syntax.  When used across 
:ref:`threads <rampart-thread:rampart thread functions>`, a
registered function is executed in the thread in which it was registered and
may be triggered from any thread.

rampart.event.on()
''''''''''''''''''

Register a named function to be run upon triggering a named event.  If the named
event does not exist, it will be created.

Usage:

.. code-block:: javascript

   rampart.event.on(eventName, funcName, callback, callbackUserVar);

Where:

   * ``eventName`` is an arbitrary :green:`String` used to identify, trigger
     and remove the event using the `rampart.event.trigger()`_ and 
     `rampart.event.remove()`_ function below.

   * ``funcName`` is an arbitrary :green:`String` used to identify and remove
     the callback function using the `rampart.event.off()`_ function below.

   * ``callback`` is a :green:`Function` to be executed when the event is triggered.
     It is called, when triggered, as such: ``callback(callbackUserVar, callbackTriggerVar)``.

   * ``callbackUserVar`` is an arbitrary variable which will be passed to the ``callback``
     :green:`Function` as its first parameter.

rampart.event.trigger()
'''''''''''''''''''''''

Trigger a named event, calling all the callbacks registered under the
given name.  Callbacks do not run in place: they are deferred to the
event loop and run after the current code returns.

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
     or :ref:`rampart-thread <rampart-thread:Rampart Thread Functions>`
     modules, special variables such as ``req`` (see: 
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

   * ``eventName`` is a :green:`String`, the ``eventName`` passed to the `rampart.event.on()`
     function above.

   * ``funcName`` is a :green:`String`, the ``funcName`` passed to the `rampart.event.on()`
     function above.

rampart.event.remove()
''''''''''''''''''''''

Remove all functions from the list of functions for the given event. This effectively
removes the event.

.. code-block:: javascript

   rampart.event.remove(eventName);

Where:

   * ``eventName`` is a :green:`String`, the ``eventName`` passed to the `rampart.event.on()`
     function above.

rampart.event.scopeToModule()
'''''''''''''''''''''''''''''

Scope ``rampart.event`` functions set with ``rampart.event.on`` from inside
a module to that module only.  If set, ``rampart.event.trigger`` will only
trigger the named event from inside a module if it was set in the same
module.  This is useful for long lived scripts such as used with :ref:`the
rampart server module <rampart-server:The rampart-server HTTP module>`.

This setting also separates events set and triggered in modules from those
in the main script.

See `Using the require Function to Import Modules`_ below for information on
modules.

Note: This should be set before any events are created.  Once this is turned
on, it cannot be turned off in the same invocation of the script.

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

If ``jsfile`` is not an absolute path name it will be searched for in the same
manner as with `Module Search Path`_ except that in addition to the 
current directory and the ``process.scriptPath`` directory, it will search in
``/usr/local/rampart/includes/`` and ``~/.rampart/includes/`` rather than the
equivalent ``*/modules/`` paths.

The ``rampart.include`` function is similar to the following code:

.. code-block:: javascript

   var icode = rampart.utils.readFile({file: jsfile, returnString:true});
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

      * ``hasHeaderRow`` -  :green:`Boolean` (default ``false``): Whether
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
        of the passes over the csv data.  It takes as arguments ``function(i, stage)``
        The variable ``stage`` is ``0`` for the initial counting of rows, ``1`` for the parsing
        of the cells in each row and ``2+`` optionally if ``normalize`` is ``true`` for the
        two stages of the analysis of each column in the csv (e.g. ``2`` for column 0 first pass,
        ``3`` for column 0 second pass, etc.).  The variable ``i`` is the current row number.

      * ``progressStep`` :green:`Number`: Where number is ``n``, execute
        ``progressFunc`` callback, if provided, for every nth row in each stage.
        

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

Note: In the callback, the loop can be canceled at any point by returning
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
path if it exists.  Example: if ``/usr/local/bin/rampart`` is run (and is the
actual location of the executable and not a symlink), ``process.installPath``
will be ``/usr/local``.  However if the executable is in a path that does
not end in ``bin/`` (e.g. ``~/mytestfiles/rampart``), ``process.installPath`` 
will be the location of the executable (and the same as ``installPathBin`` 
below).  ``process.installPath`` is used internally to locate modules
and other files used by rampart. See `Module Search Path`_ below.

installPathBin
""""""""""""""

The value of ``process.installPathBin`` is a :green:`String` containing the
canonical path of the directory containing the rampart executable.

installPathExec
"""""""""""""""

The value of ``process.installPathExec`` is a :green:`String` containing the
canonical path of the rampart executable itself, file name included (e.g.
``/usr/local/bin/rampart``).  Symlinks are resolved, so this always names
the real file being run -- which is what a
:ref:`single-file bundle <rampart-extras:single-file bundles>` reads when it
needs to examine or copy itself.

modulesPath
"""""""""""

The value of ``process.modulesPath`` is a :green:`String` containing the
canonical path (directory) in which the standard installed modules can 
be found.

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

nCpu
""""

The value of ``process.nCpu`` is a :green:`Number`, the count of online
logical CPUs as reported by ``sysconf(_SC_NPROCESSORS_ONLN)``.  Set once
at startup; not affected by subsequent CPU hot-plug events.

getTotalMem
"""""""""""

Get the total physical RAM installed in the system, in megabytes.

Usage:

.. code-block:: javascript

   var mb = process.getTotalMem();

Return Value:
   :green:`Number`. Total physical memory in MB.

Implementation: ``sysinfo(2)`` on Linux, ``sysctl HW_MEMSIZE`` on
macOS, ``sysctl HW_PHYSMEM`` on FreeBSD.

getFreeMem
""""""""""

Get the amount of memory currently available to the system, in
megabytes.

Usage:

.. code-block:: javascript

   var mb = process.getFreeMem();

Return Value:
   :green:`Number`. Available memory in MB.

Implementation: ``sysinfo(2)`` on Linux.  On macOS, sums ``free`` +
``inactive`` + ``speculative`` pages from ``host_statistics64(
HOST_VM_INFO64)`` — matching node's convention, since macOS aggressively
keeps reusable pages in the inactive list.  On FreeBSD, uses
``sysctlbyname("vm.stats.vm.v_free_count")``.

uptime
""""""

Get process uptime in seconds (matches ``process.uptime()`` in
Node.js, Deno, Bun, and other JavaScript runtimes).

Usage:

.. code-block:: javascript

   var sec = process.uptime();

Return Value:
   :green:`Number`. Seconds since *this rampart process* started.

Implementation: ``clock_gettime(CLOCK_MONOTONIC)``.  The origin is
captured once at startup, so the value includes rampart's own start-up
time — as it does in node.

The origin is process-wide.  Threads report the lifetime of the process
that created them, matching node, whose ``worker_threads`` also see the
parent's uptime.  A process created by :ref:`fork() <rampart-utils:fork>`
re-stamps its own origin and reports its own age, matching node's
process-spawning APIs, where each new process starts from zero.

For seconds since the operating system booted (was the historical
meaning of ``process.uptime()`` in rampart, now under a separate
name), see `systemUptime`_.

systemUptime
""""""""""""

Get system uptime in seconds.

Usage:

.. code-block:: javascript

   var sec = process.systemUptime();

Return Value:
   :green:`Number`. Seconds since system boot.

Implementation: ``sysinfo(2)`` on Linux, ``sysctl KERN_BOOTTIME``
(boot ``timeval`` subtracted from wall-clock now) on macOS / \*BSD.
Falls back to ``CLOCK_MONOTONIC`` if the platform-native call fails.

Equivalent to node's ``os.uptime()`` (rampart's nodeshim ``os``
module delegates here).

getCpuInfo
""""""""""

Get per-core CPU information, in the same shape node's ``os.cpus()``
returns.

Usage:

.. code-block:: javascript

   var cpus = process.getCpuInfo();
   // cpus[i] = {
   //    model: <String>,        // CPU model string ("unknown" if unavailable)
   //    speed: <Number>,        // MHz (0 if not reported by the platform)
   //    times: {
   //       user: <ms>, nice: <ms>, sys: <ms>, idle: <ms>, irq: <ms>
   //    }
   // }

Return Value:
   :green:`Array` of per-core objects.

Implementation: ``/proc/stat`` + ``/proc/cpuinfo`` on Linux;
``host_processor_info(PROCESSOR_CPU_LOAD_INFO)`` plus ``sysctlbyname(
"machdep.cpu.brand_string")`` and ``hw.cpufrequency_max``/
``hw.cpufrequency`` on macOS; ``kern.cp_times`` plus ``hw.model`` and
``dev.cpu.0.freq`` on FreeBSD.

On Apple Silicon, ``hw.cpufrequency_max`` is not available and
``speed`` will be ``0`` — matching node/libuv's behavior.  On macOS,
``nice`` and ``irq`` are always ``0`` (the platform does not separate
them from ``sys``/``user``).

setMaxMem
"""""""""

Set a hard upper bound on the virtual address space the current
process may use.  Once set, ``malloc``/buffer allocations that would
push the process over this limit fail.

Usage:

.. code-block:: javascript

   var mb = process.setMaxMem(amount);

Where ``amount`` is either:

   * a :green:`Number` — the limit in MB
   * a :green:`String` ending in ``%`` — a percentage of
     ``getTotalMem()`` (e.g. ``"75%"``)

Return Value:
   :green:`Number`. The limit actually set, in MB.

Implementation: ``setrlimit(RLIMIT_AS, ...)``.  Note that on macOS,
``RLIMIT_AS`` is accepted by ``setrlimit`` but **not enforced** by the
kernel — the call returns success and ``getTotalMem``/``getFreeMem``
report unchanged values, but allocations above the limit will still
succeed.  Effective on Linux and FreeBSD.  Cannot be raised above
any system-imposed hard limit.

Note also that the limit is applied to both the soft **and** hard
``RLIMIT_AS``.  Since a hard limit can only ever be lowered by an
unprivileged process, a later call with a larger ``amount`` will fail
with ``process.setMaxMem - Failed to set Max``.  In other words the
limit may be lowered repeatedly, but never raised again within the
same process.

Using the require Function to Import Modules
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Scripts may reference functions stored in external files.  These files are
known as modules.  A module is a compiled C program, a JavaScript file
which exports an :green:`Object` or :green:`Function`, or a JSON file,
loaded with the ``require("module-name")`` syntax.

Example for the SQL C Module:

.. code-block:: javascript

   var Sql = require("rampart-sql");

This will search the script's own directory and the rampart modules
directories for a module named ``rampart-sql``.  In this case
``rampart-sql.so`` will be found and the SQL module and its functions will
be usable via the named variable ``Sql``.  See, e.g,
:ref:`The rampart-sql documentation <rampart-sql:Loading the Javascript Module>`
for full details.

.. _require-resolution-order:

Resolution order
""""""""""""""""

When the name given to ``require()`` has no extension, each extension is
tried in turn, and for each one every search directory is checked before
moving on to the next extension, in this order: ``.js``, ``.json``,
``.so``.  If not found, the bare name is tried and loaded as JavaScript.

So a ``foo.js`` found late on the search path is preferred over a
``foo.so`` found early; give the extension — ``require("foo.so")`` — to
load a specific file.

A ``.json`` module is parsed and the result becomes ``module.exports``.
It is cached like any other module, so ``require("conf.json")`` and a
later ``require("conf")`` return the same object:

.. code-block:: javascript

   /* conf.json contains: {"host":"localhost","port":8088} */
   var conf = require("conf.json");
   printf("%s:%d\n", conf.host, conf.port);   /* localhost:8088 */

Requiring a directory
"""""""""""""""""""""

When the name resolves to a directory, its entry point is the file named
by ``"main"`` in the directory's ``package.json``, or failing that
``index.js``, ``index.json`` or ``index.so``.  With none of these,
resolution fails as usual.

``"main"`` must name a file, though its extension may be omitted
(``"main": "lib/entry"`` finds ``lib/entry.js``); it is not followed if it
names a directory.

A trailing slash forces directory resolution, which matters only when a
file and a directory share a name:

.. code-block:: javascript

   var a = require("./mymod");    // mymod.js
   var b = require("./mymod/");   // mymod/index.js

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
       "atime": 1624904227,
       "deps": []
    }

Where ``deps`` is an :green:`Array` of the modules this one has itself
required.  The ``module`` object additionally has a non-enumerable
``resolve()`` :green:`Function` (and so is not shown above), which takes a
module name and returns the full path that ``require()`` would load for it.



Example creating a C module
"""""""""""""""""""""""""""

A module can also be written in C.  Below is an example where the filename
is ``times3.c``:

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

In this example, the item on the top of the 
`value stack <https://duktape.org/api.html#concepts.4>`_ (when the C function
returns ``1``) in the ``timesthree()`` function will be the return value of
the exported function.

The ``timesthree`` function is made available to JavaScript in a function
that must be named ``duk_open_module``.  The C function pushed to the top of
the stack (when ``duk_open_module()`` returns ``1``) will be
the return value of the ``require()`` function in JavaScript.

The ``duk_open_module`` alternatively can push an :green:`Object` which
contains functions and/or other JavaScript variables.

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

See also the :ref:`rampart-cmodule <rampart-extras:rampart-cmodule>`
helper, which automatically compiles embedded C code into a JavaScript
function.

Module Search Path
""""""""""""""""""

Modules are searched for in the following order:

#. If an absolute path (``/path/to/module.js``) is given, only that path
   is checked; if not found, the search stops there.  (An absolute path is
   always resolved from disk, never from a bundle.)

#. When running a :ref:`single-file bundle <rampart-extras:single-file bundles>`,
   the entire bundle (its appended zip) is searched before any on-disk
   location: the module name at the zip root, then in the zip's
   ``modules/`` and ``lib/rampart_modules/`` subdirectories.

#. If included from within a module, in that module's own directory
   (``module.path``).

#. In :ref:`process.scriptPath <rampart-main:scriptPath>`\ , then in its
   ``modules/`` and ``lib/rampart_modules/`` subdirectories.

#. In the ``modules/`` and ``lib/rampart_modules/`` subdirectories of
   ``~/.rampart/`` (i.e. ``~/.rampart/modules/`` and
   ``~/.rampart/lib/rampart_modules/``), where ``~`` is the current user's
   home directory from the ``$HOME`` environment variable.  If ``$HOME``
   is unset or unreadable, ``/tmp`` is used.

#. If set, in the directory given by the ``$RAMPART_PATH`` environment
   variable.

#. In :ref:`process.installPath <rampart-main:installPath>`\ , then in its
   ``modules/`` and ``lib/rampart_modules/`` subdirectories.  (The modules
   directory located here is exposed as
   :ref:`process.modulesPath <rampart-main:modulesPath>`\ .)

Note that the current working directory is not on this list; a relative id
such as ``require("./mod.js")`` is resolved against the requiring script or
module, not against the directory you happen to be in.


Extra JavaScript Functionality
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart adds a number of standard-library methods and globals to those
Duktape provides.  These aim to match the ECMAScript specification; only
the rampart-specific extensions are flagged.  For language *syntax*
beyond ES5, see the
`transpiler <ECMAScript 2015+ with transpiler>`_.

Object Methods
""""""""""""""

* ``Object.values(obj)`` — Returns an :green:`Array` of the object's
  own enumerable values.  Also accepts a :green:`String` (returns char
  array) or :green:`Buffer` (passthrough).
* ``Object.hasOwn(obj, prop)`` — :green:`Boolean`; safer than
  ``obj.hasOwnProperty(prop)`` when ``obj`` lacks a prototype chain.
* ``Object.fromEntries(iter)`` — Builds an object from an array of
  ``[key, value]`` pairs (inverse of ``Object.entries``).
* ``Object.groupBy(items, keyFn)`` — Groups items by the result of
  ``keyFn``.  Returns a null-prototype object whose values are arrays
  of items sharing a key.  Accepts any iterable.

.. code-block:: javascript

   Object.values({a:1, b:2});                    /* [1, 2] */
   Object.fromEntries([['a',1],['b',2]]);        /* {a:1, b:2} */
   Object.groupBy([1.1, 2.5, 1.7], Math.floor);  /* {1:[1.1,1.7], 2:[2.5]} */

Array Methods
"""""""""""""

Standard prototype methods on :green:`Array.prototype`:
``find``, ``findIndex``, ``includes``, ``flat`` (default depth 1;
``Infinity`` supported), ``flatMap``, ``at`` (negative indices),
``findLast``, ``findLastIndex``.

Static methods on :green:`Array`: ``Array.from(iterable[, mapFn])``
(strings, array-likes, iterables) and ``Array.of(...args)``.

Iteration surface: ``arr.keys()``, ``arr.values()``, ``arr.entries()``,
and ``arr[Symbol.iterator]()`` all return spec iterators.  Note that
``for…of`` and spread *syntax* still require the
`transpiler <ECMAScript 2015+ with transpiler>`_, but the underlying
iterators work in vanilla Rampart via ``.next()`` and
``Array.from(...)``.

.. code-block:: javascript

   [1,2,3,4].findLast(function(x){return x < 4;});   /* 3 */
   [1,[2,[3]]].flat(Infinity);                       /* [1,2,3] */
   Array.from('abc', function(c){return c.toUpperCase();});  /* ['A','B','C'] */

String Methods
""""""""""""""

* ``str.trimStart()`` / ``str.trimEnd()`` — trim whitespace at one end.
* ``str.replaceAll(search, repl)`` — string-only ``search``; replaces
  every occurrence.
* ``str.matchAll(regex)`` — returns an iterator over global-regex
  matches (each match includes capture groups).  Throws
  ``TypeError`` if ``regex`` is a non-global :green:`RegExp` (per spec).

Template Literals
"""""""""""""""""

These may be used in the same manner as in standard ES6 JavaScript:

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

These may be used in the same manner as in standard ES6 JavaScript:

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

A **non-standard** (and unique to Rampart) shortcut syntax may be used in
template literals in place of :ref:`rampart.utils.sprintf
<rampart-utils:sprintf>` by specifying a format string followed by a colon
``:`` in a substituted variable (``${}``).  If the string begins with a
``%``, or if the string is quoted with single or double quotes
:ref:`rampart.utils.sprintf <rampart-utils:sprintf>` is called.

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

Unescaped Literals
""""""""""""""""""

A **non-standard** (and unique to Rampart) shortcut syntax using triple
backticks may be used in place of normal :green:`Strings` where ``\`` have
no special meaning.  It also accepts multi-lined strings.

Example:

.. code-block:: javascript

    var unescaped = ```here is a single backslash:
    \
    ```;
    /* equiv to ""here is a single backslash:\n\\\n"; */

Note that this non-standard syntax is not available when using 
:ref:`babel <babeljs>` below.


Duktape/Node.js Buffer Binding Extras
"""""""""""""""""""""""""""""""""""""

The Duktape JavaScript engine provides basic
`node.js Buffer <https://wiki.duktape.org/howtobuffers2x#node.js-buffer-bindings>`_
support — the ``Buffer`` constructor, byte indexing, ``read*`` /
``write*`` accessors for fixed-width integers and floats, ``slice``,
``equals``, ``compare``, ``copy``, ``fill``, ``concat``, ``isBuffer``,
``byteLength``, and a basic single-argument ``toString``. Rampart
extends this with encoding-aware methods, additional factories, and
several prototype helpers commonly used by node code.

Supported encodings
'''''''''''''''''''

The following encoding names are accepted by ``toString()``,
``write()``, ``Buffer.from()``, and ``Buffer.byteLength()``. Names
are case-insensitive and dashes (``utf-8``) are tolerated.

* ``utf8`` (default)
* ``hex``
* ``base64``
* ``base64url`` (URL/filename-safe alphabet, no padding)
* ``latin1`` (also ``binary``)
* ``utf16le`` (also ``ucs2``)
* ``ascii``

Static methods
''''''''''''''

* ``Buffer.alloc(size[, fill])`` — Allocate a new zero-filled
  :green:`Buffer`. If ``fill`` (a :green:`String`, :green:`Buffer`,
  or :green:`Number`) is given, the buffer is filled with that data,
  repeating if shorter than ``size``.

* ``Buffer.allocUnsafe(size)`` / ``Buffer.allocUnsafeSlow(size)`` —
  Aliases of ``Buffer.alloc``. (Node returns uninitialized memory
  for speed; rampart always zero-fills since the difference is
  negligible at our typical sizes and the safety win is real.)

* ``Buffer.from(data[, encoding])`` — Create a new :green:`Buffer`
  from any of:

    * A :green:`Buffer` or :green:`Uint8Array` (copies the bytes).
    * An :green:`Array` of integers (each truncated to a byte).
    * A :green:`String`. With no ``encoding``, the string is taken
      as UTF-8. Otherwise it is decoded per the encoding name (e.g.
      ``Buffer.from('aabbcc','hex')`` yields ``<Buffer aa bb cc>``).

* ``Buffer.byteLength(input[, encoding])`` — Number of bytes
  ``input`` occupies. For a :green:`Buffer`, returns its length
  directly. For a :green:`String`, returns the byte count for the
  given encoding (or UTF-8 by default).

* ``Buffer.isEncoding(name)`` — Returns :green:`true` if ``name`` is
  one of the supported encodings above.

* ``Buffer.isBuffer(value)`` / ``Buffer.concat(list[, totalLen])`` /
  ``Buffer.compare(a, b)`` — As in node.

* ``Buffer.poolSize`` — Numeric property. Set to ``8192`` for
  node-compat; rampart doesn't actually pool internally.

Prototype methods
'''''''''''''''''

* ``buf.toString([encoding][, start][, end])`` — Decode the bytes in
  the slice ``[start, end)`` per ``encoding``. Default encoding is
  UTF-8.

* ``buf.write(string[, offset][, length][, encoding])`` — Write a
  string into the buffer using the given encoding. Returns the
  number of bytes written. Argument-parsing follows node's flexible
  shapes (``write(str)``, ``write(str, enc)``, ``write(str, offset, enc)``,
  ``write(str, offset, length, enc)``).

* ``buf.indexOf(value[, byteOffset][, encoding])`` /
  ``buf.lastIndexOf(...)`` / ``buf.includes(...)`` — Locate
  ``value`` in ``buf``. ``value`` may be a single byte (:green:`Number`),
  a :green:`Buffer`, or a :green:`String` (interpreted in the given
  encoding). ``indexOf`` / ``lastIndexOf`` return the byte index or
  ``-1``; ``includes`` returns :green:`Boolean`.

* ``buf.swap16()`` / ``buf.swap32()`` / ``buf.swap64()`` — Byte-swap
  the buffer in place as an array of 16/32/64-bit values. Throws if
  the buffer length isn't a multiple of 2/4/8.

* ``buf.subarray([start][, end])`` — Returns a new :green:`Buffer`
  over the requested slice. (Unlike a bare ``Uint8Array.subarray``,
  this is a real ``Buffer`` so node's prototype methods are
  available on it.)

* ``buf.keys()`` / ``buf.values()`` / ``buf.entries()`` — Return
  iterators (following the ES2015 iterator protocol — ``next()`` +
  ``[Symbol.iterator]`` returning ``{value, done}``) over byte
  indexes, bytes, or ``[index, byte]`` pairs (inherited from
  :green:`%TypedArray%.prototype`).  ``buf[Symbol.iterator]()`` is
  also available so ``Buffer`` is iterable.

* All of node's ``read*`` / ``write*`` accessors for fixed-width
  integers and floats (e.g. ``readUInt32BE``, ``writeFloatLE``) are
  provided by duktape's underlying Buffer and behave as in node.
  The ``BigInt`` variants (``readBigInt64BE``, etc.) are not
  available.  Note that this is a gap in the Buffer accessors only:
  ``BigInt`` itself **is** implemented, so ``BigInt`` values may be
  used freely in JavaScript; they simply cannot be read from or
  written to a Buffer with these methods.

Examples
''''''''

.. code-block:: javascript

    /* hashing & base64 round-trip */
    var crypto = require("rampart-crypto");
    var hash = crypto.sha256("hello world", {returnType: "buffer"});
    var b64  = hash.toString('base64');
    var same = Buffer.from(b64, 'base64').equals(hash);   // true

    /* writing into a fixed-size buffer */
    var b = Buffer.alloc(8);
    b.write('aabbcc', 'hex');       // b is <Buffer aa bb cc 00 00 00 00 00>

    /* finding bytes */
    var needle = Buffer.from('world');
    var pos = Buffer.from('hello world').indexOf(needle);   // 6

    /* swap endianness */
    var u32 = Buffer.from([0x01,0x02,0x03,0x04]);
    u32.swap32();                                           // [0x04,0x03,0x02,0x01]

TypedArray Methods
""""""""""""""""""

Rampart attaches the ES2015+ iteration helpers to the shared
:green:`%TypedArray%.prototype`, so every typed array
(``Uint8Array``, ``Int8Array``, ``Float32Array``, …) and ``Buffer``
inherits them in one shot:

``forEach``, ``map``, ``filter``, ``reduce``, ``reduceRight``, ``some``,
``every``, ``find``, ``findIndex``, ``findLast``, ``findLastIndex``,
``at``, ``slice``, ``fill``, ``copyWithin``, ``reverse``, ``sort``
(numeric, not lexicographic), ``indexOf``, ``lastIndexOf``,
``includes``, ``join``, ``keys``, ``values``, ``entries``, and
``[Symbol.iterator]`` (aliased to ``values``).  ``Buffer``'s richer
string-aware ``indexOf`` / ``lastIndexOf`` / ``includes`` shadow these
for ``Buffer`` instances via the prototype chain.

TextEncoder / TextDecoder
"""""""""""""""""""""""""

Rampart provides ``TextEncoder`` and ``TextDecoder`` modeled on the
WHATWG encoding spec.  Supported encodings:
``utf-8``, ``utf-16le``, ``utf-16be``, ``iso-8859-1`` (also
``latin1``, ``binary``), ``us-ascii``, plus the standard aliases
(``ucs-2``, ``csisolatin1``, ``iso646-us``, …).  Unknown labels throw
``RangeError``.  Input may be a ``Buffer``, ``Uint8Array``,
``ArrayBuffer``, or any ``TypedArray`` view.

.. code-block:: javascript

   new TextEncoder().encode('hello');                /* Uint8Array of 5 bytes */
   new TextDecoder('utf-16le').decode(buf);          /* decode wide chars   */

Map and Set
"""""""""""

Rampart provides full ES2015 ``Map`` and ``Set``:

* ``Map``: ``set``, ``get``, ``has``, ``delete``, ``clear``,
  ``forEach``, ``entries``, ``keys``, ``values``, ``size`` (getter),
  and ``[Symbol.iterator]`` → ``entries``.
* ``Set``: ``add``, ``has``, ``delete``, ``clear``, ``forEach``,
  ``values``, ``keys``, ``entries``, ``size`` (getter), and
  ``[Symbol.iterator]`` → ``values``.

Both constructors accept any iterable for initial population.
Iterators produce ``{value, done}`` result objects and are themselves
iterable.

Note that ``[Symbol.iterator]`` is an equivalent function, but not the
identical function object -- that is,
``map[Symbol.iterator] === map.entries`` is ``false``, where the ES2015
specification requires it to be ``true``.  Iteration behaves correctly;
only an identity comparison between the two will differ.

console Extras
""""""""""""""

Alongside the standard ``log`` / ``error`` / ``warn`` / ``info`` /
``debug`` / ``trace`` / ``dir`` / ``assert``, Rampart adds the
following node-style methods:

* ``console.time(label?)`` / ``timeLog(label?, ...extras)`` /
  ``timeEnd(label?)`` — millisecond timers; label defaults to
  ``"default"``.
* ``console.count(label?)`` / ``countReset(label?)``.
* ``console.group(label?)`` / ``groupCollapsed(label?)`` /
  ``groupEnd()`` — indent-based grouping.
* ``console.clear()``.
* ``console.table(data, columns?)`` — tabular formatter.

Proxy.revocable
"""""""""""""""

``Proxy.revocable(target, handler)`` returns ``{proxy, revoke}``.
Calling ``revoke()`` makes every subsequent trap on ``proxy`` throw
``TypeError``.  As with ``new Proxy()``, ``handler`` may be empty, in
which case operations pass through to ``target``.

Reflect
"""""""

``Reflect`` is available, with ``get``, ``set``, ``has``,
``deleteProperty``, ``ownKeys`` and the rest of the standard methods.

One limitation: duktape does not implement the optional ``receiver``
argument of ``Reflect.get()`` and ``Reflect.set()``.  Calling either with
a ``receiver`` that is not the target throws ``Error: unsupported``:

.. code-block:: javascript

   Reflect.get(obj, "key");                 /* ok */
   Reflect.get(obj, "key", otherObj);       /* throws Error: unsupported */

   Reflect.set(obj, "key", val);            /* ok */
   Reflect.set(obj, "key", val, otherObj);  /* throws Error: unsupported */

The ``receiver`` only affects which object ``this`` refers to inside a
getter or setter, so omitting it matters only for accessor properties.
``Reflect.has()``, ``Reflect.deleteProperty()`` and ``Reflect.ownKeys()``
take no ``receiver`` and are unaffected.

WHATWG / W3C Web Platform APIs (experimental)
"""""""""""""""""""""""""""""""""""""""""""""

Rampart includes a substantial subset of browser Web Platform globals
— ``fetch``, ``URL``, ``Headers``/``Request``/``Response``/``FormData``,
``Blob``/``File``, the ``ReadableStream`` / ``WritableStream`` /
``TransformStream`` family, ``WebSocket``, ``XMLHttpRequest``,
``crypto`` (Web Crypto), ``structuredClone``, ``queueMicrotask``,
``EventSource``, ``localStorage`` / ``sessionStorage``, and more.

Like ``Intl``, the surface is **lazy-loaded**: ``rampart-whatwg.so`` is
only ``dlopen()``-ed when JS first references one of these names.
Scripts that never touch a Web Platform API pay zero startup cost.

Conformance is **partial and experimental** — strongest for APIs that
don't assume a browser/DOM context (Web Crypto, URL, mimesniff, data:
URLs, value-stream APIs, HTTP/1.1 ``fetch``); weaker or absent for
anything needing ``document`` / ``window`` / ``iframe``, ``WebAssembly``,
HTTP/2 streaming upload, or ``br`` / ``zstd`` content-encoding (servers
fall back to ``gzip``).  Behaviors may change as the implementation
tracks the WPT suite.

Intl
""""

``globalThis.Intl`` is installed as a lazy getter — on first access
it loads the ICU4C-backed ``rampart-intl`` module (~37 MB of bundled
locale data), which assigns ``globalThis.Intl``.  Scripts that never
touch ``Intl`` pay nothing.

All eight ECMA-402 constructors are available once loaded:
``DateTimeFormat``, ``NumberFormat``, ``Collator``, ``PluralRules``,
``RelativeTimeFormat``, ``ListFormat``, ``DisplayNames``, ``Locale``,
``Segmenter``, ``DurationFormat``, plus ``Intl.getCanonicalLocales``
and ``Intl.supportedValuesOf``.  Behaves per
`MDN's Intl reference <https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl>`_.

Other Globals
"""""""""""""

* ``globalThis`` and ``global`` — both reference the global object
  (Rampart adds the node-style ``global`` alias alongside
  ``globalThis``).
* ``JSON.parse(text[, reviver])`` — extended to accept a
  :green:`Buffer` as ``text``.  When ``reviver === true``, walks the
  parsed tree and resolves ``{"_cyclic_ref":"$.path"}`` placeholders
  produced by ``rampart.utils.sprintf("%!J", obj)`` back into shared
  references, restoring cyclic graphs.  This is a **rampart-specific**
  reviver convention; passing a function reviver behaves per spec.
* ``eval`` — when the transpiler is in effect (``-t`` or
  ``"use transpiler"``), the source is lowered before evaluation.
  Otherwise behaves as standard ES5 ``eval``.
* ``new Function(...)`` — bodies are run through the transpiler so
  ES2015+ syntax (classes, ``async``, generators) compiles inside
  dynamically-built functions.

setTimeout()
""""""""""""

Also added to Rampart is the ``setTimeout()`` function.  It supports the
asynchronous calling of functions from within Rampart's event loop in the same
manner as ``setTimeout`` in ``node.js`` or a browser such as Firefox or Chrome.

Usage:

.. code-block:: javascript

   var id = setTimeout(callback, timeOut[, arg1, arg2, ..., argn]);

Where:

* ``callback`` is a :green:`Function` to be run when the elapsed time is reached.
* ``timeOut`` is the amount of time in milliseconds to wait before the ``callback`` function is called.
* ``argX`` are arguments to be passed to the callback function. 

Return Value:
    An id (an :green:`Object`, not a :green:`Number`) which may be used with
    `clearTimeout()`_\ .

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
until canceled via `clearInterval()`_.

Usage:

.. code-block:: javascript

   var id = setInterval(callback, interval[, arg1, arg2, ..., argn]);

Where:

* ``callback`` is a :green:`Function` to be run when the elapsed time is reached.
* ``interval`` is the amount of time in milliseconds between calls to ``callback``.
* ``argX`` are arguments to be passed to the callback function. 

Return Value:
    An id (an :green:`Object`, not a :green:`Number`) which may be used with
    `clearInterval()`_\ .

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

   var id = setMetronome(callback, interval[, arg1, arg2, ..., argn]);

Where:

* ``callback`` is a :green:`Function` to be run when the elapsed time is reached.
* ``interval`` is the amount of time in milliseconds between calls to ``callback``.
* ``argX`` are arguments to be passed to the callback function. 

Return Value:
    An id (an :green:`Object`, not a :green:`Number`) which may be used with
    `clearMetronome()`_\ .

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
* `performance <https://duktape.org/guide.html#builtin-performance>`_

(``TextEncoder`` and ``TextDecoder`` are documented above under
`TextEncoder / TextDecoder`_.)

For more information, see the `Duktape Guide <https://duktape.org/guide.html>`_

.. _babeljs:

ECMAScript 2015+ and Babel.js
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Babel Acknowledgement
"""""""""""""""""""""

Rampart **experimentally** uses `Babel.js <https://babeljs.io/>`_ to support a
greater breadth of JavaScript syntax and functionality.  Babel.js is a
toolchain that converts ECMAScript 2015+ (and optionally TypeScript) code
into a version of JavaScript compatible with Duktape.  The authors of
Rampart are extremely grateful to the 
`Babel development team <https://babeljs.io/team>`_.

Babel License
"""""""""""""

Babel.js is 
`MIT licensed <https://github.com/babel/babel/blob/main/LICENSE>`__. 

Activating Babel
""""""""""""""""

A slightly modified version of babel.js (currently babel-standalone v
7.12.17) and the associated collection of polyfills (babel-polyfill.js) are
included in the Rampart distribution.  To use ECMA 2015+ features of
JavaScript, simply include the following at the beginning of the script:

.. code-block:: javascript

   "use babel"

    /* or */

    "use babelGlobally" //run included modules through babel as well

Note that the ``"use babel"`` or ``"use babelGlobally"`` string should be the
first JavaScript text in the script.  However it may come after any comments
or a hash-bang line.  It also should be the only text on the line, other
than an optional comment.

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
options on the same line.  Without options ``"use babel"`` is equivalent
to (shown wrapped for readability; the directive itself must be on one
physical line)::

   "use babel:{ presets: ['env'],
                plugins: ['proposal-class-properties',
                          'proposal-private-methods',
                          'proposal-private-property-in-object',
                          'proposal-nullish-coalescing-operator',
                          'proposal-optional-chaining'],
                retainLines: true }"

The default plugin list enables ES2020/2022 class fields, private
fields/methods, nullish coalescing (``??``), and optional chaining
(``?.``) on top of ``preset-env``.  Specifying any explicit ``plugins:``
or ``presets:`` in the directive replaces this default — include the
plugins you need in the override.  See
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
``/usr/local/rampart/test/babel-test.js`` file (also available
`here <https://github.com/aflin/rampart/blob/main/test/babel-test.js>`__\ .

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
``*.babel.js`` file, the transpile stage will be skipped and the
transpiled script will be run directly.

Caveats
"""""""

For a complicated script, the transpile stage can be very slow.  However if
the script has not changed since last run, the execution speed will be
normal as the cached/transpiled code will be used and thus no transpiling
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

ECMAScript 2015+ with transpiler
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Tree-sitter Acknowledgement
"""""""""""""""""""""""""""

Rampart **experimentally** uses the `tree-sitter <https://github.com/tree-sitter/tree-sitter>`_
and the `tree-sitter-javascript <https://github.com/tree-sitter/tree-sitter-javascript>`_
libraries to transpile a limited set of post-ES5 JavaScript.  The authors of Rampart are
indebted to the `tree-sitter contributors <https://github.com/tree-sitter/tree-sitter-javascript/graphs/contributors>`_
for this indispensable library.  

Tree-sitter License
"""""""""""""""""""

Tree-sitter is `MIT licensed <https://github.com/tree-sitter/tree-sitter-javascript?tab=MIT-1-ov-file#readme>`__\ .

.. _activating-the-transpiler:

Activating the Transpiler
"""""""""""""""""""""""""

To use the limited ECMA 2015+ features covered by the transpiler,
simply include the following at the beginning of the script:

.. code-block:: javascript

   "use transpiler"

    /* or */

    "use transpilerGlobally" //run included modules through the transpiler as well

Note that the ``"use transpiler"`` or ``"use transpilerGlobally"`` string should be the
first JavaScript text in the script.  However it may come after any comments
or a hash-bang line.  It also should be the only text on the line, other
than an optional comment.

Transpiler Options
""""""""""""""""""

The ``"use transpiler"`` (or ``"use transpilerGlobally"``) directive
optionally takes a ``:`` followed by an object literal of options.  The
object literal uses JavaScript object literal syntax (unquoted identifier
keys, ``true``/``false`` values).

Currently the only supported option is ``functionSources``:

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Option
     - Default
     - Description
   * - ``functionSources``
     - ``true``
     - When enabled (the default), the transpiler captures the original
       pre-transpile source text of each top-level ``function`` declaration,
       ``function`` expression, ``arrow_function``, ``generator_function``
       and ``async function``, and attaches it to the function so that
       ``fn.toString()`` returns the original source written by the user.
       Duktape's built-in ``Function.prototype.toString()`` otherwise
       returns ``"function NAME() { [ecmascript code] }"``, which is often
       not useful for debugging, logging or hot-reload tooling.

       Setting this option to ``false`` disables the capture and reverts
       to Duktape's built-in behavior.  Disable it if you want the smaller
       transpiled output (each captured function is stored twice — once as
       executable code, once as a string literal — which roughly doubles
       the output size for function-heavy source).

Examples:

.. code-block:: javascript

   /* default — fn.toString() returns the original source */
   "use transpiler"

   function greet(name) { return "hi " + name; }
   console.log(greet.toString());
   // -> function greet(name) { return "hi " + name; }


.. code-block:: javascript

   /* opt-out — fn.toString() uses duktape's built-in form */
   "use transpiler:{functionSources:false}"

   function greet(name) { return "hi " + name; }
   console.log(greet.toString());
   // -> function greet() { [ecmascript code] }


Note: function-source capture currently applies to ``function``
declarations, ``function`` expressions, ``arrow`` functions,
``generator`` functions and ``async`` functions.  Methods inside
``class`` bodies and object-literal method shorthand are not yet
covered; their ``toString()`` still returns the built-in form.

Example:

Please see the `transpile-test.js file <https://github.com/aflin/rampart/blob/main/test/transpile-test.js>`_
to see which features are currently supported (and the disabled tests which show
yet to be completed support).

Why use the transpiler instead of Babel?
""""""""""""""""""""""""""""""""""""""""

Though babel has better coverage of post-ES5 JavaScript, the transpiler is
several orders of magnitude faster than babel.  Both save their transpiled
output to disk (e.g., ``myscript.transpiled.js`` or ``myscript.babel.js``)
and reuse it on subsequent runs if the source has not changed.
In addition, the transpiler keeps the `Extra JavaScript Functionality`_ listed above.

Skipping a second pass: the ``"noTranspile"`` directive
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both the transpiler and babel save their processed output to disk
(``myscript.transpiled.js`` / ``myscript.babel.js``) and prefix that
output with a ``"noTranspile";`` directive.  A file that carries this
directive is treated as already-processed: it is loaded as-is and is
**not** run through the transpiler or babel a second time, even under
``-t`` / ``"use transpilerGlobally"`` (or ``-b`` / ``"use babelGlobally"``).

This matters when an already-transpiled/babelized file is renamed to a
plain ``.js`` and ``require()``\ d from a script that has global
transpilation or babel enabled — without the marker it would be
re-processed unnecessarily.

You can also place the directive in your own source to exempt a file from
processing entirely:

.. code-block:: javascript

   "noTranspile"

Like the other directives, it must be the first JavaScript text in the
file — it may follow a hash-bang line and any comments, and it coexists
with other leading directives such as ``"use strict"``.  At runtime it is
an inert string-literal statement.
