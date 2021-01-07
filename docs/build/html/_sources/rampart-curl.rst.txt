The rampart-curl module
==============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-curl module uses the `libcurl <https://curl.se/libcurl/>`_
library.  Libcurl is a multi-protocol file transfer library with a long
history of feature rich and stable performance.  It also has an exceptionally
well written and documented API.  The authors of Rampart extend our thanks
to  Daniel Stenberg, et. al. for providing this library.


License
~~~~~~~

The rampart-curl module is released under the MIT license.  
`Libcurl <https://curl.se/libcurl/>`_ is custom licensed with an 
`MIT-like license <https://curl.se/docs/copyright.html>`_.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-curl module proviedss network client capabilities for the
``http``, ``https``, ``ftp``, ``smtp``, ``pop3`` and ``imap`` protocols. 
For the ``ftp``, ``http`` and ``https`` protocols, an asynchronous parallel fetch is
also provided.


How does it work?
~~~~~~~~~~~~~~~~~

The main functions ``fetch()`` and ``submit`` take a url (or several) and options in a
manner which is similar to the 
`curl command line tool <https://linux.die.net/man/1/curl>`_.  Results are
returned in an object, which includes status, headers and body text as
appropriate for the protocol being used.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var curl = require("rampart-curl");



Main Functions
--------------

The rampart-curl module is capable of posting to or retrieving data from
network resources as addressed by a URL.  The module exports two functions
which may be used for transfering data over the network: ``fetch()`` and
``submit``.  These two functions differ only in the format of the request
and how options are passed.  For retrieving a list of URLs where each
request uses one group of settings that do not change, and where new URLs
can be added to the list at any time, ``fetch()`` is the prefered tool. 
However for retrieving a list of URLs where each has separate settings or
post content ``submit()`` is the appropriate function.

fetch()
~~~~~~~

    The ``fetch`` function takes a :green:`String` or an :green`Array` of
    :green:`Strings`, each a URL, and an optional :green:`Object` of options and an
    optional callback :green:`Function`. 
    
    Usage:

    .. code-block:: javascript
    
        var curl = require("rampart-curl");
        
        var res = curl.fetch( url|url_list[, options][, callback]); 

    Where:
    
    * ``url`` is a :green:`String`, the URL of the resource to be accessed.
    
    *  ``url_list`` is an :green`Array` of :green:`Strings`, with each
      :green:`String` being an individual URL to fetch.

    *  ``options`` is an :green:`Object` of options, which correspond to the 
       `command line options <https://linux.die.net/man/1/curl>`_  (without
       the preceding ``-`` or ``--``) of the curl command line program.
       See `Curl Options`_ below.

    *  ``callback`` is a :green:`Function`, a function which takes as its
       sole argument, an :green:`Object` which will be set to the result and
       related information for each request.  The callback :green:`Function`
       is called once for each URL retrieved, asynchronously and in the
       order of completion (not necessarily in the order in ``url_list``.
       When a single ``url`` is provided, the callback is optional.  If a
       ``url_list`` :green`Array` is given, a callback must be provided.

    Return Value:
        If a callback is provided, returns undefined.  Otherwise the return
        value is an :green:`Object` containing the `fetch() Results`_.

fetch() Results
"""""""""""""""

    Results are either passed to the callback each time a URL request is
    returned, or if no callback and only one URL is provided, as the return
    value of the `fetch()`_ function.
    
    The results :green:`Object` contains minimally:
    
    * ``body`` - a :green:`Buffer` with the contents of the body of the
      reply.

    * ``status`` - if successful ``0`` or for the "http" or "https"
      protocols, the status code returned from the server.
      
    * ``statusText`` - The corresponding message for the ``status`` code (
      -i.e. for ``status`` 301, statusText is set to "Moved Permanently")
       
    * ``url`` - the request url as given to the `fetch()`_ function.

    * ``effectiveUrl`` - the url as returned from the server, possibly
      different from ``url`` if ``location`` is ``true`` (see 
      `Curl Options`_ below).

    Other possible properties of the results :green:`Object` are (not all
    apply to non-http requests):
    
    * ``text`` - a :green:`String` copy of the ``body`` contents if
      ``returnText`` is set ``true`` in `Curl Options`_.  Same as
      ``rampart.utils.bufferToString(result.body)``.  Note, internally this
      requires a copy of data, whereas the ``body`` green:`Buffer` is directly
      written to by the Curl library.  If the return data is large, it is
      far more efficient to have ``returnText`` set ``false``.

    * ``localIp`` - a :green:`String`, the IP address used to connect to the remote server.
    
    * ``localPort`` - a :green:`Number`, the port used to connect to the remote server.
    
    * ``serverIp`` - a :green:`String`, the IP address of the remote server.
    
    * ``serverPort`` -  a :green:`Number`, the remote server's port used for the connection.
    
    * ``rawHeader`` - a :green:`String`, the raw text of the header section of the reply.
    
    * ``headers`` - an :green:`Object`, the parsed header keys and values,
      if available.

    * ``httpVersion`` - a :green:`Number`, (``1.0``, ``1.1`` or ``2.0``).
      The http version used by the server.

    * ``totalTime`` - a :green:`Number`, the total time the Curl Library spend fetching the
      requested URL.

    * ``cookies`` - an :green:`Array`, a list of cookies sent from the
      remote server.

    Example of a single URL request with no callback:
    
    Example of a multiple URL request with a callback:


Adding More Requests
""""""""""""""""""""

    The fetch callback :green:`Function` takes a single argument (the
    `fetch() Results`_ :green:`Object` above).  In addition, more requests
    can be added to the list of URLs to be fetched inside the callback. This
    is accomplished by calling ``this.addurl("http://example.com/")`` from
    within the callback.
    
    Example:


submit()
~~~~~~~~

    The ``submit()`` function operates in a similar manner to the ``fetch()``
    function, with the following exceptions:
   
    * A callback function is required.
   
    * An :green:`Array` of :green:`Objects`, containing the property ``url``
     and any optional post data and `Curl Options`_ per request is provided.

    * New URLs cannot be added from within the callback.

    Usage:
   
    .. code-block:: javascript
    
        var curl = require("rampart-curl");

        var res = curl.submit(urls, callback); 

    Where:
    
    * ``urls`` is an :green:`Object` or an :green:`Array` of
      :green:`Objects`, one for each request with the property ``url`` set
      to the URL to be retrieved and other properties set as described in
      `Curl Options`_ below.

    * ``callback`` is a :green:`Function`, which takes as an argument the
      same :green:`Object` as returned in `fetch() Results`_ above.

Curl Options
------------

The following Curl Options are supported in rampart-curl. See 
`curl command line tool <https://linux.die.net/man/1/curl>`_
for a full description of each.

    * ``0`` - :green:`Boolean` - if ``true`` forces curl to issue its requests using HTTP 1.0
    * ``1`` - :green:`Boolean` - if ``true`` forces curl to use TLS version 1.
    * ``2`` - :green:`Boolean` - if ``true`` forces curl to use SSL version 2.
    * ``3`` - :green:`Boolean` - if ``true`` forces curl to use SSL version 3
    * ``4`` - :green:`Boolean` - if ``true`` forces curl to resolve names to IPv4 addresses only.
    * ``6`` - :green:`Boolean` - if ``true`` forces curl to resolve names to IPv6 addresses only.
    * ``A`` - same as ``user-agent``.
    * ``C`` - same as ``continue-at``.
    * ``E`` - same as ``cert``.
    * ``H`` - same as ``header``.
    * ``L`` - same as ``location``.
    * ``P`` - same as ``ftp-port``.
    * ``Q`` - same as ``quote``.
    * ``X`` - same as ``request``.
    * ``Y`` - same as ``speed-limit``.
    * ``anyauth`` - :green:`Boolean` - if ``true`` tells curl to figure out authentication method by itself, and use the most secure one.
    * ``b`` - same as ``cookie``.
    * ``basic`` - :green:`Boolean` - if ``true`` tells curl to use HTTP Basic authentication
    * ``c`` - 
    * ``cacert``
    * ``capath``
    * ``cert``
    * ``cert-status``
    * ``cert-type``
    * ``ciphers``
    * ``compressed``
    * ``compressed-ssh``
    * ``connect-timeout``
    * ``connect-to``
    * ``continue-at``
    * ``cookie``
    * ``cookie-jar``
    * ``crlf``
    * ``crlfile``
    * ``delegation``
    * ``digest``
    * ``digest-ie``
    * ``disable-eprt``
    * ``disable-epsv``
    * ``dns-interface``
    * ``dns-ipv4-addr``
    * ``dns-ipv6-addr``
    * ``dns-servers``
    * ``e``
    * ``expect100_timeout``
    * ``false-start``
    * ``ftp-account``
    * ``ftp-alternative-to-user``
    * ``ftp-create-dirs``
    * ``ftp-method``
    * ``ftp-port``
    * ``ftp-pret``
    * ``ftp-skip-pasv-ip``
    * ``ftp-ssl-ccc``
    * ``ftp-ssl-ccc-mode``
    * ``get``
    * ``header``
    * ``headers``
    * ``hostpubmd5``
    * ``http-any``
    * ``http1.0``
    * ``http1.0``
    * ``http1.1``
    * ``http2``
    * ``http2-prior-knowledge``
    * ``ignore-content-length``
    * ``insecure``
    * ``interface``
    * ``ipv4``
    * ``ipv6``
    * ``j``
    * ``junk-session-cookies``
    * ``k``
    * ``keepalive-time``
    * ``key``
    * ``key-type``
    * ``krb``
    * ``l``
    * ``limit-rate``
    * ``list-only``
    * ``local-port``
    * ``location``
    * ``location-trusted``
    * ``login-options``
    * ``m``
    * ``mail-auth``
    * ``mail-from``
    * ``mail-msg``
    * ``mail-rcpt``
    * ``max-filesize``
    * ``max-redirs``
    * ``max-time``
    * ``n``
    * ``negotiate``
    * ``netrc``
    * ``netrc-file``
    * ``netrc-optional``
    * ``no-alpn``
    * ``no-keepalive``
    * ``no-npn``
    * ``no-sessionid``
    * ``noproxy``
    * ``ntlm``
    * ``ntlm-wb``
    * ``pass``
    * ``path-as-is``
    * ``pinnedpubkey``
    * ``post``
    * ``post301``
    * ``post302``
    * ``post303``
    * ``postbin``
    * ``postform``
    * ``postredir``
    * ``preproxy``
    * ``proto-default``
    * ``proto-redir``
    * ``proxy``
    * ``proxy-anyauth``
    * ``proxy-basic``
    * ``proxy-cacert``
    * ``proxy-capath``
    * ``proxy-cert``
    * ``proxy-cert-type``
    * ``proxy-ciphers``
    * ``proxy-crlfile``
    * ``proxy-digest``
    * ``proxy-digest-ie``
    * ``proxy-header``
    * ``proxy-insecure``
    * ``proxy-key``
    * ``proxy-key-type``
    * ``proxy-negotiate``
    * ``proxy-ntlm``
    * ``proxy-ntlm-wb``
    * ``proxy-pass``
    * ``proxy-service-name``
    * ``proxy-ssl-allow-beast``
    * ``proxy-ssl-no-revoke``
    * ``proxy-tlspassword``
    * ``proxy-tlsuser``
    * ``proxy-tlsv1``
    * ``proxy-user``
    * ``proxy1.0``
    * ``proxytunnel``
    * ``pubkey``
    * ``quote``
    * ``r``
    * ``random-file``
    * ``range``
    * ``referer``
    * ``request``
    * ``request-target``
    * ``resolve``
    * ``sasl-ir``
    * ``service-name``
    * ``socks4``
    * ``socks4a``
    * ``socks5``
    * ``socks5-basic``
    * ``socks5-gssapi``
    * ``socks5-gssapi-nec``
    * ``socks5-gssapi-service``
    * ``socks5-hostname``
    * ``speed-limit``
    * ``speed-time``
    * ``ssl``
    * ``ssl-allow-beast``
    * ``ssl-no-revoke``
    * ``ssl-reqd``
    * ``sslv2``
    * ``sslv3``
    * ``suppress-connect-headers``
    * ``t``
    * ``tcp-fastopen``
    * ``tcp-nodelay``
    * ``telnet-option``
    * ``tftp-blksize``
    * ``tftp-no-options``
    * ``time-cond``
    * ``tls-max``
    * ``tlspassword``
    * ``tlsuser``
    * ``tlsv1``
    * ``tlsv1.0``
    * ``tlsv1.1``
    * ``tlsv1.2``
    * ``tlsv1.3``
    * ``tr-encoding``
    * ``u``
    * ``unix-socket``
    * ``user``
    * ``user-agent``
    * ``x``
    * ``y``
    * ``z``
