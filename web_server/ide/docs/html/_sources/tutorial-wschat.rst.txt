Using Websockets to Build a Chat
================================

Preface
-------

In this tutorial, we will use  :ref:`event <rampart-main:rampart.event>`
functions
and the :ref:`Server module <rampart-server:The rampart-server HTTP module>`, along 
with some client side scripting to create a multi-user chat application.

You can download the complete project from our 
`Tutorial Repository <https://github.com/aflin/rampart_tutorials>`_
in the `chat directory <https://github.com/aflin/rampart_tutorials/tree/main/chat>`_\ .

License
~~~~~~~

The code related to this tutorial is released under the MIT license.

Concepts
--------

This module will demonstrate:

*  Using :ref:`event <rampart-main:rampart.event>` functions to relay messages between clients.
*  Using the :ref:`Server module <rampart-server:The rampart-server HTTP module>` to connect
   with clients using websockets.
*  Handling both text and files using websockets.
*  Using :ref:`rampart-redis module <rampart-redis:The rampart-redis module>`
   to replace rampart.event so we can have a saved history.

In order to complete this tutorial, you should have basic knowledge of
JavaScript and client side scripting using JavaScript (a passing
knowledge of jQuery is also helpful).  This tutorial will not provide an
in-depth explanation of how client-side scripting works.

Getting Started
---------------

Web Server Layout
~~~~~~~~~~~~~~~~~

In the Rampart binary distribution is a sample web server tree.  For our
purposes here, we assume you have downloaded and unzipped the Rampart binary
distribution into a directory named ``~/downloads/rampart``. We will use
that for this project.  
The :ref:`The rampart-server HTTP module <rampart-server:The rampart-server HTTP module>`
is configured and loaded from the included ``web_server/web_server_conf.js``
script.  It defines ``web_server/html`` as the default directory for static
html, ``web_server/wsapps`` as the default directory for websocket scripts.

To get started, copy the ``web_server`` directory to a convenient place for
this project.  Also, for our purposes, we do not need
anything in the ``web_server/apps/test_modules`` or
``web_server/apps/wsapps`` directories, so you can delete the copy of those
files. We will also add an empty file at 
``web_server/wsapps/wschat.js`` and ``web_server/html/websockets_chat/index.html``.

::

    user@localhost:~$ mkdir wschat_demo
    user@localhost:~$ cd wschat_demo
    user@localhost:~/wschat_demo$ cp -a ~/downloads/rampart/web_server ./
    user@localhost:~/wschat_demo$ cd web_server/
    user@localhost:~/wschat_demo/web_server$ rm -rf apps/test_modules wsapps/* html/index.html
    user@localhost:~/wschat_demo/web_server$ touch ./wsapps/wschat.js
    user@localhost:~/wschat_demo/web_server$ mkdir html/websockets_chat
    user@localhost:~/wschat_demo/web_server$ touch ./html/websockets_chat/index.html
    user@localhost:~/wschat_demo/web_server$ find .
    ./wsapps
    ./wsapps/wschat.js
    ./start_server.sh
    ./stop_server.sh
    ./web_server_conf.js
    ./logs
    ./apps
    ./html
    ./html/images
    ./html/images/inigo-not-fount.jpg
    ./html/websockets_chat
    ./html/websockets_chat/index.html
    ./data

The Client HTML and Script
~~~~~~~~~~~~~~~~~~~~~~~~~~

As stated before, we assume that you have a basic understanding of the
client-side scripting and therefore will dispense with a lengthy discussion
about it.  The main points are:

* There is no security or user management.  A new chat client merely types in a name
  and is connected via websockets to the server.
* The ``start()`` function makes the initial connection to the server. 
* The ``procmess()`` receives messages from the server, decodes the JSON
  and displays the message for the user.
* The ``send()`` function is called when the user presses <enter> in the text box
  at the bottom of the web page.
* The ``showMessage()`` function is used to display incoming or echo outgoing messages
  in the chat div.

So we will begin with opening ``html/websockets_chat/index.html`` in an editor and pasting
the following:

.. code-block:: html
 
    <!doctype html>
    <html>
    <head>
    <meta charset="UTF-8">
    <title>chat</title>
    <style>
        html,body {
            height:100%;
            font-family:Arial, Helvetica, sans-serif;
            margin:0;
        }
        #container{
            border:5px solid grey;
            position: absolute;
            margin-top : 10px;
            padding:10px;
            bottom:30px;
            top: 30px;
            right:20px;
            left:20px;
        }
        #chatdiv{
            padding:5px;
            border:2px solid gray;
            height: calc(100% - 175px);
            overflow-y: scroll;
            margin:0;
            margin-top: 5px;    
        }
        .event {
            color:#999;
        }
        .n {
            color:#393;
        }
        .i {
            vertical-align: top;
        }
        .s {
            color:#933;
        }
        #wrapper{
            height: 100%;
        }
        #name{
            width:220px;
        }
        #chatdiv.dropping {
            border: 2px blue dashed;
        }

        #chatin{
            width: calc(100% - 120px);
            height: 1.5em;
            margin-top: 7px;
        }
    </style>
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
    <script>
    $(document).ready(function() {
        var socket;
        var name;
        var reconnected=false;
        var cd = $('#chatdiv');
        var prot;
        var htmlEscape=true;

        // check our protocol, set matching websocket version
        if (/^https:/.test(window.location.href))
            prot='wss://'
        else
            prot='ws://'

        // check if connection is open
        function isOpen(ws) { return ws.readyState === ws.OPEN }

        function getcookie(cname){ 
            //https://www.30secondsofcode.org/js/s/parse-cookie
            var cookies = document.cookie
                .split(';').map(v => v.split('='))
                .reduce
                ( (acc, v) => {
                    acc[decodeURIComponent(v[0].trim())] = decodeURIComponent(v[1].trim());
                    return acc;
                    }, {}
                );
            return cookies[cname];
        }

        function showMessage(data){
            if(htmlEscape)
                data.msg = $('<div/>').text(data.msg).html();
            if(data.from=="System")
                cd.append('<span class="s">' + data.from + ":</span> " + data.msg +'<br>');
            else
                cd.append('<span class="n">' + data.from + ":</span> " + data.msg +'<br>');
            cd.scrollTop(cd.height());
        }

        function procmess (msg){
            var data;

            try{
                data = JSON.parse(msg.data);
            } catch (e) {
                cd.append('<span style="color:red;">error parsing message</span><br>');
            }
            // if reconnected, skip welcome message.
            if(reconnected){
                reconnected=false;
                return;
            }

            if(data){
                showMessage(data);
            }
        }

        function send(){
            var text=$('#chatin').val();

            if(text==""){
                return ;
            }

            var data= {
                msg: text,
                from: name
            };

            try{
                // attempt reconnect if discoonnected
                if(!isOpen(socket) && !reconnected) {
                    socket = new WebSocket(prot + window.location.host + "/wsapps/wschat-s.json");
                    socket.addEventListener('open', function(e){
                        socket.send(text);
                        reconnected=true;
                        $('#chatin').val("");
                        showMessage(data);
                        socket.onmessage = procmess;
                    });
                    return;
                }
                //send it
                socket.send(text);
                //echo it
                showMessage(data);
            } catch(e){
                showMessage({from:"System",msg:'error sending message'});
            }
            $('#chatin').val("");
        }

        function start() {
            if(socket)
                socket.close();
            socket = new WebSocket(prot + window.location.host + "/wsapps/wschat-s.json");
            socket.onmessage = procmess;
        }

        function setname() {
            name = $('#name').val();
            if(name=="")
            return;
            document.cookie = "username="+name + "; path=/; sameSite=Strict";
            start();
        }

        // send message to server when <enter> is pressed
        $('#chatin').keypress(function(event) {
            if (event.keyCode == '13') {
                send();
            }
        });

        // sign on
        $('#name').keypress(function(event) {
            if (event.keyCode == '13') {
                setname();
                $('#namemsg').text("You are logged in as ");
                $('#chatin').focus();
            }
        });

        // check if we signed on previously 
        name = getcookie("username");

        if(name) {
            start();
            $('#name').val(name);
            $('#namemsg').text("You are logged in as ");
            $('#chatin').focus();
        }

    });
    </script>
    </head>
    <body>
        <div id="wrapper">
            <div id="container">
                <h2>wschat tutorial</h2>
                <span id="namespan">
                    <span id="namemsg">Type Your Name and pres &lt;enter&gt; to begin:</span>
                    <input placeholder="Type your name and press enter" id="name" type="text">
                </span>
                <div id="chatdiv">
                </div>
                <input id="chatin" type="text" />
            </div>
        </div>
    </body>
    </html>

Websockets Server Script
------------------------

The Basics of Rampart-Server Websockets
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As you probably know, server scripts are passed a ``req`` object
when serving content via the http and https protocols.  Each request from a
client results in a call to the appropriate JavaScript callback, which receives
a fresh ``req`` object with the details of the request.

Websocket connections differ in that the ``req`` object is long lived and is unique
to the server-client connection rather than unique to a single request.  More
information on how this works can be found in the
:ref:`Server Websockets Section <rampart-server:Websockets>`.  However, here are the
basics we need to know for this tutorial:

 * The ``module.exports`` function is run for every incoming websocket message.
 * ``req.count`` is the number of times function has been called since connection was made.
 * The first run (``req.count==0``) will have an empty body and represents the initial connection.
 * The ``req`` object is reused with ``req.body`` updated for each incoming message.
 * Sending data to the client is done with ``req.wsSend()``.
 * Any data printed or put using ``req.printf``/``req.put`` will also be sent when
   ``req.wsSend()`` is called or when the ``module.exports`` function returns.
 * ``req.wsOnDisconnect`` registers a function that is run when you disconnect or you are disconnected by the client.
 * ``req.wsEnd`` forces a disconnect (but runs callback first);
 * ``req.websocketId`` is a unique number to identify the current connection to a single client.
 * ``req.wsIsBin`` is ``true`` if the client sends binary data.  Data will be in ``req.body``.
 * ``req.body`` is always a :green:`Buffer`.  If ``req.wsIsBin`` is ``false``, it can be converted to a :green:`String`
   using ``rampart.utils.sprintf('%s',req.body)`` or ``rampart.utils.bufferToString(req.body)``.

With your favorite editor, open the ``wsapps/wschat.js`` file and paste this stub script to begin:

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var ev = rampart.event;


    function getuser(req){
        // no real user management here
        // just use the cookie set in client-side script
        // however here is where you could add an authentication scheme
        return req.cookies.username;
    }

    function receive_message() {
    }

    function setup_event_listen(req) {
    }

    function forward_messages(req) {
    }

    // exporting a single function
    module.exports = function (req)
    {
        if (req.count==0) {
            /* first run upon connect, req.body is empty 
               Here is where we will set up the event to listen for
               incoming message from other users
             */
            setup_event_listen(req);
        } else {
            /* second and subsequent runs below.  Client has sent a message
               and we need to process and forward it to others who are
               listening via rampart.event.on above 
             */
            forward_messages(req);        
        }
        return null;
    }

Setup on Connect
~~~~~~~~~~~~~~~~

We will use the ``setup_event_listen()`` function to simulate authentication
of the user, set up an event, register a function to handle a client
disconnect and send a message letting other clients know a new client has
joined.


.. code-block:: javascript

    function setup_event_listen(req) {

        /* check for username */
        req.user_name=getuser(req);
        if(!req.user_name){
            req.wsSend({from: "System", id:req.websocketId, msg: "No user name provided, disconnecting"});
            req.wsEnd();
            return;
        }

        /* what to do if we are sent a message from another user.  Here 
           ``rampart.event.on`` registers a callback function to be
           executed.  The callback function will take two parameters: a
           variable provided by "event.on" and a provided by "trigger".  The
           function is registered with the event name "msgev" and the
           function name "userfunc_x" where x is the unique websocketId for
           this connection.  The varable "req" is passed to the
           proc_incoming_message as its first argument.  */
        ev.on("msgev", "userfunc_"+req.websocketId, receive_message, req);

        // set up function for when this user disconnects (either by browser disconnect or req.wsEnd() )
        req.wsOnDisconnect(function(){ 
            // msg -> everyone listening
            ev.trigger("msgev", {from:'System', id:req.websocketId, msg: req.user_name+" has left the conversation"});
            // remove our function unique to this use from the event
            ev.off("msgev", "userfunc_"+req.websocketId);
        });

        /* send a notification to all listening that we've joined the conversation */
        ev.trigger("msgev", {from:'System', id:req.websocketId, msg: req.user_name+" has joined the conversation"});

        // send a welcome message to client from the "System".
        req.wsSend( {from: "System", msg: `Welcome ${req.user_name}`} );
    }


The ``setup_event_listen()`` function is run only upon first connecting. 
When it is done, the server sends any pending messages and then waits for
either 1) a message from our client (which we will forward to other clients
in the code below), 2) for some other client or script to "trigger" our
event with ``rampart.event.trigger("msgev",...)`` or 3) some other event,
HTTP request, other websocket connections and any other asynchronous
function, such as ``setTimeout`` or asynchronous functions in 
:ref:`the rampart-redis module <rampart-redis:The rampart-redis module>`).  
This all happens within the Rampart event loop.

Receiving from Client and Forward
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Next we will use the ``forward_message()`` function to format and forward messages from
the client and send them to other connected clients using ``rampart.event.trigger``.

.. code-block:: javascript

    function forward_messages(req) {

        // only accepting text messages
        if(req.wsIsBin)
            return;

        if(req.body.length)
        {
            //send the plain text message to whoever is listening
            req.body = sprintf('%s',req.body);
            ev.trigger("msgev", {from:req.user_name, id:req.websocketId, msg:req.body});
        }
    }


Here we trigger the ``msgev`` event.  Every connected client, including the current one, and 
every script running, having registered the ``msgev`` event, will be triggered and run
the registered functions. 

Receiving from other Clients
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here we will fill in the function registered with ``rampart.event.on("msgev", ...)``.
Its job is to forward messages sent by other clients (via ``rampart.event.trigger``)
to the browser for display.  Since the client-side script does its own echo, we will
filter out messages from our own client.

.. code-block:: javascript

    /* process incoming message as sent below in ev.trigger() with data set to
       { from: user_name, id: user_id, [ msg: "a message" | file: binary_file_data] }
    */
    function receive_message(req, data) {
        if(data.id != req.websocketId) {//don't echo our own message
            req.wsSend({
                from: data.from, 
                msg: data.msg
            });
        }
    }


Full Script
~~~~~~~~~~~

Our script so far:

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var ev = rampart.event;

    function getuser(req){
        // no real user management here
        // just use the cookie set in client-side script
        // however here is where you could add an authentication scheme
        return req.cookies.username;
    }

    /* NOTES:
     * The module.exports function is run for every incoming websocket message.
     * The first run (req.count==0) will have an empty body and represents the initial connection.
     * The req object is reused with req.body updated for each incoming message.
     * Sending data back is done with req.wsSend().
     * Any data printed or put using req.printf/req.put will also be sent when
     *   req.wsSend() is called or when the module.exports function returns
     * req.count == number of times function has been called since connection was made.
     * req.wsOnDisconnect is a function that is run when you disconnect or you are disconnected by the client.
     * req.wsEnd forces a disconnect (but runs callback first);
     * req.websocketId is a unique number to identify the current connection to a single client.
     * req.wsIsBin is true if the client sends binary data.  Data will be in req.body
     * req.body is always a buffer.  If req.wsIsBin is false, it can be converted to a string.
     *   using rampart.utils.sprintf('%s',req.body) or rampart.utils.bufferToString(req.body)
     */


    /* process incoming message as sent below in ev.trigger() with data set to
       { from: user_name, id: user_id, [ msg: "a message" | file: binary_file_data] }
    */
    function receive_message(req, data) {
        if(data.id != req.websocketId) {//don't echo our own message
            req.wsSend({
                from: data.from, 
                msg: data.msg
            });
        }
    }

    function setup_event_listen(req) {

        /* check for username */
        req.user_name=getuser(req);
        if(!req.user_name){
            req.wsSend({from: "System", id:req.websocketId, msg: "No user name provided, disconnecting"});
            req.wsEnd();
            return;
        }

        /* what to do if we are sent a message from another user.  Here 
           ``rampart.event.on`` registers a callback function to be
           executed.  The callback function will take two parameters: a
           variable provided by "event.on" and a provided by "trigger".  The
           function is registered with the event name "msgev" and the
           function name "userfunc_x" where x is the unique websocketId for
           this connection.  The varable "req" is passed to the
           proc_incoming_message as its first argument.  */
        ev.on("msgev", "userfunc_"+req.websocketId, receive_message, req);

        // set up function for when this user disconnects (either by browser disconnect or req.wsEnd() )
        req.wsOnDisconnect(function(){ 
            // msg -> everyone listening
            ev.trigger("msgev", {from:'System', id:req.websocketId, msg: req.user_name+" has left the conversation"});
            // remove our function unique to this use from the event
            ev.off("msgev", "userfunc_"+req.websocketId);
        });

        /* send a notification to all listening that we've joined the conversation */
        ev.trigger("msgev", {from:'System', id:req.websocketId, msg: req.user_name+" has joined the conversation"});

        // send a welcome message to client from the "System".
        req.wsSend( {from: "System", msg: `Welcome ${req.user_name}`} );
    }

    function forward_messages(req) {

        // only accepting text messages
        if(req.wsIsBin)
            return;

        if(req.body.length)
        {
            //send the plain text message to whoever is listening
            req.body = sprintf('%s',req.body);
            ev.trigger("msgev", {from:req.user_name, id:req.websocketId, msg:req.body});
        }
    }

    // exporting a single function
    module.exports = function (req)
    {
        if (req.count==0) {
            /* first run upon connect, req.body is empty 
               Here is where we will set up the event to listen for
               incoming message from other users
             */
            setup_event_listen(req);
        } else {
            /* second and subsequent runs below.  Client has sent a message
               and we need to process and forward it to others who are
               listening via rampart.event.on above 
             */
            forward_messages(req);        
        }
        return null;
    }

Handling Binary Data
--------------------

What we have so far works well for chatting.  However, users might want to
send files or images as well. So we will add that functionality.


Client-side Additions
~~~~~~~~~~~~~~~~~~~~~

We will alter our client-side script to handle drag-and-drop of files and
send them to the server.  Again, a full explanation of how this works is 
beyond the scope of this tutorial.

.. code-block:: html

    <!doctype html>
    <html>
    <head>
    <meta charset="UTF-8">
    <title>chat</title>
    <style>
    html,body {
        height:100%;
        font-family:Arial, Helvetica, sans-serif;
        margin:0;
    }
    #container{
        border:5px solid grey;
        position: absolute;
        margin-top : 10px;
        padding:10px;
        bottom:30px;
        top: 30px;
        right:20px;
        left:20px;
    }
    #chatdiv{
        padding:5px;
        border:2px solid gray;
        height: calc(100% - 175px);
        overflow-y: scroll;
        margin:0;
        margin-top: 5px;    
    }
    .event {
        color:#999;
    }
    .n {
        color:#393;
    }
    .i {
        vertical-align: top;
    }
    .s {
        color:#933;
    }
    #wrapper{
        height: 100%;
    }
    #name{
        width:220px;
    }
    #chatdiv.dropping {
        border: 2px blue dashed;
    }

    #chatin{
        width: calc(100% - 120px);
        height: 1.5em;
        margin-top: 7px;
    }
    </style>
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
    <script>
    $(document).ready(function() {
        var socket;
        var name;
        var reconnected=false;
        var cd = $('#chatdiv');
        var prot;
        var htmlEscape=true;

        // check our protocol, set matching websocket version
        if (/^https:/.test(window.location.href))
            prot='wss://'
        else
            prot='ws://'

        // check if connection is open
        function isOpen(ws) { return ws.readyState === ws.OPEN }

        // display image, or link other types of binary files
        function displayfile(data, blob)
        {
            var finfo=data.file;
            var b = blob.slice(0, blob.size, finfo.type);
            var linkurl = URL.createObjectURL(b);
            if(/^image/.test(finfo.type))
            {
                cd.append('<span class="s i">' + data.from + 
                ':</span> <img style="height: 300px"><br>');
                var img = cd.find('img').last();
                img.attr({'src': linkurl, 'alt': finfo.name});            
            } else {
                cd.append('<span class="s">' + data.from +  ':</span> FILE: <a>'+finfo.name+'</a><br>');
                var a = cd.find('a').last();
                a.attr({"href":linkurl, "download":finfo.name});
            }
            cd.scrollTop(cd.height() + 300);
            data=false;
        }

        // what to do when a file is dropped on conversation div
        function handle_drop(e){
            e.preventDefault();
            e.stopPropagation();
            cd.removeClass("dropping");
            e = e.originalEvent;
            if(!isOpen(socket))
            return;//fix me.

            // send file to server, display it in conversation div
            function sendfile(file) {
                var reader = new FileReader()
                reader.onload = function (event) {
                    socket.send(event.target.result);
                };
                reader.readAsArrayBuffer(file);
                // we get to see the file too
                displayfile({from:name,file:{name:file.name,type:file.type}}, file);
            }

            // depending on browser API, send file metadata, then send file
            if (e.dataTransfer.items) {
                // Use DataTransferItemList interface to access the file(s)
                for (var i = 0; i < e.dataTransfer.items.length; i++) {
                    // If dropped items aren't files, reject them
                    if (e.dataTransfer.items[i].kind === 'file') {
                        var file = e.dataTransfer.items[i].getAsFile();
                        var json = JSON.stringify({file:{name:file.name,type:file.type}});
                        socket.send(json); // first send metadata 
                        sendfile(file);    // second send actual file
                    }
                }
            } else {
                // Use DataTransfer interface to access the file(s)
                for (var i = 0; i < e.dataTransfer.files.length; i++) {
                    var file = e.dataTransfer.files[i];
                    socket.send(JSON.stringify({file:file})); 
                    sendfile(file);
                }
            }
        }

        function getcookie(cname){ 
            //https://www.30secondsofcode.org/js/s/parse-cookie
            var cookies = document.cookie
                .split(';').map(v => v.split('='))
                .reduce
                ( (acc, v) => {
                    acc[decodeURIComponent(v[0].trim())] = decodeURIComponent(v[1].trim());
                    return acc;
                    }, {}
                );
            return cookies[cname];
        }

        function showMessage(data){
            if(htmlEscape)
                data.msg = $('<div/>').text(data.msg).html();
            if(data.from=="System")
                cd.append('<span class="s">' + data.from + ":</span> " + data.msg +'<br>');
            else
                cd.append('<span class="n">' + data.from + ":</span> " + data.msg +'<br>');
            cd.scrollTop(cd.height());
        }

        var ExpectedFileData=[];

        function procmess (msg){
            var data;
            if(ExpectedFileData.length && msg.data instanceof Blob) {
                var fdata = ExpectedFileData.shift();
                displayfile(fdata, msg.data);
                return;
            }
            try{
                data = JSON.parse(msg.data);
            } catch (e) {
                cd.append('<span style="color:red;">error parsing message</span><br>');
            }
            // if reconnected, skip welcome message.
            if(reconnected){
                reconnected=false;
                return;
            }

            if(data){
                if (data.file)
                    ExpectedFileData.push(data);
                else
                    showMessage(data);
            }
        }

        function send(){
            var text=$('#chatin').val();

            if(text==""){
                return ;
            }

            var data= {
                msg: text,
                from: name
            };

            try{
                // attempt reconnect if discoonnected
                if(!isOpen(socket) && !reconnected) {
                    socket = new WebSocket(prot + window.location.host + "/wsapps/wschat.json");
                    socket.addEventListener('open', function(e){
                        socket.send(text);
                        reconnected=true;
                        $('#chatin').val("");
                        showMessage(data);
                        socket.onmessage = procmess;
                    });
                    return;
                }
                //send it
                socket.send(text);
                //echo it
                showMessage(data);
            } catch(e){
                showMessage({from:"System",msg:'error sending message'});
            }
            $('#chatin').val("");
        }

        function start() {
            if(socket)
                socket.close();
            socket = new WebSocket(prot + window.location.host + "/wsapps/wschat.json");
            socket.onmessage = procmess;
        }

        function setname() {
            name = $('#name').val();
            if(name=="")
            return;
            document.cookie = "username="+name + "; path=/; sameSite=Strict";
            start();
        }

        //drag and drop events
        cd.on("drop",handle_drop)
        .on("dragover",function(e){
            e.preventDefault();  
            e.stopPropagation();
            cd.addClass("dropping");
        })
        .on("dragleave",function(e){
            e.preventDefault();  
            e.stopPropagation();
            cd.removeClass("dropping");
        });

        // send message to server when <enter> is pressed
        $('#chatin').keypress(function(event) {
            if (event.keyCode == '13') {
                send();
            }
        });

        // sign on
        $('#name').keypress(function(event) {
            if (event.keyCode == '13') {
                setname();
                $('#namemsg').text("You are logged in as ");
                $('#chatin').focus();
            }
        });

        // check if we signed on previously 
        name = getcookie("username");

        if(name) {
            start();
            $('#name').val(name);
            $('#namemsg').text("You are logged in as ");
            $('#chatin').focus();
        }

    });
    </script>
    </head>
    <body>
        <div id="wrapper">
            <div id="container">
                <h2>wschat tutorial</h2>
                <span id="namespan">
                    <span id="namemsg">Type Your Name and pres &lt;enter&gt; to begin:</span>
                    <input placeholder="Type your name and press enter" id="name" type="text">
                </span>
                <div id="chatdiv">
                </div>
                <input id="chatin" type="text" />
            </div>
        </div>
    </body>
    </html>

Receiving Files from Client and Forward
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We will add to our ``forward_messages()`` function in order to receive files
from the websocket client.  When a file is dropped in the message area of
the web page, the client-side script will first send some JSON metadata
in a text message, then send the actual binary file.

We will receive the metadata and file in two separate messages (with
two separate calls to ``forward_messages()``).  This means we will have
to store the metadata somewhere, and have it accessible when we receive the file.
We will then be able to assemble a single :green:`Object` to pass to
``rampart.event.trigger()``.  Since the ``req`` :green:`Object` is 
recycled upon every message, we will set ``req.files`` to a 
:green:`Array` of :green:`Objects` (one :green:`Object` per file)
for this purpose.

.. code-block:: javascript

    function forward_messages(req) {
        /* we are sent a file from the client in two parts: 1) JSON meta info, 2) binary data */
        var fileInfo;  //variable for JSON meta info
        var file;  // will hold object with meta data and binary file content

        /* STEP 1:  Check message type:  Either - 
                       1) metadata for an incoming file
                       2) binary data for an incoming file
                       3) text message
        */

        // check non binary messages for JSON
        if(!req.wsIsBin && req.body.length)
        {
            /* messages are just plain text, but
               if it is a file, first we get the file meta info in JSON format */
            try{
                fileInfo=JSON.parse(req.body);
            } catch(e) {
                /* it is not binary, or json, so it must be a message */
                fileInfo = false;
            }
        }

        /* if it is binary data, we assume it is a file
           and the file info was already sent            */
        if(req.wsIsBin)
        {
            if(req.files && req.files.length)
            {
                //get the first entry of file meta info
                file = req.files.shift();
                // add the body buffer to it
                file.content = req.body;
                // tell everyone who it's from
                file.from=req.user_name;
            }
            else // handle unlikely case that metadata is not available.
                file = {from:req.user_name, name:"", type:"application/octet-stream", content:req.body};
        }
        else if(!fileInfo)
            req.body = sprintf("%s",req.body);//It's not binary, convert body buffer to a string


        /* STEP 2:  Process info from step 1 - 
                       1) if it is file metadata, save it in the "req.files" var and wait for next message
                       2) if we have both metadata and file content, trigger event and send to all other users
                       3) if we have a text message, trigger event to send message to all other users
        */
        if(fileInfo && fileInfo.file)
        {
            if(!req.files)
                req.files = [];
            /* store file meta info in req where we will retrieve it next time */
            req.files.push(fileInfo.file);
            // do nothing and get the actual binary file in the next message
        }
        else if (file)
        {
            /* we received a file, reassembled its meta info.  Send it to all that are listening */
            ev.trigger("msgev", {from:req.user_name, id:req.websocketId, file: file});
        }
        else if(req.body.length)
        {
            //send the plain text message to whoever is listening
            ev.trigger("msgev", {from:req.user_name, id:req.websocketId, msg:req.body});
        }

        /* Step 3:
             We received data, but here no data is sent back to the client.  However,
             it is sent to others using trigger and receive by the rampart.event.on
             function which they registered in their own connections.  So we can just return
             here
        */
    }


Receiving Files from other Clients
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now we alter the ``receive_message()`` function in order to send a file
back to the client.  Again, it is sent in two parts -- metadata then
the binary file.

.. code-block:: javascript

    /* process incoming message as sent below in ev.trigger() with data set to
       { from: user_name, id: user_id, [ msg: "a message" | file: binary_file_data] }
    */
    function receive_message(req, data) {
        if(data.id != req.websocketId) {//don't echo our own message
            // is this a file?  Sent two messages.
            if (data.file)
            {
                //send file metadata, then ...
                req.wsSend({
                    from: data.from, 
                    file: {name:data.file.name, type: data.file.type}
                });
                // ... send actual binary file
                req.wsSend(data.file.content);
            }
            else
            // it is a text message
            {
                req.wsSend({
                    from: data.from, 
                    msg: data.msg
                });
            }
        }
    }



Full Script Handling Files
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript 

    rampart.globalize(rampart.utils);
    var ev = rampart.event;


    function getuser(req){
        // no real user management here
        // just use the cookie set in client-side script
        // however here is where you could add an authentication scheme
        return req.cookies.username;
    }

    /* NOTES:
     * The module.exports function is run for every incoming websocket message.
     * The first run (req.count==0) will have an empty body and represents the initial connection.
     * The req object is reused with req.body updated for each incoming message.
     * Sending data back is done with req.wsSend().
     * Any data printed or put using req.printf/req.put will also be sent when
     *   req.wsSend() is called or when the module.exports function returns
     * req.count == number of times function has been called since connection was made.
     * req.wsOnDisconnect is a function that is run when you disconnect or you are disconnected by the client.
     * req.wsEnd forces a disconnect (but runs callback first);
     * req.websocketId is a unique number to identify the current connection to a single client.
     * req.wsIsBin is true if the client sends binary data.  Data will be in req.body
     * req.body is always a buffer.  If req.wsIsBin is false, it can be converted to a string.
     *   using rampart.utils.sprintf('%s',req.body) or rampart.utils.bufferToString(req.body)
     */

    /* process incoming message as sent below in ev.trigger() with data set to
       { from: user_name, id: user_id, [ msg: "a message" | file: binary_file_data] }
    */
    function receive_message(req, data) {
        if(data.id != req.websocketId) {//don't echo our own message
            // is this a file?  Sent two messages.
            if (data.file)
            {
                //send file metadata, then ...
                req.wsSend({
                    from: data.from, 
                    file: {name:data.file.name, type: data.file.type}
                });
                // ... send actual binary file
                req.wsSend(data.file.content);
            }
            else
            // it is a text message
            {
                req.wsSend({
                    from: data.from, 
                    msg: data.msg
                });
            }
        }
    }

    function setup_event_listen(req) {

        /* check for username */
        req.user_name=getuser(req);
        if(!req.user_name){
            req.wsSend({from: "System", id:req.websocketId, msg: "No user name provided, disconnecting"});
            req.wsEnd();
            return;
        }

        /* what to do if we are sent a message from another user.  Here rampart.event.on
           registers a function to be executed.  The function takes a parameter from "on"
           and a parameter from "trigger". The function is registered with the event name
           "msgev" and the function name "userfunc_x" where x is the unique websocketId for
           this connection. The varable "req" is passed to the proc_incoming_message as its
           first argument.                                                                   */
        ev.on("msgev", "userfunc_"+req.websocketId, receive_message, req);

        // set up function for when this user disconnects (either by browser disconnect or req.wsEnd() )
        req.wsOnDisconnect(function(){ 
            // msg -> everyone listening
            ev.trigger("msgev", {from:'System', id:req.websocketId, msg: req.user_name+" has left the conversation"});
            // remove our function unique to this use from the event
            ev.off("msgev", "userfunc_"+req.websocketId);
        });

        /* send a notification to all listening that we've joined the conversation */
        ev.trigger("msgev", {from:'System', id:req.websocketId, msg: req.user_name+" has joined the conversation"});

        // send a welcome message to client from the "System".
        req.wsSend( {from: "System", msg: `Welcome ${req.user_name}`} );
    }


    function forward_messages(req) {
        /* we are sent a file from the client in two parts: 1) JSON meta info, 2) binary data */
        var fileInfo;  //variable for JSON meta info
        var file;  // will hold object with meta data and binary file content

        /* STEP 1:  Check message type:  Either - 
                       1) metadata for an incoming file
                       2) binary data for an incoming file
                       3) text message
        */

        // check non binary messages for JSON
        if(!req.wsIsBin && req.body.length)
        {
            /* messages are just plain text, but
               if it is a file, first we get the file meta info in JSON format */
            try{
                fileInfo=JSON.parse(req.body);
            } catch(e) {
                /* it is not binary, or json, so it must be a message */
                fileInfo = false;
            }
        }

        /* if it is binary data, we assume it is a file
           and the file info was already sent            */
        if(req.wsIsBin)
        {
            if(req.files && req.files.length)
            {
                //get the first entry of file meta info
                file = req.files.shift();
                // add the body buffer to it
                file.content = req.body;
                // tell everyone who it's from
                file.from=req.user_name;
            }
            else // handle unlikely case that metadata is not available.
                file = {from:req.user_name, name:"", type:"application/octet-stream", content:req.body};
        }
        else if(!fileInfo)
            req.body = sprintf("%s",req.body);//It's not binary, convert body buffer to a string


        /* STEP 2:  Process info from step 1 - 
                       1) if it is file metadata, save it in the "req.files" var and wait for next message
                       2) if we have both metadata and file content, trigger event and send to all other users
                       3) if we have a text message, trigger event to send message to all other users
        */
        if(fileInfo && fileInfo.file)
        {
            if(!req.files)
                req.files = [];
            /* store file meta info in req where we will retrieve it next time */
            req.files.push(fileInfo.file);
            // do nothing and get the actual binary file in the next message
        }
        else if (file)
        {
            /* we received a file, reassembled its meta info.  Send it to all that are listening */
            ev.trigger("msgev", {from:req.user_name, id:req.websocketId, file: file});
        }
        else if(req.body.length)
        {
            //send the plain text message to whoever is listening
            ev.trigger("msgev", {from:req.user_name, id:req.websocketId, msg:req.body});
        }

        /* Step 3:
             We received data, but here no data is sent back to the client.  However,
             it is sent to others using trigger and receive by the rampart.event.on
             function which they registered in their own connections.  So we can just return
             here
        */
    }

    // exporting a single function
    module.exports = function (req)
    {
        if (req.count==0) {
            /* first run upon connect, req.body is empty 
               Here is where we will set up the event to listen for
               incoming message from other users
             */
            setup_event_listen(req);
        } else {
            /* second and subsequent runs below.  Client has sent a message
               and we need to process and forward it to others who are
               listening via rampart.event.on above 
             */
            forward_messages(req);        
        }
        return null;
    }

Chat with History using Redis
-----------------------------

There are many enhancements you might want to make when using the above examples
as a template for a robust chat.  However it will be difficult to allow users
to refresh the page or log back and see old messages unless there is some 
databasing.  Below we will use Redis to both save messages and replace ``rampart.event``.

We will set up our new version by copying the files ``webserver/html/websockets-chat/index.html``
and ``webserver/wsapps/wschat.js`` to ``webserver/html/websockets-chat/index-redis-chat.html``
and ``webserver/wsapps/wschat-redis.js`` respectively.

Client-Side changes
~~~~~~~~~~~~~~~~~~~

The client-side script (now at ``webserver/html/websockets-chat/index-redis-chat.html``) only needs
a minimal update.  Simply replace the two references to ``/wsapps/wschat.json`` to 
``/wsapps/wschat-redis.json``.

Starting Redis
~~~~~~~~~~~~~~

We will start by editing the copied ```webserver/wsapps/wschat-redis.js`` script.

Here, we will have Redis running on the same machine and started by the
``wschat-redis.js`` script as necessary.  It is worth noting that Redis
could be running on a different machine with our script potentially running
on several machines, since Redis does all of its communication over a
socket.

The startup function checks for an existing handle to Redis attached to the
``req`` handle.  If it doesn't exist, it makes the connection to Redis and
saves the handle as a property of ``req``.  If that fails (i.e. Redis is
not running), ``redis-server`` is located and started with a simple configuration
that is passed to it via ``stdin``.  At any point, if there is an error, that
error will be returned.  If all is successful, it returns undefined.

So we add the following to our script:

.. code-block:: javascript

    var redis=require("rampart-redis");

    function init(req) {
        if(req.rcl)
            return;

        var redisDir=serverConf.dataRoot + '/redis_chat';
        var redisConf = "bind 127.0.0.1 -::1\n"                      +
                        "port 23741\n"                               +
                        "pidfile " + redisDir + "/redis_23741.pid\n" +
                        "dir " + redisDir + "\n";

        /* make the directory for redis */
        var dirstat = stat(redisDir);
        if(!dirstat) {
            try {
                rampart.utils.mkdir(redisDir);
            } catch (e){
                return e;
            }
        }

        /***** Test if Redis is already running *****/
        try {
            req.rcl=new redis.init(23741);
            return;
        } catch(e){}
        
        /***** LAUNCH REDIS *********/
        var ret = shell("which redis-server");

        if (ret.exitStatus != 0) {
            return "Could not find redis-server in PATH";
        }

        var rdexec = trim(ret.stdout);

        ret = exec("nohup", rdexec, "-", {background: true, stdin:redisConf});
        var rpid = ret.pid;

        sleep(0.5);

        if (!kill(rpid, 0)) {
            return "Failed to start redis-server";
        }

        try {
            req.rcl=new redis.init(23741);
            return;
        } catch(e){
            return e;
        }
        return;
    }

Replacing Event Functions
~~~~~~~~~~~~~~~~~~~~~~~~~

Redis has pub/sub commands. However they do not save the data for later use.
It also has `streams <https://www.redis.io/docs/manual/data-types/streams/>`_
that save the data and can be listened to using the ``x`` commands 
(``xadd``, ``xread``, etc.).  Rampart's redis client additionally includes the 
:ref:`xread_auto_async <rampart-redis:xread_auto_async>` command, which
monitors one or more Redis Streams without having to reissue the ``xread`` command
or track the last id seen.  This makes it work more like pub/sub with the
ability to save recent messages.

We will use :ref:`xread_auto_async <rampart-redis:xread_auto_async>` and 
:ref:`xadd <rampart-redis:Supported Commands>` to replace ``rampart.event.on``
and ``rampart.event.trigger`` respectively.

First, unlike ``rampart.event``, data encapsulated in :green:`Objects` needs to
be converted to and from JSON when sending it to the Redis server.  Conversion
to JSON is handled automatically.  

However there are two caveats:

  *  Since Redis can store data of many types,
     we will need to manually parse the received JSON data from Redis.
  *  JSON is not the ideal format for binary buffer data, so we will 
     encode the file to a base64 string.  Note that an alternative would be
     to use `CBOR <https://duktape.org/guide.html#builtin-cbor>`_ encoding.

We'll create a function to do just that when sending data back to the client
with ``req.wsSend()``:

.. code-block:: javascript

    function sendWsMsg(req,data) {
       if (data.file) {
       // sending a file
            // JSON from redis must be decoded.
            try{
                data.file = JSON.parse(data.file);
            } catch(e){}
            if(!data.file.content)
            {
                return;
            }
            //send file metadata, then ...
            req.wsSend({
                from: data.from, 
                file: {name:data.file.name, type: data.file.type}
            });
            // ... send actual binary file
            req.wsSend(bprintf('%!B',data.file.content));
        }
        else
        // it is a text message
        {
            req.wsSend({
                from: data.from, 
                msg: data.msg
            });
        }
    }

With ``rampart.event.on`` we could pass the ``req`` variable to be used
in the registered callback function.  Using Redis, we don't have that option,
so instead we will embed the ``receive_message()`` function inside the 
``setup_event_listen`` function.  That way ``req`` will be in scope and 
available.

Also, since we now have the ``sendWsMsg()`` function, we can use it in place
of the ``req.wsSend()`` logic we were previously using.

.. code-block:: javascript

    function receive_message(strdata) {
        if(!strdata){ // undefined on disconnect
            req.wsSend({from:'System', id:req.websocketId, msg: "you are now disconnected."})
            req.wsEnd();
            return;
        }
        var data = strdata.data[0].value;
        if(data.id != req.websocketId)
            sendWsMsg(req,data);
     }


Now we need to replace ``rampart.event.trigger`` with ``xadd``.  We'll make a function
for that as well.  We will assume that we want to limit the number of messages we
save to around 2000.  See the 
`xadd capped strings discussion <https://www.redis.io/commands/xadd/#capped-streams>`_
for more information on limiting the number of messages in a stream.

.. code-block:: javascript

    function rtrigger(req, obj) {
        try {
            req.rcl.xadd(stream, "MAXLEN", '~', '2000', "*", obj);
        } catch(e) {
            req.wsSend({
                from: "System", 
                id:req.websocketId, 
                msg: sprintf("Error sending msg: %s", e)
            });
        }
    }

Note that if the Redis server cannot be reached, or some other error
happens, the ``req.rcl.xadd`` command will throw an error.  Here
we merely send that error back to the client.

Listening for Messages
~~~~~~~~~~~~~~~~~~~~~~

At this point, we are ready to make changed to the ``setup_event_listen``
function, using the changes above.

.. code-block:: javascript

    var stream = "mystream";

    function setup_event_listen(req) {
        /* check for username */
        req.user_name=getuser(req);
        if(!req.user_name){
            req.wsSend({from: "System", id:req.websocketId, msg: "No user name provided, disconnecting"});
            setTimeout(function(){
                req.wsEnd();
            },5);
            return;
        }

        function receive_message(strdata) {
            if(!strdata){ // undefined on disconnect
                req.wsSend({from:'System', id:req.websocketId, msg: "you are now disconnected."})
                req.wsEnd();
                return;
            }
            var data = strdata.data[0].value;
            if(data.id != req.websocketId)
                sendWsMsg(req,data);
         }

        /* what to do if we are sent a message from another user. */
        var subscriptions = {};
        subscriptions[stream]='$'; //only listening for one stream
        req.rcl.xread_auto_async(subscriptions, receive_message);

        // set up function for when this user disconnects (either by browser disconnect or req.wsEnd() )
        req.wsOnDisconnect(function(){
            rtrigger(req,{from:'System', id:req.websocketId, msg: req.user_name+" has left the conversation"});
            req.rcl.close();
        });

        /* send a notification to all listening that we've joined the conversation */
        // msg -> everyone listening
        rtrigger(req, {from:'System', id:req.websocketId, msg: req.user_name+" has joined the conversation"});

        req.wsSend( {from: "System", msg: `Welcome ${req.user_name}`} );
    }

However, when the client first connects, we also want to send a few of the old
messages, so the client can see what they missed.  We will use another Redis
Stream command ``xrevrange`` and wrap it in a ``try{} catch(e){}`` block as well.

.. code-block:: javascript

    function setup_event_listen(req) {
        /* check for username */
        req.user_name=getuser(req);
        if(!req.user_name){
            req.wsSend({from: "System", id:req.websocketId, msg: "No user name provided, disconnecting"});
            setTimeout(function(){
                req.wsEnd();
            },5);
            return;
        }

        function receive_message(strdata) {
            if(!strdata){ // undefined on disconnect
                req.wsSend({from:'System', id:req.websocketId, msg: "you are now disconnected."})
                req.wsEnd();
                return;
            }
            var data = strdata.data[0].value;
            if(data.id != req.websocketId)
                sendWsMsg(req,data);
         }

        /* what to do if we are sent a message from another user. */
        var subscriptions = {};
        subscriptions[stream]='$'; //only listening for one stream
        req.rcl.xread_auto_async(subscriptions, receive_message);

        // set up function for when this user disconnects (either by browser disconnect or req.wsEnd() )
        req.wsOnDisconnect(function(){
            rtrigger(req,{from:'System', id:req.websocketId, msg: req.user_name+" has left the conversation"});
            req.rcl.close();
        });

        /* send a notification to all listening that we've joined the conversation */
        // msg -> everyone listening
        rtrigger(req, {from:'System', id:req.websocketId, msg: req.user_name+" has joined the conversation"});

        try {
            //send the last <=50 messages
            var msgRange = req.rcl.xrevrange(stream, '+', '-', "COUNT", 50);
        
            for (var i=msgRange.length-1; i>-1; i--) {
                var msg = msgRange[i];
                if(msg.value.from!='System')
                    sendWsMsg(req,msg.value);
            }

            // send a welcome message to client from the "System".
            req.wsSend( {from: "System", msg: `Welcome ${req.user_name}`} );
        } catch(e) {
            req.wsSend( {from: "System", msg: sprintf("Error getting messages: %s",e) });
        }
    }

Forwarding Messages
~~~~~~~~~~~~~~~~~~~

Using our ``rtrigger()`` function above, we can also alter our
``forward_messages()`` function.  There are two changes from the
``rampart.event`` version : 1) ``ev.trigger`` is replaced with ``rtrigger``
and when sending a file, we base64 encode it using ``sprintf("%B")``.

.. code-block:: javascript

    function forward_messages(req) {
        /* we are sent a file from the client in two parts: 1) JSON meta info, 2) binary data */
        var fileInfo;  //variable for JSON meta info
        var file;  // will hold object with meta data and binary file content

        /* STEP 1:  Check message type:  Either - 
                       1) metadata for an incoming file
                       2) binary data for an incoming file
                       3) text message
        */

        // check non binary messages for JSON
        if(!req.wsIsBin && req.body.length)
        {
            /* messages are just plain text, but
               if it is a file, first we get the file meta info in JSON format */
            try{
                fileInfo=JSON.parse(req.body);
            } catch(e) {
                /* it is not binary, or json, so it must be a message */
                fileInfo = false;
            }
        }

        /* if it is binary data, we assume it is a file
           and the file info was already sent            */
        if(req.wsIsBin)
        {
            if(req.files && req.files.length)
            {
                //get the first entry of file meta info
                file = req.files.shift();
                // add the body buffer to it
                file.content = sprintf("%B", req.body);
                // tell everyone who it's from
                file.from=req.user_name;
            }
            else // handle unlikely case that metadata is not available.
                file = {from:req.user_name, name:"", type:"application/octet-stream", content:req.body};
        }
        else if(!fileInfo)
            req.body = sprintf("%s",req.body);//It's not binary, convert body buffer to a string


        /* STEP 2:  Process info from step 1 - 
                       1) if it is file metadata, save it in the "req.files" var and wait for next message
                       2) if we have both metadata and file content, trigger event and send to all other users
                       3) if we have a text message, trigger event to send message to all other users
        */
        if(fileInfo && fileInfo.file)
        {
            if(!req.files)
                req.files = [];
            /* store file meta info in req where we will retrieve it next time */
            req.files.push(fileInfo.file);
            // do nothing and get the actual binary file in the next message
        }
        else if (file)
        {
            /* we received a file, reassembled its meta info.  Send it to all that are listening */
            rtrigger(req, {from:req.user_name, id:req.websocketId, file: file});
        }
        else if(req.body.length)
        {
            //send the plain text message to whoever is listening
            rtrigger(req, {from:req.user_name, id:req.websocketId, msg:req.body});
        }

        /* Step 3:
             We received data, but here no data is sent back to the client.  However,
             it is sent to others using trigger and receive by the rampart.event.on
             function which they registered in their own connections.  So we can just return
             here
        */
    }


Full Script with Redis
~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    rampart.globalize(rampart.utils);
    var redis=require("rampart-redis");

    var stream = "mystream";

    function init(req) {
        if(req.rcl)
            return;

        var redisDir=serverConf.dataRoot + '/redis_chat';
        var redisConf = "bind 127.0.0.1 -::1\n"                      +
                        "port 23741\n"                               +
                        "pidfile " + redisDir + "/redis_23741.pid\n" +
                        "dir " + redisDir + "\n";

        /* make the directory for redis */
        var dirstat = stat(redisDir);
        if(!dirstat) {
            try {
                rampart.utils.mkdir(redisDir);
            } catch (e){
                return e;
            }
        }

        /***** Test if Redis is already running *****/
        try {
            req.rcl=new redis.init(23741);
            return;
        } catch(e){}
        
        /***** LAUNCH REDIS *********/
        var ret = shell("which redis-server");

        if (ret.exitStatus != 0) {
            return "Could not find redis-server in PATH";
        }

        var rdexec = trim(ret.stdout);

        ret = exec("nohup", rdexec, "-", {background: true, stdin:redisConf});
        var rpid = ret.pid;

        sleep(0.5);

        if (!kill(rpid, 0)) {
            return "Failed to start redis-server";
        }

        try {
            req.rcl=new redis.init(23741);
            return;
        } catch(e){
            return e;
        }
        return;
    }

    function getuser(req){
        /* Here is where you can look at headers, cookies or whatever 
           to find user name. */
        return req.cookies.username;
    }

    function sendWsMsg(req,data) {
       if (data.file) {
       // sending a file
            // JSON from redis must be decoded.
            try{
                data.file = JSON.parse(data.file);
            } catch(e){}
            if(!data.file.content)
            {
                return;
            }
            //send file metadata, then ...
            req.wsSend({
                from: data.from, 
                file: {name:data.file.name, type: data.file.type}
            });
            // ... send actual binary file
            req.wsSend(bprintf('%!B',data.file.content));
        }
        else
        // it is a text message
        {
            req.wsSend({
                from: data.from, 
                msg: data.msg
            });
        }
    }

    function rtrigger(req, obj) {
        try {
            req.rcl.xadd(stream, "MAXLEN", '~', '2000', "*", obj);
        } catch(e) {
            req.wsSend({
                from: "System", 
                id:req.websocketId, 
                msg: sprintf("Error sending msg: %s", e)
            });
        }
    }

    function setup_event_listen(req) {
        /* check for username */
        req.user_name=getuser(req);
        if(!req.user_name){
            req.wsSend({from: "System", id:req.websocketId, msg: "No user name provided, disconnecting"});
            setTimeout(function(){
                req.wsEnd();
            },5);
            return;
        }

        function receive_message(strdata) {
            if(!strdata){ // undefined on disconnect
                req.wsSend({from:'System', id:req.websocketId, msg: "you are now disconnected."})
                req.wsEnd();
                return;
            }
            var data = strdata.data[0].value;
            if(data.id != req.websocketId)
                sendWsMsg(req,data);
         }

        /* what to do if we are sent a message from another user. */
        var subscriptions = {};
        subscriptions[stream]='$'; //only listening for one stream
        req.rcl.xread_auto_async(subscriptions, receive_message);

        // set up function for when this user disconnects (either by browser disconnect or req.wsEnd() )
        req.wsOnDisconnect(function(){
            rtrigger(req,{from:'System', id:req.websocketId, msg: req.user_name+" has left the conversation"});
            req.rcl.close();
        });

        /* send a notification to all listening that we've joined the conversation */
        // msg -> everyone listening
        rtrigger(req, {from:'System', id:req.websocketId, msg: req.user_name+" has joined the conversation"});

        try {
            //send the last <=50 messages
            var msgRange = req.rcl.xrevrange(stream, '+', '-', "COUNT", 50);
        
            for (var i=msgRange.length-1; i>-1; i--) {
                var msg = msgRange[i];
                if(msg.value.from!='System')
                    sendWsMsg(req,msg.value);
            }

            // send a welcome message to client from the "System".
            req.wsSend( {from: "System", msg: `Welcome ${req.user_name}`} );
        } catch(e) {
            req.wsSend( {from: "System", msg: sprintf("Error getting messages: %s",e) });
        }
    }

    function forward_messages(req) {
        /* we are sent a file from the client in two parts: 1) JSON meta info, 2) binary data */
        var fileInfo;  //variable for JSON meta info
        var file;  // will hold object with meta data and binary file content

        /* STEP 1:  Check message type:  Either - 
                       1) metadata for an incoming file
                       2) binary data for an incoming file
                       3) text message
        */

        // check non binary messages for JSON
        if(!req.wsIsBin && req.body.length)
        {
            /* messages are just plain text, but
               if it is a file, first we get the file meta info in JSON format */
            try{
                fileInfo=JSON.parse(req.body);
            } catch(e) {
                /* it is not binary, or json, so it must be a message */
                fileInfo = false;
            }
        }

        /* if it is binary data, we assume it is a file
           and the file info was already sent            */
        if(req.wsIsBin)
        {
            if(req.files && req.files.length)
            {
                //get the first entry of file meta info
                file = req.files.shift();
                // add the body buffer to it
                file.content = sprintf("%B", req.body);
                // tell everyone who it's from
                file.from=req.user_name;
            }
            else // handle unlikely case that metadata is not available.
                file = {from:req.user_name, name:"", type:"application/octet-stream", content:req.body};
        }
        else if(!fileInfo)
            req.body = sprintf("%s",req.body);//It's not binary, convert body buffer to a string


        /* STEP 2:  Process info from step 1 - 
                       1) if it is file metadata, save it in the "req.files" var and wait for next message
                       2) if we have both metadata and file content, trigger event and send to all other users
                       3) if we have a text message, trigger event to send message to all other users
        */
        if(fileInfo && fileInfo.file)
        {
            if(!req.files)
                req.files = [];
            /* store file meta info in req where we will retrieve it next time */
            req.files.push(fileInfo.file);
            // do nothing and get the actual binary file in the next message
        }
        else if (file)
        {
            /* we received a file, reassembled its meta info.  Send it to all that are listening */
            rtrigger(req, {from:req.user_name, id:req.websocketId, file: file});
        }
        else if(req.body.length)
        {
            //send the plain text message to whoever is listening
            rtrigger(req, {from:req.user_name, id:req.websocketId, msg:req.body});
        }

        /* Step 3:
             We received data, but here no data is sent back to the client.  However,
             it is sent to others using trigger and receive by the rampart.event.on
             function which they registered in their own connections.  So we can just return
             here
        */
    }

    // exporting a single function
    module.exports = function (req)
    {
        var err = init(req);
        if(err) {
            sendWsMsg(req, {from:'System', msg: sprintf('%s',err)});
            fprintf(stderr, '%s', err);  //this will go to error log if logging is set on
            return;
        }
        if (req.count==0) {
            /* first run upon connect, req.body is empty 
               Here is where we will set up the event to listen for
               incoming message from other users
             */
            setup_event_listen(req);
        } else {
            /* second and subsequent runs below.  Client has sent a message
               and we need to process and forward it to others who are
               listening via rampart.event.on above 
             */
            forward_messages(req);        
        }


        return null;
    }

Improvements
------------

We purposely kept these examples very simple in order to clearly
demonstrate the concepts we covered, so it has a long way to go
to be used in production.

There are several ways you could improve the above scripts on
your way to building a full app. Some of the more obvious ones 
include:

*  Create a user management system.
*  Improve the look and functionality of the client-side script.
*  Using multiple Redis Streams to add Direct Messaging and Channels.
*  Adding search by storing old messages in a 
   :ref:`rampart-sql <rampart-sql:Preface>` database with a Full Text Index.

Enjoy!
