
function init()
{
    var redis=require("rampart-redis");
    rampart.globalize(rampart.utils);
    var rcl;

    /***** Test if Redis is already running *****/
    try {
        rcl=redis.init(23741);
        printf("redis is already running on port 23741\n");
        return
    } catch(e){}
    
    /***** LAUNCH REDIS *********/
    var ret = shell("which redis-server");

    if (ret.exitStatus != 0) {
        fprintf(stderr, "Could not find redis-server\n");
        return;
    }

    var rdexec = trim(ret.stdout);

    ret = exec("nohup", rdexec, "--port", 23741, {background: true});

    var rpid = ret.pid;

    sleep(0.5);

    if (!kill(rpid, 0)) {
        fprintf(stderr, "Failed to start redis-server\n");
        process.exit(1);
    }

    sleep(0.5);

    rcl=new redis.init(23741);

    //TEMPORARY, REMOVE ME:
    //rcl.flushall();

    /****** SETUP TEST ENVIRONMENT *******/

    function makeorg(name, id) {
        //TODO: check that name is not taken
        rcl.hset("org_by_id", id, name);
        rcl.hset("org_by_name", name, id);
    }


    /* 
    userprefs = {
        dms: [list,of,dm,users], 
        chans:[list,of,sub,channels] 
    }
    */ 
    function create_user_prefs(curuser, users, chans)
    {
        var userprefs = new rcl.proxyObj(curuser+'_prefs');
        var userlist=[], utmp=Object.keys(users);
        var channellist=Object.keys(chans);

        for(var i=0;i<utmp.length;i++)
        {
            var u=utmp[i];
            if(u!=curuser)
                userlist.push(u);
        }
        userprefs.dms=userlist;
        userprefs.chans=channellist;
    }

    function load_test_data(){
        var user_list=[
            {user_id:"60735c522",username:"Cube"},
            {user_id:"60735c525",username:"Aaron"},
            {user_id:"60735f525",username:"Ben"},
            {user_id:"60735f527",username:"Rhys"},
            {user_id:"60735f529",username:"Samuel L Jackson"},
            {user_id:"60735f530",username:"John Travolta"}
        ];

        var channel_list=[
            {channel_id:"60715c522",channel_name:"Rampart"},
            {channel_id:"60715c523",channel_name:"Redis-module"},
            {channel_id:"60715c524",channel_name:"SQL-module"},
            {channel_id:"60715c525",channel_name:"LMDB-module"},
            {channel_id:"60715c526",channel_name:"Crypto-module"},
            {channel_id:"60715c527",channel_name:"Tarantino"}
        ];
     
        var organization = "Quack, Inc";
        var org_id = "60715c529"
        makeorg(organization, org_id);
        var user_hset = `${org_id}_users`;
        var channel_hset = `${org_id}_channels`;

        var userobj = new rcl.proxyObj(user_hset);
        var chanobj = new rcl.proxyObj(channel_hset); 

        for (var i=0; i<user_list.length; i++)
            userobj[user_list[i].user_id] = { name: user_list[i].username };

        for (i=0; i<channel_list.length; i++)
            chanobj[channel_list[i].channel_id] = { name: channel_list[i].channel_name };

        //make user preferences, which right now include
        // direct-message-users list and channels list 
        for (var i=0; i<user_list.length; i++)
        {
            create_user_prefs( user_list[i].user_id, userobj, chanobj);
        }
    }

    load_test_data();
    printf("started\n");
}

if(module && module.exports)
    module.exports = init;
else
    init();

