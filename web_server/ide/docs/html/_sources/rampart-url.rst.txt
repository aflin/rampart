The rampart-url module
======================

Preface
-------

NOTE: superseded by :ref:`these util functions <rampart-utils:urlComponents>`.

License
~~~~~~~

The rampart-url module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-url module provides utility functions to manipulate URLs

How does it work?
~~~~~~~~~~~~~~~~~

The rampart-url module is a JavaScript module that export functions which 
break URLs into their component parts, resolve parent directories
(``../``) and create absolute URLs given a source location and one or
more relative URL paths.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var urlutils = require("rampart-url");



URL Functions
-------------

components
~~~~~~~~~~

    The ``components`` function takes one or two arguments: The 
    URL to be broken into its component parts, and optionally a
    :green:`Boolean`, which if ``true``, signifies that the URL
    is relative.

    Usage:

    .. code-block:: javascript
    
        var urlutils = require("rampart-url");
        
        var urlinfo = urlutils.components(url[, isRelative]); 

    Where:
    
    * ``url`` is a :green:`String`, the URL to be broken down.

    * ``isRelative`` is an :green:`Boolean`, set ``true`` if 
      the URL is to be treated as a URL path without the origin.

    Return Value:
        A :green:`Object` containing the components of the URL.  Or returns ``undefined``
        if the URL cannot be parsed.

Example:

.. code-block:: javascript

    var urlutils = require("rampart-url");

    var urlinfo = urlutils.components(
        "http://me:mypass@example.com:8088/dir/mypage.html?dir=%2fusr%2flocal%2f#my-spot-on-page"
    );
    /* urlinfo = 
        {
           "scheme": "http",
           "username": "me",
           "password": "mypass",
           "origin": "http://me:mypass@example.com:8088",
           "host": "example.com",
           "authority": "//me:mypass@example.com:8088",
           "path": "/dir/",
           "fullPath": "/dir/mypage.html",
           "queryString": {
              "raw": "dir=%2fusr%2flocal%2f",
              "components": {
                 "dir": "/usr/local/"
              }
           },
           "hash": "#my-spot-on-page",
           "url": "http://me:mypass@example.com:8088/dir/mypage.html?dir=%2fusr%2flocal%2f",
           "href": "http://me:mypass@example.com:8088/dir/mypage.html?dir=%2fusr%2flocal%2f#my-spot-on-page",
           "portText": "8088",
           "port": 8088,
           "file": "mypage.html"
        }
    */

    urlinfo = urlutils.components("/a/directory/that/doesnt/../exist/mydoc.html", true);
    
    /* urlinfo =
        {
           "scheme": "",
           "username": "",
           "password": "",
           "origin": "",
           "host": "",
           "authority": "",
           "path": "/a/directory/that/exist/",
           "fullPath": "/a/directory/that/exist/mydoc.html",
           "queryString": {},
           "hash": "",
           "url": "",
           "href": "",
           "file": "mydoc.html"
        }

    */

absUrl
~~~~~~

    The ``absUrl`` function takes a starting URL and a single URL path or an :green:`Array`
    of URL paths, and converts them to absolute URLS.

    Usage:

    .. code-block:: javascript
    
        var urlutils = require("rampart-url");
        
        var newUrls = urlutils.absUrl(source_url, paths[, returnComponents]); 

    Where:
    
    * ``source_url`` is a :green:`String`, the URL from which the relative
      paths ("hrefs") were extracted.

    * ``paths`` is a :green:`String`, or an :green:`Array` of :green:`Strings`,
      the URL paths to be converted to absolute URLs.

    * ``returnComponents`` is an :green:`Boolean`, set ``true`` to have 
      the return value be an :green:`Object`, the format of which is the
      same as in `components`_ above.

    Return Value:
        A :green:`String`, or an :green:`Array` of :green:`Strings` (if
        paths is an :green:`Array`).  The single value or an :green:`Array`
        member will be ``undefined`` if the URL cannot be parsed.  
        
        If ``returnComponents`` is ``true``, the return value will be a
        :green:`Object` or an :green:`Array` of :green:`Objects` containing
        the components of the URL.

Example:

.. code-block:: javascript

    var urlutils = require("rampart-url");

    var sourceUrl = "http://example.com/dir/mypage.html"
    var links = [
        "../images/me.jpg",
        "../index.html",
        "/cgi-bin/myapp.app",
        "https://www.google.com:443/search"
    ]

    var absurls = urlutils.absUrl(sourceUrl, links);
    /* absurls = 
        [
           "http://example.com/images/me.jpg",
           "http://example.com/index.html",
           "http://example.com/cgi-bin/myapp.app",
           "https://www.google.com/search"
        ]
    */

    absurls = urlutils.absUrl(sourceUrl, links, true);
    /* absurls = 
        [
           {
              "scheme": "http",
              "username": "",
              "password": "",
              "origin": "http://example.com",
              "host": "example.com",
              "authority": "//example.com",
              "path": "/images/",
              "fullPath": "/images/me.jpg",
              "queryString": {},
              "hash": "",
              "url": "http://example.com/images/me.jpg",
              "href": "http://example.com/images/me.jpg",
              "port": 80,
              "file": "me.jpg"
           },
           {
              "scheme": "http",
              "username": "",
              "password": "",
              "origin": "http://example.com",
              "host": "example.com",
              "authority": "//example.com",
              "path": "/",
              "fullPath": "/index.html",
              "queryString": {},
              "hash": "",
              "url": "http://example.com/index.html",
              "href": "http://example.com/index.html",
              "port": 80,
              "file": "index.html"
           },
           {
              "scheme": "http",
              "username": "",
              "password": "",
              "origin": "http://example.com",
              "host": "example.com",
              "authority": "//example.com",
              "path": "/cgi-bin/",
              "fullPath": "/cgi-bin/myapp.app",
              "queryString": {},
              "hash": "",
              "url": "http://example.com/cgi-bin/myapp.app",
              "href": "http://example.com/cgi-bin/myapp.app",
              "port": 80,
              "file": "myapp.app"
           },
           {
              "scheme": "https",
              "username": "",
              "password": "",
              "origin": "https://www.google.com",
              "host": "www.google.com",
              "authority": "//www.google.com",
              "path": "/",
              "fullPath": "/search",
              "queryString": {},
              "hash": "",
              "url": "https://www.google.com/search",
              "href": "https://www.google.com/search",
              "port": 443,
              "file": "search"
           }
        ]
    */
