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
following :green:`Functions`: ``exec()``, ``eval()``, ``set()``, and ``close()``.

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
        eval:          {_func:true},
        set:           {_func:true},
        importCsv:     {_func:true},
        importCsvFile: {_func:true},
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

The exec :green:`Function` executes a sql statement on the database opened with
:ref:`init() <initconst>`.  It takes a :green:`String` containing a sql statement and
an optional :green:`Array` of sql parameters, an :green:`Object` of options and/or a callback
:green:`Function`.  The parameters may be specified in any order.

.. code-block:: javascript

    var res = sql.exec(statement [, sql_parameters] [, options] [, callback])

+--------------+------------------+---------------------------------------------------+
|Argument      |Type              |Description                                        |
+==============+==================+===================================================+
|statement     |:green:`String`   | The sql statement                                 |
+--------------+------------------+---------------------------------------------------+
|sql_parameters|:green:`Array`    | ``?`` substitution parameters                     |
+--------------+------------------+---------------------------------------------------+
|options       |:green:`Object`   | Options (skip, max, returnType, includeCounts)    |
|              |                  | *described below*                                 |
+--------------+------------------+---------------------------------------------------+
|callback      |:green:`Function` | a function to handle data one row at a time.      |
+--------------+------------------+---------------------------------------------------+

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

Sql Parameters:
    Sql Parameters are specified in an :green:`Array` with each member
    correspond to each ``?`` in the sql statement.  Example:

.. code-block:: javascript

    var res = sql.exec(
        "select * from employees where Salary > ? and Start-date < ?",
        [50000, "2018-12-31"]
    );

The use of Parameters can make the handling of user input safe from sql injection.
Note that if there is only one parameter, it still must be contained in an
:green:`Array`.

.. _execopts:

Options:
 The ``options`` :green:`Object` may contain any of the following:

   * ``max`` (:green:`Number`):  maximum number of rows to return (default: 10).
   * ``skip`` (:green:`Number`): the number of rows to skip (default: 0).
   * ``returnType`` (:green:`String`): Determines the format of the ``results`` value
     in the return :green:`Object`.

      * default: an :green:`Array` of :green:`Objects` as described :ref:`below <returnval>`.

      * ``"array"``: an :green:`Array` of :green:`Arrays`. The outer :green:`Array` members correspond to
        each row fetched.  The inner :green:`Array` members correspond to
        the fields returned in each row.  Note that column names are still
        available, in order, in :ref:`columns <returnval>`.

      * ``"novars"``: an empty :green:`Array` is returned.  The sql statement is
        still executed.  This may be useful for updates and deletes
        where the return value would otherwise not be used.

   * ``includeCounts`` (:green:`Boolean`): whether to include count
     information in the return :green:`Object`.  Default is ``true``.  The
     information will be returned as an :green:`Object` in the
     ``sql.exec()`` return :green:`Object` as the value of the key
     ``countInfo`` (or as the fourth parameter to a callback :green:`Function`).  The
     :green:`Numbers` returned will only be useful when performing a
     :ref:`text search <sql3:Intelligent Text Search Queries>` on a field
     with a fulltext index.  If count information is not available, the
     :green:`Number`s will be negative.  See :ref:`countInfo <countinfo>`
     below.

Callback:
   A :green:`Function` taking as parameters (``result_row``, ``index``, ``columns``, ``countInfo``).
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

   * Note: Regardless of ``max`` setting , returning ``false`` from the
     ``callback`` will cancel the retreival of any remaining rows. 
     Returning ``undefined`` or any other value will allow the next row to be
     retrieved up to ``max`` rows.

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

  Key: ``countInfo``; Value: if option ``includeCounts`` is not set
  ``false``, information regarding the number of total possible matches
  is set.  Otherwise undefined.  When performing a :ref:`text search
  <sql3:Intelligent Text Search Queries>` the ``countInfo`` :green:`Object`
  contains the following:

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

  If a callback :green:`Function` is specified, a :green:`Number`,the number of rows fetched is
  returned.  The callback is given the above values as arguments in the
  following order: ``cbfunc(result_row, index, columns, countInfo)``.

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
   var res = sql.exec(
       "select * from SYSTABLES where NAME='employees'",
       {"returnType":"novars"} /* we only need the count */
   );

   if(res.rowCount) /* 1 if the table exists */
   {
       /* drop table from previous run */
       res=sql.exec("drop table employees");
   }

   /* (re)create the table */
   res=sql.exec(
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
   res=sql.exec("select Name, Age from employees");
   pprint(res);
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
          "countInfo": {
              "indexCount": -1,
              "rowsMatchedMin": -1,
              "rowsMatchedMax": -2,
              "rowsReturnedMin": -1,
              "rowsReturnedMax": -2
          },
          "rowCount": 6
      }
		Note that countInfo values are all negative since no
		text search was performed.
   */

   res=sql.exec(
       "select Name, Age from employees",
       {returnType:'array', max:2}
   );
   pprint(res);
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
   */
   res=sql.exec(
       "select Name from employees where Bio likep 'proficient' and Salary > 50000"
   );
   pprint(res);

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

   res=sql.exec(
       "select Name, Salary from employees where Bio likep 'skydive' order by Salary desc",
       {returnType:"array"},
       function (res, i, coln, cinfo) {
           if(!i) {
               console.log(
                  "Total approximate number of matches in db: " +
                  cinfo.indexCount
               );
               console.log("-", coln);
           }
           console.log(i+1,res);
       }
   );
   /* expected output:
      Total approximate number of matches in db: 2
      - ["Name","Salary"]
      1 ["Debbie Dreamer",250000]
      2 ["Billie Barista",0]
   */

   console.log(res); // 2


eval()
~~~~~~

The ``eval`` :green:`Function` is a shortcut for executing sql
:ref:`sql-server-funcs:Server functions` where
only one computed result is desired.

With ``exec()``, this:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   var sql = new Sql.init("/path/to/my/db", true);

   var res1 = sql.exec("select joinpath('one', 'two/', '/three/four', 'five') newpath");
   var res=res1.results[0];
   console.log(res); /* {newpath:"one/two/three/four/five"} */

can be more easily written as:
    
.. code-block:: javascript

   var Sql = require("rampart-sql");
   var sql = new Sql.init("/path/to/my/db", true);
   
   var res = sql.eval("joinpath('one', 'two/', '/three/four', 'five') newpath");
   console.log(res); /* {newpath:"one/two/three/four/five"} */

See :ref:`sql-server-funcs:Server functions` for a complete list of Server
functions.

set()
~~~~~

The ``set`` :green:`Function` sets Texis server properties.  For a full listing, see
:ref:`sql-set:Server Properties`.  Arguments are given as keys with
corresponding values set to a :green:`String`, :green:`Number`, :green:`Array` or
:green:`Boolean` as appropriate.  Note that :green:`Booleans`
``true``/``false`` are equivalent to setting ``0``/``1``, ``on``/``off``, or
``yes``/``no`` as described in :ref:`sql-set:Server Properties`.

Normally there is no return value (``undefined``).  

FIXME once names in sql-set.html are finalized:

However if :ref:`sql-set:lstexp`,
:ref:`sql-set:lstindextmp` and/or :ref:`sql-set:listNoise` is set ``true``, an :green:`Object` is
returned with corresponding keys ``expressionsList``, ``indexTempList``,
``suffixList``, ``suffixEquivsList`` and/or
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

importCsvFile()
~~~~~~~~~~~

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
    The name of the csv file to be opened;

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

      * ``allEscapes`` -  :green:`Boolean` (default ``true``): All ``\\``
        escape sequences known by the 'C' compiler are translated, if
        ``false`` only backslash, single quote, and double quote are escaped.

      * ``europeanDecimal``  -  :green:`Boolean` (default ``false``):
        Numbers like ``123 456,78`` will be parsed as ``123456.78``.

      * ``tryParsingStrings`` -  :green:`Boolean` (default ``false``): Look
        inside quoted strings for dates and numbers to parse, if false
        anything quoted is a string.

      * ``delimiter`` - :green:`String` (default ``","``):  Use the first
        character of string as a column delimiter (e.g ``\\t``).

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
   Example:  If ``[0,3,4]`` is specified, the first, fourth and fifth row in
   the csv will be inserted into the first, second and third row of SQL table.
   ``-1`` can be used to insert a ``0`` or blank string (``""``) in that
   position in the table.  Also a :green:`String` corresponding to the csv
   column name may be used in place of a number.

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
         tableName:"testtb",
         callbackStep: 1000,
         hasHeaderRow: true,
      },

      /* reorder csv rows switching second and third */
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
even after calling ``sql.close()``, the ``sql.*`` :green:`Functions` will continue to
operate as expected and in the same manner as when the "connection" was first
opened.

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

   sql.exec("select id, Title from books where Desc like ?",
            [query],
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


CAVEATS
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
argument to be searched and printed).  The m flag and its sub-flags are only
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
    match paragraph breaks.  If given twice (PP), use another additional
    argument after REX expression as replacement string, instead of "<p/>". 

*   ``c`` to continue hit count into next query call
*   ``N`` to mark up NOT terms as well
*   ``e`` to mark up the exact query (no queryfixupmode/NOT processing)
*   ``q`` to mark up the query itself, not the text, e.g. as a legend

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

TODO:  
   Remove version references.  Explain apicp. Explain use of "@0".
   Find out why the sql.set properties aren't being applied.

Each hit found by the query has each of its sets' hits (e.g.  each term)
highlighted in the output.  With I and/or C highlighting, if there are
delimiters used in the query, the entire delimited region is also
highlighted.  The Metamorph query uses the same apicp defaults and
parameters as SQL queries.  These can be changed with the apicp function
(here).

If a width is given for the format code, it indicates the character offset
in the string argument to begin the query and printing (0 is the first
character).  Thus a large text argument can be marked up in several chunks. 
Note that this differs from the normal behavior of the width, which is to
specify the overall width of the field to print in.  The precision is the
same - it gives the maximum number of characters of the input string to
print - only it starts counting from the width.

The h flag sets HREF highlighting (the default).  Each hit becomes an HREF
that links to the next hit in the output, with the last hit pointing back to
the first.  In the output, the anchors for the hits are named hitN, where N
is the hit number (starting with 1).

Hits can be bold highlighted in the output with the b flag; this surrounds
them with <b> and </b> tags.  b and h can be combined; the default if
neither is given is HREF highlighting.  In version 5.01.1212100000 20080529
and later, the B and U flags may be given, for VT100-terminal bold and
underline highlighting; this may be useful for command-line scripts.  In
version 6.00.1297382538 20110210 and later, the R flag may be given for
VT100-terminal reverse-video highlighting.

In version 6 and later, the I or C flags may be given, for inline styles or
classes.  This allows much more flexibility in defining the markup, as a
style or class for each distinct query term may then be defined.  The styles
and classes used can be controlled with <fmtcp> (here).

In version 5.01.1223065000 20081003 and later, the q flag may be given, to
highlight the query itself, instead of the following text buffer (which must
still be given but is ignored).  This can be used at the top of a
highlighted document to give a highlighting "legend" to illustrate what
terms are highlighted and how.  The n and e flags are also implicitly
enabled when q is given.  Note that settings given inline with the query
(e.g.  "@suffixproc=0") will not be highlighted (in version 6.00.1316840000
20110924 and later), since they do not themselves ever find or match any
terms - this helps avoid misleading the user that such "terms" will ever be
found in the text.  However, since they are still considered separate query
sets - because their order in the query is significant, as they only affect
following sets - a class/style is "reserved" (i.e.  not used) for them in
the querycyclenum rotation.

Normally, hits that overlap HTML tags in the search string are truncated or
moved to appear outside the tag in the output, so that the highlighting tags
do not overlap them and muddle the HTML output.  The n tag indicates that
this truncation should not be done.  (It is also not done for the %H (HTML
escapement) format code, since the tags in the string will be escaped
already.)

The ``p`` and ``P`` flags do paragraph formatting as documented previously.

The ``c`` flag indicates that the hit count should be continued for the next
query.  By default, the last hit marked up is linked back to the first hit. 
Therefore, each ``%``-code query markup is self-contained: if multiple calls are
made, the hit count (and resulting HREFs) will start over for each call,
which may not be desired.  If the c flag is given, the last hit in the
string is linked to the "next" hit (N+1) instead of the first, and the next
query will start numbering hits at N+1 instead of 1.  Thus, all but the last
query markup call by a script should use the ``c`` flag.

The e flag indicates that the query should be used exactly as given. 
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
   sql.exec(sql "select abstract(body) abs from data_tbl where body like ?",
   	[query],
   	function(res) {
   	   results += Sql.stringFormat('<div class="hit">%mIH</div>", query, res.abs);
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

    var abstract = Sql.abstract(text [,max [,style [,query]]]);


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

sandr()
~~~~~~~

The ``sandr`` function replaces in ``data`` every occurrence of ``expr``
(`rex()`_ expression(s)) with the corresponding :green:`String`(s) from ``replace``.  It
returns ``dataOut``, a :green:`String` or :green:`Array` of :green:`String`s with any replacements.

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
|expr    |:green:`String`/:green:`Array` of :green:`Strings`   | ``rex`` :ref:`expression(s) <sql1:Expressions>` to search for |
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
   A :green:`String`, buffer or an :green:`Array` with :green:`String`(s) and/or Buffers(s) containing
   the text to be searched.

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
    length of two matches where one match is entirely encompassed in the
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

   var ret = Sql.rex(search, txt, function(match, submatches, index)
      {
      	console.log(index,  'matched string "' + match +'"')   
      	console.log("    ", 'submatches: ', submatches);
      }
   );

   var ret = Sql.rex(search, txt, function(match, index)
      {submatches:false},
      {
      	console.log(index, 'matched string "' + match +'"')   
      }
   );

*   ``match`` - the current :green:`String` matched.

*   ``sumbatches`` - :green:`Array` of submatches (one per substring matched with a
    ``+``, ``*``, ``=`` or ``{x,y}``) from search expression in the order
    specified in the search pattern.  For ``*`` or ``{0,y}``, this may be an
    empty :green:`String` ("").

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



Introduction to Texis Sql
-------------------------


Texis: Thunderstone’s Text Information Server
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

What is it?
"""""""""""
Texis is a relational database server that specializes in managing
textual information. It has many of the same abilities as products like
mysql, sqlite3 and postgresql with one key difference: its primary purpose
is to intelligently search and manage databases that contain natural language
text.

Why is that different?
""""""""""""""""""""""
Most other products are optimized for sql queries for traditional sql
relational database functionality. Texis has been highly optimized to handle 
Full Text Search functions. Where Full Text Search is an afterthought for
other sql database engines which support it, it is the primary focus of Texis.

In Texis you can store text of nearly any size, and the database can query that
information in natural language in a manner similar to any web based search.
Texis utilizes the powerful Metamorph concept based text engine and has a 
specialized relational database server built around it so that both
relational models and Full Text Search are well supported.

What can it do?
"""""""""""""""
Texis is designed to efficiently handle documents and data that
contains natural language information. This includes things like:
e-mail, personnel records, research reports, memos, product
descriptions, web pages, and general documents.  Texis allows you to
import, associate, organize  and perform natural language queries against these
items in a similar manner as traditional database, with the power of a fast, 
memory efficient Full Text Search engine. It provides a single system to handle
relational data in combination with a natural language record retrieval system.

Features Unique to Texis
""""""""""""""""""""""""

Before exploring the specifications, here are some features that are unique
to Texis.

Zero Latency Insert
"""""""""""""""""""

When a record is added or updated within a Texis table it is available for
retrieval immediately.  This includes documents with fields that have a
fulltext index on them.  Optimization of fulltext documents is automatic, so
there is no need to write maintenance code.

Variable Sized Records
""""""""""""""""""""""

Like many databases, Texis allows fields with variable length text.  The
``varchar`` field is set with a suggested size, but is efficiently managed
regardless of the amount of text added.  In Texis, any variable sized field
can contain up to one gigabyte.

Indirect Fields 
""""""""""""""" 

Indirect fields are byte fields that exist as real files within the file
system.  This field type is usually used when you are creating a database
that is managing a collection of files on the server (like word processing
files for instance).  They can also be used when the one gigabyte limitation
of fields is too small.  Texis can use indirect fields that point to your
files anywhere on the file system and optionally can manage them under the
database.  Since files may contain any amount of any kind of data, indirect
fields may be used to store arbitrarily large binary objects.  These Binary
Large OBjects are often called BLOBs in other RDBMSes.  However in Texis the
``indirect`` type is distinct from ``blob``/``blobz``.  While each
``indirect`` field is a separate external file, all of a table’s
``blob``/``blobz`` fields are stored together in one ``.blb`` file adjacent
to the ``.tbl`` file.  Thus, ``indirect`` is better suited to
externally-managed files, or data in which nearly every row’s field value is
very large.  The ``blob`` (or compressed ``blobz``) type is better suited to
data that may often be either large or small, or which Texis can manage more
easily (e.g.  faster access, and automatically track changes for index
updates).  The ``indirect``/``blob``/``blobz`` type fields have the
additional benefit of storing data that is indexed, but not often retrieved,
which reduces the main table file size and improves file system caching.

Variable Length Index Keys
""""""""""""""""""""""""""

Typical English language contains words of extremely variant length.  Texis
minimizes the overhead of storing these words in an index.  Traditional
Btrees have fixed length keys, so we invented a variable length key Btree in
order to minimize our overhead while not limiting the maximum length of a
key.

Advantages of Variable Length Fields and Btrees
"""""""""""""""""""""""""""""""""""""""""""""""
Texis stands for Text Information Server, and text databases are fundamentally
different in nature to the content of most standard databases. Texis is
optimized to handle text data in the context of text retrieval. A mix of
large and small documents can be handled efficiently in the same table.
As such, Texis is optimized for two things: Query time and variable sized data.

Specifications
""""""""""""""

+---------------------------------+-------------------------------------------------------------------------------------+
| Feature                         | Texis Specs                                                                         |
+=================================+=====================================================================================+
| Multiple Servers per machine    | Yes                                                                                 |
+---------------------------------+-------------------------------------------------------------------------------------+
| Multiple Databases per server   | Yes                                                                                 |
+---------------------------------+-------------------------------------------------------------------------------------+
| Tables per database             | 10,000                                                                              |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max table size                  | On 32-bit systems - varies with filesystem, 9 exabytes (``2^63``) on 64-bit systems |
+---------------------------------+-------------------------------------------------------------------------------------+
| Rows per table                  | 1 billion                                                                           |
+---------------------------------+-------------------------------------------------------------------------------------+
| Columns per table               | Unlimited                                                                           |
+---------------------------------+-------------------------------------------------------------------------------------+
| Indexes per table               | Unlimited                                                                           |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max field size                  | 1 gigabyte                                                                          |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max field column name           | 32 characters                                                                       |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max tables per query            | 400                                                                                 |
+---------------------------------+-------------------------------------------------------------------------------------+
| User password security          | Yes (usable but unsupported in rampart)                                             |
+---------------------------------+-------------------------------------------------------------------------------------+
| Group password security         | Yes (usable but unsupported in rampart)                                             |
+---------------------------------+-------------------------------------------------------------------------------------+
| Index types                     | Btree, Inverted, Text, Text inverted                                                |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max index key size              | 8192                                                                                |
+---------------------------------+-------------------------------------------------------------------------------------+
| Standard Data Types             |  see :ref:`Datatypes <datatypes>`                                                   |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max user defined data types     | 64                                                                                  |
+---------------------------------+-------------------------------------------------------------------------------------+


Texis as a Relational Database Management System
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis is a database management system (DBMS) which follows the relational
database model, while including methods for addressing the inclusion of
large quantities of narrative full text.  Texis provides a method for
managing and manipulating an organization’s shared data, where intelligent
text retrieval is harnessed as a qualifying action for selecting the desired
information.  Texis serves as an “intelligent agent” between the database
and the people seeking data from the database, providing an environment
where it is convenient and efficient to retrieve information from and store
data in the database.  Texis provides for the definition of the database and
for data storage.  Through security, backup and recovery, and other
services, Texis protects the stored data.  At the same time Texis provides
methods for integrating advanced full text retrieval techniques and object
manipulation with the more traditional roles performed by the RDBMS
(relational database management system).

Relational Database Background
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Texis, like all sql based DBMSs, is based on the relational data model.  The
fundamental organizational structure for data in the relational model is the
relation.  A *relation* is a two-dimensional table made up of rows and
columns.  Each relation, also called a table, stores data about *entities*. 
These entities are objects or events on which an application chooses to
collect data.  Patients, company information, products, services, metadata
descriptions of media, web pages, legal documents, documentation, personal
data and/or any grouping of text based documentation are examples of
entities.The columns in a relation represent characteristics (*attributes*,
*fields*, or *data items* of an entity, such as url, text, links, date,
address, names, descriptions, abstract, etc).  The rows (called *tuples* in
relational jargon) in the relation represent specific occurrences (or
records) of a patient, doctor, time-frame, description, location, web page,
products, customer id, url, document text, etc.  Each row consists of a
sequence of values, one for each column in the table.  In addition, each row
(or record) in a table must be unique.  The *primary key* of a relation is
the attribute or attributes whose value uniquely identifies a specific row
in a relation.  For example, a Patient identification number (ID) is
normally used as a primary key for accessing a patient’s hospital records. 
A Customer ID number can be the primary key in a business.  Many different
sets of terms can be used interchangeably when discussing the relational
model.  The following table lists these terms and shows their relationship.

.. _reldbterm:

+-------------------------------+----------------------------+-------------------+
| Relational Model Literature   | Relational DBMS Products   | File Processing   |
+===============================+============================+===================+
| Relation                      | Table                      | File              |
+-------------------------------+----------------------------+-------------------+
| Tuple                         | Row                        | Record            |
+-------------------------------+----------------------------+-------------------+
| Attribute                     | Column                     | Field             |
+-------------------------------+----------------------------+-------------------+

The following figure illustrates two relations. The first one depicts
patients and the second represents outstanding patient invoices. A row
in the PATIENT relation represents a particular patient, while a row in
the INVOICE relation represents a patient invoice. Thus, a relation
provides a structure for storing data about some entity within the
organization. In fact, a database in the relational model consists of
several relations, each representing a different entity.

a. PATIENT Relation 
::

      PATIENT ID  PATIENT NAME    ADDRESS            CITY         STATE
      107         Pryor           1 Ninigret Ave     Quonsett     RI
      111         Margolis        3 Chester Ave      Westerley    RI
      112         Frazier         7 Conch Rd         New London   CT
      123         Chen            163 Namcock Rd     Attleboro    MA
      128         Steckert        14 Homestead       Norwich      CT

b. INVOICE Relation
::

      INVOICE NO      DATE             AMOUNT             PATIENT ID
      71115           11/01/92         255.00             112
      71116           11/03/92         121.25             123
      71117           11/08/92         325.00             111
      71118           11/08/92          48.50             112
      71119           11/10/92          88.00             107
      71120           11/12/92         245.40             111
      71121           11/15/92         150.00             112
      71122           11/17/92         412.00             128
      71123           11/22/92         150.00             112

An important characteristic of the relational model is that records stored
in one table can be related to records stored in other tables by matching
common data values from the different tables.  Thus data in different
relations can be tied together, or integrated.  For example, in the above
figure, invoice 71115 in the INVOICE relation is related to Patient 112,
Frazier, in the Patient relation because they both have the same patient ID. 
Invoices 71118, 71121, and 71123 are also related to Patient 112.

A database in the relational model is made up of a collection of
interrelated relations.  Each relation represents data (to the users of the
database) as a two-dimensional table.  The terms *relation* and *table* are
interchangeable.  For the remainder of the text, the term *table* will be
used when referring to a relation.  Access to data in the database is
accomplished in two ways.  The first way is by writing application programs
written in procedural languages such as C that add, modify, delete, and
retrieve data from the database.  These functions are performed by issuing
requests to the DBMS.  The second method of accessing data is accomplished
by issuing commands, or queries, in a fourth-generation language (4GL)
directly to the DBMS to find certain data.  This language is called a *query
language*, which is a nonprocedural language characterized by high-level
English-like commands such as ``UPDATE``, ``DELETE``, ``SELECT``, etc. 
Structured Query Language (SQL, also pronounced “Sequel”) is an example of a
nonprocedural query language.

Support of SQL
~~~~~~~~~~~~~~

**YUK YUK YUK -- FIXME for 2020 **

As more corporate data processing centers use SQL, more vendors are
offering relational database products based on the SQL language.
In 1986, the American National Standards Institute (ANSI) approved SQL
as the standard relational database language. SQL is now the standard
query language for relational database management systems.
Texis supports the SQL query language. Any program capable of issuing
SQL commands can interface with Texis, to accomplish the database
management, access, and retrieval functions.
For example, Microsoft ACCESS provides a means for creating a GUI
(*graphical user interface*) front end for a database. Using icons in a
point and click fashion familiar to the user, one can maneuver through
the database options where queries are created and issued to the
database. While the user does not see the form of the query, the ACCESS
program is translating them to SQL. These queries can be passed to and
implemented in a more powerful fashion by Texis, where the results are
passed back to the user via the Windows ACCESS application.
For any application written in C, an embedded SQL processor allows the C
Programmer to use Texis within his or her application.
Texis is a SQL driven relational database server that merges the
functionality of METAMORPH, our concept based text retrieval engine with
a DB2-like database. The prime differences to other systems are in the
``LIKE`` statement and in the allowable size of text fields.
This manual will explain SQL as the query language used in an enhanced
manner by Texis, so that users will be able to write queries accessing
data from a database.

Case Example: Acme Industrial Online Corporate Library
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
To provide a frame of reference to show the concepts and syntax of SQL
for use by Texis, we will use the example of Acme Industrial’s Online
Corporate Library. It is the job of the corporate librarian to make
selectively accessible to Management, Personnel, Marketing, and Research
& Development (R&D), the full text content of management, personnel,
marketing, and R&D reports, both in tabulated and full text form.
Many entities and their related functions are involved. While a
researcher in R&D requires a conceptual search and full text study of
all work that has been done similar to her own project, the Technology
Manager may be interested in hours spent by which staff, on what
projects, and to what final results in encapsulated form. The Marketing
Director will want to keep track of finished reports on subjects of
interest, while having access to promotional budget information to plan
the focus of the ad campaign over the next two quarters.
The Corporate Librarian must be able to supply concise short form and
expanded long form information on demand to those who request it, while
maintaining discretionary security. Therefore a mix of fielded and full
text information must be available and easy to manipulate and turn into
generated report content.
It may even be that each department wishes to create their own front end
application program which defines the way in which they conduct their
daily business while accessing this information. But where the
information is shared, the online library database is common to each and
must be managed as such.
All the daily activities of Acme Industrial create the need for
recording and storing vast amounts of data. These activities affect the
Online Corporate Library System in numerous ways. Data concerning
transactions and daily events must be captured in order to keep the data
in the system accurate. The system must have the capability to answer
unplanned, one-time-only queries in addition to preplanned queries.
Texis is the SQL Relational Database Server which has the horsepower to
manage this main repository of information.
This introductory chapter has introduced you to several concepts and
terms related to relational database management systems. In addition we
have provided the background case of Acme Industrial’s Online Corporate
Library System that will be used in examples throughout the text. In the
next chapter you will learn how to define and remove tables for use by
Texis.

Table Definition
----------------
[chp:TabDef]
Texis permits users to define, access, and manipulate data stored in a
database. This chapter describes how a table is defined and deleted. In
addition, you will be shown an example of how data is loaded into a
table.

Creating the Resume Table
~~~~~~~~~~~~~~~~~~~~~~~~~
One of the functions of the Librarian is to maintain a resume database
for Personnel, for potentially qualified staff for jobs as they open up.
Therefore one of the tables in the Acme Online Corporate Library System
is the RESUME table. This table is created by issuing the CREATE TABLE
command.
If you enter the following:
::

         CREATE TABLE  RESUME
           ( RES_ID  CHAR(5),
             RNAME   CHAR(15),
             JOB     CHAR(15),
             EDUC    CHAR(60),
             EXP     VARCHAR(2000)
           );

SQL statements as passed to Texis can be entered on one or more lines.
Indenting is recommended to improve readability, but it is not required.
The CREATE TABLE command is entered interactively at a terminal, or as
embedded in an application program. Note that the list of column
definitions is enclosed in parentheses and that each column definition
is separated from the next column definition by a comma. In all examples
in this text, each SQL statement is shown in uppercase letters to help
you identify what is to be entered. However, in most cases you actually
can enter the statement in either upper or lowercase.
The first line in the CREATE TABLE statement identifies the name of the
table: RESUME. The next five lines define the five columns that make up
the RESUME table. The data types chosen to define each column are
explained further on in this chapter.

#. The first column, named RES\_ID, stores the resume’s identification
   number (ID). Five characters are allowed for a Resume ID, following
   Acme internal naming conventions of a letter followed by up to 4
   other characters; e.g., ‘``R243``’ or ‘``R-376``’.

#. The second column, named RNAME, stores the name of the resume’s job
   applicant. No name longer than 15 characters can be stored in this
   column.

#. The third column, named JOB, stores the job or jobs the person is
   applying for. A maximum of 15 characters is allowed for this column.

#. The fourth column, named EDUC, stores a brief description of the
   applicant’s education. A maximum of 60 characters is allowed for this
   column. Note: One could choose to define EDUC with VARCHAR rather
   than CHAR, so that a full educational description could be entered
   without regard to waste of allocated space.

#. The fifth column, named EXP, stores the full text description of the
   applicant’s job experience as included in the resume. You have two
   choices for the text field:

   #. You can store the entire description in the Texis table. This is
      useful for short descriptive lines, for abstracts of one or more
      paragraphs, or for short reports of one to two pages as depicts
      the usual resume. Data type would be defined as a variable length
      character VARCHAR(x) where X indicates the suggested number of
      characters.

   #. You can store filenames in the Texis table. In this case Texis
      would use the filename to direct it to the text of the actual
      file. Data type would be defined as INDIRECT.

   In our EXP text column for the RESUME table we have chosen to store
   the full text in the Texis table, as concept searches of this column
   are part of almost every resume search request. If we only
   occasionally referred to the full text content, we might prefer to
   store filenames which would point to the full text only when
   necessary.

Tables defined with the CREATE TABLE command are referred to as *base
tables*. The table definition is automatically stored in a data
dictionary referred to as the *system catalog*. This catalog is made up
of various tables that store descriptive and statistical information
related to the database. The catalog can be accessed to retrieve
information about the contents and structure of the database. The system
catalog is discussed in more detail in Chapter :ref:`sql4:Administration of the Database`.
As shown in the Figure below, the CREATE TABLE command results in an
empty table.

::

      RES_ID RNAME            JOB            EDUC         EXP
      (No data is stored in the table at the time it is created.)

Inserting Data into the Resume Table
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Once the table has been created, and before any data can be retrieved,
data must be added to the table using the ``INSERT`` command. The first
row is added to the RESUME table as follows.
If you enter:
::

         INSERT INTO RESUME
         VALUES ('R323','Perkins, Alice','Snr Engineer',
                 'M.B.A. 1984 George Washington Univ',
                 'Presently employed at ...') ;

**Syntax Notes:**

-  Columns defined as CHAR (character) and VARCHAR (variable length
   character) have values enclosed in single quotes.

-  Parentheses must be placed around the set of data values.
-  Each data value is separated by a comma.
-  A long full text column such as job experience, would be loaded by a
   program function rather than manually typed in.

In the above statement, one row of data was stored in the RESUME table.
Figure [fig:InsTab] shows the RESUME table after the first record has
been added.
[fig:InsTab]
::

    RES_ID RNAME          JOB          EDUC       EXP
    R323   Perkins, Alice Snr Engineer M.B.A. ... Presently employed ...

To add the second row into the RESUME table, you enter the ``INSERT``
command again.
If you enter
::

         INSERT INTO RESUME
         VALUES ('R421','Smith, James','Jr Analyst',
                 'B.A. 1982 Radford University'
                 'Experience has been in ...') ;

Figure [fig:Ins2Tab] shows the contents of the RESUME table after two
rows have been added.
[fig:Ins2Tab]
::

    RES_ID RNAME          JOB          EDUC       EXP
    R323   Perkins, Alice Snr Engineer M.B.A. ... Presently employed ...
    R421   Smith, James   Jr Analyst   B.A. ...   Experience has been ...

Additional ``INSERT`` commands are used to enter the RESUME data, as was
illustrated in Figure [fig:Ins2Tab]. A more complete description of the
``INSERT`` command appears in Chapter [chp:DBCurr].

Defining a Table
~~~~~~~~~~~~~~~~
As illustrated in the creation of the RESUME table, tables are created
in Texis when you specify their structure and characteristics by
executing a CREATE TABLE command.
The form of this command is:
::

         CREATE TABLE [table-type] table-name
           (column-name1 data-type
            [, column-name2 data-type] ...) ;

**Syntax Notes**: A SQL statement may contain optional clauses or
keywords. These optional parts are included in the statement only if
needed. Any clause within brackets ‘``[ xxx ]``’ indicates an optional
clause.

Command Discussion
""""""""""""""""""
The CREATE TABLE command gives the name of the table, the name of each
column in the table, and the type of data placed in each column. It can
also indicate whether null values are permitted in columns.
Table Type:

    When creating a table you can optionally specify a table type. A
    standard database table will be created if no type is specified.
    Specifying a ``RAM`` table will create a table that only exists in
    memory for the current database connection. The table is not added
    to the system catalog, and is not visible to other database
    connections. It can be used as a temporary working table in an
    application. Within Vortex a ``<sqlcp cache close>`` or switching
    databases may remove the temporary table.
    A ``BTREE`` table creates a table that is inherently indexed by the
    fields in the order listed. You can not create other indexes on a
    ``BTREE`` table. This can be useful for key-lookup tables that have
    a lot of small rows.

Table Names:

    Each table in Texis is assigned a name. A table name can have up to
    18 characters (case is significant). The first character must be a
    letter, but the remaining characters can include numbers, letters,
    and the underscore (``_``) character. Table names may not be the
    same as SQL keywords or data types. For example, ``RESUME``,
    ``BUDGET93``, and ``PROD_TEST`` are all valid table names. On MSDOS
    based systems table names must be unique regardless of case in the
    first 8 characters.

Column Names:

    A column stores data on one attribute. In our example, we have
    attributes such as Resume ID, job sought, education, and experience.
    Each column within a table has a unique name and may consist of up
    to 18 characters (case is significant). The first character must be
    a letter and the remaining characters may consist of letters,
    numbers, and the underscore (``_``) character. No blank spaces are
    allowed in the column name. Table names may not be the same as SQL
    keywords or data types. Table [tab:Names] shows examples of valid
    and invalid column names.

    +----------------------+------------------------+------------------------------------+
    | Valid Column Names   | Invalid Column Names   | Reason Invalid                     |
    +======================+========================+====================================+
    | ``EMPNBR``           | ``EMP-NBR``            | Hyphen is not allowed.             |
    +----------------------+------------------------+------------------------------------+
    | ``EMP_NBR``          | ``EMP.NBR``            | Period is not allowed.             |
    +----------------------+------------------------+------------------------------------+
    | ``COST1``            | ``COST_IN_$``          | ``$`` is not allowed.              |
    +----------------------+------------------------+------------------------------------+
    | ``COST_PER_MILE``    | ``COST PER MILE``      | Spaces are not allowed.            |
    +----------------------+------------------------+------------------------------------+
    | ``SALES1991``        | ``1991SALES``          | Name cannot start with a number.   |
    +----------------------+------------------------+------------------------------------+
    | ``Where``            | ``WHERE``              | Can not be SQL keyword.            |
    +----------------------+------------------------+------------------------------------+
    | ``Date``             | ``DATE``               | Can not be SQL data type.          |
    +----------------------+------------------------+------------------------------------+

.. _datatypes:

Data Types:

    Each column within a table can store only one type of data. For
    example, a column of names represents *character* data, a column
    storing units sold represents *integer* data, and a column of file
    dates represents *time* data. In Texis, each column name defined in
    the CREATE TABLE statement has a data type declared with it. These
    data types include *character*, *byte*, *integer*, *smallint*,
    *float*, *double*, *date*, *varchar*, *counter*, *strlst*, and
    *indirect*. The table below illustrates the general format for
    each data type. A description of each of the Data Types listed in
    the following Table.

.. _dtypes:

    +----------------+---------------------+---------------------+-----------------------+
    | Type of Data   | Texis Syntax        | Example             | Data Value            |
    +================+=====================+=====================+=======================+
    | Character      | CHAR(length)        | CHAR(10)            | SMITH                 |
    +----------------+---------------------+---------------------+-----------------------+
    | Character      | CHARACTER(length)   | CHAR(25)            | 10 Newman Rd          |
    +----------------+---------------------+---------------------+-----------------------+
    | Byte           | BYTE(length)        | BYTE(2)             | DE23                  |
    +----------------+---------------------+---------------------+-----------------------+
    | Numeric        | LONG                | LONG                | 657899932             |
    +----------------+---------------------+---------------------+-----------------------+
    | Numeric        | INTEGER             | INTEGER             | 657899932             |
    +----------------+---------------------+---------------------+-----------------------+
    | Numeric        | SMALLINT            | SMALLINT            | -432                  |
    +----------------+---------------------+---------------------+-----------------------+
    | Numeric        | FLOAT               | FLOAT               | 8.413E-04             |
    +----------------+---------------------+---------------------+-----------------------+
    | Numeric        | DOUBLE              | DOUBLE              | 2.873654219543E+100   |
    +----------------+---------------------+---------------------+-----------------------+
    | Numeric        | UNSIGNED INTEGER    | UNSIGNED INTEGER    | 4000000000            |
    +----------------+---------------------+---------------------+-----------------------+
    | Numeric        | UNSIGNED SMALLINT   | UNSIGNED SMALLINT   | 60000                 |
    +----------------+---------------------+---------------------+-----------------------+
    | Date/Time      | DATE                | DATE                | 719283474             |
    +----------------+---------------------+---------------------+-----------------------+
    | Bytes          | VARBYTE(length)     | VARBYTE(16)         | DE23..                |
    +----------------+---------------------+---------------------+-----------------------+
    | Text           | VARCHAR(length)     | VARCHAR(200)        | “The subject of …”    |
    +----------------+---------------------+---------------------+-----------------------+
    | Text           | INDIRECT            | INDIRECT            | Filename              |
    +----------------+---------------------+---------------------+-----------------------+
    | Counter        | COUNTER             | COUNTER             | 2e6cb55800000019      |
    +----------------+---------------------+---------------------+-----------------------+
    | String list    | STRLST              | STRLST              | apple,orange,peach,   |
    +----------------+---------------------+---------------------+-----------------------+

    CHAR(length):
        Used to store character data, such as names, job titles,
        addresses, etc. Length represents the maximum number of
        characters that can be stored in this column. CHAR can hold the
        value of any ASCII characters 1-127. Unless you want to limit
        the size of the field absolutely you should in general use
        VARCHAR instead as it is more flexible.
    CHARACTER(length):
        Same as CHAR, used to store character data, an alternate
        supported syntax. As with CHAR, length represents the maximum
        number of characters that can be stored in this column.
    BYTE:
        Similar to CHAR but with significant differences, BYTE is used
        to store any unsigned (non-negative) ASCII values from 0-255.
        Specifying BYTE indicates each is a one byte quantity. A byte
        would be used where you want to store a small number less than
        255 such as age, or perhaps a flag. A VARBYTE can also be used
        where the length of specified characters is variable rather than
        fixed, where you are storing arbitrary binary data.
    LONG:
        Used to store large whole numbers; i.e., those without a
        fractional part, such as population, units sold, sales in
        dollars. The range of long values will depend on the platform
        you are using. For most platforms it is identical to INTEGER.
    INTEGER:
        Used to store large whole numbers where you want to ensure a
        32-bit storage unit. The largest integer value is +2147483647.
        The smallest integer value is -2147483648.
    UNSIGNED INTEGER:
        Used for similar purposes as INTEGER when you know the number
        will never be less than zero. It also extends the maximum value
        from 2,147,483,647 to 4,294,967,295. This is synonymous with
        DWORD.
    SMALLINT:
        Used to store small whole numbers that require few digits; for
        example, age, weight, temperature. The largest value is +32,767.
        The smallest value is -32,768.
    UNSIGNED SMALLINT:
        Can store positive numbers in the range from 0 to 65,535. Can be
        used in many of the same places as SMALLINT.
    INT64:
        Used to store large whole numbers when a 64-bit quantity must be
        assured (LONG size varies by platform). Value range is
        -9,223,372,036,854,775,808 through +9,223,372,036,854,775,807.
    UINT64:
        Similar to INT64, but unsigned. Value range is 0 through
        18,446,744,073,709,551,616.
    FLOAT:
        Used to store real numbers where numerical precision is
        important. Very large or very small numbers expressed in
        scientific notation (E notation).
    DOUBLE:
        Used to hold large floating point numbers. Having the
        characteristics of a FLOAT, its precision is greater and would
        be used where numerical precision is the most important
        requirement.
    DATE:
        Used to store time measured in integer seconds since 00:00:00
        Jan. 1 1970, GMT (Greenwich mean time). When entered in this
        fashion the format is an integer representing an absolute number
        of seconds; e.g., ``719283474``. The DATE data type is used to
        avoid confusions stemming from multi-sourced information
        originating from different time zone notations. This data type
        is entered by a program function rather than manually, and would
        generally be converted to calendar time before being shown to
        the user. DATEs may also be entered as strings representing a
        date/time format such as ``'1994-03-05 3:00pm'``
    VARCHAR(length):
        Used to store text field information of variable size in a Texis table.
        The specified length is offered as a suggestion only, as this data
        type can hold an unlimited number of characters. In the example
        in :ref:`Datatypes Table <dtypes>`, there may be a short description of the
        text, or a relatively small abstract which is stored in the
        field of the column itself. However the field can handle text of any
        size up to one gigabyte.
    VARBYTE(length):
        Similar to ``VARCHAR`` Used to store a byte field information of
        variable size. The specified length is offered as a suggestion only, 
        as this data type can hold an unlimited number of bytes up to one
        gigabyte.
    BLOB:
        Used to store text, graphic images, audio, and so on, where the
        object is not stored in the table itself, but is indirectly held
        in a BLOB field. BLOB stands for Binary Large Object, and can be
        used to store the content of many fields or small files at once,
        eliminating the need for opening and closing many files while
        performing a search. BLOB is used when having a specific
        filename is not desired. The BLOB is created and managed at a
        system level. The total data held for all BLOBs in a table is
        limited by the filesystem. The BLOB file is not accessed unless
        the data in it is needed. This will improve the performance of
        queries that do not need to access the data. This can also be
        useful if you are creating a ``METAMORPH INVERTED`` or ``FULLTEXT`` 
        index, and do not allow post processing, and do not display the actual
        contents of the record, as the data will not be accessed at all,
        and can be removed. This should only be done with extreme
        caution.
    BLOBZ:
        Similar to BLOB fields, except that each BLOBZ’s data is
        compressed before storing on disk, and is decompressed upon
        reading from disk. The compression/decompression is done
        internally. Alternatively, it can be handled externally via the
        executables specified by the Blob Compress EXE and Blob
        Uncompress EXE commands in the ``[Texis]`` section of the
        ``texis.ini`` configuration file. External compression allows
        custom compression types to be deployed – perhaps better than
        the ``gzip`` format supported internally by Texis – but at a
        speed penalty due to the overhead of running the executables.
    INDIRECT:
        Used to store filenames which point to data stored in some other
        location. Most frequently an INDIRECT column would point to
        files containing quantities of full text. Only one filename may
        be stored in an INDIRECT field. The filenames can be inserted
        with SQL by specifying the filename as a string, or through a
        program, which might generate the files to store the data. The
        choice of storing text or filenames only in Texis will depend on
        what you plan to do with the files, and also how big they are.
        INDIRECT can be used to point to images or other objects as well
        as text, although currently only text files can be
        meaniningfully indexed.
    COUNTER:
        This field holds an 8 byte value, which can be made unique
        across all tables in the database. To insert a counter value in
        SQL you can use the ``COUNTER`` keyword in the insert clause. A
        counter is made up of two fields, a time, and a sequence number.
        This allows the field to be compared with times, e.g., to find all
        records inserted before a particular time.
    STRLST:
        A string list is used to hold an arbitrary number of strings. The
        strings are delimited by a user defined character in the input
        string. The delimiter character is printed as the last character
        in the result string when a ``strlst`` value is converted to a
        ``varchar`` result string (this aids conversion back to
        ``strlst`` when the ``varchartostrlstsep`` setting, p. , is
        “``lastchar``”). This type is most useful when combined with an
        application which needs lists of strings, and set-like operators
        such as IN, SUBSET or INTERSECT. Other operators are generally
        undefined for strlst, though
        equality (“``=``” comparison etc.) is defined to be monolithic
        string-compare of the entire list; equality of strlst and
        varchar is the same, treating the varchar as a one-item strlst
        (if non-empty) or empty strlst (if empty).

    One large difference in Texis over other database management systems
    is in the range of data types it supports. While the traditional
    fixed length forms of CHAR, INTEGER, FLOAT and so on are supported, there
    is a corresponding variable length data type which can be used when
    appropriate, such as is represented in VARCHAR or VARBYTE.
    The length following CHAR, as in ``CHAR(100)``, indicates that 100
    is the maximum number of allowed characters. Each record with such a
    data type defined will have a size of 100 characters, regardless of
    whether 3 characters, 57 characters, or even a NULL value is
    entered. The length following VARCHAR, as in ``VARCHAR(100)``,
    indicates that 100 characters is a suggested length. If an entry of
    350 characters is required in this field, VARCHAR will make
    allowances to handle it.
    The 100 character suggestion in this case is used for memory
    allocation, rather than field length limitation. Therefore a
    VARCHAR/VARBYTE length should be entered as the average, rather 
    than the largest size for that field. Entering an extremely large 
    length to accommodate one or two unusual entries would impair the 
    handling of memory for normal operations.
    The sophisticated aspects of database design involving choice and
    use of data types towards performance and optimization of table
    manipulation are addressed in more depth in :ref:`sql4:Administration of the Database`.
    The order in which the columns are listed in the CREATE TABLE
    command is the order in which the column names will appear in the
    table.

Removing a Table
~~~~~~~~~~~~~~~~
When a table is no longer needed, it is deleted with the DROP TABLE
command. The format of this command is:
::

         DROP TABLE  table-name ;

The information about the indicated table is removed from the system
catalog tables that Texis maintains on all tables in the database. In
effect, you can no longer access, add, modify, or delete data stored in
the table. From the user’s viewpoint, the table definition and the data
stored in the table have been eliminated.
Indirect files referenced within the dropped table are not deleted
unless they are Texis managed indirects under the database. So if you
have indirects pointing to your own word processor files, they won’t be
lost when the table is dropped. For example, if the RESUME table becomes
no longer needed, you can delete this table. If you enter the following:
::

         DROP TABLE  RESUME;

This chapter has covered the creation and dropping of tables in Texis.
You were also shown how to insert data into a table. In the next
chapter, you will begin to learn how to query the database, the most
important feature of Texis in differentiating its operation from other
database management systems.

A First Look at Queries
-----------------------
[chp:Quer]
Texis uses a query language that gives users access to data stored in a
relational database. The data manipulation component of this language
enables a user to:
-  Write queries to retrieve information from the database.
-  Modify existing data in the database.
-  Add new data to the database.
-  Delete data from the database.
In this and the next two chapters, we will review the query capabilities
of Texis. In Chapter [chp:DBCurr], we will study the update, insert, and
delete features of the language.
After the tables have been created and loaded with data, you can answer
requests for information from a database without the help of
professional programmers. You write a question, also called a query,
that consists of a single statement explaining what the user wants to
accomplish. Based on this query, the computer retrieves the results and
displays them. In this chapter you will study some of the simpler ways
to form queries.
In Texis, you retrieve data from tables using the ``SELECT`` statement,
which consists of one or more ``SELECT``-``\verb``\ FROM“-\ ``WHERE``
blocks. The structure of this statement, in its simplest form, consists
of one block containing three clauses: ``SELECT``, ``FROM``, and
``WHERE``. The form of this statement follows:
::

         SELECT  column-name1 [, column-name2] ...
         FROM    table-name
         [WHERE  search-condition] ;

**Syntax Notes:**
-  The “…” above indicates additional column names can be added.
-  Brackets ‘``[ ]``’ surrounding a clause means the clause is optional.

First Look Command Discussion
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``SELECT``:
    The ``SELECT`` clause lists the column names that you want displayed
    in answer to the query.
``FROM``:
    The ``FROM`` clause indicates the table of data “FROM” which you
    want to retrieve information.
``WHERE``:
    The ``WHERE`` clause is used to screen the rows you want to
    retrieve, based on some criteria, or search condition, that you
    specify. This clause is optional, and, if omitted, all rows from the
    table are retrieved.

Retrieving From the Entire Table
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
For this example, we will use a REPORT table, into which has been loaded
reports submitted by all departments, by title, author, and reference
filename. A three character department code is used, defined in long
form in another DEPARTMENT table.
To retrieve the columns you want displayed, indicate the column names
after the keyword ``SELECT``. The order in which the column names appear
after the ``SELECT`` clause is the order in which these columns will be
displayed.
**Example:** Let’s retrieve a list of all report titles.
If you enter the statement:
::

         SELECT  TITLE
         FROM    REPORT ;

The result displayed on the screen will be:
::

      TITLE
      Innovations in Disappearing Ink
      Disappearing Ink Promotional Campaign
      Advertising Budget for 4Q 92
      Improvements in Round Widgets
      Target Market for Colored Paperclips
      Ink Color Panorama
      Departmental Meeting Schedule

The column name is automatically used as the column heading.
The first line in the ``SELECT`` statement indicates the column name
TITLE is to be displayed. The second line indicates that TITLE is found
in the REPORT table.
**Example:** If you want to display report titles, authors, and
department, you must specify that information in the ``SELECT`` clause.
If you enter the statement:
::

         SELECT  TITLE, AUTHOR, DEPT
         FROM    REPORT ;

where each column name is separated from the next by a comma, and
columns are displayed in the order you specify in the ``SELECT`` clause,
the result displayed on the screen will be:
::

      TITLE                                  AUTHOR            DEPT
      Innovations in Disappearing Ink        Jackson, Herbert  RND
      Disappearing Ink Promotional Campaign  Sanchez, Carla    MKT
      Advertising Budget for 4Q 92           Price, Stella     FIN
      Improvements in Round Widgets          Smith, Roberta    RND
      Target Market for Colored Paperclips   Aster, John A.    MKT
      Ink Color Panorama                     Jackson, Herbert  RND
      Departmental Meeting Schedule          Barrington, Kyle  MGT

Retrieving All the Columns
""""""""""""""""""""""""""
You don’t need to know the column names to select data from a table. By
placing an asterisk (\*) in the ``SELECT`` clause, all columns of the
table identified in the ``FROM`` clause will be displayed. This is an
alternative to listing all the column names in the ``SELECT`` clause.
**Example:** Let’s look at all the data stored in the REPORT table.
If you enter the statement
::

         SELECT  *
         FROM    REPORT ;

the result displayed on the screen will be
::

      TITLE                        AUTHOR           DEPT FILENAME
      ... Disappearing Ink         Jackson, Herbert RND  /docs/rnd/ink.txt
      ... Ink Promotional Campaign Sanchez, Carla   MKT  /docs/mkt/promo.rpt
      ... Budget for 4Q 92         Price, Stella    FIN  /docs/ad/4q.rpt
      ... Round Widgets            Smith, Roberta   RND  /docs/rnd/widg.txt
      ... Paperclips               Aster, John A.   MKT  /docs/mkt/clip.rpt
      ... Color Panorama           Jackson, Herbert RND  /docs/rnd/color.txt
      ... Meeting Schedule         Barrington, Kyle MGT  /docs/mgt/when.rpt

Retrieving a Subset of Rows: Simple Conditions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Often you don’t want to retrieve all the rows in a table but want only
the rows that satisfy one or more conditions. In this case, you would
include the ``WHERE`` clause in the ``SELECT`` statement to retrieve a
portion, or subset, of the rows in a table.
A *search condition* expresses the logic by which the computer
determines which rows of the table are retrieved and which are ignored.
The search condition has many variations. A simple search condition is
formed with a *conditional expression*, which specifies a comparison
between two values. It has the following format:
::

         expression    comparison operator    expression

The expressions in the conditional expression are usually a column name
or a constant. The comparison operator indicates a mathematical
comparison such as less than, greater than, equal to, etc.
The following table shows the comparison operators allowed in Texis.

.. _compop:

+----------------------------+--------------------+
| Type of Comparison         | Texis Symbol       |
+============================+====================+
| Equal to                   | ``=``              |
+----------------------------+--------------------+
| Less than                  | ``<``              |
+----------------------------+--------------------+
| Less than or equal to      | ``<=``             |
+----------------------------+--------------------+
| Greater than               | ``>``              |
+----------------------------+--------------------+
| Greater than or equal to   | ``>=``             |
+----------------------------+--------------------+
| Not equal to               | ``<>`` or ``!=``   |
+----------------------------+--------------------+

**Example:** Let’s say there is a DEPARTMENT table which has listed in
it the department code, the long form department name, the department
head, the division to which the department belongs, and the annual
department budget. The conditional expression to find departments with a
budget above $25,000 can be written:
::

         BUDGET > 25000

In this case BUDGET is being compared to a numeric constant.
The conditional expression to find all departments in the Product
Division is written:
::

         DIV = 'PROD'

Character constants, sometimes called character strings, are enclosed in
single quotes. The conditional expression can compare numeric values to
one another or string values to one another as just shown.
Each row in the indicated table is evaluated, or tested, separately
based on the condition in the ``WHERE`` clause. For each row, the
evaluation of the conditional expression is either true or false. When a
condition is true, a row is retrieved; when the condition is false, the
row is not retrieved. For example, if a department has a $35,000 budget,
then the conditional expression “``BUDGET > 25000``” is true and the row
is included in the query result. However, if the department had a budget
of $15,000, then the result of the conditional expression
“``BUDGET > 25000``” is false and the row is not retrieved.
**Example:** Let’s develop a list of all departments, in long form, in
the Product Division.
Enter the statement:
::

         SELECT  DNAME
         FROM    DEPARTMENT
         WHERE   DIV = 'PROD' ;

``'PROD'`` is the search condition, and as a character string must be
enclosed in quotes.
The result displayed will be:
::

      DNAME
      Research and Development
      Manufacturing
      Customer Support and Service
      Product Marketing and Sales

In the ``WHERE`` clause, the condition “DIV must equal PROD” results in
the retrieval of the name of each department in the Product Division. As
only DNAME, the long form departmental name, was requested in the
``SELECT`` statement, a list of department names is all that is shown.
**Example:** Let’s develop a list of all departments with a budget above
$25,000.
Enter the statement:
::

         SELECT  DNAME, BUDGET
         FROM    DEPARTMENT
         WHERE   BUDGET > 25000 ;

Note that numeric values, as ``25000``, are not enclosed in quotes.
The result displayed will be:
::

      DNAME                                BUDGET
      Finance and Accounting               26000
      Corporate Legal Support              28000
      Research and Development             27500
      Manufacturing                        32000
      Strategic Planning and Intelligence  28500

Retrieving a Subset of Rows: Compound Conditions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The conditions illustrated in the previous section are called simple
conditions because each involves a single comparison. It is also
possible to develop more complex conditions involving two or more
conditional expressions. You combine conditions using the logical
operators AND, OR, or NOT to connect conditional expressions. When two
or more conditions are combined by logical operators, the conditional
expression is called a *compound condition*. For example, you may want a
list of departments from the Product Division only with budgets under
$20,000.
The form of the compound condition is:
::

         conditional   logical    conditional   logical    conditional
         expression1   operator   expression2   operator   expression3

As with simple conditional expressions, the evaluation of a compound
condition is either true or false, with true resulting in retrieval of a
row and false resulting in no retrieval.

Retrieval Using the AND Operator
""""""""""""""""""""""""""""""""
When AND is used to connect two conditions, each conditional expression
must be true for the condition to be true and the row retrieved. If any
condition within a compound condition is false, the compound condition
is false and the row is not selected.
For example, if you want to retrieve the records of Corporate Division
Departments with a budget under $10,000 you can write the following
compound condition:
::

         DIV = 'CORP'  AND  BUDGET < 12000

In this example, ``AND`` is the logical operator.
Table [tab:LogAnd] illustrates the four possible cases that can occur
with the logical operator AND for the compound condition just described.

+-----+--------------+--------------+------------------+--------------------+----------+-----------------+
|     | Values for   | Values for   | Condition1       | Condition2         |          |                 |
+-----+--------------+--------------+------------------+--------------------+----------+-----------------+
|     | ``DIV``      | ``BUDGET``   | ``DIV='CORP'``   | ``BUDGET<12000``   | Yields   | Row Result      |
+-----+--------------+--------------+------------------+--------------------+----------+-----------------+
| 1   | ``CORP``     | ``10500``    | True             | True               | True     | Retrieved       |
+-----+--------------+--------------+------------------+--------------------+----------+-----------------+
| 2   | ``CORP``     | ``28000``    | True             | False              | False    | Not retrieved   |
+-----+--------------+--------------+------------------+--------------------+----------+-----------------+
| 3   | ``PROD``     | ``11000``    | False            | True               | False    | Not retrieved   |
+-----+--------------+--------------+------------------+--------------------+----------+-----------------+
| 4   | ``PROD``     | ``27500``    | False            | False              | False    | Not retrieved   |
+-----+--------------+--------------+------------------+--------------------+----------+-----------------+

**Example:** Based on the above, let’s develop a list of departments in
the Corporate Division with a budget under $12,000.
If you enter the statement:
::

         SELECT  DNAME, DIV, BUDGET
         FROM    DEPARTMENT
         WHERE   DIV = 'CORP' AND BUDGET < 12000 ;

the result displayed will be:
::

      DNAME                         DIV     BUDGET
      Supplies and Procurement      CORP    10500

Retrieval Using the OR Operator
"""""""""""""""""""""""""""""""
When OR is used to connect two or more conditions, the compound
condition is true if any condition is true, and the row is then
retireved. However, if all of the conditional expressions are false,
then the row is not selected.
For example, suppose management is interested in any Product Division
department OR any department with a budget of $28,000 or greater. This
compound condition can be written as follows:
::

         DIV = 'PROD'  OR  BUDGET >= 28000

In this case OR is the logical operator used.
Table [tab:LogOr] illustrates the four possible cases that can occur
with the logical operator OR for the example just given.

+-----+--------------+--------------+------------------+---------------------+----------+-----------------+
|     | Values for   | Values for   | Condition1       | Condition2          |          |                 |
+-----+--------------+--------------+------------------+---------------------+----------+-----------------+
|     | ``DIV``      | ``BUDGET``   | ``DIV='PROD'``   | ``BUDGET>=28000``   | Yields   | Row Result      |
+-----+--------------+--------------+------------------+---------------------+----------+-----------------+
| 1   | ``PROD``     | ``32000``    | True             | True                | True     | Retrieved       |
+-----+--------------+--------------+------------------+---------------------+----------+-----------------+
| 2   | ``PROD``     | ``27500``    | True             | False               | True     | Retrieved       |
+-----+--------------+--------------+------------------+---------------------+----------+-----------------+
| 3   | ``CORP``     | ``28000``    | False            | True                | True     | Retrieved       |
+-----+--------------+--------------+------------------+---------------------+----------+-----------------+
| 4   | ``CORP``     | ``10500``    | False            | False               | False    | Not retrieved   |
+-----+--------------+--------------+------------------+---------------------+----------+-----------------+

**Example:** Based on the above, let’s develop a list of departments for
management review, which are either in the Product Division or which
have budgets of $28,000 or greater.
If you enter the statement:
::

         SELECT  DNAME, DIV, BUDGET
         FROM    DEPARTMENT
         WHERE   DIV = 'PROD' OR BUDGET >= 28000 ;

the result displayed will be:
::

      DNAME                                DIV     BUDGET
      Corporate Legal Support              CORP    28000
      Research and Development             PROD    27500
      Manufacturing                        PROD    32000
      Customer Support and Service         PROD    11000
      Product Marketing and Sales          PROD    25000
      Strategic Planning and Intelligence  INFO    28500

Retrieval Using Both AND and OR Operators
"""""""""""""""""""""""""""""""""""""""""
Compound conditions can include both AND and OR logical operators.
**Example:** If you enter the query:
::

         SELECT  DNAME, DIV, BUDGET
         FROM    DEPARTMENT
         WHERE   DIV = 'CORP'  AND  BUDGET < 12000  OR  DIV = 'PROD' ;

the result displayed will be:
::

      DNAME                         DIV     BUDGET
      Supplies and Procurement      CORP    10500
      Research and Development      PROD    27500
      Manufacturing                 PROD    32000
      Customer Support and Service  PROD    11000
      Product Marketing and Sales   PROD    25000

When you have a combination of AND and OR operators, the AND operators
are evaluated first; then the OR operators are evaluated. Therefore, in
the above query, rows from the DEPARTMENT table are retrieved if they
satisfy at least one of the folloiwng conditions:

#. The department is in the Corporate Division with a budget under $12,000.
#. The department is in the Product Division.

Retrieval Using Parentheses
"""""""""""""""""""""""""""
Parentheses may be used within a compound condition to clarify or change
the order in which the condition is evaluated. A condition within
parentheses is evaluted before conditions outside the parentheses.
**Example:** Retrieve the department name, division name, and budget of
all departments who have a budget of less than $12,000, and who are
either in the Corporate or the Product Division.
If you enter the query:
::

         SELECT  DNAME, DIV, BUDGET
         FROM    DEPARTMENT
         WHERE   BUDGET < 12000
           AND   (DIV = 'CORP' OR DIV = 'PROD') ;

the result displayed will be:
::

      DNAME                         DIV     BUDGET
      Supplies and Procurement      CORP    10500
      Customer Support and Service  PROD    11000

This query retrieves rows from the DEPARTMENT table that satisfy both of
the following conditions:
#. The department has a budget of under $12,000.
#. The department is in either the Corporate Division or the Product Division.

Logical Operator NOT
""""""""""""""""""""
The logical operator NOT allows the user to express conditions that are
best expressed in a negative way. In essence, it reverses the logical
value of a condition on which it operates. That is, it accepts all rows
except those that satisfy the condition. You write the conditional
expression with the keyword NOT preceding the condition:
::

         WHERE  NOT  condition

The condition can be a simple condition or a condition containing ANDs
and ORs. The compound condition using NOT is true if the condition
following NOT is false; and the compound condition is false if the
condition following NOT is true.
For example, suppose you are looking for all departments who are not in
the Corporate Division. You can write the conditional expression:
::

         NOT (DIV = 'CORP')

Parentheses are optional but are included to improve readability of the condition.
If a department is in the Product Division, the program evaluates the
condition in the following manner:

+----------------------------------------------------------------------+------------------------------------+
| Evaluation Process                                                   | Comments                           |
+======================================================================+====================================+
| Step 1: ``NOT (DIV = 'CORP')``                                       | Original condition.                |
+----------------------------------------------------------------------+------------------------------------+
| Step 2: ``NOT ('PROD' = 'CORP')``                                    | Substitute ``PROD`` for ``DIV``.   |
+----------------------------------------------------------------------+------------------------------------+
| Step 3: ``NOT`` (false)‘ & Since ``PROD`` does not equal ``CORP``,   |                                    |
| & the condition ``DIV = 'CORP'`` is false.                           |                                    |
| Step 4: true & NOT changes false to true,                            |                                    |
| & the row is retrieved.                                              |                                    |
+----------------------------------------------------------------------+------------------------------------+

NOT is typically used with logical operators such as IN, BETWEEN,
``LIKE``, etc., which will be covered in a later section.
In the query condition ``NOT (DIV = 'CORP')``, you are more likely to
write the condition as follows:
::

         WHERE DIV != 'CORP'

In this query the ‘``!=``’ operator is used to show that ``DIV`` must
not be equal to ``CORP``.
**Example:** The NOT operator can be used with more than one expression.
List all departments except those in the Corporate Division or those in
the Product Divison.
Enter the statement:
::

         SELECT  DNAME, DIV
         FROM    DEPARTMENT
         WHERE   NOT (DIV = 'CORP' OR DIV = 'PROD') ;

Note that ``NOT`` precedes the entire condition.
The result displayed will be:
::

      DNAME                                  DIV
      Information Systems Management         INFO
      Corporate Library                      INFO
      Strategic Planning and Intelligence    INFO

This statement retrieves the department and division name for all
departments which are not Corporate or Product, revealing a division not
yet retrieved in the previous searches, the Information Division.

Additional Comparison Operators
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Texis has several special comparison operators for use with search
conditions. These operators are indicated by the keywords BETWEEN, IN,
SUBSET, INTERSECT, ``LIKE``, ``LIKER``, ``LIKEP`` and ``LIKE3``,
``LIKEIN``.

Range and Geographical Searches Using BETWEEN
"""""""""""""""""""""""""""""""""""""""""""""
The BETWEEN operator allows you to select rows of data in a given column
if data in a given column contain values within a range. The general
form of this operator is:
::

         expression  [NOT]  BETWEEN  lower value  AND  upper value

The condition is true if the expression is greater than or equal to the
lower value and less than or equal to the upper value. If the NOT
operator is used, the row is retrieved if the expression is less than
the lower value or greater than the upper value.
**Example:** Let’s find all departments whose budgets are between
$15,000 and $25,000.
If you enter the statement:
::

         SELECT  DNAME, BUDGET
         FROM    DEPARTMENT
         WHERE   BUDGET  BETWEEN  15000  AND  25000 ;

the result displayed will be:
::

      DNAME                              BUDGET
      Product Marketing and Sales        25000
      Corporate Library                  18500
      Information Systems Management     22500

The name of each department whose budget is between $15,000 and $25,000
is retrieved. The limits include any budget of $15,000 and of $25,000;
thus the Product Marketing and Sales Department with a budget matching
the upper limit has been included.
The AND logical operator can also be used to form a query that selects
values from a range. A query similar to the last example would look like
the following.
If you enter the following statement:
::

         SELECT  DNAME, BUDGET
         FROM    DEPARTMENT
         WHERE   BUDGET >= 15000  AND  BUDGET <= 25000 ;

the result displayed will still be:
::

      DNAME                              BUDGET
      Product Marketing and Sales        25000
      Corporate Library                  18500
      Information Systems Management     22500

Notice that the results are identical to the output in example where
BETWEEN was used in the ``WHERE`` clause.
The BETWEEN operator can be modified with the logical operator NOT so
that rows outside a range will be selected.
**Example:** List the names of all departments who do not have a budget
in the range of $15,000 to $25,000.
If you enter the statement:
::

         SELECT  DNAME, BUDGET
         FROM    DEPARTMENT
         WHERE   BUDGET  NOT  BETWEEN  15000  AND  25000 ;

the result displayed will be:
::

      DNAME                                BUDGET
      Corporate Legal Support              28000
      Supplies and Procurement             10500
      Customer Support and Service         11000
      Manufacturing                        32000
      Research and Development             27500
      Strategic Planning and Intelligence  28500

This statement retrieves the names of all departments with budgets lower
than $15,000 or higher than $25,000.

Geographical Searches with BETWEEN
""""""""""""""""""""""""""""""""""
A second form of ``BETWEEN`` is used for doing geographical searches. In
this form the operator is used as:
::

        location [NOT] BETWEEN (corner1, corner2)

(The parentheses are significant, and distinguish the special
two-dimensional geographical form of ``BETWEEN`` from the normal
one-dimensional range search.) The ``location``, ``corner1`` and
``corner2`` values all represent single geographical
(latitude/longitude) points – “geocode” values. This form of the
``BETWEEN`` operator will be true for all ``location`` points that are
within (or on) the rectangular box defined by diagonally-opposite
corners ``corner1`` and ``corner2``.
The left-side ``location`` must be a ``long`` value. It is a
geographically-encoded (“geocode”) value, returned from the SQL function
``latlon2geocode()`` or the Vortex function ``<geo2code>``. Typically
``location`` is a ``long`` geocode column in a table representing the
physical location of a row’s data.
The right-side ``corner1`` and ``corner2`` points define
diagonally-opposite corners of the bounding box. They are typically
also ``long`` geocode values. However,
they may each be a single ``varchar`` (text) value
containing a space- or comma-separated latitude/longitude pair, which
will automatically be converted to geocode format. E.g.:
::

       location BETWEEN ('40N 80W', '41N 81W')

The bounding box may be
computed inline from coordinates with ``latlon2geocodebox()``; e.g. for
a 0.5-degree “radius” bounding box centered on 40.5N, 80.5W:
::

      location BETWEEN (select latlon2geocodebox(40.5, -80.5, 0.5))

When used in conjunction with a regular index on the ``expression``
column, the ``BETWEEN`` operator can greatly speed up geographical
searches, as it reduces a two-dimensional AND search (with its
potentially large merge or post-process) into a single-dimensional,
all-index operation.

Set-like Searches Using IN, SUBSET and INTERSECT
""""""""""""""""""""""""""""""""""""""""""""""""

The IN, SUBSET and INTERSECT operators can be used for set-like searches
on multi-value type fields such as ``strlst``. For example, to find rows
where a query term is present in a strlst column, use IN. To find rows
where a ``strlst`` column contains *any* of a list of query terms, use
INTERSECT to find the set intersection of the row and the query set. To
find rows where *all* query terms must be present in the row, use
SUBSET.

Searches Using IN
"""""""""""""""""
The IN operator is used to select rows that match one of several listed
values. It behaves similar to the :ref:`SUBSET <searches-using-subset>`
operator, i.e. it is true if all left-side value(s) are also
present on the right-side.
The format of this operator is:
::

         expression [NOT] IN (value1, value2, value3 ...)

Value1, value2, and so on indicates a list of values. Enclose the entire
list in parentheses. Separate items in the list by commas.
**Example:** Let’s list all departments in either the Corporate,
Product, or Information divisions.
Enter the statement:
::

         SELECT  DNAME, DIV
         FROM    DEPARTMENT
         WHERE   DIV IN ('CORP', 'PROD', 'INFO') ;

The row is retrieved if a department’s division is in the set of
divisions.
The result displayed will be:
::

      DNAME                                DIV
      Management and Administration        CORP
      Finance and Accounting               CORP
      Corporate Legal Support              CORP
      Supplies and Procurement             CORP
      Recruitment and Personnel            CORP
      Research and Development             PROD
      Manufacturing                        PROD
      Customer Support and Service         PROD
      Product Marketing and Sales          PROD
      Information Systems Management       INFO
      Corporate Library                    INFO
      Strategic Planning and Intelligence  INFO

A semantically equivalent (but usually less efficient) query can be
formed using the logical operator OR. It looks like the following:
::

         SELECT  DNAME, DIV
         FROM    DEPARTMENT
         WHERE   DIV = 'CORP'  OR  DIV = 'PROD'  OR  DIV = 'INFO' ;

The right-side of the IN operator may also be a ``strlst`` table column,
in which case for each row, the left-side value is compared against each
individual ``strlst`` item for that row. Parentheses are not needed in
this case:
::

         SELECT UserName
         FROM   Users
         WHERE  'Administrator' IN GroupMembership;

In the above example, the GroupMembership column is of type ``strlst``,
and contains the list of groups that each user (row) is a member of. The
query will thus return all UserNames that are members of the
“``Administrator``” group.
The left-side of an IN operator may also be multi-value (e.g. a
``strlst`` parameter), in which case *all* the left-side values must be
present on the right-side (if ``inmode`` is “``subset``”). The behavior
of multi-value types other than ``strlst`` (on either side of IN) is
currently undefined and thus such types should not be used.
The IN operator can be modified with the logical operator NOT (note
however that an index cannot be used to optimize such a query).
**Example:** List all departments which are not in either the Corporate
or the Information divisions.
Enter the statement:
::

         SELECT  DNAME, DIV
         FROM    DEPARTMENT
         WHERE   DIV NOT IN ('CORP','INFO') ;

The result displayed will be:
::

      DNAME                                DIV
      Research and Development             PROD
      Manufacturing                        PROD
      Customer Support and Service         PROD
      Product Marketing and Sales          PROD

Note that IN differs from SUBSET and INTERSECT in the interpretation of
empty varchar values: for IN they are single-item empty-string sets. See
p.  for details, as well as for other behaviors that IN, SUBSET and
INTERSECT share in common.

See also: :ref:`sql-set:inmode`

.. _searches-using-subset:

Searches Using SUBSET
"""""""""""""""""""""
The SUBSET operator allows subset queries, and is typically used with
multi-value (i.e. ``strlst``) fields that are treated as sets. It is
true if the left-side is a subset of the right-side, i.e. if there are
no values on the left-side that are missing from the right-side.
Duplicates count, i.e. they must match one-to-one from left side to
right.
For example, suppose the table ``Users`` contains one row per user
(``UserName``), and has a ``strlst`` column ``GroupMembership`` that
lists all the groups that row’s user is a member of. To find all users
that are members of groups “``Management``”, “``Sales``” *and*
“``Marketing``”, a SUBSET query can be used:
::

         SELECT UserName
         FROM   Users
         WHERE  ('Management', 'Sales', 'Marketing')
              IS SUBSET OF GroupMembership;

(Syntactically, SUBSET is always used as part of the phrase IS SUBSET
OF, as it is only valid in WHERE clauses.) The above query will return
the users that are members of all three groups – including any users
that may also be members of additional groups.
Note that SUBSET is not commutative, i.e. if the left- and right-sides
are reversed, the meaning is changed (unlike e.g. INTERSECT). If ``A``
is a subset of ``B``, then ``B`` is *not* necessarily a subset of ``A``;
``B`` is a subset of ``A`` if and only if both sets contain the same
values. E.g. this query:
::

         SELECT UserName
         FROM   Users
         WHERE  GroupMembership
              IS SUBSET OF ('Management', 'Sales', 'Marketing');

while merely the reversed version of the earlier query, behaves
differently: it would list the users whose are in zero or more of the
Management, Sales or Marketing groups – *and* are not in any other
groups.
In set logic the empty set is a subset of any set; thus if there are
*no* values on the left-side, SUBSET is true no matter what the
right-side value(s) are. Note that SUBSET interprets an empty varchar
value as empty-set, not single-item empty-string set set (as IN does).
See p.  for details, as well as additional behaviors that IN, SUBSET and
INTERSECT share in common.

Index Usage by SUBSET
'''''''''''''''''''''

A SUBSET query can often utilize a regular (B-tree) index to increase
performance. Generally the index should be created with ``indexvalues``
set to ``splitstrlst`` (the default), as this enables individual values
of ``strlst``\ s to be accessed as needed. There are some limitations
and caveats for SUBSET and indexes however:

-   **Empty parameter, ``strlst`` column (either side):**
     
     Queries with empty-set parameters (i.e. zero-item ``strlst``, or
     empty ``varchar``) and a ``strlst`` column cannot use an
     ``indexvalues=splitstrlst`` index, regardless of which side of
     SUBSET the parameter and column are on. An index with
     ``indexvalues=all`` can be used however. It may be created in
     addition to the normal ``indexvalues=splitstrlst`` index, and the
     Texis optimizer will choose the appropriate one at search time.

-   **Empty ``strlst`` column left-side, non-empty parameter right-side:**
     
     With a ``strlst`` column on the left-side, and a non-empty
     parameter on the right, empty rows will not be returned if an index
     is used – even though they properly match (as empty set is a subset
     of any set).

These caveats are due to limitations in ``indexvalues=strlst`` indexes;
see p.  for more information.

Searches Using INTERSECT
""""""""""""""""""""""""
The INTERSECT operator allows set-intersection queries, typically on
multi-value (i.e. ``strlst``) values. It returns the intersection of the
left and right sides, i.e. the “set” (``strlst``) of all values that are
present on both sides. Duplicates are significant, i.e. they must match
one-to-one to be included in the intersection.
For example, suppose the table ``Users`` contains one row per user
(``UserName``), and has a ``strlst`` column ``GroupMembership`` that
lists all the groups that row’s user is a member of. To find all users
that are members of groups “``Management``”, “``Sales``” *or*
“``Marketing``”, an INTERSECT query can be used:
::

         SELECT UserName
         FROM   Users
         WHERE  GroupMembership INTERSECT
              ('Management', 'Sales', 'Marketing') IS NOT EMPTY;

This will return users where the intersection of a user’s
GroupMembership with the three named groups is not empty (i.e. contains
at least one value). Thus, users that are members of any of the three
named groups are returned. The phrase IS NOT EMPTY must be added
immediately after, both to turn the expression into a true/false
condition suitable for a WHERE clause, and to allow an index to be used
to resolve the query. (The phrase IS EMPTY is also permitted, for
negation. However indexes cannot be used to resolve such queries.)
INTERSECT may also be used in a SELECT clause, to return the actual
intersection set itself, rather than be used as a true/false condition.
For example, given the same Users table above, to find each user’s
membership amongst just the three named groups, this query may be used:
::

         SELECT UserName, GroupMembership INTERSECT
              ('Management', 'Sales', 'Marketing') AS SubMembership
         FROM   Users;

This will return the membership of each user (SubMembership) in just the
three named groups, as a ``strlst``. If a user is not a member of any of
the three groups, SubMembership will be empty. If a user is a member of
some other group(s), they will not be named in SubMembership.

Note that unlike SUBSET, INTERSECT is commutative, i.e. reversing the
left- and right-sides does not change its meaning. (The “``=``” equals
operator is also commutative, for example: x = y has the same meaning as
y = x.) Also note that INTERSECT interprets an empty varchar value as
empty-set, not single-item empty-string set (as IN does). See p.  for
details, as well as additional behaviors that IN, SUBSET and INTERSECT
share in common.

Index Usage by INTERSECT
''''''''''''''''''''''''

An INTERSECT query can utilize a regular (B-tree) index to increase
performance. The index should be created with ``indexvalues`` set to
``splitstrlst`` (the default), as this enables individual values of
``strlst``\ s to be accessed as needed.

IN, SUBSET, INTERSECT Commonality
"""""""""""""""""""""""""""""""""

The IN, SUBSET and INTERSECT operators, being set-like, share certain
behaviors in common:

A ``varchar`` value on either side of these operators is treated as a
single-item ``strlst`` set – regardless of the current
``varchartostrlstsep`` setting. This aids usage of IN/SUBSET/INTERSECT
in Vortex when ``arrayconvert`` is active for parameters: it provides
consistent results whether the Vortex variable is single- or
multi-value. A single ``varchar`` value will not be unexpectedly (and
incorrectly) split into multiple values using its last character as a
separator.
However, the operators differ on interpretation of *empty* varchar
values. With IN, an empty varchar value is considered a single-item
empty-string set, because IN is most often used with single-value (i.e.
non-set-like) parameters. This makes the clause “WHERE varcharColumn IN
(’red’, ’green’, ’blue’)” only return “``red``”, “``green``” or
“``blue``” varcharColumn values – not empty-string values too, as SUBSET
would. This empty-string interpretation difference is the one way in
which IN differs from SUBSET (and INTERSECT, if ``inmode`` is
``intersect``).
With SUBSET/INTERSECT however, an empty varchar value is considered an
empty set, because SUBSET/INTERSECT are more clearly set-like operators
where both operands are sets, and an empty string is more likely to be
intended to mean “empty set”. This is also more consistent with
convert() and INSERT behavior: an empty string converted or inserted
into a strlst value becomes an empty strlst, not a one-item
(empty-string) strlst.
The current (or indexed) ``stringcomparemode`` setting value is used
during IN/SUBSET/INTERSECT operations; thus case-insensitive
comparisions can be accomplished by modifying the setting. At search
time, the Texis optimizer will choose the index whose
``stringcomparemode`` setting is closest to the current value.
**Caveat:** IN/SUBSET/INTERSECT behavior with multi-value types other
than ``strlst`` is currently undefined and should be avoided.
Single-value types other than ``varchar`` have limited support
currently; it is recommended that only ``varchar`` (and ``strlst``)
types be used.

Search Condition Using LIKE
"""""""""""""""""""""""""""
In most SQL applications, a column value that contains character values
can be matched to a pattern of characters for the purpose of retrieving
one or more rows from a table. This is often referred to as *pattern
matching*. Pattern matching is useful when a user cannot be specific
about the data to be retrieved. For instance:

-  You’re not sure if someone’s last name is Robinson, Robertson, or
   Robbins. You search using the pattern “Rob”.

-  You want a list of all employees who live on Newman Avenue, Road or
   Street. You search using the pattern “Newman”.

-  You want a list of all employees whose name ends in “man”, such as
   Waterman, Spellman, or Herman. You search using the pattern “man”.

The ``LIKE`` operator is used in the ``WHERE`` clause to enable you to
retrieve records that have a partial match with a column value. The
``LIKE`` operator has the following format:
::

         WHERE  column-name  LIKE  'pattern'

In Texis the capabilities of the ``LIKE`` clause have been exponentially
increased through implementation of all features of the Metamorph search
engine. Rather than the limited single item string search allowed in
traditional SQL applications, Texis allows any valid Metamorph query to
be substituted for the ``'pattern'`` following ``LIKE``.
Therefore, in addition to traditional string searches, text fields can
be searched with all of Metamorph’s pattern matchers to find concepts,
phrases, variable expressions, approximations, and numeric quantities
expressed as text. These queries can contain multiple search items
combining calls to different Metamorph pattern matchers. Intersections
of such items can be located in proximity to one another within defined
text units such as sentences, paragraphs, or the whole record.
It is this integration of Metamorph through the ``LIKE`` clause which
brings together intelligent full text searching with relational database
technology. For instance, within the confines of the Texis relational
database, you can also issue queries to find the following:

-  All Research and Development reports covering conceptually similar
   research done on a field of interest. For example, a request for all
   research done concerning “red lenses” could discover a report about
   “rose colored glasses”.

-  All strategic information reports concerning marketing campaigns over
   a certain dollar amount. For example, such a request for marketing
   information about wheels could reveal a “sales” campaign where
   “twenty-five thousand dollars” was allocated to promote “tires”.

-  An employee whose name sounds like Shuler who helps fix computer
   problems. For example, a query for approximately Shuler and computers
   could find Elaine “Schuller” who works in “data processing”. And
   since you are querying a relational database, you could also pull up
   her phone extension and call for help.

Full use of the Metamorph query language is discussed in depth in
Chapter [Chp:MMLike]. In this section we will concentrate on simple
examples to illustrate how the ``LIKE`` clause can be used to further
qualify ``WHERE``.

LIKE Command Discussion
"""""""""""""""""""""""

-  The column name following the ``WHERE`` clause must contain character
   values; otherwise, the ``LIKE`` operator cannot be used.

-  The ``LIKE`` operator compares the value in the specified column with
   the pattern, as inserted in single quotes following ``LIKE``. A row
   is retrieved if a match occurs.

-  You can put any Metamorph query in quotes (``'query'``) in place of a
   fixed length string, although you would need to escape a literal
   ``'`` with another ``'`` by typing ``''``, if you want the character
   ``'`` to be part of the query.

-  The “pattern” inside single quotes following ``LIKE`` will be
   interpreted exactly as Metamorph would interpret such a query on its
   query line, in any Metamorph application (with the only exception
   being that a single quote or apostrophe must be escaped with another
   ``'`` to be interpreted literally).

-  Concept searching is off by default for Metamorph queries following
   ``LIKE``, but can be selectively invoked on a word using the tilde
   ‘``~``’.

-  Syntax for complete use of Metamorph query language is covered in
   Chapter [Chp:MMLike].

-  Queries using ``LIKE`` can make use of any indexing which has been
   done. An alternate form of ``LIKE`` may also be used called
   ``LIKE3``, which uses indexing exclusively with no post search. See
   Chapter [Chp:MMLike] for a thorough explanation of all types of text
   searches possible with ``LIKE`` and ``LIKE3``, and their relation to
   indexed information.

**Example:** Let’s start with a simple example. You wish to retrieve all
reports where “ink” is part of the title, without knowing the full
title.
If you enter the statement:
::

         SELECT  TITLE
         FROM    REPORT
         WHERE   TITLE  LIKE  'ink' ;

the result displayed will be:
::

      TITLE
      Innovations in Disappearing Ink
      Disappearing Ink Promotional Campaign
      Ink Color Panorama

In this query, you are retrieving the titles of all reports whose title
is “like” the pattern “ink”.
In other cases you may not know the exact words you are looking for. A
simple example where a wildcard ’\ ``*``\ ’ is used follows.
::

         SELECT  AUTHOR, DEPT
         FROM    REPORT
         WHERE   AUTHOR  LIKE  'san*' ;

The result will be:
::

      AUTHOR                 DEPT
      Sanchez, Carla         MKT
      Sanders, George G.     FIN
      Claus, Santa           MKT

Relevance Ranking Using LIKER and LIKEP
"""""""""""""""""""""""""""""""""""""""
In addition to the Metamorph searches listed above there is another type
of search based on Metamorph. This will return rows in order of
relevance, with the most relevant record first (unless other clauses
alter this order, e.g. an ``ORDER BY``). ``LIKER`` calculates a
relevance based solely on the presence or absence of the terms in the
document. ``LIKEP`` uses this same information, but also uses the
proximity of the terms to calculate relevance.
There are several restrictions and points to note about ``LIKER`` and
``LIKEP``. The conditions that must be met to obtain a relevancy search
are that a Metamorph index exists on the field in question. ``LIKER``
can only work with an index; while ``LIKEP`` can work without such an
index, it performs best with one. The other condition is that the query
should consist of word terms only. None of the other pattern matchers
are available with ``LIKER``; they are available with ``LIKEP``, but at
a cost in performance (post-processing is required).
The query is a list of terms to be searched for. The words are weighted
by their uniqueness in the document set being searched. This means that
infrequent words are weighted more than common words.
The weight that was calculated for the record is available by selecting
the generated field ``$rank``, which will contain the rank value. The
rank value for ``LIKER`` is unscaled. With ``LIKEP`` the number will
range between 0 and 1000, where greater values indicate greater computed
relevance to the query.
The default ordering of ``LIKER`` and ``LIKEP`` (rank-descending) may be
changed by an ``ORDER BY`` clause. Historically,
an ``ORDER BY`` containing ``$rank`` (or potentially any expression
containing ``$rank``) would usually order descending as well – despite
the typical default ``ORDER BY`` order being *ascending* – because
rank-descending is considered more useful (and often low-rank results
are eliminated prior to ordering anyway). However, this caused confusion
when giving the ``DESC`` flag, as then ORDER BY $rank DESC would return
*ascending* results.
Currently, ``ORDER BY`` clauses containing
``$rank`` will order consistently with other ``ORDER BY`` clauses – i.e.
numerically ascending unless the ``DESC`` flag is given. This means that
most ORDER BY $rank clauses should probably be
ORDER BY $rank DESC, to get rank-descending behavior.

Relevance Ranking Command Discussion
""""""""""""""""""""""""""""""""""""

Result ranking is a useful feature, although due to the variety of cases
where you might want to use ranking, there are a number of variables
that control the ranking algorithm.
The first major choice will be whether proximity is important. This will
indicate if you want to use ``LIKER`` or ``LIKEP``. ``LIKER`` uses the
index to determine the frequencies of the terms, and the presence of
absence of the terms in each document to determine the rank for each
document. Each term is assigned a weight between 0 and 1000, and the
rank value for the document is the sum of the weights for all the terms
that occur.
``LIKER`` has a threshold value, such that documents with a lower rank
value than the threshold value will not be returned. This prevents a
large number of irrelevant documents from being returned. Initially the
threshold is set to the weight of the term with the highest weight. If
there are more than five terms then the threshold is doubled, and if
there are more than 10 terms the threshold is doubled again. This keeps
queries containing a lot of terms from returning irrelevant hits. It is
possible to force the threshold lower if desired to return more records.
This can be performed either by specifying the maximum number of records
a term should occur in, and still be returned by ``LIKER``. This is the
``likerrows`` variable. For example, in a three term query, where the
terms occur in 400, 900 and 1400 records respectively, setting
``likerrows`` to 1000 would allow records containing only the second
search term to be returned.
In general ``LIKEP`` will perform the same initial step as ``LIKER`` to
determine which documents to rank. ``LIKEP`` then looks at the
``likeprows`` highest ranked documents from ``LIKER``, and recalculates
the rank by actually looking inside the document to see where the
matching terms occur. Because of this it will be slower than ``LIKER``,
although if you are using a Metamorph inverted index the ranks may still
be determinable from the index alone, saving actual table accesses.
There are a number of variables that can be set with ``LIKEP``, which
affect both how documents are ranked, as well as how many documents are
returned. See the “Rank knobs” (p. ) and “Other ranking properties”
(p. ) discussions in the Server Properties section of the manual.

Query searching using LIKEIN
""""""""""""""""""""""""""""
``LIKEIN`` is used for doing profiling, where you have a lot of queries
and you want to find which queries match the given text. This is
typically used when the number of queries is large and relatively
constant, and there is a stream of new texts to match. ``LIKEIN`` will
find the queries that would match the text. To work efficiently you
should have a ``METAMORPH COUNTER`` index created on the field
containing the queries.

Search Condition Using MATCHES
""""""""""""""""""""""""""""""
The MATCHES keyword allows you to match fields against expressions. This
is most useful when you have fields with a small amount of text and do
not need the full power of Metamorph. Typical uses would be names, part
numbers or addresses.
In the query an underscore will match any single character, and a
percent sign will match any number of characters. For example
::

         SELECT  AUTHOR, DEPT
         FROM    REPORT
         WHERE   AUTHOR  MATCHES  'San%' ;

The result will be:
::

      AUTHOR                 DEPT
      Sanchez, Carla         MKT
      Sanders, George G.     FIN

The special characters used with MATCHES can be changed using the set
matchmode SQL statement. The default value of 0 produces the behavior
documented above which is standard in SQL. Setting ``MATCHMODE`` to 1
will change the special characters such that asterix will match any
number of characters, and a question mark will match any single
character, which is more familiar to many people.
Comparing the results to the earlier example using ``LIKE`` you will see
that Claus, Santa does not match, as the match has to occur at the
beginning of the field.
MATCHES can make use of a regular index on the field. It will not use a
Metamorph index.

Sorting Your Results
~~~~~~~~~~~~~~~~~~~~
The output from the above queries may not be in the desired order. For
example, you may want the list of departments arranged alphabetically.
Sorting is the process of rearranging data into some specific order. To
sort the output into a desired sequence, a field or fields are specified
that determine the order in which the results are arranged. These fields
are called *sort keys*.
For example, if the department data is sorted into alphabetical order by
department, the department name is the sort key. The budget field is the
sort key if the department table is sorted by amount of budget. Note
that the sort key can be numeric (budget) or character (department
name).
Results can be sorted into ascending or descending sequence by sort key.
Ascending means increasing order, and descending means decreasing order.
For example, sorting the department table in ascending order by budget
means the department data will be arranged so that the department with
the lowest budget is first and the department with the highest budget is
last. If we instead sorted in descending order, the department with the
highest budget would appear first, the department with the lowest budget
would appear last.
Sorting character data in ascending or descending order is based on a
coding, or collating, sequence assigned to numbers and letters by the
computer. For example, when department name is the sort key and you want
the data arranged alphabetically, that indicates ascending order. If you
want the data arranged in reverse alphabetical order, then specify
descending order.
To sort your results using Texis, add the ORDER BY clause to the
``SELECT`` statement. The form of this clause is:
::

         ORDER BY  column-name  [DESC]

where DESC indicates the rows are to be arranged in descending order. If
DESC is omitted, your output is sorted in ascending order.
This clause fits into the ``SELECT`` expression following the ``WHERE``
clause, as shown below:
::

         SELECT      column-name1 [,column-name2] ...
         FROM        table-name
         [WHERE      search-condition]
         [ORDER BY   column-name [DESC] ] ;

**Example:** Retrieve a list of departments arranged by division, and
within that division, arranged by highest budget first.
If you enter the statement:
::

         SELECT      DNAME, DIV, BUDGET
         FROM        DEPARTMENT
         ORDER BY    DIV, BUDGET DESC ;

Output will appear in ascending order automatically if DESC is omitted.
The result displayed will be:
::

      DNAME                                  DIV     BUDGET
      Corporate Legal Support                CORP    28000
      Finance and Accounting                 CORP    26000
      Management and Administration          CORP    22000
      Recruitment and Personnel              CORP    15000
      Supplies and Procurement               CORP    10500
      Strategic Planning and Intelligence    INFO    28500
      Information Systems Management         INFO    22500
      Corporate Library                      INFO    18500
      Manufacturing                          PROD    32000
      Research and Development               PROD    27500
      Product Marketing and Sales            PROD    25000
      Customer Support and Service           PROD    11000

Notice that all departments in the same division are listed together,
with the divisions listed in ascending order, as the default ordering
for DIV. Within each division, the department with the highest budget is
listed first, since descending order was specified for BUDGET.
It is possible to have as many as 50 sort keys. The order in which the
sort keys are listed is the order in which the data will be arranged.
This chapter has introduced several ways to retrieve rows and columns
from a table. In the next chapter, you will learn how to perform
calculations on data stored in a table.
