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

Main Function
-------------

After loading, a new database environment can be opened (and optionally created) as
follows:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

Where:

    *  ``path`` is a :green:`String`, the location of the database environment.

    *  ``create`` is a :green:Boolean, whether to create a new database
       environment, if it doesn't exist.

    * ``options`` is a :green:`Object`, a list of options for opening a new 
      database environment.  Options include:

        * ``mapSize`` - a :green:`Number`, an integer to set the size of the memory map
          in megabytes to use for this environment. The size of the memory
          map is also the maximum size of the database. The value should be chosen as
          large as possible, to accommodate future growth of the database. 
          It may be increased after the database is created.

        * ``conversion`` - :green:`String`, whether and what type of
          conversions should be performed before storing values.  One of
          these case-insensitive :green:`Strings`:
          
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
            (`put`_\ ) can be any type including a :green:`Buffers` and
            :green:`Objects` that contains a :green:`Buffers`.  Output
            values (`get`_\ ) will be the same as the input value. See
            `CBOR encoding description <https://duktape.org/guide.html#builtin-cbor>`_
            for more information on CBOR encoding.
         
        * ``noSync`` - a :green:`Boolean`, whether to turn off the flushing
          of sy buffers to disk when committing a transaction.  This
          optimization means a system crash can corrupt the database or lose
          the last transactions if buffers are not yet flushed to disk.  The
          risk is governed by how often the system flushes dirty buffers to
          disk and how often `sync`_ is manually called.  However, if the
          filesystem preserves write order and the ``writeMap`` setting
          below is not set or set ``false``, transactions exhibit ACI
          (atomicity, consistency, isolation) properties and only lose D
          (durability).  I.e.  database integrity is maintained, but a
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
        A set of functions to operate on the database.  See below.
        
LMDB functions
--------------

After a database environment is opened, the following functions and
flags are available in the return object:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path, create, options);
    /* return value:
        {
            get:  {_func: true},
            put:  {_func: true},
            del:  {_func: true},
            drop: {_func: true},
            sync: {_func: true},
        }
    */

get
---

Retrieve values from a database.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);


    var ret = lmdb.get(dbase, key[, nKeys]);

    /* or */

    var ret = lmdb.get(dbase, key[, endkey][, max]);

Where:

    * ``dbase`` is a :green:`String`, the name of the database
      to be accessed.  If the database does not exist, an error
      will be thrown.

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

      If ``endKey`` is set to the special string "*", all keys beginning
      with ``key`` will be returned.

    * ``max`` is an optional positive :green:`Number` greater than 0 which may be specified
      to limit the number of key/value pairs returned when using ``endkey``.

Return Value:
    If neither ``nKeys`` nor ``endkey`` is specified, a single value is
    returned.  If `JSONValues` is set, the type will be the same type
    as given.

    If either ``nKeys`` or ``endkey`` is specified, an :green:`Object` of
    key/value pairs is returned with each key set to the name of the
    retrieved key and each value set as described above.

put
---

Put (store) values in a given :green:`Object` into a given database, indexed
by the :green:`Object's` keys.

Usage:

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    lmdb.put(dbase, kvpairs);

Where:

    * ``dbase`` is a :green:`String`, the name of the database
      to be accessed.  If the database does not exist, it will
      be created.

    * ``kvpairs`` is an :green:`Object` with each :green:`Object` key
      corresponding to a database key to be used and each :green:`Object`
      value corresponding to the database value to be stored.  Note: Values must
      be a :green:`String` or :green:`Buffer` unless ``conversion`` above is
      set to "JSON" or "CBOR".

Return Value:
    ``undefined``.

del
---

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
----

Drop a database from the database environment.

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    lmdb.drop(dbase);

Where:

    * ``dbase`` is a :green:`String`, the name of the database
      to be dropped.

Return Value:
    ``undefined``.

sync
----

Sync the database envirnoment.  Useful if ``mapAsync`` or ``noSync``
is set in order to manually sync data to disk.

.. code-block:: javascript

    var Lmdb = require("rampart-lmdb");
    var lmdb = new Lmdb.init(path,create,options);

    lmdb.sync();

Return Value:
    ``undefined``.


Example
-------

.. code-block:: javascript

