The rampart-server HTTP module
==============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-server module uses the 
`libevhtp <https://github.com/criticalstack/libevhtp>`_ library,
a fast, embedded, event driven http/https server 
which itself uses the `libevent2 <https://libevent.org/>`_ library.
 
The developers of Rampart are extremely grateful for the excellent APIs and ease
of use of these libraries.

License
~~~~~~~

The rampart-server module is released under the MIT license. 
The `libevhtp <https://github.com/criticalstack/libevhtp>`_ library
and the `libevent2 <https://libevent.org/>`_ library
are both provided under a 
`BSD-3-Clause License <https://github.com/criticalstack/libevhtp/blob/develop/LICENSE>`_\ .

What does it do?
~~~~~~~~~~~~~~~~

The rampart-server module provides a multithreaded and flexible http/https webserver
which is started from, configured in and maps urls to Rampart JavaScript functions.
It also can be configured to serve files from the filesystem.


How does it work?
~~~~~~~~~~~~~~~~~

Once the module is loaded and configuration parameters are passed to the
`start()`_ function, the server maps paths to the filesystem, JavaScript
functions, :ref:`JavaScript Modules <rampart-main:Using the require Function to Import Modules>` 
and/or directories containing 
:ref:`JavaScript Modules <rampart-main:Using the require Function to Import Modules>`.
Modules allow scripts and functions to reside in separate files.  
If changes are made to a module, the server does not need to be 
restarted for the changes to take effect.

The server can operate from a pool of threads, taking advantage of systems
with multiple CPUs.  As JavaScript is inherently single-threaded, when the
server is started in multi-threaded mode, a new JavaScript stack, heap and
context is created for each thread.  Global variables, global functions
and mapped functions passed to `start()`_ are automatically copied to each
context and each runs independently.  Modules are likewise loaded from
within each thread and checked for changes upon each request.

It is worth noting that not all rampart-sql functions are thread safe.  When
sql functions are executed from within a function or module, rampart may
create a fork, one per thread, in order to handle such functions.

A timeout for script execution may also be set.  Should the script timeout,
the serving thread will cancel the request and re-initialize the JavaScript
context in order to serve new requests.  Should the script timeout from
within a forked process, that process will be terminated and the thread will
fork again upon the next request.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the server modules is a simple matter of using the require
statement:

.. code-block:: javascript

   var server = require("rampart-server");

It returns an :green:`Object` with a single function: 

::

   {
      start:    {_func:true}
   }


Configuring and Starting the Server
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

start()
"""""""

The server is configured and started using the ``start()`` function. 
Options are passed to the function in a single :green:`Object`.

Usage:

.. code-block:: javascript

   var server = require("rampart-server");

   server.start(Options);  

Where:
   ``Options`` is an :green:`Object` with the following properties:

    * ``bind`` - An :green:`Array` of :green:`Strings`, with each :green:`String`
      representing an ip address and port.  If not specified, the server will 
      bind to port 8088 on the loopback device (i.e. 127.0.0.1, which is only
      accessible from the same machine), using the following default value:
      
      ``[ "[::1]:8088", "127.0.0.1:8088" ]``. 
      
      When specifying an Ipv6 address, bracket notation is required (e.g. 
      ``[2001:db8::1111:2222]:80``) while a dot-decimal notation is used for
      ipv4 (e.g. ``172.16.254.1:80``).  To bind to all ip addresses using port 80,
      the following may be used: 
      
      ``[ "[::]:80", "0.0.0.0:80" ]``.

    * ``scriptTimeout``: A :green:`Number`, amount of time in seconds (or fraction
      thereof) to wait for a script to run before cancelling the request and
      returning a ``500 Internal Server Error`` timeout message to the
      connecting client.  Default is no timeout/unlimited.

    * ``connectTimeout``: A :green:`Number`, amount of time in seconds (or fraction
      thereof) to wait for a connected client to send a request. Default is no
      timeout/unlimited.

    * ``log``: A :green:`Boolean`, whether to log each request.  Access requests
      are logged to ``stdout`` and errors are logged to ``stderr`` unless
      ``accessLog`` and/or ``errorLog`` below are set.

    * ``accessLog``: A :green:`String`, the location of the access log.  The
      default, if not specified is to log to ``stdout``.
      
    * ``errorLog``: A :green:`String`, the location of the error log.  The
      default, if not specified is to log to ``stderr``.

    * ``daemon``: A :green:`Boolean`, whether to fork and detach from the
      controlling terminal.  If ``true``, the ``start()`` function will return
      the pid of the server. Otherwise the pid of the current process is
      returned. The default is ``false``.

    * ``useThreads``: A :green:`Boolean`, whether the server is multi-threaded. 
      If ``true`` and ``threads`` below is not set, the server will create a
      threadpool consisting of one thread per cpu core.  If set ``false``, it is
      equivalent to setting ``useThreads`` to ``true`` and ``threads`` to ``1``.
      The default is ``true``.

    * ``threads``: A :green:`Number`, the number of threads to create for the
      server thread pool.  The default, if ``useThreads`` is ``true`` or is
      unset, is the number of cpu cores on the current system.

    * ``secure``: A :green:`Boolean`, whether to use SSL/TLS layer for serving
      via the ``https`` protocol.  Default is ``false``.  If ``true``, the
      ``sslKeyFile`` and ``sslCertFile`` parameters must also be set.

    * ``sslKeyFile``: A :green:`String`, the location of the ssl key file for
      serving via the ``https`` protocol.  An example, if using 
      `letsencrypt <https://letsencrypt.org/>`_ for "example.com" might be
      ``"/etc/letsencrypt/live/example.com/privkey.pem"``.  This setting has
      no effect unless ``secure`` is ``true``.

    * ``sslCertFile``: A :green:`String`, the location of the ssl cert file for
      serving via the ``https`` protocol.  An example, if using 
      `letsencrypt <https://letsencrypt.org/>`_ for "example.com" might be
      ``"/etc/letsencrypt/live/example.com/fullchain.pem"``.  This setting has
      no effect unless ``secure`` is ``true``.

    * ``sslMinVersion``:  A :green:`String`, the minimum SSL/TLS version to use. 
      Possible values are ``ssl3``, ``tls1``, ``tls1.1`` or ``tls1.2``.  The
      default is ``tls1.2``. This setting has no effect unless ``secure`` is ``true``.

    * ``notFoundFunc``: A :green:`Function` to handle ``404 Not Found`` responses.
      See `Mapped Functions`_ below.

    * ``developerMode``: A :green:`Boolean`, whether to run the server in a
      developer mode.  If ``true``, JavaScript and other errors will cause
      the server to return a ``500 Internal Error`` message, with the error
      and error line numbers printed.  If false, JavaScript errors will
      result in the generic ``404 Not Found Page`` or alternatively, if set
      ``directoryFunc`` will be called and the request object (``req``) will
      contain the key ``errMsg`` (``req.errMsg``), with the error message. 

    * ``directoryFunc``: A :green:`Function` to handle directory listings from
      the filesystem, if no ``index.html`` file exists in the requested
      directory.  May also be set to ``true`` to use the built-in function.
      If set ``false`` (the default), a "404 Forbidden" response is sent
      where a directory listing is requested and no index.html file exists.
      See `Built-in Directory Function`_ below for more information.

    * ``user``: A :green:`String`, the user account which the server will switch 
      to after binding to the specified ip address and port.  Only valid if
      server is started as ``root``.  This setting is used for binding to
      privileged ports as ``root`` and then dropping privileges.  If the server
      is started as root, ``user`` must be set.

    * ``bufferMem``: A positive :green:`Number`.  If equal to or below 100, 
      the percent of system memory to use for buffers for printing
      directly to the client.  If above 100, the amount in kilobytes of
      system memory to use for buffers for printing directly to the client.
      This amount is divided by the number of threads, with each thread
      using a buffer of the resulting size.  The default, if not specified
      is ``10`` (10% of the system's physical memory).  See 
      `Advanced Functions`_ below.

    * ``cacheControl``: A :green:`String` or a :green:`Boolean`.  If a
      :green:`String` - the text to set the "Cache-Control" header when
      serving files off of the filesystem.  The default is "max-age=84600,
      public", if not set or set ``true``.  If set ``false``, no header is
      sent. 

    * ``mimeMap``: An :green:`Object`, additions or changes to the standart extension
      to mime mappings.  Normally, if, e.g., ``return { "m4v": mymovie };`` is
      set as `The Return Object`_ to a mapped function, the header
      ``content-type: video/x-m4v`` is sent.  Thought the ''content-type" header
      can be changed using the ``headers`` object in `The Return Object`_\ , it
      does not affect files served from the filesystem. If it is necessary to change
      the "content-type" for both `Mapped Functions`_ and files served from
      `Mapped Directories`_\ , extension:mime-types mappings may be set or changed as follows:
      
      .. code-block:: javascript
      
          server.start({
              ...,
              mimeMap: {
                  /* make these movies play as mp4s */
                  "m4v": "video/mp4",
                  "mov": "video/mp4"
              },
              map: {
                  "/": "/var/www/html",
                  ...,
              }
          });

      For a complete list of defaults, see `Key to Mime Mappings`_ below.

    * ``mapSort``: A :green:`Boolean`, whether to automatically sort the
      mapped paths given as keys to the :green:`Object` passed to ``map`` below. 
      Default is ``true``.  If ``false``, paths from the ``map`` :green:`Object`
      will be matched in the order they are given.  

      Note that regardless of this setting, paths are match by type of path (see
      below) with Exact paths tested first, then regular expression paths and
      lastly glob paths.  However, it is usually desirable for longer paths to
      have priority over shorter ones.  For example, if ``/`` and
      ``/search.html`` are both specified (both are "Exact" paths),
      ``/search.html`` should be checked first, otherwise ``/`` will match and
      ``/search.html`` will never match.  When ``mapSort`` is ``true``,
      key/paths are automatically sorted by length.
      
    * ``map``: An :green:`Object` of url to function or filesystem mapping.
      The keys of the object are exact paths, regular expressions, partial
      paths or globbed paths to be matched against incoming requests.  For
      example, a key ``/myscript.html`` would match an incoming request for
      ``http://example.com/myscript.html``.  The value to which the key is
      set controls which function, module or filesystem path will be used.
      
      If the value is a :green:`Function`, that function is used as
      the callback function.  If the value is an :green:`Object` with
      ``module`` or ``modulePath`` key set, it is assumed to 
      be a script name (the same as is used for 
      :ref:`require() <rampart-main:using the require function to import modules>`)
      or a path with scripts.
      
      If the value is a :green:`String`, or it is an :green:`Object` with
      ``path`` set, it is assumed to be a mapping to the filesystem.  A
      mapping to a filesystem path may also include headers.
      
      Example:

      .. code-block:: javascript

        var server = require("rampart-server");

        var pid = server.start({
            bind: [ "[::]:8088", "0.0.0.0:8088" ], /* bind to all */
            map : 
            {
                "/":            "/usr/local/etc/httpd/htdocs"  /* map all file requests */
                "/search.html": function (req) { ... },         /* search function */
                "/images/":     {
                                    path: "/path/to/my/jpgs/",
                                    headers: {
                                        "Content-Control": "max-age=31556952, public",
                                        "X-Custom-Header": 1
                                    }
                                }
            }
        });

      In the above example, the ``"/search.html"`` key will have priority over
      ``"/"`` key, so that a request ``http://localhost:8088/search.html`` will
      cause the function to be executed while anything else will match ``"/"``
      (assuming ``mapSort`` is not set to ``false``).

      Keys/paths used for mapping a :green:`Function` may be given in one of
      four different formats, which are tested for a match in the following order:
       
      * Exact Paths - Paths starting with a "/" and having no unescaped ``*`` characters
        will be matched exactly with the incoming request.

      * Regular Expression paths - A path/key that starts with ``~`` will match the
        Perl Regular Expression following the ``~``.  Example: 
        ``map: {"~/.*/myfile.html": myfunction }`` will match any path ending
        in ``myfile.html`` and run the named function ``myfunction``.
       
      * Glob Paths - A glob path will have the last priority for matching the
        requested url.  Example: ``map: {"/*/myfile.html": myfunction2 }`` will
        match the same as the example above, but would have lower priority.  If
        both these examples were present, ``myfunction2`` would never match.

      Keys/paths used for mapping to the **filesystem** are always taken as an Exact path. 
      Regular expressions and globs are not allowed.

Return Value
  A :green:`Number`, the pid of the current process, or if ``daemon`` is
  set to ``true``, the pid of the forked server.

Server Usage Details
--------------------

Path Mapping
~~~~~~~~~~~~

  Path mapping using the ``map`` property in `start()`_ above may be used to
  map URL paths to both :green:`Functions` and to a directories on the local
  filesystem.

Mapped Functions
""""""""""""""""

  A mapped function may be expressed in one of several ways.
  
  * Inline function: ``map: {"/search.html": function(res) { ... } }``.
  
  * A Global function: ``map: {"/search.html": myfunc }`` where ``myfunc`` is a
    function declared **globally** in the current script.
  
  * A module with ``module.exports`` set to the desired function.   Example:
    ``map: {"/search.html" : {module:"mysearchmod"} }`` where mysearchmod.js is
    in the current directory or in the module's search path.

  * A directory of modules where the directory contains one or more modules
    with ``module.exports`` set to functions.  Example:
    ``map: {"/scripts/": {modulePath: "/path/to/myscriptsdir/"} }``.  In this
    case, if ``/path/to/myscriptsdir/mymod.js`` script exists, it might be
    available from the URL ``http://localhost:8088/scripts/mymod.ext`` 
    where ``.ext`` can be ``.html``, ``.txt`` or any other extension desired.
    Note that regardless of the extension used, the mime-type is set
    in `The Return Object`_\ .

  For normal use, it is always preferable to use modules.  The
  advantage of using modules is that they can be changed at any time without
  having to restart the server and that variables declared in the module
  have their scopes appropriately set.
  
  See :ref:`rampart-main:Using the require Function to Import Modules` 
  for details on writing and using modules.

  It is also important to note that only global variables and functions, and
  inline functions are copied to each JavaScript context for each server
  thread.  Any other variable or function that might otherwise appear to be
  in scope when ``server.start()`` is executed will not be available from
  within each server thread.  This is true regardless of the state of ``useThreads``
  setting above.  Any semantic confusion that might be caused by this
  limitation can be mostly avoided by placing functions in separate scripts
  as modules, since variables declared in the module will be available and
  properly scoped (though separately and distintly; variables are not shared
  between threads).

  Example of a scoped variable that would not be available:
  
  .. code-block:: javascript
     
    var server = require("rampart-server");

    function startserver() {
       var html = "<pre>HELLO WORLD!</pre>";

       return server.start({
           map: {
               "/myfunc.html": function(){ return {html:html}; }
           }
       });
    }

    var pid=startserver();

          
    /* result from http://localhost:8088/myfunc.html:
          Internal Server Error
          ReferenceError: identifier 'html' undefined
            at [anon] (duk_js_var.c:1236) internal
            at [anon] (test-server.js:8) preventsyield
    */
    

  Note that if ``var html`` was declared globally (e.g. directly after 
  ``var server`` line), the function would not throw an error.
 
  Example of local variables that are available in a module:
  
  .. code-block:: javascript
  
    /* mymod.js */

    var html = "<pre>HELLO WORLD!</pre>";

    module.exports = function(){ return {html:html}; }

  With the main script containing:

  .. code-block:: javascript

    /* test-server.js */

    var server=require("rampart-server");

    var pid = server.start({
      map: {
        "/myfunc.html": {module:'mymod'}
      }
           
    });

  In the above example, ``var html`` would be set once when the module is
  loaded.  It is then accessible from the exported function and its scope is
  limited to the ``mymod.js`` file.

Mapped Directories
""""""""""""""""""

  Mapped Directories are specified by setting the value of a path key to a
  :green:`String`, where the :green:`String` is the name of the directory on
  the current filesystem to use:
  
  .. code-block:: javascript

      var server = require("rampart-server");

      var pid = server.start({
          map: {
            "/"   : "/var/www/html",
            /* trailing '/' in '/css' is implied */
            "/css": "/usr/local/etc/httpd/css"
          }
      });

  Mapped directories may also be mapped using the following syntax, which allows for custom headers
  to be sent with each file served:
  
  .. code-block:: javascript

      var server = require("rampart-server");

      var pid = server.start({
          map: {
            "/"   : {
                path: "/var/www/html",
                headers: {
                    "X-Custom-Header-1": "myval1",
                    "X-Custom-Header-2": "myval2"
                }
            },
            "/css/": "/usr/local/etc/httpd/css"
          }
      });
  
  In the above example, all the files in ``/var/www/html/*`` would be mapped
  to ``http://localhost:8088/*`` including any subdirectories.  However,
  ``http://localhost:8088/css/*`` is mapped from
  ``/usr/local/etc/httpd/css/*`` even if a ``/var/www/html/css/``
  directory exists.

  Note that globs and regular expressions are not allowed for mapped
  directories.  Note also that keys for mapped directories are always
  treated as directories and have a trailing ``/`` added if not present. 
  If, e.g., ``map:{"/file.html":"/my/dir"}`` was specified,
  ``http://localhost:8088/file.html`` would return "NOT FOUND" but URLs
  beginning with ``http://localhost:8088/file.html/`` would return files
  from ``/my/dir/``.

The Request Object
~~~~~~~~~~~~~~~~~~

  Mapped :green:`Functions` are passed a single :green:`Object` which contains the details
  of the request.  For example, if the url
  ``http://localhost:8088/showreq.html?q=search+terms`` is requested 
  (with a couple of cookies set), the
  object passed to the function might look something like this:
  
  .. code-block::  javascript

        {
           "ip": "::1",
           "port": 33948,
           "method": "GET",
           "path": {
              "file": "showreq.html",
              "path": "/showreq.html",
              "base": "/",
              "scheme": "http://",
              "host": "localhost:8088",
              "url": "http://localhost:8088/showreq.html?q=search+terms"
           },
           "query": {
              "q": "search terms"
           },
           "body": {},
           "query_raw": "q=search+terms",
           "cookies": {
              "mycookie": "cookietext",
              "cookiewquote": "my\"cookie\""
           },
           "headers": {
              "Host": "localhost:8088",
              "Connection": "keep-alive",
              "DNT": "1",
              "Upgrade-Insecure-Requests": "1",
              "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36",
              "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
              "Sec-Fetch-Site": "none",
              "Sec-Fetch-Mode": "navigate",
              "Sec-Fetch-User": "?1",
              "Sec-Fetch-Dest": "document",
              "Accept-Encoding": "gzip, deflate, br",
              "Accept-Language": "en-US,en;q=0.9",
              "Cookie": "mycookie=cookietext; cookiewquote=my\"cookie\""
           },
           "params": {
              "q": "search terms",
              "mycookie": "cookietext",
              "cookiewquote": "my\"cookie\"",
              "Host": "localhost:8088",
              "Connection": "keep-alive",
              "DNT": "1",
              "Upgrade-Insecure-Requests": "1",
              "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36",
              "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9",
              "Sec-Fetch-Site": "none",
              "Sec-Fetch-Mode": "navigate",
              "Sec-Fetch-User": "?1",
              "Sec-Fetch-Dest": "document",
              "Accept-Encoding": "gzip, deflate, br",
              "Accept-Language": "en-US,en;q=0.9",
              "Cookie": "mycookie=cookietext; cookiewquote=my\"cookie\""
           }
        }

  The above example could be printed out to the client using the following function:

  .. code-block:: javascript

        server.start(
        {
            ...,
            map : {
                "/showreq.html" : function(req) {
                return( { txt: rampart.utils.sprintf("%3J",req) } );
              }
            }
        });

  Note that the ``params`` key is an :green:`Object` with properties set to an
  amalgam of all the useful variables sent from the client.  It includes
  variables from headers, cookies, GET query parameters and POST data,
  prioritize in that order.  If, e.g., a query parameter has the same name
  as a cookie, the cookie value will override the the query parameter.


Posting Form Data
"""""""""""""""""

    When posting form data, the request object will include an additional
    property ``postData``, which will contain the parsed content of the
    posted form as well as the ``Content-Type`` which will be set to
    ``"application/x-www-form-urlencoded"``.  The ``postData`` ``content``
    will also be copied to ``params``, so long as there are no name
    collisions between those keys and variables set from cookies, headers or
    query parameters.  The raw posted content will be returned in the
    property ``body`` as a :green:`Buffer`.  Example:

    .. code-block:: javascript

        server.start(
        {
            ...,
            map : {

                "post.html": function(){
                    var html = '<html><body><form action="/showreq.txt" method="POST">'+
                        '<label for="fname">First name:</label><br>' +
                        '<input type="text" id="fname" name="fname"><br>' +
                        '<label for="lname">Last name:</label><br>' +
                        '<input type="text" id="lname" name="lname">'+
                        '<input type="submit" name="go">'+                
                    '</form></body></html>';
            
                     return {html:html};
                },

                "/showreq.txt" : function(req) {

                    /* convert "body" to text so we can print it out */
                    req.body=rampart.utils.bufferToString(req.body);

                    return( { txt: rampart.utils.sprintf("%3J",req) } );
                }
            }
        });

        /* response from posting form at http://localhost:8088/post.html
           might include:

            {
               "ip": "::1",
               "port": 38680,
               "method": "POST",
               "path": {
                  "file": "showreq.html",
                  "path": "/showreq.html",
                  "base": "/",
                  "scheme": "http://",
                  "host": "localhost:8088",
                  "url": "http://localhost:8088/showreq.html"
               },
               "query": {},
               "body": "fname=Joe&lname=Public&go=Submit",
               "query_raw": "",

                ...,

               "postData": {
                  "Content-Type": "application/x-www-form-urlencoded",
                  "content": {
                       "fname": "Joe",
                       "lname": "Public",
                       "go": "Submit"
                  }
               },
               "params": {
                  "fname": "Joe",
                  "lname": "Public",
                  "go": "Submit",

                  ...,

               }
            }    
        */

Posting Multipart Form Data
"""""""""""""""""""""""""""

    Multipart form data will also be returned in the property ``formData``
    and will have the ``Content-Type`` property set to
    ``"multipart/form-data"``.  The ``content`` property will contain an
    array of objects, one object for each "part" of the form data.  The key
    and values of an object provides details and the content for each part. 

    Example:
    
    .. code-block:: javascript

        server.start(
        {
            ...,
            map : {

                "postfile.html": function(){
                    var html = '<html><body><form action="/showreq.txt" enctype="multipart/form-data" method="POST">'+
                        'File: <input type="FILE" name="file"/>' +
                        '<input type="submit" name="Upload" value="Upload" />' +
                    '</form></body></html>';

                    return {html: html};    
                },

                "/showreq.txt" : function(req) {

                    /* convert "body" to text so we can print it out */
                    req.body=rampart.utils.bufferToString(req.body);

                    return( { txt: rampart.utils.sprintf("%3J",req) } );
                }
            }
        });
    
        /* posting a small file called "helloWorld.txt with the contents "Hello World!"

        {
           "ip": "::1",
           "port": 39004,
           "method": "POST",
           "path": {
              "file": "showreq.html",
              "path": "/showreq.html",
              "base": "/",
              "scheme": "http://",
              "host": "localhost:8088",
              "url": "http://localhost:8088/showreq.html"
           },
           "query": {},
           "body": "------WebKitFormBoundaryB4UZ3AZ5kFBUZpR6\r\nContent-Disposition: form-data; name=\"file\"; filename=\"helloWorld.txt\"\r\nContent-Type: text/plain\r\n\r\nHello World!\r\n------WebKitFormBoundaryB4UZ3AZ5kFBUZpR6\r\nContent-Disposition: form-data; name=\"Upload\"\r\n\r\nUpload\r\n------WebKitFormBoundaryB4UZ3AZ5kFBUZpR6--\r\n",
           "query_raw": "",
           "cookies": {
              "mycookie": "cookietext",
              "cookiewquote": "my\"cookie\""
           },
           "headers": {
              "Host": "localhost:8088",
              "Content-Length": "299",
              ...,
           },
           "postData": {
              "Content-Type": "multipart/form-data",
              "content": [
                 {
                    "Content-Disposition": "form-data",
                    "name": "file",
                    "filename": "helloWorld.txt",
                    "Content-Type": "text/plain",
                    "content": {
                       "0": 72,
                       "1": 101,
                       "2": 108,
                       "3": 108,
                       "4": 111,
                       "5": 32,
                       "6": 87,
                       "7": 111,
                       "8": 114,
                       "9": 108,
                       "10": 100,
                       "11": 33
                    }
                 },
                 {
                    "Content-Disposition": "form-data",
                    "name": "Upload",
                    "content": {
                       "0": 85,
                       "1": 112,
                       "2": 108,
                       "3": 111,
                       "4": 97,
                       "5": 100
                    }
                 }
              ]
           },
           "params": {
              "helloWorld.txt": {
                 "0": 72,
                 "1": 101,
                 "2": 108,
                 "3": 108,
                 "4": 111,
                 "5": 32,
                 "6": 87,
                 "7": 111,
                 "8": 114,
                 "9": 108,
                 "10": 100,
                 "11": 33
              },
              "Upload": {
                 "0": 85,
                 "1": 112,
                 "2": 108,
                 "3": 111,
                 "4": 97,
                 "5": 100
              },
              "Host": "localhost:8088",
              "Connection": "keep-alive",
              "Content-Length": "299",
              "Cache-Control": "max-age=0",
              ...,
           }
        }
        */
    
    Note that like ``body``, the ``contents`` property of each uploaded part is a :green:`Buffer`.

Posting JSON Data
"""""""""""""""""

    JSON data, sent with ``Content-Type`` set to ``"application/json"`` will also be parsed in 
    a manner similar to `Posting Form Data`_.

    .. code-block:: javascript

        var server=require("rampart-server");

        server.start(
        {
            user:"root",
            map : {
                "post.html": function(){
                    var html = '<html><head><script>\n'+
                           'function senddata(){\n' +
                             'var first= document.querySelector("#fname");\n' +
                             'var last = document.querySelector("#lname");\n' +
                             'var res  = document.querySelector("#res");\n' +
                             'var xhr = new XMLHttpRequest();\n' +
                             'xhr.open("POST", "/showreq.json");\n' +
                             'xhr.setRequestHeader("Content-Type", "application/json");\n' +
                             'xhr.onreadystatechange = function () { \n' +
                               'if (xhr.readyState === 4 && xhr.status === 200) {\n' + 
                                  'res.innerHTML = "<pre>"+ this.responseText +"</pre>";\n' +
                               '} \n' +
                             '};\n' +
                             'xhr.send( JSON.stringify({first:first.value, last:last.value}) );\n'+
                             'return false;'+
                           '}\n'+
                        '</script></head><body>'+
                        '<label for="fname">First name:</label><br>' +
                        '<input type="text" id="fname" name="fname"><br>' +
                        '<label for="lname">Last name:</label><br>' +
                        '<input type="text" id="lname" name="lname">'+
                        '<button onclick="return senddata()">Submit</button>'+
                    '<div id="res"></div></body></html>';

                     return {html:html};
                },

                "/showreq.json" : function(req) {
                    /* convert "body" to text so we can send */
                    req.body=rampart.utils.bufferToString(req.body);

                    return( { json: rampart.utils.sprintf("%3J",req) } );
                }
            }
        });

        /* results might be:
        {
           "ip": "::1",
           "port": 46586,
           "method": "POST",
           "path": {
              "file": "showreq.json",
              "path": "/showreq.json",
              "base": "/",
              "scheme": "http://",
              "host": "localhost:8088",
              "url": "http://localhost:8088/showreq.json"
           },
           "query": {},
           "body": "{\"first\":\"Joe\",\"last\":\"Public\"}",
           "query_raw": "",
           "headers": {
              "Host": "localhost:8088",
              "Connection": "keep-alive",
              "Content-Length": "31",
              "Content-Type": "application/json",
              ...,
           },
           "postData": {
              "Content-Type": "application/json",
              "content": {
                 "first": "Joe",
                 "last": "Public"
              }
           },
           "params": {
              "first": "Joe",
              "last": "Public",
              "Content-Length": "31",
              "Content-Type": "application/json",
              "Referer": "http://localhost:8088/post.html",
              ...,
           }
        }
        */

Posting Other Types
"""""""""""""""""""

  Posting with a ``Content-Type`` other than the three above will return
  ``postData`` with the provided ``Content-Type`` set, and ``contents``
  will be the same as the unparsed :green:`Buffer` ``body``.

The Return Object
~~~~~~~~~~~~~~~~~

  The return value from a mapped :green:`Function` contains the contents of
  the text or data (a :green:`String` or :green:`Buffer`) that will be
  returned to the client.  The name of the key (which usually matches the
  well known file extension) determines the mime-type that is returned.  For
  example: to return an HTML (``text/html`` mime type) document to the
  client, ``{ html: myhtmlvar}`` would be specified where the variable
  ``myhtmlvar`` contains the HTML text to be sent to the client.  The name
  of the key (``html``) controls which mime-type will be sent to the
  connecting client.  Supported key-names to mime-types are listed
  :ref:`below <rampart-server:Key to Mime Mappings>`.
  
  The return object can optionally contain header parameters to be sent to
  the client.
  
  .. code-block:: javascript
  
     return { 
        html: myhtmltext,
        headers: { "X-Custom-Header": "custom value"}
     }
  				
  To set more than one header with the same name, the value must be an :green:`Array`.
  
  .. code-block:: javascript
  
     return { 
        html: myhtmltext,
        headers: { 
            "X-Custom-Header": "custom value",
            "Set-Cookie": [
                rampart.utils.sprintf("id=%U; Expires=Wed, 15 Oct 2025 10:28:00 GMT", id),
                rampart.utils.sprintf("session=%U; Max-Age=86400", session_id)
            ]
        }
     }

  A Status Code may also be specified. For example, to redirect a url to a
  new one:
  
  .. code-block:: javascript
  
     var newurl = "https://example.com/myNewLocation.html";
     return {
        html:rampart.utils.sprintf(
             "<html><body><h1>302 Moved Temporarily</h1>"+
             '<p>Document moved <a href="%s">here</a></p></body></html>',
             newurl
        ),
        status:302,
        headers: { "location": newurl}
     }

  The specified mime-type can also be overwritten using the 
  ``content-type`` header.  This way, any arbitrary mime-type can be
  set regardless of the name of the key (though the name of the key
  must be a known extension):
  
  .. code-block:: javascript

    var jpg = rampart.utils.readFile("/path/to/my/jpeg.jpg"); 
    /* overwrite the bin -> "application/octet-stream" header */
    return {
       bin:jpg
       headers: {"content-type": "image/jpeg"}
    };
    
  See also ``mimeMap`` in `start()`_ above.

  The content of a file may be sent by returning the file name prepended
  with an ``@`` character.

  .. code-block:: javascript

    return {
       jpg: "@/path/to/my/jpeg.jpg"
    };

  This will be more efficient than reading the file and returing its
  content as shown in the previous example.

  Note that in order to send a string whose first character is ``@``, it 
  must be escaped.

  .. code-block:: javascript

    return {
       txt: "\\@home is a defunct internet service"
    };
  
Built-in Directory Function
~~~~~~~~~~~~~~~~~~~~~~~~~~~

    If ``directoryFunc`` in `start()`_ above is set to ``true``, the
    following script will be used to return an HTML formatted a directory
    listing, where an ``index.html`` file is not present in the requested
    directory.  It is shown below so that if modifications to the default
    are desired, it can be used as a starting point for a custom function
    that can be set using the ``directoryFunc`` property.

    Note that the ``req`` variable passed to the function contains an extra
    parameter ``fsPath``, which is the path on the filesystem being requested.

    .. code-block:: javascript

        function dirlist(req) {
            var html="<!DOCTYPE html>\n"+
                '<html><head><meta charset="UTF-8"><title>Index of ' + 
                req.path.path+ 
                "</title><style>td{padding-right:22px;}</style></head><body><h1>"+
                req.path.path+
                '</h1><hr><table>';

            function hsize(size) {
                var ret=rampart.utils.sprintf("%d",size);
                if(size >= 1073741824)
                    ret=rampart.utils.sprintf("%.1fG", size/1073741824);
                else if (size >= 1048576)
                    ret=rampart.utils.sprintf("%.1fM", size/1048576);
                else if (size >=1024)
                    ret=rampart.utils.sprintf("%.1fk", size/1024); 
                return ret;
            }

            if(req.path.path != '/')
                html+= '<tr><td><a href="../">Parent Directory</a></td><td></td><td>-</td></tr>';
            rampart.utils.readdir(req.fsPath).sort().forEach(function(d){
                var st=rampart.utils.stat(req.fsPath+'/'+d);
                if (st.isDirectory())
                    d+='/';
                html=rampart.utils.sprintf('%s<tr><td><a href="%s">%s</a></td><td>%s</td><td>%s</td></tr>',
                    html, d, d, st.mtime.toLocaleString() ,hsize(st.size));
            });
            
            html+="</table></body></html>";
            return {html:html};
        }

        server.start({
            ...,
            directoryFunc: dirlist
        });

Advanced Functions
~~~~~~~~~~~~~~~~~~

The ``rampart-server`` module creates a ``mmap``\ ed buffer to efficiently store data
that will be returned to the client by the webserver.  There is one buffer per thread
and it is used from within each thread and shared with any child processes.  The size of
this "server buffer" is controlled from the ``bufferMem`` setting in `start()`_ above.

Though returning an object with, .e.g. ``{html: mydata}`` set is not limited by the size of 
the server buffer, the functions below are so limited and will throw
an error if the total size written to it is larger than the size allotted.
 
The request object contains the functions to manipulate and print to the server buffer, 
which will be directly sent to the client without extra copying.

req.printf()
""""""""""""

The request object to a callback function includes the ``printf`` function
which will print directly to the server buffer that will be sent to the client. 
It uses the same formats as :ref:`rampart.utils.printf <rampart-utils:printf>`.
The advantages of using ``req.printf`` rather than returning a string is that 
content is not copied, but instead placed directly in the server buffer to be 
returned to the client.

Example from a normal server callback function:

.. code-block:: javascript

    function mycallback(req) {
        var html;
        ... add content to html ...
        return {html: html};
    }

Example using ``req.printf`` from a server callback function:

.. code-block:: javascript

    function mycallback(req) {
        var html;
        var end_cont = "</body></html>";
        // add content to html
        req.printf("%s", content);
        return {html: end_cont};
    }

Return Value:
    The number of bytes written to the server buffer.

Note: 
    If ``content`` is large, it is more efficiently handled using
    ``req.printf`` and/or ``req.put`` below than concatenating strings in
    JavaScript.
    
    The one exception to this is if ``content`` is a :green:`Buffer` and is
    the total content to be returned to the client without concatenation or
    manipulation, doing ``return {html:content}`` is the most efficient
    method.
    
    However, in nearly all cases, if a function needs to print many strings
    that make up the totality of the data sent to the client, using
    ``req.printf`` or ``req.put`` is preferable.
    
    When printing to the server buffer, each thread's buffer must be
    large enough to hold all the data from every ``req.printf`` statement.
    If the server buffer size is smaller than the data inserted, an error
    will be thrown.

req.put()
"""""""""

Put a :green:`String` or a :green:`Buffer` into the server buffer to be returned
to the client.

Example:

.. code-block:: javascript

    function mycallback(req) {
        var html;
        var end_cont = "</body></html>";
        ... add content to html ...
        req.put(content);
        return {html: end_cont};
    }
                                                
Return Value:
    The number of bytes written to the server buffer.

req.getpos()
""""""""""""

Get the current end position in the server buffer.

Return Value
    A :green:`Number` - the end position of the server buffer.

req.setpos()
""""""""""""

Set the current end position in the server buffer.

Usage:

.. code-block:: javascript

    function mycallback(req) {
        ...
        var pos = req.setpos(pos);
        ...
    }

Where ``pos`` is the offset to position the end pointer in the server buffer.

Return Value
    ``undefined``.

req.getBuffer()
"""""""""""""""

Get a copy of the contents of the server buffer and return it in a JavaScript
buffer.

Return Value:
    A :green:`Buffer` - the contents of the server buffer.

 
Full Example
~~~~~~~~~~~~

Below is a full example:

.. code-block:: javascript

    var pid=server.start(
    {
        /* bind: string|[array,of,strings]
           default: [ "[::1]:8088", "127.0.0.1:8088" ] 
            ipv6 format: [2001:db8::1111:2222]:80
            ipv4 format: 127.0.0.1:80
            spaces are ignored (i.e. " [ 2001:db8::1111:2222 ] : 80" is ok)
        */
        /* bind to all */
        bind: [ "[::]:8088", "0.0.0.0:8088" ],

        /* if started as root, set user here.  
           If not root, option "user" is ignored. */
        user: "nobody",

        /* max time to spend in scripts */
        scriptTimeout: 10.0,

        /* how long to wait before client sends
           a req or server can send a response */
        connectTimeout:20.0,

        /*** logging ***/
        log: true,           //turn logging on, by default goes to stdout/stderr
        accessLog: "./access.log",    //access log location, instead of stdout. Can be set if daemon==true
        errorLog: "./error.log",     //error log location, instead of stderr. Can be set if daemon==true

        /*  fork and return pid server start (see end of the script) */
        daemon: true,

        /* make server singe-threaded. */
        //useThreads: false,

        /*  By default, number of threads is set to cpu core count.
            "threads" has no effect unless useThreads is set true.
            The number can be changed here:
        */
        //threads: 8, /* for a 4 core, 8 virtual core hyper-threaded processor. */

        /* 
            for https support, these three are the minimum number of options needed:
        */
        secure:true,
        sslKeyFile:  "/etc/letsencrypt/live/mydom.com/privkey.pem",
        sslCertFile: "/etc/letsencrypt/live/mydom.com/fullchain.pem",

        /* sslMinVersion (ssl3|tls1|tls1.1|tls1.2). "tls1.2" is default*/
        sslMinVersion: "tls1.2",

        /* a custom 404 page */
        notFoundFunc: function(req){
            return {
                status:404,
                html: '<html><head><title>404 Not Found</title></head>'+
                      '<body style="background: url(/img/page-background.png);">'+
                      '<center><h1>Not Found</h1><p>The requested URL '+
                        req.path.path+
                      ' was not found on this server.</p>'+
                      '</center></body></html>'
            }
        },

        /* if a function is given, directoryFunc will be called each time a url
            which corresponds to a directory is called if there is no index.htm(l)
            present in the directory.  Added to the normal request object
            will be the property (string) "fsPath" (req.fsPath), which can be used
            to create a directory listing.  See function dirlist() above.
            It is substantially equivelant to the built-in server.defaultDirList function.

            If directoryFunc is not set, a url pointing to a directory without an index.htm(l)
            will return a 403 Forbidden error.
        */

        directoryFunc: true, //use default directory list function

		/* remap a few extensions -> mimetypes */
        mimeMap: {
          	  "m4v": "video/mp4",
          	  "mov": "video/mp4"
        },

        /* **********************************************************
           map urls to functions or paths on the filesystem 
           If it ends in a '/' then matches everything in that path
           except a more specific ('/something.html') path
           
           priority is given to Exact Paths (Begins with '/' and no '*' in path), then
             regular expressions, then globs.
             
           If mapSort: false, then in each of these groups
             is left unsorted.
           Otherwise, within these groups, they are then ordered by length, 
             with longest having priority.

           If you wish to specify your own priority, set:

        mapSort: false,

           and then put them in your prefered order below.
           ********************************************************** */
        map:
        {
            "/helloWorld.html" : function(){ 
                return {
                    html:"<pre>Hello World!</pre>"
                }
            },

            /* directory for scripts */
            "/scripts/": { "modulePath" : "/var/www/scripts" }

            /* static content */
            "/" : "/var/www/html"
        }
    });

    console.log("server started with pid: "+pid);

Key to Mime Mappings
--------------------

Key/extension to mime-type mappings are shown below. They apply to both the
return value of `Mapped Functions`_ as well as the extension of files served
from `Mapped Directories`_\ . This list of defaults can be appended or modified
using the ``mimeMap`` property in the :green:`Object` passed to `start()`_ \.

An example: If the variable ``jpg`` is set
(e.g. ``var jpg = rampart.utils.readFile("/path/to/my/jpeg.jpg");``), 
then ``return {jpeg:jpg};`` at the end of a mapped function would send 
the contents of the file ``/path/to/my/jpeg.jpg`` with the 
mime-type ``image/jpeg`` to the client.  The same applies to files served
from the filesystem which end in ``.jpeg`` or ``.jpg``.

::

    "3dm"	->	"x-world/x-3dmf"
    "3dmf"	->	"x-world/x-3dmf"
    "3gp"	->	"video/3gpp"
    "3gpp"	->	"video/3gpp"
    "7z"	->	"application/x-7z-compressed"
    "a"		->	"application/octet-stream"
    "aab"	->	"application/x-authorware-bin"
    "aam"	->	"application/x-authorware-map"
    "aas"	->	"application/x-authorware-seg"
    "abc"	->	"text/vnd.abc"
    "acgi"	->	"text/html"
    "afl"	->	"video/animaflex"
    "ai"	->	"application/postscript"
    "aif"	->	"audio/aiff"
    "aifc"	->	"audio/aiff"
    "aiff"	->	"audio/aiff"
    "aim"	->	"application/x-aim"
    "aip"	->	"text/x-audiosoft-intra"
    "ani"	->	"application/x-navi-animation"
    "aos"	->	"application/x-nokia-9000-communicator-add-on-software"
    "aps"	->	"application/mime"
    "arc"	->	"application/octet-stream"
    "arj"	->	"application/arj"
    "art"	->	"image/x-jg"
    "asf"	->	"video/x-ms-asf"
    "asm"	->	"text/x-asm"
    "asp"	->	"text/asp"
    "asx"	->	"video/x-ms-asf"
    "atom"	->	"application/atom+xml"
    "au"	->	"audio/x-au"
    "avi"	->	"video/x-msvideo"
    "avs"	->	"video/avs-video"
    "bcpio"	->	"application/x-bcpio"
    "bin"	->	"application/octet-stream"
    "bm"	->	"image/bmp"
    "bmp"	->	"image/x-ms-bmp"
    "boo"	->	"application/book"
    "book"	->	"application/book"
    "boz"	->	"application/x-bzip2"
    "bsh"	->	"application/x-bsh"
    "bz"	->	"application/x-bzip"
    "bz2"	->	"application/x-bzip2"
    "c"		->	"text/plain"
    "c++"	->	"text/plain"
    "cat"	->	"application/vnd.ms-pki.seccat"
    "cc"	->	"text/plain"
    "ccad"	->	"application/clariscad"
    "cco"	->	"application/x-cocoa"
    "cdf"	->	"application/x-cdf"
    "cer"	->	"application/x-x509-ca-cert"
    "cha"	->	"application/x-chat"
    "chat"	->	"application/x-chat"
    "class"	->	"application/x-java-class"
    "com"	->	"application/octet-stream"
    "conf"	->	"text/plain"
    "cpio"	->	"application/x-cpio"
    "cpp"	->	"text/x-c"
    "cpt"	->	"application/x-cpt"
    "crl"	->	"application/pkix-crl"
    "crt"	->	"application/x-x509-ca-cert"
    "csh"	->	"text/x-script.csh"
    "css"	->	"text/css"
    "cxx"	->	"text/plain"
    "data"	->	"application/octet-stream"
    "dcr"	->	"application/x-director"
    "deb"	->	"application/octet-stream"
    "deepv"	->	"application/x-deepv"
    "def"	->	"text/plain"
    "der"	->	"application/x-x509-ca-cert"
    "dif"	->	"video/x-dv"
    "dir"	->	"application/x-director"
    "dl"	->	"video/x-dl"
    "dll"	->	"application/octet-stream"
    "dmg"	->	"application/octet-stream"
    "doc"	->	"application/msword"
    "docx"	->	"application/vnd.openxmlformats-officedocument.wordprocessingml.document"
    "dot"	->	"application/msword"
    "dp"	->	"application/commonground"
    "drw"	->	"application/drafting"
    "dump"	->	"application/octet-stream"
    "dv"	->	"video/x-dv"
    "dvi"	->	"application/x-dvi"
    "dwf"	->	"model/vnd.dwf"
    "dwg"	->	"image/x-dwg"
    "dxf"	->	"image/x-dwg"
    "dxr"	->	"application/x-director"
    "ear"	->	"application/java-archive"
    "el"	->	"text/x-script.elisp"
    "elc"	->	"application/x-elc"
    "env"	->	"application/x-envoy"
    "eot"	->	"application/vnd.ms-fontobject"
    "eps"	->	"application/postscript"
    "es"	->	"application/x-esrehber"
    "etx"	->	"text/x-setext"
    "evy"	->	"application/x-envoy"
    "exe"	->	"application/octet-stream"
    "f"		->	"text/plain"
    "f77"	->	"text/plain"
    "f90"	->	"text/plain"
    "fdf"	->	"application/vnd.fdf"
    "fif"	->	"image/fif"
    "fli"	->	"video/x-fli"
    "flo"	->	"image/florian"
    "flv"	->	"video/x-flv"
    "flx"	->	"text/vnd.fmi.flexstor"
    "fmf"	->	"video/x-atomic3d-feature"
    "for"	->	"text/plain"
    "fpx"	->	"image/vnd.fpx"
    "frl"	->	"application/freeloader"
    "funk"	->	"audio/make"
    "g"		->	"text/plain"
    "g3"	->	"image/g3fax"
    "gif"	->	"image/gif"
    "gl"	->	"video/x-gl"
    "gsd"	->	"audio/x-gsm"
    "gsm"	->	"audio/x-gsm"
    "gsp"	->	"application/x-gsp"
    "gss"	->	"application/x-gss"
    "gtar"	->	"application/x-gtar"
    "gz"	->	"application/x-gzip"
    "gzip"	->	"application/x-gzip"
    "h"		->	"text/plain"
    "hdf"	->	"application/x-hdf"
    "help"	->	"application/x-helpfile"
    "hgl"	->	"application/vnd.hp-hpgl"
    "hh"	->	"text/plain"
    "hlb"	->	"text/x-script"
    "hlp"	->	"application/x-helpfile"
    "hpg"	->	"application/vnd.hp-hpgl"
    "hpgl"	->	"application/vnd.hp-hpgl"
    "hqx"	->	"application/mac-binhex40"
    "hta"	->	"application/hta"
    "htc"	->	"text/x-component"
    "htm"	->	"text/html"
    "html"	->	"text/html"
    "htmls"	->	"text/html"
    "htt"	->	"text/webviewhtml"
    "htx"	->	"text/html"
    "ice"	->	"x-conference/x-cooltalk"
    "ico"	->	"image/x-icon"
    "idc"	->	"text/plain"
    "ief"	->	"image/ief"
    "iefs"	->	"image/ief"
    "iges"	->	"application/iges"
    "igs"	->	"application/iges"
    "ima"	->	"application/x-ima"
    "imap"	->	"application/x-httpd-imap"
    "img"	->	"application/octet-stream"
    "inf"	->	"application/inf"
    "ins"	->	"application/x-internett-signup"
    "ip"	->	"application/x-ip2"
    "iso"	->	"application/octet-stream"
    "isu"	->	"video/x-isvideo"
    "it"	->	"audio/it"
    "iv"	->	"application/x-inventor"
    "ivr"	->	"i-world/i-vrml"
    "ivy"	->	"application/x-livescreen"
    "jad"	->	"text/vnd.sun.j2me.app-descriptor"
    "jam"	->	"audio/x-jam"
    "jar"	->	"application/java-archive"
    "jardiff"	->	"application/x-java-archive-diff"
    "jav"	->	"text/plain"
    "java"	->	"text/plain"
    "jcm"	->	"application/x-java-commerce"
    "jfif"	->	"image/jpeg"
    "jfif-tbnl"	->	"image/jpeg"
    "jng"	->	"image/x-jng"
    "jnlp"	->	"application/x-java-jnlp-file"
    "jpe"	->	"image/jpeg"
    "jpeg"	->	"image/jpeg"
    "jpg"	->	"image/jpeg"
    "jps"	->	"image/x-jps"
    "js"	->	"application/javascript"
    "json"	->	"application/json"
    "jut"	->	"image/jutvision"
    "kar"	->	"music/x-karaoke"
    "kml"	->	"application/vnd.google-earth.kml+xml"
    "kmz"	->	"application/vnd.google-earth.kmz"
    "ksh"	->	"application/x-ksh"
    "la"	->	"audio/x-nspaudio"
    "lam"	->	"audio/x-liveaudio"
    "latex"	->	"application/x-latex"
    "lha"	->	"application/x-lha"
    "lhx"	->	"application/octet-stream"
    "list"	->	"text/plain"
    "lma"	->	"audio/nspaudio"
    "log"	->	"text/plain"
    "lst"	->	"text/plain"
    "lsx"	->	"text/x-la-asf"
    "ltx"	->	"application/x-latex"
    "lzh"	->	"application/x-lzh"
    "lzx"	->	"application/x-lzx"
    "m"		->	"text/plain"
    "m1v"	->	"video/mpeg"
    "m2a"	->	"audio/mpeg"
    "m2v"	->	"video/mpeg"
    "m3u"	->	"audio/x-mpequrl"
    "m3u8"	->	"application/vnd.apple.mpegurl"
    "m4a"	->	"audio/x-m4a"
    "m4v"	->	"video/x-m4v"
    "man"	->	"application/x-troff-man"
    "map"	->	"application/x-navimap"
    "mar"	->	"text/plain"
    "mbd"	->	"application/mbedlet"
    "mc$"	->	"application/x-magic-cap-package-1.0"
    "mcd"	->	"application/x-mathcad"
    "mcf"	->	"text/mcf"
    "mcp"	->	"application/netmc"
    "me"	->	"application/x-troff-me"
    "mht"	->	"message/rfc822"
    "mhtml"	->	"message/rfc822"
    "mid"	->	"audio/midi"
    "midi"	->	"audio/midi"
    "mif"	->	"application/x-frame"
    "mime"	->	"message/rfc822"
    "mjf"	->	"audio/x-vnd.audioexplosion.mjuicemediafile"
    "mjpg"	->	"video/x-motion-jpeg"
    "mm"	->	"application/x-meme"
    "mme"	->	"application/base64"
    "mml"	->	"text/mathml"
    "mng"	->	"video/x-mng"
    "mod"	->	"audio/x-mod"
    "moov"	->	"video/quicktime"
    "mov"	->	"video/quicktime"
    "movie"	->	"video/x-sgi-movie"
    "mp2"	->	"audio/mpeg"
    "mp3"	->	"audio/mpeg"
    "mp4"	->	"video/mp4"
    "mpa"	->	"audio/mpeg"
    "mpc"	->	"application/x-project"
    "mpe"	->	"video/mpeg"
    "mpeg"	->	"video/mpeg"
    "mpg"	->	"video/mpeg"
    "mpga"	->	"audio/mpeg"
    "mpp"	->	"application/vnd.ms-project"
    "mpt"	->	"application/x-project"
    "mpv"	->	"application/x-project"
    "mpx"	->	"application/x-project"
    "mrc"	->	"application/marc"
    "ms"	->	"application/x-troff-ms"
    "msi"	->	"application/octet-stream"
    "msm"	->	"application/octet-stream"
    "msp"	->	"application/octet-stream"
    "mv"	->	"video/x-sgi-movie"
    "my"	->	"audio/make"
    "mzz"	->	"application/x-vnd.audioexplosion.mzz"
    "nap"	->	"image/naplps"
    "naplps"	->	"image/naplps"
    "nc"	->	"application/x-netcdf"
    "ncm"	->	"application/vnd.nokia.configuration-message"
    "nif"	->	"image/x-niff"
    "niff"	->	"image/x-niff"
    "nix"	->	"application/x-mix-transfer"
    "nsc"	->	"application/x-conference"
    "nvd"	->	"application/x-navidoc"
    "o"		->	"application/octet-stream"
    "oda"	->	"application/oda"
    "odg"	->	"application/vnd.oasis.opendocument.graphics"
    "odp"	->	"application/vnd.oasis.opendocument.presentation"
    "ods"	->	"application/vnd.oasis.opendocument.spreadsheet"
    "odt"	->	"application/vnd.oasis.opendocument.text"
    "ogg"	->	"audio/ogg"
    "omc"	->	"application/x-omc"
    "omcd"	->	"application/x-omcdatamaker"
    "omcr"	->	"application/x-omcregerator"
    "p"		->	"text/x-pascal"
    "p10"	->	"application/x-pkcs10"
    "p12"	->	"application/x-pkcs12"
    "p7c"	->	"application/x-pkcs7-mime"
    "p7m"	->	"application/x-pkcs7-mime"
    "p7r"	->	"application/x-pkcs7-certreqresp"
    "p7s"	->	"application/pkcs7-signature"
    "part"	->	"application/pro_eng"
    "pas"	->	"text/pascal"
    "pbm"	->	"image/x-portable-bitmap"
    "pcl"	->	"application/x-pcl"
    "pct"	->	"image/x-pict"
    "pcx"	->	"image/x-pcx"
    "pdb"	->	"application/x-pilot"
    "pdf"	->	"application/pdf"
    "pem"	->	"application/x-x509-ca-cert"
    "pfunk"	->	"audio/make"
    "pgm"	->	"image/x-portable-graymap"
    "pic"	->	"image/pict"
    "pict"	->	"image/pict"
    "pkg"	->	"application/x-newton-compatible-pkg"
    "pko"	->	"application/vnd.ms-pki.pko"
    "pl"	->	"application/x-perl"
    "plx"	->	"application/x-pixclscript"
    "pm"	->	"application/x-perl"
    "pm4"	->	"application/x-pagemaker"
    "pm5"	->	"application/x-pagemaker"
    "png"	->	"image/png"
    "pnm"	->	"image/x-portable-anymap"
    "pot"	->	"application/mspowerpoint"
    "pov"	->	"model/x-pov"
    "ppa"	->	"application/vnd.ms-powerpoint"
    "ppm"	->	"image/x-portable-pixmap"
    "pps"	->	"application/mspowerpoint"
    "ppt"	->	"application/vnd.ms-powerpoint"
    "pptx"	->	"application/vnd.openxmlformats-officedocument.presentationml.presentation"
    "ppz"	->	"application/mspowerpoint"
    "prc"	->	"application/x-pilot"
    "pre"	->	"application/x-freelance"
    "prt"	->	"application/pro_eng"
    "ps"	->	"application/postscript"
    "psd"	->	"application/octet-stream"
    "pvu"	->	"paleovu/x-pv"
    "pwz"	->	"application/vnd.ms-powerpoint"
    "py"	->	"text/x-script.phyton"
    "pyc"	->	"application/x-bytecode.python"
    "qcp"	->	"audio/vnd.qcelp"
    "qd3"	->	"x-world/x-3dmf"
    "qd3d"	->	"x-world/x-3dmf"
    "qif"	->	"image/x-quicktime"
    "qt"	->	"video/quicktime"
    "qtc"	->	"video/x-qtc"
    "qti"	->	"image/x-quicktime"
    "qtif"	->	"image/x-quicktime"
    "ra"	->	"audio/x-realaudio"
    "ram"	->	"audio/x-pn-realaudio"
    "rar"	->	"application/x-rar-compressed"
    "ras"	->	"image/x-cmu-raster"
    "rast"	->	"image/cmu-raster"
    "rexx"	->	"text/x-script.rexx"
    "rf"	->	"image/vnd.rn-realflash"
    "rgb"	->	"image/x-rgb"
    "rm"	->	"audio/x-pn-realaudio"
    "rmi"	->	"audio/mid"
    "rmm"	->	"audio/x-pn-realaudio"
    "rmp"	->	"audio/x-pn-realaudio"
    "rng"	->	"application/ringing-tones"
    "rnx"	->	"application/vnd.rn-realplayer"
    "roff"	->	"application/x-troff"
    "rp"	->	"image/vnd.rn-realpix"
    "rpm"	->	"application/x-redhat-package-manager"
    "rss"	->	"application/rss+xml"
    "rt"	->	"text/richtext"
    "rtf"	->	"application/rtf"
    "rtx"	->	"text/richtext"
    "run"	->	"application/x-makeself"
    "rv"	->	"video/vnd.rn-realvideo"
    "s"		->	"text/x-asm"
    "s3m"	->	"audio/s3m"
    "saveme"	->	"application/octet-stream"
    "sbk"	->	"application/x-tbook"
    "scm"	->	"text/x-script.scheme"
    "sdml"	->	"text/plain"
    "sdp"	->	"application/x-sdp"
    "sdr"	->	"application/sounder"
    "sea"	->	"application/x-sea"
    "set"	->	"application/set"
    "sgm"	->	"text/sgml"
    "sgml"	->	"text/sgml"
    "sh"	->	"text/x-script.sh"
    "shar"	->	"application/x-shar"
    "shtml"	->	"text/html"
    "sid"	->	"audio/x-psid"
    "sit"	->	"application/x-stuffit"
    "skd"	->	"application/x-koan"
    "skm"	->	"application/x-koan"
    "skp"	->	"application/x-koan"
    "skt"	->	"application/x-koan"
    "sl"	->	"application/x-seelogo"
    "smi"	->	"application/smil"
    "smil"	->	"application/smil"
    "snd"	->	"audio/basic"
    "sol"	->	"application/solids"
    "spc"	->	"text/x-speech"
    "spl"	->	"application/futuresplash"
    "spr"	->	"application/x-sprite"
    "sprite"	->	"application/x-sprite"
    "src"	->	"application/x-wais-source"
    "ssi"	->	"text/x-server-parsed-html"
    "ssm"	->	"application/streamingmedia"
    "sst"	->	"application/vnd.ms-pki.certstore"
    "step"	->	"application/step"
    "stl"	->	"application/sla"
    "stp"	->	"application/step"
    "sv4cpio"	->	"application/x-sv4cpio"
    "sv4crc"	->	"application/x-sv4crc"
    "svf"	->	"image/x-dwg"
    "svg"	->	"image/svg+xml"
    "svgz"	->	"image/svg+xml"
    "svr"	->	"application/x-world"
    "swf"	->	"application/x-shockwave-flash"
    "t"		->	"application/x-troff"
    "talk"	->	"text/x-speech"
    "tar"	->	"application/x-tar"
    "tbk"	->	"application/x-tbook"
    "tcl"	->	"application/x-tcl"
    "tcsh"	->	"text/x-script.tcsh"
    "tex"	->	"application/x-tex"
    "texi"	->	"application/x-texinfo"
    "texinfo"	->	"application/x-texinfo"
    "text"	->	"text/plain"
    "tgz"	->	"application/gnutar"
    "tif"	->	"image/tiff"
    "tiff"	->	"image/tiff"
    "tk"	->	"application/x-tcl"
    "tr"	->	"application/x-troff"
    "ts"	->	"video/mp2t"
    "tsi"	->	"audio/tsp-audio"
    "tsp"	->	"audio/tsplayer"
    "tsv"	->	"text/tab-separated-values"
    "turbot"	->	"image/florian"
    "txt"	->	"text/plain"
    "uni"	->	"text/uri-list"
    "unis"	->	"text/uri-list"
    "unv"	->	"application/i-deas"
    "uri"	->	"text/uri-list"
    "uris"	->	"text/uri-list"
    "ustar"	->	"application/x-ustar"
    "uu"	->	"text/x-uuencode"
    "uue"	->	"text/x-uuencode"
    "vcd"	->	"application/x-cdlink"
    "vcs"	->	"text/x-vcalendar"
    "vda"	->	"application/vda"
    "vdo"	->	"video/vdo"
    "vew"	->	"application/groupwise"
    "viv"	->	"video/vivo"
    "vivo"	->	"video/vivo"
    "vmd"	->	"application/vocaltec-media-desc"
    "vmf"	->	"application/vocaltec-media-file"
    "voc"	->	"audio/voc"
    "vos"	->	"video/vosaic"
    "vox"	->	"audio/voxware"
    "vqe"	->	"audio/x-twinvq-plugin"
    "vqf"	->	"audio/x-twinvq"
    "vql"	->	"audio/x-twinvq-plugin"
    "vrml"	->	"application/x-vrml"
    "vrt"	->	"x-world/x-vrt"
    "vsd"	->	"application/x-visio"
    "vst"	->	"application/x-visio"
    "vsw"	->	"application/x-visio"
    "w60"	->	"application/wordperfect6.0"
    "w61"	->	"application/wordperfect6.1"
    "w6w"	->	"application/msword"
    "war"	->	"application/java-archive"
    "wav"	->	"audio/wav"
    "wb1"	->	"application/x-qpro"
    "wbmp"	->	"image/vnd.wap.wbmp"
    "web"	->	"application/vnd.xara"
    "webm"	->	"video/webm"
    "webp"	->	"image/webp"
    "wiz"	->	"application/msword"
    "wk1"	->	"application/x-123"
    "wmf"	->	"windows/metafile"
    "wml"	->	"text/vnd.wap.wml"
    "wmlc"	->	"application/vnd.wap.wmlc"
    "wmls"	->	"text/vnd.wap.wmlscript"
    "wmlsc"	->	"application/vnd.wap.wmlscriptc"
    "wmv"	->	"video/x-ms-wmv"
    "woff"	->	"font/woff"
    "woff2"	->	"font/woff2"
    "word"	->	"application/msword"
    "wp"	->	"application/wordperfect"
    "wp5"	->	"application/wordperfect"
    "wp6"	->	"application/wordperfect"
    "wpd"	->	"application/wordperfect"
    "wq1"	->	"application/x-lotus"
    "wri"	->	"application/x-wri"
    "wrl"	->	"application/x-world"
    "wrz"	->	"model/vrml"
    "wsc"	->	"text/scriplet"
    "wsrc"	->	"application/x-wais-source"
    "wtk"	->	"application/x-wintalk"
    "x-png"	->	"image/png"
    "xbm"	->	"image/x-xbitmap"
    "xdr"	->	"video/x-amt-demorun"
    "xgz"	->	"xgl/drawing"
    "xhtml"	->	"application/xhtml+xml"
    "xif"	->	"image/vnd.xiff"
    "xl"	->	"application/excel"
    "xla"	->	"application/excel"
    "xlb"	->	"application/excel"
    "xlc"	->	"application/excel"
    "xld"	->	"application/excel"
    "xlk"	->	"application/excel"
    "xll"	->	"application/excel"
    "xlm"	->	"application/excel"
    "xls"	->	"application/vnd.ms-excel"
    "xlsx"	->	"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
    "xlt"	->	"application/excel"
    "xlv"	->	"application/excel"
    "xlw"	->	"application/excel"
    "xm"	->	"audio/xm"
    "xml"	->	"text/xml"
    "xmz"	->	"xgl/movie"
    "xpi"	->	"application/x-xpinstall"
    "xpix"	->	"application/x-vnd.ls-xpix"
    "xpm"	->	"image/xpm"
    "xspf"	->	"application/xspf+xml"
    "xsr"	->	"video/x-amt-showrun"
    "xwd"	->	"image/x-xwd"
    "xyz"	->	"chemical/x-pdb"
    "z"		->	"application/x-compressed"
    "zip"	->	"application/zip"
    "zoo"	->	"application/octet-stream"
    "zsh"	->	"text/x-script.zsh"

