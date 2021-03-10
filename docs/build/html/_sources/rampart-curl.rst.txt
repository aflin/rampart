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

The rampart-curl module provides network client capabilities for the
``http``, ``https``, ``ftp``, ``smtp``, ``pop3`` and ``imap`` protocols. 
For the ``ftp``, ``http`` and ``https`` protocols, an asynchronous parallel fetch is
also provided.


How does it work?
~~~~~~~~~~~~~~~~~

The main functions ``fetch()`` and ``submit()`` take a URL (or several) and options in a
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
which may be used for transferring data over the network: ``fetch()`` and
``submit``.  These two functions differ only in the format of the request
and how options are passed.  For retrieving a list of URLs where each
request uses one group of settings that do not change, and where new URLs
can be added to the list at any time, ``fetch()`` is the preferred tool. 
However for retrieving a list of URLs where each has separate settings or
post content ``submit()`` is the appropriate function.

fetch()
~~~~~~~

    The ``fetch`` function takes a :green:`String` or an :green:`Array` of
    :green:`Strings`, each a URL, and an optional :green:`Object` of options and an
    optional callback :green:`Function`. 
    
    Usage:

    .. code-block:: javascript
    
        var curl = require("rampart-curl");
        
        var res = curl.fetch( url|url_list[, options][, callback]); 

    Where:
    
    * ``url`` is a :green:`String`, the URL of the resource to be accessed.
    
    *  ``url_list`` is an :green:`Array` of :green:`Strings`, with each
       :green:`String` being an individual URL to fetch.

    *  ``options`` is an :green:`Object` of options, which correspond to 
       `curl command line options <https://linux.die.net/man/1/curl>`_  (without
       the preceding ``-`` or ``--``).
       See `Curl Options`_ below.

    *  ``callback`` is a :green:`Function`, a function which takes as its
       sole argument, an :green:`Object` which will be set to the result and
       related information for each request.  The callback :green:`Function`
       is called once for each URL retrieved, asynchronously and in the
       order of completion (not necessarily in the order in ``url_list``.
       When a single ``url`` is provided, the callback is optional.  If a
       ``url_list`` :green:`Array` is given, a callback must be provided.

    Return Value:
        If a callback is provided, returns ``undefined``.  Otherwise the return
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
      ``returnText`` is set ``true`` in `Curl Options`_.  It is set to the
		value equivalent to ``rampart.utils.bufferToString(result.body)``.  Note, internally
		this requires a copy of data, whereas the ``body`` :green:`Buffer` is
		directly written to by the Curl library.  If the return data is large,
		it is more efficient to have ``returnText`` set ``false``.

    * ``localIp`` - a :green:`String`, the IP address used to connect to the remote server.
    
    * ``localPort`` - a :green:`Number`, the port used to connect to the remote server.
    
    * ``serverIp`` - a :green:`String`, the IP address of the remote server.
    
    * ``serverPort`` -  a :green:`Number`, the remote server's port used for the connection.
    
    * ``rawHeader`` - a :green:`String`, the raw text of the header section of the reply.
    
    * ``headers`` - an :green:`Object`, the parsed header keys and values,
      if available.

    * ``httpVersion`` - a :green:`Number`, (``1.0``, ``1.1`` or ``2.0``).
      The http version used by the server if URL is an http request.

    * ``totalTime`` - a :green:`Number`, the total time the Curl Library spend fetching the
      requested URL.

    * ``cookies`` - an :green:`Array`, a list of cookies sent from the
      remote server.


Adding More Requests
""""""""""""""""""""

    The fetch callback :green:`Function` takes a single argument (the
    `fetch() Results`_ :green:`Object` above).  In addition, more requests
    can be added to the list of URLs to be fetched inside the callback. This
    is accomplished by calling ``this.addurl("http://example.com/")`` from
    within the callback.
    
    See `Multiple HTTP request with addurl()`_ example below.


submit()
~~~~~~~~

    The ``submit()`` function operates in a similar manner to the ``fetch()``
    function, with the following exceptions:
   
    * A callback function is required.
   
    * An :green:`Object` or an :green:`Array` of :green:`Objects`, containing
      the property ``url`` and any `Curl Options`_ per
      request must be provided.

    * New URLs cannot be added from within the callback.

    Usage:
   
    .. code-block:: javascript
    
        var curl = require("rampart-curl");

        var res = curl.submit(request, callback); 

    Where:
    
    * ``request`` is an :green:`Object` or an :green:`Array` of
      :green:`Objects`, one :green:`Object` for each request with the
      property ``url`` set to the URL to be retrieved and other properties
      set as described in `Curl Options`_ below.

    * ``callback`` is a :green:`Function`, which takes as an argument the
      same :green:`Object` as returned in `fetch() Results`_ above.

Curl Options
------------

The following Options are available in rampart-curl.


Curl long options
~~~~~~~~~~~~~~~~~

Some of the options below have not been fully tested. The descriptions below are an
abbreviated version of the man page (written by Daniel Stenberg, et. al.). 
See `curl command line tool <https://linux.die.net/man/1/curl>`_ for a full
description of each.  

Supported Options
"""""""""""""""""

    * ``anyauth`` - :green:`Boolean` - if ``true`` tells curl to figure out authentication method by itself, and use the most secure one.
    * ``basic`` - :green:`Boolean` - if ``true`` tells curl to use HTTP Basic authentication
    * ``cacert`` - :green:`String` - Tells curl to use the specified certificate file to verify the peer.
    * ``capath`` - :green:`String` - Tells curl to use the specified certificate directory to verify the peer.
    * ``cert`` - :green:`String` - Tells curl to use the specified certificate file when getting a file with HTTPS or FTPS.
    * ``cert-status`` - :green:`Boolean` - Tells  curl to verify the status of the server certificate by using the Certificate Status Request (aka. OCSP stapling) TLS extension.
    * ``cert-type`` - Tells curl what certificate type the provided certificate is in. PEM, DER and ENG are recognized types.
    * ``ciphers`` - :green:`String` - Specifies  which ciphers to use in the connection. See `SSL Ciphers <https://curl.se/docs/ssl-ciphers.html>`_.
    * ``compressed`` - :green:`Boolean` - Request  a compressed response using one of the algorithms curl supports (``br``, ``gzip`` or ``deflate``), and return the uncompressed document.
    * ``connect-timeout`` - :green:`Number` - Maximum time to spend in the connection phase of a transaction.
    * ``connect-to`` - :green:`String` - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``continue-at`` - :green:`Number` - Skip this number of bytes from the beginning of the source file.
    * ``cookie`` - :green:`String` - Send cookie with request. Provided in format ``NAME1=VALUE1; NAME2=VALUE2``.
    * ``cookie-jar`` - :green:`String` - File from which to save and retrieve cookies.
    * ``crlf`` - :green:`Boolean` - For FTP and STMP -  Convert LF to CRLF in upload.
    * ``crlfile`` - :green:`String` - Provide a file using PEM format with a Certificate Revocation List that may specify peer certificates that are to be considered revoked.
    * ``digest`` - :green:`Boolean` - Enables HTTP Digest authentication.
    * ``digest-ie`` - :green:`Boolean` - Same as ``digest``, except curl will use a special "quirk" that IE is known to have used before version 7 and that some servers require the client to use.
    * ``disable-eprt`` - :green:`Boolean` - For FTP - Disable the use of the EPRT an LPRT commands.
    * ``disable-epsv`` - :green:`Boolean` - For FTP - Disable the use of the  EPSV Command.
    * ``expect100_timeout`` - :green:`Number` - Maximum time in seconds that you allow curl to wait for a 100-continue response.
    * ``ftp-account`` - :green:`String` - ``ACCT`` data to send when an FTP server asks for "account data".
    * ``ftp-alternative-to-user`` - :green:`String` - Authentication using using "SITE AUTH" command. See `man page <https://linux.die.net/man/1/curl>`_.
    * ``ftp-create-dirs`` - :green:`Boolean` -Tells curl to attempt to create missing directories rather than fail.
    * ``ftp-method`` - :green:`String` - One of ``multicwd``, ``nocwd`` or ``singlecwd``.  See `man page <https://linux.die.net/man/1/curl>`_.
    * ``ftp-port`` - :green:`String` - Interface name, IP address or host name for FTP active mode.  See `man page <https://linux.die.net/man/1/curl>`_.
    * ``ftp-pret`` - :green:`Boolean` - Tell curl to send a PRET command before PASV (and EPSV). Certain FTP servers, mainly drftpd, require this.
    * ``ftp-skip-pasv-ip`` - :green:`Boolean` - Tell curl to not use the IP address the server suggests in its response to curl's PASV command.
    * ``header`` - same as ``headers``.
    * ``headers`` - :green:`Array` or :green:`String` - a header or list of headers to send with the request. 
    * ``http-any`` - :green:`Boolean` - When set ``true``, curl will use whatever http version it thinks fit.
    * ``http1.0`` - :green:`Boolean` - Tell curl to use HTTP 1.0 requests.
    * ``http1.1`` - :green:`Boolean` - Tell curl to use HTTP 1.1 requests.
    * ``http2`` - :green:`Boolean` - Tell curl to use HTTP 2.0 requests.
    * ``http2-prior-knowledge`` - :green:`Boolean` - Issue non-TLS HTTP requests using HTTP/2 without HTTP/1.1 Upgrade. It requires prior knowledge that the server supports HTTP/2 straight away.
    * ``ignore-content-length`` - :green:`Boolean` - Ignore the Content-Length header.
    * ``insecure`` - :green:`Boolean` - Skip server certificate check. See `man page <https://linux.die.net/man/1/curl>`_.
    * ``interface`` - :green:`String` - Perform an operation using a specified interface. You can enter interface name, IP address or host name. See `man page <https://linux.die.net/man/1/curl>`_.
    * ``ipv4`` - :green:`Boolean` - Tell curl to resolve IPv4 addresses only.
    * ``ipv6`` - :green:`Boolean` - Tell curl to resolve IPv6 addresses only.
    * ``junk-session-cookies`` - When using ``cookie`` or ``cookie-jar``, discard all session cookies.
    * ``keepalive-time`` - :green:`Number` - The  time  a  connection needs to remain idle before sending keepalive probes and the time between individual keepalive probes.
    * ``limit-rate`` - :green:`Number` - Specify  the  maximum transfer rate you want curl to use.
    * ``list-only`` - :green:`Boolean` - When listing an FTP directory, this switch forces a name-only view.
    * ``local-port``  :green:`Number`  or :green:`Array` of two :green:`Numbers` - set a preferred single number or range (FROM-TO) of local port numbers to use for the connection.
    * ``location`` - :green:`Boolean` - Tell curl to follow ``3xx`` redirect requests.
    * ``location-trusted`` - Like  ``location``, but will allow sending the name + password to all hosts that
      the site may redirect to.  See `man page <https://linux.die.net/man/1/curl>`_ for security ramifications.

    * ``mail-auth`` - :green:`String` - For SMTP - an address to be used to specify the  authentication address
      (identity) of a submitted message that is being relayed to another server

    * ``mail-from`` - :green:`String` - For SMTP - Specify a single address that the given mail should get sent from.

    * ``mail-rcpt`` - a :green:`String` or an :green:`Array` of :green:`Strings` - Recipient email address or addresses.
    * ``max-filesize`` - a :green:`Number` - the maximum size (in bytes) of a file to download.  If the size is exceeded, the ``errMsg:`` 
      property in the return value will be set to ``"curl failed: Maximum file size exceeded"`` and ``text``/``body`` will be empty.

    * ``max-redirs`` - a :green:`Number` - if ``location`` above is set true, the maximum number of redirects to follow.
    * ``max-time`` - a :green:`Number` - the maximum time in seconds that the whole operation is allowed to take.
    * ``no-keepalive`` - a :green:`Boolean` - Disables  the use of keepalive messages on the TCP connection.
    * ``noproxy`` - a :green:`String` - a comma-separated list of hosts which do not use a proxy, if one is specified in ``proxy`` below.
    * ``pass`` - a :green:`String` - Password for authentication.
    * ``path-as-is`` - a :green:`Boolean` - if ``true``, tells curl to not squash or merge ``/../`` or ``/./`` in the given URL path.

    * ``post301`` - a :green:`Boolean` - For HTTP with ``location`` set ``true``. If ``true`` do not  convert  POST  requests  into  GET
      requests  when  following  a  301  redirection.

    * ``post302`` - a :green:`Boolean` - Same as ``post301`` but for 302 redirection.
    * ``post303`` - a :green:`Boolean` - Same as ``post301`` but for 303 redirection.
    * ``postredir`` - Set (or clear if ``false``) all of ``post301``, ``post302`` and ``post303``.  Options are taken in order, so that
      ``{"postredir": true, "post303": false}`` will set ``post301`` and ``post302`` ``true``.

    * ``proto-default`` - a :green:`String` - Tells curl to use protocol for any URL missing a scheme name.
    * ``proto-redir`` - a :green:`String` - Tells curl to limit what protocols it may use on redirect. See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy`` - a :green:`String` - set the name of a proxy server to use (``[protocol://]host[:port]``).

    * ``range`` - a :green:`String` - Retrieve a byte range (i.e a partial document). See `man page <https://linux.die.net/man/1/curl>`_ for examples.
    * ``referer`` - a :green:`String` - Set the "Referer" header.
    * ``request`` - a :green:`String` - Specify a custom request for HTTP (i.e. ``GET``, ``PUT``, ``DELETE``, etc.).
    * ``request-target`` - a :green:`String` - Specify an alternative "target" (path) instead of using the path as provided in the URL.
    * ``resolve`` - a :green:`String` or an :green:`Array` of :green:`Strings` - Provide custom resolve mappings of host/port to ip.  Format is ``host:port:address``.
      See `man page <https://linux.die.net/man/1/curl>`_ for details.

    * ``speed-limit`` - a :green:`Number` - If a download is slower than this given speed (in bytes per second) for speed-time seconds it gets aborted.
    * ``speed-time`` - a :green:`Number` - If a download is slower than speed-limit bytes per second during a speed-time period,  the
      download gets aborted.  If limit is reached, the return value will contain ``errMsg: "curl failed: Timeout was reached"``.

    * ``ssl`` - a :green:`Boolean` - Try to use SSL/TLS for the connection.  Reverts to a non-secure  connection  if the server doesn't support SSL/TLS.
    * ``ssl-reqd`` - a :green:`Boolean` - For FTP IMAP POP3 SMTP, require SSL/TLS for the connection.
    * ``sslv2`` - a :green:`Boolean` - Forces  curl  to use SSL version 2 when negotiating with a remote SSL server. See `man page <https://linux.die.net/man/1/curl>`_.
    * ``sslv3`` - a :green:`Boolean` - Forces  curl  to use SSL version 3 when negotiating with a remote SSL server. See `man page <https://linux.die.net/man/1/curl>`_.
    * ``time-cond`` - a :green:`Date` - *Differs from Command Line option* - date is used to set ``"If-Modified-Since"`` header.
    * ``tls-max`` - a :green:`String` - Maximum  supported TLS version. May be one of ``"1.0"``, ``"1.1"``, ``"1.2"`` or ``"1.3"``.
    * ``tlsv1`` - a :green:`Boolean` - Tells  curl  to use TLS version 1.x when negotiating with a remote TLS server.
    * ``tlsv1.0`` - a :green:`Boolean` - Forces curl to use TLS version 1.0 when connecting to a remote TLS server.
    * ``tlsv1.1`` - a :green:`Boolean` - Forces curl to use TLS version 1.1 when connecting to a remote TLS server.
    * ``tlsv1.2`` - a :green:`Boolean` - Forces curl to use TLS version 1.2 when connecting to a remote TLS server.
    * ``tlsv1.3`` - a :green:`Boolean` - Forces curl to use TLS version 1.3 when connecting to a remote TLS server.
    * ``tr-encoding`` - a :green:`Boolean` - Request  a  compressed Transfer-Encoding response using one of the algorithms curl
      supports, and uncompress the data while receiving it.

    * ``user`` - a :green:`String` - Specify the user name and password to use for server authentication.  Specified as either a ``username``
      (with password set in ``pass`` above), or as ``username:password``.

    * ``user-agent`` - a :green:`String` - Specify the User-Agent string to send to the HTTP server.  The default if not set is a :green:`String`
      specifying ``libcurl-rampart/`` + version number.

Unsupported Extras
""""""""""""""""""

    * ``ftp-ssl-ccc`` - :green:`Boolean` -  *Untested* - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``ftp-ssl-ccc-mode``  :green:`Boolean` -  *Untested* - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``login-options`` - :green:`String` - *Untested* - For IMAP, POP3 and SMTP.  See `man page <https://linux.die.net/man/1/curl>`_.
    * ``negotiate``- a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``netrc`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``netrc-file`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``netrc-optional`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``no-alpn``  - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``no-npn`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``no-sessionid`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``ntlm`` - a :green:`Boolean` - **Untested** - Enables NTLM authentication.
    * ``ntlm-wb`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``pinnedpubkey`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``preproxy`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-anyauth`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-basic`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-cacert`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-capath`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-cert``  - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-cert-type``  - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-ciphers``  - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-crlfile``  - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-digest`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-digest-ie`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-header``  - a :green:`String` or a :green:`Array` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-insecure`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-key`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-key-type`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-negotiate`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-ntlm``  - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-ntlm-wb`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-pass`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-service-name`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-ssl-allow-beast`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-tlspassword`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-tlsuser`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-tlsv1`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy-user`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxy1.0`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``proxytunnel`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``pubkey`` - a :green:`String` - **Untested** - Public key file name. Allows you to provide 
      your public key  in  this  separate file.

    * ``quote`` - a :green:`String` or :green:`Array` of :green:`Strings` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_. 
    * ``random-file`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``sasl-ir`` - a :green:`Boolean` - **Untested** - Enable initial response in SASL authentication.
    * ``service-name`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks4`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks4a`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks5`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks5-basic``  - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks5-gssapi`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks5-gssapi-nec`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks5-gssapi-service`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``socks5-hostname`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``ssl-allow-beast`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``suppress-connect-headers`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``tcp-fastopen`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``tcp-nodelay`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``telnet-option`` - a :green:`String` or :green:`Array` of :green:`Strings` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``tftp-blksize`` - a :green:`Number` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``tftp-no-options`` - a :green:`Boolean` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``tlspassword`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``tlsuser`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.
    * ``unix-socket`` - a :green:`String` - **Untested** - See `man page <https://linux.die.net/man/1/curl>`_.




Additional options
~~~~~~~~~~~~~~~~~~

The following are additional options provided by the JavaScript module and
have no corresponding setting in the `curl command line tool <https://linux.die.net/man/1/curl>`_.
Note that `examples`_ are provided below.

    * ``arrayType`` - :green:`String` - How to translate arrays into
      parameters for ``get`` and ``post`` below.  See
      :ref:`rampart-main:objectToQuery`.

    * ``get`` - :green:`Object` or :green:`String` - **THIS OPTION DIFFERS FROM CURL COMMAND LINE**.
      If an :green:`Object` is provided, it is converted to a
      :green:`String` using :ref:`rampart-main:objectToQuery` first.  The
      :green:`String` is then joined to the URL using a ``?``.  See also
      ``arrayType`` above.

    * ``mail-msg`` - :green:`String` or :green:`Object` - For SMTP, set
      content of a mail message.

        *  If a :green:`String`, the string must be a pre-formatted email
           message with headers and body.

        *  If an :green:`Object`, The :green:`Object` will be used to create
          the message, using the following properties:

           * ``date``: a :green:`String` or a :green:`Date Object` - for the ``Date`` header value.
           * ``to``: a :green:`String` - for the ``To`` header value.
           * ``from``: a :green:`String` - for the ``From`` header value.
           * ``subject``: a :green:`String` - for the ``Subject`` header value.
           * ``xxx``: a :green:`String` - for any other desired header value (e.g. ``cc``).
           * ``message``: a :green:`String` or an :green:`Object` - the content of the email message.

              If ``message`` is set to a :green:`String`, the content of the
              :green:`String` is sent as is.

              If ``message`` is set to an :green:`Object`, The following may
              be set (and one or both of ``html`` or ``text`` must be set):

                * ``html`` send the :green:`String` in a multipart mime
                  message with this part's mime type set to ``text/html``.

                * ``text`` send the :green:`String` in a multipart mime
                  message with this part's mime type set to ``text/plain``.

                * ``attach`` - an :green:`Array` of :green:`Objects`.  Each
                  object may have following properties (and must have
                  ``data``):

                   * ``data`` - a :green:`Buffer` or a :green:`String` set
                     to the data to be sent as an attachment.  If a
                     :green:`String` that starts with ``@``, the data will
                     be read from a file (e.g.  ``data:
                     "@/path/to/my/pic.jpg"``).
                   
                   * ``name`` - optionally a :green:`String` which will be
                     set in the ``Content-Disposition: attachment; name=``
                     header.
                   
                   * ``type`` - optionally a :green:`String` which will be
                     set in the ``Content-Type:`` header.
                   
                   * ``cid`` - optionally a :green:`String` or
                     :green:`Boolean` which, if a :green:`String` will be
                     set to the ``Content-ID:``/``X-Attachment-Id:``
                     headers.  If ``cid`` is not present or set to ``true``,
                     the ``Content-ID`` will be set to the value of
                     ``name``, or if ``cid`` is set to ``false``, no
                     ``Content-ID`` header is set.
                   
              Note that if ``html`` and ``text`` are both set, they are sent
              embedded in a ``"multipart/alternative"`` part.  Both ``html``
              and ``text`` parts are encoded as ``quoted-printable``. 
              Attachment ``data`` is encoded as ``base64``.

    * ``post`` - a :green:`String`, :green:`Buffer` or  :green:`Object`. 
      For POSTing content to a server using HTTP or HTTPS.

      * If an :green:`Object`, data is automatically converted and posted
        similar to ``get`` using :ref:`rampart-main:objectToQuery`.
      
      * If a :green:`String` or :green:`Buffer`, data is sent as is.  By
        default the header ``Content-Type: application/x-www-form-urlencoded`` 
        is sent with the request.  If data is a :green:`String` or
        :green:`Buffer`, and is not pre-formatted, url-encoded data,
        ``header`` above should be used to set the appropriate content type.
        
        If data is a :green:`String`, and it starts with ``@``, the data
        will be read from a file (e.g.  
        ``{post: "@/path/to/my/pic.jpg", header: "Content-Type: image/jpeg"}``).

    * ``postform`` - an :green:`Object`. For POSTing with ``Content-Type: multipart/form-data``
      where each key/property sets the the name in each part of the posted data (i.e. - 
      ``Content-Disposition: form-data; name=``), and the value is a :green:`String`,
      :green:`Buffer`, :green:`Object` or :green:`Array` as follows:

        * If a :green:`String` or :green:`Buffer`, data is sent as is.
        
          If data is a :green:`String`, and it starts with ``@``, the data
          will be read from a file (e.g.  ``{myvarname: "@/path/to/my/pic.jpg"}``).

        * If an :green:`Object`, and the :green:`Object` has the key/property
          ``data`` set, data will be sent as is (if value a :green:`String` or
          :green:`Buffer`), or if a :green:`String`, and it starts with ``@``
          it will be read from a file, or if it is an :green:`Object` or 
          :green:`Array`, it will be sent as JSON.
          In addition to ``data``, the properties ``filename`` (to set the filename
          header) and ``type`` (to set the ``Content-Type`` header) may also be set
          to an appropriate :green:`String`.

        * If an :green:`Object`, and the :green:`Object` has the key/property
          ``data`` is **NOT** set, the Object is converted to JSON.
  
        * If an :green:`Array`, it must be an :green:`Array` of green:`Object`
          with ``data`` and optionally ``filename`` and/or ``type`` set as above.


Curl short options
~~~~~~~~~~~~~~~~~~

    * ``0`` - same as ``http-1.0``.
    * ``1`` - same as ``tlsv1``.
    * ``2`` - same as ``sslv2``.
    * ``3`` - same as ``sslv3``.
    * ``4`` - same as ``ipv4``.
    * ``6`` - same as ``ipv6``.
    * ``A`` - same as ``user-agent``.
    * ``C`` - same as ``continue-at``.
    * ``E`` - same as ``cert``.
    * ``H`` - same as ``header``.
    * ``L`` - same as ``location``.
    * ``P`` - same as ``ftp-port``.
    * ``Q`` - same as ``quote``.
    * ``X`` - same as ``request``.
    * ``Y`` - same as ``speed-limit``.
    * ``b`` - same as ``cookie``.
    * ``c`` - same as ``cookie-jar``.
    * ``e`` - same as ``referer``.
    * ``j`` - same as ``junk-session-cookies``
    * ``k`` - same as ``insecure``.
    * ``l`` - same as ``list-only``.
    * ``m`` - same as ``max-time``.
    * ``n`` - same as ``netrc``.
    * ``r`` - same as ``range``.
    * ``u`` - same as ``user``.
    * ``x`` - same as ``proxy``.
    * ``y`` - same as ``speed-time``.
    * ``z`` - same as ``time-cond``.

Examples
--------

Each example with a request to ``localhost:8088`` below shows the output
from requests made to the following script using the :ref:`rampart-server
module <rampart-server:The rampart-server HTTP module>`.

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    /* load the http server module */
    var server=require("rampart-server");

    /* echo the request back to the client */
    function echo(res)
    {
        var keys, key, val, i;

        /* convert the body to text for easy viewing */
        res.body=bufferToString(res.body);

        /* convert any params.* Buffers to text for easy viewing */
        keys=Object.keys(res.params)    
        for (i=0;i<keys.length;i++) {
            key=keys[i];
            val=res.params[key];
            if (getType(val) == "Buffer")
                res.params[key]= bufferToString(val);
        }

        /* same for postData */
        if(res.postData && res.postData.content && getType(res.postData.content)=="Array")
        {
            var arr = res.postData.content;
            for (i=0;i<arr.length;i++) {
                if (arr[i].content && getType(arr[i].content)=="Buffer")
                    arr[i].content = bufferToString(arr[i].content);
            }
        }
        if (res.postData && getType(res.postData.content)=="Buffer")
            res.postData.content = bufferToString(res.postData.content);

        return {text: sprintf("%4J\n", res)} ;

    }


    var pid=server.start(
    {
        user: "nobody",
        log: true,
        map:
        {
            "/": "/usr/local/rampart/examples/sample-server/mPurpose",
            "/echo.txt" : echo
        }
    });


Simple HTTP Get request
~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    var curl = require("rampart-curl");

    var ret=curl.fetch("http://localhost:8088/echo.txt");

    console.log(ret.text);
    /* expected output:
    {
        "ip": "127.0.0.1",
        "port": 41838,
        "method": "GET",
        "path": {
            "file": "echo.txt",
            "path": "/echo.txt",
            "base": "/",
            "scheme": "http://",
            "host": "localhost:8088",
            "url": "http://localhost:8088/echo.txt"
        },
        "query": {},
        "body": "",
        "query_raw": "",
        "headers": {
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Accept": "*/*"
        },
        "params": {
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Accept": "*/*"
        }
    }
    */

Multiple HTTP Get Requests
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    var curl = require("rampart-curl");

    curl.fetch(["http://localhost:8088/echo.txt","http://google.com/"],
      function(res) {
        res.body = rampart.utils.bufferToString(res.body);
        printf("%4J\n", res);
      }
    );

    /* expected results:
    {
        "statusText": "OK",
        "status": 200,
        "text": "{\n    \"ip\": \"127.0.0.1\",\n    \"port\": 41888,\n    \"method\": \"GET\",\n    \"path\": {\n        \"file\": \"echo.txt\",\n        \"path\": \"/echo.txt\",\n        \"base\": \"/\",\n        \"scheme\": \"http://\",\n        \"host\": \"localhost:8088\",\n        \"url\": \"http://localhost:8088/echo.txt\"\n    },\n    \"query\": {},\n    \"body\": \"\",\n    \"query_raw\": \"\",\n    \"headers\": {\n        \"Host\": \"localhost:8088\",\n        \"User-Agent\": \"libcurl-rampart/0.1\",\n    },\n    \"params\": {\n        \"Host\": \"localhost:8088\",\n        \"User-Agent\": \"libcurl-rampart/0.1\",\n    }\n}\n",
        "body": "{\n    \"ip\": \"127.0.0.1\",\n    \"port\": 41888,\n    \"method\": \"GET\",\n    \"path\": {\n        \"file\": \"echo.txt\",\n        \"path\": \"/echo.txt\",\n        \"base\": \"/\",\n        \"scheme\": \"http://\",\n        \"host\": \"localhost:8088\",\n        \"url\": \"http://localhost:8088/echo.txt\"\n    },\n    \"query\": {},\n    \"body\": \"\",\n    \"query_raw\": \"\",\n    \"headers\": {\n        \"Host\": \"localhost:8088\",\n        \"User-Agent\": \"libcurl-rampart/0.1\",\n    },\n    \"params\": {\n        \"Host\": \"localhost:8088\",\n        \"User-Agent\": \"libcurl-rampart/0.1\",\n    }\n}\n",
        "effectiveUrl": "http://localhost:8088/echo.txt",
        "url": "http://localhost:8088/echo.txt",
        "localIP": "127.0.0.1",
        "localPort": 41888,
        "serverIP": "127.0.0.1",
        "serverPort": 8088,
        "rawHeader": "HTTP/1.1 200 OK\r\nDate: Mon, 11 Jan 2021 06:46:34 GMT\r\nContent-Type: text/plain\r\nContent-Length: 583\r\n\r\n",
        "headers": {
            "STATUS": "HTTP/1.1 200 OK",
            "Date": "Mon, 11 Jan 2021 06:46:34 GMT",
            "Content-Type": "text/plain",
            "Content-Length": "583"
        },
        "httpVersion": 1.1,
        "totalTime": 0.000979,
        "errMsg": ""
    }
    {
        "statusText": "Moved Permanently",
        "status": 301,
        "text": "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n<TITLE>301 Moved</TITLE></HEAD><BODY>\n<H1>301 Moved</H1>\nThe document has moved\n<A HREF=\"http://www.google.com/\">here</A>.\r\n</BODY></HTML>\r\n",
        "body": "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n<TITLE>301 Moved</TITLE></HEAD><BODY>\n<H1>301 Moved</H1>\nThe document has moved\n<A HREF=\"http://www.google.com/\">here</A>.\r\n</BODY></HTML>\r\n",
        "effectiveUrl": "http://google.com/",
        "url": "http://google.com/",
        "localIP": "2001:470:88:88:ec4:7aff:fe44:77d6",
        "localPort": 51186,
        "serverIP": "2607:f8b0:4005:805::200e",
        "serverPort": 80,
        "rawHeader": "HTTP/1.1 301 Moved Permanently\r\nLocation: http://www.google.com/\r\nContent-Type: text/html; charset=UTF-8\r\nDate: Mon, 11 Jan 2021 06:46:34 GMT\r\nExpires: Wed, 10 Feb 2021 06:46:34 GMT\r\nCache-Control: public, max-age=2592000\r\nServer: gws\r\nContent-Length: 219\r\nX-XSS-Protection: 0\r\nX-Frame-Options: SAMEORIGIN\r\n\r\n",
        "headers": {
            "STATUS": "HTTP/1.1 301 Moved Permanently",
            "Location": "http://www.google.com/",
            "Content-Type": "text/html; charset=UTF-8",
            "Date": "Mon, 11 Jan 2021 06:46:34 GMT",
            "Expires": "Wed, 10 Feb 2021 06:46:34 GMT",
            "Cache-Control": "public, max-age=2592000",
            "Server": "gws",
            "Content-Length": "219",
            "X-XSS-Protection": "0",
            "X-Frame-Options": "SAMEORIGIN"
        },
        "httpVersion": 1.1,
        "totalTime": 0.057215,
        "errMsg": ""
    }
    */


Multiple HTTP request with addurl()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There may be cases where, while fetching HTML pages, more resources are discovered and can be 
added to the list of URLs that ``fetch()`` will retrieve.  The following example uses
:ref:`the rampart-html module <rampart-html:The rampart-server HTML module>` to extract
links from the ``index.html`` page on the sample website in the ``/usr/local/rampart/examples``
directory, and "crawl" the rest of the site.

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    var curl = require("rampart-curl");

    var http = require("rampart-http");

    /* use an object to keep a list of unique urls */
    var fetched = { "http://localhost:8088/" : true };

    /* 
       remove "/index.html" from link, return false if not html 
       or if html, but from another domain
       or if we've already put this one on the list
    */
    function sanitize_link(l) {

      l=l.replace(/#.*/, '');
      l=l.replace(/\/index.html?$/, "/");

      if ( 
           /* if it starts with http://localhost, or if it doesnt have a '://' */
           (l.match(/^https?:\/\/localhost:8088\//) || !l.match(/:\/\//) )
           && 
           /* and if it ends in a '/' or a '.html' */
           (l.match(/\/$/) || l.match(/\.html?$/) ) 
         )
      {
          /* if not on the list */
          if (!fetched[l])
          {
            /* add it to the list */
            fetched[l]=true;
            /* fix relative link */
            if (!l.match(/https?:\/\//))
            {
              if(l.charAt(0) == '/')
                l = "http://localhost:8088" + l;
              else
                l = "http://localhost:8088/" + l;
            }
            return(l);
          }
      }
      return false;
    }

    function savedoc(doc, title) {
      printf ("saving %s\n", doc.effectiveUrl);
      /* save code goes here */
    }

    curl.fetch( "http://localhost:8088/", function(res) {

      /* check if successful */
      if(res.status == 200) {
        /* parse document and extract 'a href=' links */
        var doc=html.newDocument(res.text);
        var links=doc.findTag("a").filterAttr("href").getAttr("href");

        /* add any links not already retrieved */
        for (var i=0; i<links.length; i++) {
          var l = sanitize_link(links[i]);
          if(l){
            printf("adding new link %s\n", l);
            this.addurl(l);
          }
        }
        savedoc(res);
      } else {
        printf("error getting %s (%d)\n", res.url, res.status);
      }
    });
    /* expected output:
    adding new link http://localhost:8088/page-shopping-cart.html
    adding new link http://localhost:8088/page-login.html
    adding new link http://localhost:8088/index.html
    adding new link http://localhost:8088/features.html
    adding new link http://localhost:8088/page-homepage-sample.html
    adding new link http://localhost:8088/page-services-1-column.html
    adding new link http://localhost:8088/page-services-3-columns.html
    adding new link http://localhost:8088/page-services-4-columns.html
    adding new link http://localhost:8088/page-pricing.html
    adding new link http://localhost:8088/page-team.html
    adding new link http://localhost:8088/page-vacancies.html
    adding new link http://localhost:8088/page-job-details.html
    adding new link http://localhost:8088/page-portfolio-2-columns-1.html
    adding new link http://localhost:8088/page-portfolio-2-columns-2.html
    adding new link http://localhost:8088/page-portfolio-3-columns-1.html
    adding new link http://localhost:8088/page-portfolio-3-columns-2.html
    adding new link http://localhost:8088/page-portfolio-item.html
    adding new link http://localhost:8088/page-about-us.html
    adding new link http://localhost:8088/page-contact-us.html
    adding new link http://localhost:8088/page-faq.html
    adding new link http://localhost:8088/page-testimonials-clients.html
    adding new link http://localhost:8088/page-events.html
    adding new link http://localhost:8088/page-404.html
    adding new link http://localhost:8088/page-sitemap.html
    adding new link http://localhost:8088/page-register.html
    adding new link http://localhost:8088/page-password-reset.html
    adding new link http://localhost:8088/page-terms-privacy.html
    adding new link http://localhost:8088/page-coming-soon.html
    adding new link http://localhost:8088/page-products-3-columns.html
    adding new link http://localhost:8088/page-products-4-columns.html
    adding new link http://localhost:8088/page-products-slider.html
    adding new link http://localhost:8088/page-product-details.html
    adding new link http://localhost:8088/page-blog-posts.html
    adding new link http://localhost:8088/page-blog-post-right-sidebar.html
    adding new link http://localhost:8088/page-blog-post-left-sidebar.html
    adding new link http://localhost:8088/page-news.html
    adding new link http://localhost:8088/credits.html
    saving http://localhost:8088/
    saving http://localhost:8088/page-shopping-cart.html
    saving http://localhost:8088/page-login.html
    saving http://localhost:8088/page-services-1-column.html
    saving http://localhost:8088/page-pricing.html
    saving http://localhost:8088/page-vacancies.html
    saving http://localhost:8088/page-job-details.html
    saving http://localhost:8088/page-portfolio-2-columns-1.html
    saving http://localhost:8088/page-portfolio-2-columns-2.html
    saving http://localhost:8088/page-portfolio-3-columns-1.html
    saving http://localhost:8088/page-portfolio-3-columns-2.html
    saving http://localhost:8088/page-portfolio-item.html
    saving http://localhost:8088/page-about-us.html
    saving http://localhost:8088/page-contact-us.html
    saving http://localhost:8088/page-testimonials-clients.html
    saving http://localhost:8088/page-events.html
    saving http://localhost:8088/page-404.html
    saving http://localhost:8088/page-sitemap.html
    saving http://localhost:8088/page-register.html
    saving http://localhost:8088/page-password-reset.html
    saving http://localhost:8088/page-coming-soon.html
    saving http://localhost:8088/page-products-slider.html
    saving http://localhost:8088/page-product-details.html
    saving http://localhost:8088/page-blog-posts.html
    saving http://localhost:8088/page-news.html
    saving http://localhost:8088/credits.html
    saving http://localhost:8088/index.html
    saving http://localhost:8088/features.html
    saving http://localhost:8088/page-homepage-sample.html
    saving http://localhost:8088/page-services-3-columns.html
    saving http://localhost:8088/page-services-4-columns.html
    saving http://localhost:8088/page-team.html
    saving http://localhost:8088/page-faq.html
    saving http://localhost:8088/page-terms-privacy.html
    saving http://localhost:8088/page-products-3-columns.html
    saving http://localhost:8088/page-products-4-columns.html
    saving http://localhost:8088/page-blog-post-right-sidebar.html
    saving http://localhost:8088/page-blog-post-left-sidebar.html
    */

Posting Multipart-Form Data
~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example shows how to send files using ``postform``.  Each file is sent in 
the body as ``form-data`` with the top level ``Content-Type`` set to
``multipart/form-data``.  Here, the file ``myfile1.txt`` and ``myfile2.txt`` 
each contain a single line of text (``contents of file #``).

.. code-block:: javascript

    var curl = require("rampart-curl");

    var res = curl.fetch("http://localhost:8088/echo.txt",
    {
      postform: {
        file1: {"filename":"myfile1.txt" ,"type": "text/plain", "data":"@myfile1.txt"},
        file2: {"filename":"myfile2.txt" ,"type": "text/plain", "data":"@myfile2.txt"}
      }
    });

    console.log(res.text);

    /* expected output: 
    {
        "ip": "127.0.0.1",
        "port": 49544,
        "method": "POST",
        "path": {
            "file": "echo.txt",
            "path": "/echo.txt",
            "base": "/",
            "scheme": "http://",
            "host": "localhost:8088",
            "url": "http://localhost:8088/echo.txt"
        },
        "query": {},
        "body": "--------------------------98b4d26c39da32e6\r\nContent-Disposition: form-data; name=\"file1\"; filename=\"myfile1.txt\"\r\nContent-Type: text/plain\r\n\r\ncontents of file 1\r\n--------------------------98b4d26c39da32e6\r\nContent-Disposition: form-data; name=\"file2\"; filename=\"myfile2.txt\"\r\nContent-Type: text/plain\r\n\r\ncontents of file 2\r\n--------------------------98b4d26c39da32e6--\r\n",
        "query_raw": "",
        "headers": {
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Content-Length": "370",
            "Content-Type": "multipart/form-data; boundary=------------------------98b4d26c39da32e6"
        },
        "postData": {
            "Content-Type": "multipart/form-data",
            "content": [
                {
                    "Content-Disposition": "form-data",
                    "name": "file1",
                    "filename": "myfile1.txt",
                    "Content-Type": "text/plain",
                    "content": "contents of file 1"
                },
                {
                    "Content-Disposition": "form-data",
                    "name": "file2",
                    "filename": "myfile2.txt",
                    "Content-Type": "text/plain",
                    "content": "contents of file 2"
                }
            ]
        },
        "params": {
            "myfile1.txt": "contents of file 1",
            "myfile2.txt": "contents of file 2",
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Content-Length": "370",
            "Content-Type": "multipart/form-data; boundary=------------------------98b4d26c39da32e6"
        }
    }
    */

Multiple HTTP Post with Submit()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example shows how different post data and settings can be submitted to different URLs
in parallel using ``submit()``.  In this example, the file ``myfile1.txt`` and ``myfile2.txt`` each contain
a single line of text (``contents of file #``).

.. code-block:: javascript

    var curl = require("rampart-curl");

    var req1 = {
      url: "http://localhost:8088/echo.txt",
      postform: {
        myvar: "myval",
        myfile: {"filename":"myfile1.txt" ,"type": "text/plain", "data":"@myfile1.txt"}
      }
    }

    var req2 = {
      url: "http://localhost:8088/echo.txt?getvar=getval",
      header: "Content-Type: text/plain",
      post: "@myfile2.txt"
    }

    curl.submit([req1,req2] , function(res) {
      console.log(res.text);
    });

    /* expected output:
    {
        "ip": "127.0.0.1",
        "port": 49454,
        "method": "POST",
        "path": {
            "file": "echo.txt",
            "path": "/echo.txt",
            "base": "/",
            "scheme": "http://",
            "host": "localhost:8088",
            "url": "http://localhost:8088/echo.txt"
        },
        "query": {},
        "body": "--------------------------bd4bfd204892f559\r\nContent-Disposition: form-data; name=\"myvar\"\r\n\r\nmyval\r\n--------------------------bd4bfd204892f559\r\nContent-Disposition: form-data; name=\"myfile\"; filename=\"myfile1.txt\"\r\nContent-Type: text/plain\r\n\r\ncontents of file 1\r\n--------------------------bd4bfd204892f559--\r\n",
        "query_raw": "",
        "headers": {
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Content-Length": "308",
            "Content-Type": "multipart/form-data; boundary=------------------------bd4bfd204892f559"
        },
        "postData": {
            "Content-Type": "multipart/form-data",
            "content": [
                {
                    "Content-Disposition": "form-data",
                    "name": "myvar",
                    "content": "myval"
                },
                {
                    "Content-Disposition": "form-data",
                    "name": "myfile",
                    "filename": "myfile1.txt",
                    "Content-Type": "text/plain",
                    "content": "contents of file 1"
                }
            ]
        },
        "params": {
            "myvar": "myval",
            "myfile1.txt": "contents of file 1",
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Content-Length": "308",
            "Content-Type": "multipart/form-data; boundary=------------------------bd4bfd204892f559"
        }
    }

    {
        "ip": "127.0.0.1",
        "port": 49456,
        "method": "POST",
        "path": {
            "file": "echo.txt",
            "path": "/echo.txt",
            "base": "/",
            "scheme": "http://",
            "host": "localhost:8088",
            "url": "http://localhost:8088/echo.txt?getvar=getval"
        },
        "query": {
            "getvar": "getval"
        },
        "body": "contents of file 2",
        "query_raw": "getvar=getval",
        "headers": {
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Transfer-Encoding": "chunked",
            "Content-Type": "text/plain",
            "Content-Length": "18",
            "Expect": "100-continue"
        },
        "postData": {
            "Content-Type": "text/plain",
            "content": "contents of file 2"
        },
        "params": {
            "getvar": "getval",
            "Host": "localhost:8088",
            "User-Agent": "libcurl-rampart/0.1",
            "Transfer-Encoding": "chunked",
            "Content-Type": "text/plain",
            "Content-Length": "18",
            "Expect": "100-continue"
        }
    }
    */


Sending Email with SMTP
~~~~~~~~~~~~~~~~~~~~~~~

The following example sends a preformatted email through gmail.

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    var curl = require("rampart-curl");

    var user = "example_user@gmail.com";
    var pass ='xxxxxxxx';
    var to_email  = "example_recip@example.com"
     

    var curl = require("rampart-curl");

    var res=curl.fetch("smtps://smtp.gmail.com:465",
    {
        "mail-rcpt": to_email,
        "mail-from": user,
        user: user,
        pass:pass,
        "mail-msg":
            "To: " + to_email + "\r\n" +
            "From: " + user + "\r\n" +
            "Subject: My first Email using rampart-curl\r\n" +
            "\r\n" +
            "Hi " + to_email + ",\nWelcome to my first Email\n"
    });

    printf("%4J\n", res);


    /* expected output
    {
        "statusText": "Unknown Status Code",
        "status": 250,
        "text": "",
        "body": {},
        "effectiveUrl": "smtps://smtp.gmail.com:465/",
        "url": "smtps://smtp.gmail.com:465",
        "localIP": "2001:db8::1",
        "localPort": 41312,
        "serverIP": "2001:db8::2",
        "serverPort": 465,
        "rawHeader": "220 smtp.gmail.com ESMTP d10xxxxxx.218 - gsmtp\r\n250-smtp.gmail.com at your service, ..."
        "headers": {
            "STATUS": "220 smtp.gmail.com ESMTP d10xxxxxxx.218 - gsmtp",
            ...
            "HeaderLine_12": "354  Go ahead d10xxxxxxxx.218 - gsmtp",
            "HeaderLine_13": "250 2.0.0 OK  1610420924 d10xxxxxxxx.218 - gsmtp"
        },
        "httpVersion": -1,
        "totalTime": 2.67785,
        "errMsg": ""
    }
    */

Sending Email with Attachments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following example sends a JPEG as an attachment and the message
as both text and HTML (sent as ``multipart/alternative``).  The HTML 
refers to the attachment in an ``<img>`` tag and displays the image 
inline in the HTML mail.

.. code-block:: javascript

    rampart.globalize(rampart.utils);

    var curl = require("rampart-curl");

    var user = "example_user@gmail.com";
    var pass ='xxxxxxxx';
    var to_email  = "example_recip@example.com"
     

    var curl = require("rampart-curl");

    var img=readFile("/path/to/myimage.jpg");

    var res=curl.fetch("smtps://smtp.gmail.com:465",
    {
        "mail-rcpt": to_email,
        "mail-from": user,
        user: user,
        pass:pass,
        "mail-msg": {
            from: user,
            to: to_email,
            subject: "An email with html/text and attachment via rampart-curl",
            date: new Date(),
            message: {
                html: '<p>Dear ' + to_email + ',</p><p>Here is a message in html</p><img src="cid:myimage"><p>An Image</p>',
                text:'Dear ' + to_email + ',\nHere is a message in text.\n[image]\nAn Image\n',
                attach: [
                    { data:img, name:"img.jpg", type:"image/jpeg", cid:"myimage"}
                    /* or 
                    { data:"@/path/to/myimage.jpg", name:"img.jpg", type:"image/jpeg", cid:"myimage"}
                    */
                ]
            }
        }
    });

    /* expected output
    {
        "statusText": "Unknown Status Code",
        "status": 250,
        "text": "",
        "body": {},
        "effectiveUrl": "smtps://smtp.gmail.com:465/",
        "url": "smtps://smtp.gmail.com:465",
        "localIP": "2001:db8::1",
        "localPort": 41312,
        "serverIP": "2001:db8::2",
        "serverPort": 465,
        "rawHeader": "220 smtp.gmail.com ESMTP d10xxxxxx.218 - gsmtp\r\n250-smtp.gmail.com at your service, ..."
        "headers": {
            "STATUS": "220 smtp.gmail.com ESMTP d10xxxxxxx.218 - gsmtp",
            ...
            "HeaderLine_12": "354  Go ahead d10xxxxxxxx.218 - gsmtp",
            "HeaderLine_13": "250 2.0.0 OK  1610420924 d10xxxxxxxx.218 - gsmtp"
        },
        "httpVersion": -1,
        "totalTime": 2.67785,
        "errMsg": ""
    }
    */

