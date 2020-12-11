Indexing for Increased Performance
----------------------------------


When and How to Index
~~~~~~~~~~~~~~~~~~~~~

There are two basic mechanisms for accessing Texis tables: a table space
scan which is sequential, and an index based scan which is direct. Index
based retrieval is usually more efficient than table space scan with
some exceptions.

The use of indexes is one of the major ways in which the performance of
queries (i.e. the speed with which results are retrieved) can be
improved in relational databases. Indexes allow the DBMS to retrieve
rows from a table without scanning the entire table, much as a library
user can use a card catalog to find books without scanning the entire
library.

The creation of indexes improves the performance associated with
processing large tables. However, an excessive number of indexes can
result in an increase in processing time during update operations
because of the additional effort needed to maintain the indexes. Thus,
for tables undergoing frequent change, there could be a “cost”
associated with an excessive number of indexes.

In addition, as the number of indexes increases, the storage
requirements needed to hold the indexes becomes significant. Other
cautions include not indexing small amounts of data as doing so may slow
down searches due to the overhead of looking in an index. The point at
which it makes sense to use an index will depend upon the system in use.
And it would be important to use the correct kind of index for the job.

Since there are so many factors involved in the decision as to whether
and what kind of index will most optimize the search, Texis largely
takes over the management of these decisions. The user can suggest those
tables on which an index ought to be created. Beyond this, the user
would not know the status of the index, which is always in flux, nor
whether it has been updated at the time of the search.

Unlike other systems, Texis ensures that all information which has been
added to any table can be searched immediately, regardless of whether it
has been indexed, and regardless of whether it has been suggested that
an index be maintained on that table or not. Sequential table space
scans and index based scans are efficiently managed by Texis so that the
database can always be searched in the most optimized manner, with the
most current information available to the user.

To this end, there are two types of indexes supported by Texis:

#. The canonical sorted order alphabetical index found in traditional
   SQL systems, and

#. The Metamorph index, optimized for systems containing a large number
   of rows, or a lot of text, or both. The Metamorph index is used when
   it is expected that ``LIKE`` or its variants will be common on the
   field being indexed.

When an index is created, neither an end user nor an application
programmer need (nor can) reference the index in a query. Indexes are
used automatically by Texis when needed to choose the best path to the
data.


Creating An Index
~~~~~~~~~~~~~~~~~

The CREATE INDEX command is used to establish an index. The form of this
command is:

::

         CREATE INDEX index-name
         ON table-name (column-name [DESC] [, column-name [DESC]] ...)
         [WITH option-name [value] [option-name [value] ...]] ;


Command Discussion
""""""""""""""""""

-  Each index is assigned a name and is related to a particular table
   based on the ON table-name clause.

-  The entries in an index are ordered on a specified column within the
   specified table, and arranged in either ascending or descending
   order.

-  The term *index key* refers to the column (or set of columns) in a
   table that is used to determine the order of entries in an index.
   Where an index does consist of multiple columns, the most important
   column is placed first.

-  Further options controlling how and what type of index is created may
   be set in the WITH clause; see p. .

-  A table can have many different indexes associated with its columns.
   Each index is created by issuing a separate CREATE INDEX command.

**Example**: Many queries to the ``EMPLOYEE`` table reference the
employee’s department; therefore, the database designer decides to
create an index using the ``DEPT`` column to improve performance related
to these queries.

This command:

::

         CREATE INDEX DEPTINDEX
         ON EMPLOYEE (DEPT) ;

would direct the creation of an index called ``DEPTINDEX`` (the Index
name) on the table ``EMPLOYEE``, using the ``DEPT`` column as indicated
in parentheses as the Index key.

The index can be used by the system to provide quick access to employee
data that are subject to conditions related to the employee’s
department.


Creating a Unique Index
~~~~~~~~~~~~~~~~~~~~~~~

The *primary key* is an important concept in data processing. The
primary key is a field or combination of fields in a record that allows
you to uniquely identify a record from all the other records in a table.
For example, companies assign IDs to employees as unique identifiers.
Thus, an employee ID can serve as a primary key for personnel records.

When a table is created in Texis, duplicate records can be stored in the
table. The uniqueness characteristic is not enforced automatically. To
prevent duplicate records from being stored in a table, some steps must
be taken.

First, a separate file called an “index” must be created. In this case
the index is created so that the DBMS can ensure that all values in a
special column or columns of a table are unique. For example, the
``EMPLOYEE`` table can be indexed on ``EID`` (employee ID) so that each
row of the ``EMPLOYEE`` table contains a different employee ID value
(i.e., no duplicate ``EID``\ s can be entered.)

A variation of the CREATE INDEX command, CREATE UNIQUE INDEX, is used to
establish an index that assures no duplicate primary key values are
stored in a table. The form of this command is:

::

         CREATE [UNIQUE] INDEX index-name
         ON table-name (column-name [DESC] [,column-name [DESC]] ...) ;


Command Discussion
""""""""""""""""""

-  The keyword ``UNIQUE`` in the clause CREATE UNIQUE INDEX specifies
   that in the creation and maintenance of the index no two records in
   the index table can have the same value for the index column (or
   column combination). Thus, any ``INSERT`` or ``UPDATE`` command that
   attempts to add a duplicate row in the index would be rejected.

-  Each index is assigned a name and is related to a particular table
   based on the ON table-name clause.

-  An index is based on the specified column within the specified table,
   and will be arranged in ascending order.

**Example:** Create an index for the ``EMPLOYEE`` table that prevents
records with the same employee ID from being stored in the table with
this command:

::

         CREATE UNIQUE INDEX EMPINDEX
         ON EMPLOYEE (EID) ;

This command directs the creation of a unique index on the ``EMPLOYEE``
table, where the index name is ``EMPINDEX``. The ``EID`` column as
indicated in parentheses is the Index key.

In other words, an index called ``EMPINDEX`` has been created on the
``EMPLOYEE`` table for the employee ID number.

The index is stored separately from the ``EMPLOYEE`` table. The example
below shows the relationship between ``EMPLOYEE`` and ``EMPINDEX`` after
ten employees have been added to the ``EMPLOYEE`` table. Each row of the
index, ``EMPINDEX``, consists of a column value for the index column and
a pointer, or physical address, to the location of a row in the
``EMPLOYEE`` table. As employees are added or deleted from the
``EMPLOYEE`` table, Texis automatically updates the index in the most
efficient and timely manner.

To conceptualize how the index works, assume you didn’t realize
Chapman’s record was already stored in the ``EMPLOYEE`` table and you
attempt to add her record again. You enter the command:

::

         INSERT INTO EMPLOYEE
         VALUES ('103','Chapman, Margaret','LIB','STAFF','PART',22000) ;

and Texis responds with an error message, such as:

::

         ERROR: Duplicate Value in Index

This message occurs because the value 103, the employee ID (``EID``), is
already stored in ``EMPINDEX`` and attempting to add another 103 value
results in a duplicate value, which is not permitted in a unique index.

When we add a new employee named Krinski with an ``EID`` equal to 110 by
entering this command:

::

         INSERT INTO EMPLOYEE
         VALUES ('110','Krinski','LIB','DHEAD','FULL',32500) ;

the record is successfully created. The value 110 did not previously
exist in the unique index ``EMPINDEX``, and so it was allowed to be
entered as a new row in the ``EMPLOYEE`` table. As the ``EMPINDEX`` is
in sorted order, it is much faster to ascertain that information than it
would be by searching the entire ``EMPLOYEE`` table.

The relationship between ``EMPINDEX`` the index, and ``EMPLOYEE`` the
table, appear below as they would containing 10 employee records. The
dashed lines indicate pointers from the index to rows in the table.
However, this is conceptual rather than actual, and not all pointers are
shown.

::

    EMPINDEX      EMPLOYEE
    Index         Table
                  EID  ENAME               DEPT   RANK   BENEFITS   SALARY
    101 --------> 101  Aster, John A.      MKT    STAFF  FULL       32000
    102 --+       109  Brown, Penelope     MKT    DHEAD  FULL       37500
    103   |       104  Jackson, Herbert    RND    STAFF  FULL       30000
    104   |  +--> 108  Jones, David        RND    DHEAD  FULL       37500
    105   +--|--> 102  Barrington, Kyle    MGT    DHEAD  FULL       45000
    106      |    106  Sanchez, Carla      MKT    STAFF  FULL       35000
    108 -----+    105  Price, Stella       FIN    DHEAD  FULL       42000
    107           103  Chapman, Margaret   LIB    STAFF  PART       22000
    109           107  Smith, Roberta      RND    STAFF  PART       25000
    110 --------> 110  Krinski, Wanda      LIB    DHEAD  FULL       32500


Creating a Metamorph Index
~~~~~~~~~~~~~~~~~~~~~~~~~~

A sorted order index is optimized for columns containing values of
limited length, which can easily be canonically managed. In some cases,
especially when a column contains a large amount of text, there is a
need for an index which goes beyond the methods used in these previous
examples.

For example, let us take the case of the News database being archived on
a daily basis by the Strategic Planning and Intelligence Department. The
entire body of the news article is stored in a table, whether the data
type in use is ``VARCHAR``, indicating a variable length number of
characters, or ``INDIRECT``, indicating it points elsewhere to the
actual location of the files. While subjects, dates, and bylines are
important, the most often queried part is the body of the article, or
the text field itself. The column we want to index is a text column
rather than something much more concise like an an employee ID number.

To accurately find text in the files, where search items are to be found
in proximity to other search items within some defined delimiters, all
the words of all the text in question must be indexed in an efficient
manner which still allows everything relevant to be found based on its
content, even after it has been archived away. A Metamorph index
combines indexing technology with a linear free text scan of selected
portions of the database where appropriate in order to accomplish this.
This linear scan following the index lookup is referred to as a
*post-search* or *post-processing*.

Metamorph query language as used following ``LIKE`` and its variants is
described in detail in Chapter [Chp:MMLike], *Intelligent Text Search
Queries*. Where you anticipate such ``LIKE`` queries will be common on
that field, it would be appropriate to create a Metamorph index.

The form of the command is:

::

         CREATE METAMORPH [INVERTED|COUNTER] INDEX index-name
         ON table-name (column-name [, column-name...]) ;

Syntax is the same as in the previous CREATE INDEX examples, except that
you are specifying the type of index you want created (i.e. a Metamorph
index).

**Example:** The news database that is being accumulated from selected
news articles is getting too large to search from beginning to end for
content based searches which make heavy use of the ``LIKE`` clause. A
Metamorph index should be created for the Strategic Planning and
Intelligence Department to enhance their research capability. The column
containing the text of the articles is called ``BODY``.

An index called ``BODYINDEX`` will be created and maintained on the
``BODY`` column of the ``NEWS`` table, which contains the full text of
all collected news articles. Now content searches can stay fast as well
as accurate, regardless of how large this database becomes.

Additional columns can be specified in addition to the text field to be
indexed. These should be fixed length fields, such as dates, counters or
numbers. The extra data in the index can be used to improve searches
which combine a ``LIKE`` statement with restrictions on the other
fields, or which ORDER BY some or all of the other fields.


Metamorph Index Types: Inverted vs. Compact vs. Counter
"""""""""""""""""""""""""""""""""""""""""""""""""""""""

There are three types of Metamorph index: inverted, compact and counter.
All are used to aid in resolving
``LIKE``/``LIKEP``/``LIKE3``/``LIKER``/``LIKEIN`` queries, and are
created with some variant of the syntax CREATE METAMORPH INDEX.


Inverted
""""""""

An inverted Metamorph index is the most commonly used type of Metamorph
index, and is created with CREATE METAMORPH INVERTED INDEX. In Texis
version 7 (and ``compatibilityversion`` 7) and later, this is the
default Metamorph index type created when no other flags are given, e.g.
CREATE METAMORPH INDEX; in version 6 (or ``compatibilityversion`` 6), a
compact index is created. The version 7 index option WORDPOSITIONS ’on’
(p. ) also explicitly creates this type of Metamorph index (same effect
as the ``INVERTED`` flag after ``METAMORPH``).

An inverted Metamorph index maintains knowledge not only of what rows
words occur in, but also what position in each row the words occur in
(the ``WORDPOSITIONS``). With such an index Texis can often avoid a
post-search altogether, because the index contains all the information
needed for phrase resolution and rank computation. This can speed up
searches more than a compact Metamorph index, especially for ranking
queries using ``LIKEP``, or phrase searches. Because of the greater
range of queries resolvable with an inverted Metamorph index (vs.
compact), in Texis version 7 and later it is the default Metamorph type
created. However, an inverted Metamorph index consumes more disk space,
typically 20-30% of the text size versus about 7% for a compact
Metamorph index. Index updating is also slower because of this.


Compact
"""""""

A compact Metamorph index maintains knowledge of what rows words occur
in, but does not store word position information. In Texis version 7 and
later, it is created by adding the index option WORDPOSITIONS ’off’
(p. ). In Texis version 6 and earlier, this was the default Metamorph
index type, and was created with CREATE METAMORPH INDEX (no
flags/options).

Because of the lack of word position information, a compact Metamorph
index only consumes about 7% of the text size in disk space (vs. about
20-30% for a Metamorph inverted index); this compact size can also speed
up its usage. However, a post-process search is needed after index usage
if the query needs word-position information (e.g. to resolve phrases,
within “``w/N``” operators, ``LIKEP`` ranking), which can greatly slow
such queries. Thus a compact Metamorph index is best suited to queries
that do not need word position information, such as single non-phrased
words with no special pattern matchers, and no ranking (e.g. ``LIKE``).
A ``LIKER`` or ``LIKE3`` search (below), which never does a post-search,
can also use a compact Metamorph index without loss of performance.


Counter
"""""""

A Metamorph counter index contains the same information that a compact
Metamorph index has, but also includes additional information which
improves the performance of ``LIKEIN`` queries. If you are doing
``LIKEIN`` queries then you should create this type of index, otherwise
you should use either the normal or inverted forms of the Metamorph
index. A Metamorph counter index is created with CREATE METAMORPH
COUNTER INDEX; in Texis version 7 and later the COUNTS ’on’ index option
(p. ) can be given instead of the ``COUNTER`` flag to accomplish the
same action.


Metamorph Index Capabilities and Limitations
""""""""""""""""""""""""""""""""""""""""""""

As with any tool the best use can be obtained by knowing the
capabilities and limitations of the tool. The Metamorph index allows for
rapid location of records containing one or more keywords. The Metamorph
index also takes care of some of the set logic.

The following should be noted when generating queries. The most
important point is the choice of keywords. If a keyword is chosen that
occurs in many files, then the index will have to do more work to keep
track of all the files possibly containing that word. A good general
rule of thumb is “The longer the word, the faster the search”.

Also, neither type of Metamorph index is useful for special pattern
matchers (REX, XPM, NPM) as these terms cannot be indexed. If other
indexable terms are present in the query, the index will be used with
them to narrow the result list, but a post-search or possibly even a
complete linear scan of the table may be needed to resolve special
pattern matchers.


Using LIKE3 for Index Only Search (No Post-Search)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In certain special cases, such as static information which does not
change at all except under very controlled circumstances easily managed
by the system administrator, there may be instances where an index based
search with no qualifying linear post-search may be done without losing
meaningful responses to entered queries.

This kind of search is completely optimized based on certain defaults
which would be known to be acceptable, including:

-  All the search items can be keywords (i.e., no special pattern
   matchers will be used).

-  All searches can be done effectively within the length of the field
   (i.e., the delimiters used to define proximity of search items is the
   length of the whole text field).

As more often than not maintaining all the above rules is impractical,
dispensing with the post-search would not be done very frequently.
However, in some circumstances where these rules fit, the search
requirements are narrow, and speed is of the essence, the post-search
can be eliminated for optimization purposes.

Texis will examine the query given to ``LIKE``, and if it can guarantee
the same results without the post-search it will not perform the
post-search, and ``LIKE`` will then be equivalent to ``LIKE3``. With
these caveats in mind, ``LIKE3`` may be substituted for ``LIKE`` in any
of the queries illustrated in the previous chapters.


Creating an Inverted Index
~~~~~~~~~~~~~~~~~~~~~~~~~~

The inverted index (not to be confused with a Metamorph inverted index–a
different type of index) is a highly specialized index, which is
designed to speed up one class of query only, and as such has some
limitations on its use. The primary limitation is that it can currently
only be used on a field that is of the type UNSIGNED INT or ``DATE``.
Inverted indexes can be used to speed up the ORDER BY operation in the
case that you are ordering by the field that was indexed only. For
maximum effect you should also have the indexed ordered in the same
manner as the ORDER BY. In other words if you want a descending order,
you should have a descending index.

An inverted index can be created using this command:

::

         CREATE INVERTED INDEX SALINDEX
         ON EMPLOYEE (SALARY) ;


Index Options
~~~~~~~~~~~~~

A series of index options may be specified using a WITH clause at the
end of any CREATE [index-type] INDEX statement:

::

         CREATE [index-type] INDEX index-name
         ON table-name (column-name [DESC] [, column-name [DESC]] ...)
         [WITH option-name [value] [option-name [value] ...]] ;

Index options control how the index is made, or what sub-type of index
is created. Many options are identical to global or server properties
set with the SET statement (p. ), but as options are set directly in the
CREATE INDEX statement, they override those server properties, yet only
apply to the statement they are set in. Thus, using index options allows
modularization of CREATE INDEX statements, making it clearer what
settings apply to what index by directly specifying them in the CREATE
statement, and avoiding side-effects on later statements.

Note that the WITH clause is only supported in Texis version 7 and
later. Previous releases can only set server-wide properties, via the
SET statement.


Available Options
"""""""""""""""""

The available index options are as follows. Note that some options are
only applicable to certain index types, as noted; using a option that
does not apply to the given index type will result in an error:

-  | ``counts 'on'|'off'``
   | For Metamorph or Metamorph counter index types only. If set to
     “``on``”, creates a Metamorph counter type index (useful for
     ``LIKEIN`` searches); if “``off``” (the default), a regular
     Metamorph or Metamorph inverted index is created.

-  | ``indexmaxsingle N``
   | For Metamorph, Metamorph inverted, and Metamorph counter index
     types only. Same as the indexmaxsingle server property (p. ).

-  | ``indexmem N``

-  | ``indexmeter N|type``

-  | ``indexspace N``

-  | ``indexvalues type``
   | These options have the same effect as the same-named server
     properties set with SET.

-  | ``indexversion N``

-  | ``keepnoise 'on'|'off'``

-  | ``noiselist ('word','word',...)``

-  | ``textsearchmode mode``

-  | ``wordexpressions ('expr','expr',...)``
   | For Metamorph, Metamorph inverted, and Metamorph counter index
     types only. Same effect as the same-named server properties.

-  | ``wordpositions 'on'|'off'``
   | For Metamorph and Metamorph inverted index types only. If “``on``”
     (the default in version or ``compatibilityversion`` 7 and later),
     creates a full-inversion (Metamorph inverted) index; if “``off``”,
     creates a compact (Metamorph) index.

-  | ``max_index_text N``

-  | ``stringcomparemode mode``
   | For regular index types only. Same effect as the same-named server
     properties.


Dropping an Index
~~~~~~~~~~~~~~~~~

Any index – unique or non-unique, sorted order or Metamorph – can be
eliminated if it is no longer needed. The DROP INDEX command is used to
remove an index. The format of this command is similar to the DROP TABLE
command illustrated in Chapter [chp:TabDef].

::

         DROP INDEX  index-name ;

**Example:** Let’s say the ``DEPTINDEX`` is no longer needed. Delete it
with this statement:

::

         DROP INDEX  DEPTINDEX ;

The table on which the index was created would not be touched. However,
the index created for it has been removed.

.. _DBCurr:

Keeping the Database Current
----------------------------

To keep the data in your database current, three types of transactions
must be performed on the data. These transactions are:

#. Adding new records.

#. Changing existing records.

#. Deleting records no longer needed.


Adding New Records
~~~~~~~~~~~~~~~~~~

Once a table has been defined and before any data can be retrieved, data
must be entered into the table. Initially, data can be entered into the
table in several ways:

-  Batch mode: Data is loaded into the table from a file.

-  Interactive mode: Data for each record is added by interactive
   prompting of each column in a record.

-  Line Input: A row of data is keyed for insertion into a table using a
   line editor and then is submitted to the database.

Generally a Load Program would be used to load data into the tables at
the outset. It would be unusual to use line input especially to get
started, but it is used in the following examples so that the correct
syntax can be clearly seen.


Inserting One Row at a Time
"""""""""""""""""""""""""""

In addition to initially loading data into tables, records can be added
at any time to keep a table current. For example, if a new employee is
hired, a new record, or row, would be added to the EMPLOYEE table. The
``INSERT`` command is used to enter a row into a table. The command has
two formats:

#. Entering one row at a time.

#. Entering multiple rows at a time.

In the first format, the user enters values one row at a time, using the
following version of the ``INSERT`` command:

::

         INSERT INTO  table-name [(column-name1 [,column-name2] ... )]
         VALUES  (value1, value2 ... ) ;


Command Discussion
""""""""""""""""""

-  The INSERT INTO clause indicates that you intend to add a row to a
   table.

-  Following the INSERT INTO clause, the user specifies the name of the
   table into which the data is to be inserted.

-  When data values are being entered in the same order the columns were
   created in there is no need to list the column names following the
   INSERT INTO clause. However, sometimes when a row is added, the
   correct ordering of column values is not known. In those cases, the
   columns being added must be listed following the table name in the
   order that the values will be supplied.

-  Following the keyword VALUES are the values to be added to one row of
   a table. The entire row of values is placed within parentheses. Each
   data value is separated from the next by a comma. The first value
   corresponds to the first column in the table; the second value
   corresponds to the second column in the table, and so on.

**Example:** When the EMPLOYEE table is first created, it has no
employee data stored in it. Add the first record to the table, where the
data values we have are as follows:

::

         EID = 101
         ENAME = Aster, John A.
         DEPT = MKT
         RANK = STAFF
         BENEFITS = FULL
         SALARY = 32000

You can create a record containing these values by entering this
command:

::

         INSERT INTO EMPLOYEE
         VALUES (101,'Aster, John A.','MKT','STAFF','FULL',32000) ;

Quotes are placed around character values so Texis can distinguish data
values from column names.

A new employee record gets added to the EMPLOYEE table, so that the
table now looks like this, if this were the only row entered:

::

      EID  ENAME               DEPT   RANK   BENEFITS   SALARY

      101  Aster, John A.      MKT    STAFF  FULL       32000


Inserting Text
""""""""""""""

There are a few different ways to manage large quantities of text in a
database. The previous examples given for the REPORT table concentrated
on the VARCHAR (variable length character) column which held a filename
as a character string; e.g., ``'/data/rnd/ink.txt'`` as stored in the
FILENAME column. This column manages the filename only, not the text
contained in that file.

In the examples used in Chapter [chp:TabDef], *Table Definition*, a
RESUME table is created which uses a VARCHAR field of around 2000
characters to hold the text of the resumes. In this case, the job
experience text of each resume is stored in the column EXP. A Load
Program would be used to insert text of this length into the column of a
table.

Another way Texis has of managing text is to allow the files to remain
outside the confines of the table. Where the INDIRECT data type is used,
a filename can be entered as a value which points to an actual file,
rather than treated as a character string. The INDIRECT type looks at
the contents of the file when doing ``LIKE``, and these contents can be
retrieved using the API (Application Program Interface).

The form of the INSERT INTO command is the same as above. Where a data
type is defined as ``INDIRECT``, a filename may be entered as the value
of one or more columns.

**Example:** Let’s say we have the following information available for a
resume to be entered into the RESUME table, and that the job experience
column EXP has been defined as INDIRECT.

::

        RES_ID = R421
        RNAME = Smith, James
        JOB = Jr Analyst
        EDUC = B.A. 1982 Radford University
        EXP = contained in the resume file "/usr/local/resume/smith.res"

Use this INSERT INTO statement to add a row containing this information
to the RESUME table:

::

         INSERT INTO RESUME
         VALUES ('R421','Smith, James','Jr Analyst',
                 'B.A. 1982 Radford University',
                 '/usr/local/resume/smith.res') ;

The EXP column acts as a pointer to the full text files containing the
resumes. As such, the text in those files responds to all
``SELECT``-``FROM``-``WHERE`` statements. Thus Metamorph queries used
after ``LIKE`` can be done on the text content manipulated by Texis in
this table.


Inserting Multiple Rows at a Time
"""""""""""""""""""""""""""""""""

In addition to adding values to a table one row at a time, you can also
use a variation of the ``INSERT`` command to load some or all data from
one table into another table. The second form of the ``INSERT`` command
is used when you want to create a new table based on the results of a
query against an existing table. The form of this ``INSERT`` command is:

::

         INSERT INTO table-name
           SELECT  expression1 [,expression2] ...
           FROM    table-name
           [WHERE  search-condition] ;


Command Discussion
""""""""""""""""""

-  The INSERT INTO clause indicates that you intend to add a row or rows
   to a table.

-  Following the INSERT INTO clause, the user specifies the name of the
   table to be updated.

-  The query is evaluated, and a copy of the results from the query is
   stored in the table specified after the INSERT INTO clause. If rows
   already exist in the table being copied to, then the new rows are
   added to the end of the table.

-  Block inserts of text columns using ``INDIRECT`` respond just as any
   other column.

**Example:** Finance wants to do an analysis by department of the
consequences of a company wide 10% raise in salaries, as it would affect
overall departmental budgets. We want to manipulate the relational
information stored in the database without affecting the actual table in
use.

*Step 1:* Create a new table named EMP\_RAISE, where the projected
results can be studied without affecting the live stored information.
Use this CREATE TABLE statement, which defines data types as in the
original table, EMPLOYEE, creating an empty table.

::

         CREATE TABLE  EMP_RAISE
           (EID       INTEGER
            ENAME     CHAR(15)
            DEPT      CHAR(3)
            RANK      CHAR(5)
            BENEFITS  CHAR(4)
            SALARY    INTEGER) ;

*Step 2:* Copy the data in the EMPLOYEE table to the EMP\_RAISE table.
We will later change salaries to the projected new salaries using the
``UPDATE`` command. For now, the new table must be loaded as follows:

::

         INSERT INTO  EMP_RAISE
           SELECT  *
           FROM    EMPLOYEE ;

The number of records which exist in the EMPLOYEE table at the time this
INSERT INTO command is done is the number of records which will be
created in the new EMP\_RAISE table. Now that the new table has data
values, it can be queried and updated, without affecting the data in the
EMPLOYEE table.

An easier way to create a copy of the table is to use the following
syntax:

::

         CREATE TABLE  EMP_RAISE AS
           SELECT  *
           FROM    EMPLOYEE ;

which creates the table, and copies it in one statement. Any indexes on
the original table will not be created on the new one.


Updating Records
~~~~~~~~~~~~~~~~

Very often data currently stored in a table needs to be corrected or
changed. For example, a name may be misspelled or a salary figure
increased. To modify the values of one or more columns in one or more
records of a table, the user specifies the ``UPDATE`` command. The
general form of this statement is:

::

         UPDATE  table-name
         SET     column-name1 = expression1
                 [,column-name2 = expression2] ...
         [WHERE  search-condition] ;


Command Discussion
""""""""""""""""""

-  The ``UPDATE`` clause indicates which table is to be modified.

-  The SET clause is followed by the column or columns to be modified.
   The expression represents the new value to be assigned to the column.
   The expression can contain constants, column names, or arithmetic
   expressions.

-  The record or records being modified are found by using a search
   condition. All rows that satisfy the search condition are updated. If
   no search condition is supplied, all rows in the table are updated.

**Example:** Change the benefits for the librarian Margaret Chapman from
partial to full with this statement:

::

         UPDATE EMPLOYEE
         SET    BENEFITS = 'FULL'
         WHERE  EID = 103 ;

The value ``'FULL'`` is the change being made. It will replace the
current value ``'PART'`` listed in the BENEFITS column for Margaret
Chapman, whose employee ID number is 103. A change is made for all
records that satisfy the search condition; in this example, only one row
is updated.

**Example:** The finance analysis needs to include the effects of a 10%
pay raise to all staff; i.e., to all employees whose RANK is STAFF.

Use this statement to update all staff salaries with the intended raise:

::

         UPDATE  EMP_RAISE
         SET     SALARY = SALARY * 1.1
         WHERE   RANK = 'STAFF' ;

If a portion of the EMP\_RAISE table looked like this before the update:

::

      EID  ENAME               DEPT   RANK   BENEFITS   SALARY
      101  Aster, John A.      MKT    STAFF  FULL       32000
      102  Barrington, Kyle    MGT    DHEAD  FULL       45000
      103  Chapman, Margaret   LIB    STAFF  PART       22000
      104  Jackson, Herbert    RND    STAFF  FULL       30000
      105  Price, Stella       FIN    DHEAD  FULL       42000
      106  Sanchez, Carla      MKT    STAFF  FULL       35000
      107  Smith, Roberta      RND    STAFF  PART       25000

It would look like this after the update operation:

::

      EID  ENAME               DEPT   RANK   BENEFITS   SALARY
      101  Aster, John A.      MKT    STAFF  FULL       35200
      102  Barrington, Kyle    MGT    DHEAD  FULL       45000
      103  Chapman, Margaret   LIB    STAFF  PART       24200
      104  Jackson, Herbert    RND    STAFF  FULL       33000
      105  Price, Stella       FIN    DHEAD  FULL       42000
      106  Sanchez, Carla      MKT    STAFF  FULL       38500
      107  Smith, Roberta      RND    STAFF  PART       27500

Notice that only the STAFF rows are changed to reflect the increase.
DHEAD row salaries remain as they were. As a word of caution, it’s easy
to “accidentally” modify all rows in a table. Check your statement
carefully before executing it.


Making a Texis Owned File
~~~~~~~~~~~~~~~~~~~~~~~~~

When a file is inserted into an INDIRECT column, the ownership and
location of the file remains as it was when loaded. If the resume file
called “``/usr/local/resume/smith.res``” was owned by the Library, it
will remain so when pointed to by the INDIRECT column unless you take
steps to make it otherwise. For example, if Personnel owns the RESUME
table but not the files themselves, an attempt to update the resume
files would not be successful. The management and handling of the resume
files is still in the domain of the Library.

The system of INDIRECT data types is a system of pointers to files. The
file pointed to can either exist on the system already and remain where
it is, or you can instruct Texis to create a copy of the file under its
own ownership and control. In either case, the file still exists outside
of Texis.

Where you want Texis to own a copy of the data, a Texis owned file can
be made with the TOIND function. You can then do whatever you want with
one version without affecting the other, including removing the original
if that is appropriate. The permissions on such Texis owned files will
be the same as the ownership and permissions assigned to the Texis table
which owns it.

The file is copied into the table using an ``UPDATE`` statement. The
form of ``UPDATE`` is the same, but with special use of the expression
for the column name following SET. The form of this portion of the
``UPDATE`` statement would be:

::

         UPDATE  table-name
         SET     column-name = toind (fromfile ('local-file') ) ;

The change you are making is to the named column. With SET, you are
taking text from the file (“fromfile”) as it currently exists on the
system (“local-file”), and copying it to an INDIRECT text column
(“toind”) pointed to by the Texis table named by ``UPDATE``. The name of
the local file is in quotes, as it is a character string, and is in
parentheses as the argument of the function “``fromfile``”. The whole
“``fromfile``” function is in parentheses as the argument of the
function “``toind``”.

**Example:** To make a Texis owned copy of the Smith resume file for the
RESUME table, use this ``UPDATE`` statement:

::

         UPDATE  RESUME
         SET     EXP = toind (fromfile ('/usr/local/resume/smith.res') ) ;

The “``smith.res``” file now exists as part of the Texis table RESUME,
while still remaining outside it. Once you have made Texis owned copies
of any such files, you can operate on the text in the table without
affecting the originals. And you can decide whether it is prudent to
retain the original copies of the files or whether that would now be an
unnecessary use of space.


Deleting Records
~~~~~~~~~~~~~~~~

Records are removed from the database when they are no longer relevant
to the application. For example, if an employee leaves the company, data
concerning that person can be removed. Or if we wish to remove the data
from certain departments which are not of interest to the pay raise
analysis, we can delete those records from the temporary analysis table.

Deleting a record removes all data values in a row from a table. One or
more rows from a table can be deleted with the use of the ``DELETE``
command. This command has the following form:

::

         DELETE FROM  table-name
         [WHERE  search-condition] ;


Command Discussion
""""""""""""""""""

-  The DELETE FROM clause indicates you want to remove a row from a
   table. Following this clause, the user specifies the name of the
   table from which data is to be deleted.

-  To find the record or records being deleted, use a search condition
   similar to that used in the ``SELECT`` statement.

-  Where INDIRECT text columns are concerned, such rows will be deleted
   just as any other when DELETE FROM is used. However, the files
   pointed to by INDIRECT will only be removed where managed by Texis,
   as defined in the previous section on Texis owned files.

An employee whose ID number is 117 has quit his job. Use this statement
to delete his record from the EMPLOYEE table.

::

         DELETE FROM EMPLOYEE
         WHERE  EID = 117 ;

All records which satisfy the search condition are deleted. In this
case, one record is deleted from the table. Note that the entire record:

::

         117  Peters, Robert      SPI    DHEAD  FULL       34000

is deleted, not just the column specified in the ``WHERE`` clause.

When you delete records, aim for consistency. For example, if you intend
to delete Peters’ record in the EMPLOYEE table, you must also delete the
reference to Peters as department head in the DEPARTMENT table and so
on. This would involve two separate operations.

**Example:** Let’s say we want to delete all the department heads from
the EMP\_RAISE table as they are not really part of the analysis. Use
this statement:

::

         DELETE FROM EMP_RAISE
         WHERE  RANK = 'DHEAD' ;

The block of all records of employees who are department heads are
removed from the EMP\_RAISE table, leaving the table with just these
entries:

::

      EID  ENAME               DEPT   RANK   BENEFITS   SALARY
      101  Aster, John A.      MKT    STAFF  FULL       32000
      103  Chapman, Margaret   LIB    STAFF  PART       22000
      104  Jackson, Herbert    RND    STAFF  FULL       30000
      106  Sanchez, Carla      MKT    STAFF  FULL       35000
      107  Smith, Roberta      RND    STAFF  PART       25000

If the finance analyst wanted to empty the table of existing entries and
perhaps load in new ones from a different part of the organization, this
could be done with this statement:

::

         DELETE FROM  EMP_RAISE ;

All rows of EMP\_RAISE would be deleted, leaving an empty table.
However, the definition of the table has not been deleted; it still
exists even though it has no data values, so rows can be added to the
table at any time.

It is important to note the difference between the ``DELETE`` command
and the DROP TABLE command. In the former, you eliminate one or more
rows from the indicated table. However, the structure of the table is
still defined, and rows can be added to the table at any time. In the
case of the DROP TABLE command, the table definition is removed from the
system catalog. You have removed not only access to the data in the
table, but also access to the table itself. Thus, to add data to a
“dropped” table, you must first create the table again.


Security
--------

[Chp:Sec]

Many people have access to a database: managers, analysts, data-entry
clerks, programmers, temporary workers, and so on. Each individual or
group needs different access to the data in the database. For example,
the Finance Director needs access to salary data, while the receptionist
needs access only to names and departments. R&D needs access to the
library’s research reports, while Legal needs access to depositions in
pertinent court cases.

Texis maintains permissions which work in conjunction with the operating
system security. Texis will not change the operating system permissions
on a table, but it will change the permissions on the indices to match
those on the table.

This scheme allows the operating system to give a broad class of
security, while Texis maintains finer detail. The reason for the
combination is that Texis can not control what the user does with the
operating system, and the operating system does not have the detailed
permissions required for a database.

When a table is created it initially has full permissions for the
creator, and read/write operating system permissions for the creator
only.

When using Texis permissions the operating system can still deny access
which Texis believes is proper. To prevent this from happening Texis
should always be run as one user id, which owns the database. The
easiest way of doing this on Unix is to set the suid bit on all the
programs that form the Texis package, as well as any user programs
written with the direct library, and change the user to a common user,
for example texis. Alternative methods may exist for other operating
systems.


Creating Users and Logging In
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When a database is created there are two users created by default. The
default users are PUBLIC and \_SYSTEM. PUBLIC has the minimal
permissions possible in Texis, and \_SYSTEM has the maximum permissions
in Texis. When these are created they are created without any password.
You should change the password on \_SYSTEM to prevent security issues.
The password on PUBLIC can be left blank to allow anonymous access to
the database, or it can be set to restrict access.

When logging into Texis the username will default to PUBLIC if none is
specified. This means that tables will be created owned by PUBLIC, and
all users will have permissions to use the tables. It is possible to use
only the PUBLIC user while developing a database, although if multiple
people are working on the database you should see the previous comments
about operating system permissions.

To create new users in the database you must may either use the program
tsql -a a, and you will be prompted for the user’s information, or you
can user the CREATE USER SQL statement. You must log in as \_SYSTEM to
add or delete users.

The syntax for the user administration command in SQL are as follows:

::

    CREATE USER username IDENTIFIED BY password ;

    ALTER  USER username IDENTIFIED BY password ;

    DROP   USER username ;

You may issue the ``ALTER`` ``USER`` command if you are logged in as the
same user that is given in the statement, or if you are \_SYSTEM. The
password should be given as a string in single quotes. The ``ALTER``
``USER`` statement is used to change a user’s password; the new password
being specified in the command. Dropping a user will not remove any
tables owned by that user.


Granting Privileges
~~~~~~~~~~~~~~~~~~~

In Texis, the creator of the database would be the automatic
administrator of security. This individual can grant to other users
different powers, such as the ability to read only, to modify, or to
delete data in the database. Through the authorization subsystem, user
names and password control which users can see what data. Each user
signs onto the computer system with his or her own user name and
password (i.e., user identification) and cannot access without
permission tables created by some other user with a different user name.

The person who creates a table is considered the “owner” of the table.
Initially, that person is the only one who can access, update, and
destroy the table. The owner, however, can grant to other users the
right, or privilege, to do the following:

-  Access the tables created by the owner.

-  Add, change, or delete values in a table.

-  Grant rights the user receives from the owner to other users.

The owner of the table can grant to other users privileges that include
the following:

``SELECT``: Retrieve rows without changing values in a table.

``INSERT``: Add new rows to a table.

``UPDATE``: Change values in a table.

``DELETE``: Remove rows from a table.

The authorization subsystem of Texis is based on privileges that are
controlled by the statements GRANT and REVOKE. The GRANT command allows
the “owner” of a table to specify the operations, or privileges, that
other users may perform on a table. The format of the command is:

::

         GRANT  [ALL]
                privilege1 [,privilege2] ...
         ON     table-name1
         TO     PUBLIC
                userid1 [,userid2] ...
         [WITH GRANT OPTION] ;


Command Discussion
""""""""""""""""""

-  GRANT is a required keyword that indicates you are granting access to
   tables to other users.

-  Privilege refers to the type of privilege or privileges you are
   granting. One or more of the following privileges can be granted:
   ``SELECT``, ``INSERT``, ``UPDATE``, ``DELETE``, and ``ALTER``.
   Alternatively, ALL can be specified if all of the above actions are
   to be granted to the user.

-  ON indicates the table(s) to which these privileges are being
   assigned.

-  PUBLIC is used if the privileges are to granted to all users. If you
   want only certain users to have privileges assigned to this table,
   you must list the user identifications (“``userid's``”) of all those
   who will be allowed to share the table.

-  If the clause WITH GRANT OPTION is specified, the recipient of the
   privileges specified can grant these privileges to other users.

**Example:** The Systems Administrator in the Information Systems
Management Department created the EMPLOYEE table, and is therefore its
owner. As owner of the EMPLOYEE table, he grants the ``SELECT``
privilege to the firm’s CPA in Accounting. As owner of the table he
issues the following command:

::

         GRANT   SELECT
         ON      EMPLOYEE
         TO      CPA ;

**Syntax Notes:**

-  When the ``SELECT`` privilege is granted, it is done so with
   read-only access. Therefore the person granted the ``SELECT``
   privilege can read the data in the table, but cannot write to it, or
   in other words, cannot change it with ``UPDATE`` or other such
   privileges.

-  ON refers to the table these privileges are being granted on; in this
   case, the EMPLOYEE table.

-  What follows TO is the user ID (``userid``) of the person to whom the
   privilege is granted. In this case the ``SELECT`` privilege is
   granted to the person in accounting whose user ID is “CPA”.

**Example:** The owner of the EMPLOYEE table allows the clerks in
Personnel to add and modify employee data with this command:

::

         GRANT   UPDATE, INSERT
         ON      EMPLOYEE
         TO      CLERK1, CLERK2 ;

In this case there are two clerks with two separate user ID’s, “CLERK1”
and “CLERK2”. Both are granted privileges to ``UPDATE`` and ``INSERT``
new information into the EMPLOYEE table.

**Example:** The owner of the EMPLOYEE table, the System Administrator,
gives the Director of Personnel complete access (``SELECT``, ``INSERT``,
``UPDATE``, ``DELETE``, ``ALTER``) to the EMPLOYEE table, along with
permission to assign these privileges to others. This statement is used:

::

         GRANT   ALL
         ON      EMPLOYEE
         TO      PERS
         WITH GRANT OPTION ;

ALL following GRANT includes all 5 of the privileges. PERS is the user
ID of the Director of Personnel. WITH GRANT OPTION allows the Director
of Personnel to grant these privileges to other users.

**Example:** A systems analyst in the Strategic Planning and
Intelligence Department has created and is owner of the NEWS table in
which they are daily archiving online news articles of interest. It is
decided to give all employees read-only access to this database. Owner
of the table can do so with this command:

::

         GRANT   SELECT
         ON      NEWS
         TO      PUBLIC ;

Anyone with access to the server on which the news table is stored will
have permission to read the articles in the NEWS table, since the
``SELECT`` privilege has been granted to PUBLIC.


Removing Privileges
~~~~~~~~~~~~~~~~~~~

Privileges assigned to other users can be taken away by the person who
granted them. In Texis, the REVOKE statement would be used to remove
privileges granted by the GRANT command. The general form of this
statement is:

::

         REVOKE  [ALL]
                 privilege1 [,privilege2] ...
         ON      table-name1
         TO      PUBLIC
                 userid1 [,userid2] ... ;


Command Discussion
""""""""""""""""""

-  REVOKE is a required keyword that indicates you are removing access
   to tables .

-  Privilege refers to the type of privilege or privileges you are
   revoking. One or more of the following privileges can be revoked:
   ``SELECT``, ``INSERT``, ``UPDATE``, ``DELETE``, and ``ALTER``.
   Alternatively, ALL can be specified if all of the above actions are
   to be taken away from the user.

-  The ON clause indicates the table(s) from which these privileges are
   being removed.

-  PUBLIC is used if the privileges are taken away from all users of the
   indicated table(s). Otherwise, you list the user names of only those
   who are no longer allowed to share the table.

**Example:** The Personnel clerks no longer need to access the EMPLOYEE
table. Revoke their privileges as follows:

::

         REVOKE  UPDATE, INSERT
         ON      EMPLOYEE
         FROM    CLERK1, CLERK2 ;

This completes the discussion of security features in Texis. In the next
chapter, you will be introduced to certain other administrative features
that can be implemented in Texis.


Administration of the Database
------------------------------

[chp:AdmDB]

This chapter covers topics related to the administration of the
database. The topics include the following:

-  Accessing information about the database by using Texis’s system
   catalog.

-  Texis reserved words to avoid in naming tables and columns.


System Catalog
~~~~~~~~~~~~~~

In Texis, information about the database, such as the names of tables,
columns, and indexes, is maintained within a set of tables referred to
as the *system catalog*. Texis automatically maintains these tables in
the system catalog in response to commands issued by users. For example,
the catalog tables are updated automatically when a new table is defined
using the CREATE TABLE command.

Database administrators and end users can access data in the system
catalog just as they access data in other Texis tables by using the
``SELECT`` statement. This enables a user to inquire about data in the
database and serves as a useful reference tool when developing queries.

Table [tab:SysCat] lists the tables that make up the system catalog for
Texis.

[tab:SysCat]

+--------------------+---------------------------------------------------------+
| Table Name         | Description                                             |
+====================+=========================================================+
| ``SYSTABLES``      | Contains one row per table in the database.             |
+--------------------+---------------------------------------------------------+
| ``SYSCOLUMNS``     | Contains one row per column for each database table.    |
+--------------------+---------------------------------------------------------+
| ``SYSINDEX``       | Contains one row per canonical index in the database.   |
+--------------------+---------------------------------------------------------+
| ``SYSPERMS``       | Holds the permissions information.                      |
+--------------------+---------------------------------------------------------+
| ``SYSUSERS``       | Contains information about users known to the system.   |
+--------------------+---------------------------------------------------------+
| ``SYSTRIG``        | Contains one row per trigger defined to the system.     |
+--------------------+---------------------------------------------------------+
| ``SYSMETAINDEX``   | Contains one row per Metamorph index in the database.   |
+--------------------+---------------------------------------------------------+

Table: Overview of System Catalog Tables in Texis

One commonly referenced table, SYSTABLES, contains a row for each table
that has been defined. For each table, the name of the table, authorized
ID of the user who created the table, type of table, and so on is
maintained. When users access SYSTABLES, they see data pertaining to
tables that they can access.

Texis’s system catalog table, “SYSTABLES” has these columns, defined
with the following data types:

::

         NAME     -  CHAR(20)
         TYPE     -  CHAR
         WHAT     -  CHAR(255)
         FC       -  BYTE
         CREATOR  -  CHAR(20)
         REMARK   -  CHAR(80)

Each field is fixed length rather than variable length, so the
designated size limits do apply.

NAME
    is the name of the table. Each of the tables comprising the system
    catalog are entered here, as well as each of the other database
    relations existing as “normal” tables.

TYPE
    indicates the type of table.

    S
        indicates a System table, and is Texis owned. ‘S’ is assigned to
        all tables where the user who created the table is “texis”.

    T
        indicates a normal Table.

    V
        indicates a normal View.

    B
        indicates a Btree table. A Btree is a special type of table that
        can be created through the API only, that contains all the data
        in the index. It is of limited special purpose use. It is
        somewhat quicker and more space efficient if you have a few,
        small fields, and if you will never need to index on the fields
        in a different order. Use of the API is covered in
        Part V, Chapter [Part:V:Chp:Embed].

    t
        indicates a temporary table. These are not directly accessible,
        and exist only briefly. They are used when a temporary table is
        needed by the system – for example when compacting a table – and
        may have the same name as another, normal table. They are
        automatically removed when no longer needed.

    D
        indicates a Deleted table. On some operating systems (such as
        Windows), when a table is ``DROP``\ ped, it cannot be removed
        immediately and must continue to exist – as a deleted table –
        for a short time. Deleted tables are not directly accessible,
        and are automatically removed as soon as possible.

WHAT
    is the filename designating where the table actually exists on the
    system.

FC
    stands for Field Count. It shows how many columns have been defined
    for each table entered.

CREATOR
    is a User ID and shows who created the table.

REMARK
    is reserved for any explanatory comments regarding the table.

**Example:** Provide a list of all tables in the database with this
statement:

::

         SELECT  NAME, TYPE
         FROM    SYSTABLES ;

The result will be a listing of the available tables, as follows:

::

      NAME             TYPE

      SYSCOLUMNS       S
      SYSINDEX         S
      SYSMETAINDEX     S
      SYSTABLES        S
      CODES            T
      DEPARTMENT       T
      EMPLOYEE         T
      NEWS             T
      REPORT           T
      RESUME           T

In the above example, the first four tables: SYSCOLUMNS, SYSINDEX,
SYSMETAINDEX, and SYSTABLES, comprise the system catalog and are marked
as type S, for “*system*”.

The next six in the list are the tables which have been used for
examples throughout this manual: CODES, DEPARTMENT, EMPLOYEE, NEWS,
REPORT, and RESUME. These are marked as type T, for “*table*”.

The table SYSCOLUMNS contains a row for every column of every table in
the database. For each column, its name, name of the table to which it
belongs, data type, length, position in the table, and whether NULL is
permitted in the columns is maintained information. Users querying
SYSCOLUMNS can retrieve data on columns in tables to which they have
access.

Texis’s system catalog table “SYSCOLUMNS” has these columns, defined
with the following data types:

::

         NAME     -  CHAR(20)
         TBNAME   -  CHAR(20)
         TYPE     -  CHAR(15)
         INDEX    -  CHAR(20)
         NONNULL  -  BYTE
         REMARK   -  CHAR(80)

NAME
    is the column name itself.

TBNAME
    is the table the column is in.

TYPE
    is the data type assigned to the column, defined as a string. TYPE
    might contain “char”, “varchar”, “integer”, “indirect”, and so on.

INDEX
    is the name of an index created on this column. (This field is
    reserved for use in future versions of Texis. As it is not currently
    being used, one should not be surprised if the INDEX field is
    empty.)

NONNULL
    indicates whether NULL fields should be disallowed. (This field is
    reserved for use in future versions of Texis. As it is not currently
    being used, one should not be surprised if the INDEX field is
    empty.)

REMARK
    is reserved for any user comment about the column.

**Example:** A user wants to obtain data about employees in the R&D
Department, but doesn’t know any of the column names in the EMPLOYEE
table. Assume that the user does know there is a table named EMPLOYEE.

This statement:

::

         SELECT  NAME
         FROM    SYSCOLUMNS
         WHERE   TBNAME = 'EMPLOYEE' ;

would result in the following:

::

      NAME

      EID
      ENAME
      DEPT
      RANK
      BENEFITS
      SALARY

In this way one can find out what kind of data is stored, so as to
better formulate queries which will reveal what you actually want to
know.

Texis has two other system catalog tables called “SYSINDEX” and
“SYSMETAINDEX”. Texis’s system catalog table “SYSINDEX” has these
columns, defined with the following data types:

::

         NAME     -  CHAR(20)
         TBNAME   -  CHAR(20)
         FNAME    -  CHAR(20)
         ORDER    -  CHAR
         TYPE     -  BYTE
         UNIQUE   -  BYTE
         FIELDS   -  CHAR(20)

NAME
    is the name of the index.

TBNAME
    is the table the index is on.

FNAME
    is the file name of the index.

ORDER
    indicates sort order. ‘A’ indicates *ascending*; ‘D’ indicates
    *descending*. This field is not currently used, but is planned for
    future releases.

TYPE
    indicates the type of index, either Btree or Metamorph.

UNIQUE
    indicates whether the values entered should be unique. This field is
    not currently used, but is planned for future releases.

FIELDS
    indicates which field is indexed.

“SYSMETAINDEX” controls a demon that checks Metamorph indexes, those
indexes used on text oriented columns. The demon waits a certain number
of seconds between checks, and has a threshold in bytes at which size
the update process is required to run.

Texis’s system catalog table “SYSMETAINDEX” has these columns, defined
with the following data types:

::

         NAME     -  CHAR(20)
         WAIT     -  INTEGER
         THRESH   -  INTEGER

NAME
    is the name of the Metamorph index.

WAIT
    indicates how long to wait in seconds between index checks.

THRESH
    is a number of bytes which have changed. This is the threshold
    required to re-index.

The system catalog tables are a good place to start when initially
becoming familiar with what a database has to offer.


Optimization
~~~~~~~~~~~~


Table Compaction
""""""""""""""""

After a table has been extensively modified, its disk file(s) may
accumulate a certain amount of unused free space, especially if a large
number of rows have been deleted. This free space will be re-used as
much as possible whenever new rows are inserted or updated, to try to
avoid expanding the table’s disk footprint. However, if the table is no
longer to be modified in the future – e.g. it is now a search-only
archive – this free space will never be reclaimed. It is now wasted disk
space, as well as a potential performance impairment, as larger seeks
may be needed by the operating system to access actual payload data.

Free space in a table may be reclaimed by compacting the table
(retaining all payload data), with the following SQL:

``ALTER`` ``TABLE`` :math:`name` ``COMPACT``

This will compact the table :math:`name` to eliminate its free space.
The process may take some time for a large table, or where there are
many indexes on it. Also, while the end result will generally be less
disk usage for the table, *during* the compaction disk usage will
temporarily increase, as copies of the table and most of its index files
are created. Therefore, before starting, ensure that there is free disk
space (in the database’s partition) at least equal to the combined size
of the table and its indexes.

Because extensive modifications are needed, the table will not be
modifiable during compaction: attempts to insert, delete or update rows
will block until compaction is finished. The table is readable during
compaction, however, so ``SELECT``\ s are possible. Progress meters may
be printed during compaction by setting the SQL property ``meter`` to
``'compact'``. The ``ALTER`` ``TABLE`` :math:`name` ``COMPACT`` syntax
was added in version 6.00.1291080000 20101129. **NOTE: Versions prior to
version 6.00.1291080000 20101129 should not attempt to access the table
during compaction, or corruption may result.**

Note that compacting a table is generally only useful when the table
will no longer be modified, or has undergone a large amount of deletions
that will not be replaced by inserts. Conversely, a “steady-state”
continuously-modified table rarely benefits from compaction, because it
will merely accumulate free space again: the short-term gains of
compaction are outweighed by the significant cost and delay of
repeatedly runnning the compaction.


Index Maintenance
"""""""""""""""""

B-tree (regular) and inverted indexes never require explicit
optimization by the database administrator, as they are automatically
kept up-to-date (optimized) at every table modification (``INSERT``,
``DELETE`` or ``UPDATE``).

However, this is not possible for Metamorph indexes due to their
fundamentally different nature. Instead, table changes are logged for
incorporation into the index at the next optimization (index update),
and Texis must linearly search the changed data until then. Thus, the
more a table has been modified since a Metamorph index’s last
optimization, the more its search performance potentially degrades. When
the index is re-optimized, those changes are indexed and merged into the
Metamorph index, restoring its performance. A Metamorph index may be
optimized in one of several ways, as follows.


Manual Index Optimization via CREATE METAMORPH INDEX
""""""""""""""""""""""""""""""""""""""""""""""""""""

A Metamorph index may be optimized manually simply by re-issuing the
same ``CREATE`` ``METAMORPH``
:math:`[`\ ``INVERTED``\ :math:`|`\ ``COUNTER``\ :math:`]` ``INDEX``
:math:`...` statement that was used to create it. Instead of producing
an error noting that the index already exists – as would happen with
regular or inverted indexes – the Metamorph index is re-optimized. (If
the index is already fully optimized, the statement returns success
immediately.)

Note that the ALTER INDEX statement (p. ) is an easier method of
optimizing indexes.


Manual Index Optimization via ALTER INDEX
"""""""""""""""""""""""""""""""""""""""""

Since the full syntax of the original ``CREATE`` statement may not be
known, or may be cumbersome to remember and re-enter, a Metamorph index
may also be optimized with an ``ALTER INDEX`` statement:

| ``     ALTER INDEX`` :math:`indexName`\ :math:`|`\ ``ALL`` [``ON``
  :math:`tableName`]
| ``         OPTIMIZE``\ :math:`|`\ ``REBUILD``

This will optimize the index named :math:`indexName`, or all indexes in
the database if ``ALL`` is given. Adding the optional ``ON``
:math:`tableName` clause will limit the index(es) optimized to only
those on the table named :math:`tableName`. If a non-Metamorph index is
specified, it will be silently ignored, as non-Metamorph indexes are
always in an optimized state.

If the keyword ``REBUILD`` is given instead of ``OPTIMIZE``, the index
is rebuilt from scratch instead. This usually takes more time, as it is
the same action as the initial creation of the index, and thus the whole
table must be indexed, not just changes since last optimization. Any
index type may be rebuilt, not just Metamorph indexes. During
rebuilding, the original index is still available for search use;
however inserts, deletes and updates may be postponed until the rebuild
completes. Rebuilding is not generally needed, but may be useful if the
index is suspected to be corrupt. The ``ALTER INDEX`` syntax was added
in Texis version 7.


Automatic Index Optimization via chkind
"""""""""""""""""""""""""""""""""""""""

Another method of Metamorph index optimization is automatically, via the
``chkind`` daemon, and is enabled by default. This is a process that
runs automatically in the background (as part of the database monitor),
and periodically checks how out-of-date Metamorph indexes are. When an
index reaches a certain (configurable) threshold of “staleness”, it is
re-optimized. See p.  for more details on ``chkind`` and its
configuration.


Choosing Manual vs. Automatic Index Optimization
""""""""""""""""""""""""""""""""""""""""""""""""

Whether to optimize Metamorph indexes manually (via a SQL statement) or
automatically (via ``chkind``) depends on the nature of table changes
and searches.

Deployments where table changes occur in batches, and/or search load
predictably ebbs and flows, are good candidates for manual optimization.
The optimizations can be scheduled for just after the batch table
updates, and if possible when search load is low. This will keep the
index(es) up-to-date (and thus performing best) for the longest amount
of time, while also avoiding the performance penalty of updating both
the table and the index simultaneously. Optimizing at off-peak search
times also improves peak-load search performance by freeing up resources
during the peak. Contrast this with automatic optimization, which cannot
know about upcoming table updates or search load, and thus might trigger
an index update that coincides with either, negatively impacting
performance.

Applications where tables are changed at a more constant rate (e.g. a
steady stream of changes) may be better candidates for automatic
updating. There may not be any predictable “best time” to run the
optimization, nor may it be known how much the indexes are out-of-date.
Thus the decision on when to optimize can be left to ``chkind``\ ’s
automatic out-of-date scan, which attempts to minimize both staleness of
the index and frequency of index optimizations.

Some situation may call for a combination, e.g. ``chkind`` to handle
miscellaneous table updates, and an occasional manual optimization after
batch updates, or just before peak search load.


Reserved Words
~~~~~~~~~~~~~~

The following words are reserved words in SQL. Texis makes use of many
of them, and future versions may make use of others. These words should
not be used as ordinary identifiers in forming names.

Allowances will be made in future versions of Texis so that the words
may be used as delimited identifiers if deemed vital, by enclosing them
between double quotation marks.

::

    ADA                 DELETE              INTO                REFERENCES
    ADD                 DESC                IS                  REVOKE
    ALL                 DESCRIPTOR          KEY                 ROLLBACK
    ALTER               DISTINCT            LANGUAGE            SCHEMA
    AND                 DOUBLE              LIKE                SECQTY
    ANY                 DROP                LIKE3               SELECT
    AS                  EDITPROC            LOCKSIZE            SET
    ASC                 END-EXEC            MATCHES             SMALLINT
    AUTHORIZATION       ERASE               MAX                 SOME
    AVG                 ESCAPE              METAMORPH           SQLCODE
    BETWEEN             EXECUTE             MIN                 STOGROUP
    BLOB                EXISTS              MODULE              SUM
    BUFFERPOOL          FETCH               NOT                 SYNONYM
    BY                  FIELDPROC           NULL                TABLE
    C                   FLOAT               NUMERIC             TABLESPACE
    CHAR(ACTER)?        FOR                 NUMPARTS            TO
    CHECK               FOREIGN             OF                  UNION
    CLOSE               FORTRAN             ON                  UNIQUE
    CLUSTER             FOUND               OPEN                UPDATE
    COBOL               FROM                OPTION              USER
    COLUMN              GO                  OR                  USING
    COMMIT              GO[ \t]*TO          ORDER               VALIDPROC
    COMPACT             GOTO                PART                VALUES
    CONTINUE            GRANT               PASCAL              VARCHAR
    COUNT               GROUP               PLAN                VCAT
    CREATE              HAVING              PLI                 VIEW
    CTIME               IMMEDIATE           PRECISION           VOLUMES
    CURRENT             IN                  PRIMARY             WHENEVER
    CURSOR              INDEX               PRIQTY              WHERE
    DATABASE            INDICATOR           PRIVILEGES          WITH
    DATE                INDIRECT            PROCEDURE           WORK
    DECIMAL             INSERT              PUBLIC
    DECLARE             INT(EGER)?          REAL
    DEFAULT
