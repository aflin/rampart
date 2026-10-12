The rampart-net module
======================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-net module makes extensive use of the 
`libevent2 <https://libevent.org/>`_ library embedded in 
the main rampart executable as well as the 
`OpenSSL <https://www.openssl.org/>`_ library, imported from
:ref:`the rampart-crypto module <rampart-crypto:The rampart-crypto module>`.
 
License
~~~~~~~

The rampart-net module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-net module allows the creation of network connections and
servers in a manner similar to (but with significant differences from)
`the node.js net module <https://nodejs.org/api/net.html>`_\ .

All of the callback functions in rampart-net are asynchronously executed in
rampart's event loop unless stated otherwise.

All functions return their parent object, for chainability, unless otherwise
stated.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var net = require("rampart-net");


Socket Functions
----------------

new net.Socket()
~~~~~~~~~~~~~~~~  

    In order to make a tcp connection to a server, a new Socket object must
    be created.  The ``new net.Socket()`` statement returns functions to
    connect and communicate over tcp, with or without ssl/tls.
 
    Usage:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var socket = new net.Socket();

    Return Value:
        An object with the below listed function.

socket.connect()
~~~~~~~~~~~~~~~~

    Make a new tcp connection.

    Usage:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var socket = new net.Socket();

        socket.connect(options[, connect_callback]);

        /* or */

        socket.connect(port[, host][, options][, connect_callback]); 

    Where:
    
    * ``port`` - Required. The port of the server to which a connection
      will be attempted.

    * ``host`` - The hostname or ip address of the server.  Default is
      ``127.0.0.1``.

    * ``connect_callback`` is a callback :green:`Function`` which is run
      after a connection is established and the "connect" event is emitted.

    * ``options`` is an :green:`Object` with the following optional
      properties:

        * ``host`` - The hostname or ip address of the server.  Default is
          ``127.0.0.1``.

        * ``port`` - Required.  A :green:`Number`.  The port of the server
          to which a connection will be attempted.

        * ``timeout`` - How long in milliseconds before a connection is
          terminated for inactivity (both read and write).  Default is
          forever.

        * ``family`` - A :green:`Number`. Must be ``0`` (the default), ``4``
          or ``6`` to specify any ip family, ipv4 only or ipv6 only
          respectively.  May be used if ``host`` above resolves to both
          ipv4 and ipv6 addresses to force the use of a particular one.

    	* ``keepalive`` - A :green:`Boolean`. If ``true`` set tcp keepalive
          on the connection.  Packets will be transparently sent to the
          server to aid in keeping the connection alive (should it be
          necessary).  See 
          `this document <https://tldp.org/HOWTO/TCP-Keepalive-HOWTO/overview.html>`_
          for more information.  Default is ``false``.

        * ``keepAliveInitialDelay`` - A :green:`Number`. How many seconds to
          wait before sending the first keepalive packet.  Has no effect
          unless ``keepalive`` above is ``true``. Default is ``1``.

        * ``keepAliveInterval`` - A :green:`Number`. How many seconds
          between sending keepalive packets. Has no effect
          unless ``keepalive`` above is ``true``.  Default is ``1``.

        * ``keepAliveCount`` - A :green:`Number`. How many keepalive packets
          should be sent with no reply before disconnecting.  Has no effect
          unless ``keepalive`` above is ``true``. Default is ``10``.

        * ``tls`` - A :green:`Boolean`.  Whether to use SSL/TLS for the
          connection.  Default is ``false``.

        * ``ssl`` - Same as ``tls``.  If both set, ``ssl`` is ignored.

        * ``insecure`` - A :green:`Boolean`.  Whether to skip verification
          of the server's credentials when making a connection over
          SSL/TLS.  The default is ``false`` (i.e. check credentials and
          fail the connection if the server's credentials are not
          verified).

        * ``cacert`` - A :green:`String`. The path to the CA certificate
          store file, required to verify the server when using SSL/TLS.  The 
          default is system dependent (usually ``/etc/ssl/cert.pem`` on
          MacOS or ``/etc/ssl/certs/ca-certificates.crt`` on Linux).

        * ``capath`` - A :green:`String`.  The path to the CA directory.  CA
          certificates need to be stored as individual PEM files in this
          directory.  No Default.

        * ``hostname`` - A :green:`String`. Name to use to verify the server
          if different than provided in ``host`` above or if ``host`` is a 
          numeric ip address.  No Default.

socket.write()
~~~~~~~~~~~~~~

    Write data to the server. The parameter can be a :green:`String` or a
    :green:`Buffer`.

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var socket = new net.Socket();

        function mycallback(){
            // now connected, so we can write to server
            socket.write("hello world");
        }

        socket.on("connect", mycallback);

        socket.connect(port, host);

socket.startTls()
~~~~~~~~~~~~~~~~~

    Upgrade an already-connected plaintext socket to SSL/TLS, performing the
    handshake on the existing TCP connection. This is the primitive that
    makes "STARTTLS"-style protocols possible — you connect and speak the
    protocol in the clear until the server says it is ready for TLS (e.g.
    SMTP ``STARTTLS``, IMAP ``STARTTLS``, POP3 ``STLS``, LDAP StartTLS,
    PostgreSQL ``SSLRequest``), then call ``startTls()`` to encrypt the rest
    of the conversation. The socket object is unchanged after the upgrade:
    subsequent ``socket.write()`` calls are encrypted, and subsequent
    ``data`` events carry decrypted bytes.

    Usage:

    .. code-block:: javascript

        socket.startTls([options][, callback]);

    Where ``options`` is an :green:`Object` with any of these optional keys
    (all of them behave the same as in `socket.connect()`_ above):

        * ``hostname`` - A :green:`String`.  Name to use for SNI and
          certificate verification. Defaults to the ``host`` that was passed
          to ``socket.connect()``.  Override only when you connected by IP
          and the server certificate uses a different name.

        * ``insecure`` - A :green:`Boolean`.  Whether to skip verification of
          the server's certificate.  Default is ``false``.

        * ``cacert`` - A :green:`String`.  Path to the CA certificate file.
          Default is system dependent (same as in ``socket.connect()``).

        * ``capath`` - A :green:`String`.  Path to a directory of CA
          certificates.  No default.

    And ``callback`` is a :green:`Function` that, if provided, is invoked
    exactly once when the TLS handshake finishes (on success or failure).
    It receives ``(err, info)`` where:

        * ``err`` is ``null`` on success or a :green:`String` describing the
          failure (e.g.  ``"wrong version number"``,
          ``"certificate has expired"``).

        * ``info`` on success is an :green:`Object` with:

            * ``cipher`` - A :green:`String`, e.g.
              ``"TLS_AES_256_GCM_SHA384"``.

            * ``protocol`` - A :green:`String`, e.g. ``"TLSv1.3"``.

    The same information is also emitted as an ``upgrade`` event (see
    `socket.on()`_ below). The callback form and the event form can be used
    together or interchangeably.

    Calling ``startTls()`` on a socket that is not connected, is already
    using TLS, or has a TLS upgrade in progress throws an error.  On a
    handshake failure the ``upgrade`` listener (and callback) fires with the
    error string, and the ``error`` event fires as well, and the socket is
    closed.

    Example - SMTP STARTTLS:

    .. code-block:: javascript

        var net = require("rampart-net");
        rampart.globalize(rampart.utils);

        var sock  = new net.Socket();
        var state = 'greeting';
        var buf   = '';

        sock.on('data', function(data) {
            buf += bufferToString(data);
            var idx;
            while ((idx = buf.indexOf('\r\n')) >= 0) {
                var line = buf.substring(0, idx);
                buf = buf.substring(idx + 2);

                if (state === 'greeting' && line.substring(0,3) === '220') {
                    state = 'ehlo';
                    sock.write('EHLO client.example.com\r\n');
                }
                else if (state === 'ehlo' && line.substring(0,4) === '250 ') {
                    state = 'starttls';
                    sock.write('STARTTLS\r\n');
                }
                else if (state === 'starttls' && line.substring(0,3) === '220') {
                    state = 'upgrading';
                    sock.startTls(function(err, info) {
                        if (err) {
                            printf("TLS handshake failed: %s\n", err);
                            sock.destroy();
                            return;
                        }
                        printf("TLS %s / %s\n", info.protocol, info.cipher);
                        sock.write('EHLO client.example.com\r\n');
                        state = 'done';
                    });
                }
                else if (state === 'done' && line.substring(0,4) === '250 ') {
                    sock.write('QUIT\r\n');
                    sock.destroy();
                }
            }
        });

        sock.on('error', function(err) { printf("ERROR: %s\n", err); });
        sock.connect(587, 'smtp.example.com');

socket.on()
~~~~~~~~~~~

    Register a callback :green:`Function` to be run when an event on
    ``socket`` is emitted.

    Usage:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var socket = new net.Socket();

        socket.on(event, callback);

    Where:

    * ``event`` is one of the following possible events for a socket:

        * ``connect`` - emitted after a connection has been established.

        * ``data`` - emitted after data has been received on the socket.
          The provided function takes one argument, the data received
          in a :green:`Buffer`.

        * ``ready`` - For compatibility. Emitted immediately after
          "connect".

        * ``drain`` - emitted when data has been written.

        * ``end`` - emitted when disconnected by the server. Note: "close" 
          below will also be emitted.

        * ``timeout`` - emitted if the connection exceeds the provided
          timeout interval.  Note: "close" below will also be emitted.

        * ``close`` - emitted whenever a connection is terminated.

        * ``upgrade`` - emitted when a TLS handshake initiated by
          `socket.startTls()`_ finishes.  The callback receives
          ``(err, info)`` - ``err`` is ``null`` on success or a
          :green:`String` describing the failure; ``info`` on success is an
          :green:`Object` with ``cipher`` and ``protocol`` strings.  See
          `socket.startTls()`_ for details.

        * ``error``  - emitted upon error. Note: if no error callback is registered for a socket,
          rampart will throw an error instead.

    * ``callback`` is a function.  If ``error``, function will have its
      first parameter be the error object/message.  If ``data``, function
      will have its first parameter be the received data in a :green:`Buffer`.

socket.off()
~~~~~~~~~~~~

    Unregister a callback :green:`Function` previously registered with
    ``socket.on``.  Function must be a named function.

    Usage example:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var socket = new net.Socket();

        function mycallback(){
            ...
        }

        function finishcb(){
            this.off("connect", mycallback);
            this.off("close",   finishcb);
        }

        socket.on("connect", mycallback);

        ...

        socket.on("close", finishcb);

socket.once()
~~~~~~~~~~~~~

    Same as ``socket.on``, except the event will be removed after being
    called once.  This is equivalent to calling off at the beginning of a
    callback, except with once, the function may be anonymous (unnamed).

    Example:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var socket = new net.Socket();

        /*
            with on()

        function mycallback(){
            // 'socket' and 'this' are the same
            socket.off("connect", mycallback);
            ...
        }

        socket.on("connect", mycallback);

        */
        
        /* with once */
        
        socket.once("connect", function(){
            ...
        });
 
socket.destroy()
~~~~~~~~~~~~~~~~  

    Close the connection to server.  The "close" event is emitted upon
    the actual disconnect.

socket.setTimeout()
~~~~~~~~~~~~~~~~~~~ 

    Set a timeout and optional timeout event callback.  This is a shortcut
    for setting timeout value (in milliseconds) in ``socket.connect`` and a
    callback using ``socket.on("timeout", mytimeoutfunc)``.

    Usage example:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var socket = new net.Socket();
 
        function timedout(){
            console.log("connection timed out");
        }

        socket.setTimeout(5000, timedout); // five seconds

socket.setKeepAlive()
~~~~~~~~~~~~~~~~~~~~~ 

    Set keepalive on or off, or adjust settings at any point while the
    socket is connected.


    Usage:

    .. code-block:: javascript

        socket.setKeepAlive(enable[, initialDelay[, interval[, count]]]);

    Where ``enable`` is a :green:`Boolean` and the optional parameters are
    the same as in `socket.connect()`_\ .

socket.trigger()
~~~~~~~~~~~~~~~~~~~~~ 

    Trigger functions registered with `socket.on()`_ for a named event.

    Usage:

    .. code-block:: javascript

        socket.trigger(event[, argument]);

    Where ``event`` is a :green:`String`, the name of an event registered with
    `socket.on()`_\ , and ``argument`` is optionally an argument to pass to the
    registered callbacks for the event.

    Arbitarty events can be registered with `socket.on()`_\ , and then
    called with this function.

socket.bytesWritten
~~~~~~~~~~~~~~~~~~~

    A :green:`Number` - the number of bytes written to the server for the
    current connection.

socket.bytesRead
~~~~~~~~~~~~~~~~

    A :green:`Number` - the number of bytes read from the server for the
    current connection.

Other socket properties
~~~~~~~~~~~~~~~~~~~~~~~

    The ``socket`` :green:`Object` may include these possible status properties.
    Note that they are set as the socket changes state, so a property may be
    ``undefined`` rather than ``false`` before the relevant event has occurred
    (``connecting`` and ``destroyed``, for example, are ``undefined`` until a
    connection has been attempted):

    * ``connecting`` - :green:`Boolean`. Whether the connection has been
      initiated, but not yet established.

    * ``connected`` - :green:`Boolean`. Whether the connection has been
      established.

    * ``tls`` - :green:`Boolean`. Whether this is a secure connection.

    * ``destroyed`` - :green:`Boolean`. Whether this connection has been
      closed or destroyed.

    * ``pending`` - :green:`Boolean`. Whether a connection has not yet been
      attempted.  ``true`` before ``connect`` is called and after
      ``close`` and/or ``end`` event. ``false`` after connection
      is established.

    * ``readyState`` - :green:`String`.  "open" when connected, "opening"
      after ``socket.connect()`` is called and ``undefined`` after close or 
      before ``socket.connect()`` is called.

    * ``_events`` - :green:`Object`. Registered callbacks for events.

    * ``timeout`` - :green:`Number`. Timeout value, if set.

    * ``remoteAddress`` - :green:`String`.  IP address of the connected remote peer.

    * ``remotePort`` - :green:`Number`.  Port of the connected remote peer.

    * ``remoteFamily`` - :green:`String`.  IP version used for connection
      (``IPv4`` or ``IPv6``).

    * ``_hostPort`` - :green:`Number`. Same as ``remotePort``

    * ``_hostAddrs`` - :green:`Object`. Host address used for this connection
      returned from a call to `new net.Resolver()`_ by `socket.connect()`_ internally.

    * ``sslCipher`` - :green:`String`.  If ``tls`` is true, the name of the
      openssl cipher being used for this connection.

Socket Full Example
~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    /* simulate a https request to google.com */
    rampart.globalize(rampart.utils);

    var net = require('rampart-net');

    var socket = new net.Socket();

    socket.on("connect", function(){
        console.log("CONNECTED");
        this.write("GET / HTTP/1.0\r\nHost: google.com\r\n\r\n");
    });

    socket.on("ready", function(){
        console.log("READY");
    });

    // http 1.0, server should disconnect us.
    socket.on('end', function() {
        console.log("END EVENT");;
    });

    /* just to demonstrate multiple callbacks */

    socket.on('data', function(data) {
        printf("\nlength=%d\n",data.length);
    });

    socket.on('data', function(data) {
        printf("\ncontent:\n%s\n",data);
    });

    socket.on('close', function() {
        printf("Close - written: %s, read: %s\n", this.bytesWritten, this.bytesRead);
    });

    socket.on('error', function(err) {
        console.log("ERROR:", err);
    });

    socket.on('timeout', function(){
        console.log("TIMEOUT")
    });

    socket.setTimeout(1000);

    // now actually connect
    socket.connect({
        host: "google.com",
        port: 443,
        tls: true
    });

    /* end of script, event loop started, connection made, callbacks executed */

    /*
        Expected results:
            CONNECTED
            READY

            length=703

            content:
            HTTP/1.0 301 Moved Permanently
            Location: https://www.google.com/
            Content-Type: text/html; charset=UTF-8
            Date: Thu, 07 Jul 2022 06:19:02 GMT
            Expires: Sat, 06 Aug 2022 06:19:02 GMT
            Cache-Control: public, max-age=2592000
            Server: gws
            Content-Length: 220
            X-XSS-Protection: 0
            X-Frame-Options: SAMEORIGIN
            Alt-Svc: h3=":443"; ma=2592000,h3-29=":443"; ma=2592000,h3-Q050=":443"; ma=2592000,h3-Q046=":443"; ma=2592000,h3-Q043=":443"; ma=2592000,quic=":443"; ma=2592000; v="46,43"

            <HTML><HEAD><meta http-equiv="content-type" content="text/html;charset=utf-8">
            <TITLE>301 Moved</TITLE></HEAD><BODY>
            <H1>301 Moved</H1>
            The document has moved
            <A HREF="https://www.google.com/">here</A>.
            </BODY></HTML>

            END EVENT
            Close - written: 36, read: 703
    */

Server Functions
----------------


new net.Server()
~~~~~~~~~~~~~~~~

    In order to listen for tcp connections from clients, a new Server object
    must be created.  The ``new net.Server()`` statement returns functions
    to listen and create sockets to communicate over tcp, with or without ssl/tls.

    Usage:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var server = new net.Server([options ][,connection_callback]);

    Where:

    * ``options`` is an :green:`Object` of options:

        * ``tls`` - AKA ``secure`` - a :green:`Boolean` - Whether to serve
          using ssl/tls. Default is ``false``.  If ``true``, the
          ``sslKeyFile`` and ``sslCertFile`` parameters must also be set.

        * ``sslKeyFile``: A :green:`String`, the location of the ssl key file for
          serving  over ssl/tls.  An example, if using 
          `letsencrypt <https://letsencrypt.org/>`_ for "example.com" might be
          ``"/etc/letsencrypt/live/example.com/privkey.pem"``.  This setting has
          no effect unless ``tls`` or ``secure`` is ``true``.

        * ``sslCertFile``: A :green:`String`, the location of the ssl cert file for
          serving over ssl/tls.  An example, if using 
          `letsencrypt <https://letsencrypt.org/>`_ for "example.com" might be
          ``"/etc/letsencrypt/live/example.com/fullchain.pem"``.  This setting has
          no effect unless ``tls`` or ``secure`` is ``true``.

        * ``sslMinVersion``:  A :green:`String`, the minimum SSL/TLS version to use. 
          Possible values are ``ssl3``, ``tls1``, ``tls1.1`` or ``tls1.2``.  The
          default is ``tls1.2``. This setting has no effect unless ``tls``
          or ``secure`` is ``true``.

    * ``connection_callback`` - a :green:`Function` - a callback executed when
      the ``connection`` event is emitted (when the server accepts a new
      connection).  The "connection" event calls registered functions with 
      a single parameter (the ``socket`` object, representing the socket
      connection to the client).
 
    Return Value:
        An object with the below listed function.

server.listen()
~~~~~~~~~~~~~~~

    Set server to listen on the given port, and optionally ip addresses.

    Usage:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var server = new net.Server([ [options ][,connection_callback]]);

         server.listen(port[, host[, backlog]][, listen_callback]);
         
         /* or */
         
         server.listen(options[, listen_callback]);

    Where:

    * ``port`` - A :green:`Number`. The port upon which to listen. Required.

    * ``host`` - A :green:`String` or :green:`Array` of :green:`Strings`. 
      Hosts and/or IP addresses to bind. Default is ``"any"``
      If unset or set to ``"any"``, it will bind all available IPV4 and IPV6 addresses.

    * ``backlog`` - A :green:`Number`. The maximum length of the queue of pending connections.
      Default is ``511``.

    * ``listen_callback`` - A :green:`Function`. A function to be executed
      when the "listening" event is emitted (when server starts listening).

    * ``options`` - An :green:`Object` with the following properties:

        * ``port`` - Same as above.

        * ``host`` - Same as above.

        * ``backlog`` - Same as above.

        * ``maxConnections`` - Same as `server.maxConnections()`_ below.

        * ``family`` - A :green:`Number`. Must be ``0`` (the default), ``4``
          or ``6`` to specify any ip family, ipv4 only or ipv6 only
          respectively.  May be used if ``host`` above resolves to both
          ipv4 and ipv6 addresses to force the use of a particular one.

server.on()
~~~~~~~~~~~

    Register a callback :green:`Function` to be run when an event on
    ``server`` is emitted.

    Usage:

    .. code-block:: javascript
    
        var net = require("rampart-net");
        
        var server = new net.Server();

        server.on(event, callback);

    Where:

    * ``event`` is one of the following possible events for a socket:

        * ``connection`` - emitted after a new connection has been established.
          The callback is provided a ``socket`` object, connected to the
          client.

        * ``listening`` - emitted after server has binds to the given port
          and is ready to accept connection.

        * ``close`` - emitted when the server is terminated.

        * ``error``  - emitted upon error. Note: if no error callback is registered for
          the server, rampart will throw an error instead.

    * ``callback`` is a function.  If ``error``, function will have its
      first parameter be the error object/message.  If ``data``, function
      will have its first parameter be the received data in a :green:`Buffer`.

server.off()
~~~~~~~~~~~~

    Unregister a callback :green:`Function` previously registered with
    ``server.on``.  Function must be a named function.

server.once()
~~~~~~~~~~~~~

    Same as ``server.on``, except the event will be removed after being called once. 
    This is equivalent to calling off at the beginning of the callback,
    except with ``once``, the function may be anonymous (unnamed).  See example for
    `socket.once()`_\ .

server.connectionCount()
~~~~~~~~~~~~~~~~~~~~~~~~

    Get the number of connected clients.

    Note that the count is updated as connections are fully established and
    fully torn down.  Inside a ``connection`` callback the connection being
    reported is not yet counted (so a first connection reads ``0``), and a
    closed connection may still be counted until the following event-loop
    cycle.

    Return Value:
        A :green:`Number`, the number of connected clients.

server.maxConnections()
~~~~~~~~~~~~~~~~~~~~~~~

    Set the maximum number of connections concurrently connected.  The
    server will drop new connections if this number is reached.
    This function can be called at any time to set or adjust the
    connection limit.

    Note that ``maxConnections`` is a function, not a readable property:
    the limit can be set or changed, but not read back.  Use
    `server.connectionCount()`_ for the number of clients currently
    connected.
   
    Usage:
    
    .. code-block:: javascript

        var net = require("rampart-net");

        var server = new net.Server();
        
        server.maxConnections([max]);

    Where ``max`` is a :green:`Number`, the maximum allowed connections. 
    Default is ``0`` (meaning no max) if no value is provided.  Setting to a
    number greater than 4,294,967,295 or less than 0 is equivalent to
    setting ``0``.  Actual system maximum number of connections varies by
    platform and settings.
    
Other server properties
~~~~~~~~~~~~~~~~~~~~~~~

    The ``server`` :green:`Object` may include these possible status properties:

    * ``listening`` - :green:`Boolean`. Whether the server is bound and
      listening for connections.

    * ``_events`` - :green:`Object`. Registered callbacks for events.

    * ``tls`` - :green:`Boolean`. Whether server accepts secure connections.

    * ``sslKeyFile`` - :green:`String`.  The SSL/TLS key file, if provided  

    * ``sslCertFile`` - :green:`String`.  The SSL/TLS cert file, if provided.

    * ``_hostAddrs`` - :green:`Array` of :green:`Objects`.  Host addresses
      that the server is listening on as returned from a call to 
      `resolver.resolve()`_  by `server.listen()`_ internally.

    * ``_hostPort`` - :green:`Number`.  The port used by the server.

    * ``backlog`` - :green:`Number`. ``backlog`` value, if set, or the
      default of ``511``.

Server Full Example
~~~~~~~~~~~~~~~~~~~

    .. code-block:: javascript

        /* Simulate an https server, return request headers as text */
        rampart.globalize(rampart.utils);

        var net = require('rampart-net');

        var cert = "/etc/letsencrypt/live/example.com/fullchain.pem";
        var key =  "/etc/letsencrypt/live/example.com/privkey.pem";

        var nc=0;
        
        var server = new net.Server(
            {
                "secure":true,
                sslKeyFile:key,
                sslCertFile:cert
            },
            function(socket) {
                console.log("CONNECTED");

                /* assuming all request data will be provided in a single callback */
                socket.on('data', function(data){
                    var ind = bufferToString(data);
                    printf("connection %s, open connections %s\n", ++nc, server.connectionCount());
                    socket.write(
                        "HTTP/1.0 200 OK\r\n" +
                        "Content-type: text/plain\r\n" +
                        "Content-Length: " + ind.length + "\r\n\r\n"
                    );
                    socket.write(ind);
                    socket.destroy();
                })
                
                .on('end', function(){
                    console.log("peer ended connection ", this.remoteAddress);
                })
          
                .on('error', 
                    console.log
                )
                
                .on('close', function(){
                    console.log("closed connection to ", this.remoteAddress);
                });
            }
        );

        server.maxConnections(1200);

        server.on("error", function (err) {
            console.log("server err:",err);
        })

        .on("close", function () {
            console.log("server closed");
        })

        .on("listening", function(){
            printf("LISTENING. server properties:\n%3J\n", this);
        })

        .listen({
            port: 8888,
            maxConnections: 1200
        });
        
        /*
        Output upon Start:
            LISTENING. server properties:
            {
               "listening": true,
               "_events": {
                  "connection": {},
                  "error": {},
                  "close": {},
                  "listening": {}
               },
               "sslKeyFile": "/etc/letsencrypt/live/example.com/privkey.pem",
               "sslCertFile": "/etc/letsencrypt/live/example.com/fullchain.pem",
               "tls": true,
               "maxConnections": 1200,
               "_hostAddrs": [
                  {
                     "host": "0.0.0.0",
                     "ip4addrs": [
                        "0.0.0.0"
                     ],
                     "ip6addrs": [],
                     "ipaddrs": [
                        "0.0.0.0"
                     ],
                     "canonName": "0.0.0.0",
                     "ip": "0.0.0.0",
                     "ipv4": "0.0.0.0"
                  },
                  {
                     "host": "::",
                     "ip4addrs": [],
                     "ip6addrs": [
                        "::"
                     ],
                     "ipaddrs": [
                        "::"
                     ],
                     "canonName": "::",
                     "ip": "::",
                     "ipv6": "::"
                  }
               ],
               "_hostPort": 8888,
               "backlog": 511
            }

        Request:
            curl https://example.com:8888/

            GET / HTTP/1.1
            Host: example.com:8888
            User-Agent: curl/7.58.0
            Accept: * /*

        Output after request:
            CONNECTED
            connection 1, open connections 1
            closed connection to  2001:db8::1

        */


WebSocket Client Functions
--------------------------

net.wsConnect()
~~~~~~~~~~~~~~~

    Establish a WebSocket connection to a server.  The ``net.wsConnect()``
    function creates a new socket, performs the WebSocket opening handshake
    (as defined in `RFC 6455 <https://datatracker.ietf.org/doc/html/rfc6455>`_\ ),
    and returns a wsclient object with additional WebSocket methods.

    Unlike lower-level socket connections, WebSocket framing, masking, and
    control frame handling (ping/pong, close handshake) are managed
    automatically.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var wsclient = net.wsConnect(options);

    Where ``options`` is an :green:`Object` with the following properties:

    * ``url`` - Required.  A :green:`String`.  The WebSocket URL to connect
      to.  Must begin with ``ws://`` (unencrypted) or ``wss://`` (SSL/TLS).
      The URL may include a port (e.g. ``ws://host:8080/path``) and a
      path.  If the port is omitted, ``80`` is used for ``ws://`` and
      ``443`` for ``wss://``.

    * ``callbacks`` - An :green:`Object` of event name/callback function
      pairs.  See `WebSocket Client Events`_ below for the list of
      available events.

    * ``headers`` - An :green:`Object` of additional HTTP headers to include
      in the WebSocket upgrade request.  Keys are header names and values
      are header values, both as :green:`Strings`.

    * ``timeout`` - A :green:`Number`.  How long in milliseconds before
      the connection is terminated for inactivity (no data read or
      written).  Default is ``0`` (no timeout).  Note that the
      ``pingInterval`` option provides a separate keepalive mechanism
      at the WebSocket protocol level.

    * ``pingInterval`` - A :green:`Number`.  Interval in seconds between
      WebSocket ping frames sent to the server.  If the server does not
      respond to three consecutive pings, the connection is closed and
      an ``error`` event is emitted.  Set to ``0`` to disable ping
      keepalive.  Default is ``30``.

    * ``insecure`` - A :green:`Boolean`.  Whether to skip verification of
      the server's SSL/TLS certificate when connecting via ``wss://``.
      Default is ``false``.

    Return Value:
        A wsclient :green:`Object` (a socket as returned from
        `new net.Socket()`_\ ) with the additional methods described below.

wsclient.wsSend()
~~~~~~~~~~~~~~~~~

    Send a WebSocket message to the connected server.  This method is
    available only on sockets returned by `net.wsConnect()`_\ .

    Usage:

    .. code-block:: javascript

        wsclient.wsSend(data[, isBinary]);

    Where:

    * ``data`` - A :green:`String` or :green:`Buffer`.  The message payload
      to send.

    * ``isBinary`` - A :green:`Boolean`.  If ``true``, the message is sent
      as a binary frame (opcode ``0x2``).  If ``false`` or omitted, it is
      sent as a text frame (opcode ``0x1``).

    Return Value:
        The wsclient :green:`Object`, for chaining.

    NOTE:
        All outgoing frames are masked as required by
        `RFC 6455 Section 5.3 <https://datatracker.ietf.org/doc/html/rfc6455#section-5.3>`_\ .

wsclient.wsClose()
~~~~~~~~~~~~~~~~~~

    Initiate the WebSocket close handshake by sending a close frame with
    status code ``1000`` (normal closure) to the server.  The underlying
    connection is destroyed after the close frame is sent.  The ``close``
    event is emitted upon actual disconnection.

    Usage:

    .. code-block:: javascript

        wsclient.wsClose();

WebSocket Client Events
~~~~~~~~~~~~~~~~~~~~~~~~

    The following events may be registered via the ``callbacks`` property
    of `net.wsConnect()`_ or by calling ``wsclient.on()`` on the returned
    wsclient object.

    * ``wsConnect`` - Emitted after the WebSocket handshake has completed
      successfully and the connection is ready for sending and receiving
      messages.  Note that the lower-level ``connect`` event is used
      internally and should not be registered by user code.

    * ``message`` - Emitted when a complete WebSocket message is received.
      The callback is provided a single argument, an :green:`Object` with
      the following properties:

        * ``message`` - A :green:`Buffer` containing the message payload.

        * ``binary`` - A :green:`Boolean`.  ``true`` if the message was
          sent as a binary frame, ``false`` if sent as a text frame.

    * ``close`` - Emitted when the connection is closed.

    * ``error`` - Emitted upon error.  The callback receives the error
      message as its first argument.  If no error callback is registered,
      rampart will throw an error instead.

    * ``timeout`` - Emitted if the connection exceeds the timeout interval.

    In addition, standard socket events (``drain``, ``end``) are available
    as documented in `socket.on()`_\ .

    NOTE:
        Within event callbacks, ``this`` refers to the wsclient object.
        This can be used to send messages, close the connection, or access
        wsclient properties from within a callback.

WebSocket Client Example
~~~~~~~~~~~~~~~~~~~~~~~~~

    .. code-block:: javascript

        /* Connect to a WebSocket server, send a message,
           and print the response.                         */
        rampart.globalize(rampart.utils);

        var net = require("rampart-net");

        var wsclient = net.wsConnect({
            url: "ws://127.0.0.1:8080/wsecho",
            pingInterval: 30,
            callbacks: {
                "wsConnect": function() {
                    printf("WebSocket connected\n");
                    this.wsSend("Hello, World!");
                },
                "message": function(ev) {
                    if (ev.binary) {
                        printf("Received %d bytes of binary data\n",
                            ev.message.length);
                    } else {
                        printf("Received: %s\n", ev.message);
                    }
                    this.wsClose();
                },
                "error": function(err) {
                    printf("Error: %s\n", err);
                },
                "close": function() {
                    printf("Connection closed\n");
                }
            }
        });

        /* Expected output:
            WebSocket connected
            Received: Hello, World!
            Connection closed
        */

    Sending binary data:

    .. code-block:: javascript

        /* Send binary data to the server */
        var data = new Buffer(256);
        for (var i = 0; i < 256; i++)
            data[i] = i;

        wsclient.wsSend(data, true);


Resolve functions
-----------------

    The following functions are used to resolve a host name to one or more
    ip addresses.

new net.Resolver()
~~~~~~~~~~~~~~~~~~

    Create a new resolve object.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var resolver = new net.Resolver();

resolver.resolve()
~~~~~~~~~~~~~~~~~~

    Resolve a host name to ip address.        

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var resolver = new net.Resolver();

        resolver.resolve(host[, nameserver][, lookup_callback]);

    Where: 

    * ``host`` is a :green:`String` - the host name to be resolved.

    * ``nameserver`` is a :green:`String` - the ip address of the DNS server
      to query (default is system specified nameservers in, e.g.
      `/etc/resolv.conf`).

    * ``lookup_callback`` is a :green:`Function` - an optional "lookup"
      event callback.

    NOTE:
        ``resolver.resolve()`` may be called multiple times at any time, however
        each time an **anonymous** function is provided as the
        ``lookup_callback``, that additional callback will be run for each
        "lookup" event. Note that duplicate **named** functions are only run
        once per event.

        .. code-block:: javascript

            var net = require("rampart-net");

            var resolver = new net.Resolver();

            /* console.log is run once per lookup */
            
            resolver.resolve("google.com", console.log);

            resolver.resolve("rampart.dev", console.log);

        In contrast:

        .. code-block:: javascript

            var net = require("rampart-net");

            var resolver = new net.Resolver();

            /* console.log is run TWICE per lookup since two different
             * functions call it.                                       */
            
            resolver.resolve("google.com", function(hobj){console.log(hobj);});

            resolver.resolve("rampart.dev", function(hobj){console.log(hobj);});

resolver.reverse()
~~~~~~~~~~~~~~~~~~

    Resolve an ip address to host name.        

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var resolver = new net.Resolver();

        resolver.reverse(ip_addr[, nameserver][, lookup_callback]);

    Where: 

    * ``ip_addr`` is a :green:`String` - the ip address to look up.

    * ``nameserver`` is a :green:`String` - the ip address of the DNS server
      to query (default is system specified nameservers in, e.g.
      `/etc/resolv.conf`).

    * ``lookup_callback`` is a :green:`Function` - an optional "lookup"
      event callback.

    NOTE:
        See above.  Note applies to ``resolver.reverse()`` as well.


resolver.on()
~~~~~~~~~~~~~

    Register a callback function for a resolver event.  Currently, the only
    events are ``lookup`` and ``error``.

    Usage example:

    .. code-block:: javascript

        var net = require("rampart-net");

        var resolver = new net.Resolver();

        resolver.on("lookup", function(hobj){
            rampart.utils.printf("%3J\n", hobj);
        });

        resolver.on("error", function(emsg){
            rampart.utils.printf("Resolver error: %s\n", emsg);
        });

        resolver.resolve("rampart.dev");

        resolver.resolve("google.com");

        /* probable output:

            {
               "host": "google.com",
               "ip4addrs": [
                  "142.251.214.142"
               ],
               "ip6addrs": [
                  "2607:f8b0:4005:80f::200e"
               ],
               "ipaddrs": [
                  "142.251.214.142",
                  "2607:f8b0:4005:80f::200e"
               ],
               "ip": "142.251.214.142",
               "ipv4": "142.251.214.142",
               "ipv6": "2607:f8b0:4005:80f::200e"
            }
            {
               "host": "rampart.dev",
               "ip4addrs": [
                  "184.105.177.37"
               ],
               "ip6addrs": [
                  "2001:470:1:393::37"
               ],
               "ipaddrs": [
                  "184.105.177.37",
                  "2001:470:1:393::37"
               ],
               "ip": "184.105.177.37",
               "ipv4": "184.105.177.37",
               "ipv6": "2001:470:1:393::37"
            }
        */

net.resolve()
~~~~~~~~~~~~~

    Resolve a host name.  **This function is not asynchronous**.  The lookup
    will occur immediately, potentially before the event loop starts,
    and block further execution while waiting for an answer.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var hostobj = net.resolve(hostname[, type]);

    Where:

    * ``hostname`` is a :green:`String` - the host name to resolve.

    * ``type`` is an optional :green:`String` - the DNS record type to query.
      If omitted, the function performs a standard address lookup (using
      ``getaddrinfo``) and returns an object with ``ip4addrs``, ``ip6addrs``,
      etc.  If provided, a raw DNS query is performed for the specified record
      type and the result is returned in a different format (see
      `Typed DNS Queries`_ below).

    **Address lookup (no type):**

    .. code-block:: javascript

        var net = require("rampart-net");

        var hostobj = net.resolve("yahoo.com");

        /* hostobj =
            {
               "host": "yahoo.com",
               "ip4addrs": [
                  "74.6.231.21",
                  "98.137.11.164",
                  "98.137.11.163",
                  "74.6.143.26",
                  "74.6.231.20",
                  "74.6.143.25"
               ],
               "ip6addrs": [
                  "2001:4998:24:120d::1:0",
                  "2001:4998:44:3507::8000",
                  "2001:4998:44:3507::8001",
                  "2001:4998:124:1507::f000",
                  "2001:4998:24:120d::1:1",
                  "2001:4998:124:1507::f001"
               ],
               "ipaddrs": [
                  "2001:4998:24:120d::1:0",
                  "2001:4998:44:3507::8000",
                  "2001:4998:44:3507::8001",
                  "2001:4998:124:1507::f000",
                  "2001:4998:24:120d::1:1",
                  "2001:4998:124:1507::f001",
                  "74.6.231.21",
                  "98.137.11.164",
                  "98.137.11.163",
                  "74.6.143.26",
                  "74.6.231.20",
                  "74.6.143.25"
               ],
               "ip": "2001:4998:24:120d::1:0",
               "ipv6": "2001:4998:24:120d::1:0",
               "ipv4": "74.6.231.21"
            }

        */

.. _typed-dns-queries:

Typed DNS Queries
"""""""""""""""""

    When ``type`` is provided, ``net.resolve()`` performs a raw DNS query
    using ``res_query`` (or ``DnsQuery_A`` on Windows/Cygwin) and returns an
    :green:`Object` with the following properties:

    * ``host`` - a :green:`String`, the hostname that was queried.

    * ``type`` - a :green:`String`, the record type that was requested.

    * ``records`` - an :green:`Array` of results.  The structure of each
      element depends on the record type (see below).

    If the query fails, the returned :green:`Object` will instead contain an
    ``errMsg`` property with a description of the error.

    Supported record types (case-insensitive):

    * ``"A"`` — IPv4 address records.  Each element of ``records`` is a
      :green:`String` (e.g. ``"93.184.216.34"``).

    * ``"AAAA"`` — IPv6 address records.  Each element of ``records`` is a
      :green:`String` (e.g. ``"2606:2800:220:1:248:1893:25c8:1946"``).

    * ``"MX"`` — Mail exchanger records.  Each element of ``records`` is an
      :green:`Object` with:

      * ``priority`` - a :green:`Number`, the MX preference value (lower is
        higher priority).
      * ``exchange`` - a :green:`String`, the mail server hostname.

    * ``"NS"`` — Name server records.  Each element of ``records`` is a
      :green:`String` (the nameserver hostname).

    * ``"CNAME"`` — Canonical name records.  Each element of ``records`` is a
      :green:`String` (the canonical hostname).

    * ``"PTR"`` — Pointer records.  Each element of ``records`` is a
      :green:`String` (the pointed-to hostname).

    * ``"TXT"`` — Text records.  Each element of ``records`` is a
      :green:`String` (the concatenated text data of the record).

    * ``"SOA"`` — Start of authority records.  Each element of ``records`` is
      an :green:`Object` with:

      * ``mname`` - a :green:`String`, the primary nameserver.
      * ``rname`` - a :green:`String`, the responsible party's email (in DNS
        notation, e.g. ``"dns-admin.google.com"``).
      * ``serial`` - a :green:`Number`, the zone serial number.
      * ``refresh`` - a :green:`Number`, the refresh interval in seconds.
      * ``retry`` - a :green:`Number`, the retry interval in seconds.
      * ``expire`` - a :green:`Number`, the expiry time in seconds.
      * ``minimum`` - a :green:`Number`, the minimum TTL in seconds.

    * ``"SRV"`` — Service locator records.  Each element of ``records`` is an
      :green:`Object` with:

      * ``priority`` - a :green:`Number`.
      * ``weight`` - a :green:`Number`.
      * ``port`` - a :green:`Number`.
      * ``target`` - a :green:`String`, the target hostname.

    **MX lookup example:**

    .. code-block:: javascript

        var net = require("rampart-net");

        var mx = net.resolve("gmail.com", "MX");

        /* mx =
            {
               "host": "gmail.com",
               "type": "MX",
               "records": [
                  {
                     "priority": 5,
                     "exchange": "gmail-smtp-in.l.google.com"
                  },
                  {
                     "priority": 10,
                     "exchange": "alt1.gmail-smtp-in.l.google.com"
                  },
                  {
                     "priority": 20,
                     "exchange": "alt2.gmail-smtp-in.l.google.com"
                  },
                  {
                     "priority": 30,
                     "exchange": "alt3.gmail-smtp-in.l.google.com"
                  },
                  {
                     "priority": 40,
                     "exchange": "alt4.gmail-smtp-in.l.google.com"
                  }
               ]
            }
        */

    **TXT lookup example:**

    .. code-block:: javascript

        var net = require("rampart-net");

        var txt = net.resolve("example.com", "TXT");

        /* txt =
            {
               "host": "example.com",
               "type": "TXT",
               "records": [
                  "v=spf1 -all",
                  "wgyf8z8cgv2qct8..."
               ]
            }
        */

    **SRV lookup example:**

    .. code-block:: javascript

        var net = require("rampart-net");

        var srv = net.resolve("_sip._tcp.example.com", "SRV");

        /* srv =
            {
               "host": "_sip._tcp.example.com",
               "type": "SRV",
               "records": [
                  {
                     "priority": 10,
                     "weight": 60,
                     "port": 5060,
                     "target": "sip1.example.com"
                  }
               ]
            }
        */

    **Error handling example:**

    .. code-block:: javascript

        var net = require("rampart-net");

        var res = net.resolve("nonexistent.invalid", "MX");

        /* res =
            {
               "errMsg": "Unknown host"
            }
        */

net.reverse()
~~~~~~~~~~~~~

    Resolve an ip address to a hostname.  **This function is not asynchronous**.  The lookup
    will occur immediately, potentially before the event loop starts,
    and wait for an answer.

    Usage example:

    .. code-block:: javascript

        var net = require("rampart-net");

        var hostname = net.reverse("1.1.1.1");

        // hostname == "one.one.one.one"

Shortcut Functions
------------------

net.createConnection()
~~~~~~~~~~~~~~~~~~~~~~

    Short cut for ``new net.Socket()`` and ``socket.connect()``.  AKA -
    ``net.connect()``.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var socket = net.createConnection(options[, connect_callback]);

        /* or */
        
        var socket = net.connect(options[, connect_callback]);

    Where:

    * ``options`` is an :green:`Object` of options, the same as found in
      `new net.Socket()`_ and `socket.connect()`_ above.

    * ``connect_callback`` is a :green:`Function`, the connect callback
      function.

    This is equivalent to the following:

    .. code-block:: javascript

        var net = require("rampart-net");

        function netconnect(opt, cb) {
            var socket = new net.Socket(opt);
            socket.connect(opt, cb);
            return socket;
        }

        var socket = netconnect(options, connect_callback);

    Alternate usage with ``port``:

    .. code-block:: javascript

        var net = require("rampart-net");

        var socket = net.createConnection(port[, host][, connect_callback]);

        /* or */
        
        var socket = net.connect(port[, host][, connect_callback]);

  
net.createServer()
~~~~~~~~~~~~~~~~~~

    Short cut for ``new net.Server()`` and ``server.listen()``.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var server = net.createServer(options, connection_callback);

        /* or */

        var server = net.createServer(port[, host[, backlog]][, connection_callback]);


    Where

    * ``options`` is an :green:`Object` of options, the same as found in
      `new net.Server()`_ and `server.listen()`_ above.

    * ``connection_callback`` is a :green:`Function`, the connection callback
      function.

    This is roughly equivalent to the following (when using ``options``
    above):

    .. code-block:: javascript

        var net = require("rampart-net");

        function makeserver(opt, cb) {
            var server = new net.Server(opt, cb);
            server.listen(opt);
            return server;
        }

        var server = makeserver(options, connect_callback);

net.setCaCert()
~~~~~~~~~~~~~~~

    Set the Certificate Authority bundle used to verify peer certificates
    for secure (``tls``) connections.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        net.setCaCert(path);

    Where ``path`` is a :green:`String`, the location of a CA bundle file.
    An error is thrown if the file does not exist or cannot be read.

    The bundle in effect is also readable as the :green:`String` property
    ``net.default_ca_file``, which is set at load time to the system bundle
    if one was found.

    Return Value:
        ``undefined``.

net.resolve_async()
~~~~~~~~~~~~~~~~~~~

    Short cut for ``new net.Resolver()`` and ``resolver.resolve()``.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var resolver = net.resolve_async(host[, nameserver], lookup_callback);

    Where:

    * ``host`` is a :green:`String` - the host name to be resolved.

    * ``nameserver`` is a :green:`String` - the ip address of the DNS server
      to query (default is system specified nameservers in, e.g.
      `/etc/resolv.conf`).

    * ``lookup_calback`` is a :green:`Function` - the "lookup" event
      function.


    This is equivalent to the following:

    .. code-block:: javascript

        var net = require("rampart-net");

        function resolve_async(hn, cb) {
            var resolver = new net.Resolver();
            resolver.resolve(hn, cb);
            return resolver;
        }

        var resolver = resolve_async(host, callback);

net.reverse_async()
~~~~~~~~~~~~~~~~~~~

    Short cut for ``new net.Resolver()`` and ``resolver.reverse()``.

    Usage:

    .. code-block:: javascript

        var net = require("rampart-net");

        var resolver = net.reverse_async(ip_addr[, nameserver], lookup_callback);

    Where:

    * ``ip_addr`` is a :green:`String` - the ip address to look up.

    * ``nameserver`` is a :green:`String` - the ip address of the DNS server
      to query (default is system specified nameservers in, e.g.
      `/etc/resolv.conf`).

    * ``lookup_calback`` is a :green:`Function` - the "lookup" event
      function.


    This is equivalent to the following:

    .. code-block:: javascript

        var net = require("rampart-net");

        function reverse_async(ip, cb) {
            var resolver = new net.Resolver();
            resolver.reverse(ip, cb);
            return resolver;
        }

        var resolver = reverse_async(ip, callback);


