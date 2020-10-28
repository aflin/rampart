
Preface
-------

Acknowledgment
~~~~~~~~~~~~~~
The developers of Rampart are indebted to Thunderstone, LLC for the
decades of development it took to create the Texis library.

Documentation Caveat
~~~~~~~~~~~~~~~~~~~~
As the Texis library encompasses more functionality than is currently
used in Rampart, some settings and procedures may not be directly
applicable in the context of its use from within a javascript program.
We include the nearly all of it here for completeness and as it may
provide a deeper understanding of philosophy behind the library's 
operation.


Javascript Function
-------------------

Loading the Module
~~~~~~~~~~~~~~~~~~

Loading of the sql module from within Rampart javascript is a simple matter
of using the ``require`` statement:

.. code-block:: javascript

    var Sql=require("rampart-sql");

Return value:

.. code-block:: none

    {
        init:         {_func:true},
        singleUser:   {_func:true},
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

The returned object contains functions that can be split into two groups:
Database functions and String functions.

Database Functions
~~~~~~~~~~~~~~~~~~

.. _initconst:

init() constructor
""""""""""""""""""

The ``init`` constructor function takes a path (and an optional boolean) as 
parameters and returns a new connection to the specified database.

.. code-block:: javascript

    var sql = new Sql.init(dbpath [,create]);

+--------+------------+---------------------------------------------------+
|Argument|Value       |Meaning                                            |
+========+============+===================================================+
|dbpath  |String      | The path to the directory containing the database |
+--------+------------+---------------------------------------------------+
|create  |Boolean     | if true, and the directory does not exist, the    |
|        |            | directory and a new database will be created.     |
+--------+------------+---------------------------------------------------+

Return Value:

.. code-block:: none

    {
        exec:  {_func:true},
        eval:  {_func:true},
        set:   {_func:true},
        close: {_func:true}
    }
    
Example:

.. code-block:: javascript
    
	var Sql = require("rampart-sql");

	/* create database if it does not exist */
	var sql = new Sql.init("/path/to/my/db", true);


exec()
""""""

The exec function executes a sql statement on the database opened with
:ref:`init() <initconst>`.  It takes a string containing a sql statement and an optional
array, object and/or function.  They may be specified in any order.

.. code-block:: javascript

    var res = sql.exec(statement [, parameters, options, callback])

+--------+------------------+---------------------------------------------------+
|Argument|Value             |Meaning                                            |
+==============+============+===================================================+
|statement     |String      | The sql statement                                 |
+--------------+------------+---------------------------------------------------+
|parameters    |Array       | `?` substitution parameters                       |
+--------------+------------+---------------------------------------------------+
|options       |object      | Options (skip, max, returnType, includeCounts)    |
|              |            | *described below*                                 |
+--------------+------------+---------------------------------------------------+
|callback      |function    | a function to handle data one row at a time.      |
+--------------+------------+---------------------------------------------------+

.. _returnval:

Return Value:
	With no options or callback, an object is returned.  The object contains
	two or three key/value pairs.  
	
	The first is ``results``: an array of objects.  Each
	object will have a key value equal to the corresponding column name 
	and the value equal to the corresponding field of the retrieved row.
	
	The second is ``rowCount``: a number corresponding to the number of rows
	returned.

	The third possible value is ``countInfo`` (if option ``includeCounts``
	is set ``true``).  See description :ref:`below <execopts>`.

	If a callback function is specified, the number of rows fetched is
	returned.

	Other return values are discussed under `Options` below.

Statement:
    A statement is a string containing a single sql statement to be executed. A trailing
    ``;`` (semicolon) is optional. Example:

.. code-block:: javascript

    var res = sql.exec(
    	"select * from employees where Salary > 50000 and Start_date < '2018-12-31'"
    );

Note that you must use multiple ``exec()`` for each statement to be executed.

Parameters:
    Parameters are contained in array and correspond to ``?`` in the sql
    statement.  Example:

.. code-block:: javascript

    var res = sql.exec(
    	"select * from employees where Salary > ? and Start-date ?",
    	[50000,"2018-12-31"]
    );

The use of Parameters can make the handling of user input safe from sql injection.
Note that if there is only one parameter, it still must be contained in an
array.

.. _execopts:

Options:
    The ``options`` object contains the following:

   * ``max`` (number):  maximum number of rows to return (default: 10).
   * ``skip`` (number): the number of rows to skip (default: 0)
   * ``returnType`` (string): Determines the format of the ``results`` value
     in the return object.

      * default: an array of objects as described :ref:`above <returnval>`.
      * ``array``: an array of arrays. The outer array corresponds to
        each row fetched. The inner array corresponds to the fields
        returned in each row.  No information regarding column names
        is returned.

      * ``arrayh``: an array of arrays.  Same as ``array`` except that
        the first row will contain the names of the columns. Actual
        results begin in the second outer array member.

      * ``novars``: an empty array is returned.  The sql statement is
        still executed.  This may be useful for updates and deletes
        where the return value would otherwise not be used.

   * ``includeCounts`` (boolean): whether to include count information in the return object.
     The information will be returned as an object in the return object
     with the key ``countInfo``.  The numbers returned will only be
     useful when performing an :ref:`text search <sql3:Intelligent Text Search Queries>`.
     The ``countInfo`` contains the following:

      * ``indexCount`` (number): a single value estimating the number
        of matching rows.

      * ``rowsMatchedMin`` (number): Minimum number of rows matched *before* 
        any ``group by``, likeprows, aggregates or multivaluetomultirow
        are applied

      * ``rowsMatchedMax`` (number): Maximum number of rows matched *before* 
        any ``group by``, likeprows, aggregates or multivaluetomultirow
        are applied

      * ``rowsReturnedMin`` (number): Minimum number of rows matched *after* 
        any ``group by``, likeprows, aggregates or multivaluetomultirow
        are applied

      * ``rowsReturnedMax`` (number): Maximum number of rows matched *after* 
        any ``group by``, likeprows, aggregates or multivaluetomultirow
        are applied

Callback:
   A function taking as parameters (``result``, ``index`` [``countInfo``]).
   The callback is executed once for each row retrieved:

   * ``result``: (array/object): depending on the setting of ``returnType``
     in ``Options`` above, a single row is passed to the callback as a an
     object or an array.

   * ``index``: The ordinal number of this search result.  If ``arrayh`` is
     used, the first ``index value`` (where ``results`` contains the column
     names) will be ``-1``.

   * ``countInfo``: an object as described above in :ref:`Options <execopts>` if the
     ``includeCounts`` option is set ``true``.  Otherwise it will be
     ``undefined``. 

Introduction to Texis Sql
-------------------------


Texis: Thunderstone’s Text Information Server
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

What is it?
"""""""""""
Texis is a relational database server that specializes in managing
textual information. It has many of the same abilities as products like
Oracle(r), Informix(r), and Sybase(r) with one key difference: it can
intelligently search and manage databases that have natural language
text in them, which is something they can’t do.

Why is that different?
""""""""""""""""""""""
Most other products are capable of storing a limited field that contains
text, and usually that information is limited to being less than 500
characters long. And when it comes to finding something in that field
you’re limited to being able to search for a single word or string of
characters.
In Texis you can store text of any size, and you’re able to query that
information in natural language for just about anything you can imagine.
We took our powerful Metamorph concept based text engine and built a
specialized relational database server around it so that you have the
best of both worlds.

What can we do with it?
"""""""""""""""""""""""
Look at your current info-system and for the occurrence of anything that
contains natural language information. This includes things like:
e-mail, personnel records, research reports, memos, faxes, product
descriptions, and word processing documents. Now imagine being able to
collect and perform queries against these items as if they were a
traditional database.
If you look closely enough you’ll probably discover that about half the
information that is resident within the organization has natural
language or text as one of its most significant attributes. And chances
are that there is little or no ability to manage and query these
information resources based on their content. Traditional databases are
fine as long as you are just adding up numbers or manipulating
inventories, but people use language to communicate and no product
except Texis can provide real access to the “natural language”
components.

Features Unique to Texis
""""""""""""""""""""""""
Before we give you the specifications, we need to explain some features
that are unique to Texis.

Zero Latency Insert
"""""""""""""""""""
When a record is added or updated within a Texis table it is available
for retrieval immediately. With any other product you would have to wait
until the record was “indexed” before it would become available. The
reason for this follows.

Variable Sized Records
""""""""""""""""""""""
Most databases allocate disk space in fixed size blocks that each
contain a fixed number of records. The space allocated to each record is
the maximum size that that record definition allows.
For example, let’s say you have a table that contains a 6 digit fixed
length part number, and a variable length part description that could be
up to 1000 characters long. Under the definition above, a 10,000 record
database would require at least 10,060,000 bytes of disk space.
But let’s look a little closer at the facts behind our table. Our
*maximum* part description is 1000 bytes, but that’s only because we
have a few parts with really long winded descriptions; most of our part
descriptions have an average length of about 100 characters. This is
where Texis comes in; it only stores what you use, not what you might
use. This means that our table would only require about 1 MB of disk
space instead of 10 Mbytes.
But wait, it doesn’t stop there! We have another hat trick. Because we
store our records in this manner we also remove the limitation of having
to specify the maximum length of a variable length field. In Texis any
variable field can contain up to a Gigabyte. (Not that we recommend 1
Gig fields.)

Indirect Fields
"""""""""""""""
Indirect fields are byte fields that exist as real files within the file
system. This field type is usually used when you are creating a database
that is managing a collection of files on the server (like word
processing files for instance). They can also be used when the 1 Gig
limitation of fields is too small.
You may use indirect fields to point to your files anywhere on your file
system or you may let Texis manage them under the database.
Since files may contain any amount of any kind of data indirect fields
may be used to store arbitrarily large binary objects. These Binary
Large OBjects are often called BLOBs in other RDBMSes.
However in Texis the ``indirect`` type is distinct from
``blob``/``blobz``. While each ``indirect`` field is a separate external
file, all of a table’s ``blob``/``blobz`` fields are stored together in
one ``.blb`` file adjacent to the ``.tbl`` file. Thus, ``indirect`` is
better suited to externally-managed files, or data in which nearly every
row’s field value is very large. The ``blob`` (or compressed ``blobz``) 
type is better suited to data that may often be either large or small, 
or which Texis can manage more easily (e.g. faster access, and 
automatically track changes for index updates).

Variable Length Index Keys
""""""""""""""""""""""""""
Traditional database systems allocate their indexes in fixed blocks, and
so do we; but we were faced with a problem. Typical English language
contains words of extremely variant length, and we wanted to minimize
the overhead of storing these words in an index. Traditional Btrees have
fixed length keys, so we invented a variable length key Btree in order
to minimize our overhead while not limiting the maximum length of a key.

Why all the variable stuff?
"""""""""""""""""""""""""""
Texis stands for Text Information Server, and text databases are really
different in nature to the content of most standard databases. Have a
look at the word processing files that are on your machine. The vast
majority are probably relatively small, but then there’s an occasional
whopper. The same is true for the content of most documents: their size
and content are extremely variable.
Texis is optimized for two things: Query time and variable sized data.

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
| Max table size                  | 2 gigabytes (``2^31``) on 32-bit systems, 9 exabytes (``2^63``) on 64-bit systems   |
+---------------------------------+-------------------------------------------------------------------------------------+
| Rows per table                  | 1 billion                                                                           |
+---------------------------------+-------------------------------------------------------------------------------------+
| Columns per table               | Unlimited                                                                           |
+---------------------------------+-------------------------------------------------------------------------------------+
| Indexes per table               | Unlimited                                                                           |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max field size                  | 1 gigabyte                                                                          |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max field name                  | 32 characters                                                                       |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max tables per query            | 400                                                                                 |
+---------------------------------+-------------------------------------------------------------------------------------+
| User password security          | Yes                                                                                 |
+---------------------------------+-------------------------------------------------------------------------------------+
| Group password security         | Yes                                                                                 |
+---------------------------------+-------------------------------------------------------------------------------------+
| Index types                     | Btree, Inverted, Text, Text inverted                                                |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max index key size              | 8192                                                                                |
+---------------------------------+-------------------------------------------------------------------------------------+
| Standard Data Types             |                                                                                     |
+---------------------------------+-------------------------------------------------------------------------------------+
| Max user defined data types     | 64                                                                                  |
+---------------------------------+-------------------------------------------------------------------------------------+


Texis as a Relational Database Management System
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Texis is a database management system (DBMS) which follows the
relational database model, while including methods for addressing the
inclusion of large quantities of narrative full text. Texis provides a
method for managing and manipulating an organization’s shared data,
where intelligent text retrieval is harnessed as a qualifying action for
selecting the desired information.
Texis serves as an “intelligent agent” between the database and the
people seeking data from the database, providing an environment where it
is convenient and efficient to retrieve information from and store data
in the database. Texis provides for the definition of the database and
for data storage. Through security, backup and recovery, and other
services, Texis protects the stored data.
At the same time Texis provides methods for integrating advanced full
text retrieval techniques and object manipulation with the more
traditional roles performed by the RDBMS (relational database management
system).

Relational Database Background
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Texis, as most recent DBMSs, is based on the relational data model. The
fundamental organizational structure for data in the relational model is
the relation. A *relation* is a two-dimensional table made up of rows
and columns. Each relation, also called a table, stores data about
*entities*. These entities are objects or events on which an
organization chooses to collect data. Patients, doctors, services, and
insurance carriers are examples of entities.
The columns in a relation represent characteristics (*attributes*,
*fields*, or *data items* of an entity, such as patient identification
number, patient name, address, etc. The rows (called *tuples* in
relational jargon) in the relation represent specific occurrences (or
records) of a patient, doctor, insurance group number, service rendered,
etc. Each row consists of a sequence of values, one for each column in
the table.
In addition, each row (or record) in a table must be unique. The
*primary key* of a relation is the attribute or attributes whose value
uniquely identifies a specific row in a relation. For example, a Patient
identification number (ID) is normally used as a primary key for
accessing a patient’s hospital records. A Customer ID number can be the
primary key in a business.
Over the years, many different sets of terms have been used
interchangeably when discussing the relational model.
The following table lists these terms and shows their relationship.

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

An important characteristic of the relational model is that records
stored in one table can be related to records stored in other tables by
matching common data values from the different tables. Thus data in
different relations can be tied together, or integrated. For example, in
the above figure, invoice 71115 in the INVOICE relation is related
to Patient 112, Frazier, in the Patient relation because they both have
the same patient ID. Invoices 71118, 71121, and 71123 are also related
to Patient 112.
A database in the relational model is made up of a collection of
interrelated relations. Each relation represents data (to the users of
the database) as a two-dimensional table. The terms *relation* and
*table* are interchangeable. For the remainder of the text, the term
*table* will be used when referring to a relation.
Access to data in the database is accomplished in two ways. The first
way is by writing application programs written in procedural languages
such as C that add, modify, delete, and retrieve data from the database.
These functions are performed by issuing requests to the DBMS. The
second method of accessing data is accomplished by issuing commands, or
queries, in a fourth-generation language (4GL) directly to the DBMS to
find certain data. This language is called a *query language*, which is
a nonprocedural language characterized by high-level English-like
commands such as ``UPDATE``, ``DELETE``, ``SELECT``, etc. Structured
Query Language (SQL, also pronounced “Sequel”) is an example of a
nonprocedural query language.

Support of SQL
~~~~~~~~~~~~~~
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
catalog is discussed in more detail in Chapter [chp:AdmDB].
As shown in Figure [fig:CrTab], the CREATE TABLE command results in an
empty table.
[fig:CrTab]
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

Data Types:

    Each column within a table can store only one type of data. For
    example, a column of names represents *character* data, a column
    storing units sold represents *integer* data, and a column of file
    dates represents *time* data. In Texis, each column name defined in
    the CREATE TABLE statement has a data type declared with it. These
    data types include *character*, *byte*, *integer*, *smallint*,
    *float*, *double*, *date*, *varchar*, *counter*, *strlst*, and
    *indirect*. Table [tab:DTypes] illustrates the general format for
    each data type. A description of each of the Data Types listed in
    Table [tab:DTypes] follows.

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
        Used to store text field information entirely in Texis. The
        specified length is offered as a suggestion only, as this data
        type can hold an unlimited number of characters. In the example
        in Table [tab:DTypes], there may be a short description of the
        text, or a relatively small abstract which is stored in the
        field of the column itself.
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
        useful if you are creating a METAMORPH INVERTED index, and do
        not allow post processing, and do not display the actual
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
        This allows the field to be compared with times, to find all
        records inserted before a particular time for example.
    STRLST:
        A string list is used to hold a number of different strings. The
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
    fixed length forms of CHAR, INTEGER, FLOAT and so on are used, there
    is a corresponding variable length data type which can be used when
    appropriate, such as is represented in VARCHAR.
    The length following CHAR, as in ``CHAR(100)``, indicates that 100
    is the maximum number of allowed characters. Each record with such a
    data type defined will have a size of 100 characters, regardless of
    whether 3 characters, 57 characters, or even a NULL value is
    entered. The length following VARCHAR, as in ``VARCHAR(100)``,
    indicates that 100 characters is a suggested length. If an entry of
    350 characters is required in this field, VARCHAR can make
    allowances to handle it.
    The 100 character suggestion in this case is used for memory
    allocation, rather than field length limitation. Therefore a VARCHAR
    length should be entered as the average, rather than the largest
    size for that field. Entering an extremely large length to
    accommodate one or two unusual entries would impair the handling of
    memory for normal operations.
    The sophisticated aspects of database design involving choice and
    use of data types towards performance and optimization of table
    manipulation are addressed in more depth in Chapter [chp:AdmDB],
    *Administration of the Database*.
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
