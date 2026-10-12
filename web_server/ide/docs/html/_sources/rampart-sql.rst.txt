Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The developers of Rampart are indebted to Thunderstone, LLC for the
Texis library and the decades of development behind it.

License
~~~~~~~

Use of the ``rampart-sql`` module and the Texis library is governed by the
`Rampart Source Available License <https://github.com/aflin/rampart/blob/main/LICENSE-rsal.txt>`_\ .

Vector Index Acknowledgment
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``CREATE VECTOR INDEX`` feature in ``rampart-sql`` is backed by two
embedded engines:

* `usearch <https://github.com/unum-cloud/usearch>`_, an HNSW
  vector search engine by Ash Vardanian and Unum, used under the
  `Apache License 2.0 <https://github.com/unum-cloud/usearch/blob/main/LICENSE>`_.
* `FAISS <https://github.com/facebookresearch/faiss>`_, the IVFPQ
  inverted-list / product-quantization index from Meta, used under the
  `MIT License <https://github.com/facebookresearch/faiss/blob/main/LICENSE>`_.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-sql module provides database and full text search capabilities
in Rampart.  It includes Texis, a SQL relational database with a fully integrated
full text search engine.

Documentation Caveat
~~~~~~~~~~~~~~~~~~~~

In this document, the term "Texis" used to refer to the SQL engine, the text
search index and the text search engine.  The term "Metamorph" is used to
solely refer to the text search engine.

The convention used in this document is ``var Sql`` with a capital
"S" for the return :green:`Object` of the loaded module and ``var sql`` with a
lower-case "s" for the instance of a connection to a database made with the
JavaScript :ref:`connection() constructor <connection_const>` :green:`Function`.

Further Reading
~~~~~~~~~~~~~~~
As the Texis library and its included Metamorph search engine encompasses
more functionality than is expressed in this documentation, a full understanding
of the workings of Texis and Metamorph can be found by referring to the
documentation at Thunderstone.com:

* `Texis Manual <https://docs.thunderstone.com/site/texisman/>`_
* `Metamorph Manual <https://docs.thunderstone.com/site/texisman/metamorph_intelligent_query.html>`_

Loading the Javascript Module
-----------------------------

Loading of the sql module from within Rampart JavaScript is a simple matter
of using the ``require`` statement:

.. code-block:: javascript

    var Sql=require("rampart-sql");

Return value:

.. code-block:: none

    {
        connection:    {_func:true},
        connect:       {_func:true},
        init:          {_func:true},
        list:          {_func:true},
        stringFormat:  {_func:true},
        abstract:      {_func:true},
        sandr:         {_func:true},
        sandr2:        {_func:true},
        rex:           {_func:true},
        re2:           {_func:true},
        rexFile:       {_func:true},
        re2File:       {_func:true},
        searchFile:    {_func:true},
        searchText:    {_func:true},
    }

The returned :green:`Object` contains :green:`Functions` that can be split into two groups:
`Database Functions`_ and `String Functions`_.

Database Functions
------------------

Module Functions
~~~~~~~~~~~~~~~~

Functions on the :green:`Object` returned by ``require("rampart-sql")``.

.. _connection_const:

connection() constructor
''''''''''''''''''''''''

The ``connection`` constructor function takes a :green:`String`, the path to the database
and an optional :green:`Boolean` as parameters. It returns an :green:`Object` representing a
new connection to the specified database.  The return :green:`Object` includes the
following :green:`Functions`: ``exec()``, ``one()``, ``set()``,
``reset()``, ``importCsvFile()``, ``importCsv`` and ``close()``.

Usage:

.. code-block:: javascript

    var sql = new Sql.connection(path [,create]);
           /* or */
    var sql = new Sql.connection(options);

+--------+------------------+---------------------------------------------------+
|Argument|Type              |Description                                        |
+========+==================+===================================================+
|path    |:green:`String`   | The path to the directory containing the database |
+--------+------------------+---------------------------------------------------+
|create  |:green:`Boolean`  | if true, and the directory does not exist, the    |
|        |                  | directory and a new database will be created in   |
|        |                  | the location specified.                           |
+--------+------------------+---------------------------------------------------+
|options |:green:`Object`   | options must include `path` and may include any   |
|        |                  | of the others:                                    |
|        |                  |                                                   |
|        |                  | * ``path`` - :green:`String` - The path to the    |
|        |                  |   directory containing the database.              |
|        |                  | * ``create`` - :green:`Boolean` create the        |
|        |                  |   database if it doesn't exist.                   |
|        |                  | * ``force`` - :green:`Boolean` force create a     |
|        |                  |   database in a non-empty directory (must not     |
|        |                  |   contain a db). This option implies ``create``.  |
|        |                  | * ``addTables`` - :green:`Boolean` - Automatically|
|        |                  |   add any table files that are in ``path``        |
|        |                  |   but are not in the database.  If path           |
|        |                  |   does not contain a database, create one first.  |
|        |                  |   Implies ``force``. See `addTable()`_ below.     |
|        |                  | * ``user`` - :green:`String` - The database user  |
|        |                  |   name for this connection.                       |
|        |                  |   Default is ``"PUBLIC"``.  For normal usage      |
|        |                  |   this should be left unset.                      |
|        |                  | * ``pass`` - :green:`String` - The password       |
|        |                  |   for above ``user``.  Default is ``""``.         |
|        |                  | * ``noUpdater`` - :green:`Boolean` - if true, do  |
|        |                  |   not check for or launch the index monitor       |
|        |                  |   process.  See `scheduleUpdate()`_ below.        |
|        |                  | * **DO NOT USE** the sql command ``GRANT``, and/or|
|        |                  |   ``user`` and ``pass`` above to implement        |
|        |                  |   security for your application.                  |
+--------+------------------+---------------------------------------------------+



Return Value:
   An :green:`Object` of :green:`Functions`:

.. code-block:: none

    {
        exec:            {_func:true},
        query:           {_func:true},
        one:             {_func:true},
        set:             {_func:true},
        reset:           {_func:true},
        close:           {_func:true},
        addTable:        {_func:true},
        importCsv:       {_func:true},
        importCsvFile:   {_func:true},
        scheduleUpdate:  {_func:true},
        scheduleRebuild: {_func:true},
        errMsg:          "",
        db:              "/path/to/db",
        selectMaxRows:   10
    }

Example:

.. code-block:: javascript

	var Sql = require("rampart-sql");

	/* create database if it does not exist */
	var sql = new Sql.connection("/path/to/my/db", true);

Note that to create a new database, the folder ``/path/to/my/db`` **must
not** exist, but ``/path/to/my`` **must** exist and have write permissions for
the current user.


connect()
'''''''''

A shortcut for ``new Sql.connection``

Example:

.. code-block:: javascript

	var Sql = require("rampart-sql");

	/* create database if it does not exist */
	var sql = Sql.connect("/path/to/my/db", true);


init() constructor
''''''''''''''''''

DEPRECATED: see connection() below.

.. _sqllist:

list() - IN-list parameters
'''''''''''''''''''''''''''

To bind a JavaScript :green:`Array` of values as a SQL ``IN (?)`` list,
wrap it with ``Sql.list()``.  The wrapper carries an explicit signal
that the parameter should be expanded into a list rather than treated
as a single scalar:

.. code-block:: javascript

    var Sql = require("rampart-sql");
    var sql = new Sql.connection("./mytestdb");

    /* numeric IN against an int or double column */
    var res = sql.exec(
        "select * from employees where Id in (?)",
        [ Sql.list([101, 205, 309]) ]
    );

    /* string IN against a varchar column */
    var res = sql.exec(
        "select * from employees where Name in (?)",
        [ Sql.list(["Alice", "Bob", "Carol"]) ]
    );

The argument to ``Sql.list()`` must be a non-empty :green:`Array` whose
elements are **all** :green:`Numbers` or **all** :green:`Strings`.  Mixed
types, ``NaN``, ``Infinity``, ``null``, or :green:`Boolean` values throw
an error at the ``Sql.list()`` call.

Numeric lists are bound as a SQL ``DOUBLE`` array; the underlying
column may be any numeric type (``int``, ``int64``, ``uint64``,
``double``, etc.) and Texis will convert at compare time.  String lists
are bound as a Texis ``strlst`` (string list); ``WHERE varchcol IN (?)``
matches when the column value is one of the strings in the list.

.. note::
    Values outside the JavaScript safe-integer range (``±2``\ :sup:`53`\ )
    have already lost precision in JavaScript before reaching
    ``Sql.list()``.  For exact comparisons against ``int64``/``uint64``
    columns with values larger than 2\ :sup:`53`, pass the values as
    :green:`Strings`: ``Sql.list(["9007199254740993", ...])``.

setMapSize()
''''''''''''

Sets the size of the shared-memory segment used to move data between a
connection and its helper process.

Usage:

.. code-block:: javascript

    var Sql = require("rampart-sql");

    var adopted = Sql.setMapSize(bytes);

Where ``bytes`` is a :green:`Number`.  The return value is the size
actually adopted, which may be larger than requested: it is rounded up
to a whole number of memory pages, with a floor of one page.  A missing,
negative or non-numeric argument throws.

For thread safety and performance, a connection used from any thread other
than the main one runs its SQL in a forked helper process, and every
statement, parameter and result row crosses a shared-memory segment between
the two.  Data larger than the segment is sent in successive pieces and
reassembled on the other side, so this setting affects **performance only,
never correctness** — a four-kilobyte segment returns exactly the same rows
as an eight-megabyte one, just with more round trips.

The default is 8MB on 64-bit builds, 1MB on 32-bit.

The setting is process-wide, and applies only to helpers started *after*
the call; a helper already running keeps the size it was given.  Set it
once, early — in a server, at the top of the configuration, since worker
threads fork their helpers on first use:

.. code-block:: javascript

    var Sql = require("rampart-sql");

    Sql.setMapSize(32 * 1024 * 1024);   /* before any threaded connection */

Raising it reduces the number of round trips for large rows or large
statements.  Lowering it reduces the memory a process can commit when
many worker threads each hold a helper.  The segment is a reservation
rather than an allocation — pages are committed only as they are used,
so a large setting costs nothing until a large message needs it.

.. note::
    On Linux the pages touched by an unusually large message are handed
    back afterwards.  macOS and FreeBSD cannot release them, so on those
    systems a single very large row leaves its helper holding those
    pages for the rest of its life.  That is why the default is modest
    rather than generous.

Connection Functions
~~~~~~~~~~~~~~~~~~~~

Functions on a connection :green:`Object`, as returned by the
`connection() constructor`_ or `connect()`_\ .

exec()
''''''

The exec :green:`Function` executes a sql statement on the database opened
with :ref:`connection() <connection_const>`.  It takes a :green:`String` containing a sql
statement and an optional :green:`Object` or :green:`Array` of sql
parameters, an optional :green:`Object` of options and an optional callback
:green:`Function`.  The parameters may be specified in any order.

.. code-block:: javascript

    var res = sql.exec(statement [, options] [, sql_parameters] [, callback]);

+--------------+------------------+--------------------------------------------------------+
|Argument      |Type              |Description                                             |
+==============+==================+========================================================+
|statement     |:green:`String`   | The sql statement                                      |
+--------------+------------------+--------------------------------------------------------+
|options       |:green:`Object`   | Options (skipRows, maxRows, returnType, includeCounts  |
|              |                  | returnRows and arg) *as described below*               |
+--------------+------------------+--------------------------------------------------------+
|sql_parameters|:green:`Array`    | ``?`` substitution parameters                          |
+              +------------------+--------------------------------------------------------+
|              |:green:`Object`   | ``?named`` substitution parameters                     |
+--------------+------------------+--------------------------------------------------------+
|callback      |:green:`Function` | a function to handle data one row at a time.           |
+--------------+------------------+--------------------------------------------------------+

The arguments after ``statement`` are identified by type and may be given
in any order.

Statement:
    A statement is a :green:`String` containing a single sql statement to be
    executed.  A trailing ``;`` (semicolon) is optional.  Example:

.. code-block:: javascript

    var res = sql.exec(
        "select * from employees where Salary > 50000 and Start_date < '2018-12-31'"
    );

Note that concatenating statements separated by ``;`` is not supported in
JavaScript, and as such, a script must use a separate ``exec()`` for each
statement to be executed.

.. _sql_params:

SQL Parameters:
    SQL Parameters are specified in an :green:`Array` with each member
    corresponding to each ``?`` in the SQL statement. Alternatively parameters
    can be named in an :green:`Object` with each value in the
    :green:`Object` corresponding to each ``?key_name`` in the SQL
    statement.

    Example:

.. code-block:: javascript

    var res = sql.exec(
        "select * from employees where Salary > ? and Start_date < ?",
        [50000, "2018-12-31"]
    );

    /* or */

    var res = sql.exec(
        "select * from employees where Salary > ?salary and Start_date < ?date",
        { salary: 50000, date: "2018-12-31"}
    );

The use of Parameters can make the handling of user input safe from
`SQL injection <https://en.wikipedia.org/wiki/SQL_injection>`_\ .
Note that if there is only one parameter, it still must be contained in an
:green:`Array` or :green:`Object`.

.. _execopts:

Options:
 The ``options`` :green:`Object` may contain any of the following:

   * ``maxRows`` (:green:`Number`):  maximum number of rows to return (default: 10
     for ``select`` statements; unlimited (``-1``) for others).  See Caveats
     below.

   * ``skipRows`` (:green:`Number`): the number of rows to skip (default: 0).

   * ``returnType`` (:green:`String`): Determines the format of the ``rows`` value
     in the return :green:`Object`.

      * **default**: if ``returnType`` is not set, ``rows`` in
	the return value of ``select`` statements will be an :green:`Array`
        of :green:`Objects`, as if ``"object"`` below was set.  For
        ``delete``, ``update`` and ``insert`` statements, ``rows`` will
        be an empty array as if ``"novars"`` was set.

      * ``"object"``: An :green:`Array` of :green:`Objects`.  Each
        :green:`Array` member  corresponds to each row fetched. Each
        :green:`Object` has its property names (keys) set to the names of the
        corresponding column and its values set to the field value of the
        corresponding row for the named column.

      * ``"array"``: An :green:`Array` of :green:`Arrays`. The outer :green:`Array`
        members correspond to each row fetched.  The inner :green:`Array`
        members correspond to the fields returned in each row.  Note that
        column names are still available, in order, in :ref:`columns <returnval>`.

      * ``"novars"``: An empty :green:`Array` is returned.  The sql statement is
        still executed.  This is the default for inserts, updates and deletes
        where the return value would normally not be used.

      * **Note**: If the values of a deleted, inserted or updated row are needed,
        ``returnType`` can be set to either ``"object"`` or ``"array"`` and
        the statement will be executed as normal with ``rows`` set as if
        the row or rows operated upon were ``select``\ ed.

   * ``returnRows`` (:green:`Boolean`): If set ``true``, performs the same
     function as ``{returnType: "object"}`` above.  If set ``false``,
     performs the same function as ``{returnType: "novars"}`` above.  This
     If both are present, the ``returnType`` setting takes precedence.

   * ``includeCounts`` (:green:`Boolean`): whether to include count
     information in the return :green:`Object`.  Default is ``false``.  The
     information will be returned as an :green:`Object` in the
     ``sql.exec()`` return :green:`Object` as the value of the key
     ``countInfo`` (or as the fourth parameter to a callback :green:`Function`).  The
     :green:`Numbers` returned will only be useful when performing a
     `text search <https://docs.thunderstone.com/site/texisman/intelligent_text_search.html>`_ on a field
     with a fulltext index.  If count information is not available, the
     :green:`Numbers` will be negative.  See :ref:`countInfo <countinfo>`
     below.  If ``false``, ``countInfo`` will be ``undefined``.

   * ``argument``: (aka ``arg``). A variable of any type to be passed to the
     callback below.

Caveats for Options, maxRows and skipRows:
   *  SQL ``select`` statements are by default limited to 10
      rows (``{maxRows:10}``) unless ``maxRows`` above is set.  This default
      can be changed by setting the special variable ``sql.selectMaxRows``.

     Example:

     .. code-block:: javascript

        var Sql = require("rampart-sql");

        var sql = new Sql.connection("./mytestdb");

        sql.selectMaxRows=20;

        var res = sql.exec("select * from mytable");
        /* expected results: 20 rows, if 20 are available from "mytable" */


   *  ``maxRows`` defaults to ``-1`` (unlimited) if not set and the
      SQL statement is not a ``select`` statement.

   *  ``maxRows`` and ``skipRows`` may be specified, as a shortcut, as
      parameters to the exec function.  Placement of the :green:`Numbers` in the
      ``exec()`` function is arbitrary, except that the first number given
      will be treated as ``maxRows`` and the second, if present will be
      treated as ``skipRows``.  Also note that if ``maxRows`` and/or
      ``skipRows`` is also set in ``options`` above, the last set value will be
      used.

      Example:

     .. code-block:: javascript

        var Sql = require("rampart-sql");

        var sql = new Sql.connection("./mytestdb");

        var sqlopts = {maxRows: 5, returnType: "array"};

        var res = sql.exec(20, 10, "select * from mytable");
        /* expected results: 20 rows, skipping the first 10,
           if 30 are available from "mytable"                */

        var res = sql.exec("select * from mytable", 20, 10, sqlopts);
        /* expected results: 5 rows, skipping the first 10,
           if 15 are available from "mytable".  The option maxRows
           is specified last from within "sqlopts", so it is used       */

        var res = sql.exec("select * from mytable", sqlopts, 20, 10);
        /* expected results: 20 rows, skipping the first 10,
           if 30 are available from "mytable".  The parameter 20 is
           specified last, so maxRows is overwritten and 20 is used     */

Callback:
   A :green:`Function` taking as parameters (``result_row``, ``index``, ``columns``, ``countInfo``, ``user_argument``).
   The callback is executed once for each row retrieved:

   * ``result_row``: (:green:`Array`/:green:`Object`): depending on the setting of ``returnType``
     in ``Options`` above, a single row is passed to the callback as an
     :green:`Object` or an :green:`Array`.

   * ``index``: (:green:`Number`) The ordinal number of the current search result.

   * ``columns``: an :green:`Array` corresponding to the column names or
     aliases selected and returned in results.

   * ``countInfo``: an :green:`Object` as described below in `countinfo`_ if the
     ``includeCounts`` option is set ``true``.  Otherwise an empty
     :green:`Object` is passed.  Note that this differs from the return value
     of ``exec()``, where the ``countInfo`` property is genuinely absent when
     ``includeCounts`` is not set; a callback testing ``if(countInfo)`` will
     always see a truthy value, so test a property such as
     ``countInfo.indexCount`` instead.

   * ``user_argument``: a variable that is supplied to the callback after
     being set in the :ref:`options <execopts>` ``argument``
     option above.  If not set above, ``undefined`` will be passed as the
     fifth argument.

   * Note: Regardless of ``maxRows`` setting , returning ``false`` from the
     ``callback`` will cancel the retrieval of any remaining rows.
     Returning ``undefined`` or any other value will allow the next row to be
     retrieved up to ``maxRows`` rows.

   Example

   .. code-block:: javascript

      var nrows = sql.exec(
          "select NAME, REMARK from SYSTABLES", // statement
          { returnType:"array"},                // options
          function(row, i, cols) {              // callback
              if(i==0)
                  rampart.utils.printf("%s\n", cols.join(" "));
              rampart.utils.printf("%s\n", row.join(" "));
          }
      );

      console.log("nrows =", nrows);

      /* expected output:
          NAME REMARK
          SYSCOLUMNS Catalog of Columns
          SYSTABLES Catalog of Tables
          SYSINDEX Catalog of Indices
          SYSUSERS Texis Users
          SYSPERMS Texis Permissions
          SYSTRIG Texis Triggers
          SYSMETAINDEX Chkind Parameters
          SYSSTATISTICS Database Statistics
          nrows = 8
      */

.. _returnval:

Return Value:
	:green:`Number`/:green:`Object`.

        With no callback, an :green:`Object` is returned.  The :green:`Object` contains
	three or four key/value pairs.
	
	Key: ``rows``; Value: an :green:`Array` of :green:`Objects`.
	Each :green:`Object` corresponds to a row in the database and will
	have keys set to the corresponding column names and the values set
	to the corresponding field of the retrieved row.  If ``returnType``
	is set to ``"array"``, an :green:`Array` of :green:`Arrays`
	containing the values (one inner :green:`Array` per row) will be
	returned.
	
	Key: ``rowCount``; Value: a :green:`Number` corresponding to the number of rows
	returned.

	Key:  ``columns``; Value: an :green:`Array` corresponding to the column names or
	aliases selected and returned in rows.

.. _countinfo:

  Key: ``countInfo``; Value: if option ``includeCounts`` is set
  ``true``, information regarding the number of total possible matches
  is set.  Otherwise ``countInfo`` is undefined.  When performing a
  `text search <https://docs.thunderstone.com/site/texisman/intelligent_text_search.html>`_
  the ``countInfo`` :green:`Object` contains the following:

   * ``indexCount`` (:green:`Number`): a single value estimating the number
     of matching rows (for a vector search, see below).

   * ``rowsMatchedMin`` (:green:`Number`): Minimum number of rows matched **before**
     any `group by <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ ,
     :ref:`sql-set:likeprows`,
     :`aggregates <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ , or
     :ref:`sql-set:multivaluetomultirow` are applied.

   * ``rowsMatchedMax`` (:green:`Number`): Maximum number of rows matched **before**
     any `group by <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ ,
     :ref:`sql-set:likeprows`,
     :`aggregates <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ , or
     :ref:`sql-set:multivaluetomultirow` are applied.

   * ``rowsReturnedMin`` (:green:`Number`): Minimum number of rows matched **after**
     any `group by <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ ,
     :ref:`sql-set:likeprows`,
     :`aggregates <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ , or
     :ref:`sql-set:multivaluetomultirow` are applied.

   * ``rowsReturnedMax`` (:green:`Number`): Maximum number of rows matched **after**
     any `group by <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ ,
     :ref:`sql-set:likeprows`,
     :`aggregates <https://docs.thunderstone.com/site/texisman/summarizing_values.html>`_\ , or
     :ref:`sql-set:multivaluetomultirow` are applied.

  **Vector searches** (``LIKEV``, including a fused ``LIKEP OR LIKEV``):
  the ``rowsMatched``/``rowsReturned`` values are ``-1``/``-2``
  (unknown).  Every row has some similarity to the query, so there is no
  match set to count; the index returns a candidate pool capped by
  :ref:`likevRows <sql-set:likevRows>` (default ``1000``).
  ``indexCount`` stays valid and is that pool -- keyword hits plus up to
  ``likevRows`` candidates -- so it is a retrieval budget, not a result
  total, and changes if you change ``likevRows``.

  If a callback :green:`Function` is specified, a :green:`Number` (the
  number of rows retrieved) is returned.  The callback is given the above
  values as arguments in the following order: ``cbfunc(result_row, index,
  columns, countInfo)``.

  Note also that if ``includeCounts`` is set ``true`` and the sql query is
  not a text search, the values of the properties of ``countInfo`` will be
  negative.

.. _errormsgs:

Error Messages:
   All errors throw a JavaScript exception.  Errors are also copied to
   the special variable ``sql.errMsg``.

   Error Message Example:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   /* create database if it does not exist */
   var sql = new Sql.connection("./mytestdb",true);

   /* create a table */
   sql.exec("create table testtb (text varchar(16), number double)");

   try {
   	sql.exec("insert into testtb values ('D E F', 456, 789)");
   } catch (e) {
   	console.log(e);
   }
   /* output =
       "Error: sql prep error: 100 More Values Than Fields in the function: Insert
        000 SQLPrepare() failed with -1: An error occurred in the function: texis_prepare"
      sql.errMsg is similar.
   */

   try {
       sql.exec("select Nonexistent from testtb", function(row,i){
           console.log(i, row);
       });
   } catch (e) {
        console.log(e);
   }

   /* output =
        Error: sql prep error: 115 Field `Nonexistent' non-existent
        115 Field non-existent or type error in `Nonexistent'
        000 SQLPrepare() failed with -1: An error occurred in the function: texis_prepare
      sql.errMsg is similar.
   */



Exec Full Example
"""""""""""""""""

Below is a full example of ``exec()`` functionality:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   /* create database if it does not exist */
   var sql = new Sql.connection("./mytestdb",true);

   /* check if table exists */
   var res = sql.exec(
       "select * from SYSTABLES where NAME='employees'",
       {"returnType":"novars"} /* we only need the count */
   );

   if(res.rowCount) /* 1 if the table exists */
   {
       /* drop table from previous test run of this script */
       res=sql.exec("drop table employees");
   }

   /* (re)create the table */
   sql.exec(
           "create table employees (Classification varchar(8), " +
           "Name varchar(16), Age int, Salary int, Title varchar(16), " +
           "Start_date date, Bio varchar(128) )",
           {"returnType":"novars"}
   );

   /* populate variables for insertion */
   var emp1 = {
     cl:     "principal",
     name:   "Debbie Dreamer",
     age:    63,
     title:  "Chief Executive Officer",
     start:  '1999-12-31',
     salary: 250000,
     bio:    "Born and raised in Manhattan, New York. U.C. Berkeley graduate. " +
             "Loves to skydive. Built Company from scratch. Still uses word-perfect.",
   }

   var emp2 = {
     cl:     "principal",
     name:   "Rusty Grump",
     age:    58,
     title:  "Chief Financial Officer",
     start:  '1999-12-31', // Strings are converted to local time
     salary: 250000,
     bio:    "Born in Switzerland, raised in South Dakota. Columbia graduate. " +
             "Financed operation with inheritance. Has no sense of humor.",
   }

   var emp3 = {
     cl:     "salary",
     name:   "Georgia Geek",
     age:    44,
     title:  "Lead Programmer",
     start:  '2001-3-15',
     salary: 100000,
     bio:    "Stanford graduate. Enjoys pizza and beer. Proficient in Perl, COBOL," +
             "FORTRAN and IBM System/360",
   }

   var emp4 = {
     cl:     "salary",
     name:   "Sydney Slacker",
     age:    44,
     title:  "Programmer",
     start:  new Date('2002-5-12T00:00:00.0-0800'), // Dates are UTC unless offset is given.
     salary: 100000,
     bio:    "DeVry University graduate. Enjoys a good nap. Proficient in Python, " +
             "Perl and JavaScript",
   }

   var emp5 = {
     cl:     "hourly",
     name:   "Pat Particular",
     age:    32,
     title:  "Systems Administrator",
     start:  new Date('2003-7-14'),
     salary: 80000,
     bio:    "Lincoln High School graduate. Self taught Linux and windows administration skills. Proficient in " +
             "Bash and GNU utilities. Capable of crashing or resurrecting machines with a single ping.",
   }

   var emp6 = {
     cl:     "intern",
     name:   "Billie Barista",
     age:    22,
     title:  "Intern",
     start:  new Date('2020-3-18'),
     salary: 0,
     bio:    "Harvard graduate, full ride scholarship, top of class.  Proficient in C, C++, " +
             "Rust, Haskell, Node, Python. Into skydiving. Makes a mean latte."
   }

   var employees = [ emp1, emp2, emp3, emp4, emp5, emp6 ];

   /* insert rows */
   for (var i=0; i<employees.length; i++)
   {
       sql.exec(
           "insert into employees values(?cl,?name,?age,?salary,?title,?start,?bio)",
           employees[i]
       );
   }

   /* create text index */
   sql.exec("create fulltext index employees_Bio_text on employees(Bio)");

   /* perform some queries */
   res=sql.exec("select Name, Age from employees");
   rampart.utils.printf('%3J\n', res);
   /* expected output:
      {
          "columns": [
              "Name",
              "Age"
          ],
          "rows": [
              {
                  "Name": "Debbie Dreamer",
                  "Age": 63
              },
              {
                  "Name": "Rusty Grump",
                  "Age": 58
              },
              {
                  "Name": "Georgia Geek",
                  "Age": 44
              },
              {
                  "Name": "Sydney Slacker",
                  "Age": 44
              },
              {
                  "Name": "Pat Particular",
                  "Age": 32
              },
              {
                  "Name": "Billie Barista",
                  "Age": 22
              }
          ],
          "rowCount": 6
      }
   */

   res=sql.exec(
       "select Name, Age from employees",
       {returnType:'array', maxRows:2, includeCounts:true}
   );
   rampart.utils.printf('%3J\n', res);
   /* expected output:
      {
          "columns": [
              "Name",
              "Age"
          ],
          "rows": [
              [
                  "Debbie Dreamer",
                  63
              ],
              [
                  "Rusty Grump",
                  58
              ]
          ],
          "countInfo": {
              "indexCount": -1,
              "rowsMatchedMin": -1,
              "rowsMatchedMax": -2,
              "rowsReturnedMin": -1,
              "rowsReturnedMax": -2
          },
          "rowCount": 2
      }
                Note that countInfo values are all negative since no
                text search was performed.
   */
   res=sql.exec(
       "select Name from employees where Bio likep 'proficient' and Salary > 50000",
        {includeCounts:true}
   );
   rampart.utils.printf('%3J\n', res);

   /* expected output:
      {
          "columns": [
              "Name"
          ],
          "rows": [
              {
                  "Name": "Georgia Geek"
              },
              {
                  "Name": "Sydney Slacker"
              },
              {
                  "Name": "Pat Particular"
              }
          ],
          "countInfo": {
              "indexCount": 4,
              "rowsMatchedMin": 0,
              "rowsMatchedMax": 4,
              "rowsReturnedMin": 0,
              "rowsReturnedMax": 4
          },
          "rowCount": 3
      }
      Note that indexCount is the count before "Salary > 50000" filter
   */

   /* skydive => skydiving */
   sql.set({
       minwordlen: 5,
       suffixproc: true
   });

   nrows=sql.exec(
       "select Name, Salary from employees where Bio likep 'skydive' order by Salary desc",
       {returnType:"array", includeCounts:true},
       function (row, i, coln, cinfo) {
           if(!i) {
               console.log(
                  "Total approximate number of matches in db: " +
                  cinfo.indexCount
               );
               console.log("-", coln);
           }
           console.log(i+1,row);
       }
   );
   console.log("Total: " + nrows); // 2

   /* expected output:
      Total approximate number of matches in db: 2
      - ["Name","Salary"]
      1 ["Debbie Dreamer",250000]
      2 ["Billie Barista",0]
      Total: 2
   */

query()
'''''''

``sql.query()`` performs in the same manner as `exec()`_\ , except it will not throw JavaScript errors
when there are sql errors that would otherwise do so in `exec()`_ (see `Error Messages <rampart-sql.html#errormsgs>`_
above).
Instead, the errors are only reported in ``sql.errMsg``.  Note that when opening a database, 
or using other sql functions, Javascript errors still may be thrown and other non-error messages 
might appear in ``sql.errMsg``.

Example:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   /* create database if it does not exist, creation message written */
   var sql = Sql.connect("./mytestdb",true);
   if(sql.errMsg.length) console.log(sql.errMsg);

   /* create a table, no error */
   sql.query("create table testtb (text varchar(16), number double)");
   if(sql.errMsg.length) console.log("err =", sql.errMsg);

   /* create a unique index on number, no error */
   sql.query("create unique index testtb_number_ux on testtb(number)");
   if(sql.errMsg.length) console.log("err =", sql.errMsg);

   /* insert a row, no error */
   sql.query("insert into testtb values ('A B C', 123)");
   if(sql.errMsg.length) console.log("err =", sql.errMsg);

   /* attempt to insert a duplicate, sql.errMsg will be set */
   sql.query("insert into testtb values ('D E F', 123)");
   if(sql.errMsg.length) console.log("err =", sql.errMsg);

   /* attempt to insert 3 values into two columns, sql.errMsg will be set */
   sql.query("insert into testtb values ('D E F', 456, 789)");
   if(sql.errMsg.length) console.log("err =", sql.errMsg);

   /* attempt to select a non-existent column, returns -1 and sql.errMsg will be set */
   var res = sql.query("select Nonexistent from testtb", function(row,i){
       console.log(i, row);
   });

   console.log("res =",res)
   console.log("err =", sql.errMsg);


   /* output = 
      100 User _SYSTEM has been added to database ./mytestdb without a password
      100 User PUBLIC has been added to database ./mytestdb without a password

      err = 178 Trying to insert duplicate value (123) in index ./mytestdb/testtb_number_ux.btr

      err = 100 More Values Than Fields in the function: Insert
      000 SQLPrepare() failed with -1: An error occurred in the function: texis_prepare

      res = -1
      err = 115 Field `Nonexistent' non-existent
      115 Field non-existent or type error in `Nonexistent'
      000 SQLPrepare() failed with -1: An error occurred in the function: texis_prepare

   */


.. remove this?
    eval()
    ~~~~~~

    The ``eval`` :green:`Function` is a shortcut for executing sql
    :ref:`sql-server-funcs:Server functions` where
    only one computed result is desired.

    With ``exec()``, this:

    .. code-block:: javascript

       var Sql = require("rampart-sql");

       var sql = new Sql.connection("/path/to/my/db", true);

       var res1 = sql.exec("select joinpath('one', 'two/', '/three/four', 'five') newpath");
       var res=rows1.rows[0];
       console.log(res); /* {newpath:"one/two/three/four/five"} */

    can be more easily written as:

    .. code-block:: javascript

       var Sql = require("rampart-sql");
       var sql = new Sql.connection("/path/to/my/db", true);

       var res = sql.eval("joinpath('one', 'two/', '/three/four', 'five') newpath");
       console.log(res); /* {newpath:"one/two/three/four/five"} */

    See :ref:`sql-server-funcs:Server functions` for a complete list of Server
    functions.

one()
'''''

The ``one`` :green:`Function` is a shortcut for executing sql
where only one row is desired and the extra information normally
returned from `exec()`_ is not needed.  It throws a JavaScript
exception in the same manner as `exec()`_\ .

Usage:

.. code-block:: javascript

    var res = sql.one(statement [, sql_parameters]);

This allows:

.. code-block:: javascript

   var res = sql.exec("select email from Users where user=?user", {maxRows:1}, {user:user_name});
   var row=res.rows[0];
   /* row = { email : "user@example.com" } */

to be more easily written as:

.. code-block:: javascript

   var row = sql.one("select email from Users where user=?user",{user:user_name});
   /* row = { email : "user@example.com" } */

Note: ``one`` returns ``undefined`` if a matching row is not found. It thus
can be used in an ``if`` statement to test the existence of a row:

.. code-block:: javascript

   if(! sql.one("select email from Users where user=?", [user_name]) )
      console.log("user " + user_name + " does not exist in the database.");

set()
'''''

The ``set`` :green:`Function` sets Texis server properties.  For a full listing, see
:ref:`sql-set:Server Properties`.  Arguments are given as keys with
corresponding values set to a :green:`String`, :green:`Number`, :green:`Array` or
:green:`Boolean` as appropriate.  Note that :green:`Booleans`
``true``/``false`` are equivalent to setting ``1``/``0``
as described in :ref:`sql-set:Server Properties`.

Normally there is no return value (``undefined``).

However if :ref:`sql-set:lstexp`,
:ref:`sql-set:lstindextmp`, :ref:`sql-set:listPrefix`,
:ref:`sql-set:listSuffix`, :ref:`sql-set:listSuffixEquivs`,  and/or
:ref:`sql-set:listNoise` is set ``true``, an :green:`Object` is
returned with corresponding keys ``expressionsList``, ``indexTempList``,
``prefixList``, ``suffixList``, ``suffixEquivsList`` and/or
``noiseList`` respectively.

Example:

.. code-block:: javascript

        /* rank higher docs with words appearing at beginning of document *
         *  and only return matches with all the given query terms.       */
	sql.set({
		likepleadbias: 750,
		likepallmatch: true
	});

	/* an example with a return value.  addExp appends to the
	   expression list: the default word expression stays, and code-ish
	   tokens (emails, flags, identifiers) are additionally indexed. */
	var lists = sql.set({
		addExp: "[\\alnum\\$%@\\-_\\+]{2,99}",
		addIndexTmp: ["/tmp","/var/tmp"],
		listNoise: true,
		listIndextemp: true,
		listExpressions: true
	});
	/*
	   lists =
	   {
	   	noiseList:        ["a","about",...,"you","your"],
	   	indexTempList:    ["/tmp","/var/tmp"],
	   	expressionsList:  ["[\\uword]{2,99}", "[\\alnum\\$%@\\-_\\+]{2,99}"]
	   }
	*/

get()
'''''

Return the settings this ``sql`` handle will apply.  With no argument it
returns an :green:`Object` of every setting, keyed by the same names
`set()`_ takes, so the result can be passed straight back to `set()`_ to
restore them.  With a :green:`String` property name it returns that one
value.

.. code-block:: javascript

    var all   = sql.get();              // every setting
    var rows  = sql.get("likevRows");   // 1000

    var saved = sql.get();              // snapshot, change, restore
    sql.set({likepRows: 500});
    sql.set(saved);

See :ref:`Reading Current Settings <sql-set:Reading Current Settings>` for
what is and is not included.

reset()
'''''''

Reset all settings set with `set()`_ above to their original default values.

Example:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   var sql = new Sql.connection("/path/to/my/db");

   ...

   sql.set({...});  //settings changed in script

   ...

   sql.reset(); //reset all to default

importCsvFile()
'''''''''''''''

The importCsvFile :green:`Function` imports data from a csv
formatted file 
into a SQL table.  It takes a :green:`String` containing a file name, an
:green:`Object` of options, optionally an :green:`Array` specifying the
order of columns and optionally a callback :green:`Function`.  The
parameters may be specified in any order.

Usage:

.. code-block:: javascript

    var res = sql.importCsvFile(filename, options [, ordering] [, callback]);

+--------------+------------------+---------------------------------------------------+
|Argument      |Type              |Description                                        |
+==============+==================+===================================================+
|filename      |:green:`String`   | The csv file to import                            |
+--------------+------------------+---------------------------------------------------+
|options       |:green:`Object`   | Options *described below*                         |
+--------------+------------------+---------------------------------------------------+
|ordering      |:green:`Array`    | Order of csv columns to table columns             |
+--------------+------------------+---------------------------------------------------+
|callback      |:green:`Function` | a function to monitor the progress of the import. |
+--------------+------------------+---------------------------------------------------+

filename:
    The name of the csv file to be opened.

options:
    The ``options`` :green:`Object` may contain any of the following.

      * ``tableName`` - :green:`String` (no default; **required**) -
        The name of the table into which the csv data will be inserted.

      * ``callbackStep`` - :green:`Number` - Where number is ``n``, execute
        callback, if provided, for every nth row imported.

      * ``stripLeadingWhite`` -  :green:`Boolean` (default ``true``):
        Remove leading whitespace characters from cells.

      * ``stripTrailingWhite`` - :green:`Boolean` (default ``true``): Remove
        trailing whitespace characters from cells.

      * ``doubleQuoteEscape`` -  :green:`Boolean` (default ``false``):
        ``""`` within strings is used to embed ``"`` characters.

      * ``singleQuoteNest`` -  :green:`Boolean` (default ``true``): Strings
        may be bounded by ``'`` pairs and ``"`` characters within are ignored.

      * ``backslashEscape`` -  :green:`Boolean` (default ``true``):
        Characters preceded by '\\' are translated and escaped.

      * ``allEscapes`` -  :green:`Boolean` (default ``true``): All ``\``
        escape sequences known by the 'C' compiler are translated, if
        ``false`` only backslash, single quote, and double quote are escaped.

      * ``europeanDecimal``  -  :green:`Boolean` (default ``false``):
        Numbers like ``123 456,78`` will be parsed as ``123456.78``.

      * ``tryParsingStrings`` -  :green:`Boolean` (default ``false``): Look
        inside quoted strings for dates and numbers to parse, if ``false``
        anything quoted is a string.

      * ``delimiter`` - :green:`String` (default ``","``):  Use the first
        character of string as a column delimiter (e.g ``\t``).

      * ``timeFormat`` -  :green:`String` (default ``"%Y-%m-%d %H:%M:%S"``):
        Set the format for parsing a date/time. See manpage for
        `strptime() <https://man7.org/linux/man-pages/man3/strptime.3p.html>`_.

      * ``hasHeaderRow`` - -  :green:`Boolean` (default ``false``): Whether
        to treat the first row as column names. If ``false``, the first row
        is imported as csv data and the column names will
        default to ``col_1, col_2, ..., col_n``.

      * ``normalize`` - :green:`Boolean` (default ``true``): If ``true``,
        examine each column in the parsed CSV object to find the majority
        type of that column.  It then casts all the members of that column
        to the majority type, or set it to ``null`` if it is
        unable to do so. If ``false``, each cell is individually normalized.
	NOTE: unlike the
	:ref:`rampart.import.csvFile <rampart-main:csvFile>` :green:`Function`,
	the default is ``true``.

      * ``progressFunc`` - :green:`Function`: A function to monitor the progress
        of the passes over the csv data.  It takes as arguments ``function (i, stage)``
        The variable ``stage`` is ``0`` for the initial counting of rows, ``1`` for the parsing
        of the cells in each row and ``2+`` optionally if ``normalize`` is ``true`` for the
        two stages of the analysis of each column in the csv (e.g. ``2`` for column 0 first pass,
        ``3`` for column 0 second pass, etc.).  The variable ``i`` is the row number.

      * ``progressStep`` :green:`Number`: Where number is ``n``, execute
        ``progressFunc`` callback, if provided, for every nth row in each stage.


ordering:
   An :green:`Array` of :green:`Strings` or :green:`Numbers` corresponding
   to the csv columns, listed in the order of insertion into the table.
   Example: If ``[0,3,4]`` is specified, the first, fourth and fifth column
   in the csv will be inserted into the first, second and third column of
   SQL table.  ``-1`` can be used to insert a ``0`` or blank string (``""``)
   in that position in each row of the SQL table.  Also a :green:`String`
   corresponding to the csv column name may be used in place of a number.

callback:
   A :green:`Function` taking as its sole parameter (``index``), the
   current ``0`` based row being imported.
   The callback is executed once for each row in the csv file unless the
   option ``callbackStep`` is specified.

Return Value:
	:green:`Number`. The return value is set to number of rows in the
	csv file.

Note: In the callback, the loop can cancel the import at any point by returning
``false``.  The return value (number of rows) will still be the total number
of rows in the csv file.

Example:

.. code-block:: javascript

   var ret=sql.importCsvFile(
      /* csv file to import */
      "sample.csv",

      /* options */
      {
         tableName:"testtb", /* table in which to insert csv data */
         callbackStep: 1000, /* do callback every 1000th row      */
         hasHeaderRow: true, /* first row of csv are column names */
      },

      /* reorder csv columns switching second and third */
      [0,2,1],

      /* print progress */
      function(i){
         console.log(i);
      }
   );

   console.log("total="+ret);

   /* expected output for 10000 row csv:
   1000
   2000
   ...
   9000
   total=10000
   */

importCsv()
'''''''''''

Same as `importCsvFile()`_ except instead of a file name, a :green:`String` or
:green:`Buffer` containing the csv data is passed as a parameter.

Example:

.. code-block:: javascript

   var Sql=require("rampart-sql");
   var sql= new Sql.connection("/path/mytestdb");

   var csv =
   "Dept,       item1 Quantity, item1 Description, item1 Value, item2 Quantity, item2 Description, item2 Value\n" +
   "accounting, 5,              Macbook Pro,       1200.0,      300,            Pencils,           0.1\n" +
   "marketing,  20,             Dell XPS 15,       1150.0,      350,            Pens,              0.5\n" +
   "logistics,  30,             iPad Air,          300.0,       100,            Duktape,           1.5\n"

   /* note this table has more rows than the csv*/
   sql.exec("create table company_assets(Department varchar(16), "+
              "Num_item1 int, Desc_item1 varchar(16), Val_item1 float, Tot_Val_item1 float, " +
              "Num_item2 int, Desc_item2 varchar(16), Val_item2 float, Tot_Val_item2 float, " +
              "Tot_Val_items float);");

   /* import the csv data */
   sql.importCsv(
      csv,
      {
          tableName: "company_assets",
          hasHeaderRow: true
      },
      /*
         order of insertion. Can be column name or column number
         "" or -1 means insert a null value (0, 0.0 or "")
      */
      [
         "Dept",
         "item1 Quantity", "item1 Description", "item1 Value", -1,
         "item2 Quantity", "item2 Description", "item2 Value", -1,
          -1
      ]
   );

   /* update rows that defaulted to 0*/
   sql.exec("update company_assets set Tot_Val_item1 = ( Num_item1 * Val_item1 )");
   sql.exec("update company_assets set Tot_Val_item2 = ( Num_item2 * Val_item2 )");
   sql.exec("update company_assets set Tot_Val_items = ( Tot_Val_item1 + Tot_Val_item2 )");

   /* print the rows */
   sql.exec("select * from company_assets", {returnType:'array'},function(row,i,cols) {
       if( i==0)
           console.log("-", cols);
       console.log(i, row);
   });

   /* output:
   - ["Department","Num_item1","Desc_item1","Val_item1","Tot_Val_item1","Num_item2","Desc_item2","Val_item2","Tot_Val_item2","Tot_Val_items"]
   0 ["accounting",5,"Macbook Pro",1200,6000,300,"Pencils",0.10000000149011612,30,6030]
   1 ["marketing",20,"Dell XPS 15",1150,23000,350,"Pens",0.5,175,23175]
   2 ["logistics",30,"iPad Air",300,9000,100,"Duktape",1.5,150,9150]
   */

addTable()
''''''''''

Add a table to the current database.

Usage:

.. code-block:: javascript

   var sql = new Sql.connection('/path/to/db');

   sql.addTable(path);

Where ``path`` is the path to the ``*.tbl`` file from another database.
This file should be a copy and upon adding, may not exist in more than one
database.  The file will not be copied and used as is from ``path``.

Note:
   Any existing indexes on the added table that existed in the old database
   will need to be recreated.

See also: :ref:`The addtable command line utility <sql-utils:The addtable Command Line Utility>`.

close()
'''''''

In general it is not necessary to use ``close()`` as the "connection" to the
database is not over a socket.  However, if resources to a database are no
longer needed, ``close()`` will clean up some of those resources.  Note that
even after calling ``sql.close()``, using the ``sql.*`` :green:`Functions`
will re-open handles to the database and continue to operate as expected and
in the same manner as when the "connection" was first opened.

Column Data Types
-----------------

Texis supports a broad set of column types. The summary below covers the
types most commonly used from Rampart applications. For the full reference
including precision, range, and conversion rules, see the
`Texis data types documentation <https://docs.thunderstone.com/site/texisman/command_discussion-11.html>`_\ .

A note on the ``var`` prefix
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For most types, prefixing the type name with ``var`` makes it variable
length. The size parameter behaves differently in each form:

* On a fixed-length type — ``char(N)``, ``byte(N)`` — ``N`` is the
  exact per-row size. Shorter values are padded; longer values are
  truncated.
* On a variable-length type — ``varchar(N)``, ``varbyte(N)``,
  ``varlong(N)``, ``varint64(N)``, ``varuint64(N)`` — ``N`` is a
  *hint*, not a limit. It controls only the initial in-memory
  buffer size allocated for the field when rows are read; if an
  incoming row's data exceeds ``N``, the buffer is resized
  automatically. ``N`` does not affect on-disk storage (values are
  always stored at their actual length), and inserts are never
  rejected for exceeding it.

  Practical guidance: pick ``N`` close to the *typical* (not
  maximum) row size. Setting it too small wastes CPU on
  reallocations during row reads; setting it too large wastes RAM
  on oversized buffers in memory.

For vector types, ``vec*`` and ``varvec*`` are aliases that both
resolve to the same variable-length representation internally; the
``(N)`` size parameter is parsed but advisory.

Character and text
~~~~~~~~~~~~~~~~~~

.. list-table::
   :widths: 22 78
   :header-rows: 1

   * - Type
     - Description
   * - ``char(N)`` / ``character(N)``
     - Fixed-length character data, exactly N bytes per row.
   * - ``varchar(N)``
     - Variable-length text; N is a typical-size hint, not a limit.
   * - ``strlst``
     - Delimiter-separated string list; supports set-style operators in SQL.

Binary and blob
~~~~~~~~~~~~~~~

.. list-table::
   :widths: 22 78
   :header-rows: 1

   * - Type
     - Description
   * - ``byte(N)``
     - Fixed-length binary, exactly N bytes per row.
   * - ``varbyte(N)``
     - Variable-length binary; N is a typical-size hint, not a limit.
   * - ``blob``
     - Binary Large Object stored out-of-line; for large text, images, audio.
   * - ``blobz``
     - Compressed BLOB variant.
   * - ``indirect``
     - Stores a filename; the actual data lives outside the database.

Integer
~~~~~~~

.. list-table::
   :widths: 22 78
   :header-rows: 1

   * - Type
     - Description
   * - ``smallint``
     - 16-bit signed; range ±32,767.
   * - ``int`` / ``integer``
     - 32-bit signed; range ±2,147,483,647.
   * - ``long``
     - Platform-native long (typically 32- or 64-bit signed).
   * - ``varlong(N)``
     - Variable-length encoding of ``long``; N is a size hint.
   * - ``int64``
     - 64-bit signed.
   * - ``varint64(N)``
     - Variable-length encoding of ``int64``; N is a size hint.
   * - ``uint64``
     - 64-bit unsigned.
   * - ``varuint64(N)``
     - Variable-length encoding of ``uint64``; N is a size hint.
   * - ``word`` / ``dword``
     - 16-bit / 32-bit unsigned.

Floating point
~~~~~~~~~~~~~~

.. list-table::
   :widths: 22 78
   :header-rows: 1

   * - Type
     - Description
   * - ``float``
     - 32-bit IEEE-754 single-precision.
   * - ``double``
     - 64-bit IEEE-754 double-precision.

Date and time
~~~~~~~~~~~~~

.. list-table::
   :widths: 22 78
   :header-rows: 1

   * - Type
     - Description
   * - ``date``
     - Seconds since 1970-01-01 UTC; accepts most date/time string forms on
       insert.  A string without a timezone is parsed as system local time;
       values are retrieved in JavaScript as :green:`Date` objects (which
       serialize as UTC/ISO strings).
   * - ``udate``
     - Signed 64-bit count of **microseconds** since 1970-01-01 UTC.  Fixed
       eight bytes on every platform, range approximately +/-292,000 years,
       resolution one microsecond.  Accepts the same date/time string forms
       as ``date`` (including a fractional-seconds part and a timezone
       offset, so the string form of a JavaScript :green:`Date` can be fed
       straight back in), the keyword ``'now'`` (which carries full
       microsecond resolution), a JavaScript
       :green:`Date`, or a :green:`Number` of **seconds** since the epoch
       (integral or fractional -- both mean seconds, so a value that is
       out of range as seconds is rejected with a warning rather than
       silently reinterpreted).  Retrieved in
       JavaScript as a :green:`Date`, which holds whole milliseconds, so
       sub-millisecond precision is **lost on retrieval** -- use
       ``convert(col,'int64')`` (or ``'uint64'``) when the exact
       microsecond value is needed;
       note that this is the one asymmetric conversion -- it *returns*
       microseconds, whereas a :green:`Number` *supplied* to the column is
       seconds, so an ``int64`` value cannot be inserted back unchanged.
       ``convert(col,'double')`` gives seconds and does round-trip.
       Compares, sorts and indexes as an integer.

Identifier and special
~~~~~~~~~~~~~~~~~~~~~~

.. list-table::
   :widths: 22 78
   :header-rows: 1

   * - Type
     - Description
   * - ``counter``
     - 8-byte unique row identifier combining a timestamp and a sequence.
   * - ``recid``
     - Internal row identifier.

Vector
~~~~~~

Vector types store an ordered sequence of numeric elements. They power
``LIKEV`` similarity queries and ``CREATE VECTOR INDEX`` (see
`Vector Indexes`_ below).

.. list-table::
   :widths: 28 14 58
   :header-rows: 1

   * - Type
     - Element width
     - Description
   * - ``varvecF64`` / ``vecF64``
     - 8 bytes (f64)
     - High-precision research / scientific use.
   * - ``varvecF32`` / ``vecF32``
     - 4 bytes (f32)
     - Natural precision; no quantization loss.
   * - ``varvecF16`` / ``vecF16``
     - 2 bytes (f16)
     - Most embedding pipelines (llama.cpp, OpenAI Ada, etc.).
   * - ``varvecBf16`` / ``vecBf16``
     - 2 bytes (bf16)
     - TPU / Google-style pipelines.
   * - ``varvecI8`` / ``vecI8``
     - 1 byte (i8)
     - Reserved; indexing not yet enabled.
   * - ``varvecU8`` / ``vecU8``
     - 1 byte (u8)
     - Reserved; indexing not yet enabled.

A vector value can also be stored in a ``byte(N)`` or ``varbyte(N)``
column as raw bytes.  This is supported for compatibility with existing
tables and for code that produces raw byte buffers, but **a typed
``varvec*`` column is preferred wherever possible**: storage is
byte-identical, and the column itself declares the element type.  A
``varbyte`` column does not — its dtype lives only in a vector index's
parameters (``CREATE VECTOR INDEX`` on it requires
``WITH vec_dtype '<dtype>'``) — with several consequences:

* Inserts are unchecked: any blob is accepted, and a wrong-dtype or
  wrong-dimension vector only surfaces at query time as a silently
  non-matching row.
* The bytes are ambiguous on their own (768 bytes are 384 f16 cells or
  192 f32 cells), so nothing reading the column can interpret a value
  without consulting the index.
* An *unindexed* ``varbyte`` column cannot auto-embed a string
  ``LIKEV`` parameter at all — there is nowhere to learn the element
  type from.  A typed column supports index-less (linear-scan)
  ``LIKEV`` with string parameters.

Database Indexing
-----------------

Regular Indexes
~~~~~~~~~~~~~~~

A Regular Index is an index on a column or columns of a table which aids the
lookup of a matching row.  In general, an index can greatly improve the
performance when ``SELECT``\ ing rows predicated on a "WHERE" clause.

If a table has many millions of rows, and an application will need to
look up rows by a particular column, placing an index on that column will
allow the matching row or rows to be found without having to do a full, linear
scan of every entry.  In this way, an index is much like an index found in
the back of a reference manual or encyclopedia.

In Rampart, `Regular Indexes`, once created, are automatically maintained.
There are several versions and variations of `Regular Indexes`, as listed
below.

Non-Unique Index
''''''''''''''''

A non unique index is an index which may be used on any column or columns
of a table in order to speed up lookup.

The syntax for creating an index is as follows:

.. code-block:: sql

    CREATE INDEX index-name
    ON table-name (column-name [DESC] [, column-name [DESC]] ...)
    [WITH option-name [value] [option-name [value] ...]]

Where:

* ``index-name`` is an arbitrary name for the index.
* ``table-name`` is the name of the table being indexed.
* ``column-name`` is a column in the current table.
* ``DESC`` is an optional flag used to create the index in descending order.  This speeds
  up SQL queries with the ``ORDER BY`` phrase where the order is descending.
* ``option-name`` is an optional option. See
  `Texis Documentation <https://docs.thunderstone.com/site/texisman/available_options.html>`_
  for more information.

Thus if you have a table created with:

.. code-block:: sql

   create table employees (Classification varchar(8),
   Name varchar(16), Age int, Salary int, Title varchar(16),
   Start_date date, Bio varchar(128));

An index that would allow efficient lookup by name when executing
``select * from employees where Name = 'Rusty Grump'`` could be achieved
by creating an index with the following command:

.. code-block:: sql

    create index employees_Name_x on employees(Name);

If the employees table is large, the progress of index creation can be
monitored with the ``option-name`` ``indexmeter`` as such:

.. code-block:: sql

    create index employees_Name_x on employees(Name) with indexmeter 'on';

This will print a progress meter to ``stdout`` as the index is being
created.

Unique Index
''''''''''''

A Unique Index indexes a column just as above, except that duplicate entries
cannot be inserted into the table.

The syntax for creating a unique index is as follows:

.. code-block:: sql

    CREATE UNIQUE INDEX index-name
    ON table-name (column-name [DESC] [, column-name [DESC]] ...)
    [WITH option-name [value] [option-name [value] ...]]

The following example illustrates the properties of a unique index:

.. code-block:: javascript

    var Sql = require("rampart-sql");

    var sql = new Sql.connection("./testdb", true);

    sql.exec("create table people (Name varchar(16), Age int);");

    sql.exec("create unique index people_Name_ux on people(Name)");

    var ret = sql.exec("insert into people values ('John Doe', 32);");

    console.log("First insert:", ret);
    console.log("Error Msg:", sql.errMsg);

    // try to insert duplicate
    ret = sql.exec("insert into people values ('John Doe', 54);");

    console.log("Second Insert:", ret);
    console.log("Error Msg:", sql.errMsg);

    /* output:

       First insert: {rows:[],rowCount:1}
       Error Msg:
       Second Insert: {rows:[],rowCount:0}
       Error Msg: 178 Trying to insert duplicate value (John Doe) in index /home/rampart/testdb/people_Name_ux.btr
    */

Inverted Index
''''''''''''''

An Inverted Index may be used on ``UNSIGNED INT`` or ``DATE`` fields to speed up
``ORDER BY`` operations.  See
`this section <https://docs.thunderstone.com/site/texisman/creating_an_inverted_index.html>`__
of the `Texis Manual <https://docs.thunderstone.com/site/texisman/>`_ for more information.

Fulltext Indexes
~~~~~~~~~~~~~~~~

Fulltext indexes are indexes on text fields which speed up full text searches
using the ``WHERE column-name likep 'keyword keyword'`` syntax.

A Fulltext index is also known as a "Metamorph Inverted Index".
More information can be found
`here <https://docs.thunderstone.com/site/texisman/creating_a_metamorph_index.html>`__
and
`here <https://docs.thunderstone.com/site/vortexman/create_index_with_options.html>`__\ .

Unlike Regular Indexes, Fulltext indexes do not automatically update when
inserting, deleting or updating rows.  However, the ``likep`` search will
still function as normal, new and updated rows will be linearly scanned in
order to find matches.

A Fulltext index may be manually updated at any time
and while the database and index is in use.  See :ref:`Updating A Fulltext
Index <updating-a-fulltext-index>` below.

Creating A Fulltext Index
'''''''''''''''''''''''''

The syntax for creating a Fulltext index is as follows:

.. code-block:: sql

    CREATE FULLTEXT INDEX index-name
    ON table-name (column-name [DESC] [, column-name [DESC]] ...)
    [WITH option-name [value] [option-name [value] ...]]


Assuming the field ``Bio`` in the employees table example above
contains plain English text, the following will create a Fulltext
index on that field.

.. code-block:: sql

    create fulltext index employees_Bio_text on employees(Bio);

If there are several columns which need to be treated as a single
virtual column for Fulltext indexing, the following may be used:

.. code-block:: sql

    create fulltext index employees_NameBio_text on employees(Name\Bio);

Note that in Rampart Javascript, the backslash (``\``) needs to be escaped:

.. code-block:: javascript

    sql.exec("create fulltext index employees_NameBio_text on employees(Name\\Bio);");

Variations of Fulltext Indexes
''''''''''''''''''''''''''''''

Though a ``LIKEP`` search on a ``FULLTEXT`` index (created as described above)
is the most common and most capable version, there are two other versions
of text indexes and
`LIKE <https://docs.thunderstone.com/site/texisman/search_condition_using_like.html>`_
searches which are available:

* Fulltext Index: - Default ``FULLTEXT`` or ``METAMORPH`` index - best used with
  `LIKEP <https://docs.thunderstone.com/site/texisman/relevance_ranking_using_liker.html>`_ -
  see `Texis Documentation for Metamorph Inverted Indexes <https://docs.thunderstone.com/site/texisman/inverted.html>`_\ .

* Compact Index - best used with
  `LIKER <https://docs.thunderstone.com/site/texisman/relevance_ranking_using_liker.html>`_ and
  `LIKE3 <https://docs.thunderstone.com/site/texisman/using_like3_for_index_only.html>`_\ .
  Created with ``create fulltext index ... WITH WORDPOSITIONS 'off';``.
  see `Texis Documentation for Metamorph Compact Indexes <https://docs.thunderstone.com/site/texisman/compact.html>`_ \.

* Counter Index - best used with
  `LIKEIN <https://docs.thunderstone.com/site/texisman/query_searching_using_likein.html>`_ .
  Created with ``create fulltext index ... WITH COUNTS 'on';``.
  see `Texis Documentation for Metamorph Counter Indexes <https://docs.thunderstone.com/site/texisman/counter.html>`_ \.

.. _updating-a-fulltext-index:

Updating A Fulltext Index
'''''''''''''''''''''''''

After a Fulltext Index is created, and more rows are inserted, the index may
be optimized by using the *exact* same command used to create the index above:

.. code-block:: sql

    create fulltext index employees_Bio_text on employees(Bio);

Alternatively, ``ALTER INDEX`` syntax may be used.

.. code-block:: sql

    alter index employees_Bio_text OPTIMIZE;

Word Expressions
''''''''''''''''

A Fulltext index is created by matching the definition of a "word"
using `rex()`_ regular expressions.  As used above, with no extra
settings, the default regular expression is ``[\uword]{2,99}``: two to
ninety-nine Unicode letters, digits or combining marks.  For English
text this separates words much like the following JavaScript splits
words into an array:

.. code-block:: javascript

    var text = "Remember, wherever you go, there you are"
    var words = text.match(/[a-zA-Z0-9]{2,99}/g);
    console.log(words);
    /* ["Remember","wherever","you","go","there","you","are"] */

The ``\uword`` class matches whole UTF-8 characters in any script, with
the repetition counting characters rather than bytes, so the default is
also correct for non-English text: Unicode white space, punctuation and
invisible format characters (such as the zero-width non-joiner)
separate words.

.. note::
   Word-level indexing works for every language that separates words
   with spaces or punctuation — including Arabic, Persian, Hebrew,
   Cyrillic- and Greek-script languages, and the Indic family.  Scripts
   written *without* word separators — Chinese, Japanese, Thai, Lao,
   Khmer, Burmese — are **not yet supported** at the word level: text
   in those scripts indexes as one term per punctuation-bounded run, so
   only exact runs (e.g. short titles) match.  Proper support requires
   dictionary or n-gram segmentation.  For Korean (which does use
   spaces), consider ``WITH WORDEXPRESSIONS ('[\uword]{1,99}')`` so
   common single-syllable words are indexed.

The list of word expressions can be
altered using ``sql.set()`` and the :ref:`lstexp <sql-set:lstexp>`,
:ref:`addexp <sql-set:addexp>` and :ref:`delexp <sql-set:delexp>` settings.

Alternatively, the expression list can also be set for a single SQL statement using
the ``CREATE INDEX`` and the ``WITH`` syntax:

.. code-block:: sql

    CREATE FULLTEXT employees_Bio_text ON employees(Bio)
    WITH WORDEXPRESSIONS ('[\uword]{1,99}');

which in this example also indexes single-character words that the
default's two-character minimum would skip.

In some cases, there may be datasets where the matching of a limited amount
of punctuation is desirable.

Consider the following small snippet of a C Program that might be cataloged in
a full text searchable database:

::

   /* a pthread mutex needed for multi-threading */
   pthread_mutex_t mylock;

In order to distinguish between the separate strings "pthread" and "mutex",
using the default word expression would be sufficient.

However to have a "pthread_mutex_t" entry in the index, the ``_`` character needs
to be added to the expression.  Further, for maximum flexibility, the index can
contain both versions to index all desired permutations (i.e., "pthread", "mutex"
and "pthread_mutex_t") by using two expressions:

.. code-block:: sql

    CREATE FULLTEXT cprogs_Snippits_text ON cprogs(Snippits)
    WITH WORDEXPRESSIONS ( '[\uword]{1,99}', '[\uword_]{2,99}' );

Punctuation that belongs *inside* a token, such as the dots and hyphens
of a document or section number, presents the opposite problem: an
expression permissive enough to keep them will also swallow the period
that ends a sentence.  The ``\bound`` operator (see
:ref:`Expressions <sql-expressions>`) solves this, since a trailing
``\bound`` gives characters back until the match ends on a word
character:

.. code-block:: sql

    CREATE FULLTEXT docs_Doc_text ON docs(Doc)
    WITH WORDEXPRESSIONS ( '[\uword]{2,99}', '\bound[\alnum.\-]+\bound' );

The second expression indexes ``4.4.3-1a``, ``6.5-4``, ``12.4.15-2b``
and ``NASA-STD-5001B`` as single terms -- keeping the dots and hyphens
within them, but not the closing period of
``as found in section 6.5-4.`` -- while the first continues to index
ordinary prose in any script.

.. note::
   Terms containing a hyphen are searched for as single words, which is
   what makes ``6.5-4`` findable above.  Before version 0.7.1 the default
   was to treat the hyphen as a space and search for the words on either
   side of it as a phrase; set
   :ref:`hyphenPhrase <sql-set:hyphenPhrase>` to ``true`` for that
   behavior, at the cost of not being able to search for a hyphenated
   term as itself.

NOTE:
   Word expressions must be specified when the index is created.  New expressions
   cannot be added upon optimizing the Fulltext index with a ``CREATE``
   statement.


Text Index Maintenance
''''''''''''''''''''''

If a Fulltext index is large, the time and CPU resources it takes to update
the index may be more than is desirable during active use of the database.
If many rows are being added and deleted in an application, choosing an
appropriate time to do the update, and limiting the update to a threshold
of changed rows is appropriate.

Depending on the size of the table, it may be more efficient to linearly scan
new rows rather than update the index.  A script (named, e.g.,
``update-index.js``) like the following would only execute an update if more
than 1000 rows have been changed.

.. code-block:: javascript

    var Sql = require("rampart-sql");

    var sql = new Sql.connection("/path/to/employee_db", true);

    sql.exec("alter index employees_NameBio_text optimize having COUNT(NewRows) > 1000;");

Then adding a crontab entry like the following would execute the script at 2 am every night:

.. code-block:: bash

    00 02 * * * /usr/local/bin/rampart /path/to/update-index.js


scheduleUpdate()
''''''''''''''''

Auto Maintenance of a fulltext or vector index is accomplished by
scheduling a time for an ``OPTIMIZE`` pass using ``sql.scheduleUpdate()``.

For vector indexes, ``OPTIMIZE`` folds accumulated inserts/updates from the delta tier
back into the sealed index but does not retrain quantizers or rebuild the graph.  To
schedule a full rebuild (e.g. when IVFPQ codebooks have drifted relative to the
embedding distribution), use ``scheduleRebuild()`` below.

Usage:

.. code-block:: javascript

    var Sql = require("rampart-sql");

    var sql = new Sql.connection("/path/to/db");

    sql.scheduleUpdate(indexName, startTime, Interval [,minRows]);

Where:

    * ``indexName`` is a :green:`String`, the name of the index to be updated.

    * ``startTime`` is a :green:`String` passed to :ref:`autoScanDate <rampart-utils:autoScanDate>`,
      a :green:`Date`, or a :green:`Number` (seconds since the epoch), the start time of
      the first update/check.  A time given without a timezone offset or abbreviation is
      taken as system local time, using the UTC offset in effect on that date.
      The :green:`String` ``"now"`` starts checking immediately.

    * ``Interval`` is a :green:`Number` or :green:`String`, the time between update checks.
      If a :green:`Number`, the number of seconds between checks (i.e. ``86400`` for one day).
      If a :green:`String`, it will parse plain English values in minutes, hours, days or weeks
      (i.e. ``"hourly"``, ``"120 minutes"``, ``"every third day"`` or ``"2 weeks"``).
      The minimum interval is 60 seconds.

    * ``minRows`` is an optional :green:`Number`.  An update is only run when the number of
      pending rows found at a check is strictly greater than ``minRows``.  Default is ``1000`` for
      fulltext indexes and ``10000`` for vector indexes (vector ``OPTIMIZE`` rewrites the
      sealed index file, so a larger delta amortizes the cost better).  For a vector
      index the count is added plus deleted rows -- an ``UPDATE`` is both, so it counts
      twice.  For a fulltext index it is added rows only.

Return Value:

    ``Undefined``.  An :green:`Error` is thrown for an unknown index, an index that is not
    a fulltext or vector index, or an invalid time, interval or ``minRows``.

Note:

    * If ``sql.scheduleUpdate`` is run again with the same index name, the old schedule is replaced.

    * If ``startTime`` is ``-1``, ``"never"`` or ``"delete"``, the index is unscheduled.  Its row
      in ``SYSUPDATE`` is kept, with ``NEXT``, ``INTV`` and ``THRESH`` set to ``-1``, so the
      ``PREVIOUS`` and ``COMMENTS`` history remains visible.

    * The schedule is saved in the ``SYSUPDATE`` table in the database in question.  Texis
      creates this table automatically; if it has been dropped, ``sql.scheduleUpdate`` recreates it.

    * If ``sql.scheduleUpdate`` is never run for an index, the index will not be touched by
      the monitor process (i.e. in cases where the table/index is never or rarely updated, or
      another method such as a cron job is used instead).

    * Dropping an index (or its table) removes its schedule.  An index re-created with the
      same name is not scheduled until ``sql.scheduleUpdate`` is run again.

The monitor process:

    * The first time an index is scheduled for update on a database, an index monitor process
      is launched.  It appears in process listings as ``rampart indexUpdater /path/to/db``, records
      its process id in ``updater.pid`` and logs its activity to ``update.log``, both in the
      database directory.

    * The monitor is also checked and relaunched if necessary every time ``new Sql.connection(database)``
      is called.  Only one monitor runs per database, no matter how many processes or threads
      connect.  Passing ``{path: "/path/to/db", noUpdater: true}`` to ``new Sql.connection()``
      skips this check.

    * The monitor checks the schedule once a minute.  An update whose time has come is run
      at the next check, so it may start up to a minute late.  Updates are run one at a time;
      a manual ``ALTER INDEX`` on an index the monitor is working on is refused with the message
      ``Index ... appears to be being updated or rebuilt``.

    * If the monitor was not running when a scheduled time passed by more than a minute
      (e.g. the machine was down), that update is skipped and the next one runs at the
      following interval.

    * A check that is long-running (longer than one minute) does not cause missed checks
      to pile up; the next check happens one minute after the long check finishes.

    * The monitor exits when no index in the database is scheduled.

    * If the monitor is killed during an update, the index is left as it was before the update
      started.  The next monitor (launched by the next ``new Sql.connection(database)``) removes
      any temporary files and runs the update again.

    * Status and progress can be viewed in the relevant row of the ``SYSUPDATE`` table.
      Useful columns:

      * ``PREVIOUS`` and ``NEXT``: UNIX-epoch seconds for the previous and next scheduled run.
      * ``ACTION``: ``OPTIMIZE`` or ``REBUILD`` for the in-progress operation; empty when idle.
      * ``STAGE`` and ``NSTAGES``: current stage number and total stages for the in-progress
        operation.  Stage count varies by index type (fulltext and HNSW: 3 stages;
        IVFPQ ``OPTIMIZE``: 2 stages; IVFPQ ``REBUILD``: 5 stages).  When idle
        both are ``0``.
      * ``STAGENAME``: human-readable label for the current stage (e.g. ``"indexing"``,
        ``"training PQ subquantizers"``, ``"encoding"``).
      * ``PROGRESS``: a ``double`` between ``0`` and ``1`` showing fractional progress within
        the current stage.  Reported regardless of whether ``indexmeter`` was set on the
        index.
      * ``COMMENTS``: ``"completed at <timestamp>"`` on success, the error message on
        failure, or ``"skipped: HAVING condition not met (NewRows = N)"`` when the
        ``minRows`` gate short-circuited the run.

Example viewing progress:

.. code-block:: shell

   tsql -d wdb "select convert(PREVIOUS, 'date') LAST, convert(NEXT, 'date') NEXT, ACTION, STAGE, NSTAGES, STAGENAME, PROGRESS from SYSUPDATE where NAME='wtext_Text_ftx'"

       LAST                NEXT          ACTION      STAGE   NSTAGES   STAGENAME    PROGRESS
   -------------------+-------------------+----------+--------+---------+-----------+----------+
   2026-05-04 02:00:02 2026-05-05 02:00:00 OPTIMIZE        2         3 indexing       0.759

   #when done:

   tsql -d wdb "select convert(PREVIOUS, 'date') LAST, convert(NEXT, 'date') NEXT, ACTION, STAGE, NSTAGES, PROGRESS, COMMENTS from SYSUPDATE where NAME='wtext_Text_ftx'"

       LAST                NEXT          ACTION   STAGE   NSTAGES   PROGRESS    COMMENTS
   -------------------+-------------------+--------+--------+---------+----------+----------------------------+
   2026-05-05 02:00:32 2026-05-06 02:00:00              0         0          0   completed at 2026-05-05 02:00:32


scheduleRebuild()
'''''''''''''''''

Like ``scheduleUpdate()``, but schedules a full ``REBUILD`` instead of an ``OPTIMIZE``.

Primarily useful for IVFPQ vector indexes, whose codebooks (k-means centroids and PQ
subquantizers) become stale relative to the embedding distribution as new rows
accumulate.  ``REBUILD`` retrains the codebooks from a reservoir sample of the current
table, re-encodes every row, and atomically swaps in the new index.

For HNSW vector indexes, ``REBUILD`` re-encodes from scratch (no codebooks involved,
but it can be useful to compact the on-disk graph after heavy churn).

For fulltext indexes, ``REBUILD`` is unusual — ``OPTIMIZE`` is generally sufficient.

Usage:

.. code-block:: javascript

    sql.scheduleRebuild(indexName, startTime, Interval [,minRows]);

Arguments and return value are identical to ``scheduleUpdate()``.

Note:

    * Each index has at most one schedule at a time; calling ``scheduleRebuild()`` after
      ``scheduleUpdate()`` on the same index replaces the prior schedule (and vice versa).

    * ``REBUILD`` is significantly more expensive than ``OPTIMIZE`` (it retrains and
      re-encodes the full table).  A higher ``minRows`` threshold is recommended.

    * Progress is reported via the same ``SYSUPDATE`` columns as ``scheduleUpdate()``;
      the ``ACTION`` column will read ``REBUILD`` instead of ``OPTIMIZE``.

Compound Indexes
~~~~~~~~~~~~~~~~

As noted in the syntax `above <#creating-a-fulltext-index>`_\ , an index may be made on multiple fields.  If a
search will always be performed by matching more than one column, it may be
advantageous to create a compound index.

Given this query:

.. code-block:: sql

    SELECT * from employees where Name\Bio likep 'Debbie skydive' and
    Start_date BETWEEN '1999-01-01' and '2005-12-31';

The following could be used to create a Compound Index on the appropriate fields:

.. code-block:: sql

    CREATE FULLTEXT INDEX employees_NameBio_Start_date_cx ON
    employees(Name\Bio, Start_date);

Vector Indexes
~~~~~~~~~~~~~~

A Vector Index is an Approximate-Nearest-Neighbor (ANN) index over a
vector column, used to accelerate the SQL ``LIKEV`` operator for top-K
similarity search.  Two backends are available:

* **IVFPQ** (default) — FAISS inverted-list + product-quantization
  index.  Mmap-backed (scales beyond RAM); needs ~10,000 rows
  minimum to train; lossy approximation typically recovered with a
  ``vecdist()`` re-rank pass.
* **HNSW** — usearch graph index.  In-RAM; no minimum size; near-exact
  distances out of the box.

See :ref:`Choosing a backend <choosing-a-backend>` below for when each is appropriate.

Per-row ``INSERT``, ``UPDATE``, and ``DELETE`` automatically update
the index.  Search-time consistency is maintained without per-row
``save_atomic`` cost via a small newrec / tombstone btree pair next
to the sealed index; see `Maintenance`_ for the periodic OPTIMIZE
step that folds those back in.

Quick start
'''''''''''

.. code-block:: javascript

    var Sql = require("rampart-sql");
    var sql = new Sql.connection('/path/to/db', true);

    sql.exec("create table emb (id int, v varvecF16(384));");
    for (var i = 0; i < rows.length; i++)
        sql.exec("insert into emb values (?, ?);",
                 [rows[i].id, embed_text(rows[i].text)]);

    // Default backend is IVFPQ (needs >= ~10k rows).  For small
    // tables, force HNSW: WITH backend 'hnsw'
    sql.exec("create vector index emb_vec on emb (v);");

    // Top-10 nearest neighbors.  vecdist() re-rank lifts IVFPQ recall.
    var hits = sql.exec(
        "select id, vecdist(v, ?) d from emb where v likev ? " +
        "order by 2 desc;",
        [query_vec, query_vec], 10);

The supported column types for the vector data are listed in
`Column Data Types`_.  Typed ``varvec*`` columns (preferred) auto-detect
their dimensionality; ``byte`` and ``varbyte`` columns require
``WITH vec_dtype '<dtype>'``.

.. _choosing-a-backend:

Choosing a backend
''''''''''''''''''

.. list-table::
   :widths: 25 35 40
   :header-rows: 1

   * -
     - HNSW
     - IVFPQ
   * - Minimum table size
     - none
     - ~10,000 rows (training floor)
   * - On-disk size
     - large (full vectors + graph)
     - moderate (PQ codes in a preallocated container; see note below)
   * - In-RAM footprint
     - full graph resident
     - mmap; kernel-paged
   * - Native recall
     - near-exact
     - lossy; pair with ``vecdist()`` re-rank
   * - Scales beyond RAM
     - no
     - yes
   * - Distribution-shift sensitivity
     - none
     - codebooks need REBUILD if data shifts
   * - Default
     - opt-in via ``WITH backend 'hnsw'``
     - default for new ``CREATE VECTOR INDEX``

Rule of thumb for ``all-MiniLM-L6-v2``-class 384-dim vectors: skip
the index entirely below ~2,000 rows (brute force is fine), HNSW
between ~2k and ~10k rows, IVFPQ above that.

Creating a Vector Index
'''''''''''''''''''''''

.. code-block:: sql

    CREATE VECTOR INDEX <index_name> ON <table>(<column>)
        [ WITH <option> [<option> ...] ]
    ;

``CREATE VEC INDEX`` is accepted as a shortcut.  The ``WITH`` clause
accepts the options below.  Re-running ``CREATE VECTOR INDEX`` on an
existing index of the same table+column is silently treated as
``ALTER INDEX … OPTIMIZE`` (mirroring fulltext); WITH options other
than ``indexmeter`` are ignored on re-CREATE.

Index creation is **non-blocking**: the table stays fully readable
*and writable* while the index builds.  Rows inserted, updated or
deleted during the build are tracked live and are searchable the
moment the index goes live — nothing is missed and no follow-up
``OPTIMIZE`` is required.  (Fulltext index creation is also
non-blocking, but rows added *during* a fulltext CREATE or REBUILD
only become searchable at the next ``ALTER INDEX … OPTIMIZE``; the
vector index has no such blind spot.)  Searches issued while the
build is still running do not yet use the index (an index-less
``LIKEV`` at that point follows the ``allinear`` policy above).

An HNSW index may be created on an **empty table** if ``vec_dim`` is
given (there is no row to infer the dimension from); rows inserted
afterwards are tracked as usual.  IVFPQ cannot: its product-quantizer
training needs data — at least ``256 × vec_pq_min_points_per_centroid``
vectors (the coarse/PQ k-means floor).

``ALTER TABLE … COMPACT`` is refused while a table has vector
indexes, because compaction moves row ids that the sealed index
stores: ``DROP`` the vector index(es) first, compact, then
re-``CREATE``.

Common options (both backends):

.. list-table::
   :widths: 22 28 14 36
   :header-rows: 1

   * - option
     - values
     - default
     - meaning
   * - ``backend``
     - ``'hnsw'``, ``'ivfpq'`` (aliases ``'usearch'``, ``'faiss'``)
     - ``'ivfpq'``
     - Selects backend.
   * - ``vec_metric``
     - ``'dot'``, ``'l2'`` (also ``'ip'`` / ``'inner'``)
     - ``'dot'``
     - For cosine similarity, store unit-normalized vectors and use
       ``'dot'``.
   * - ``vec_dtype``
     - ``'f64'``, ``'f32'``, ``'f16'``, ``'bf16'``, ``'i8'``, ``'u8'``
     - matches column
     - Required on ``byte``/``varbyte`` columns.  On typed
       ``varvec*`` columns it overrides the column's element type
       (HNSW only — IVFPQ uses its own PQ quantization regardless).
   * - ``vec_dim``
     - positive integer
     - auto from first row
     - Per-vector dimensionality.  Usually **optional**: the first
       usable row decides it — from the self-describing header
       :ref:`chunkembed() <sql-server-funcs:chunkembed>` writes into
       each value (which records ``dim`` even for ``k × dim``-cell
       chunked rows), or, for plain headerless values, from the row's
       cell count.  Required only when no row can reveal it: CREATE on
       an **empty table**, or *headerless* multi-vector data (raw
       ``k × dim`` concatenations not made by ``chunkembed()``).  An
       explicit ``vec_dim`` always wins; rows whose header disagrees
       with the index dim are warned about and skipped.
   * - ``indexmeter``
     - ``'on'``, ``'off'``, ``'always'``, ...
     - process default
     - Build-time progress meter.  A ``CREATE``-time option, also honored
       on re-CREATE (which is silently an OPTIMIZE).  ``ALTER INDEX``
       accepts no ``WITH`` clause; use ``sql.set({indexMeter: ...})``
       before ``OPTIMIZE`` / ``REBUILD``.

HNSW-only options:

.. list-table::
   :widths: 22 28 14 36
   :header-rows: 1

   * - option
     - values
     - default
     - meaning
   * - ``vec_m``
     - integer in ``[4, 1024]``
     - 64
     - Graph connectivity (max neighbors per node).
   * - ``vec_efc``
     - integer in ``[8, 4096]``
     - 128
     - Expansion factor at construction.
   * - ``vec_scale``, ``vec_zero_point``, ``vec_calibrate``
     - see below
     - see below
     - i8/u8 quantization controls.  See :ref:`i8 / u8 quantized indexes <i8-u8-quantized-indexes>`.

IVFPQ-only options:

.. list-table::
   :widths: 22 28 14 36
   :header-rows: 1

   * - option
     - values
     - default
     - meaning
   * - ``vec_pq_nlist``
     - integer ``>= 4`` (no upper cap when set explicitly)
     - auto from row count: ``round_pow2(4·sqrt(N))``,
       clamped to ``[64, 65536]``
     - Number of inverted-list (coarse) clusters.  Powers of two are
       conventional but not required when set explicitly.  The
       auto-formula clamp is a sane-default ceiling, not a hard limit
       — set a larger value with ``WITH vec_pq_nlist N`` if you need
       it.
   * - ``vec_pq_m``
     - integer multiple of 4, in ``[8, 96]``
     - auto: ``round4(dim/8)`` (e.g. 48 for dim=384)
     - PQ subquantizers — bytes per row in the sealed segment.
   * - ``vec_pq_nbits``
     - 8
     - 8
     - Bits per PQ code.  Only ``8`` is supported in v1.
   * - ``vec_pq_target_rows``
     - positive integer
     - 0 (use current row count)
     - Hint for the auto-tuner when you intend to grow the table.
   * - ``vec_pq_min_points_per_centroid``
     - integer in ``[1, 39]``
     - 39
     - Lower values let the index build on smaller tables (each
       centroid trains on fewer points; recall suffers).  Mostly a
       test knob.
   * - ``vec_encode_batch``
     - integer in ``[1, 1048576]``
     - auto from :ref:`indexMem <sql-set:indexMem>`, clamped to
       ``[16384, 262144]``
     - Vectors accumulated per encoding call during the build.  Larger
       batches let the encoder use the whole machine (blocked BLAS +
       all cores); the buffer costs ``dim × 4`` bytes per vector.  A
       build-performance knob only — the resulting index is identical.
   * - ``vec_encode_gpu``
     - ``'auto'``, ``'on'``, ``'off'``
     - ``'auto'``
     - Run the encode stage's coarse assignment on a GPU when a
       CUDA-enabled ``rampart-faiss`` module is installed and a device
       is present (one message reports it; any GPU problem falls back
       to CPU with a warning).  ``'auto'`` also skips the GPU for
       small jobs — e.g. a routine ``ALTER INDEX ... OPTIMIZE`` over a
       few thousand inserts — where one-time GPU startup would cost
       more than it saves.  ``'on'`` always uses the GPU and fails
       the build if it can't; ``'off'`` never tries.  The index is
       identical either way.

Examples:

.. code-block:: sql

    -- Default (IVFPQ): tens of thousands of rows or more
    CREATE VECTOR INDEX emb_vec ON emb (v);

    -- HNSW for a smaller table; near-exact recall
    CREATE VECTOR INDEX emb_vec ON emb (v) WITH backend 'hnsw';

    -- IVFPQ with explicit shape (override the auto-tuner)
    CREATE VECTOR INDEX emb_vec ON emb (v)
        WITH backend 'ivfpq' vec_pq_nlist 1024 vec_pq_m 32;

    -- HNSW, smaller graph
    CREATE VECTOR INDEX emb_vec ON emb (v)
        WITH backend 'hnsw' vec_m 32 vec_efc 256;

    -- HNSW with i8 quantization
    CREATE VECTOR INDEX emb_vec ON emb (v)
        WITH backend 'hnsw' vec_dtype 'i8' vec_calibrate 'auto';

    -- byte / varbyte column with explicit dtype
    CREATE VECTOR INDEX emb_vec ON emb (v) WITH vec_dtype 'f16';

.. _i8-u8-quantized-indexes:

i8 / u8 quantized indexes
'''''''''''''''''''''''''

**HNSW only.**  IVFPQ does its own product quantization (8-bit codes
per subquantizer) regardless of column dtype — the ``vec_dtype``
option is ignored for IVFPQ on typed ``varvec*`` columns.

For HNSW workloads where the column stores full-precision vectors
(``varvecF32``, ``varvecF16``, etc.) but the *index* should be
smaller, use ``vec_dtype 'i8'`` (or ``'u8'``).  The column data
stays at its declared precision; the index quantizes each vector to
one byte per element on the way in.

Default calibration is symmetric for ``i8``
(``vec_scale = 1/127``, ``vec_zero_point = 0``) and
asymmetric-centered for ``u8`` (``vec_scale = 1/127``,
``vec_zero_point = 128``).  Both fit L2-normalized vectors in
``[-1, 1]`` exactly.  For unnormalized data, either pass explicit
``vec_scale`` and ``vec_zero_point`` (quote negative integers, e.g.
``vec_zero_point '-10'``), or add ``vec_calibrate 'auto'`` to derive
them from a pre-scan at build time.

Native ``varvecI8`` / ``varvecU8`` columns are indexable directly;
no conversion happens.

.. _querying-with-likev:

Querying with LIKEV
'''''''''''''''''''

Basic top-K:

.. code-block:: sql

    SELECT id, $rank
      FROM emb
     WHERE v LIKEV ?;

* ``LIKEV`` returns rows **automatically ordered by rank**, best
  first (like ``LIKEP``) — no ``ORDER BY`` needed.  This is true with
  and without a vector index: a linear (index-less) search produces
  the **identical** result — same rows, same ``$rank`` values, same
  order, same ``likevRows`` cap — just slower, because every row is
  scored.  If it is too slow, build the index.
* Like ``LIKEP``, an index-less ``LIKEV`` requires opting in to the
  full table scan: without ``allinear`` set the query is refused
  ("would require linear search") and returns zero rows.  Enable it
  per connection with ``sql.set({allinear: true})`` (or per statement
  batch with ``SET allinear=1;``).  This applies only when there is
  **no usable vector index** — the incremental scan of rows added
  after the index was created (or last optimized) is part of the
  indexed search and never needs ``allinear``.
* ``$rank`` is the per-row score, scaled to ``[-100000, 100000]``.
  With unit-norm vectors the practical range is ``[0, 100000]``.
  (In a hybrid keyword-OR-vector query this same value is available
  per row as ``$vrank`` — see `Per-side scores: $krank and $vrank`_.)
* Use the ``maxRows`` parameter of ``exec()`` to retrieve top-K.
* If the index exists but can't serve the query — wrong column type,
  dimension mismatch, unreadable index files — ``LIKEV`` falls back to
  the same linear scan (subject to the ``allinear`` policy above).
  A **query-dimension mismatch** additionally warns explicitly —
  "*query vector has N cells but the index dim is D — embedding model
  mismatch for this table?*" — since querying with a different
  embedding model than the table was built with is almost always the
  cause.  (The linear scan warns the same way when the query vector is
  size-incompatible with every row.)  In a hybrid ``OR`` the keyword
  side still serves, so a wrong-model query degrades to keyword-only
  results with both messages in the error text.
* A bare **string** right-hand side auto-embeds with or without an
  index (typed ``varvec*`` columns; an unindexed ``varbyte`` column
  cannot auto-embed, since the element type is only recorded in a
  vector index's parameters).
* On a *chunked* (multi-vector) row — see
  `Chunked documents (multi-vector rows)`_ — ``$rank`` is the row's
  **best-matching chunk's** score (max over the row's chunks), on both
  the index path and the brute-force/re-score path.
* ``$rank`` **is exact at the column's stored precision** for both
  HNSW and IVFPQ.  The index returns a candidate pool (up to
  :ref:`likevRows <sql-set:likevRows>` rows, default 1000) ordered by
  the backend's native score; each candidate is then re-scored
  per-row by ``rp_vector_distance`` against the actual stored column
  bytes before ``$rank`` is set.  The IVFPQ PQ approximation only
  affects which rows are picked as candidates and in what initial
  order — never the final score you sort on.

  This means an extra ``ORDER BY vecdist(v, ?) DESC`` pass is **not**
  needed for top-K ordering — it would produce the same ordering as
  ``ORDER BY $rank DESC`` (just as a float instead of a scaled int).
  See `When to use vecdist()`_ below for the cases where
  ``vecdist()`` is genuinely useful.

  To grow the candidate pool when recall feels too low, raise
  :ref:`likevRows <sql-set:likevRows>` (both backends), or, for
  IVFPQ, raise :ref:`likevPqNprobe <sql-set:likevPqNprobe>` — these
  surface more candidates that ``$rank`` will then exact-score.

.. _hybrid-rank-fusion:

Hybrid keyword + vector queries (rank fusion)
'''''''''''''''''''''''''''''''''''''''''''''

A single ``OR`` combines a full-text (``LIKEP``) and a vector
(``LIKEV``) search into one hybrid query, ranked by **Reciprocal Rank
Fusion** — the standard method for merging retrievers whose scores
live on incomparable scales (metamorph proximity vs scaled cosine):

.. code-block:: sql

    SELECT id, title, $rank
      FROM docs
     WHERE doc LIKEP ? OR v LIKEV ?;

When **both** columns are indexed (a metamorph index on ``doc``, a
vector index on ``v``), the engine takes each side's ranked candidate
list and fuses them with a **modified Reciprocal Rank Fusion**: each
side contributes by its list position, so no score calibration between
the two retrievers is needed; a document found by **both** sides
outranks documents found by only one, and on otherwise-equal evidence
the keyword match wins.  The fused ``$rank`` is a positional score, not
a calibrated relevance value.

Both lists are cut to the same depth, :ref:`rrfRows
<sql-set:rrfRows>`, so a position carries the same meaning on either
side.  The vector index still searches to :ref:`likevRows
<sql-set:likevRows>` and its list is truncated afterwards, leaving ANN
recall untouched.

The fusion is adjustable per connection:

+--------------------------------------------------------+------------------------------------------+
| setting                                                | controls                                 |
+========================================================+==========================================+
| :ref:`rrfRows <sql-set:rrfRows>`                       | pool depth for both sides                |
+--------------------------------------------------------+------------------------------------------+
| :ref:`rrfK <sql-set:rrfK>`                             | how much cross-engine agreement is worth |
|                                                        | against one strong hit                   |
+--------------------------------------------------------+------------------------------------------+
| :ref:`rrfTieBreak <sql-set:rrfTieBreak>`               | which side wins an exact tie             |
+--------------------------------------------------------+------------------------------------------+
| :ref:`rrfKwWeight <sql-set:rrfKwWeight, rrfVecWeight>` | relative weight of each side             |
+--------------------------------------------------------+------------------------------------------+
| :ref:`likevMinRank <sql-set:likevMinRank>`             | similarity floor for vector candidates   |
+--------------------------------------------------------+------------------------------------------+

See :ref:`Rank Fusion Properties <sql-set:Rank Fusion Properties>` for
the formula and the full description of each.

Rows are returned **automatically ordered by the fused rank**, best
first — no ``ORDER BY`` needed, exactly as a single ``LIKEP`` or
``LIKEV`` returns rank-ordered rows on its own.  This holds with or
without the vector index: a missing index only makes the vector side
a (slower) linear scan producing the same ranked candidate list, so
results are identical — if they arrive too slowly, build the index.

Per-side scores: $krank and $vrank
""""""""""""""""""""""""""""""""""

The fused ``$rank`` is built from list *positions*, so it does not
reveal how strong each side's match actually was.  Two additional
columns expose the native scores that produced those positions:

.. code-block:: sql

    SELECT $rank, $krank, $vrank, id, title
      FROM docs
     WHERE doc LIKEP ? OR v LIKEV ?;

* ``$krank`` — the keyword side's metamorph rank for this row, scaled
  ``[0, 1000]``: **exactly the value a solitary** ``LIKEP`` **query's**
  ``$rank`` **would show**.  ``0`` means the row is not in the keyword
  candidate pool (the top ``likeprows``).
* ``$vrank`` — the vector side's similarity, scaled ``[0, 100000]``:
  exactly the value a solitary ``LIKEV`` query's ``$rank`` would show.
  ``0`` means the row is not in the vector pool (the top
  ``likevRows``).

A consensus row carries both; a single-sided row carries one, with
``0`` in the other.  Typical uses: displaying each retriever's own
score beside a result, or application-side re-scoring experiments
that blend the native scales differently than RRF does.

Both columns are meaningful **in the SELECT list only**.  They are
accepted in ``WHERE`` and ``ORDER BY`` without complaint, but have no
effect there — a ``WHERE $krank > 0`` neither filters nor errors — so
they should not be used in those clauses.  In a non-hybrid query they
read ``0`` on a freshly opened handle; there they add nothing, since a
solitary ``LIKEP``'s or ``LIKEV``'s ``$rank`` *is* that native score
already.  Note that after a hybrid query on the same handle, a later
non-hybrid ``SELECT`` may report the previous query's values rather
than ``0``.

Notes:

* The vector side fuses **with or without its index**, but the
  index-less case is subject to the same ``allinear`` policy as
  ``LIKEP``: with ``allinear`` set, the vector side's ranked list is
  built by linear scan and fused results are identical (slower);
  without it that side is refused and contributes nothing (a
  ``LIKEP``-indexed OR then returns keyword results only).  A missing
  metamorph index degrades the same way: without its index and
  without ``allinear`` the keyword side has no ranked list.
* The two list depths are the fusion pool knobs: ``likeprows``
  (default 100) and ``likevRows`` (default 1000) bound what each
  retriever contributes.
* **Performance — do not add** ``ORDER BY $rank DESC``.  Output is
  already fused-rank-ordered.  Without an ORDER BY the SELECT list is
  evaluated *lazily* — only for the rows ``maxRows`` actually returns
  — so even an expensive function like
  :ref:`abstract() <sql-server-funcs:abstract>` in the select list
  costs only ``maxRows`` evaluations.  An explicit ORDER BY destroys
  both properties: the engine materializes every fused candidate (up
  to ``likeprows + likevRows`` rows) and runs the full select list on
  each before sorting what was already in order — on a large table
  with an in-query abstract that is the difference between
  milliseconds and minutes.
* The 5-argument vec form of
  :ref:`abstract() <sql-server-funcs:abstract>` works directly in the
  fused query — the complete hybrid search is one statement::

      SELECT $rank score, id, title,
             abstract(doc, 400, 'querybest', ?, v) snip
        FROM docs
       WHERE doc LIKEP ? OR v LIKEV ?;

  ``abstract()`` selects the winning chunk itself: it embeds its
  query argument (a cache hit — ``LIKEV`` just embedded the same
  string) and scores the row's chunks with the same scorer ``LIKEV``
  uses, so best-chunk seeding is deterministic in any query shape —
  fused, plain ``LIKEV``, or a per-row ``id = ?`` lookup — and never
  depends on what the surrounding query evaluated.  (Only when the
  abstract query argument is empty does it fall back to the
  surrounding ``LIKEV``'s per-row scoring state, e.g. vector-literal
  queries with no text.)

  When the output feeds an LLM instead of a results page, swap the
  snippet column for :ref:`excerpt() <sql-server-funcs:excerpt>` —
  ``excerpt(doc, 2000, ?, v)`` — which returns the best-matching
  chunks verbatim, in document order, instead of an ellipsized
  display abstract.

* An ``AND`` of the two predicates is a **filter**, not a fusion:
  "must match the keywords, ranked by vector similarity" (e.g. an
  exact product code plus semantic ordering).  Its ``$rank`` derives
  from the branch ranks and is not a calibrated hybrid score — use
  the ``OR`` form when ranking quality is the goal.
* ``OR`` of two ``LIKEP`` predicates (or two ``LIKEV`` predicates) is
  unchanged — fusion applies only to the mixed keyword/vector shape.

When to use vecdist()
'''''''''''''''''''''

``vecdist()`` is a standalone distance function, not a re-rank
companion to LIKEV.  It is useful when you want:

* **A non-default metric.**  ``LIKEV`` always uses dot product;
  ``vecdist(v, ?, 'l2')`` gives L2 distance.
* **A numeric distance value as a projection column** — for
  thresholding, application-side post-processing, or display
  alongside the row, when the scaled integer ``$rank`` isn't the
  form you want.
* **Distance to a *different* reference than the LIKEV query.**
  ``WHERE v LIKEV ?A`` retrieves nearest neighbors of A;
  ``SELECT vecdist(v, ?B)`` in the same SELECT annotates each hit
  with its distance to a second reference B.
* **Brute force without an index** — on small tables or for ad-hoc
  exploration; ``SELECT id, vecdist(v, ?) FROM emb ORDER BY 2 DESC``
  does a full scan with no ``CREATE VECTOR INDEX`` required.

Vector predicates may be combined with regular ``WHERE`` clauses:

.. code-block:: sql

    SELECT id, $rank, label
      FROM emb
     WHERE v LIKEV ?
       AND label = 'animal'
     ORDER BY 2 DESC;

The vector predicate uses the index; other predicates filter the
returned candidates.

INSERT, UPDATE, DELETE
''''''''''''''''''''''

A vector index participates in the standard SQL update path:

* ``INSERT`` adds the row's recid to a small ``_T.btr`` (newrec)
  alongside the sealed index.  Searches consult both transparently.
* ``DELETE`` adds the row's recid to ``_del.btr`` (tombstone), but only
  when the sealed index actually holds that row -- deleting a row that
  is still only in ``_T.btr`` just removes it from there.
* ``UPDATE`` is delete+insert; the new vector lands in ``_T.btr``,
  the old recid is tombstoned.

Per-row mutations are durable on commit (no per-row rewrite of the
sealed index file).  Between maintenance runs the newrec/tombstone
btrees grow and search latency drifts up; ``ALTER INDEX … OPTIMIZE``
folds them back into sealed and empties both.  See `Maintenance`_.

A note on type matching: a typed ``varvec*`` column should be given a
``rampart.vector`` of the matching dtype.  A vector of a different dtype is
**not** rejected -- it is silently converted to the column's declared type
(the element count is preserved, so a 3-element ``f16`` vector inserted into a
``varvecF32(3)`` column is stored as 3 ``f32`` elements).  Since that
conversion is lossy in one direction and merely wasteful in the other, it is
better to be explicit: the vector object's
:ref:`conversion methods <rampart-vector:Vector Object Conversion Functions>`
(``.toF32()``, ``.toF16()``, etc.) make the adjustment visible at the call
site, and make an unintended dtype obvious when reading the code:

.. code-block:: javascript

    var v = new rampart.vector('f16', [...]);
    sql.exec("insert into emb values (?, ?);", [id, v.toF32()]);  // OK on varvecF32

A ``byte`` or ``varbyte`` column accepts the raw bytes via
``.toRaw()``.

Maintenance
'''''''''''

``ALTER INDEX <name> OPTIMIZE`` folds the accumulated newrec /
tombstone delta back into the sealed segment.  New rows are encoded
into it and deleted rows are physically removed from it, so both delta
btrees are left empty and search no longer pays to filter them.
Searches stay live during the build (only the brief commit phase fences
writes), and INSERTs and DELETEs landing during the build are picked up
via a carry-forward step.  Returns immediately only when both deltas are
empty -- a run with nothing but deletes pending still rewrites the
sealed segment to drop them.

``ALTER INDEX <name> REBUILD`` re-encodes the entire table from
scratch.  Use this if the embedding distribution has shifted (IVFPQ
codebooks become stale) or if you want to reset to a clean baseline.
Same atomic-swap semantics as OPTIMIZE.

Re-running ``CREATE VECTOR INDEX <name> ON <table>(<col>) ...``
against an existing index of the same table+column is silently a
``OPTIMIZE`` of that index — same shape as fulltext's re-CREATE.
``WITH`` options other than ``indexmeter`` are ignored.

.. code-block:: sql

    -- Periodic absorption (after bulk INSERTs)
    ALTER INDEX emb_vec OPTIMIZE;

    -- Heavy re-build (after distribution shift, model swap, etc.)
    ALTER INDEX emb_vec REBUILD;

``ALTER INDEX`` takes no ``WITH`` clause.  To show a progress meter while
optimizing or rebuilding, set it on the connection first:

.. code-block:: javascript

    sql.set({indexMeter: 'percent'});
    sql.exec("ALTER INDEX emb_vec OPTIMIZE;");

Removing a Vector Index
'''''''''''''''''''''''

.. code-block:: sql

    DROP INDEX emb_vec;

Removes the SYSINDEX entry plus all on-disk artifacts:

* HNSW: ``<index_name>.vec``, ``<index_name>_T.btr``,
  ``<index_name>_del.btr``.
* IVFPQ: ``<index_name>_H.idxpq``, ``<index_name>_I.idxpq``,
  ``<index_name>_T.btr``, ``<index_name>_del.btr``.

The vector column on the table itself is unaffected.

.. note::

   **IVFPQ on-disk file size.**  The inverted-list file
   ``<index_name>_I.idxpq`` is written through FAISS's
   ``OnDiskInvertedLists`` container, which **preallocates the file to
   a fixed budget with growth headroom** rather than packing it tightly
   to actual data.  A typical 46M-row, dim-384, ``pq_m=48`` index
   occupies about 8 GiB on disk even though the actual PQ codes plus
   row IDs are closer to 3 GiB.  This is intentional — the
   preallocated slots let inverted lists grow in place to accommodate
   incremental ``INSERT``/``UPDATE`` activity between rebuilds without
   rewriting the file.  ``ALTER INDEX … REBUILD`` produces a fresh
   container of the same shape; ``OPTIMIZE`` reuses the existing
   container.  ``du`` will typically report less actual block usage
   than the apparent file size; both numbers are normal.

Removing Indexes
~~~~~~~~~~~~~~~~

If an index is no longer needed, it may be removed using the following syntax:

.. code-block:: sql

    DROP INDEX index-name;

Further Reading on Indexing
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Detailed information about indexing and options can be found on the
`Texis Documentation Website <https://docs.thunderstone.com/site/texisman/indexing_for_increased.html>`_\ .


Vector Search
-------------

This section is the end-to-end guide to semantic vectors in ``rampart-sql``.
It ties together the column types, the embedding function, the SQL ``set``
knobs, the ``LIKEV`` operator, and the vector index — each of which has a
detailed reference elsewhere in the docs.

Overview
~~~~~~~~

A *semantic vector* (also called an *embedding*) is a fixed-length array of
floats that represents a piece of text in a continuous space, such that two
texts with similar meaning have vectors that are close together.  Embeddings
are produced by a model, loaded once into the SQL engine via
``sql.set({llamaEmbed:'/path/to/model.gguf'})`` (a ``llama.cpp`` GGUF
model) or ``sql.set({onnxEmbed:{model:'/path/to/model-dir'}})`` (an ONNX
model directory); similarity is measured by dot product or L2 distance
and reported by ``LIKEV`` and ``vecdist()``.

The typical end-to-end pipeline:

.. code-block:: javascript

    var Sql = require("rampart-sql");
    var sql = new Sql.connection('/path/to/db', true);

    // 1. Load an embedding model (one-shot for the process)
    sql.set({llamaEmbed: '/models/all-minilm-l6-v2_f16.gguf'});

    // 2. Create a table whose vector column matches the model's output dim
    //    (all-MiniLM-L6-v2 → 384).  varvecF16 is the right default for most
    //    embedding pipelines (small, indexable, near-lossless).
    sql.exec("create table docs (id int, text varchar(2048), v varvecF16);");

    // 3. Insert with embed() computing the vector inline
    for (var i = 0; i < rows.length; i++)
        sql.exec("insert into docs values (?, ?, embed(?))",
                 [rows[i].id, rows[i].text, rows[i].text]);

    // 4. Build an ANN index for top-K search.  (Index-less LIKEV is a
    //    linear scan and requires sql.set({allinear:true}) first --
    //    otherwise the query returns zero rows with the reason in
    //    sql.errMsg.)
    sql.exec("create vector index docs_vec on docs (v) with backend 'hnsw';");

    // 5. Query.  A string on the right-hand side of LIKEV is auto-coerced
    //    to embed(?).  Rows arrive rank-ordered (best first).
    var hits = sql.exec(
        "select id, text, $rank from docs where v likev ?",
        ["chocolate cake recipe"],
        {maxRows: 10}
    );

Each step has knobs and caveats covered below and in the linked reference
sections.

Storage
~~~~~~~

Vectors are stored in typed ``varvec*`` columns or in raw ``byte`` /
``varbyte`` columns; see :ref:`Vector <rampart-sql:Vector>` under "Column
Data Types" for the full dtype table and a discussion of when to prefer
each.  In short:

* ``varvecF16`` — most embedding pipelines.  2 bytes per element,
  near-lossless on L2-normalized vectors, indexable.
* ``varvecF32`` — natural precision; no quantization loss; useful when
  you need to migrate or re-quantize later.
* ``varvecBf16`` — TPU/Google-style pipelines.
* ``varvecF64`` — high-precision research/scientific use.
* ``varvecI8`` / ``varvecU8`` — 1 byte per element; reserved (typed
  storage only — direct indexing is not yet enabled, but HNSW i8/u8
  *quantized indexes over varvecF\* columns* are supported via
  ``WITH vec_dtype 'i8'``).
* ``byte(N)`` / ``varbyte(N)`` — opaque bytes, supported for
  compatibility: pre-existing tables, code that produces raw byte
  buffers, or a column that must hold vectors of more than one dtype.
  **Prefer a typed ``varvec*`` column** — a ``varbyte`` column cannot
  carry the element type, so inserts are unchecked, values are
  ambiguous without the index (which requires
  ``WITH vec_dtype '<dtype>'``), and string ``LIKEV`` parameters only
  work once that index exists.  See the caveat list under
  :ref:`Vector <rampart-sql:Vector>` in "Column Data Types".

The column dimensionality of a typed ``varvec*`` is auto-detected from the
first inserted vector.  Subsequent inserts must match.

Generating embeddings
~~~~~~~~~~~~~~~~~~~~~

Embedding generation runs inside the SQL engine via a loaded embedding
model.  Two engines are available; a connection uses whichever was set
most recently:

* :ref:`llamaEmbed <sql-set:llamaEmbed>` — a ``llama.cpp`` ``.gguf``
  model file (module ``rampart-llamacpp``).
* :ref:`onnxEmbed <sql-set:onnxEmbed>` — an ONNX model *directory*
  (module ``rampart-onnx``); the directory self-configures tokenizer,
  pooling and special tokens.  ONNX sessions run concurrently across
  threads with no per-thread setup.

.. rubric:: Which engine?

Both engines produce equivalent vectors from the same underlying model
(most embedding models are published in both GGUF and ONNX form), so the
choice is about hardware and model availability, not accuracy:

* **Linux with an NVIDIA GPU** — use ``onnxEmbed``.  The CUDA-enabled
  install uses the GPU automatically, and it is the fastest path for
  bulk ingestion (``chunkembed()`` over millions of rows).

* **CPU-only Linux, and macOS** — use ``llamaEmbed``.  llama.cpp's CPU
  kernels are 2–3× faster than ONNX Runtime at every model size we
  measured (x86 and ARM), and on Apple Silicon its Metal backend is an
  order of magnitude faster again on large models.  (Apple's CoreML
  path gains little for text encoders and is not the answer there.)

* **Model only published as ONNX** — use ``onnxEmbed`` on any hardware;
  it also reproduces HuggingFace tokenizer behavior exactly, which
  matters for models with unusual tokenizers.

Vectors are interchangeable: a table built with one engine can be
queried with the other, provided the *model* is the same — results
stay correct and relevant.  They are not byte-identical, though:
published GGUF and ONNX files of the same model differ in weight
precision (f16 vs f32), which shifts near-tie rankings and which
chunk of a document wins (measured: ~80% identical top results,
~84% identical snippets).  For reproducible output, serve with the
same engine and weight file the table was built with.

.. note::

    Embedding requires the corresponding module (``rampart-llamacpp`` or
    ``rampart-onnx``) to be installed and loadable.

    On the first ``sql.set({llamaEmbed: ...})`` call, if no llamacpp
    module is already loaded, ``rampart-sql`` automatically tries
    ``require()`` in order:

    1. ``rampart-llamacpp`` (the canonical name; on systems with a
       single installed variant this is what that variant ships under)
    2. ``rampart-llamacpp_cuda``
    3. ``rampart-llamacpp_cpu``

    Whichever resolves first wins.  For most installs this is
    transparent — no explicit ``require()`` is needed.

    To force a specific build (e.g. you have both CPU and CUDA
    installed and want the CPU one), load it explicitly **before**
    ``sql.set({llamaEmbed: ...})``:

    .. code-block:: javascript

        require('rampart-llamacpp_cpu');     // or _cuda, _metal, etc.
        sql.set({llamaEmbed: '/models/...'});

    ``rampart-sql`` discovers the embed functions via ``dlsym`` from
    whichever variant is already loaded, so a manually required
    module always takes precedence over the auto-load fallback.

    If no module is available, ``sql.set({llamaEmbed: ...})`` throws
    with a "cannot load rampart-llamacpp" error and ``embed()`` is
    inoperable.  ``rampart-llamacpp`` ships separately in
    ``rampart-langtools``; see its module documentation for build
    variants and install instructions.

The settings below configure it, all documented in detail under
:ref:`sql-set:Embedding Properties`:

* :ref:`llamaEmbed <sql-set:llamaEmbed>` — the path to a ``.gguf`` embedding
  model.  Setting this loads the model into the SQL process; once loaded,
  the model is shared across all subsequent ``embed()`` calls (and across
  all threads, when ``llamaEmbedPerThread`` is on).
* :ref:`llamaEmbedPerThread <sql-set:llamaEmbedPerThread>` — when ``true``
  (the default), each worker thread keeps its own ``llama_context`` over
  the shared model, so concurrent ``embed()`` calls run in parallel on GPU
  or multi-core CPU.  Set to ``false`` to serialize through a single
  context (lower memory, lower throughput).  (llama.cpp only — ONNX
  sessions are concurrent by nature.)
* :ref:`onnxEmbed <sql-set:onnxEmbed>` — load an ONNX model directory as
  the connection's embedding engine instead of a GGUF.
* :ref:`likevCache <sql-set:likevCache>` — size of the per-model
  document-embedding result cache (default 10 documents; ``0``
  disables).  Any of ``embed()`` / ``chunkembed()`` / ``chunkavg()`` /
  ``chunkcoherence()`` — or a repeated ``LIKEV`` search string — on the
  *same text* is served from it, so the model runs once per distinct
  text.

A couple of llama.cpp knobs that affect embedding throughput are *not*
``sql.set`` properties: the SQL path loads the model through
rampart-llamacpp without passing an options object, so they are set on
that module instead.  The two worth knowing are the thread count used
for the multi-token decodes that embedding performs, and whether a
document's chunks are packed into a single decode:

.. code-block:: javascript

    require('rampart-llamacpp').embedDefaults({
        threadsBatch: 8,       // else ggml uses 4, whatever the core count
        batchChunks:  false    // default is auto: on for GPU, off for CPU
    });

    sql.set({llamaEmbed: '/models/bge-m3-FP16.gguf'});

The order shown is required.  Models are cached by path, so once one is
loaded, a later ``sql.set({llamaEmbed: ...})`` hands back the existing
model and an ``embedDefaults()`` call made after that point silently
does not apply to it.  Batching is worth turning off only to reproduce
vectors from a build that predates it, or to A/B measure it — see
:ref:`embedDefaults <rampart-langtools:embedDefaults>` for the full set
and for what batching does and does not change.

Once a model is loaded, :ref:`embed() <sql-server-funcs:embed>` produces a
typed vec for use in SELECT projections, INSERT values, or on the
right-hand side of ``LIKEV`` / ``vecdist()``:

.. code-block:: sql

    -- default dtype is f16
    SELECT embed('the cat sat on the mat');

    -- explicit dtype: f16, f32, f64, bf16
    SELECT embed('the cat sat on the mat', 'f32');

    -- in INSERT: embed each row's text into a vec column
    INSERT INTO docs (id, text, v) VALUES (?, ?, embed(?));

The dtype named in the second argument propagates to the result column
type, so ``select embed(?, 'f32')`` returns a ``varvecF32``, not a
``varvecF16``.  The text argument is typically a column or a bound
parameter; a literal string is also accepted.

Chunked documents (multi-vector rows)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``embed()`` reduces a whole document to *one* vector — fine for short
texts, but a long document's meaning gets averaged away.
:ref:`chunkembed() <sql-server-funcs:chunkembed>` instead has the
embedding engine split the document into chunks (structure-aware:
paragraph boundaries first, then the model's token window) and returns
**all** of the chunk vectors concatenated — a ``k × dim``-cell value
stored in the ordinary ``varvec*`` column types.  No new column type, no
separate chunk table: one document, one row.

.. code-block:: javascript

    sql.exec("create table docs (id int, doc varchar(16000), v varvecF16);");

    // one row per document; k chunk vectors land in v.  The title as
    // the per-chunk PREFIX ('' = default dtype): every chunk embeds as
    // "<title> <chunk text>", so name-seeking queries match any chunk
    // of the document by its name.
    sql.exec("insert into docs values (?, ?, chunkembed(?, '', ?))",
             [id, doc, doc, title]);

    // no vec_dim needed: chunkembed()'s value header records the
    // per-chunk dim, and the first row's header sets the index.
    // 'hnsw' works at any table size; the default (IVFPQ) backend
    // needs ~10,000 vectors of training data before it can build.
    sql.exec("create vector index docs_vec on docs (v) with backend 'hnsw';");

    // rank by each document's best-matching chunk; snip is the text of
    // the chunk that won
    var hits = sql.exec(
        "select id, abstract(doc, 200, 'smart', ?, v) snip, $rank " +
        "from docs where v likev ?",
        [q, q], {maxRows: 10});

How the pieces behave with chunked rows:

* **Index / LIKEV** — every chunk is indexed under its row; a row's
  ``$rank`` is its best chunk's score (max over chunks).  A row whose
  cell count is a multiple of the query dimension is treated as chunked;
  a single-vector row is simply ``k = 1``, so chunked and plain rows mix
  freely in one column.
* **Dimension mismatches** — ``varvec*`` columns do not enforce a
  dimension (the declared size is a hint), so an INSERT whose vector
  doesn't fit the index always **succeeds at the table level**; what
  varies is how the index treats it:

  - a value **with a chunkembed() header** whose recorded ``dim``
    disagrees with the index dim is definitively wrong (a different
    embedding model, or corruption) — it is warned about and skipped
    at INSERT time and during CREATE/REBUILD scans, *even when* its
    total cell count happens to be a multiple of the index dim.  One
    bad row never fails a build; it is skipped and the rest proceed.
  - a **headerless** cell count that is a multiple of the index dim is
    indistinguishable from a legitimate chunked row and is silently
    indexed as ``k`` chunks — inserting a bare 1024-cell vector into a
    512-dim index makes a 2-chunk row, searchable by either half.
    Nothing can detect this case; keep your writers honest.
  - a cell count that is **not a multiple** of the index dim can never
    be served: the row is stored but is invisible to ``LIKEV``, and the
    engine warns at every point it notices — at INSERT time
    ("*inserted vector has N cells, not a multiple of the index dim
    D; the row is stored but will not be searchable via LIKEV*") and
    during a CREATE/REBUILD table scan.  The index itself is never
    corrupted — every reader validates before touching vector bytes.
  - NULL/empty vectors are legal, stored, and quietly non-matching.

  One caveat: rows written *while* the index is still being built
  can't be dimension-checked at INSERT time (the in-progress index
  doesn't expose its dim yet); a non-multiple row that slips in during
  that window is still harmless and invisible, just without the
  INSERT-time warning.
* **abstract()** — pass the vector column as a 5th argument
  (``abstract(doc, size, style, query, v)``) in a query whose ``WHERE``
  contains ``LIKEV`` on that column, and the snippet is seeded from the
  text span of the winning chunk.  An empty ``query`` string gives a
  pure vector snippet; a real query merges keyword hits with the chunk
  span.  See :ref:`abstract <sql-server-funcs:abstract>`.
* **excerpt()** — the retrieval (RAG) companion:
  ``excerpt(doc, maxsize, query, v[, window])`` returns the document's
  best-matching chunks *verbatim*, assembled in document order within
  ``maxsize`` bytes — context for an LLM rather than a display
  snippet.  See :ref:`excerpt <sql-server-funcs:excerpt>`.
* **Companion scalars** —
  :ref:`chunkavg() <sql-server-funcs:chunkavg>` returns the document's
  single combined vector (identical to ``embed()`` of the same text)
  for a separate coarse-search column, and
  :ref:`chunkcoherence() <sql-server-funcs:chunkcoherence>` returns a
  0-to-1 "how single-topic is this document" signal.  All of the
  embedding scalars share a per-model result cache
  (:ref:`likevCache <sql-set:likevCache>`), so

  .. code-block:: sql

      INSERT INTO docs VALUES (?, ?, chunkavg(?), chunkembed(?));

  embeds the document **once**, regardless of argument order.  (When
  using the per-chunk prefix, pass the *same* prefix to every scalar —
  ``chunkavg(?, '', ?title)`` etc. — the prefix is part of the cache
  key.)
* **UPDATE / DELETE** — the standard paths work; an updated row's old
  chunks are retired and the new ones indexed.
* **JavaScript** — a selected chunked value is a normal
  `Vector Object` with ``dim == k × 384``; use
  :ref:`split() <rampart-vector:Utility Functions>` to recover the
  individual chunk vectors: ``row.v.split(384)``.

Querying
~~~~~~~~

Similarity queries use the ``LIKEV`` predicate.  See
:ref:`Querying with LIKEV <querying-with-likev>` for the
authoritative reference; a brief overview:

.. code-block:: sql

    SELECT id, $rank
      FROM docs
     WHERE v LIKEV ?;

* The right-hand side of ``LIKEV`` is a vector.  A :green:`String` literal
  or bound :green:`String` parameter is auto-coerced via ``embed()`` using
  the currently-loaded model — so ``v LIKEV 'chocolate cake recipe'`` is
  equivalent to ``v LIKEV embed('chocolate cake recipe')``.
* ``$rank`` is the per-row score, scaled to ``[-100000, 100000]``.  With
  unit-norm vectors the practical range is ``[0, 100000]``.  Indexed
  ``LIKEV`` returns rows rank-ordered automatically.
* Use the ``maxRows`` parameter of ``exec()`` to retrieve top-K.
* ``$rank`` **is exact at the column's stored precision** — the value is
  computed per-row by ``rp_vector_distance`` on the actual stored column
  bytes, not on the index's quantized representation.  The IVFPQ
  approximation only affects which rows are returned as candidates (and
  in what initial order); once a row is in the candidate pool it is
  re-scored against the full column vector before ``$rank`` is set.
  No extra ``ORDER BY vecdist(...)`` re-rank pass is needed for top-K
  ordering.

``vecdist()`` is a separate function with its own uses, not a re-rank
companion to LIKEV.  Reach for it when you want:

* **A non-default metric.**  ``LIKEV`` uses dot product;
  ``vecdist(v, ?, 'l2')`` gives L2 distance.
* **A numeric distance value as a column** — for thresholding,
  application-side post-processing, or display alongside the row, when
  the scaled integer ``$rank`` isn't the form you want.
* **Distance to a *different* reference vector than the LIKEV query** —
  e.g. ``WHERE v LIKEV ?`` retrieves nearest neighbors of vector A while
  ``SELECT vecdist(v, ?B)`` annotates each hit with its distance to a
  second reference B.
* **Brute force without an index** — on small tables or for ad-hoc
  exploration; ``SELECT id, vecdist(v, ?) FROM docs ORDER BY 2 DESC``
  does a full scan without needing ``CREATE VECTOR INDEX``.

See :ref:`sql-server-funcs:vecdist` for the full signature.

Indexing
~~~~~~~~

For small tables (rule of thumb: below ~2,000 rows of 384-dim vectors), a
brute-force scan is fine — ``LIKEV`` works without an index, just slower
(set ``sql.set({allinear:true})`` first, or the index-less query returns
zero rows with the reason in ``sql.errMsg``).
Above that, build an ANN index:

.. code-block:: sql

    -- HNSW: near-exact, in-RAM, no minimum size
    CREATE VECTOR INDEX docs_vec ON docs (v) WITH backend 'hnsw';

    -- IVFPQ: scales beyond RAM, default backend, needs ~10k+ rows to train
    CREATE VECTOR INDEX docs_vec ON docs (v);

See :ref:`Vector Indexes <rampart-sql:Vector Indexes>` for the full
discussion of HNSW vs IVFPQ, the ``WITH`` option matrix, ``ALTER
INDEX … OPTIMIZE``, i8/u8 quantization, and maintenance.

Migrating between dtypes
~~~~~~~~~~~~~~~~~~~~~~~~

Use nested ``convert()`` calls to move data between vec dtypes — for
example, an existing column stored as ``varbyte`` holding f32 bytes that
you want to quantize down to a ``varvecF16`` column:

.. code-block:: sql

    -- Source: varbyte(384) with f32 bytes (1536 bytes per row)
    CREATE TABLE wikivecs2 (
        Idsec uint64, Vec varvecF16, Title varchar(64), Text varchar(2048)
    );

    INSERT INTO wikivecs2
      SELECT Idsec,
             convert(convert(Vec, 'varvecF32'), 'varvecF16'),
             Title, Text
        FROM wikivecs;

The inner ``convert(Vec, 'varvecF32')`` reinterprets the raw bytes as
varvecF32 (no data motion, just a type relabel).  The outer
``convert(..., 'varvecF16')`` then does real per-element f32→f16 rounding
through an f32 intermediate.  The same pattern works for any vec→vec
dtype pair (f64 ↔ f32 ↔ f16 ↔ bf16 ↔ i8 ↔ u8).

A single-step ``convert(varbyte, 'varvec*')`` is also supported but is a
**pure byte reinterpret**: it gives you a vec value whose bytes are the
varbyte's bytes, with ``dim = byte_count / target_elsz``.  This is the
right choice when the varbyte already holds bytes in the destination
dtype's layout (e.g. you stored f16 bytes in a varbyte column).  It is
*not* the right choice when the varbyte holds bytes in a different dtype's
layout — for that, use the nested form above so texis knows the source
dtype.

Two errors guard the byte path:

* The size check: if ``byte_count`` is not a multiple of the target
  element size (e.g. 15-byte varbyte → ``varvecF32`` would need a multiple
  of 4), ``convert()`` fails with a clear "byte length is not a multiple
  of element size" message rather than silently truncating.
* Intent-wrong size-aligned cases (the wikivecs example — 1536 f32 bytes
  reinterpreted as 768 f16 elements) cannot be detected automatically;
  always use the nested form when source and target dtypes differ.


String Functions
----------------
As Texis is adept at handling text information, it includes several
text handling :green:`Functions` which Rampart exposes for use in JavaScript.

stringFormat()
~~~~~~~~~~~~~~

The ``stringFormat()`` :green:`Function` is identical to the
:ref:`server function <sql-server-funcs:Server functions>`
:ref:`sql-server-funcs:stringformat`, except that it is not limited to five
arguments.

.. code-block:: javascript

    var Sql = require("rampart-sql");
    var output = Sql.stringFormat(format [,args, ...]);

+--------+------------------+---------------------------------------------------+
|Argument|Type              |Description                                        |
+========+==================+===================================================+
|format  |:green:`String`   | A printf() style format                           |
+--------+------------------+---------------------------------------------------+
|args    |Varies            | Arguments corresponding to ``%`` format options   |
+--------+------------------+---------------------------------------------------+

Return Value:
   The formatted :green:`String`.

Escape Sequences
''''''''''''''''

The following escape sequences are recognized in the format :green:`String`:

*   ``\n`` Newline (ASCII 10)
*   ``\r`` Carriage return (ASCII 13)
*   ``\t`` Tab (ASCII 9)
*   ``\a`` Bell character (ASCII 7)
*   ``\b`` Backspace (ASCII 8)
*   ``\e`` Escape character (ASCII 27)
*   ``\f`` Form feed (ASCII 12)
*   ``\v`` Vertical tab (ASCII 11)
*   ``\\`` Backslash
*   ``\xhh`` Hexadecimal escape. hh is 1 or more hex digits.
*   ``\ooo`` Octal escape. ooo is 1 to 3 octal digits.

Standard Formats
''''''''''''''''

A format code is a ``%`` (percent sign), followed by zero or more flag characters,
an optional width and/or precision size, and the format character itself. The
standard format codes, which are the same as in printf(), and how they print
their arguments are:

*   ``%d`` or ``%i`` Integer number.
*   ``%u`` Unsigned integer number.

*   ``%x`` or ``%X`` Hexadecimal (base 16) number; upper-case letters are
    used if upper-case X.

*   ``%o`` Octal (base 8) number.
*   ``%f`` Floating-point decimal number.

*   ``%e`` or ``%E`` Exponential floating-point number (e.g. 1.23e+05). Upper-case
    exponent if upper-case E.

*   ``%g`` or ``%G`` Either ``%f`` or ``%e`` format, whichever is shorter. Upper-case
    exponent if upper-case G.

*   ``%s`` A text string. The ``j`` flag may be given for newline
    translation.

*   ``%c`` A single character. If the argument is a decimal, hexadecimal
    or octal integer, it is interpreted as the ASCII code of the character
    to print.  If the ``!`` flag is given, a character is decoded instead:
    prints the decimal ASCII code for the first character of the argument.

*   ``%%`` A percent-sign; no argument and no flags are given. This
    is for printing out a literal ``%`` in the format :green:`String`, which
    otherwise would be interpreted as a format code.

A simple example (with its output):

.. code-block:: javascript

   var Sql=require("rampart-sql");
   var output = Sql.stringFormat("This is %s number %d (in hex: %x).",
   	 "test", 42, 42);
   /* output = "This is test number 42 (in hex: 2a)." */

Standard Flags
''''''''''''''
After the ``%`` sign (and before the format code letter), zero or more of the
following flags may appear:

..
  Warning: the ``⠀`` line below is not a space, it is a U+2800 Braille Pattern Blank
  the only way I could get a literal string containing one single white space character.

*   ``#`` (pound sign) Specifies that the value should be printed using an
    "alternate format", depending on the format code.  For format code(s):

   *   ``%o`` A non-zero result will be prepended with 0 (zero) in the output.
   *   ``%x``, %X A non-zero result will be prepended with ``0x`` or ``0X``.

   *   ``%e``, ``%E``, ``%f``, ``%g``, ``%G`` The result will always contain
       a decimal point, even if no digits follow it (normally, a decimal
       point appears in the results of those conversions only if a digit
       follows).  For ``%g`` and ``%G`` conversions, trailing zeros are not
       removed from the result as they would otherwise be.

   *   ``%b`` A non-zero result will be prepended with 0b.

*   ``0`` (digit zero) Specifies zero padding. For all numeric formats,
    the output is padded on the left with zeros instead of spaces.

*   ``-`` (minus sign) Indicates that the result is to be left
    adjusted in the output field instead of right.  A ``-`` overrides a
    ``0`` flag if both are present.

    For the ``%L`` extended code, this flag indicates the argument is a
    latitude.)

*   ``⠀`` (a space) Indicates that a space should be left before a positive
    number produced by a signed format (e.g.  ``%d``, ``%i``, ``%e``,
    ``%E``, ``%f``, ``%g``, or ``%G``).

*   ``+`` (plus sign) If given with a numeric code, indicates that a sign
    always be placed before a number produced by a signed format.  A ``+``
    overrides a space if both are used.

    For the ``%L`` extended code, a ``+`` flag indicates the argument is a
    location with latitude and longitude, or a geocode.

    If given with a string code, ``+`` indicates that if the :green:`String` value
    exceeds the given precision, truncate the :green:`String` by a further 3 bytes, and
    append an ellipsis ("...").  This can be useful to give an indication of
    when a value is being truncated on display.

Examples:

.. code-block:: javascript

   var Sql=require("rampart-sql");
   var output = Sql.stringFormat("%#x %#x", 42, 0);
   var output2= Sql.stringFormat("%+d %+d",  42, -42);
   /*
      output  = "0x2a 0"
      output2 = "+42 -42"
   */

Following any flags, an optional width :green:`Number` may be given.  This indicates
the minimum field width to print the value in (unless using the ``m`` flag;
see :ref:`Metamorph Hit Mark-up <metamorph-hit-markup>`).  If the printed value is narrower, the output
will be padded with spaces on the left.  Note the horizontal spacing in this
example:

.. code-block:: javascript

   var x = [42, 12345, 87654321, 912];
   for (var i=0; i<x.length; i++)
      console.log(Sql.stringFormat("%6d",x[i]));
   /* output:
       42
    12345
   87654321
      912
   */

After the width, a decimal point (``.``) and precision :green:`Number` may
be given.  For the integer formats (``%d``, ``%i``, ``%o``, ``%u``, ``%x``
and ``%X``), the precision indicates the minimum number of digits to print;
if there are fewer the output value is prepended with zeros.  For the
``%e``, ``%E`` and ``%f`` formats, the precision is the number of digits to
appear after the decimal point; the default is 6.  For the ``%g`` and ``%G``
formats, the precision is the maximum number of significant digits (default
6).  For the ``%s`` (string) format, it is the maximum number of characters
to print.

Examples:

.. code-block:: javascript

   var output = Sql.stringFormat("Error number %5.3d:", 5);
   /* output = "Error number   005:" */

   output = Sql.stringFormat("The %1.6s is %4.2f.",
      "answering machine", 123.456789);
   /* output="The answer is 123.46." */

The field width or precision, or both, may be given as a parameter instead
of a digit string by using an * (asterisk) character instead.  In this case,
the width or precision will be taken from the next (integer) argument.
Example (note spacing):

.. code-block:: javascript

   var width = 10;
   var prec = 2;
   var output = Sql.stringFormat("%*.*f", width, prec, 123.4567);
   /* output = "    123.46" */

An ``h`` or ``l`` (el) flag may appear immediately before the format code
for numeric formats, indicating a short or long value (``l`` has a different
meaning for ``%H``, ``%/`` and ``%:``, see :ref:`Extended Flags <extended-flags>`).  These flags
are for compatibility with the C function printf(), and are not generally
needed.

Printing Date/Time Values
'''''''''''''''''''''''''

Dates can be printed with ``stringFormat()`` by using the ``%at`` format.
The ``t`` code indicates a time is being printed, and the a flag indicates
that the next argument is a strftime()-style format string.  Following that
is a time argument.

Example:

.. code-block:: javascript

   var output=Sql.stringFormat("%at", "%B", "now");
   /* "%B" is the strftime()-style string
      (indicating the month should be printed) */

A capital ``T`` may be used insteadof lower-case ``t`` to change the timezone to
Universal Time (GMT/UTC) instead of local time for output.  These strftime()
codes are available:

*   ``%a`` for the abbreviated weekday name (e.g. Sun, Mon, Tue, etc.)
*   ``%A`` for the full weekday name (e.g. Sunday, Monday, Tuesday, etc.)
*   ``%b`` for the abbreviated month name (e.g. Jan, Feb, Mar, etc.)
*   ``%B`` for the full month name (e.g. January, February, March, etc.)
*   ``%c`` for the preferred date and time representation.
*   ``%d`` for the day of the month as a decimal number (range 01 through 31).
*   ``%H`` for the hour as a decimal number using a 24-hour clock (range 00 through 23).
*   ``%I`` for the hour as a decimal number using a 12-hour clock (range 01 through 12).
*   ``%j`` for the day of the year as a decimal number (range 001 through 366).
*   ``%m`` for the month as a decimal number (range 01 through 12).
*   ``%M`` for the minute as a decimal number (range 00 through 59).
*   ``%p`` for AM or PM, depending on the time.
*   ``%S`` for the second as a decimal number (range 00 through 60; 60 to allow for possible leap second if implemented).
*   ``%U`` for the week number of the current year as a decimal number, starting with the first Sunday as the first day of the first week (range 00 through 53).
*   ``%W`` for the week number of the current year as a decimal number, starting with the first Monday as the first day of the first week (range 00 through 53).
*   ``%w`` for the day of the week as a decimal, Sunday being 0.
*   ``%x`` for the preferred date representation without the time.
*   ``%X`` for the preferred time representation without the date.
*   ``%y`` for the year as a decimal number without a century (range 00 through 99).
*   ``%Y`` for the year as a decimal number including the century.
*   ``%Z`` for the time zone or name or abbreviation.
*   ``%%`` for a literal ``%`` character.

Since ``stringFormat`` arguments are typecast if needed, the date argument can be
a Texis date or counter type, or a Texis-parseable date string.  For
example, to print today's date in the form month/day/year:

.. code-block:: javascript

   var output=Sql.stringFormat("%at", "%m/%d/%y", "now");
   console.log(output);


Or to print the title and insertion date of books matching a query, in the
style "February 20, 1997" (assuming the id field is a Texis counter data type):

.. code-block:: javascript

   sql.exec("select id, Title from books where Desc like ?q",
            {q:query},
            function(res) {
               console.log(
               	Sql.stringFormat("%at %s", "%B %d, %Y", res.id, res.Title)
               );
            }
   );

To use a default strftime() format, eliminate the ``a`` flag and its corresponding strftime() format argument:

.. code-block:: javascript

	var curDate = Sql.stringFormat("%t", "now");

This will print today's date in a default format.


CAVEAT:
   As dates are printed using the standard C library, not all strftime() codes are available or behave identically on all platforms.


Latitude, Longitude and Location
''''''''''''''''''''''''''''''''

The ``%L`` code may be used with ``stringFormat`` to print a latitude, longitude
or location (geocode) value, in a manner similar to how date/time values are
printed with ``%t``.  Flags indicate what type of value is expected, and/or if a
subformat is provided:

*   ``-`` (minus) A latitude argument is expected (memory aid: latitude
    lines are horizontal, so is minus sign).  This is the default.

*   ``|`` (pipe) A longitude is expected (memory aid: longitude lines are
    vertical; so is pipe).

*   ``+`` (plus) A location is expected; either a geocode long value, or a
    latitude and longitude (e.g.  comma-separated).

*   ``a`` Like ``%at`` (date/time format), the next argument (before the
    latitude/longitude/location) is a subformat indicating how to print the
    latitude and/or longitude.  Without this flag, no subformat argument is
    expected, and a default subformat is used.

Latitude, longitude and location arguments should be in one of the formats
supported by the
:ref:`parselatitude() <sql-server-funcs:parselatitude, parselongitude>`,
:ref:`parselongitude() <sql-server-funcs:parselatitude, parselongitude>`,
or :ref:`latlon2geocode() <sql-server-funcs:latlon2geocode, latlon2geocodearea>`
(with single arg) SQL functions, as appropriate.  If the ``a`` flag is given,
the subformat string may contain the following codes:

*   ``%D`` for degrees
*   ``%M`` for minutes
*   ``%S`` for seconds
*   ``%H`` for the hemisphere letter ("N", "S", "E" or "W")
*   ``%h`` for the hemisphere sign ("+" or "-")
*   ``%o`` for an ISO-8859-1 degree sign
*   ``%O`` for a UTF-8 degree sign
*   ``%%`` for a percent sign

A field width, precision, space, zero and/or minus flags may be given with
the ``%D``/``%M``/``%S`` codes, with the same meaning as for numeric
``stringFormat()`` codes.  If no flags are given to a code, the width is set
to 2 (or 3 for longitude degrees), with space padding for degrees and zero
padding for minutes and seconds.

Additionally, a single ``d``, ``i``, ``f`` or ``g`` numeric-type flag may be
given with the ``%D``/``%M``/``%S`` codes.  This flag will print the value
with the corresponding ``stringFormat()`` numeric code, e.g.  truncated to
an integer for ``d`` or ``i``, floating-point with potential roundoff for
``f`` or ``g``.  This flag is only valid for the smallest unit
(degrees/minutes/seconds) printed: larger units will always be printed in
integer format.  This ensures that a fractional value will not be printed
twice erroneously, e.g.  20.5 degrees will not have its ".5" degrees
fractional part printed if "30" minutes is also being printed, because the
degrees numeric-type will be forced to integer regardless of flags.

The default numeric-type flag is ``g`` for the smallest unit.  This helps ensure
values are printed with the least number of decimal places needed (often
none), yet with more (sub-second) accuracy if specified in the original
value.  Additionally, for the ``g`` type, if a degrees/minutes/seconds value is
less than ( 10^-(p-2) ), where p is the format code's precision (default 6),
it will be truncated to 0.  This helps prevent exponential-format printing
of values, which is often merely an artifact of floating-point roundoff
during unit conversion, and not part of the original user-specified value.

Examples:

.. code-block:: javascript

   sql.exec("create table geotest(city varchar(64), lat double, lon double, geocode long);");
   sql.exec("insert into geotest values('Cleveland, OH, USA', 41.4,  -81.5,  -1);");
   sql.exec("insert into geotest values('Seattle, WA, USA',   47.6, -122.3,  -1);");
   sql.exec("insert into geotest values('Dayton, OH, USA',    39.75, -84.19, -1);");
   sql.exec("insert into geotest values('Columbus, OH, USA',  39.96, -83.0,  -1);");
   sql.exec("update geotest set geocode = latlon2geocode(lat, lon);");
   sql.exec("create index xgeotest_geocode on geotest(geocode);");

   var nres=sql.exec("select city, lat, lon, geocode, distlatlon(41.4, -81.5, lat, lon) MilesAway "+
      "from geotest " +
      "where geocode between (select latlon2geocodearea(41.4, -81.5, 3.0)) " +
      "order by 4 asc;",
      function(row,i) {
         console.log(i+1,row);
         console.log(Sql.stringFormat("  Loc: %+L", row.geocode));
      }
   );
   /* expected output:
   1 {city:"Dayton, OH, USA",lat:39.75,lon:-84.19,geocode:253806089136,MilesAway:181.31350567274416}
     Loc: 39°45'00"N  84°11'24"W
   2 {city:"Columbus, OH, USA",lat:39.96,lon:-83,geocode:253824238336,MilesAway:126.70040182902217}
     Loc: 39°57'36"N  83°00'00"W
   3 {city:"Cleveland, OH, USA",lat:41.4,lon:-81.5,geocode:253913441856,MilesAway:0}
     Loc: 41°24'00"N  81°30'00"W
   */


Other Format Codes
''''''''''''''''''

In addition to the standard printf() formatting codes, other
``stringFormat`` codes are available:

*   ``%t``, ``%T`` strftime()-style output of a date or counter field (see
    above)

*   ``%L`` Output of a latitude, longitude, or location (geocode); see above

*   ``%H`` Prints its string (e.g.  varchar) argument, applying HTML escape
    codes where needed to make the string "safe" for HTML output (``"``,
    ``&``, ``<``, ``>``, ``DEL`` and control chars less than 32 except
    ``TAB``, ``LF``, ``FF`` and ``CR`` are escaped).  With the ``!`` flag,
    decodes instead (to ISO-8859-1); see also the ``l`` (el) flag, here.
    The ``j`` flag (here) may be given for newline translation.  When
    decoding with ``!``, out-of-ISO-8859-1-range characters are output as
    ``?``; to decode HTML to UTF-8 instead, use ``%hV``.

*   ``%U`` Prints its string argument, encoding for a URL, i.e using
    %-codes.  With the !  flag, decodes instead.  With the p (path) flag,
    spaces are encoded as ``%20`` instead of ``+``.  With the ``q`` flag,
    ``/`` (slash) and ``@`` (at-sign) are encoded as well (or only
    unreserved/safe chars are decoded, if ``!``  too).
    See :ref:`Extended Flags <extended-flags>`.

*   ``%V`` (upper-case vee) Prints its string argument, encoding 8-bit
    ISO-8859-1 chars for UTF-8 (compressed Unicode).  With the ``!``  flag,
    decodes instead (to ISO-8859-1).  Illegal, truncated, or out-of-range
    sequences are translated as question-marks (?); this can be modified with
    the ``h`` flag (here).  The ``j`` flag (here) may be given for newline
    translation.

*   ``%v`` (lower-case vee) Prints its UTF-8 string argument, encoding to
    UTF-16.  With the ``!`` flag (here), decodes to UTF-8 instead.
    Illegal, truncated, or out-of-range sequences are translated as ``?``
    (question-marks).  This can be modified with the ``h`` flag.  The ``<``
    (less-than) flag forces UTF-16LE (little-endian) output (encode) or
    treats input as little-endian (decode).  The ``>`` flag forces UTF-16BE
    (big-endian) output (encode) or treats input as big-endian (decode).
    The default endian-ness is big-endian; for decode, a leading
    byte-order-mark character (hex 0xFEFF) will determine endian-ness if
    present.  The ``_`` (underscore) flag skips printing a leading
    byte-order-mark when encoding; when decoding the ``_`` flag saves (does
    not delete) a leading byte-order-mark in the input.  The ``j`` flag may
    be given for newline translation.

*   ``%B`` Prints its string argument, encoding to base64.  If a non-zero
    field width is given, a newline is output after every "width" bytes output
    (absolute value, rounded up to 4) and at the end of the base64 output.
    Thus "%64B" would format with no more than 64 bytes per line.  This is
    useful for encoding into a MIME mail message with line length restraints.
    A ``!`` flag indicates that the string is to be decoded instead of encoded.
    The ``j`` flag (here) may be given to set the newline style, though it only
    applies to soft (output) newlines; input CR/LF bytes are never modified
    since base64 is a binary encoding.

*   ``%Q`` Prints its string argument, encoding to quoted-printable (per RFC
    2045).  If a non-zero field width is given, a newline is output after
    every "width" bytes output (absolute value, rounded up where needed).  A
    negative field width or ``-`` flag indicates "binary" encoding: input CR and
    LF bytes are also hex-encoded; normally they are output as-is (or subject
    to the ``j`` flag, here) and therefore subject to possible newline translation
    by a mail transfer agent etc.  A ``!`` flag indicates that decoding instead
    of encoding is to be done (and the field width and negative flag are
    ignored).  The ``j`` flag (here) may be given for newline translation.  If an
    ``_`` (underscore) flag is given, "Q" encoding (per RFC 2047) is used instead
    of quoted-printable: it is similar, except that U+0020 (space) is output
    as underscore (_), no whitespace is ever output (e.g.  tab/CR/LF are
    hex-encoded, and the field width is ignored), and certain other special
    characters are hex-encoded that normally would not be (e.g.  dollar sign,
    percent, ampersand etc.).  With the underscore flag, the resulting output
    is safe for all RFC 2047 "Q" encoding contexts.

*   ``%W`` Prints its UTF-8 string argument, encoding
    linear-whitespace-separated tokens to RFC 2047 encoded-word format
    (i.e.  "=?...?=" mail header tokens) as needed.  Tokens that do not
    require encoding are left as-is.  A ``!`` flag indicates that decoding
    instead of encoding should be done.  A ``q`` flag for ``%W`` indicates
    that only the "Q" encoding should be used for encoded words; normally
    either "Q" or base64 - whichever is shorter - is used.  The ``hh``,
    ``hhh``, ``j``, ``^`` and ``|`` flags are respected.  The ``h`` flag is
    also supported for %``!W``.  If a non-zero field width is given, it is
    used as the desired maximum byte length of encoded words: if an encoded
    word would be longer than this, it is split atomically into multiple
    words, separated by newline-space.

*   ``%z`` Prints its argument, encoded (compressed) in the gzip deflate
    format.  The ``!`` flag will decode (decompress) the argument instead.
    A precision value will limit the output to that many bytes, as with
    ``%s``; this can be used to "peek" at the start of compressed data
    without decoding all of it (and consuming memory to do so).

*   For either encode or decode, a single ``l`` flag may be given to indicate
    zlib deflate format instead, or a ``ll`` (double el) to indicate raw
    deflate format instead.  All variants use the same deflate algorithm,
    but gzip adds (typically) 18 bytes of headers/footers, zlib 6, and raw
    none.  Additionally, decoding with ``%!z`` (no flags) will accept any
    of the three variants.

*   ``%b`` Binary output of an integer.

*   ``%F`` Prints a float as a fraction: whole number plus fraction.

*   ``%r`` Lowercase Roman numeral output of an integer.

*   ``%R`` Uppercase Roman numeral output of an integer.

All the standard flags, as well as the extended flags (below), can be given
to these codes, where applicable.

Examples:

.. code-block:: javascript

   console.log(
      Sql.stringFormat("Year %R %H %R", 1977, "<", 1997)
   );
   /* Year MCMLXXVII &lt; MCMXCVII */

   console.log(
      Sql.stringFormat("%F", 5.75)
   );
   /* 5 3/4 */

.. _extended-flags:

Extended Flags
''''''''''''''

The following flags are available for format codes, in addition to the standard
printf() flags described above:

*   ``a`` Next argument is strftime() format string; used for ``%t``/``%T``
    time code (here).

*   ``k`` For numeric formats, print a comma (,) every 3 places to the left
    of the decimal (e.g.  every multiple of a thousand).

*   ``K`` (upper case "K") Same as ``k``, but print the next argument instead of
    a comma.

*   ``&`` (ampersand) Use the HTML entity ``&nbsp``; instead of space when
    padding fields.  This is of some use when printing in an HTML
    environment where spaces are normally compressed when displayed, and
    thus space padding would be lost.

*   ``!`` (exclamation point) When used with ``%H``, ``%U``, ``%V``, ``%B``,
    ``%c``, ``%W`` or ``%z``, decode appropriately instead of encoding.
    (Note that for ``%H``, only ampersand-escaped entities are decoded)

*   ``_`` (underscore) Use decimal ASCII value 160 instead of 32 (space)
    when padding fields.  This is the ISO Latin-1 character for the HTML
    entity &nbsp;.  For the ``%v`` (UTF-16 encode) format code, a leading
    BOM (byte-order-mark) will not be output.  For the ``%!v`` (UTF-16
    decode) format code, a leading BOM in the input will be preserved
    instead of stripped in the output.  For the ``%Q``/``%!Q``
    (quoted-printable encode/decode) format codes, the "Q" encoding will be
    used instead of quoted-printable.

*   ``^`` (caret) Output only XML-safe characters; unsafe characters are
    replaced with a question mark.  Valid for ``%V``, ``%=V``, ``%!V``,
    ``%v``, ``%!v``, ``%W``, ``%!W`` and ``%s`` format codes (text is
    assumed to be ISO-8859-1 for ``%s``).  XML safe characters are all
    characters except: ``U+0000`` through ``U+0008`` inclusive, ``U+000B``,
    ``U+000C``, ``U+000E`` through ``U+001F`` inclusive, ``U+FFFE`` and
    ``U+FFFF``.

*   ``=`` (equal sign) Input encoding is "equal to" (the same) as output
    encoding, i.e.  just validate it and replace illegal encoding sequences
    with "?".  Unescaping of HTML sequences in the source (``h`` flag) is
    disabled.  Valid for ``%V`` format code.

*   ``|`` (pipe) Interpret illegal encoding sequences in the source as
    individual ISO-8859-1 bytes, instead of replacing with the "?"
    character.  When used with ``%=V`` for example, this allows UTF-8 to be
    validated and passed through as-is, yet isolated ISO-8859-1 characters
    (if any) will still be converted to UTF-8.  Valid for ``%!V``, ``%=V``,
    ``%v``, ``%W`` and %``!W`` format codes.

*   ``h`` For ``%!V`` (UTF-8 decode) and ``%v`` (UTF-16 encode): if given once,
    HTML-escapes out-of-range (over 255 for ``%!V`` , over ``0x10FFFF`` for
    %v) characters instead of replacing with ``?``.  For ``%V`` (UTF-8
    encode) and ``%!v`` (UTF-16 decode): if given once, unescapes HTML
    sequences first; this allows characters that are out-of-range in the
    input encoding to be represented natively in the output encoding.  For
    ``%V``, ``%!V``, ``%v``, ``%!v``, ``%W`` and ``%!W``, if given twice
    (e.g.  ``hh``), also HTML-escapes low (7-bit) values (e.g.  control
    chars, ``<``, ``>``) in the output.  If given three times (e.g.
    ``hhh``), just HTML-escapes 7-bit values; does not also decode HTML
    entities in the input.  Note that the ``h`` flag is also used in another
    context as a sub-flag for :ref:`Metamorph Hit Mark-up <metamorph-hit-markup>`.

*   ``j`` (jay)   For the ``%s``, ``%H``, ``%v``, ``%V``, ``%B`` and ``%Q``
    format codes (and their ``!``-decode variants), also do newline
    translation.  Any of the newline byte sequences CR, LF, or CRLF in the
    input will be replaced with the machine-native newline sequence in the
    output, instead of being output as-is.  This allows text newlines to be
    portably "cleaned up" for the current system, without having to detect
    what the system is.  If ``c`` is given immediately after the ``j``,
    ``CR`` is used as the output sequence, instead of the machine-native
    sequence.  If ``l`` (el) is given immediately after the ``j``, ``LF`` is
    used as the output sequence.  If both ``c`` and ``l`` are given (in
    either order), CRLF is used.  The ``c`` and ``l`` subflags allow a
    non-native system's newline convention to be used, e.g.  by a web
    application that is adapting to browsers of varying operating systems.
    Note that for the ``%B`` format code, input CR/LF bytes are never
    translated (since it is a binary encoding); ``j`` and its subflags only
    affect the output of "soft" line-wrap newlines that do not correspond to
    any input character.

*   ``l`` (el) For ``%H``, only encode low (7-bit) characters; leave characters
    above 127 as-is.  This is useful when HTML-escaping UTF-8 text, to avoid
    disturbing multi-byte characters.  When combined with ``!`` (decode),
    escape sequences are decoded to low (7-bit) strings, e.g.  "&copy;" is
    replaced with "(c)" instead of ASCII character 169.  (The ``l`` flag is
    also used with numeric format codes to indicate a long integer or
    double, and with the ``j`` flag as a subflag.) The l flag has yet
    another meaning when used with the %/ or %: format codes; see discussion
    of those codes above.

*   ``m`` For the ``%s``, ``%H``, ``%V`` and ``%v`` codes, mark up with a
    Metamorph query.  See next section for a discussion of this flag and its
    subflags ``b``, ``B``, ``U``, ``R``, ``h``, ``n``, ``p``, ``P``, ``c`` and
    ``e``.

*   ``p`` Perform paragraph markup (for ``%s`` and ``%H`` codes).  Paragraph breaks
    (text matching the REX expression "$=\space+") are replaced with "<p/>"
    tags in the output.  For the ``%U`` code, do path escapement: space is encoded
    to ``%20`` not ``+``, and  ``&+;=`` are left as-is and ``+`` is
    not decoded when also using ``!``.

*   ``P`` (upper case "P") For ``%s`` and ``%H``, same as p, but use the next
    additional argument as the REX expression to match paragraph breaks.  If
    given twice (PP), use another additional argument after the REX expression
    as the replacement string, instead of "<p/>".  PP was added in version 6.

*   ``q`` For the %U code, in version 7 and earlier, do full-encoding:
    encode "/" (forward slash) and "@" (at-sign) as well (implies ``p`` flag as
    well).

For the %W code, only the "Q" encoding will be used (no base64).

Example:

.. code-block:: javascript

   var output = Sql.stringFormat("You owe $%10.2kf to us.", 56387.34);
   /* output  = "You owe $ 56,387.34 to us." */

.. _metamorph-hit-markup:

Metamorph Hit Mark-up
'''''''''''''''''''''

The ``%s``, ``%H``, ``%V`` and ``%v`` stringFormat codes can execute Metamorph queries on the
:green:`String` argument and mark-up the resulting hits.  An ``m`` flag to these codes
indicates that Metamorph hit mark-up should occur; the Metamorph query
string is then taken to be the next argument (before the normal :green:`String`
argument to be searched and printed).  The ``m`` flag and its sub-flags are only
valid for the ``%s`` and ``%H`` codes.

Following the m flag can be any of the following sub-flags.  These must
immediately follow the m flag, as some letters have other meanings
elsewhere:

*   ``I`` for inline stylesheet (<span style=...>) highlighting with different styles per term
*   ``C`` for class (<span class=...>) highlighting with different classes per term
*   ``b`` for HTML bold highlighting of hits
*   ``B`` for VT100 bold highlighting of hits
*   ``U`` for VT100 underline highlighting of hits
*   ``R`` for VT100 reverse-video highlighting of hits
*   ``h`` for HTML HREF highlighting (default)
*   ``n`` indicates that hits that overlap tags should not be truncated/moved
*   ``p`` for paragraph formatting: print "<p/>" at paragraph breaks

*   ``P`` same as ``p``, but use (next additional argument) REX expression to
    match paragraph breaks.  If given twice (``PP``), use another additional
    argument after REX expression as replacement string, instead of "<p/>".

*   ``c`` to continue hit count into next query call
*   ``N`` to mark up NOT terms as well
*   ``q`` to mark up the query itself, not the text, e.g. as a legend


.. queryfixupmode is not currently available - todo: figure out how this applies

   *   ``e`` to mark up the exact query (no queryfixupmode/NOT processing)

Examples:

To highlight query terms from ``query`` in the text contained in
``text`` in different colors, insert paragraph breaks, and escape the output
to be HTML-safe, use:

.. code-block:: javascript

   var query = "format javascript";
   var text = "Highlight formatting made easy in javascript.\n\n<Try some formatting today!>";
   var output = Sql.stringFormat("%mIpH", query, text);
   /* output  = `
   Highlight <span style="background:#ffff66;color:black;font-weight:bold;">formatting</span> made easy in <span style="background:#a0ffff;color:black;font-weight:bold;">javascript</span>.
   <p/>

   &lt;Try some <span style="background:#ffff66;color:black;font-weight:bold;">formatting</span> today!&gt;`
   */

To highlight query terms from ``query`` in ``text`` in bold with anchors
and links, insert paragraph breaks, and escape the output
to be HTML-safe, use:

.. code-block:: javascript

   var query  = "format javascript";
   var text   = "Highlight formatting made easy in javascript.\n\n<Try some formatting today!>";
                                 /* qc = mark up query itself and continue counting hits   *
                                  *                 hb = create links, highlight in bold   *
                                  *                   pH = mark paragraphs and html escape */
   var output = Sql.stringFormat("%mqchbpH\n<p/>\n%mhbpH", query, "", query, text);
   /* output  = `
   <a name="hit1" href="#hit2"><b>format</b></a> <a name="hit2" href="#hit3"><b>javascript</b></a>
   <p/>
   Highlight <a name="hit3" href="#hit4"><b>formatting</b></a> made easy in <a name="hit4" href="#hit5"><b>javascript</b></a>.
   <p/>

   &lt;Try some <a name="hit5" href="#hit1"><b>formatting</b></a> today!&gt;`
   */


.. _inlineprops:


:ref:`sql-set:Server Properties` may be given inline.  For example, in the
above example, if you did not want to match "formatting" from the query term
"format" but still wanted to highlight "javascript" where "format" is not
present (``@0`` for zero intersections; see
`this section <https://docs.thunderstone.com/site/texisman/specifying_fewer_intersections.html>`__
of the Texis documentation for full explanation),
the following could be used:


.. code-block:: javascript

                /* no suffix proc, 0 intersections required */
   var query  = "@suffixproc=0 @0 format javascript";
   var text   = "Highlight formatting made easy in javascript.\n\n<Try some formatting today!>";
   var output = Sql.stringFormat("%mbpH", query, text);
   /* output  = `
   Highlight formatting made easy in <b>javascript</b>.
   <p/>

   &lt;Try some formatting today!&gt;
   */

Discussion:
   :blue:`⠀`

Each hit found by the query has each of its sets' hits (e.g.  each term)
highlighted in the output.  With ``I`` and/or ``C`` highlighting, if there are
delimiters used in the query, the entire delimited region is also
highlighted.  The Metamorph query uses the same apicp defaults and
parameters as SQL queries.  These can be changed as described
:ref:`above <inlineprops>`.

If a width is given for the format code, it indicates the character offset
in the string argument to begin the query and printing (0 is the first
character).  Thus a large text argument can be marked up in several chunks.
Note that this differs from the normal behavior of the width, which is to
specify the overall width of the field to print in.  The precision is the
same - it gives the maximum number of characters of the input string to
print - only it starts counting from the width.

The ``h`` flag sets HREF highlighting (the default).  Each hit becomes an HREF
that links to the next hit in the output, with the last hit pointing back to
the first.  In the output, the anchors for the hits are named hitN, where N
is the hit number (starting with 1).

Hits can be bold highlighted in the output with the ``b`` flag; this surrounds
them with ``<b>`` and ``</b>`` tags.  ``b`` and ``h`` can be combined; the default if
neither is given is HREF highlighting.

The ``B`` and ``U`` flags may be given for VT100-terminal bold and underline
highlighting; this may be useful for command-line scripts.  The ``R`` flag
may be given for VT100-terminal reverse-video highlighting.

The ``I`` or ``C`` flags may be given, for inline styles or
classes.  This allows much more flexibility in defining the markup, as a
style or class for each distinct query term may then be defined.

The ``q`` flag may be given, to highlight the query itself, instead of the
following text buffer (which must still be given but is ignored).  This can
be used at the top of a highlighted document to give a highlighting "legend"
to illustrate what terms are highlighted and how.  The ``n`` and ``e`` flags
are also implicitly enabled when ``q`` is given.  Note that settings given
inline with the query (e.g.  "@suffixproc=0") will not be highlighted since
they do not themselves ever find or match any terms - this helps avoid
misleading the user that such "terms" will ever be found in the text.
However, since they are still considered separate query sets - because their
order in the query is significant, as they only affect following sets - a
class/style is "reserved" (i.e.  not used) for them in the querycyclenum
rotation.

Normally, hits that overlap HTML tags in the search string are truncated or
moved to appear outside the tag in the output, so that the highlighting tags
do not overlap them and muddle the HTML output.  The ``n`` tag indicates that
this truncation should not be done.  (It is also not done for the ``%H`` (HTML
escapement) format code, since the tags in the string will be escaped
already.)

The ``p`` and ``P`` flags do paragraph formatting as documented previously.

The ``c`` flag indicates that the hit count should be continued for the next
query.  By default, the last hit marked up is linked back to the first hit.
Therefore, each ``%``-code query markup is self-contained: if multiple calls are
made, the hit count (and resulting HREFs) will start over for each call,
which may not be desired.  If the ``c`` flag is given, the last hit in the
string is linked to the "next" hit (N+1) instead of the first, and the next
query will start numbering hits at N+1 instead of 1.  Thus, all but the last
query markup call by a script should use the ``c`` flag.

.. Need help with this-
   The ``e`` flag indicates that the query should be used exactly as given.
   Normally, queryfixupmode (here) and ``N`` flag processing is done to the query,
   which might cause more terms to be highlighted than are actually found by
   the query (e.g.  highlighting of sets in the query that are not needed to
   resolve it, if not all sets are required).  With ``e`` set, such processing is
   not done, and some apparent hits may be left unhighlighted.

   See queryfixupmode (here) for details on how the query is modified when
   ``e`` is not given.

The following example creates an abstract, marks up each abstract value from
a table that matches the user's submitted query string.  Each set (term) is
color-coded differently, and the ``abstract(body)`` is HTML-escaped:

.. code-block:: javascript

   var results='<div class="results">';
   sql.exec("select abstract(body) abs from data_tbl where body like ?q",
   	{q:query},
   	function(row) {
   	   results += Sql.stringFormat('<div class="hit">%mIH</div>', query, row.abs);
   	}
   );
   results +="</div>";

For more information on ``abstract``, see `abstract()`_ below and
``abstract`` in :ref:`sql-server-funcs:Server functions`.

abstract()
~~~~~~~~~~

The abstract function generates an abstract of a given portion of text.

.. code-block:: javascript

   var Sql = require("rampart-sql");
   var options=
      {
         max: max,
         style: style,
         query: query
      };
   var abstract = Sql.abstract(text, options);

**or**

.. code-block:: javascript

    var Sql = require("rampart-sql");
    var abstract = Sql.abstract(text [,max [,style [,query [,markup]]]]);


+--------+------------------+-----------------------------------------------------------------------------+
|Argument|Type              |Description                                                                  |
+========+==================+=============================================================================+
|text    |:green:`String`   | The text from which an abstract will be generated.                          |
+--------+------------------+-----------------------------------------------------------------------------+
|max     |:green:`Number`   | Maximum length in bytes of the abstract.                                    |
+--------+------------------+-----------------------------------------------------------------------------+
|style   |:green:`String`   | Method used to generate the abstract.                                       |
+--------+------------------+-----------------------------------------------------------------------------+
|query   |:green:`String`   | query or keywords used to center the abstract.                              |
+--------+------------------+-----------------------------------------------------------------------------+
|markup  |:green:`String` or| perform markup as :ref:`Metamorph Hit Mark-up <metamorph-hit-markup>` above.|
|        |:green:`Boolean`  | May be ``true`` for "%mbH" or a :green:`String`                             |
|        |                  | for a custom format (such as "%mCH").                                       |
+--------+------------------+-----------------------------------------------------------------------------+

Return Value:
   :green:`String`. The abstract text.

The abstract will be less than ``max`` bytes long, and will attempt to
end at a word boundary (a multi-byte UTF-8 character is never split).
If ``max`` is not specified (or is less than or equal to 0) then a
default size of 230 bytes is used.

The ``style`` argument allows a choice between several different ways of
creating the abstract.  Note that some of these styles require the ``query``
argument as well, which is a Metamorph search query:

*   ``dumb`` Start the abstract at the top of the document.

*   ``smart`` This style will look for the first meaningful chunk of text,
    skipping over any headers at the top of the text.  This is the default if
    neither ``style`` nor ``query`` is given.

*   ``querysingle`` Center the abstract contiguously on the best occurrence
    of ``query`` in the document.

*   ``querymultiple`` Like ``querysingle``, but also break up the abstract into
    multiple sections (separated with ``...``) if needed to help ensure all
    terms are visible.  Also it will take care with URLs to try to show the start
    and end.

*   ``querybest`` An alias for the best available query-based style; currently the
    same as ``querymultiple``.  Using ``querybest`` in a script ensures that
    if improved styles become available in future releases, the script will
    automatically "upgrade" to the best style.


If no ``query`` is given with a ``query*`` mode (``querysingle``,
``querymultiple`` or ``querybest``), it falls back to ``dumb`` mode.
If a ``query`` is given with anything other than a ``query*`` mode
(``dumb``/``smart``), the mode is promoted to ``querybest``.  The current locale
and index expressions also have an effect on the abstract in the ``query*``
modes, so that it more closely reflects an index-obtained hit.

Example:

.. code-block:: javascript

   var gba= "Four score and seven years ago our fathers brought forth on " +
   "this continent, a new nation, conceived in Liberty, and dedicated to " +
   "the proposition that all men are created equal.\n" +

   "Now we are engaged in a great civil war, testing whether that nation, " +
   "or any nation so conceived and so dedicated, can long endure.  We are " +
   "met on a great battle-field of that war.  We have come to dedicate a " +
   "portion of that field, as a final resting place for those who here " +
   "gave their lives that that nation might live.  It is altogether " +
   "fitting and proper that we should do this.\n" +

   "But, in a larger sense, we can not dedicate -- we can not consecrate " +
   "-- we can not hallow -- this ground.  The brave men, living and dead, " +
   "who struggled here, have consecrated it, far above our poor power to " +
   "add or detract.  The world will little note, nor long remember what we " +
   "say here, but it can never forget what they did here.  It is for us " +
   "the living, rather, to be dedicated here to the unfinished work which " +
   "they who fought here have thus far so nobly advanced.  It is rather " +
   "for us to be here dedicated to the great task remaining before us -- " +
   "that from these honored dead we take increased devotion to that cause " +
   "for which they gave the last full measure of devotion -- that we here " +
   "highly resolve that these dead shall not have died in vain -- that " +
   "this nation, under God, shall have a new birth of freedom -- and that " +
   "government of the people, by the people, for the people, shall not " +
   "perish from the earth.\n";

   var abstract = Sql.abstract(gba);
   /* abstract =
      Four score and seven years ago our fathers brought forth on this
      continent, a new nation, conceived in Liberty, and dedicated to the
      proposition that all men are created equal.  Now we are engaged in a
      great civil war, testing ...
   */

   abstract = Sql.abstract(gba, 100, "querybest", "unfinished work");
   /* abstract =
      It is for us the living, rather, to be dedicated here to the
      unfinished work which they who fought ...
   */

   abstract = Sql.abstract(gba, {
       max:250,
       style: "querybest",
       query: "unfinished work",
       markup: "%mCH"
   });
   /* abstract =
      The world will little note, nor long remember what we say here, but it can
      never forget what they did here. It is for us the living,
      rather, to be dedicated here to the <span class="query queryset1">unfinished</span>
      <span class="query queryset2">work</span> which they who fought here have thus far
      so nobly advanced. It is ...
   */

sandr()
~~~~~~~

The ``sandr`` function replaces in ``data`` every occurrence of ``expr``
(`rex()`_ expression(s)) with the corresponding :green:`String`\ (s) from ``replace``.  It
returns ``dataOut``, a :green:`String` or :green:`Array` of :green:`Strings` with any replacements.

If ``replace`` has fewer values than ``expr``, it is "padded" with empty
replacement :green:`Strings` for the extra search values.

.. code-block:: javascript

   var Sql = require("rampart-sql");

   var dataOut = Sql.sandr(expr, replace, data);

   /* or */

   var dataOut = Sql.sandr(rexargs, data);

+--------+--------------------------------------------------------+---------------------------------------------------+
|Argument|Type                                                    |Description                                        |
+========+========================================================+===================================================+
|expr    |:green:`String`/:green:`Array` of :green:`Strings`      | `rex()`_ expression(s) to search for              |
+--------+--------------------------------------------------------+---------------------------------------------------+
|replace |:green:`String`/:green:`Array` of :green:`Strings`      | Text to replace the `rex()`_ expressions          |
+--------+--------------------------------------------------------+---------------------------------------------------+
|rexargs |:green:`Array` of :green:`Arrays` - search/replace pairs| pairs of expr and replace strings                 |
+--------+--------------------------------------------------------+---------------------------------------------------+
|data    |:green:`String`/:green:`Array` of :green:`Strings`      | string(s) as input for search and replace         |
+--------+--------------------------------------------------------+---------------------------------------------------+


Return Value:
   If ``data`` is an :green:`Array`, an :green:`Array` of :green:`Strings` corresponding to the ``data``
   :green:`Array` with replacements made.

   If ``data`` is a :green:`String`, a :green:`String` corresponding to the ``data`` :green:`String` with
   replacements made.

Replacement Strings:
''''''''''''''''''''

   *   The characters ``?`` ``#`` ``{`` ``}`` ``+`` and ``\`` are special.
       To use them literally, precede them with the escapement character
       ``\``.

   *   Replacement strings may just be a literal string or they may include
       the "ditto" character ``?``.  The ditto character will copy the character
       in the position specified in the replace-string from the same position
       in the located expression.

   *   A decimal digit placed within curly-braces (e.g.  {5}) will place
       that character of the located expression to the output.

   *   A ``\`` followed by a decimal number will place that subexpression to
       the output.  Subexpressions are numbered starting at 1.

   *   The sequence ``\&`` will place the entire expression match (not
       including ``\P`` and ``\F`` portions) to the output.

   *   A plus-character ``+`` will place an incrementing decimal number to the
       output.  One purpose of this operator is to number lines.

   *   A ``#`` followed by a number will cause the numbered subexpression to
       be printed in hexadecimal form.

   *   Any character in the replace-string may be represented by the
       hexadecimal value of that character using the following syntax:
       ``\xhh`` where hh is the hexadecimal value.


Example:

.. code-block:: javascript

	var data="I am not unhappy and am not unwilling to participate";
	var expr=["participate", "not un"];
	var replace="try"; /* "participate"->"try", "not un"->"" */
	var dataOut=Sql.sandr(expr, replace, data);
	/* dataOut = "I am happy and am willing to try" */

Example using search/replace argument pairs :

.. code-block:: javascript

    var data="I am not unhappy and am not unwilling to participate";
    var rexargs =[ ["participate","try"], ["not un",""]];
    var dataOut=Sql.sandr(rexargs, data);
    /* dataOut = "I am happy and am willing to try" */

See `rex()`_ for rex regular expression syntax.

sandr2()
~~~~~~~~

The ``sandr2`` function operates in the same manner as ``sandr``, with the
exception that it uses `re2()`_ regular expressions.

rex()
~~~~~

The ``rex`` function uses special (non-perlre) regular expressions to search for
substrings in text.

.. code-block:: javascript

   var Sql = require("rampart-sql");
   var ret = Sql.rex(expr, data [, callback] [, options]);


+--------+-----------------------------------------------------+-------------------------------------------------------------------------------------+
|Argument|Type                                                 |Description                                                                          |
+========+=====================================================+=====================================================================================+
|expr    |:green:`String`/:green:`Array` of :green:`Strings`   | ``rex`` :ref:`Expressions <sql-expressions>` to search for                          |
+--------+-----------------------------------------------------+-------------------------------------------------------------------------------------+
|data    |:green:`String`/Buffer/:green:`Array`                | string(s)/buffers() as input text to be searched                                    |
+--------+-----------------------------------------------------+-------------------------------------------------------------------------------------+
|callback|:green:`Function`                                    | Optional callback Function                                                          |
+--------+-----------------------------------------------------+-------------------------------------------------------------------------------------+
|options |:green:`Object`                                      | ``exclude`` and ``submatches`` options                                              |
+--------+-----------------------------------------------------+-------------------------------------------------------------------------------------+

expr:
   A :green:`String` or :green:`Array` of :green:`Strings` of ``rex`` regular expressions used to match
   the text in ``data``. See :ref:`Expressions <sql-expressions>` below for full syntax.

data:
   A :green:`String`, buffer or an :green:`Array` with :green:`Strings` and/or
   :green:`Buffers` containing the text to be searched.

options:
   The ``rex`` function may take an :green:`Object` of options:

.. code-block:: javascript

   {
      "exclude":    [ "none" | "overlap" | "duplicate" ],
      "submatches": [ true | false ]
   }

The default value of ``submatches`` is ``true`` if there is a callback,
otherwise ``false``.

If the ``submatches`` option is set ``false`` and no ``callback`` is
provided, an :green:`Array` of matching :green:`Strings` is returned.

If the ``submatches`` option is set ``true`` and no ``callback`` is
provided, the return value is set to an :green:`Array` of :green:`Objects`, one per match
containing the following information:

.. code-block:: javascript

   [
      {
         match:"match1",
         expressionIndex:matchedExpressionNo,
         submatches:
            [
               "array",
               "of",
               "submatches"
            ]
      },
      {...},
      ...
   ]

*   ``match`` - the matched :green:`String`.

*   ``expressionIndex - the index in ``expr`` of the expression that
    produced ``match``, if ``expr`` is an :green:`Array`.  Otherwise ``0``.

*   ``submatches`` - :green:`Array` of submatches (one per substring matched with a
    ``+``, ``*``, ``=`` or ``{x,y}``) from search expression in the order
    specified in the search pattern.  For ``*`` or ``{0,y}``, this may be an
    empty :green:`String` ("").

See `Callback`_ below for callback() parameters where ``submatches`` is set
``true`` or ``false``.

The ``exclude`` option is used for when there are multiple expressions (as
provided by an :green:`Array` of :green:`Strings` for the ``expr`` argument) that might match
the same portion of text.

*   ``none`` returns all possible matches, even if the portion of text that
    matches is the same or overlaps with another.

*   ``overlap`` will remove the shorter (in character length) of two matches
    where one match overlaps with the other.

*   ``duplicate`` (the default mode) will remove the shorter (in character
    length) of two matches where one match is entirely encompassed in the
    other.

Example:

.. code-block:: javascript

   var search =  ['th=','>>is=','this ','his= is='];
   var txt    =  'hello, this is a message';

   var ret = Sql.rex(search, txt, {exclude:'duplicate'});
   /* ret == [ "this ", "his is" ] */

   ret = Sql.rex(search, txt, {exclude:'overlap'});
   /* ret == [ "his is" ] */

   ret = Sql.rex(search, txt, {exclude:'none'});
   /* ret == ["this ", "th", "his is", "is", "is"] */

.. _Callback:

Callback:
   The callback function will be passed the following:

.. code-block:: javascript

   var ret = Sql.rex(search, txt,
      function(match, info, index)
      {
      	console.log(index,  'matched string "' + match +'"')
      	console.log("    ", 'submatches: ', info.submatches);
      }
   );

   var ret = Sql.rex(search, txt, {submatches:false},
      function(match, index)
      {
      	console.log(index, 'matched string "' + match +'"')
      }
   );

*   ``match`` - the current :green:`String` matched.

*   ``info`` - an :green:`Object` with the following properties:

    *   ``submatches`` - :green:`Array` of submatches (one per substring
        matched with a ``+``, ``*``, ``=`` or ``{x,y}``) from the search
        expression in the order specified in the search pattern.  For
        ``*`` or ``{0,y}``, this may be an empty :green:`String` (``""``).

    *   ``expressionIndex`` - :green:`Number`, the index into the
        expression :green:`Array` (if ``expression`` was given as an
        :green:`Array`) of the expression that produced this match.

*   ``index`` - ordinal position of current match.

Return Value:
   Depending on the ``submatches`` option, an :green:`Array` of matching :green:`Strings` or
   an :green:`Array` of :green:`Objects` with matching :green:`String` and submatch information.

   If a callback function is specified, a :green:`Number`, the number of matches is returned.

.. _sql-expressions:

Expressions
'''''''''''

*   Expressions are composed of characters and operators.  Operators
    are characters with special meaning to REX.  The following
    characters have special meaning: ``\=?+*{},[]^$.-!`` and must
    be escaped with a ``\`` if they are meant to be taken literally.
    The string ">>" is also special and if it is to be matched,
    it should be written ``\>>``.  Not all of these characters are
    special all the time; if an entire string is to be escaped so it
    will be interpreted literally, only the characters ``\=?+*{[^$.!>``
    need be escaped.

*   A ``\`` followed by an ``R`` or an ``I`` means to begin respecting
    or ignoring alphabetic case distinction, until the end of the
    sub-expression.  (Ignoring case is the default, and will re-apply
    at the next sub-expression.)  These switches DO NOT apply to
    characters inside range brackets.

*   A ``\`` followed by an ``L`` indicates that the characters following
    are to be taken literally up to the next ``\L``.  The purpose of
    this operation is to remove the special meanings from characters.

*   A sub-expression following ``\F`` (followed by) or ``\P`` (preceded by)
    can be used to root the rest of an expression to which it is tied.
    It means to look for the rest of the expression "as long as followed
    by ..." or " as long as preceded by ..." the sub-expression
    following the \F or \P, but the designated sub-expression will be
    considered excluded from the located expression itself.

*   A ``\`` followed by one of the following ``C`` language character
    classes matches any character in that class: ``alpha``, ``upper``,
    ``lower``, ``digit``, ``xdigit``, ``alnum``, ``space``, ``punct``,
    ``print``, ``graph``, ``cntrl``, ``ascii``.  Note that the definition of
    these classes may be affected by the current locale.

*   A ``\`` followed by one of the following Unicode character classes
    matches any whole UTF-8 character in that class:

    .. list-table::
       :widths: 15 85
       :header-rows: 1

       * - Class
         - Matches
       * - ``\ualpha``
         - Unicode letters (includes ASCII ``a-z``, ``A-Z``)
       * - ``\udigit``
         - Unicode decimal digits (includes ASCII ``0-9``)
       * - ``\ualnum``
         - ``\ualpha`` plus ``\udigit``
       * - ``\umark``
         - Combining marks (accents, Arabic harakat, Indic matras)
       * - ``\uspace``
         - Unicode spaces (includes ASCII whitespace)
       * - ``\upunct``
         - Unicode punctuation and symbols (includes ASCII punctuation)
       * - ``\uword``
         - ``\ualpha`` plus ``\udigit`` plus ``\umark`` -- word
           characters, for tokenizing text in any script

    A repetition counts *characters*, not bytes, so ``[\uword]{2,99}``
    matches two to ninety-nine word characters of any script.  Invalid
    or truncated UTF-8 never matches a Unicode class, so matches break
    cleanly at malformed bytes in dirty data.  Zero-width joiners
    (ZWNJ/ZWJ), soft hyphens and other invisible format characters are
    deliberately in no class, so ``[\uword]+`` breaks words at them --
    the correct tokenization for scripts that use them, such as Persian
    ZWNJ.

    These classes identify word *characters*, not word *boundaries*:
    scripts written without spaces between words -- Chinese, Japanese,
    Thai, Lao, Khmer and Burmese -- produce one ``[\uword]+`` match per
    punctuation-bounded run rather than one per word.  Proper word
    segmentation for those languages requires dictionary or n-gram
    tokenization, which ``rex`` does not provide.

    A Unicode class must be a sub-expression of its own.  It may carry a
    repetition operator and may include extra explicit bytes (so
    ``[\uword\X5F]+`` also matches ``_``), but it cannot be part of a
    longer fixed set sequence, inverted with ``[^...]``, subtracted with
    ``--``, negated with ``!``, or used with the ``{x*}`` repetition
    operator.

*   ``\bound`` matches a word boundary: the zero-width position between
    a word character (a ``\uword`` character -- letter, digit or mark,
    in any script) and a non-word character.  The start and the end of
    the searched text also count as boundaries, so a token at either end
    of the text is bounded.

    ``\bound`` matches no text of its own, and is written at the start
    or at the end of a sub-expression.  A *trailing* ``\bound`` shortens
    the repetition it is attached to, one character at a time, until the
    boundary is satisfied; this is what keeps trailing punctuation out of
    a match.  For example ``\bound[\alnum.\-]+\bound`` matches
    ``4.10.3`` in ``see section 4.10.3.``: the sentence's closing period
    is given back, while the dots inside the number are kept.  The
    give-back applies only to the repetition ``\bound`` is attached to --
    REX does not otherwise backtrack.

    A ``\bound`` must be attached to a pattern that matches at least once,
    and cannot be used inside a set (``[\bound]``), in the middle of a
    sub-expression, with ``!``, or with a zero-minimum repetition such as
    ``*``, ``?`` or ``{0,9}``.

*   A ``\`` followed by one of the following special characters
    will assume the following meaning: ``n`` = newline, ``t`` = tab,
    ``v`` = vertical tab, ``b`` = backspace, ``r`` = carriage return,
    ``f`` = form feed, ``0`` = the null character.

*   A ``\`` followed by  ``Xn`` or ``Xnn`` where ``n`` is a hexadecimal digit
    will match that character.

*   A ``\`` followed by any single character (not one of the above
    special escape characters/tokens) matches that character.  Escaping
    a character that is not a special escape is not recommended, as the
    expression could change meaning if the character becomes an escape
    in a future release.

*   The character ``^`` placed anywhere in an expression (except after a
    ``[``) matches the beginning of a line (same as \x0A).

*   The character ``$`` placed anywhere in an expression
    matches the end of a line (\x0A in Unix).

*   The character ``.`` matches any character.

*   A single character not having special meaning matches that
    character.

*   A string enclosed in brackets (``[]``) is a set, and matches any
    single character from the string.  Ranges of ASCII character codes
    may be abbreviated with a dash, as in ``[a-z]`` or ``[0-9]``.
    A ``^`` occurring as the first character of the set will invert
    the meaning of the set, i.e. any character NOT in the set will
    match instead.  A literal ``-`` must be preceded by a ``\``.
    The case of alphabetic characters is always respected within brackets.

    A double-dash (``--``) may be used inside a bracketed set to subtract
    characters from the set; e.g. ``[\alpha--x]`` for all alphabetic
    characters except ``x``.  The left-hand side of a set subtraction
    must be a range, character class, or another set subtraction.
    The right-hand side of a set subtraction must be a range, character
    class, or a single character.  Set subtraction groups left-to-right.
    The range operator ``-`` has precedence over set subtraction.

*   The ``>>`` operator in the first position of a fixed expression
    will force REX to use that expression as the "root" expression
    off which the other fixed expressions are matched.  This operator
    overrides one of the optimizers in REX.  This operator can
    be quite handy if you are trying to match an expression
    with a ``!`` operator or if you are matching an item that
    is surrounded by other items.  For example: ``x+>>y+z+``
    would force REX to find the "y's' first then go backwards
    and forwards for the leading "x's" and trailing "z's".

*   The ``!`` character in the first position of an expression means
    that it is NOT to match the following fixed expression.
    For example: ``start=!finish+`` would match the word "start"
    and anything past it up to (but not including the word "finish".
    Usually operations involving the NOT operator involve knowing
    what direction the pattern is being matched in.  In these cases
    the ``>>`` operator comes in handy.  If the ``>>`` operator is used,
    it comes before the ``!``.  For example: ``>>start=!finish+finish``
    would match anything that began with "start" and ended with
    "finish".  THE NOT OPERATOR CANNOT BE USED BY ITSELF in an
    expression, or as the root expression in a compound expression.

    Note that ``!`` expressions match a character at a time, so their
    repetition operators count characters, not expression-lengths
    as with normal expressions.  E.g. ``!finish{2,4}`` matches 2 to 4
    characters, whereas ``finish{2,4}`` matches 2 to 4 times the length
    of ``finish``.

Repetition Operators
''''''''''''''''''''
*   A regular expression may be followed by a repetition operator in
    order to indicate the number of times it may be repeated.

*   An expression followed by the operator ``{X,Y}`` indicates that
    from X to Y occurrences of the expression are to be located.  This
    notation may take on several forms: "{X}" means X occurrences of
    the expression, "{X,}" means X or more occurrences of the
    expression, and "{,Y}" means from 0 (no occurrences) to Y
    occurrences of the expression.

*   The '?' operator is a synonym for the operation ``{0,1}``.
    Read as: "Zero or one occurrence."

*   The '*' operator is a synonym for the operation ``{0,}``.
    Read as: "Zero or more occurrences."

*   The '+' operator is a synonym for the operation ``{1,}``.
    Read as: "One or more occurrences."

*   The '=' operator is a synonym for the operation ``{1}``.
    Read as: "One occurrence."

Discussion
''''''''''
``rex`` is a highly optimized pattern recognition tool that has been modeled
after the Unix family of tools: GREP, EGREP, FGREP, and LEX.  Wherever
possible its syntax has been held consistent with these tools, but
there are several major departures that may bite those who are used to
using GREP or Perl Regular Expression families.

``rex`` uses a combination of techniques that allow it to surpass the speed of
anything similar to it by a very wide margin.

The technique that provides the largest advantage is called
"state-anticipation or state-skipping" which works as follows:

if we were looking for the pattern:

::

                       ABCDE

in the text:

::

                       AAAAABCDEAAAAAAA

a normal pattern matcher would do the following:

::

                       ABCDE
                        ABCDE
                         ABCDE
                          ABCDE
                           ABCDE
                       AAAAABCDEAAAAAAA

The state-anticipation scheme would do the following:

::

                       ABCDE
                           ABCDE
                       AAAAABCDEAAAAAAA

The normal algorithm moves one character at time through the text,
comparing the leading character of the pattern to the current text
character of text, and if they match, it compares the leading pattern
character +1 to the current text character +1 , and so on...

The state anticipation pattern matcher is aware of the length of the
pattern to be matched, and compares the last character of the pattern to
the corresponding text character.  If the two are not equal, it moves
over by an amount that would allow it to match the next potential hit.

If one were to count the number of comparison cycles for each pattern
matching scheme using the example above, the normal pattern matcher would
have to perform 13 compare operations before locating the first occurrence
vs. 6 compare operations for the state-anticipation pattern matcher.

One concept to grasp here is that: "The longer the pattern to be found,
the faster the state-anticipation pattern matcher will be."  While a
normal pattern matcher will slow down as the pattern gets longer.

Herein lies the first major syntax departure: ``rex`` always applies
repetition operators to the longest preceding expression.  It does
this so that it can maximize the benefits of using the state-skipping
pattern matcher.

If you were to give GREP the expression : ab*de+
It would interpret it as:

   an "a" then 0 or more "b"'s then a "d" then 1 or more "e"'s.

``rex`` will interpret this as

   0 or more occurrences of "ab" followed by 1 or more occurrences of "de".


The second technique that provides ``rex`` with a speed advantage is ability
to locate patterns both forwards and backwards indiscriminately.

Given the expression: "abc*def", the pattern matcher is looking for
"Zero to N occurrences of 'abc' followed by a 'def'".

The following text examples would be matched by this expression:

.. code-block:: none

     abcabcabcabcdef
     def
     abcdef

But consider these patterns if they were embedded within a body of text:

.. code-block:: none

     My country 'tis of abcabcabcabcdef sweet land of def, abcdef.

A normal pattern matching scheme would begin looking for 'abc*' .  Since
'abc*' is matched by every position within the text, the normal pattern
matcher would plod along checking for 'abc*' and then whether it's there
or not it would try to match "def".  ``rex`` examines the expression
in search of the most efficient fixed length sub-pattern and uses it
as the root of search rather than the first sub-expression.  So, in the
example above, ``rex`` would not begin searching for "abc*" until it has located
a "def".

There are many other techniques used in ``rex`` to improve the rate at which
it searches for patterns, but these should have no effect on the way in
which you specify an expression.

The three rules that will cause the most problems to experienced Perl
Regular Expression users are:

1.  Repetition operators are always applied to strings, rather than
    single characters.

2.  There must be at least one sub-expression that has one or more
    repetitions.

3.  No matched sub-expression will be located as part of another.

Rule 1 example:

   ``abc=def*``  means one "abc" followed by 0 or more "def"'s .

Rule 2 example:

   ``abc*def*``  *can not* be located because it matches every
   position within the text.

Rule 3 example:

   ``a+ab``  Is idiosyncratic because "a+" is a subpart of "ab".

Note that when using ``\`` escapes in JavaScript :green:`Strings`, they must be
double escaped as javascript interprets the ``\`` before it is passed on to
the ``rex`` function (e.g.  ``Sql.rex("\\n=[^\\n]+", text)``).
However the following **unsupported** syntax can also be used in limited cases:
``Sql.rex(/\n=[^\n]+/, text)``.  This may be useful for quick
scripting, but as the ``/pattern/`` is compiled by javascript, and then
again by ``rex``, this will perform unnecessary computation and can fail if
the syntax of the statement is supported by ``rex`` but not by javascript.


Example:

.. code-block:: javascript

   var html    =  '<img src="/img.gif" alt="my image">' +
                  '<img alt = "second img" src ="/img2.gif">' +
                  '<map>'+
                     '<area shape="rect" coords="34,44,270,350" ' +
                         'alt="not an img"href="/nai.html"></area>'+
                  '</map>';

   /* find alt text in img tags
      start at "alt", search forward for alt text
      and backwards for "<img"
      exclude all but the alt text.
   */
   var ret = Sql.rex('<img=!<...*>>alt=\\space*\\==\\space*"\\P=[^"]+', html );
   /* ret == [ "my image", "second img" ] */
	
Note that this example is not robust and would also match
``<img src="/img.gif"><a alt="alt">link text</a>``.  A more robust solution would be
as follows:

.. code-block:: javascript

   var html    =  '<img src="/img.gif" alt="my image">' +
                  '<img alt = "second img" src ="/img2.gif">' +
                  '<map>'+
                     '<area shape="rect" coords="34,44,270,350" ' +
                         'alt="not an img"href="/nai.html"></area>'+
                  '</map>'+
                  '<img src="/img.gif"><a alt="alt">link text</a>';

   var ret = Sql.rex(">><img =[^>]*>=", html);
   ret = Sql.rex('>>alt=\\space*\\==\\space*"\\P=[^"]+', ret);
   /* ret == [ "my image", "second img" ] */




re2()
~~~~~

The ``re2`` function operates identically to the ``rex`` function
except that it uses Perl Regular Expressions and no submatch information
is returned (empty :green:`Array`).  See `rex()`_ above.

.. code-block:: javascript

   var Sql = require("rampart-sql");
   var ret = Sql.re2(re2_expr, data [, callback] [, options]);

rexFile()
~~~~~~~~~

The ``rexFile`` function operates identically to the ``rex`` function
except that it takes a file name for the text to search.
See `rex()`_ above.

.. code-block:: javascript

   var Sql = require("rampart-sql");
   var ret = Sql.rexFile(expr, filename [, callback] [, options]);

It includes the same ``options`` as available in `rex()`_\ .

re2File()
~~~~~~~~~

The ``re2File`` function operates identically to the ``rexFile`` function
except that it uses Perl Regular Expressions and no submatch information
is returned (empty :green:`Array`). See `rexFile()`_ above.

.. code-block:: javascript

   var Sql = require("rampart-sql");
   var ret = Sql.re2File(re2_expr, filename [, callback] [, options]);


searchFile()
~~~~~~~~~~~~

The ``searchFile`` function performs a Fulltext keyword search on a file
and returns the matching portions of that file.

Usage:

.. code-block:: javascript

   var Sql = require("rampart-sql");
   var res = Sql.searchFile(query, filename [, options]);

Where:

*  ``query`` is a :green:`String` containing the terms used for the search.

*  ``filename`` is a :green:`String` specifying the file to be searched.

*  ``options`` is an :green:`Object` containing optional search settings.
   The following can be set (see :ref:`sql-set:Server Properties` for
   usage):  ``alIntersects``, ``suffixProc``, ``prefixProc``, ``defSuffRm``,
   ``rebuild``, ``withinProc``, ``intersects``, ``minWordLen``,
   ``useEquiv``, ``keepNoise``, ``eqPrefix``, ``uEqPrefix``, ``suffixLst``,
   ``prefixLst``, ``noiseLst``, ``qMaxSets``, ``qMaxSetWords``,
   ``qMaxWords``, ``qMinWordLen``, ``qMinPreLen``, ``wordc`` and ``langc``.

.. todo:  hyeqsp, see, sdexp, edexp, incsd, inced


Return Value:
   An :green:`Array` of :green:`Objects` (one :green:`Object` per match)
   where each :green:`Object` contains the properties ``match`` (a selection
   of text matching the query) and ``offset`` (the position in the file of the
   match).

Example:


.. code-block:: javascript

   var res = Sql.searchFile(
      "live",
      "gettysburg.txt",
      { minwordlen:3 }
   );

   rampart.utils.printf("%3J\n", res);

   /* expected output (offsets are byte positions in the source file and so
      depend on its exact wrapping):
   [
      {
         "offset": 359,
         "match": "We have come to dedicate a portion of that\nfield, as a final resting place for those who here gave their lives that\nthat nation might live."
      },
      {
         "offset": 668,
         "match": "The brave men, living and dead, who\nstruggled here, have consecrated it, far above our poor power to add or\ndetract."
      },
      {
         "offset": 895,
         "match": "It is for us the living,\nrather, to be dedicated here to the unfinished work which they who fought\nhere have thus far so nobly advanced."
      }
   ]
   */

searchText()
~~~~~~~~~~~~

The same as the ``searchFile`` function except the text to be searched is given in a
:green:`String` or :green:`Buffer` and returns the matching portions of that :green:`String` or
:green:`Buffer`.

Usage:

.. code-block:: javascript

   var Sql = require("rampart-sql");
   var res = Sql.searchText(query, text [, options]);
