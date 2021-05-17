var redis=require("rampart-redis");
rampart.globalize(rampart.utils);

var rcl=new redis.init(23741);

/* subscribing to a user:user dm "channel" requires concatenation the
   ids to create the name of the dm pseudo-channel                      */
function dmname(id,user) {
    return [id,user].sort().join(":");
}

function getMsgCnt(id, userprefs){
    var lastprop = id + '_last';
    var lastid=userprefs[lastprop];
    var ret;

    if(lastid)
    {
        ret=rcl.xrange(id, lastid, '+', 'count', 100);
        if(ret.length)
            ret = ret.length -1;
        else
            ret=0;
    }
    else
    {
        ret=rcl.xrange(id, '-', '+', 'count', 99);
        if(ret)
            ret=ret.length;
        else
            ret=0;
    }

    return ret;
}

// put authentication here and return id.
// return false if auth fails
function authenticate(req,husers)
{
    if(req.params.user)
    {
        var keys = Object.keys(husers);
        for (var i=0;i<keys.length;i++){
//console.log(husers[keys[i]].name, req.params.user);
            if(husers[keys[i]].name == req.params.user)
                return keys[i];
        }
    }
    return "60735c525"; //aaron
}


var datadir = process.scriptPath + "/data/quack_redis_chat/";

function sendfile(req) {
    // TODO: check for nasty stuff in req.filename here
    //       and check that user has permission to access files in this directory
    var paths = req.params.filename.split("/");
    if(paths.length != 2)
        return {status:404};
    if(
        /^\./.test(paths[0]) || /\//.test(paths[0]) || 
        /^\./.test(paths[1]) || /\//.test(paths[1]) 
    )
        return {status:404};

    /* @/path/filename.ext means serve content directly from disk */
    var filepath = '@' + datadir + req.params.filename;
    var ext = req.params.filename.split('.').pop();
    // req.getMime - look up mimetype by extension
    if (!ext || !req.getMime(ext))
        ext = "bin"; //set to bin->appication/octet-stream if none found

    var ret={};
    ret[ext]=filepath;
    return ret;   
}

module.exports = function (req)
{
    /* send a file here, proper auth needs to be done before this*/
    if (req.params.filename)
        return sendfile(req);

    var orgname = req.params.orgname;
    if(!orgname)
        return {status: 404, json: {error: "no organization name provided"} }

    var id = rcl.hget("org_by_name", orgname);
    if(!id)
        return {status: 404, json: {error: "organization not found"} }

    var users = [], chans=[];
    var husers = new rcl.proxyObj(`${id}_users`);
    var hchans = new rcl.proxyObj(`${id}_channels`);

    var userId=authenticate(req,husers);
    if(!userId)
        return {status: 404, json: {error: "user not found or failed to authenticate"} }


    var userprefs = new rcl.proxyObj(`${userId}_prefs`);

//console.log(id,userId,userprefs);

    if(!userprefs.dms || !userprefs.chans)
        return {status: 404, json: {error: "user preferences not found"} }

//    var keys = Object.keys(husers);
    var keys = userprefs.dms;
    var orgUserStats = rcl.hgetall(id + "_userstats");
    var curUserStat =  orgUserStats[userId] == "sleep" ? "sleep": "online";

    //push current user
    users.push({user_id: userId, username: husers[userId].name, status:curUserStat, unread: 0});
    //push dm users
    for (var i=0;i<keys.length;i++){
        var key=keys[i]
        var stat = orgUserStats[key] ? orgUserStats[key] : "offline";
        if(husers[key].name)
            users.push({user_id: key, username: husers[key].name, status:stat, unread:getMsgCnt(dmname(key, userId), userprefs)});
    }


//    keys = Object.keys(hchans);
    keys = userprefs.chans;
    for (var i=0;i<keys.length;i++){
        var key=keys[i]
        if(hchans[key].name)
            chans.push({channel_id:key,channel_name:hchans[key].name,unread:getMsgCnt(key, userprefs)});
    }

    return {json: {orgid:id, users:users, channels:chans, id:userId} };
}
