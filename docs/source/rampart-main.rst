Introduction to Rampart
-----------------------

Acknowledgement
~~~~~~~~~~~~~~

Rampart uses the `Duktape Javascript Engine <https://duktape.org>`_. It is an 
embeddable Javascript engine, with a focus on portability and compact footprint.
The developers of Rampart are extremely grateful for the excellent api and
ease of use of this library.

License
~~~~~~~

Duktape and the Core Rampart program are MIT licensed.

Core features of Duktape
~~~~~~~~~~~~~~~~~~~~~~~~

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
~~~~~~~~~~~~~~~~~

In addition to the standard features in Duktape javascript, Rampart adds the
following:

* Standard module support for ``C`` and ``Javascript`` modules via the
  ``require()`` function.

* File and c-functions utilities such as ``printf``, ``fseek``, and ``exec``.

* Included ``C`` modules (``rampart-curl``, ``rampart-sql``, ``rampart-server`` and
  ``rampart-crypto``).

* Event loop using ``libevent2``

* ECMA 2015, 2016, 2017 and 2018 support using the `Babel <https://babeljs.io/>`_
  javascript transpiler.

* Full Text Search and Sql databasing via ``rampart-sql``.

* Multithreaded execution http server via ``rampart-server``.

* http, ftp, etc. functionality via ``rampart-curl``.

* Cryptography functions from openssl via ``rampart-crypto``.

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
stacks, all in a single product. Further, it provides Full Text Search functionality
through the :ref:`sqltoc:The rampart-sql module` which far surpasses that of
the nearest competition in terms of ease of use, speed and quality of
results.

The rampart Global Variable
~~~~~~~~~~~~~~~~~~~~~~~~~~~


rampart.utils
"""""""""""""

These functions bring file io and other functionality to Duktape javascript.

printf
''''''

Provides standard printf functionality in javascript.  Flags include

Usage:

::

   printf(fmt, ...)
   
Standard flags:

* %s - corresponding argument is treated as a string (converted if
  necessary)

* %J -

fopen
'''''

fclose
''''''

seek
''''


