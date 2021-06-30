Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The developers of Rampart are indebted to Thunderstone, LLC for the
Texis library and the decades of development behind it.

License
~~~~~~~

Use of the ``rampart-sql`` module and the Texis library is governed by the
Rampart Source Available License.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-sql module provides database and full text search capabilities
in Rampart.  It includes a SQL relational database with a fully integrated
full text search engine.

Documentation Caveat
~~~~~~~~~~~~~~~~~~~~

In this document, the term "Texis" used to refer to the SQL engine, the text
search index and the text search engine.  The term "Metamorph" is used to
solely refer to the text search engine.

As the Texis library and its included Metamorph search engine encompasses
more functionality than is currently used in Rampart, some settings and
procedures may not be directly applicable in the context of its use from
within a JavaScript program.  We include nearly all of it here for
completeness and as it may provide a deeper understanding of philosophy
behind the library's functionality.

Note that the convention used in this document is ``var Sql`` with a capital
"S" for the return :green:`Object` of the loaded module and ``var sql`` with a
lower-case "s" for the instance of a connection to a database made with the
JavaScript :ref:`init() constructor <initconst>` :green:`Function`.

Loading the Javascript Module
-----------------------------

Loading of the sql module from within Rampart JavaScript is a simple matter
of using the ``require`` statement:

.. code-block:: javascript

    var Sql=require("rampart-sql");

Return value:

.. code-block:: none

    {
        init:         {_func:true},
        stringFormat: {_func:true},
        abstract:     {_func:true},
        sandr:        {_func:true},
        sandr2:       {_func:true},
        rex:          {_func:true},
        re2:          {_func:true},
        rexFile:      {_func:true},
        re2File:      {_func:true},
        searchFile:   {_func:true}
    }

The returned :green:`Object` contains :green:`Functions` that can be split into two groups:
`Database Functions`_ and `String Functions`_.

Database Functions
------------------

.. _initconst:

init() constructor
~~~~~~~~~~~~~~~~~~

The ``init`` constructor function takes a :green:`String`, the path to the database
and an optional :green:`Boolean` as parameters. It returns an :green:`Object` representing a
new connection to the specified database.  The return :green:`Object` includes the
following :green:`Functions`: ``exec()``, ``one()``, ``set()``,
``importCsvFile()``, ``importCsv`` and ``close()``.

Usage:

.. code-block:: javascript

    var sql = new Sql.init(dbpath [,create]);

+--------+------------------+---------------------------------------------------+
|Argument|Type              |Description                                        |
+========+==================+===================================================+
|dbpath  |:green:`String`   | The path to the directory containing the database |
+--------+------------------+---------------------------------------------------+
|create  |:green:`Boolean`  | if true, and the directory does not exist, the    |
|        |                  | directory and a new database will be created in   |
|        |                  | the location specified.                           |
+--------+------------------+---------------------------------------------------+

Return Value:
   An :green:`Object` of :green:`Functions`:

.. code-block:: none

    {
        exec:          {_func:true},
        one:           {_func:true},
        set:           {_func:true},
        reset:         {_func:true},
        importCsvFile: {_func:true},
        importCsv:     {_func:true},
        close:         {_func:true}
    }
    
Example:

.. code-block:: javascript
    
	var Sql = require("rampart-sql");

	/* create database if it does not exist */
	var sql = new Sql.init("/path/to/my/db", true);

Note that to create a new database, the folder ``/path/to/my/db`` **must
not** exist, but ``/path/to/my`` **must** exist and have write permissions for
the current user.


exec()
~~~~~~

The exec :green:`Function` executes a sql statement on the database opened
with :ref:`init() <initconst>`.  It takes a :green:`String` containing a sql
statement and an optional :green:`Array` of sql parameters, an optional
:green:`Object` of options and an optional callback :green:`Function`.  The
parameters may be specified in any order.

.. code-block:: javascript

    var rows = sql.exec(statement [, options] [, sql_parameters] [, callback])

+--------------+------------------+--------------------------------------------------------+
|Argument      |Type              |Description                                             |
+==============+==================+========================================================+
|statement     |:green:`String`   | The sql statement                                      |
+--------------+------------------+--------------------------------------------------------+
|options       |:green:`Object`   | Options (skipRows, maxRows, returnType, includeCounts) |
|              |                  | *described below*                                      |
+--------------+------------------+--------------------------------------------------------+
|sql_parameters|:green:`Array`    | ``?`` substitution parameters                          |
+              +------------------+--------------------------------------------------------+
|              |:green:`Object`   | ``?named`` substution parameters                       |
+--------------+------------------+--------------------------------------------------------+
|callback      |:green:`Function` | a function to handle data one row at a time.           |
+--------------+------------------+--------------------------------------------------------+

Statement:
    A statement is a :green:`String` containing a single sql statement to be
    executed.  A trailing ``;`` (semicolon) is optional.  Example:

.. code-block:: javascript

    var rows = sql.exec(
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

    var rows = sql.exec(
        "select * from employees where Salary > ? and Start-date < ?",
        [50000, "2018-12-31"]
    );

    /* or */

    var rows = sql.exec(
        "select * from employees where Salary > ?salary and Start-date < ?date",
        { salary: 50000, date: "2018-12-31"}
    );

The use of Parameters can make the handling of user input safe from sql injection.
Note that if there is only one parameter, it still must be contained in an
:green:`Array`.

.. _execopts:

Options:
 The ``options`` :green:`Object` may contain any of the following:

   * ``maxRows`` (:green:`Number`):  maximum number of rows to return (default: 10
     for ``select`` statements; unlimited (``-1``) for others).  See Caveats
     below.

   * ``skipRows`` (:green:`Number`): the number of rows to skip (default: 0).

   * ``returnType`` (:green:`String`): Determines the format of the ``results`` value
     in the return :green:`Object`.

      * **default**: if ``returnType`` is not set, ``results`` in 
	the return value of ``select`` statements will be an :green:`Array`
        of :green:`Objects`, as if ``"object"`` below was set.  For
        ``delete``, ``update`` and ``insert`` statements, ``results`` will
        be an empty array as if ``"novars"`` was set.

      * ``"object"``: An :green:`Array` of :green:`Objects`.  Each
        green:`Array` member  correspond to each row fetched. Each
        :green:`Object` has its property names (keys) set the names of the
        corresponding column and its values set to the field value of the
        corresponding row for the named column.

      * ``"array"``: An :green:`Array` of :green:`Arrays`. The outer :green:`Array` 
        members correspond to each row fetched.  The inner :green:`Array`
        members correspond to the fields returned in each row.  Note that
        column names are still available, in order, in :ref:`columns <returnval>`.

      * ``"novars"``: An empty :green:`Array` is returned.  The sql statement is
        still executed.  This is the default for inserts, updates and deletes
        where the return value would otherwise not be used.  
        
      * **Note**: If the values of a deleted, inserted or updated row are needed,
        ``returnType`` can be set to either ``"object"`` or ``"array"`` and
        the statement will be executed as normal with ``results`` set as if
        the row or rows were selected.

   * ``returnRows`` (:green:`Boolean`): If set ``true``, performs the same
     function as ``{returnType: "object"}`` above.  If set ``false``,
     performs the same function at ``{returnType: "novars"}`` above.  This
     setting overrides the ``returnType`` setting if both are present.

   * ``includeCounts`` (:green:`Boolean`): whether to include count
     information in the return :green:`Object`.  Default is ``false``.  The
     information will be returned as an :green:`Object` in the
     ``sql.exec()`` return :green:`Object` as the value of the key
     ``countInfo`` (or as the fourth parameter to a callback :green:`Function`).  The
     :green:`Numbers` returned will only be useful when performing a
     :ref:`text search <sql3:Intelligent Text Search Queries>` on a field
     with a fulltext index.  If count information is not available, the
     :green:`Numbers` will be negative.  See :ref:`countInfo <countinfo>`
     below.  If ``false``, `countInfo <countinfo>` will be ``undefined``.

   * ``argument``: (aka ``arg``). A variable of any type to be passed to the
     callback below.

Caveats for Options, maxRows and skipRows:
   *  SQL ``select`` statements are by default limited to 10
     rows (``{maxRows:10}``) unless ``maxRows`` above is set.  This default
     can be changed by setting the special variable ``sql.selectMaxRows``. 

     Example:

     .. code-block:: javascript
     
        var Sql = require("rampart-sql");
           
        var sql = new Sql.init("./mytestdb");
        
        sql.selectMaxRows=20;
        
        var rows = sql.exec("select * from mytable");
        /* expected results: 20 rows, if 20 are available from "mytable" */
                 

   *  ``maxRows`` defaults to ``-1`` (unlimited) if not set and the
      SQL statement is not a ``select`` statement.

   *  ``maxRows`` and ``skipRows`` may be specified, as a shortcut, as
      parameters to the exec function.  Placement of the :green:`Numbers` in the
      ``exec()`` function is arbitrary, except that the first number given
      will be treated as ``maxRows`` and the second, if present will be
      treated as ``skipRows``.  Also note that if ``maxRows`` and/or
      ``skipRows`` is set in ``options`` above, the last set value will be
      used.
      
      Example:
      
     .. code-block:: javascript
     
        var Sql = require("rampart-sql");
           
        var sql = new Sql.init("./mytestdb");

        var sqlopts = {maxRows: 5, returnType: "array"};

        var rows = sql.exec(20, 10, "select * from mytable");
        /* expected results: 20 rows, skipping the first 10,
           if 30 are available from "mytable"                */

        var rows = sql.exec("select * from mytable", 20, 10, sqlopts);
        /* expected results: 5 rows, skipping the first 10,
           if 15 are available from "mytable".  The option maxRows
           is specified last from within "sqlopts", so it is used       */

        var rows = sql.exec("select * from mytable", sqlopts, 20, 10);
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
   
   * ``countInfo``: an :green:`Object` as described above in `countinfo`_ if the
     ``includeCounts`` option is not set ``false``.  Otherwise it will be
     ``undefined``. 

   * ``user_argument``: a variable that is supplied to the callback after
     being set in the :ref:`options <execopts>` ``arguments``
     option above.  If not set above, ``undefined`` will be passed as the
     fifth argument.

   * Note: Regardless of ``maxRows`` setting , returning ``false`` from the
     ``callback`` will cancel the retrieval of any remaining rows. 
     Returning ``undefined`` or any other value will allow the next row to be
     retrieved up to ``maxRows`` rows.

.. _returnval:

Return Value:
	:green:`Number`/:green:`Object`.

        With no callback, an :green:`Object` is returned.  The :green:`Object` contains
	three or four key/value pairs.  
	
	Key: ``results``; Value: an :green:`Array` of :green:`Objects`. 
	Each :green:`Object` corresponds to a row in the database and will
	have keys set to the corresponding column names and the values set
	to the corresponding field of the retrieved row.  If ``returnType``
	is set to ``"array"``, an :green:`Array` of :green:`Arrays`
	containing the values (one inner :green:`Array` per row) will be
	returned.
	
	Key: ``rowCount``; Value: a :green:`Number` corresponding to the number of rows
	returned.

	Key:  ``columns``; Value: an :green:`Array` corresponding to the column names or
	aliases selected and returned in results.

.. _countinfo:

  Key: ``countInfo``; Value: if option ``includeCounts`` is set
  ``true``, information regarding the number of total possible matches
  is set.  Otherwise ``countInfo`` is undefined.  When performing a 
  :ref:`text search <sql3:Intelligent Text Search Queries>` the 
  ``countInfo`` :green:`Object` contains the following:

   * ``indexCount`` (:green:`Number`): a single value estimating the number
     of matching rows.

   * ``rowsMatchedMin`` (:green:`Number`): Minimum number of rows matched **before** 
     any :ref:`group by <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>`, 
     :ref:`sql-set:likeprows`, 
     :ref:`aggregates <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>` or
     :ref:`sql-set:multivaluetomultirow` are applied.

   * ``rowsMatchedMax`` (:green:`Number`): Maximum number of rows matched **before** 
     any :ref:`group by <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>`, 
     :ref:`sql-set:likeprows`, 
     :ref:`aggregates <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>` or
     :ref:`sql-set:multivaluetomultirow` are applied.

   * ``rowsReturnedMin`` (:green:`Number`): Minimum number of rows matched **after** 
     any :ref:`group by <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>`, 
     :ref:`sql-set:likeprows`, 
     :ref:`aggregates <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>` or
     :ref:`sql-set:multivaluetomultirow` are applied.

   * ``rowsReturnedMax`` (:green:`Number`): Maximum number of rows matched **after** 
     any :ref:`group by <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>`, 
     :ref:`sql-set:likeprows`, 
     :ref:`aggregates <sql2:Summarizing Values: GROUP BY Clause and Aggregate Functions>` or
     :ref:`sql-set:multivaluetomultirow` are applied.

  If a callback :green:`Function` is specified, a :green:`Number` (the
  number of rows retrieved) is returned.  The callback is given the above
  values as arguments in the following order: ``cbfunc(result_row, index,
  columns, countInfo)``.

  Note also that if ``includeCounts`` is set ``true`` and the sql query is
  not a text search, the values of the properties of ``countInfo`` will be
  negative.

Error Messages:
   Errors may or may not throw a JavaScript exception depending on the
   error.  If the syntax is correct but the statement cannot be executed, no
   exception is thrown and ``sql.errMsg`` will contain the error message. 
   Otherwise an exception is thrown, ``sql.errMsg`` is set and the error may
   be caught with ``catch(error)``.

   Error Message Example:

.. code-block:: javascript

   var Sql = require("rampart-sql");
   
   /* create database if it does not exist */
   var sql = new Sql.init("./mytestdb",true);
            
   /* create a table */
   sql.exec("create table testtb (text varchar(16), number double)");
   
   /* create a unique index on number */
   sql.exec("create unique index testtb_number_ux on testeb(number)");

   /* insert a row */
   sql.exec("insert into testtb values ('A B C', 123)");
   
   /* attempt to insert a duplicate */
   sql.exec("insert into testtb values ('D E F', 123)");

   console.log(sql.errMsg);
   /* output = 
      "178 Trying to insert duplicate value (123) in index
      ./mytestdb/testtb_number_ux.btr"
   */

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

.. _exec_full_example:

Full Example:
  Below is a full example of ``exec()`` functionality:

.. code-block:: javascript

   function pprint(obj) {
       console.log ( JSON.stringify(obj, null, 4) );
   }

   var Sql = require("rampart-sql");

   /* create database if it does not exist */
   var sql = new Sql.init("./mytestdb",true);

   /* check if table exists */
   var rows = sql.exec(
       "select * from SYSTABLES where NAME='employees'",
       {"returnType":"novars"} /* we only need the count */
   );

   if(rows.rowCount) /* 1 if the table exists */
   {
       /* drop table from previous test run of this script */
       rows=sql.exec("drop table employees");
   }

   /* (re)create the table */
   rows=sql.exec(
           "create table employees (Classification varchar(8), " +
           "Name varchar(16), Age int, Salary int, Title varchar(16), " +
           "Start_date date, Bio varchar(128) )",
           {"returnType":"novars"}
   );

   /* populate variables for insertion */
   var cl = [
       "principal", "principal", "salary",
       "salary", "hourly", "intern"
   ];
   var name = [
       "Debbie Dreamer", "Rusty Grump","Georgia Geek",
       "Sydney Slacker", "Pat Particular", "Billie Barista"
   ];
   var age = [ 63, 58, 44, 44, 32, 22 ];
   var salary = [ 250000, 250000, 100000, 100000, 80000, 0 ];
   var title = [
       "Chief Executive Officer", "Chief Financial Officer", "Lead Programmer",
       "Programmer", "Systems Administrator", "Intern"
   ];

   /* 
     String dates are converted to local time .
     Javascript dates are UTC unless offset
     is given.
   */
   var startDate = [ 
       '1999-12-31', 
       '1999-12-31', 
       '2001-3-15', 
       new Date('2002-5-12T00:00:00.0-0800'),
       new Date('2003-7-14'), 
       new Date('2020-3-18')
   ];

   var bio = [
   "Born and raised in Manhattan, New York. U.C. Berkeley graduate. " +
       "Loves to skydive. Built Company from scratch. Still uses word-perfect.",

   "Born in Switzerland, raised in South Dakota. Columbia graduate. " +
       "Financed operation with inheritance. Has no sense of humor.",

   "Stanford graduate. Enjoys pizza and beer. Proficient in Perl, COBOL," +
       "FORTRAN and IBM System/360",

   "DeVry University graduate. Enjoys a good nap. Proficient in Python, " +
       "Perl and JavaScript",

   "Lincoln High School graduate. Self taught Linux and windows administration skills. Proficient in " +
       "Bash and GNU utilities. Capable of crashing or resurrecting machines with a single ping.",

   "Harvard graduate, full ride scholarship, top of class.  Proficient in C, C++, " +
       "Rust, Haskell, Node, Python. Into skydiving. Makes a mean latte."
   ];

   /* insert rows */
   for (var i=0; i<6; i++)
   {
       sql.exec(
           "insert into employees values(?,?,?,?,?,?,?)",
           [ cl[i], name[i], age[i], salary[i], title[i], startDate[i], bio[i] ]
       );
   }

   /* create text index */
   sql.exec("create fulltext index employees_Bio_text on employees(Bio)");

   /* perform some queries */
   rows=sql.exec("select Name, Age from employees");
   pprint(rows);
   /* expected output:
      {
          "columns": [
              "Name",
              "Age"
          ],
          "results": [
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

   rows=sql.exec(
       "select Name, Age from employees",
       {returnType:'array', maxRows:2, includeCounts:true}
   );
   pprint(rows);
   /* expected output:
      {
          "columns": [
              "Name",
              "Age"
          ],  
          "results": [
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
   rows=sql.exec(
       "select Name from employees where Bio likep 'proficient' and Salary > 50000",
	{includeCounts:true}
   );
   pprint(rows);

   /* expected output:
      {
          "columns": [
              "Name"
          ],
          "results": [
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

   rows=sql.exec(
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
   /* expected output:
      Total approximate number of matches in db: 2
      - ["Name","Salary"]
      1 ["Debbie Dreamer",250000]
      2 ["Billie Barista",0]
   */

   console.log(rows); // 2

.. remove this?
    eval()
    ~~~~~~

    The ``eval`` :green:`Function` is a shortcut for executing sql
    :ref:`sql-server-funcs:Server functions` where
    only one computed result is desired.

    With ``exec()``, this:

    .. code-block:: javascript

       var Sql = require("rampart-sql");

       var sql = new Sql.init("/path/to/my/db", true);

       var rows1 = sql.exec("select joinpath('one', 'two/', '/three/four', 'five') newpath");
       var row=rows1.results[0];
       console.log(row); /* {newpath:"one/two/three/four/five"} */

    can be more easily written as:
        
    .. code-block:: javascript

       var Sql = require("rampart-sql");
       var sql = new Sql.init("/path/to/my/db", true);
       
       var rows = sql.eval("joinpath('one', 'two/', '/three/four', 'five') newpath");
       console.log(rows); /* {newpath:"one/two/three/four/five"} */

    See :ref:`sql-server-funcs:Server functions` for a complete list of Server
    functions.

one()
~~~~~

The ``one`` :green:`Function` is a shortcut for executing sql
where only one row is desired and the extra information normally
returned from `exec()`_ is not needed.

With ``exec()``, this:

.. code-block:: javascript

   var rows = sql.exec("select email from Users where user=?user", {maxRows:1}, {user:user_name});
   var row=rows.results[0];
   /* row = { email : "user@example.com" } */

can be more easily written as:
    
.. code-block:: javascript

   var row = sql.one("select user from Users where user=?user",{user:user_name});
   /* row = { email : "user@example.com" } */

Note: ``one`` returns ``undefined`` if a matching row is not found.

set()
~~~~~

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

Note also that though ``sql.set()`` is a :green:`Function` of ``sql`` (a single opened
database), settings apply to all databases in use by the current process.

Example:

.. code-block:: javascript

        /* rank higher docs with words appearing at beginning of document *
         *  and only return matches with all the given query terms.       */
	sql.set({
		likepleadbias: 750,
		likepallmatch: true
	});

	/* an example with a return value */
	var lists = sql.set({
		addExp: [ "[\\alnum\\x80-\\xff]+","[\\alnum\\x80-\\xff,']+"],
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
	   	expressionsList:  ["\\alnum{2,99}", "[\\alnum\\x80-\xff]+", "[\\alnum\\x80-\xff,']+"]
	   }
	*/		                        	 

reset()
~~~~~~~

Reset all settings set with `set()`_ above to their original values.

Example:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   var sql = new Sql.init("/path/to/my/db");

   ...

   sql.set({...});  //settings changed in script

   ...

   sql.reset(); //reset all to default

importCsvFile()
~~~~~~~~~~~~~~~

The importCsvFile :green:`Function` is similar to the
:ref:`rampart.import.csvFile <rampart-main:csvFile>` :green:`Function` 
except that it imports csv data from a file directly
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

Note: In the callback, the loop can cancell the import at any point by returning
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
~~~~~~~~~~~

Same as `importCsvFile()`_ except instead of a file name, a :green:`String` or
:green:`Buffer` containing the csv data is passed as a parameter.

Example:

.. code-block:: javascript

   var Sql=require("rampart-sql");
   var sql= new Sql.init("/path/mytestdb");

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

   /* print the results */
   sql.exec("select * from company_assets", {returnType:'array'},function(res,i,cols) {
       if( i==0)
           console.log("-", cols);
       console.log(i, res);
   });

   /* output:
   - ["Department","Num_item1","Desc_item1","Val_item1","Tot_Val_item1","Num_item2","Desc_item2","Val_item2","Tot_Val_item2","Tot_Val_items"]
   0 ["accounting",5,"Macbook Pro",1200,6000,300,"Pencils",0.10000000149011612,30,6030]
   1 ["marketing",20,"Dell XPS 15",1150,23000,350,"Pens",0.5,175,23175]
   2 ["logistics",30,"iPad Air",300,9000,100,"Duktape",1.5,150,9150]
   */

close()
~~~~~~~

In general it is not necessary to use ``close()`` as the "connection" to the
database is not over a socket.  However, if resources to a database are no
longer needed, ``close()`` will clean up some of those resources.  Note that
even after calling ``sql.close()``, using the ``sql.*`` :green:`Functions`
will re-open handles to the database and continue to operate as expected and
in the same manner as when the "connection" was first opened.

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
""""""""""""""""
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
""""""""""""""""

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
""""""""""""""
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
see `Metamorph Hit Mark-up`_).  If the printed value is narrower, the output
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
meaning for ``%H``, ``%/`` and ``%:``, see `Extended Flags`_).  These flags
are for compatibility with the C function printf(), and are not generally
needed.

Printing Date/Time Values
""""""""""""""""""""""""" 

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
style "February 20, 1997" (assuming id is a :ref:`Texis counter field <dtypes>`):

.. code-block:: javascript

   sql.exec("select id, Title from books where Desc like ?q",
            {q:query},
            function(res) {
               console.log(
               	Sql.stringFormat("%at %s", "%B %d, %Y", res.id, res.Title) 
               );
            }
   );
   
To use a default strftime() format, eliminate the a flag and its corresponding strftime() format argument:

.. code-block:: javascript

	var curDate = Sql.stringFormat("%t", "now");

This will print today's date in a default format.


CAVEAT:
   As dates are printed using the standard C library, not all strftime() codes are available or behave identically on all platforms.


Latitude, Longitude and Location
""""""""""""""""""""""""""""""""

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
:ref:`parselatitude() <sql-server-funcs:parselatitude,parselongitude>`, 
:ref:`parselongitude() <sql-server-funcs:parselatitude,parselongitude>`, 
or :ref:`latlon2geocode() <sql-server-funcs:latlon2geocode, latlon2geocodearea>
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
      function(res,i) {
         console.log(i+1,res);
         console.log(Sql.stringFormat("  Loc: %+L", res.geocode));
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
""""""""""""""""""

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
    See `Extended Flags`_.

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
    aslo supported for %``!W``.  If a non-zero field width is given, it is
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

Extended Flags
""""""""""""""

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
    context as a sub-flag for `Metamorph Hit Mark-up`_.

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

Metamorph Hit Mark-up
"""""""""""""""""""""

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
:ref:`this section <sql3:Specifying Fewer Intersections>` for full explanation), 
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
   	function(res) {
   	   results += Sql.stringFormat('<div class="hit">%mIH</div>', query, res.abs);
   	}
   );
   results +="</div>";

For more information on ``abstract``, see `abstract()`_ below and
``abstract`` in :ref:`sql-server-funcs:Server functions`.

abstract()
~~~~~~~~~~

The abstract function generates an abstract of a given portion of text.

.. code-block:: javascript

   var options=
      {
         max: max,
         style: style,
         query: query
      }; 
   var abstract = Sql.abstract(text, options);

**or**

.. code-block:: javascript

    var abstract = Sql.abstract(text [,max [,style [,query [,markup]]]]);


+--------+------------------+---------------------------------------------------+
|Argument|Type              |Description                                        |
+========+==================+===================================================+
|text    |:green:`String`   | The text from which an abstract will be generated.|
+--------+------------------+---------------------------------------------------+
|max     |:green:`Number`   | Maximum length in characters of the abstract.     |
+--------+------------------+---------------------------------------------------+
|style   |:green:`String`   | Method used to generate the abstract.             |
+--------+------------------+---------------------------------------------------+
|query   |:green:`String`   | query or keywords used to center the abstract.    |
+--------+------------------+---------------------------------------------------+
|markup  |:green:`String` or| perform markup as `Metamorph Hit Mark-up`_ above. |
|        |:green:`Boolean`  | May be ``true`` for "%mbH" or a :green:`String`   |
|        |                  | for a custom format (such as "%mCH").             |
+--------+------------------+---------------------------------------------------+

Return Value:
   :green:`String`. The abstract text.

The abstract will be less than ``max`` characters long, and will attempt to
end at a word boundary.  If ``max`` is not specified (or is less than or
equal to 0) then a default size of 230 characters is used.

The ``style`` argument allows a choice between several different ways of
creating the abstract.  Note that some of these styles require the ``query``
argument as well, which is a Metamorph search query:

*   ``dumb`` Start the abstract at the top of the document.

*   ``smart`` This style will look for the first meaningful chunk of text,
    skipping over any headers at the top of the text.  This is the default if
    neither ``style`` nor ``query`` is given.

*   ``querysingle`` Center the abstract contiguously on the best occurence
    of ``query`` in the document.

*   ``querymultiple`` Like ``querysingle``, but also break up the abstract into
    multiple sections (separated with ``...``) if needed to help ensure all
    terms are visible.  Also it wll take care with URLs to try to show the start
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
      never forget what they did here. <span class="query">It is for us the living,
      rather, to be dedicated here to the <span class="queryset1">unfinished</span>
      <span class="queryset2">work</span> which they who fought here have thus far
      so nobly advanced. </span>It is ...
   */

sandr()
~~~~~~~

The ``sandr`` function replaces in ``data`` every occurrence of ``expr``
(`rex()`_ expression(s)) with the corresponding :green:`String`\ (s) from ``replace``.  It
returns ``dataOut``, a :green:`String` or :green:`Array` of :green:`Strings` with any replacements.

If ``replace`` has fewer values than ``expr``, it is "padded" with empty
replacement :green:`Strings` for the extra search values.

.. code-block:: javascript

   var dataOut = Sql.sandr(expr, replace, data);


+--------+---------------------------------------------------+---------------------------------------------------+
|Argument|Type                                               |Description                                        |
+========+===================================================+===================================================+
|expr    |:green:`String`/:green:`Array` of :green:`Strings` | `rex()`_ expression(s) to search for              |
+--------+---------------------------------------------------+---------------------------------------------------+
|replace |:green:`String`/:green:`Array` of :green:`Strings` | Text to replace the `rex()`_ expressions          |
+--------+---------------------------------------------------+---------------------------------------------------+
|data    |:green:`String`/:green:`Array` of :green:`Strings` | string(s) as input for search and replace         |
+--------+---------------------------------------------------+---------------------------------------------------+ 


Return Value:
   If ``data`` is an :green:`Array`, an :green:`Array` of :green:`Strings` corresponding to the ``data``
   :green:`Array` with replacements made.

   If ``data`` is a :green:`String`, a :green:`String` corresponding to the ``data`` :green:`String` with
   replacements made.

Replacement Strings:
""""""""""""""""""""

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

   var ret = Sql.rex(expr, data [, callback] [, options]);


+--------+-----------------------------------------------------+---------------------------------------------------------------+
|Argument|Type                                                 |Description                                                    |
+========+=====================================================+===============================================================+
|expr    |:green:`String`/:green:`Array` of :green:`Strings`   | ``rex`` `Expressions`_ to search for                          |
+--------+-----------------------------------------------------+---------------------------------------------------------------+
|data    |:green:`String`/Buffer/:green:`Array`                | string(s)/buffers() as input text to be searched              |
+--------+-----------------------------------------------------+---------------------------------------------------------------+
|callback|:green:`Function`                                    | Optional callback Function                                    |
+--------+-----------------------------------------------------+---------------------------------------------------------------+
|options |:green:`Object`                                      | ``exclude`` and ``submatches`` options                        |
+--------+-----------------------------------------------------+---------------------------------------------------------------+

expr:
   A :green:`String` or :green:`Array` of :green:`Strings` of ``rex`` regular expressions used to match
   the text in ``data``. See `Expressions`_ below for full syntax.

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

*   ``sumbatches`` - :green:`Array` of submatches (one per substring matched with a
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
   /* ret == [ "this", "his is" ] */

   ret = Sql.rex(search, txt, {exclude:'overlap'});
   /* ret == [ "his is" ] */

   ret = Sql.rex(search, txt, {exclude:'none'});
   /* ret == ["this ", "th", "his is", "is", "is"] */

.. _Callback:

Callback:
   The callback function will be passed the following:

.. code-block:: javascript

   var ret = Sql.rex(search, txt, 
      function(match, submatches, index)
      {
      	console.log(index,  'matched string "' + match +'"')   
      	console.log("    ", 'submatches: ', submatches);
      }
   );

   var ret = Sql.rex(search, txt, {submatches:false}, 
      function(match, index)
      {
      	console.log(index, 'matched string "' + match +'"')   
      }
   );

*   ``match`` - the current :green:`String` matched.

*   ``sumbatches`` - :green:`Array` of submatches (one per substring matched with a
    ``+``, ``*``, ``=`` or ``{x,y}``) from search expression in the order
    specified in the search pattern.  For ``*`` or ``{0,y}``, this may be an
    empty :green:`String` (``""``).

*   ``index`` - ordinal position of current match.

Return Value:
   Depending on the ``submatches`` option, an :green:`Array` of matching :green:`Strings` or
   an :green:`Array` of :green:`Objects` with matching :green:`String` and submatch information.
   
   If a callback function is specified, a :green:`Number`, the number of matches is returned.

Expressions
"""""""""""

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

Repitition Operators
""""""""""""""""""""
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
""""""""""
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
in search of the the most efficient fixed length sub-pattern and uses it
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
the ``rex`` function (.e.g.  ``Sql.rex("\\n=[^\\n]+"``, text)``). 
However the following *unsupported* syntax can also be used in most cases:
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

   var ret = Sql.re2(re2_expr, data [, callback] [, options]);

rexFile()
~~~~~~~~~

The ``rexFile`` function operates identically to the ``rex`` function
except that it takes a file name for the text to search.
See `rex()`_ above.

.. code-block:: javascript

   var ret = Sql.rexFile(expr, filename [, callback] [, options]);

In addition to the ``options`` available in `rex()`_, (``exclude`` and
``submatches``), there is also the option to specify a read buffer
``delimiter``:

*  ``delimiter`` - pattern to match at the end of the read buffer.  Default
   is ``$`` (end of line).  If your pattern crosses lines (includes a
   ``\n`` character), this may be use to specify a delimiter which will not
   be included in the pattern to be matched.  As such, this provides the
   guarantee that matching of the desired pattern will occur even if a match
   would otherwise cross the internal read buffer boundry.

re2File()
~~~~~~~~~

The ``re2File`` function operates identically to the ``rexFile`` function
except that it uses Perl Regular Expressions and no submatch information
is returned (empty :green:`Array`). See `rexFile()`_ above.

.. code-block:: javascript

   var ret = Sql.re2File(re2_expr, filename [, callback] [, options]);


searchFile()
~~~~~~~~~~~~

The ``searchFile`` function performs a keyword search on a file and returns
the matching portions of that file.  

Usage:

.. code-block:: javascript

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
            
   /* expected output:
   [
      {
         "offset": 359,
         "match": " We have come to dedicate a portion of that\nfield, as a final resting place for those who here gave their lives that\nthat nation might live. "
      },
      {
         "offset": 668,
         "match": " The brave men, living and dead, who\nstruggled here, have consecrated it, far above our poor power to add or\ndetract. "
      },
      {
         "offset": 895,
         "match": " It is for us the living,\nrather, to be dedicated here to the unfinished work which they who fought\nhere have thus far so nobly advanced. "
      }
   ]
   */

