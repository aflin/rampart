var Ramis=require("rpramis");

/* babel flat polyfill */
if (!Array.prototype.flat) {
    Object.defineProperty(Array.prototype, 'flat', {
        configurable: true,
        value: function flat () {
            var depth = isNaN(arguments[0]) ? 1 : Number(arguments[0]);

            return depth ? Array.prototype.reduce.call(this, function (acc, cur) {
                if (Array.isArray(cur)) {
                    acc.push.apply(acc, flat.call(cur, depth - 1));
                } else {
                    acc.push(cur);
                }

                return acc;
            }, []) : Array.prototype.slice.call(this);
        },
        writable: true
    });
}

var ret={
    ramis: Ramis,
    createClient: function(a,b){
        var ip="127.0.0.1";
        var cret;
        var self=this;

        if(a==undefined)
            cret = new Ramis.init();
        else if (typeof a == "object" && typeof a.port== "number" )
        {
            if(typeof a.ip=="string")
                ip=a.ip;
            cret = new Ramis.init(ip,port);
        }
        else if (typeof a == "number")
        {
            if (typeof b == "string")
                ip=b;
            cret = new Ramis.init(ip,a);
        }
        this.client=cret;

        var handler =
        {
            get: function (targ, key) {
                /* reserve all _keys */
                if (key[0] == '_')
                    return targ[key];
                if(!targ._client) throw("error: ramis.ramvar(): container object has been destroyed");
                var enc=targ._client.exec("HGET %s %s", targ._hname, key);
                if (!enc[0]) {
                    delete targ[key]; //keep in sync with redis/ramis
                    return undefined;
                }
                return CBOR.decode(rampart.utils.stringToBuffer(enc[0]));
            },
            
            set: function (targ, key, val, recv) {
                if(!targ._client) throw("error: ramis.ramvar(): container object has been destroyed");
                if(key[0] == '_')
                {
                    targ[key]=val;
                    return;
                }
                var enc = CBOR.encode(val);
                var dec = CBOR.decode(enc)
                targ._client.exec("HSET %s %s %b", targ._hname, key, enc, enc.byteLength);
                /* ownKeys return cannot be arbitrary.  Key must actually exist in target */
                targ[key]=null;
            },

            deleteProperty: function (targ, key) {
                delete targ[key];
                targ._client.exec("HDEL %s %s", targ._hname, key);
            },

            ownKeys: function (targ) {
                // https://github.com/svaarala/duktape/issues/2153
                // delete keys in target, re-enumerate from redis/ramis and recreate keys
                // only keys that exist in target can be returned
                var i=-0, ikeys=Object.keys(targ)
                for ( i=0; i<ikeys.length; i++ )
                {
                    var k=ikeys[i];
                    if(k[0] != '_')
                        delete targ[k];
                }

                //ret = [ [key1,key2,...] ]
                var ret=targ._client.exec("HKEYS %s", targ._hname);
                if (ret && ret.length)
                {
                    ret=ret[0];
                    for ( i=0; i<ret.length; i++ )
                        targ[ret[i]]=null;

                    return ret;
                }
                //console.log("in ownkeys:",ret[0]);
                return [];
            },

            //unused:
            enumerate: function (targ) {
                var ret=targ._client.exec("HKEYS %s", targ._hname);
                //console.log("in enumerate:",ret[0]);
                return ret[0];
            }
        }


        ret.createClient.prototype.ramvar=function(hname)
        {
            this._client=self.client;
            this._hname=hname;
            this._destroy=(
              function (self)
              { 
                return function ()
                {
                    self._client.exec("DEL %s", self._hname);
                    var k=Object.keys(self);
                    for (var i=0; i<k.length; i++)
                        delete self[k[i]];
                }    
              }
            )(this);
            var proxy = new Proxy(this, handler);
            return proxy;
        }

    }/* createClient */
}

function dofunc(fmt,args)
{
    var cb=[];

    for (var i=0; i < args.length; i++)
    {
        var arg=args[i];
        var atype=typeof arg;

        if( atype == 'object' && arg.byteLength != undefined )
        {
            fmt+=" %b";
            args.splice(i,0,args.byteLength);
        }
        else
        {
            switch(atype)
            {
                case "number":   fmt += " %f"; break;
                case "string":   fmt += " %s"; break;
                case "function": cb.push(args[i]);   break;
                default:         fmt += " %s"; args[i]=JSON.stringify(args[i]); break;
            }
        }
    }

    for (var i=0; i < args.length; i++)
    {
        if ( typeof args[i] == 'function')
            args.splice(i--,1);
    }    

    args.unshift(fmt);

    var fret=this.client.exec.apply(this.client,args);
    if(cb.length)
    {
        for( i=0; i<cb.length; i++)
            cb[i](fret);
    }
    else
        return fret;
}

function setfunc(name) {
    ret.createClient.prototype[name]=function()
    {
        // turn {"0":"a","1":["b","c"]} into ["a","b","c"]
        var args=Array.prototype.slice.call(arguments).flat();
        var fmt=name.toUpperCase().replace(/_/g, " ");
        return dofunc.call(this,fmt,args);
    }
}

var fnames=["acl_load", "acl_save", "acl_list", "acl_users", "acl_getuser", "acl_setuser", "acl_deluser", "acl_cat", "acl_genpass", 
    "acl_whoami", "acl_log", "acl_help", "append", "auth", "bgrewriteaof", "bgsave", "bitcount", "bitfield", "bitop", "bitpos", "blpop", 
    "brpop", "brpoplpush", "bzpopmin", "bzpopmax", "client_caching", "client_id", "client_kill", "client_list", "client_getname", "client_getredir", 
    "client_pause", "client_reply", "client_setname", "client_tracking", "client_unblock", "cluster_addslots", "cluster_bumpepoch", 
    "cluster_count-failure-reports", "cluster_countkeysinslot", "cluster_delslots", "cluster_failover", "cluster_flushslots", "cluster_forget", 
    "cluster_getkeysinslot", "cluster_info", "cluster_keyslot", "cluster_meet", "cluster_myid", "cluster_nodes", "cluster_replicate", "cluster_reset", 
    "cluster_saveconfig", "cluster_set-config-epoch", "cluster_setslot", "cluster_slaves", "cluster_replicas", "cluster_slots", "command", "command_count", 
    "command_getkeys", "command_info", "config_get", "config_rewrite", "config_set", "config_resetstat", "dbsize", "debug_object", "debug_segfault", "decr", 
    "decrby", "del", "discard", "dump", "echo", "eval", "evalsha", "exec", "exists", "expire", "expireat", "flushall", "flushdb", "geoadd", "geohash", "geopos", 
    "geodist", "georadius", "georadiusbymember", "get", "getbit", "getrange", "getset", "hdel", "hello", "hexists", "hget", "hgetall", "hincrby", "hincrbyfloat", 
    "hkeys", "hlen", "hmget", "hmset", "hset", "hsetnx", "hstrlen", "hvals", "incr", "incrby", "incrbyfloat", "info", "lolwut", "keys", "lastsave", "lindex", 
    "linsert", "llen", "lpop", "lpos", "lpush", "lpushx", "lrange", "lrem", "lset", "ltrim", "memory_doctor", "memory_help", "memory_malloc-stats", "memory_purge", 
    "memory_stats", "memory_usage", "mget", "migrate", "module_list", "module_load", "module_unload", "monitor", "move", "mset", "msetnx", "multi", "object", 
    "persist", "pexpire", "pexpireat", "pfadd", "pfcount", "pfmerge", "ping", "psetex", "psubscribe", "pubsub", "pttl", "publish", "punsubscribe", "quit", 
    "randomkey", "readonly", "readwrite", "rename", "renamenx", "restore", "role", "rpop", "rpoplpush", "rpush", "rpushx", "sadd", "save", "scard", "script_debug", 
    "script_exists", "script_flush", "script_kill", "script_load", "sdiff", "sdiffstore", "select", "set", "setbit", "setex", "setnx", "setrange", "shutdown", 
    "sinter", "sinterstore", "sismember", "slaveof", "replicaof", "slowlog", "smembers", "smove", "sort", "spop", "srandmember", "srem", "stralgo", "strlen", 
    "subscribe", "sunion", "sunionstore", "swapdb", "sync", "psync", "time", "touch", "ttl", "type", "unsubscribe", "unlink", "unwatch", "wait", "watch", "zadd", 
    "zcard", "zcount", "zincrby", "zinterstore", "zlexcount", "zpopmax", "zpopmin", "zrange", "zrangebylex", "zrevrangebylex", "zrangebyscore", "zrank", "zrem", 
    "zremrangebylex", "zremrangebyrank", "zremrangebyscore", "zrevrange", "zrevrangebyscore", "zrevrank", "zscore", "zunionstore", "scan", "sscan", "hscan", 
    "zscan", "xinfo", "xadd", "xtrim", "xdel", "xrange", "xrevrange", "xlen", "xread", "xgroup", "xreadgroup", "xack", "xclaim", "xpending", "latency_doctor", 
    "latency_graph", "latency_history", "latency_latest", "latency_reset", "latency_help"];

for (var i=0;i<fnames.length;i++)
    setfunc(fnames[i]);

module.exports=ret;
