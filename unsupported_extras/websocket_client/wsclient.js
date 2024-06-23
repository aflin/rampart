#!/usr/bin/env rampart

/* This is a stand alone websocketclient as well as a module.

   * For usage as a client run without arguments for usage help
     When connected, there are a few commands you can use:
         .save -- save the last incoming binary message to a file
         .send -- send a file as binary
         .close -- close the connection and exit.


   * Example usage as a module:

    var wsclient = require("/path/to/wsclient.js");
      or
    var wsclient = require("wsclient"); //if wsclient is in a standard location for modules

    // the function that handles incoming messages
    function messIn(msg) {
        if(msg.binary){
            rampart.utils.printf("received binary data\n");
            do_something_with_bdata(msg.message);
        } else {
            rampart.utils.printf("message: %s\n",msg.message);
        }

        var resp = produce_response(msg);
        wsocket.wsSend(resp);
    }

    // the init function
    // callbacks   - an object of events and handlers
    // headers     - extra headers to send or override the standard ones
    //               when negotiating websockets over http
    // url         - the url (beginning with "ws://' or 'wss://')
    // showheaders - Boolean - print request headers to stdout upon connecting (default false)  

    var wsocket = wsclient({
        callbacks: {"message": msgIn},
        headers: headers,
        url: wurl,
        showheaders: showheaders
    });

    / * wsocket is a net.socket (see https://rampart.dev/docs/rampart-net.html#socket-functions )
       with the additional functions 'wsocket.wsClose()' and  'wsocket.wsSend()'. The former sends
       a close request to the server. The latter formats
       the message and sends it to the connected server in a websocket frame.

       USAGE:       wsocket.wsSend(mystringOrBuffer[, isBinary])
           Where:
                 - mystringOrBuffer is a string or buffer object and
                 - isBinary is a boolean, whether to flag message as binary (default is false)

        Also messages are received by registering a "message" event function (e.g.
        'wsocket.on("message", msgIn)') or by suppying it to
        the exported init function (see above).
     * /

     TODO:
         Add robust error checking
         Test receiving multi-fragment messages

*/

rampart.globalize(rampart.utils);
load.net;
load.url;
load.crypto;

/*
GET /wsapps/websockets_chat/wschat.json HTTP/1.1
Host: localhost:8088
User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:104.0) Gecko/20100101 Firefox/104.0
Accept: * / *
Accept-Language: en-US,en;q=0.5
Sec-WebSocket-Version: 13
Origin: http://localhost:8088
Sec-WebSocket-Key: YLwCPmkxgf2QqAijRGMsuw==
Connection: keep-alive, Upgrade
Pragma: no-cache
Cache-Control: no-cache
Upgrade: websocket

Accept-Encoding: gzip, deflate, br
Sec-WebSocket-Extensions: permessage-deflate
Sec-Fetch-Dest: websocket
Sec-Fetch-Mode: websocket
Sec-Fetch-Site: same-origin
*/

var defaults = {
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64; rv:104.0) Gecko/20100101 Firefox/104.0",
    "Accept": "*/*",
    "Pragma": "no-cache",
    "Cache-Control": "no-cache",
    "Upgrade": "websocket",
    "Sec-WebSocket-Version": "13",

    "Connection": "keep-alive, Upgrade"
}

function make_headers(h) {
    var ret={};

    var randbytes = crypto.rand(16);
    var randhash = sprintf('%B',randbytes);
    h["Sec-WebSocket-Key"] = randhash;
    Object.assign(ret, defaults, h);
    return ret;
}

function warn(msg, socket){
    if(socket._events.warn) {
        var wfunc = Object.values(socket._events.warn);
        if(wfunc.length) {
            for(var i=0;i<wfunc.length;i++) {
                wfunc[i](msg);
            }
            return;
        }
    }
    fprintf(stderr, "Warn: '%s'\r\n", msg);
}

function parseheaders(h,socket) {
    var ret = {}, line;
    var top = h.split('\r\n\r\n');
    var hs = top[0].split('\n');

    if(hs[0])
        ret.Status=hs[0].trim();    

    for (var i=1;i<hs.length;i++) {
        line = hs[i];
        comp=line.split(':');
        if(comp.length < 2){
            if(line!='\r' && line.length!=0)
                warn("Illegal header response from server: " + line.length, socket);
        } else {
            var k = comp.shift();
            var v = comp.join(':');
            ret[k] = v.trim();
        }
    }

    return ret;
}

function checkSec(h,e) {
    var s;
    for (var k in h) {
        var k2 = k.toLowerCase();
        if(k2 == 'sec-websocket-accept')
        {
            s=h[k];
            break;
        }
    }
    return (s==e);
}

function error(socket, msg){
    socket.trigger('error',"Unhandled Error: "+msg);
}

function wsclose() {
    var b=bprintf("%c%c", 136, 128);
    this.write(b);
    this.destroy();
}

function sendResp(socket, msg, opcode) {
    var mtype = getType(msg), len, hsize;

    if(opcode===false || opcode===undefined)
        opcode=1;
    else if(opcode===true)
        opcode=2;
    else if(getType(opcode) != 'Number')
        throw("wsocket.wsSend: Opcode must be a Boolean or a number (0,1,2,8,9,10)");

    if(mtype!='Buffer' && mtype != 'String')
        msg=sprintf('%J',msg);

    len=msg.length;

    if(len < 126)
        hsize=6;
    else if (len < 65536)
        hsize=8;
    else if (len < 9007199254740992)
        hsize=14;
    else
        error(socket,"data too large to send");

    var buf = Buffer.alloc(hsize);

    buf[0]= 128 | opcode;

    var r = crypto.rand(4);
    buf[hsize-1]=r[3];
    buf[hsize-2]=r[2];
    buf[hsize-3]=r[1];
    buf[hsize-4]=r[0];

    switch (hsize) {
        case 6:
            buf[1] = 128 + len;
            break;
        case 8:
            buf[1] = 254;
            buf[2] = len >> 8;
            buf[3] = len & 255;
            break;
        case 14:
            var hi32 = parseInt(len/2**32)
            var low32 = len - hi32 * 2**32;
            buf[1] = 255;
            buf[2] = 0;
            buf[3] = hi32 >> 16;
            buf[4] = hi32 >> 8 & 255;
            buf[5] = hi32 & 255;
            buf[6] = low32 >> 24;
            buf[7] = low32 >> 16 & 255;
            buf[8] = low32 >> 8 & 255;
            buf[9] = low32 & 255;
            break;
    }

    var pos, byten, outbuf = abprintf(buf,hsize,'%s',msg);
    
    for (var i=0; i<len; i++) {
        pos = hsize + i;
        byten = i % 4;
        outbuf[pos] = outbuf[pos] ^ r[byten];
    }
    socket.write(outbuf);
    //console.log(socket.bytesWritten);
}

function procmessage(socket, payload) {
    //printf("fin = %J, opcode=%J, size=%d\n", socket._fin, socket._opcode, payload.length);

    // if fin==1 && opcode!=0, a complete message in one frame.  Return the message.
    // if fin==1 && opcode==0, a continue frame, the last one.  Combine with previous and return message
    // if fin==0 && opcode!=0, the first frame in a multi-frame message.  Store for next call.
    // if fin==0 && opcode==0, a continue frame, not the first or last.  Combine with previous and store for next call.

    if (socket._fin){
        // got fin flag, so message is complete
        if(socket._opcode) {  // the easiest version. One packet, unfragmented.
            socket.trigger("message", {message:payload, binary: socket._opcode == 2?true:false} );
        } else {
            //socket._opcode==continue(0), the last fragment frame in a multi-fragment message
            if(!socket._fragment)
                socket.trigger("error", "received continue frame with no prior data");
            else {
                socket._fragment = abprintf(socket._fragment, "%s", payload);
                socket.trigger("message", {message: socket._fragment, binary: socket._opcode == 2?true:false} );
            }
        }
        // we are finished, so let go of related data
        socket._fragment = undefined;
        socket._opcode = undefined;
        socket._fin = undefined;
    } else {
        if(socket._opcode) {
            // opcode!=continue(0), fin!=1; first fragment of multi-frame message
            socket._fragment = bprintf('%s',payload);
        } else {
            //opcode==continue(0) and fin!=1, not the first fragment, and not the final
            if(!socket._fragment)
                socket.trigger("error", "received continue frame with no prior data");
            else
                socket._fragment = abprintf(socket._fragment, "%s", payload);
        }
    }
}

// process data that spans multiple tcp packets
function proc_partial(socket, data) {
    var partialdata;

    socket._wspartial = bprintf('%s%s', socket._wspartial, data);
    
    // we have enough to finish frame
    if (socket._wspartial.length >= socket._wslen)
    {
        //there's extra data for the next frame in this packet
        if(socket._wspartial.length > socket._wslen) {
            partialdata = socket._wspartial.subarray(0, socket._wslen);
            // return data after this frame to be processed
            data = socket._wspartial.subarray(socket._wslen);
        } else { // socket._wspartial.length == socket._wslen (packet end == frame end)
            partialdata = socket._wspartial;
            data=undefined;
        }
        procmessage(socket, partialdata);

        socket._wspartial = undefined;
        socket._wslen = undefined;
    }
    else {// we have more to get.
        //console.log("got continued need more "+ socket._wslen + " > " + socket._wspartial.length);
        data = undefined;
    }
    return data;
}

// the socket.on('data') callback; handle incoming tcp packets
function proc_data(data) {
    var socket = this, verified;

    //printf("got %d of data: %s\r\n", data.length, data);
    /* Just connected and finished http upgrade handshake; store server headers */
    if(!socket.headers) {
        socket.headers = parseheaders(sprintf("%s",data), socket);

        if(!socket.headers.Status)
        {
            socket.trigger("error", "http status line not found while negotiating websocket connection");
            socket.destroy();
            return;
        }

        var statcomp = socket.headers.Status.split(/\s+/);
        if(statcomp.length < 3) {
            socket.trigger("error", "invalid http status line while negotiating websocket connection: " + socket.headers.Status);
            socket.destroy();
            return;
        }

        var statcode = statcomp[1];

        socket._httpStatusCode=parseInt(statcode);

        if(statcode != "101") {
            socket.trigger("error", "bad http status: " + socket.headers.Status);
            socket.destroy();
            return;
        }

        verified = checkSec(socket.headers, socket._expectedhash);

        if(!verified)
            socket.trigger("error", "sec-websocket-key does not match hash computed from sec-websocket-accept"); 

        //printf('%s\nheaders: %3J\nverified=%J\n', expectedhash,socket.headers,verified);
        return;
    } //end server headers

    /* 
    This function gets a tcp packet, which might hold a full, partial or multiple websocket frames.
    Three possibilities:
        1) tcp packet is the same size as the websocket frame
              -- eazy peazy
        2) tcp packet holds more than one websocket frame (or partial frame)
              -- handle it in the do/while loop below
        3) websocket frame spans multiple tcp packets.
              -- save partial frame in socket._wspartial, revisit and combine
                 when this function is called again.
    */

    // another tcp packet of data, continuing message frame from previous call
    if(socket._wspartial) {
        data=proc_partial(socket,data);
        if(!data)
            return;
    }

    // set to true if we process a webframe and there is still data in the packet
    var moredata = false;

    do {
        var start=2, len = 0, 
            lenbyte=data[1] & 127, 
            opcode= data[0] & 15, 
            fin = data[0] & 128;

        moredata=false;

        if (lenbyte < 126) // if less than 126, lenbyte is the length
            len=lenbyte;
        else if (lenbyte == 126) { //if 126, len is in the next two bytes
            len = data[2] * 256 + data[3];
            start=4;
        } else if (lenbyte == 127) { //if 127, len is in the next 8 bytes
            // doubles can only handle 53 bits with integer precision, so
            // in javascript, we cannot accurately get the size of an incoming
            // message larger than 8192 terabytes (2**53)
            if (data[2] !=0 || (data[3] && 224) )
            {
                error(socket,"size of incoming data is too large");
            }
            len = 
                data[3] * 2**48 +
                data[4] * 2**40 +
                data[5] * 2**32 +
                data[6] * 2**24 +
                data[7] * 2**16 +
                data[8] * 2**8 +
                data[9];
            start=10;
        }
        
        var payload=data.subarray(start,len+start);

        if (opcode == 8) {
            socket.destroy();
        } else if (opcode == 9) { //ping
            data[0] += 1;
            data[1] |= 128;

            sendResp(socket, data.subarray(2,len+2), 10); 
            
            if(start+len < data.length) {  //there's another frame or partial frame in this packet
                data = data.subarray(start+len);
                moredata=true;
            }
        } else if (opcode < 3) { //cont=0, text=1, binary=2

            socket._fin = fin?true:false;
            socket._opcode = opcode;

            if(payload.length < len) {
                // payload will disappear with data, so copy it.
                socket._wspartial = bprintf('%s',payload);
                socket._wslen = len;
                return;
            }

            procmessage(socket, payload);

        }

        if(start+len < data.length) {  //theres another frame or partial frame in this packet
            data = data.subarray(start+len);
            moredata=true;
            //console.log("Getting another message in this packet");
        }

    } while (moredata);
}



function initconn(socket, wurl, opts) {
    var components = url.components(wurl);
    var req = "GET ";

    if(!opts) opts = {headers:{}};

    if(!opts.insecure) opts.insecure=false;
    if(!opts.timeout) opts.timeout=60000;

    if (getType(opts.headers)!='Object')
        throw("headers must be an object");
    //printf("%3J\n",components)
    opts.headers.Host=components.host  + (components.port?':'+components.port:"");

    req+=components.fullPath+" HTTP/1.1\r\n"
    var headers=make_headers(opts.headers);

    //printf("%3J\n%3J\n",headers, components);

    for (var key in headers) {
        var val=headers[key];
        req+=`${key}: ${val}\r\n`;
    }
    req+='\r\n';
    
    if(opts.showheaders) console.log(req);

    socket.on("connect", function(){
        //console.log("CONNECTED");
        this.write(req);
    });

    socket._expectedhash = sprintf('%B', crypto.sha1(headers["Sec-WebSocket-Key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", true) );

    socket.on("data", proc_data);

    var sopts = {
        port: components.port?components.port:(components.scheme=='wss'?443:80),
        host: components.host,
        timeout: opts.timeout,
        tls: components.scheme=='wss'?true:false,
        insecure: opts.insecure
    }

    socket.connect(sopts);

    return socket;
}

function init(opts) {
    var socket = new net.Socket();
    var headers = {};
    var url;

    if (getType(opts) == 'Object') {
        if(opts.callbacks && getType(opts.callbacks) == 'Object') {
            var cb = opts.callbacks;
            for (var k in cb) {
                if (getType(cb[k]) != 'Function')
                    throw("wsclient: option 'callbacks' must be an object with functions as values");
                socket.on(k,cb[k]);
            }
        } else if (opts.callbacks)
            throw("wsclient: option 'callbacks' must be an Object with functions as values");

        if(opts.headers && getType(opts.headers) == 'Object')
            headers=opts.headers;
        else if (opts.headers)
            throw("wsclient: option 'headers' must be an Object (key/value for headers to send)");
        if(opts.url) url = opts.url;
    }
    else if(!url) url=opts
    
    if (!url) {
        socket.initws = initcon;
        return socket;
    }

    initconn(socket, url, opts);
    socket.wsSend = function(msg, binary) {
        sendResp(socket, msg, binary)
    }
    socket.wsClose = wsclose;

    return socket;
}



if(global.module && global.module.exports)
    module.exports = init;  //we are the module loaded with require("wsclient.js");
else
{
    /* COMMAND LINE TOOL */
    var args = process.argv.slice(2), i, flag, wurl, headers={}, showheaders=false;

    function usage(msg,exitc) {
        if(!exitc) exitc=0;
        if(msg) printf("%s\n", msg);
        printf("%s [ -h header] [-s] url:\n"+
               "    url    where scheme is ws:// or wss://\n"+
               "    -H     header is a header to be added ('headername=headerval'). May be used more than once.\n"+
               "    -s     show raw http request to server on connect\n", 
               process.argv[1]
        );
        process.exit(exitc);
    }


    if (args.length < 1)
        usage();

    function procheader(s) {
        var e=s.indexOf('=');
        if (e < 0)
            usage("-H option must be followed by a header ('headername=headerval')", 1);
        headers[s.substring(0,e)] = s.substring(e+1);
    }

    for (var i=0; i<args.length;i++) {
        arg=args[i];
        if(arg.charAt(0) == '-') {
            flag=arg.substring(1);
            switch (flag) {
                case 'H':
                    i++;
                    if (i>=args.length)
                        usage("-h option must be followed by a header ('headername=headerval')", 1);
                    procheader(args[i]);
                    break;
                case 's':
                    showheaders=true;
                    break;
                default:
                    usage(arg+ " is not a known option", 1);
            }
        }
        else
            wurl=arg;
            if( ! /^wss?:\/\//.test(wurl) )
                usage(arg+ " is not a websocket url (must start with ws:// or wss://)",1); 
    }
    var binarydata;

    global.lines=repl(" > "); //copied to thread
    var thr = new rampart.thread();
    
    thr.exec(function(){
    });

    function domsg(msg) {
        if(msg.binary){
            printf("\r\nreceived binary data: (enter '.save' to save to file)\r\n");
            binarydata=msg.message;
        } else {
            printf("\r\nmessage: %s\r\n",msg.message);
        }
        repl.refresh();
    }

    var wsocket = init({
        callbacks: {"message": domsg},
        headers: headers,
        url: wurl,
        showheaders: showheaders
    });

    wsocket.on("connect", function(){
        function readstdin() {
            return global.lines.next();
        }

        function writeout(line, err) {
            //not doing much in thread that could cause an error.
            if(err) {
                global.lines.close();
                console.log(e);
                process.exit(1);
            }

            if(line===null)
                process.exit();
            switch(line) {
                case '.save':
                    if (binarydata) {
                        printf("Enter file name: ");
                        fflush(stdout);
                        var fname = readLine(stdin).next().trim();
                        if(fname != "") {
                            try {
                                fprintf(fname, '%s', binarydata);
                                printf("file '%s' saved\n", fname);
                            } catch (e) {
                                var errline = sprintf("%J", e).split('\n');
                                printf("Could not save '%s': %J\n", fname, errline[0]);
                            }
                        }
                        
                    } else {
                        printf("no binary data has been received\n");
                    }
                    break;
                case '.send':
                    printf("Enter file name: ");
                    fflush(stdout);
                    var fname = readLine(stdin).next().trim();
                    if(fname != "") {
                        try{
                            var f=readFile(fname);
                            wsocket.wsSend(f,true);
                            printf("file '%s' sent\n", fname);
                        } catch (e) {
                            var errline = sprintf("%J", e).split('\n');
                            printf("Could not save '%s': %J\n", fname, errline[0]);
                        }
                    }
                    break
                case '.close':
                    wsocket.wsClose();
                    return;
                default:
                    wsocket.wsSend(line);
                    break;
            }
            thr.exec( readstdin, writeout );
        }
        thr.exec( readstdin, writeout );
        
    }).on('error',function(e){
        global.lines.close();
        console.log(e);
        process.exit(1);
    });
}
