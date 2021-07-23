rampart.globalize(rampart.utils);
var ev = rampart.event;

var redis=require("rampart-redis");

/* an explanation of variable names:
    req  -  	    The variable passed to the websocket callback.
                    It is recycled for each incoming websocket message with
                    req.body being updated to the current message.  Other
                    variables can be attached as properties to req so that
                    they are perpetually available when subsequent messages
                    come in.

    req.count       How many times we have received messages.  when
                    req.count==0 req has an empty body (req.body) and is
                    useful for initializing variables on the first
                    connection.

    req.wsSend      Function.  Sends a websocket reply to the client.  May
                    be a buffer, string or object.  If object, it is
                    automatically converted to JSON.

    req.rcl -       Set to the redis connection handle.  Initialized once
                    per client connection and available always.
                    
    req.rcl.xread_auto_async - 
                    
                    XREAD [COUNT count] [BLOCK milliseconds] STREAMS key [key ...] ID [ID ...] 
                    
                    This is a modified version of the redis command XREAD, made to work more like
                    redis SUBSCRIBE.  It assumes and sends "XREAD BLOCK 0 STREAMS". 
                    It takes two parameters:
                        1) an object of key:id pairs
                        2) a callback function
                    When new data comes in, instead of exiting, it will reissue
                    the "XREAD BLOCK 0 STREAMS" command with the {key:id,key:id} object 
                    with the last received id updated in order to wait for the next message.

    req.userprefs - Set using req.rcl - user preferences stored in a redis
                    proxyObj that automatically saves property values in
                    redis in the appropriately named hkey.

    req.userId    - The id of the currently connected user.

    req.subscriptions - 
                    a list of subscriptions suitably formatted for req.rcl.xread_auto_async
                    built based on req.userprefs
*/

function authenticate(req){
    /* Here is where you can look at headers, cookies or whatever 
       to authenticate. */

    /* quick hack to get the user's name -- please fix when we get a proper auth */
    var husers = new req.rcl.proxyObj(`${req.params.orgid}_users`);
    req.userName=husers[req.params.userid].name;

    return req.params.userid;
}

/* subscribing to a user:user dm "channel" requires concatenation the
   ids to create the name of the dm redis stream                      */
function dmname(id,user) {
    return [id,user].sort().join(":");
}

/*  subscribe channels/streams

    Normal channels are multi-user #named groupings of messages which may be
    accessed by anyone in an organization.

    DM channels are private conversations between two users.

    A Stream is the redis storage/retrieval mechanism for a channel.
    
    Normal Channels and Users have a single ID such as 60715c588 and that ID
    is used for the name of the redis Stream.

    A DM Channel takes the ID of the user (not the current user).  The redis
    Stream name is a sorted concatenation of the current user and the DM
    user (e.g, 60715c00b and 60715c00a would result in a Stream named
    "60715c00a:60715c00b")

    A third Stream type also exists for general communication within an
    organization.  This "organization wide temp messages stream" is
    currently only used to update the online/offline/sleep status of a user
    and to display "is typing" messages.

    The Org Stream's name is identical to the Organization's ID.  The main
    difference between this Stream and other normal streams is the limited
    number of messages allowed in the Stream.  It is meant to act more like
    pub/sub where old messages do not need to be saved, but is implemented
    with the redis X* commands to avoid having more than one connection open
    to redis.

*/
function subscribe(req)
{
    /* cancel any old async connections and callbacks */
    req.rcl.close_async();

    /* subscribe to everything the user is entitled to see */
    if(!req.subscriptions)
    {
        var id = req.userId;
        req.subscriptions={};

        //organization wide temp messages stream
        req.subscriptions[req.orgStream]='$';

        //proxyObj userprefs holds the list of Normal and DM Channels
        var chans   = req.userprefs.chans;
        var dms     = req.userprefs.dms;

        /* '$': we are listening only for new messages.
           Old messages are sent upon request in 
           handle_messages() below */
        for (var i=0;i<chans.length;i++) {
            req.subscriptions[chans[i]]='$';
        }

        for (i=0;i<dms.length;i++) {
            dmchan = dmname(id, dms[i]);
            req.subscriptions[dmchan]='$';
        }
    }

    /* sample message (x.data.length should always be 1 in this setup, since we are using '$'):
        {
            stream:"60735c525:60735f529",
            data:[
                    {
                        id:"1618707735059-0",
                        value:
                            {
                                msg: "{\"who\":\"Aaron\",\"when\":1618707735010,\"message\":\"some message\"}",
                                from:"60735c525",
                                stream: "60735c525:60735f529"
                            }
                    }
                ]
        }

        NOTE: value.stream is the reference stream when sending messages on the 
              organization wide temp messages stream (see handle_messages() below)
    */
    req.rcl.xread_auto_async(req.subscriptions, function(x){
        
        if(x.stream == req.orgStream) //a organization wide message
        {
            // don't send org updates regarding dm if req.userId is not a part of the conversation
            var ids = x.data[0].value.stream.split(':');
            if (ids.length==2 && ids[0]!=req.userId && ids[1]!=req.userId)
                return;
            // TODO: if channel, check that channel is the user's list of channels, or don't send.  
        }

        //send every message from every subscribed channel or dm user (or org message not filtered from above).
        req.wsSend(x);

        // this is the stream the user is watching, so update lastseen here.
        if(x.stream == req.curStream)
        {
            var lsprop = req.curStream + '_last';
            if(x.data.length){ //should always eval true
                // set lastseen.channel to the id of the last message
                req.userprefs[lsprop]=x.data[x.data.length-1].id;
            }
        }
    });
    req.xsubscribed=true;
}

/* handle incoming messages */
var maxUpdate=100;
function handle_message(req, data)
{
    var stream;

    if(!req.xsubscribed) //this should happen only once per websocket connection
        subscribe(req);        

    /* Stream subscription requests are sent int data.msg.subStream */
    if (data.msg.subStream)
        stream=data.msg.subStream;

    /* make sure the message we are getting comes from the logged in user */
    if(data.from != req.userId) 
    {
        req.wsSend({from:"System", error: 'error parsing websocket message'});
        return;
    }

    /* send message via organization wide temporary messages stream */
    if(data.msg.orgMsg)
    {
        req.rcl.xadd(req.orgStream, "MAXLEN", '~', '20', "*", {msg:data.msg, from:data.from, stream:data.stream});
    }

    /* when user clicks on channel or dm or scrolls to the top/bottom*/
    else if(stream)
    {
        var nUpdate=30;

        if(data.msg.nUpdate !== undefined && typeof data.msg.nUpdate=="number")
            nUpdate=data.msg.nUpdate;

        if(nUpdate>maxUpdate) nUpdate=maxUpdate;

        if(nUpdate) {
            var msgRange, backlog;
            // appending starting at a certain message
            if(data.msg.lastId)
            {
                nUpdate++; //we are going to exclude the lastId message below
                msgRange = req.rcl.xrange(stream, data.msg.lastId, '+', "COUNT", nUpdate);
                msgRange.shift();//exclude lastId;
                backlog = { stream: stream, data: msgRange , isHistory: true, append:true};
            }
            // prepending (if(data.msg.firstId)) OR getting most recent messages
            else
            {
                /* get some history (most recent xx messages) and send to client*/
                var firstId='+'; //by default, start with latest message

                if(data.msg.firstId)
                {
                    firstId=data.msg.firstId;
                    nUpdate++; //we are going to exclude the firstId message below
                }

                // query redis for most recent messages backwards from firstId to nUpdate previous messages
                msgRange = req.rcl.xrevrange(stream, firstId, '-', "COUNT", nUpdate);

                if(!msgRange || !msgRange.length) 
                    return;

                backlog = { stream: stream, data: msgRange.reverse(), isHistory: true}

                /* if getting previous messages by scrolling up */
                if(data.msg.firstId)
                {
                    backlog.data.pop();//exclude firstId
                    backlog.prepend=true;
                }
                //for non-scrollup, update last seen message to the last one in this reply
                else
                    req.userprefs[stream+'_last'] = backlog.data[backlog.data.length-1].id;

                /* send up to last xx messages.  Format is the same as the sample message
                   above, except that backlog.data.length may be >  1                      */
            }
            req.wsSend(backlog);
        }
        //TODO: check if user is allowed to view this stream (same org, and in pref list)
        req.curStream=stream;
        
        //save the last monitored channel for next time we connect.
        req.userprefs.lastchan = stream;
    }

    //This is the PUB. Add incoming message to the stream
    else if (req.curStream)
    {
        if(!data.msg || data.msg.length==0)
            return;// do nothing if got nothing
                                                    //don't include uninvited data
        var ret = req.rcl.xadd(req.curStream, "*", {msg:data.msg, from:data.from, stream:data.stream});
    }
}


/* save a file sent over websocket to appropriate directory */

var datadir = process.scriptPath + "/data/quack_redis_chat/";

function savefile(req, file) {
    var path, rfname, fname, name, ext;

    /* sanity check for file.stream goes here.  make sure it is in the proper format.
     * and the user has permission to the provided channel/stream 
     * right now, we will only accept file.stream if it matches req.curStream         
     * However, having file.stream gives us the flexibility to later implement the ability to
     * upload a file to a stream which isn't the currently selected one                       */
    if(!file.stream || file.stream!=req.curStream)
        return;

    path = datadir + file.stream + '/';
    //file.name is also provided from client. Do a more robust sanity check here
    if(/^\./.test(file.name) || /\//.test(file.name) )
        return;

    fname = path + file.name;
    rfname = file.stream + '/' + file.name;
    //check for path
    if(!stat(path))
        mkdir(path);

    /* save current file as file_1.ext if file.ext exists */
    if(stat(fname)) {
        var i=1, name, ext="", parts=file.name.split('.');
        if(parts.length==1) {
            name=fname;
        } else {
            name = parts.slice(0,parts.length-1).join('.');
            ext = parts.pop();
        }
        do {
            rfname = sprintf('%s/%s_%d%s%s', file.stream, name, i, (ext==""?"":'.'), ext);
            fname = datadir + rfname;
            i++;
            if(i>500) return; // should we have a limit on this?
        } while(stat(fname))
    }

    fprintf(fname, '%s', file.content);    
    return rfname;
}




/* This function is run for every incoming websocket message.
 * The first run will have an empty body.
 * The req object is reused with req.body updated for each incoming message.
 * Sending data back is done with req.wsSend.
 * Any data printed or put using req.printf/req.put will also be sent when
 *   req.wsSend() is called.
 * req.count == number of times function has been called since connection was made.
 * req.wsOnDisconnect is a function that is run when you disconnect or you are disconnected by the client.
 * req.wsEnd forces a disconnect (but runs the disconnect callback first);
 * req.websocketId is a unique number to identify the current connection.
 * req.wsIsBin is true if the client send binary data.
 */
module.exports = function (req)
{
    /* first run, req.body is empty. The req object is recycled, 
       so req.userId, req.rcl and req.userprefs will be continually
       available after being set during initial connection (req.count==0)   */

    if (!req.count) {
        var status;

        /* we need a separate redis connection per client */
        req.rcl=new redis.init(23741);

        /* authenticate user here */
        req.userId=authenticate(req);

        /* org stream/channel for temp messages */
        req.orgStream = req.params.orgid;

        /* userstat hset key name */
        req.userStatsKey = req.orgStream + "_userstats";

        /* we need to close it when we disconnect */
        req.wsOnDisconnect(function(){
            console.log("We are closing redis connection");
            req.rcl.hset(req.userStatsKey, req.userId, "offline");
            req.rcl.xadd(req.orgStream, "MAXLEN", '~', '20', "*", {msg:{status:"offline"}, from:req.userId, stream:""});
            req.rcl.close();
        });

        /* keep a handy reference to the user's preference */
        req.userprefs = new req.rcl.proxyObj(`${req.userId}_prefs`);

        /* check the user's last status */
        status = req.rcl.hget(req.userStatsKey, req.userId);        
        if(status!="sleep")
        {
            req.rcl.hset(req.userStatsKey, req.userId, "online");
            status="online";
        }
        /* notify everyone in organization of user's new status */
        req.rcl.xadd(req.orgStream, "MAXLEN", '~', '20', "*", {msg:{status:status}, from:req.userId, stream:""});

        /* either "req.wsSend" or "return" can be used and both send the same data */
        /* note that return frow websockets is a different format than http.  Messages
           are strings. Objects are serialized as JSON.  There are no headers or content type */
        if(req.userprefs.lastchan)
            return {from: "System", msg: 'connected', lastchan: req.userprefs.lastchan, status:status};
        else
            return {from: "System", msg: 'connected', status:status};
    }

    /* second and subsequent run.  Client has sent a message
       and we need to process and forward it to others who are
       listening via rampart.event.on above                      */

    /* Simple Messages
       To avoid having to convert every incoming message to a string and test
       whether it is JSON, all simple messages are 4 chars long       */
    if(req.body.length == 4)
    {
        var msg = sprintf("%s",req.body);
        if(msg == "ping")
            return "pong";
        else if (msg=="slep")
        {
            req.rcl.xadd(req.orgStream, "MAXLEN", '~', '20', "*", {msg:{status:"sleep"}, from:req.userId, stream:""});
            req.rcl.hset(req.userStatsKey, req.userId, "sleep");
            return;
        }
        else if (msg=="onln")
        {
            req.rcl.xadd(req.orgStream, "MAXLEN", '~', '20', "*", {msg:{status:"online"}, from:req.userId, stream:""});
            req.rcl.hset(req.userStatsKey, req.userId, "online");
            return;
        }
    }

    /* FYI: Most of the code below is for handling binary attachments, which
            has not yet been implemented, but is taken from the webchat example.  
            The real action happens in handle_message() above                    */

    /* we are sent a file from the client in two parts: 
       1) JSON meta info,
       2) binary data                                     */
    var fileInfo=false, data=false;
    if(!req.wsIsBin && req.body.length)
    {
        /* messages are just plain text, but if sending a file, fileInfo
           will be set, and next round client should send the binary data
           and req.wsIsBin will be true */
        try{
            data=JSON.parse(req.body);
            if(data.msg.file)
            {
                fileInfo=data.msg;
                fileInfo.file.stream=data.stream;
            }
        } catch(e) {
            return {from:"System", error: 'error parsing websocket message'}
        }
    }

    /* if it is binary data, we assume it is a file
       and the file info was already sent            */
    var file;
    if(req.wsIsBin)
    {
        if(req.files && req.files.length)
        {
            //get the first entry of file meta info
            file = req.files.shift();
            // add the body buffer to it
            file.content = req.body;
        }
        else //TODO: make a unique name for this nameless file.  BTW: this shouldn't happen.
            file = {name:"file.bin", type:"application/octet-stream", content:req.body};
    }
    else
        req.body = sprintf("%s",req.body);//It's not binary, convert body to a string

    if(fileInfo && fileInfo.file)
    {
        if(!req.files)
            req.files = [];
        /* store file meta info in req where we will retrieve it next time */
        req.files.push(fileInfo.file);
        // return undefined (or null) and get the actual file contents in the next message
    }
    else if (file)
    {
        var imagemark="";

        // display images with image markup
        if(/^image\//.test(file.type))
            imagemark="!";

        if(!req.curStream)
            return {from:"System", error: 'cannot save file with no channel selected'}
        /* we received a file, process it*/
        var fname = savefile(req, file);
        if(!fname)
            return {from:"System", error: 'error saving file'}

        var msg = { 
            message: `${imagemark}[${file.name}](/apps/quack_redis_chat/quackerjax.json?filename=${%U:fname})`,
            who: req.userName, 
            when: new Date().getTime()
        }
        req.rcl.xadd(req.curStream, "*", {msg:msg, from: req.userId, stream:req.curStream});
    }
    else if(data)
    {
        //parse message here
//        console.log(data);
        handle_message(req, data);
    }

    return null;
}
