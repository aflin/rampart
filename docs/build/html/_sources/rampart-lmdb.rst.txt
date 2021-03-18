The rampart-lmdb module
==============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-lmdb module uses 
`Symas LMDB library <https://symas.com/lmdb/>`_.
The authors of Rampart extend our thanks to 
`Symas <https://symas.com/>`_
for this library.

License
~~~~~~~

The LMDB library is licensed under the 
`OpenLdap Public License <https://github.com/LMDB/lmdb/blob/mdb.master/libraries/liblmdb/LICENSE>`_\ ,
which is a BSD-like license.
The rampart-lmdb module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-lmdb module provides a key/value store database using the LMDB
library.  For background information, see 
`Symas' description here <https://symas.com/lmdb/>`_. 



How does it work?
~~~~~~~~~~~~~~~~~

The rampart-lmdb module exports a get and put function which allow a program
to store data indexed by its key.  Data may be a :green:`String` or
:green:`Buffer`, or optionally data can be converted to and from JSON
automatically.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");


Terminology
-----------

Within LMDB, a "database environment" is a single file which lives in its own
directory, and has an accompanying lock file.  Within that file, data may be
partitioned into "databases".  Thus an LMDB "database environment" is
organizationally similar to a SQL database and an LMDB "database" is similar
to a SQL table.

Main Function (init)
--------------------

After loading, a new database environment can be opened (and optionally created) as
follows:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

Where:

    *  ``path`` is a :green:`String`, the location of the database environment.

    *  ``create`` is a :green:`Boolean`, whether to create a new database
       environment, if it doesn't exist.

    * ``options`` is a :green:`Object`, a list of options for opening a new 
      database environment.  Options include:

        * ``mapSize`` - a :green:`Number`, an integer to set the size of the memory map
          in megabytes to use for this environment.  The size of the memory
          map is also the maximum size of the database environment.  The
          value should be chosen as large as possible, to accommodate future
          growth of the database environment.  It may be increased after the
          database environment is created.  The default is ``16`` (16Mb).

        * ``conversion`` - :green:`String`, whether and what type of
          conversions should be performed before storing values.  This
          applies to `LMDB Easy Functions`_ only.  The value is of these
          case-insensitive :green:`Strings`:
          
          * ``Buffer`` - the default if not specified.  Input values
            (`put`_\ ) can be a :green:`String` or a :green:`Buffer` and are
            stored as is.  Output values (`get`_\ ) will be a
            :green:`Buffer`.

          * ``String`` - Input values (`put`_\ ) can be a :green:`String` 
            or a :green:`Buffer` and are stored as is.  Output values
            (`get`_\ ) will be a :green:`String`.  If the output value
            includes NULL characters, the :green:`String` will be truncated.

          * ``JSON`` - Input values (`put`_\ ) can be any type 
            except a :green:`Buffer` or an :green:`Object` that contains a
            :green:`Buffer`.  Output values (`get`_\ ) will be the same as
            the input value.

          * ``CBOR`` - Input values
            (`put`_\ ) can be any type including a :green:`Buffer` or
            :green:`Objects` that contains a :green:`Buffers`.  Output
            values (`get`_\ ) will be the same as the input value. See
            `CBOR encoding description <https://duktape.org/guide.html#builtin-cbor>`_
            for more information on CBOR encoding.

        * ``maxDbs`` - a positive :green:`Number`, the maximum number of
          named databases that can be used in the opened database
          environment.  Default is 256.  There is a cost to opening an
          environment with a large ``maxDbs`` value.
 
        * ``noSync`` - a :green:`Boolean`, whether to turn off the flushing
          of LMDB buffers to disk when committing a transaction.  This
          optimization means a system crash can corrupt the database or lose
          the last transactions if buffers are not yet flushed to disk.  The
          risk is governed by how often the system flushes dirty buffers to
          disk and how often `sync`_ is manually called.  However, if the
          filesystem preserves write order and the ``writeMap`` setting
          below is not set or set ``false``, transactions exhibit ACI
          (atomicity, consistency, isolation) properties and only lose D
          (durability).  This means database integrity is maintained, but a
          system crash may undo the final transactions.

        * ``noMetaSync`` - a :green:`Boolean`, whether flushing of the system buffers
          to disk happens only once per transaction, omitting the metadata
          flush.  If true, LMDB will defer that until the system flushes
          files to disk, or when `sync`_ below is called. 
          This optimization maintains database integrity, but a system crash
          may undo the last committed transaction.

        * ``mapAsync`` - a :green:`Boolean`, whether, when using
          ``writeMap``, LMDB should use asynchronous flushes to disk. As with
          ``noSync``, a system crash can then corrupt the database or lose
          the last transactions. Calling `sync`_ below ensures on-disk
          database integrity.

        * ``noReadAhead`` - a :green:`Boolean`, whether LMDB should Turn off
          readahead. Most operating systems perform readahead on read
          requests by default. This option turns it off if the OS supports
          it. Turning it off may help random read performance when the DB is
          larger than RAM and system RAM is full. 

        * ``writeMap`` - a :green:`Boolean`, whether to write data directly
          to LMDB's memory map of the database environment. This is faster
          and uses fewer mallocs, but loses protection from application bugs
          like wild pointer writes and other bad updates into the database. 

Return Value:
        A set of functions to operate on the database environment.  See below.
        
LMDB Easy Functions
-------------------

After a database environment is opened, the following functions and
flags are available in the return object:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path, create, options);
    /* return value:
        {
            openDb:      {_func: true},
            getCount:    {_func: true},
            get:         {_func: true},
            put:         {_func: true},
            del:         {_func: true},
            drop:        {_func: true},
            sync:        {_func: true},
            transaction: {_func: true}
        }
    */

openDb
~~~~~~

Open a database for use with the below functions.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    var ret = lmdb.openDb([dbase[, create]]);


Where:

    ``dbase`` is a :green:`String`, the name of the database
    to be accessed.  If ``undefined``, the lmdb default database
    for the database envirnoment will be opened.

    ``create`` is a :green:`Boolean`, if ``true`` openDb will be 
    a write transaction which creates the database, if it does not
    exist.

Return Value:
    A ``dbi object`` representing the opened database.

Note:  
    If opening a database for use in one of the 
    `LMDB Transaction Functions`_ below, this function must be called
    before any transaction is opened (before calling ``new lmdb.transaction()``).


get
~~~

Retrieve values from a database.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);


    var ret = lmdb.get(dbase, key[, nKeys]);

    /* or */

    var ret = lmdb.get(dbase, key[, endkey][, max]);

Where:

    * ``dbase`` is ``dbi object`` returned from `openDb`_ or a 
      :green:`String`, the name of the database to be accessed.  If the
      database does not exist, an error will be thrown.

    * ``key`` is a :green:`String`, the name of the key whose data
      will be retrieved.
      
    * ``nKeys`` is an optional :green:`Number`, the total number of keys to 
      retrieve.  If a positive number, the ``key`` will be returned
      along with the next ``nKeys - 1`` keys that follow in lexical
      order.  If a negative number, the ``key`` will be returned
      along with the previous ``nKeys - 1`` keys that preceed in lexical
      order. NOTE that if key is not found, no other keys will be returned.
      See the glob version in ``endkey`` below for that functionality.

    * ``endkey`` is an optional :green:`String`, the last key to retrieve, retrieving
      all keys between ``key`` and ``endKey`` (but no more than ``max``, if given).  If
      ``endKey`` preceeds ``key`` in lexical order, the keys and values will
      be returned in reverse order (but no more than ``max``, if given).

      Globbing: If ``endKey`` is set to the special string ``"*"``, all keys beginning
      with ``key`` will be returned.

    * ``max`` is an optional positive :green:`Number` greater than 0 which may be specified
      to limit the number of key/value pairs returned when using ``endkey``.

Return Value:
    If neither ``nKeys`` nor ``endkey`` is specified, a single value is
    returned.  The type of the return value is determined by the ``conversion``
    setting above.

    If either ``nKeys`` or ``endkey`` is specified, an :green:`Object` of
    key/value pairs is returned with each key set to the name of the
    retrieved key and each value set as described above.

put
~~~

Put (store) values in a given :green:`Object` or in a given key:value pair
into a given database, indexed by the key(s).

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    lmdb.put(dbase, kvpairs);

    /* or */

    lmdb.put(dbase, key, value);

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_ or a 
      :green:`String`, the name of the database to be accessed.  If the
      database does not exist, it will be created.

    * ``kvpairs`` is an :green:`Object` with each :green:`Object` key
      corresponding to a database key to be used and each :green:`Object`
      value corresponding to the database value to be stored.  Note: Values must
      be a :green:`String` or :green:`Buffer` unless ``conversion`` above is
      set to "JSON" or "CBOR".

    * ``key`` is the key of a single key:value pair.
    
    * ``value`` is the value of a single key:value pair.

Return Value:
    ``undefined``.

del
~~~

Delete values in a database.
 
Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    var ret = lmdb.del(dbase, key[, nKeys][, retValues]);

    /* or */

    var ret = lmdb.del(dbase, key[, endkey][, max][, retValues]);

Where options are the same as in `get`_ above, with the addition of
``retValues`` (which, if provided, is a :green:`Boolean`).  If ``retValues``
is set ``true``, the deleted values are returned in the same manner as get.
Otherwise ``undefined`` is returned.

drop
~~~~

Drop a database from the database environment removing all the
items in the database along with the database itself.

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    lmdb.drop(dbase);

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_ or a
      :green:`String`, the name of the database to be dropped.

      To drop the default database, pass an empty string or ``null``:
      ``lmdb.drop(null);``. 


Return Value:
    ``undefined``.

Note: 
    If a ``dbi object`` is specified, it must not be used as a parameter
    to any other function after executing ``lmdb.drop()``.  It, however,
    may be recreated calling ``openDb(dbname, true)`` again.

Note:
    Dropping the default database will delete the its contents, 
    however it will not be removed and the named database metadata
    will remain.

sync
~~~~

Sync the database envirnoment.  Useful if ``mapAsync`` or ``noSync``
is set in order to manually sync data to the disk.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    lmdb.sync();

Return Value:
    ``undefined``.

getCount
~~~~~~~~

Count the number of items in a database.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    var count = lmdb.getCount(dbase);


Where:

    ``dbase`` is ``dbi object`` returned from `openDb`_ or a 
    :green:`String`, the name of the database to be accessed.  If the
    database does not exist, an error will be thrown.


Return Value:
    A :green:`Number`, the number of items in the database.

listDbs
~~~~~~~

List the named databases in a database environment.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    var list = lmdb.listDbs(dbase);

Return Value:
    An :green:`Array` of :green:`Strings`, the names
    of all named databases.

Note:  
    The names of named databases are stored in the default database.  To
    retrieve the names, every item in the default database must be scanned. 
    When using named databases, the best practice is to not store data in
    the default database.

close
~~~~~

Close the database envirnoment.  After closing, all transaction
handles, database handles and all functions using the previously 
opened environment will throw errors if used again.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    /* do stuff */

    lmdb.close();

Return Value:
    ``undefined``.

Easy Functions Full Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following creates random entries of names and addresses.  After the
database is populated with the sample data, a selection that meet specific
criteria is printed.

.. code-block:: javascript

    /* make printf and sprintf easier to use */
    rampart.globalize(rampart.utils, ["printf","sprintf"]);

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(
        "./lmdb-test",
        true, /* create if does not exist */
        {
            mapSize: 1024, //one gigabyte
            conversion: "CBOR",
            noMetaSync:true,
            noSync: true,
        }
    );

    var dbi = lmdb.openDb("testdata", true);

    /* create test data */

    var fnames = [ "Mohamed", "Imene", "Santiago", "Sofía", "Wei", "Jing",
                   "Noel", "Amelia", "Oliver", "Olivia" ];

    var lnames = [ "Beridze", "Cohen", "Kovačević", "Nielsen", "Tremblay",
                   "Hernández", "Smith", "Kumar", "Mabiala", "Kimetto"];

    var initials = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    var streets = ["Main", "Broadway", "First", "Second", "Grove", "College",
                   "University", "Park", "Oak", "Pine"];

    var stsuff  = ["Street", "Drive", "Avenue", "Blvd", "Court", "Circle.",
                   "Road", "Lane", "Way", "Highway"];

    function makeName(){
        var ret = {
            first: fnames[Math.floor(Math.random()*10)],
            mi:    initials.charAt(Math.floor(Math.random()*26)),
            last:  lnames[Math.floor(Math.random()*10)]
        }
        ret.full = sprintf ("%s %s. %s", ret.first, ret.mi, ret.last);
        return ret;
    }

    function makeAddr(obj){
        obj.streetno = Math.floor(Math.random()*10000);
        obj.street = sprintf("%s %s", 
                        streets[Math.floor(Math.random()*10)],
                        stsuff[Math.floor(Math.random()*10)]
                     );
        obj.zip = 10000 + Math.floor(Math.random()*90000);
        return obj;
    }

    /* insert entries */
    for (var i=0; i<5000; i++) {
        var entry = makeName();
        entry = makeAddr(entry);
        var key = entry.full + " from " + entry.zip;
        lmdb.put(dbi, key, entry);
    }

    // sync now, or sync will happen upon script exit.
    //lmdb.sync(); 

    /* get all the Sofías in the database */
    var entries = lmdb.get(dbi, "Sofía", "*");
    var keys = Object.keys(entries);
    var total = lmdb.getCount(dbi);

    printf("There are %d Sofías out of %d entries in the database.\n", keys.length, total);

    for (i=0;i<keys.length;i++) {
        var key = keys[i];
        var entry = entries[key];
        // find the Sofías living on Main Street
        if(entry.street == "Main Street")
            printf("%s lives at %d %s\n", key, entry.streetno, entry.street);
    }

    /* possible output:
    There are 471 Sofías out of 5000 entries in the database.
    Sofía D. Cohen from 85916 lives at 2320 Main Street
    Sofía H. Hernández from 17267 lives at 518 Main Street
    Sofía J. Tremblay from 69088 lives at 4701 Main Street
    Sofía T. Kimetto from 73446 lives at 441 Main Street
    Sofía U. Mabiala from 94846 lives at 1608 Main Street
    Sofía Z. Hernández from 57045 lives at 905 Main Street
    */

    lmdb.drop("testdata");

Note that the above is a naive way of inserting data into a database since
it opens and closes a transaction for every record inserted.

Though more memory intensive, it is far more efficient to use the following
code in order to insert all of the records in a single transaction:

.. code-block:: javascript

    /* create entries in an object*/
    var insertobj = {}
    for (var i=0; i<5000; i++) {
        var entry = makeName();
        entry = makeAddr(entry);
        var key = entry.full + " from " + entry.zip;
        insertobj[key]=entry; 
    }
    /* insert all at once */
    lmdb.put(dbi, insertobj);

In order to conserver memory and to insert in a single transaction, see the
`Transaction Functions Full Example`_ below.
    

LMDB Transaction Functions
--------------------------

Transactions provide lower level access to lmdb function.  In the above
functions, transactions are automatically opened and closed without
explicitely having to do so.  In order to have more flexibility and possibly
see a performance gain, the transaction model outlined below provides
the relevant functions.


lmdb.transaction
~~~~~~~~~~~~~~~~

The ``lmdb.transaction`` function opens a new transaction and returns
functions which may perform tasks within the open transaction.  

Note that the `LMDB Easy Functions`_ above that open an internal transaction
for writing will throw an error if they are called while any write
transaction below is open.

These include `put`_\ , `del`_\ , `drop`_ and `openDb`_
(only when a database is being created).


Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    var txn = new lmdb.transaction([dbase, ] open_rw [, commit_by_default] );

    /* return value:
        {
            get:         {_func: true},
            put:         {_func: true},
            del:         {_func: true},
            cursorGet:   {_func: true},
            cursorPut:   {_func: true},
            cursorDel:   {_func: true},
            cursorNext:  {_func: true},
            cursorPrev:  {_func: true},
            commit:      {_func: true},
            abort:       {_func: true},
            lmdb:        {} /*the above lmdb object */
        }
    */

    /* use connection, then commit or abort */
    tnx.commit();

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_ or a 
      :green:`String`, the name of the database to be accessed.  If the
      database does not exist, it will be created.  If omitted, the 
      lmdb default database for the current database environment will
      be use.  This database will be the default for all operations
      below.  However, more than one database may be used per transaction.

    * ``open_rw`` is an :green:`Boolean`, if ``true``, open the transaction
      for read/write.  This is needed if any data will be added or deleted
      from a database.  If ``false``, the transaction will be read only.

    * ``commit_by_default`` is an :green:`Boolean`.  When a transaction is
      opened, it must be eventually closed using either `txn.commit`_ or
      `txn.abort`_\ .  If the script exits or ``var txn`` goes out of
      scope (e.g. the function in which ``var txn`` was declared returns) without
      closing, it will be automatically closed.  If ``commit_by_default``
      is ``true``, `txn.commit`_ will be called. Otherwise if ``false`` or
      not set, `txn.abort`_ will be called.  This is most relevant when 
      ``open_rw`` is ``true`` and the database is being altered.

Note:
    Only one read/write transaction per database environment may be open at
    any time.  Attempting to open one while another is open will throw an
    error.  However, along with one read/write transaction, several read
    only transactions may be concurrently open in a single database
    environment.
    
    Note also that opening a new transaction with a :green:`String`
    ``dbase`` parameter, and where ``dbase`` does not exist is a read/write
    open even if ``open_rw`` is ``false``.  As such, if another read/write
    transaction is open, an error will be thrown.  In such a case, the
    database should be opened using `openDb`_ to create it before any
    read/write transactions are opened.

    Note also that the one open read/write transaction restriction is
    per-thread, so no special care is needed when using LMDB from 
    :ref:`rampart-server:The rampart-server HTTP module`.  However, when
    using LMDB from the server, a mutex lock is used to ensure that only one
    read/write transaction is open at any given time.  Therefore read/write
    transactions should be closed as soon as all writes are finished.

Return Value:
    An :green:`Object` of functions.


txn.get
~~~~~~~

Get a single value from a database.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    var res = txn.get([dbase ,] key [, return_string]);

    tnx.commit();

Where:
    
    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``key`` is a :green:`String` or :green:`Buffer`, the key of the item
      to be retrieved.

    * ``return_string`` is a :green:`Boolean`. If ``true`` the return value
      will be a :green:`String`.  If ``false`` or not set, the return value
      will be a :green:`Buffer`.

Return Value:
    A :green:`Buffer` or, if ``return_string`` is ``true``, a
    :green:`String`.

txn.getRef
~~~~~~~~~~

Get a single value from a database in a mmaped backed buffer.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    var res = txn.getRef([dbase ,] key);

    /* use res here */

    tnx.commit();

    /* res data is invalid and buffer is reset to zero length */

Where:
    
    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``key`` is a :green:`String` or :green:`Buffer`, the key of the item
      to be retrieved.

Return Value:
    A :green:`Buffer`.  The buffer is backed by the mmaped data on disk and
    is only valid until `txn.commit`_ or `txn.abort`_ is called, or until
    the ``txn`` variable is no longer in scope.

Note:
    This can be used to have :ref:`rampart-server:The rampart-server HTTP module`
    serve content directly from the disk with no copies. In order to do so,
    the ``txn`` variable must stay in scope until the content is served. 

    In the following example, ``setTimeout`` is used to keep the ``txn``
    object in scope until after the http transaction is complete.  Otherwise
    the transaction would be automatically closed upon the return of the
    function and the contents of the buffer would be invalid and reset to
    zero length.

.. code-block:: javascript

    /* callback function for the rampart-server module */
    function cb (req)
    {
        var txn = new lmdb.transaction(false);
        /* get a mmap backed reference to our data */
        var refbuf = txn.getRef("myjpg");

        /* close transaction after http request is served. *
         * The setTimeout function is inserted into the    *
         * event loop and will be run after the http reply *
         * is sent to the client.                          */
        setTimeout(
            function() {
                txn.commit();
            }, 
            0 
        );
        /* serve data directly from disk */
        return({"jpg":refbuf});
    }


txn.put
~~~~~~~

Put (store) values in a given :green:`Object` into a given database, indexed
by the :green:`Object's` keys.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read write */
    var txn = new lmdb.transaction([dbase, ] true);

    txn.put([dbase, ] kvpairs);

    /* or */

    txn.put([dbase, ] key, value);

    tnx.commit();

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``kvpairs`` is an :green:`Object` with each :green:`Object` key
      corresponding to a database key to be used and each :green:`Object`
      value corresponding to the database value to be stored.

    * ``key`` is a :green:`String` or :green:`Buffer`, the key of a single key:value pair.
    
    * ``value`` is a :green:`String` :green:`Buffer`, or :green:`Object`. The value of a single key:value pair.
      If ``value`` is an :green:`Object`, it will be automatically converted
      to a :green:`Buffer` using ``CBOR.encode()``.  Note, when retrieved
      using ``txn.get`` or ``txn.cursorGet``, a CBOR encoded value will not
      be automatically decoded.

Return Value:
    ``undefined``.

txn.del
~~~~~~~

Delete an the item with the given key.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read write */
    var txn = new lmdb.transaction([dbase, ] true);

    txn.del([dbase, ] key);

    tnx.commit();

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``key`` is the key of the item to be deleted.

Return Value:
    A :green:`Boolean`:  ``true`` if the item was deleted.  ``false`` if
    there was no item with the given ``key`` found.
    

Using Cursors
~~~~~~~~~~~~~

When any of the below Cursor Functions is used, a cursor is automatically
created for the database being accessed. The cursor keeps track of the
position of the last operation and may be used for subsequent operations.

Each database specified in the below functions has its own associated cursor. 
Each cursor is automatically destroyed when the transaction is committed or
aborted.

txn.cursorGet
~~~~~~~~~~~~~

Position the cursor using one of several possible "operation modes" and
return the key and value of the item at the cursor's new position.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    var res = txn.cursorGet([dbase ,] op [, key] [, key_is_string [, val_is_string] ]);

    tnx.commit();

Where:
    
    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``op`` is a flag, which specifies the operation mode and is one of the following:

        * ``lmdb.op_set`` - Position the cursor at the item with the key
          ``key``.

        * ``lmdb.op_setRange`` - Position the cursor at the first item with a key greater than or equal to
          ``key``.

        * ``lmdb.op_first`` - Position the cursor at the first item in the
          given database.

        * ``lmdb.op_last`` - Position the cursor at the last item in the given
          database. 

        * ``lmdb.op_next`` - Position the cursor one after its current
          position.  If the cursor has not been set, position at the
          first item in the database.

        * ``lmdb.op_prev`` - Position the cursor one before its current
          position. If the cursor has not been set, position at the
          last item in the database.

        * ``lmdb.op_current`` - Cursor stays at its current position.

    * ``key`` is the key of the item to be retrieved.  Used for ``op_set``
      and ``op_setRange``.

    * ``key_is_string`` is a :green:`Boolean`. If ``true`` (the default),
      the return ``key`` will be converted to a :green:`String`.
      If ``false``, (the default) the return ``key`` will be a :green:`Buffer`.

    * ``val_is_string`` is a :green:`Boolean`. If ``true``, the returned
      ``value`` will be converted to a :green:`String`.  If ``false``,
      (the default)``value`` will be a :green:`Buffer`.

Return Value:
    An :green:`Object` with the propertis ``key`` and ``value`` set to the 
    key and value of the retrieved item (e.g. ``{"key": dbkey, "value": dbval}``).

txn.cursorPut
~~~~~~~~~~~~~

Put (store) value in a given into a database, indexed
by the the given key.  Move cursor to the items location.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read write */
    var txn = new lmdb.transaction([dbase, ] true);

    txn.cursorPut([dbase, ] key, value);

    tnx.commit();

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``key`` is a :green:`String` or :green:`Buffer`, the key of a single key:value pair.
    
    * ``value`` is a :green:`String` :green:`Buffer`, or :green:`Object`. The value of a single key:value pair.
      If ``value`` is an :green:`Object`, it will be automatically converted
      to a :green:`Buffer` using ``CBOR.encode()``.  Note, when retrieved
      using ``txn.get`` or ``txn.cursorGet``, a CBOR encoded value will not
      be automatically decoded.

Return Value:
    ``undefined``.

txn.cursorDel
~~~~~~~~~~~~~

Delete the item at the cursor's position.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read write */
    var txn = new lmdb.transaction([dbase, ] true);

    /* e.g. to position the cursor */
    txn.cursorGet([dbase, ] lmdb.op_set, key);

    /* delete item at cursor position */
    txn.cursorDel([dbase, ]);

    tnx.commit();

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

Return Value:
    ``undefined``.

txn.cursorNext
~~~~~~~~~~~~~~

Move cursor position to the next item and return the key:value pair.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    /* position the cursor at next item*/
    var res = txn.cursorNext([dbase, ] [key_is_string [, val_is_string] ]);

    tnx.commit();

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``key_is_string`` is a :green:`Boolean`. If ``true``
      the return ``key`` will be converted to a :green:`String`.
      If ``false`` (the default) the return ``key`` will be a :green:`Buffer`.

    * ``val_is_string`` is a :green:`Boolean`. If ``true`` the returned
      ``value`` will be converted to a :green:`String`.  If ``false``
      (the default)``value`` will be a :green:`Buffer`.

This operates identical to:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    /* position the cursor at next item*/
    var res = txn.cursorGet([dbase, ] lmdb.op_next);

    tnx.commit();

The exception is that if the cursor is already at the last item,
``undefined`` is returned instead of an empty object.

It allows the following:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    var res;

    /* 
        Get every item in the database.
        If cursor has not been previously set cursorNext()
          starts at the first item.
    */
    while( (res = txn.cursorNext()) )
    {
        /* do something with res */
    }

    tnx.commit();

Return Value:
    Same as `txn.cursorGet`_ (an :green:`Object`) unless the cursor
    is at the last item (in which case ``undefined`` is returned).

txn.cursorPrev
~~~~~~~~~~~~~~

Move cursor position to the previous item and return the key:value pair.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    /* position the cursor at previous item*/
    var res = txn.cursorPrev([dbase, ] [key_is_string [, val_is_string] ]);

    tnx.commit();

Where:

    * ``dbase`` is a ``dbi object`` returned from `openDb`_\ . If the
      database does not exist, it will be created.  If omitted, the 
      database specified in ``new lmdb.transaction`` will be used.

    * ``key_is_string`` is a :green:`Boolean`. If ``true`` 
      the return ``key`` will be converted to a :green:`String`.
      If ``false`` (the default) the return ``key`` will be a :green:`Buffer`.

    * ``val_is_string`` is a :green:`Boolean`. If ``true`` the returned
      ``value`` will be converted to a :green:`String`.  If ``false``
      (the default)``value`` will be a :green:`Buffer`.



This operates identical to:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase, ] false);

    /* position the cursor at previous item*/
    var res = txn.cursorGet([dbase, ] lmdb.op_prev);

    tnx.commit();

The exception is that if the cursor is already at the first item,
``undefined`` is returned instead of an empty object.

It allows the following:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    /* open read only if only reading in this transaction */
    var txn = new lmdb.transaction([dbase], false);

    var res;

    /* 
        Get every item in the database in reverse order.
        If cursor has not been previously set cursorPrev()
          starts at the last item.
    */
    while( (res = txn.cursorPrev()) )
    {
        /* do something with res */
    }

    tnx.commit();


Return Value:
    Same as `txn.cursorGet`_ (an :green:`Object`) unless the cursor
    is at the first item (in which case ``undefined`` is returned).


txn.commit
~~~~~~~~~~

Commit the current transaction.  If it is a read/write transactions, the
associated mutex lock is released.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    var txn = new lmdb.transaction([dbase], open_rw);

    /* use tnx here */
   
    tnx.commit();

Return Value:
    ``undefined``

txn.abort
~~~~~~~~~

Abort the current transaction. All data written since starting the
transaction will be discarded. If it is a read/write transactions, the
associated mutex lock is released.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");

    var lmdb = new Lmdb.init(path,create,options);

    var txn = new lmdb.transaction([dbase], open_rw);

    /* use tnx here */
   
   /* discard any/all changes */
    tnx.abort();

Return Value:
    ``undefined``


Transaction Functions Full Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This example performs the same tasks as the `Easy Functions Full Example`_
except that it uses transactions.  As a result, execution speed is
significantly improved.

.. code-block:: javascript

    /* make printf and sprintf easier to use */
    rampart.globalize(rampart.utils, ["printf","sprintf"]);

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(
        "./lmdb-test",
        true,  /* create if does not exist */
        {
            mapSize: 1024, /* one gigabyte */
            conversion: "CBOR",
            noMetaSync:true,
            noSync: true,
        }
    );

    var dbi = lmdb.openDb("testdata", true);

    /* create test data */

    var fnames = [ "Mohamed", "Imene", "Santiago", "Sofía", "Wei", "Jing",
                   "Noel", "Amelia", "Oliver", "Olivia" ];

    var lnames = [ "Beridze", "Cohen", "Kovačević", "Nielsen", "Tremblay",
                   "Hernández", "Smith", "Kumar", "Mabiala", "Kimetto"];

    var initials = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    var streets = ["Main", "Broadway", "First", "Second", "Grove", "College",
                   "University", "Park", "Oak", "Pine"];

    var stsuff  = ["Street", "Drive", "Avenue", "Blvd", "Court", "Circle.",
                   "Road", "Lane", "Way", "Highway"];

    function makeName(){
        var ret = {
            first: fnames[Math.floor(Math.random()*10)],
            mi:    initials.charAt(Math.floor(Math.random()*26)),
            last:  lnames[Math.floor(Math.random()*10)]
        }
        ret.full = sprintf ("%s %s. %s", ret.first, ret.mi, ret.last);
        return ret;
    }

    function makeAddr(obj){
        obj.streetno = Math.floor(Math.random()*10000);
        obj.street = sprintf("%s %s", 
                        streets[Math.floor(Math.random()*10)],
                        stsuff[Math.floor(Math.random()*10)]
                     );
        obj.zip = 10000 + Math.floor(Math.random()*90000);
        return obj;
    }

    /* Open a new transaction and insert entries */
    var txn = new lmdb.transaction(dbi,true); //read/write
    for (var i=0; i<5000; i++) {
        var entry = makeName();
        entry = makeAddr(entry);
        var key = entry.full + " from " + entry.zip;
        /* since entry is an object, it will be converted to CBOR */
        txn.put(dbi, key, entry); 
    }
    txn.commit();

    // sync now, or sync will happen upon script exit.
    //lmdb.sync(); 

    /* this must be done while not in a transaction */
    var total = lmdb.getCount(dbi);

    /* get all the Sofías in the database */
    txn = new lmdb.transaction(dbi,false); //read only

    /* get first entry, make the key a string, leave value as a buffer */
    var entry = txn.cursorGet(dbi, lmdb.op_setRange, "Sofía", true);
    i=0;

    /* process first entry and loop to get entries after */
    do {
        /* if there were no Sofías from the above cursorGet or if 
           the current cursorNext is not a Sofía, we are done      */ 
        if( ! entry.key || ! /^Sofía./.test(entry.key) )
            break;
        i++;
        /* in transactions, CBOR is not automatically decoded */
        entry.value = CBOR.decode(entry.value);
        if(entry.value.street == "Main Street")
            printf("%s lives at %d %s\n", 
                    entry.key, 
                    entry.value.streetno,
                    entry.value.street
                  );
    } while(entry = txn.cursorNext(dbi, true));

    printf("There are %d Sofías out of %d entries in the database.\n", i, total);

    txn.commit();

    lmdb.drop(dbi);

    /* possible output:
    Sofía A. Cohen from 25350 lives at 4458 Main Street
    Sofía I. Beridze from 11345 lives at 8483 Main Street
    Sofía P. Cohen from 48234 lives at 9441 Main Street
    Sofía S. Kimetto from 38632 lives at 5244 Main Street
    Sofía X. Smith from 19604 lives at 21 Main Street
    There are 501 Sofías out of 5000 entries in the database.
    */
