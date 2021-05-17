The rampart-redis module
==============================

Preface
-------

License
~~~~~~~

The rampart-redis module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-redis module provides a client interface from Rampart to an
existing Redis server.


How does it work?
~~~~~~~~~~~~~~~~~

The rampart-redis module connects to an existing Redis server and allows
commands to be sent, and responses to be retrieved from Redis.  For most
operations, commands and replies are syncronous, meaning that the module
waits for a return value from Redis and provides it to the script before
moving on.  However, any command may be optionally run asyncronously. If so,
the command will be sent immediately to the server with the reply from the
server handled in Rampart's event loop.

Note: Unlike Redis modules in other frameworks (e.g., Node), a separate
connection is maintained for syncronous and asyncronous requests from a
single handle.  However, only one asyncronous request can be active at a
time per handle.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var redis = require("rampart-redis");



Main Function
-------------

The rampart-redis module exports a single constructor function `init()`_
which may be used to establish a connection with a redis server.

init()
~~~~~~~~~~~

    Usage:

    .. code-block:: javascript

        var redis = require("rampart-redis");

        var rcl = new redis.init([ip, ][port, ][timeout]);

        /* or */

        var rcl = new redis.init(options);

    Where:

    * ``options`` is an :green:`Object` with the optional properties
      ``ip``, ``port`` and/or ``timeout`` as described below.

    * ``ip`` is a :green:`String`, the ip address of the Redis server.  The
      default if not specified is ``127.0.0.1``.

    * ``port`` is a :green:`Number`, the port number of the Redis server.  The
      default if not specified is ``6379``.

    * ``timeout`` is a :green:`Number`, the amount of time to wait for redis to
      reply to a syncronous request for data in seconds.  The default is ``-1``,
      which means to wait forever.
        
    Return Value:
        A :green:`Object` - the handle representing the connection to the Redis
        server as well as containing the functions/redis commands listed below.

Client Functions
----------------

The majority of the client functions provided by the ``rampart-redis``
module are one-to-one mappings of the `Published Redis Commands <https://redis.io/commands>`_\ .
There are also specialized and shortcut commands available.

Client Commands
~~~~~~~~~~~~~~~

    The set of standard client commands includes `Supported Commands`_ and `Unsupported Commands`_
    as listed below.

    Usage:

    .. code-block:: javascript

        var redis = require("rampart-redis");

        var rcl = new redis.init([ip, ][port, ][timeout]);

        rcl.command_name([options ][, param1][, param2][, ...][, callback_function]); 

    Where:

        *  ``command_name`` is the name of one of the functions listed below.

        *  ``paramX`` is one or more parameters, required or optional for the given Redis command.

        *  ``callback_function`` is an optional (if synchronous) or required (if
           asynchronous) function which takes a single parameter.
        
        *  ``options`` is an optional :green:`Object` with the following optional
           properties:

            * ``timeout`` - the :green:`Number` of seconds to wait for a response from the redis
              server when using a syncronous command. It overrides the timeout set
              in ``redis.init`` for the single command.

            * ``async`` - a :green:`Boolean`, which if ``true``, the client will
              send the command to server and wait for reply in the
              Rampart event loop.  If ``true``, a callback function must be provided.

            * ``returnBuffer`` - a :green:`Boolean`, which if true,  Redis
              ``strings`` and ``bulk strings`` will be returned in :green:`Buffers`.

    Return Value:
        See below.

    Note: 
        All commands are by default syncronous commands, except
        for shortcut commands which end in ``*_async`` and for the
        ``subscribe``/``psubscribe`` commands.

        Only one ``async`` command may be active at any given time. 
        Additional concurrent asynchronous requests require that multiple
        handles be opened.
        
        The ``rcl`` handle opens separate connections for synchronous and
        asynchronous commands.

Shortcut Commands
-----------------

The following commands do not map one-to-one to the 
`Published Redis Commands <https://redis.io/commands>`_\ .

_async Commands
~~~~~~~~~~~~~~~

    ``blmove_async``, ``brpop_async``, ``bzpopmax_async``,
    ``bzpopmin_async`` and ``format_async`` are equivalent to
    their non-async counterparts with the ``options`` :green:`Object` set to
    ``{async:true}``.

format
~~~~~~

    Send a formatted command.  Return value is not specially reformatted
    (returns the same as format ``2`` below).
    
    Usage:
    
    .. code-block:: javascript

        var redis = require("rampart-redis");

        var rcl = new redis.init([ip, ][port, ][timeout]);

        rcl.format([options,] format_string [, param2][, ...][, callback_function]); 

    Where:

        * ``options`` is the same as above.

        * ``format_string`` is a :green:`String` with format codes
          corresponding to the parameters that follow (much in the same way
          as :ref:`rampart-utils:printf`).  However only a limited number
          of format codes are available:

            * ``%s`` - the corresponding parameter is and will be send as a
              :green:`String`

            * ``%c`` - the corresponding parameter is a :green:`String`, the
              first character of which will be send as a single character.

            * ``%i`` - the corresponding parameter is a :green:`Number` and
              will be converted and sent as an integer.

            * ``%d``  the corresponding parameter is a :green:`Number` and
              will be sent as a double float.

            * ``%b``  the corresponding parameter is a :green:`Buffer` or a
              :green:`String` and will be sent as buffer data.


    Example:

        .. code-block:: javascript

            var redis = require("rampart-redis");

            var rcl = new redis.init(); //localhost and default port

            rcl.format("HMSET %s %s %d %s %d", "myset", "one", 1, "two", 2);
            
            rampart.utils.printf("%3J\n", rcl.format("HGETALL myset") );

            /* expected output (generic array)
                [  
                   "one",
                   1,
                   "two",
                   2
                ]
            */

            rampart.utils.printf("%3J\n", rcl.hgetall("myset"));

            /* expected output (formatted for the HGETALL command)
                {  
                   "one": 1,
                   "two": 2
                }
            */
 
xread_block_async
~~~~~~~~~~~~~~~~~

    This is equivalent to ``xread({async:true}, "BLOCK", ...)``. 

xread_auto_async
~~~~~~~~~~~~~~~~

    This is equivalent to ``xread_block_async`` above, except that the
    returned IDs are tracked and the ``XREAD BLOCK`` command is reissued
    with the proper IDs each time a new Stream Message is received.  In this
    way it acts more like ``subscribe`` and continually accepts new streamed
    messages similar to a PUB/SUB model.

Supported Commands
------------------

The following commands have been tested and return values formatted
appropriately for the given command (number in parantheses is the format of
the :ref:`return value <rampart-redis:Return Values from Supported Commands>`):

    ``bitcount``\ →(1), ``bitfield``\ →(2), ``bitop``\ →(1), ``bitpos``\ →(1),
    ``blmove``\ →(1), ``blmove_async``\ →(1), ``blpop``\ →(13),
    ``blpop_async``\ →(13), ``brpop``\ →(13), ``brpop_async``\ →(13),
    ``brpoplpush``\ →(1), ``bzpopmax``\ →(14), ``bzpopmax_async``\ →(14),
    ``bzpopmin``\ →(14), ``bzpopmin_async``\ →(14), ``decr``\ →(1),
    ``decrby``\ →(1), ``del``\ →(1), ``discard``\ →(1), ``dump``\ →(1),
    ``echo``\ →(1), ``exists``\ →(6), ``expire``\ →(1), ``expireat``\ →(1),
    ``flushall``\ →(1), ``flushdb``\ →(1), ``format``\ →(2), ``format_async``\ →(2),
    ``get``\ →(1), ``getbit``\ →(1), ``getdel``\ →(1), ``getex``\ →(1),
    ``getrange``\ →(1), ``getset``\ →(1), ``hdel``\ →(1), ``hexists``\ →(6),
    ``hget``\ →(1), ``hgetall``\ →(4), ``hincrby``\ →(1), ``hincrbyfloat``\ →(1),
    ``hkeys``\ →(2), ``hlen``\ →(1), ``hmget``\ →(2), ``hmset``\ →(1),
    ``hrandfield``\ →(2), ``hscan``\ →(7), ``hset``\ →(1), ``hsetnx``\ →(6),
    ``hstrlen``\ →(1), ``hvals``\ →(2), ``incr``\ →(1), ``incrby``\ →(1),
    ``incrbyfloat``\ →(1), ``info``\ →(2), ``keys``\ →(2), ``lindex``\ →(1),
    ``linsert``\ →(1), ``llen``\ →(1), ``lmove``\ →(1), ``lpop``\ →(2),
    ``lpos``\ →(1), ``lpush``\ →(1), ``lpushx``\ →(1), ``lrange``\ →(2),
    ``lrem``\ →(1), ``lset``\ →(1), ``ltrim``\ →(1), ``mget``\ →(2), ``move``\ →(1),
    ``mset``\ →(1), ``msetnx``\ →(6), ``object``\ →(1), ``persist``\ →(1),
    ``pexpire``\ →(1), ``pexpireat``\ →(1), ``psetex``\ →(1), ``psubscribe``\ →(1),
    ``pttl``\ →(1), ``publish``\ →(1), ``pubsub``\ →(1), ``punsubscribe``\ →(1),
    ``rpop``\ →(2), ``rpoplpush``\ →(1), ``rpush``\ →(1), ``rpushx``\ →(1),
    ``sadd``\ →(1), ``save``\ →(1), ``scan``\ →(8), ``scard``\ →(1), ``sdiff``\ →(2),
    ``sdiffstore``\ →(1), ``set``\ →(1), ``setbit``\ →(1), ``setex``\ →(1),
    ``setnx``\ →(6), ``setrange``\ →(1), ``sinter``\ →(2), ``sinterstore``\ →(1),
    ``sismember``\ →(6), ``smembers``\ →(2), ``smismember``\ →(9), ``smove``\ →(1),
    ``sort``\ →(2), ``spop``\ →(2), ``srandmember``\ →(2), ``srem``\ →(1),
    ``sscan``\ →(8), ``stralgo``\ →(4), ``strlen``\ →(1), ``subscribe``\ →(3),
    ``sunion``\ →(2), ``sunionstore``\ →(1), ``time``\ →(2), ``touch``\ →(1),
    ``ttl``\ →(1), ``type``\ →(1), ``unlink``\ →(1), ``unsubscribe``\ →(1),
    ``xadd``\ →(1), ``xdel``\ →(1), ``xinfo``\ →(4), ``xlen``\ →(1), ``xrange``\ →(5),
    ``xread``\ →(12), ``xread_auto_async``\ →(12), ``xread_block_async``\ →(12),
    ``xrevrange``\ →(5), ``xtrim``\ →(1), ``zadd``\ →(1), ``zcard``\ →(1),
    ``zcount``\ →(1), ``zdiff``\ →(10), ``zdiffstore``\ →(10), ``zincrby``\ →(1),
    ``zinter``\ →(10), ``zinterstore``\ →(10), ``zlexcount``\ →(1),
    ``zmscore``\ →(2), ``zpopmax``\ →(10), ``zpopmin``\ →(10),
    ``zrandmember``\ →(10), ``zrange``\ →(10), ``zrangebylex``\ →(2),
    ``zrangebyscore``\ →(10), ``zrangestore``\ →(1), ``zrank``\ →(1),
    ``zrem``\ →(1), ``zremrangebylex``\ →(1), ``zremrangebyrank``\ →(1),
    ``zremrangebyscore``\ →(1), ``zrevrange``\ →(10), ``zrevrangebylex``\ →(10),
    ``zrevrangebyscore``\ →(10), ``zrevrank``\ →(1), ``zscan``\ →(11),
    ``zscore``\ →(1), ``zunion``\ →(10), ``zunionstore``\ →(10)

  



Return Values from Supported Commands
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    #. Single Value - return is a :green:`String`, :green:`Buffer`, 
       :green:`Number` or ``null``.

    #. Multi-Value - return is an :green:`Array`.  When used with a callback, 
       the callback will be called once for each member of the
       :green:`Array`.

    #. Multi-Value - return is an :green:`Array` of :green:`Arrays`. When used 
       with a callback, the callback will be called once for each member of
       the outer :green:`Array` and have its sole parameter one of each of the 
       inner :green:`Arrays`.

    #. Associative - return is an :green:`Object` with one or more key:value
       pairs.

    #. Associative - for ``xrange`` and ``xrevrange`` only.  Format is as
       follow:

        .. code-block:: javascript
           
              [
                 {
                    "id": "1620437646452-0",
                    "value": {
                       "key1": "val1",
                       "key2": "val2"
                    }
                 },
                 {
                    "id":"1620437646452-1",
                    "value": {
                       "key3": "val3"
                    }
                 }
              ]

    #. Boolean - return is a :green:`Boolean`.

    #. Associative - for ``hscan`` only.  Format is as follows:

        .. code-block:: javascript
           
              {
                 "cursor": 0,
                 "values": {
                    "key1": 99,
                    "key2": 100,
                    "key3": 101
                 }
              }

        With a callback, the return value is set to cursor and the callback
        function is called for each key:value pair in its own
        :green:`Object`.

    #. Multi-Value - for ``scan`` and ``sscan`` only.  
       Format is as follows:

        .. code-block:: javascript
           
              {
                 "cursor": 0,
                 "values": [
                    "one",
                    "three",
                    "four",
                    "two"
                 ]
              }

        With a callback, the return value is set to cursor and the callback
        function is called for each value.

    #. Boolean - for ``sismember`` only.  An :green:`Array` of
       :green:`Booleans`.

    #. Multi-Value - for ``zset`` commands which specify ``WITHSCORES``. 
       Values are returned in an object along with the score.  With a
       callback, a single :green:`Object` with value and score passed to the 
       function.  Example:

        .. code-block:: javascript
           
              [  
                 {  
                    "value": "b",
                    "score": 0
                 },
                 {  
                    "value": "c",
                    "score": 1
                 },
              ]

       If ``WITHSCORES`` is not specified, the format is the same as ``2``
       above.

    #. Multi-Value - for ``zscan`` only.  Combination of ``10`` and ``8``
       above. Example:

        .. code-block:: javascript
           
              {
                 "cursor": 0,
                 "values": [
                    {
                       "value": "b",
                       "score": 0
                    },
                    {
                       "value": "c",
                       "score": 1
                    }
                 ]
              }

    #. Associative - for ``xread`` commands only.  Format is as follows:

        .. code-block:: javascript
           
              [
                 {
                    "stream": "mystream1",
                    "data": [
                       {
                          "id": "1620441129127-1",
                          "value": {
                             "key1": "val1",
                             "key2": "val2"
                          }
                       },
                       {
                          "id": "1620441129127-2",
                          "value": {
                             "key3": "val3"
                          }
                       }
                    ]
                 },
                 {
                    "stream": "mystream2",
                    "data": [
                       {
                          "id": "1620441129127-0",
                          "value": {
                             "key4": "val4",
                             "key5": "val5"
                          }
                       }
                    ]
                 }
              ]

       With a callback function, one member of the outer :green:`Array` is
       passed for each iteration of the callback.

    #. Single Value - returns a value along with the Redis key name in an
       :green:`Object` (e.g. ``{key:"mylist",value:"three"}``).

    #. Single Value - for blocking ``zset`` commands. Same as ``13`` above 
       with the addition of a score (e.g. ``{key:"mylist",value:"three",score:1}``). 
       
Unsupported Commands
--------------------

    The following functions will send the appropriate commands.  However
    they have not (yet) been tested for the format of their return value:

    ``acl_cat``, ``acl_deluser``, ``acl_genpass``, ``acl_getuser``,
    ``acl_help``, ``acl_list``, ``acl_load``, ``acl_log``, ``acl_save``,
    ``acl_setuser``, ``acl_users``, ``acl_whoami``, ``append``, ``auth``,
    ``bgrewriteaof``, ``bgsave``, ``client_caching``, ``client_getname``,
    ``client_getredir``, ``client_id``, ``client_kill``, ``client_list``,
    ``client_pause``, ``client_reply``, ``client_setname``,
    ``client_tracking``, ``client_unblock``, ``cluster_addslots``,
    ``cluster_bumpepoch``, ``cluster_count-failure-reports``,
    ``cluster_countkeysinslot``, ``cluster_delslots``, ``cluster_failover``,
    ``cluster_flushslots``, ``cluster_forget``, ``cluster_getkeysinslot``,
    ``cluster_info``, ``cluster_keyslot``, ``cluster_meet``,
    ``cluster_myid``, ``cluster_nodes``, ``cluster_replicas``,
    ``cluster_replicate``, ``cluster_reset``, ``cluster_saveconfig``,
    ``cluster_set-config-epoch``, ``cluster_setslot``, ``cluster_slaves``,
    ``cluster_slots``, ``command``, ``command_count``, ``command_getkeys``,
    ``command_info``, ``config_get``, ``config_resetstat``,
    ``config_rewrite``, ``config_set``, ``dbsize``, ``debug_object``,
    ``debug_segfault``, ``eval``, ``evalsha``, ``exec``, ``geoadd``,
    ``geodist``, ``geohash``, ``geopos``, ``georadius``,
    ``georadiusbymember``, ``hello``, ``lastsave``, ``latency_doctor``,
    ``latency_graph``, ``latency_help``, ``latency_history``,
    ``latency_latest``, ``latency_reset``, ``lolwut``, ``memory_doctor``,
    ``memory_help``, ``memory_malloc-stats``, ``memory_purge``,
    ``memory_stats``, ``memory_usage``, ``migrate``, ``module_list``,
    ``module_load``, ``module_unload``, ``monitor``, ``multi``, ``pfadd``,
    ``pfcount``, ``pfmerge``, ``ping``, ``psync``, ``psync``, ``quit``,
    ``randomkey``, ``readonly``, ``readwrite``, ``rename``, ``renamenx``,
    ``replicaof``, ``reset``, ``restore``, ``role``, ``script_debug``,
    ``script_exists``, ``script_flush``, ``script_kill``, ``script_load``,
    ``select``, ``shutdown``, ``slaveof``, ``slowlog``, ``swapdb``,
    ``swapdb``, ``sync``, ``sync``, ``unwatch``, ``wait``, ``watch``,
    ``xack``, ``xclaim``, ``xgroup``, ``xpending`` and ``xreadgroup``

    If any of the above commands returns values in an unsuitable format, the
    `format`_ command above may be used to get a generic response.

Proxy Objects
-------------

A Rampart Redis Proxy Object is an JavaScript :green:`Object` whose
properties are backed by a `Redis Hash <https://redis.io/topics/data-types-intro#redis-hashes>`_
and for which operations on the :green:`Object` serves as a shortcut for the
``hset``, ``hget``, ``hgetall`` and ``hdel`` commands.  They are intended as
a quick and easy storage system for occassionally used variables that are
shared between scripts or threads.  They should not be used as a robust
databasing solution.

Creation
~~~~~~~~

    A proxy object is created as follows:

    .. code-block:: javascript

        var redis = require("rampart-redis");

        var rcl = new redis.init([ip, ][port, ][timeout]);

        var myProxyObject = new rcl.proxyObj(name);

    Where ``name`` is the name of the Redis key that will store the Redis
    Hash.

Usage
~~~~~

    Setting and reading the property keys of the returned proxy object will automatically
    set and retrieve the keys in the specified Hash Key.  These values will then
    be available in any other thread or script which connects to the same Redis
    Server instance using the identical proxy object.

    Example:

    .. code-block:: javascript

        /* script 1 - insert new data */

        var redis = require("rampart-redis");

        var rcl = new redis.init(); //localhost and default port

        var users = new rcl.proxyObj("Users");

       /* populate variables for insertion */
       var cl = [
           "principal", "principal", "salary",
           "salary", "hourly", "intern"
       ];
       var name = [
           "Debbie Dreamer", "Rusty Grump","Georgia Geek",
           "Sydney Slacker", "Pat Particular", "Billie Barista"
       ];
       var id = [ "dd", "rg", "gg", "ss", "pp", "bb" ];
       var age = [ 63, 58, 44, 44, 32, 22 ];
       var salary = [ 250000, 250000, 100000, 100000, 80000, 0 ];
       var title = [
           "Chief Executive Officer", "Chief Financial Officer", "Lead Programmer",
           "Programmer", "Systems Administrator", "Intern"
       ];

       var startDate = [ 
           '1999-12-31', 
           '1999-12-31', 
           '2001-3-15', 
           '2002-5-12',
           '2003-7-14', 
           '2020-3-18'
       ];

       /* insert rows */
       for (var i=0; i<6; i++)
       {
            users[id] = {
                employeeClass: cl[i], 
                name: name[i], 
                age: age[i], 
                salary: salary[i], 
                title: title[i], 
                startDate: startDate[i] 
            }
       }
       /* end script 1 */

        /* script 2 - access data */

        var redis = require("rampart-redis");

        var rcl = new redis.init(); //localhost and default port

        var users = new rcl.proxyObj("Users");

        delete users.bb; //Billie's internship is finished.

        rampart.utils.printf("%3J\n", users);

        /* expected output:
            {
               "rg": {
                  "employeeClass": "principal",
                  "name": "Rusty Grump",
                  "age": 58,
                  "salary": 250000,
                  "title": "Chief Financial Officer",
                  "startDate": "1999-12-31"
               },
               "ss": {
                  "employeeClass": "salary",
                  "name": "Sydney Slacker",
                  "age": 44,
                  "salary": 100000,
                  "title": "Programmer",
                  "startDate": "2002-5-12"
               },
               "dd": {
                  "employeeClass": "principal",
                  "name": "Debbie Dreamer",
                  "age": 63,
                  "salary": 250000,
                  "title": "Chief Executive Officer",
                  "startDate": "1999-12-31"
               },
               "pp": {
                  "employeeClass": "hourly",
                  "name": "Pat Particular",
                  "age": 32,
                  "salary": 80000,
                  "title": "Systems Administrator",
                  "startDate": "2003-7-14"
               },
               "gg": {
                  "employeeClass": "salary",
                  "name": "Georgia Geek",
                  "age": 44,
                  "salary": 100000,
                  "title": "Lead Programmer",
                  "startDate": "2001-3-15"
               }
            }
        */

Caveats
~~~~~~~

    *  JavaScript primitives and :green:`Objects` are stored and retrieved by converting
       to and from `CBOR <https://duktape.org/guide.html#builtin-cbor>`_
       which limits what can be stored (no :green:`Dates` or
       :green:`Functions`);

    *  Proxy Objects are saved to the Redis Key specified when the object is
       created (i.e. ``new rcl.proxyObj(redisKeyName)``).  If the Redis Key
       already exists and is not a Redis Hash, an error will be thrown.

    *  Any Proxy Object key that begins with ``_`` (underscore) will not be
       saved to the Redis Hash and will only exist locally.  The following
       are preset:

       *  ``_destroy`` - A :green:`Function` which when called, deletes the
          Redis Hash from the database. Example:


          .. code-block:: javascript

              var rcl = new redis.init(); //localhost and default port

              var users = new rcl.proxyObj("Users");

              users._destroy(); // same as rcl.del("Users");

       *  ``_hname`` - A :green:`String` set to the name of the Redis Key.
          In the above example, it would be set to ``"Users"``;
       
    *  A Proxy Object is a shallow proxy - meaning that only key/value pairs
       of the :green:`Object` returned from ``new rcl.proxyObj()`` will be stored
       to the Redis database.  Example:
       
       .. code-block:: javascript

           var rcl = new redis.init(); //localhost and default port

           var users = new rcl.proxyObj("Users");

           users.myid = {name:"Joe"}; //myid.name is stored in Redis

           users.myid.age = 42;       // users.myid.age is not set;
           
           console.log(users.myid)    // {name:"Joe"}

           var myid = users.myid;     // var myid is created from the Redis Hash "myid"
                                      // which is CBOR decoded into a JavaScript Object
           myid.age = 42;
                                
           users.myid = myid;         // myid.age and myid.name are now stored in Redis

           console.log(users.myid)    // {name:"Joe", age: 42}

    *  Operations which may otherwise appear to be atomic will not work
       properly with concurrency.  Example:  ``users.count++`` will indeed
       increment the variable stored under the Hash key ``count``.  However,
       it will retrieve the value, add one, then set it again, during which
       time another script may have retrieved the value to do the same. 
       Thus, doing this simultaneously in several scripts or threads will
       not work as intended.





