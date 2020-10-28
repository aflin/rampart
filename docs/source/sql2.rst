
Queries Involving Calculated Values
-----------------------------------

While Texis focuses on manipulation of textual information, data can
also be operated on numerically. Queries can be constructed which
combine calculated values with text search.
To illustrate the material in this chapter, we’ll use an employee table
which the Personnel Department keeps to manage salaries and benefits. A
sampling of the data stored in this table follows:
::
      EID  ENAME               DEPT   SALARY   BENEFITS
      101  Aster, John A.      MKT    32000    FULL
      102  Barrington, Kyle    MGT    45000    FULL
      103  Chapman, Margaret   LIB    22000    PART
      104  Jackson, Herbert    RND    30000    FULL
      105  Price, Stella       FIN    42000    FULL
      106  Sanchez, Carla      MKT    35000    FULL
      107  Smith, Roberta      RND    25000    PART

Arithmetic Calculations
~~~~~~~~~~~~~~~~~~~~~~~
This section covers the computational features of Texis. They are
adequate to allow the user to perform computations on the data and/or
retrieve rows based on conditions involving computations. For example,
you can adjust salaries for a 5 percent across-the-board increase, or
you can compute weekly salaries (i.e., salary divided by 52).
Arithmetic calculations are performed on fields, or columns, in the
database. An *arithmetic expression* is used to describe the desired
computation. The expression consists of column names and numeric
constants connected by parentheses and arithmetic operators.
The following table shows the arithmetic operators used in Texis.

+------------------------+------------------+---------------------+
| Arithmetic Operation   | Texis Operator   | Example             |
+========================+==================+=====================+
| Addition               | ``+``            | ``SALARY + 2000``   |
+------------------------+------------------+---------------------+
| Subtraction            | ``-``            | ``SALARY - 1000``   |
+------------------------+------------------+---------------------+
| Multiplication         | ``*``            | ``SALARY * 1.05``   |
+------------------------+------------------+---------------------+
| Division               | ``/``            | ``SALARY / 26``     |
+------------------------+------------------+---------------------+
Table: Arithmetic Operators Supported in Texis

Typically, the arithmetic expression is used in the ``SELECT`` clause to
perform calculations on data stored in the table.
**Example:** Next year every employee will receive a 5 percent salary
increase. List the names of each employee, his or her current salary,
and next year’s salary.
Enter this statement:
::
         SELECT  ENAME, SALARY, SALARY * 1.05
         FROM    EMPLOYEE ;
Where “``SALARY * 1.05``” is the arithmetic expression.
The results are:
::
      ENAME               SALARY     SALARY * 1.05
      Aster, John A.      32000      33600
      Barrington, Kyle    45000      47250
      Chapman, Margaret   22000      23100
      Jackson, Herbert    30000      31500
      Price, Stella       42000      44100
      Sanchez, Carla      35000      36750
      Smith, Roberta      25000      26250
The expression “``SALARY * 1.05``” results in each value in the salary
column being multiplied by 1.05. The results are then displayed in a new
column that is labeled ``SALARY * 1.05``.
If more than one arithmetic operator is used in an arithmetic
expression, parentheses can be used to control the order in which the
arithmetic calculations are performed. The operations enclosed in
parentheses are computed before operations that are not enclosed in
parentheses. For example, the expression:
::
         12 * (SALARY + BONUS)
means bonus is added to salary, and then this result is multiplied by
12.
If parentheses are omitted or if several operations are included within
the parentheses, the order in which calculations are performed is as
follows:
#. First, all multiplication, division and modulo [1]_ operations are
   performed.
#. Then, all addition and subtraction operations are performed.
For example, in the expression:
::
         SALARY + SALARY * .05
the value in the SALARY column is multiplied by .05, and then the salary
value is added to this intermediate result.
When two or more computations in an expression are at the same level
(e.g., multiplication and division), the operations are executed from
left to right. For example, in the expression:
::
         SALARY / 12 * 1.05
the salary value is first divided by 12, and then this result is
multiplied by 1.05.
Arithmetic calculation can also be used in a ``WHERE`` clause to select
rows based on a calculated condition. In addition, arithmetic
expressions can be used in the HAVING and ORDER BY clauses, which will
be discussed in later sections of this chapter.
**Example:** List the names of all employees earning a monthly salary
above $3000.
This query:
::
         SELECT  ENAME
         FROM    EMPLOYEE
         WHERE   (SALARY/12) > 3000 ;
results in:
::
      ENAME
      Barrington, Kyle
      Price, Stella
The rows in the ``EMPLOYEE`` table are retrieved if the condition
“salary divided by 12” is greater than $3000. This was true only for
Barrington and for Price, whose annual salaries (respectively $45,000
and $42,000) are greater than $3000 when divided by 12 months.

Manipulating Information By Date
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
In Texis dates are stored as integers representing an absolute number of
seconds from January 1, 1970, Greenwich Mean Time. This is done for
efficiency, and to avoid confusions stemming from differences in
relative times assigned to files from different time zones. The
allowable range of years is 1970 through 2037. Years between 1902 and
1970 may be stored and compared for equality (``=``) but will not
compare correctly using less than (``<``) and greater than (``>``).
Counters may also be treated as dates for comparison purposes. They may
be compared to date fields or date strings. When compared with dates
only the date portion of the counter is considered and the sequence
number is ignored.
The comparison operators as given in Table [tab:CompOp] are used to
compare date values, so that dates may be used as qualifying statements
in the ``WHERE`` clause.
**Example:** The Strategic Planning and Intelligence Department is
responsible for polling online news information on a daily basis,
looking for information relevant to Acme’s ongoing business. Articles of
interest are stored in an archived ``NEWS`` table which retains the full
text of the article along with its subject, byline, source, and date.
The date column is named NDATE, for “News Date”, as “date” is a special
reserved SQL name and can’t be used for column names.
A Date field may be compared to a number representing the number of
seconds since 1/1/70 0:0:0 GMT (e.g.: 778876248). It may also be
compared to a string representing a human readable date in the format
``'YYYY-MM-DD [HH:MM[:SS] [AM|PM]]'`` (e.g.: ``'1994-03-05 06:30 pm'``
or ``'1994-07-04'``). The date string may also be preceded by
“``begin of``” or “``end of``” meaning the first or last second of a
day, respectively.
Enter this query:
::
         SELECT   NDATE, SUBJECT
         FROM     NEWS
         WHERE    NDATE BETWEEN 'begin of 1993-07-30'
                            AND 'end of 1993-07-30' ;
Although the date column is stored with an absolute value, it is
converted to the correct relative value when displayed. However, a date
assigned to a file is to the second, and to match that time, you must
match the same number of seconds. Stating the date as ``1993-07-30``
refers to a particular second of that day. An article which came in at 2
p.m. would not match in seconds. Thus you state the range of seconds
that span the 24 hour period called “``'1993-07-30'``” by specifying a
range between the first to last moment of the day.
In this example, all the articles which were saved from July 30, 1993
are displayed with their subject lines. The date as formatted by Texis
when displaying the date column is the format used inside the single
quotes. It is put in quotes because it is a text string rather than an
absolute value.
Dates are usually used to limit the amount of text retrieved based on
some other search requirement, and would be so used along with other
qualifying statements in the ``WHERE`` clause. The next query is
identical to the last, but it adds another requirement.
::
         SELECT   NDATE, SUBJECT
         FROM     NEWS
         WHERE    NDATE BETWEEN 'begin of 1993-07-30'
                            AND 'end of 1993-07-30'
         AND      BODY LIKE 'bill gates' ;
Now we can retrieve articles from July 30, 1993, but only a list of
those articles whose text body mentions Bill Gates. A listing of Date
and Subject of the article will be displayed, as dictated in ``SELECT``.
Now we know which articles are available and can pick any we would want
to read in full.
This example uses a text query to find sentences in the body of the
information with reference to “Bill Gates”. Use of this type of query in
the ``LIKE`` clause is explained in Chapter [Chp:MMLike]. The following
articles are retrieved:
::
      NDATE                SUBJECT
      1993-30-07 04:46:04  High-Technology R&D Has Lost Its Cost-Effect...
      1993-30-07 13:10:08  Heavy R&D Spending No Longer the Magic Route...
Date fields can use any of the comparison operators as shown in
table 
- :ref:`sql1:compop` 
to manipulate information. We could broaden the date
range of this search by increasing the BETWEEN range, or we could do it
as follows:
::
         SELECT   NDATE, SUBJECT
         FROM     NEWS
         WHERE    BODY LIKE 'bill gates'
         AND      NDATE > 'begin of 1993-07-30'
         AND      NDATE < 'end of 1993-08-01' ;
Remember that the actual value of the date is in a number of seconds.
Therefore, greater than (``>``) translates to “a greater number of
seconds than the stated value”, and therefore means “newer than”, while
lesser than (``<``) translates to “a fewer number of seconds than the
stated value”, and therefore means “older than”.
This would increase the output list to include dates in the specified
range; that is, between July 30th and August 1st 1993.
::
      NDATE       SUBJECT
      1993-07-30 04:46:04  High-Technology R&D Has Lost Its Cost-Effect...
      1993-07-30 13:10:08  Heavy R&D Spending No Longer the Magic Route...
      1993-07-31 07:56:44  Microsoft-Novell battle out in the open
      1993-07-31 16:40:28  Microsoft to Undergo Justice Department Scrutiny
      1993-08-01 09:50:24  Justice Dept. Reportedly to Study Complaints ...
Date strings have some additional operators, “today” and “now”. When
used following DATE they are converted to today’s date and time in
seconds for both “today” and “now”. A time period of seconds, minutes,
hours, days, weeks, or months, can also be specified. A leading plus
(``+``) or minus (``-``) may also be specified to indicate past or
future. Using our example from the ``NEWS`` table, the form of the
command would be:
::
         SELECT   NDATE, SUBJECT
         FROM     NEWS
         WHERE    NDATE > '-7 days' ;
This query requests all articles less than seven days old and would
produce a list of their subjects and date.
::
         SELECT   NDATE, SUBJECT
         FROM     NEWS
         WHERE    NDATE < '-1 minute'
           AND    NDATE > '-1 hour' ;
This query would produce a list of articles which came in over the last
hour. The date must be older than 1 minute ago, but newer than 1 hour
ago.

Summarizing Values: GROUP BY Clause and Aggregate Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
So far, the examples presented have shown how to retrieve and manipulate
values from individual rows in a table. In this section, we will
illustrate how summary information can be obtained from groups of rows
in a table.
Often we find it useful to group data by some characteristic of the
group, such as department or division, or benefit level, so that summary
statistics about the group (totals, averages, etc.) can be calculated.
For example, to calculate average departmental salaries, the user could
group the salaries of all employees by department. In Texis, the GROUP
BY clause is used to divide the rows of a table into groups that have
matching values in one or more columns. The form of this clause is:
::
         GROUP BY   column-name1 [,column-name2] ...
and it fits into the ``SELECT`` expression in the following manner.
::
         SELECT     column-name1 [,column-name2] ...
         FROM       table-name
         [WHERE     search-condition]
         [GROUP BY  column-name1 [,column-name2] ... ]
         [ORDER BY  column-name1 [DESC] [,column-name2] [DESC] ] ... ;
The column(s) listed in the GROUP BY clause are used to form groups. The
grouping is based on rows with the same value in the specified column or
columns being placed in the same group. It is important to note that
grouping is conceptual; the table is not physically rearranged.
As an extension Texis also allows the GROUP BY clause to consist of
expressions instead of just column names. This should be used with
caution, and the same expression should be used in the ``SELECT`` as in
the GROUP BY clause. This is especially true if the expression will fold
multiple values together, such as dividing a number by 1000 to group
quantities together if they are in the same 1000. If you select SALARY,
and GROUP BY SALARY/1000 you will see one sample salary from the
matching group.
The GROUP BY clause is normally used along with five built-in, or
“aggregate” functions. These functions perform special operations on an
entire table or on a set, or group, of rows rather than on each row and
then return one row of values for each group.
Table [tab:AggFunc] lists the aggregate functions available with Texis.
[tab:AggFunc]
+--------------------+-------------------------------------------+-------------------+
| Function Name      | Meaning                                   | Example           |
+====================+===========================================+===================+
| SUM(column name)   | Total of the values in a numeric column   | ``SUM(SALARY)``   |
+--------------------+-------------------------------------------+-------------------+
| AVG(column name)   | Average of the values in a column         | ``AVG(SALARY)``   |
+--------------------+-------------------------------------------+-------------------+
| MAX(column name)   | Largest value in a column                 | ``MAX(SALARY)``   |
+--------------------+-------------------------------------------+-------------------+
| MIN(column name)   | Smallest value in a column                | ``MIN(SALARY)``   |
+--------------------+-------------------------------------------+-------------------+
| COUNT(\*)          | Count of the number of rows selected      | ``COUNT(*)``      |
+--------------------+-------------------------------------------+-------------------+
Table: Texis Aggregate Function Names
Aggregate functions are used in place of column names in the ``SELECT``
statement. The form of the function is:
::
         Function name ([DISTINCT] argument)
In all situations the argument represents the column name to which the
function applies. For example, if the sum of all salaries is needed,
then the function SUM is used and the argument is the column SALARY.
When COUNT is used an asterisk (\*) can be placed within the parentheses
instead of a column name to count all the rows without regard to field.
If the DISTINCT keyword is used then only the unique values are
processed. This is most useful with COUNT to find the number of unique
values. If you use DISTINCT then you must supply a column name. DISTINCT
will work with the other aggregate functions, although there is
typically very little need for them. The DISTINCT feature was added in
version 4.00.1002000000
**Example:** What is the average salary paid in each department?
Enter this statement:
::
         SELECT     DEPT, AVG(SALARY)
         FROM       EMPLOYEE
         GROUP BY   DEPT ;
**Syntax Notes:**
-  ``AVG`` is the aggregate function name.
-  ``(SALARY)`` is the column on which the average is computed.
-  ``DEPT`` is the column by which the rows will be grouped.
The above statement will produce the following results:
::
      DEPT      AVG(SALARY)
      MKT       33500
      MGT       45000
      LIB       22000
      RND       27500
      FIN       42000
In this query, all rows in the ``EMPLOYEE`` table that have the same
department codes are grouped together. The aggregate function AVG is
calculated for the salary column in each group. The department code and
the average departmental salary are displayed for each department.
A ``SELECT`` clause that contains an aggregate function cannot contain
any column name that does not apply to a group; for example:
The statement:
::
         SELECT     ENAME, AVG(SALARY)
         FROM       EMPLOYEE
         GROUP BY   DEPT ;
results in the message
::
         Error at Line 1: Not a GROUP BY Expression
It is not permissible to include column names in a ``SELECT`` clause
that are not referenced in the GROUP BY clause. The only column names
that can be displayed, along with aggregate functions, must be listed in
the GROUP BY clause. Since ``ENAME`` is not included in the GROUP BY
clause, an error message results.
**Example:** The chair of the Marketing Department plans to participate
in a national salary survey for employees in Marketing Departments.
Determine the average salary paid to the Marketing Department employees.
This statement:
::
         SELECT     COUNT(*), AVG(SALARY)
         FROM       EMPLOYEE
         WHERE      DEPT = 'MKT'
Results in:
::
      COUNT(*)   AVG(SALARY)
      2          33500
In this example, the aggregate function AVG is used in a ``SELECT``
statement that has a ``WHERE`` clause. Texis selects the rows that
represent Marketing Department employees and then applies the aggregate
function to these rows.
You can divide the rows of a table into groups based on values in more
than one column. For example, you might want to compute total salary by
department and then, within a department, want subtotals by benefits
classification.
**Example:** What is the total salary paid by benefits classification in
each department?
Enter this statement:
::
         SELECT     DEPT, BENEFITS, SUM(SALARY)
         FROM       EMPLOYEE
         GROUP BY   DEPT, BENEFITS ;
In this example, we are grouping by department, and within department,
by benefits classification.
We’ll get the following results:
::
      DEPT      BENEFITS    SUM(SALARY)
      FIN       FULL        42000
      LIB       PART        22000
      MGT       FULL        45000
      MKT       FULL        67000
      RND       FULL        30000
      RND       PART        25000
In this query, the rows are grouped by department and, within each
department, employees with the same benefits are grouped so that totals
can be computed. Notice that the columns DEPT and BENEFITS can appear in
the ``SELECT`` statement since both columns appear in the GROUP BY
clause.
If the GROUP BY clause is omitted when an aggregate function is used,
then the entire table is considered as one group, and the group function
displays a single value for the entire table.
**Example:** What is the total salary paid to all employees?
The statement:
::
         SELECT     SUM(SALARY)
         FROM       EMPLOYEE ;
results in:
::
      SUM(SALARY)
      231000

Groups With Conditions: HAVING Clause
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Sometimes you may want to specify a condition that applies to groups
rather than to individual rows. For example, you might want a list of
departments where the average departmental salary is above $30,000. To
express such a query, the HAVING clause is used. This clause specifies
which groups should be selected and is used in combination with the
GROUP BY clause. The form of this clause is as follows:
::
         [GROUP BY  column-name1 [,column-name2] ...
         [HAVING    search-condition ]
Conditions in the HAVING clause are applied after groups are formed. The
search condition of the HAVING clause examines the grouped rows and
produces a row for each group where the search condition in the HAVING
clause is true. The clause is similar to the ``WHERE`` clause, except
the HAVING clause applies to groups.
**Example:** Which departments have an average salary above $30,000?
Order the results by average salary, with highest average salary
appearing first.
The statement:
::
         SELECT     DEPT, AVG(SALARY) AS AVG_SALARY
         FROM       EMPLOYEE
         GROUP BY   DEPT
         HAVING     AVG_SALARY > 30000
         ORDER BY   AVG_SALARY DESC ;
**Syntax Notes:**
-  When HAVING is used, it always follows a GROUP BY clause.
-  When referring to aggregate values in the HAVING and ORDER BY clauses
   of a GROUP BY you must assign an alternative name to the field, and
   use that in the HAVING and ORDER BY clauses.
The results are:
::
      DEPT      AVG_SALARY
      MGT       45000
      FIN       42000
      MKT       33500
In this query, the average salary for all departments is computed, but
only the names of those departments having an average salary above
$30,000 are displayed. Notice that Research and Development’s average of
$27,500 is not displayed, nor is the Library’s average of $22,000.
The GROUP BY clause does not sort the results, thus the need for the
ORDER BY clause. Finally, note that the ORDER BY clause must be placed
after the GROUP BY and HAVING clauses.
This chapter has covered the computational capabilities of Texis. In the
next chapter, you will learn how to develop more complex queries by
using the join operation and the nesting of queries.

Advanced Queries
----------------
[Chp:AdvQuer]
This chapter is divided into three sections. The first one focuses on
using the join operation to retrieve data from multiple tables. The
second section covers nesting of queries, also known as subqueries. The
final section introduces several advanced query techniques, including
self-joins, correlated subqueries, subqueries using the EXISTS operator.

Retrieving Data From Multiple Tables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
All the queries looked at so far have been answered by accessing data
from one table. Sometimes, however, answers to a query may require data
from two or more tables.
For example, for the Corporate Librarian to display a list of
contributing authors with their long form department name requires data
from the ``REPORT`` table (author) and data from the ``DEPARTMENT``
table (department name). Obtaining the data you need requires the
ability to combine two or more tables. This process is commonly referred
to as “*joining the tables*”.
Two or more tables can be combined to form a single table by using the
*join operation*. The join operation is based on the premise that there
is a logical association between two tables based on a common attribute
that links the tables. Therefore, there must be a common column in each
table for a join operation to be executed. For example, both the
``REPORT`` table and the ``DEPARTMENT`` table have the department
identification code in common. Thus, they can be joined.
Joining two tables in Texis is accomplished by using a ``SELECT``
statement. The general form of the ``SELECT`` statement when a join
operation is involved is:
::
         SELECT   column-name1 [,column-name2] ...
         FROM     table-name1, table-name2
         WHERE    table-name1.column-name = table-name2.column-name ;
The combination of table name with column name as stated in the
``WHERE`` clause describes the Join condition.

Command Discussion
""""""""""""""""""
#. A join operation pulls data from two or more tables listed in the
   ``FROM`` clause. These tables represent the source of the data to be
   joined.
#. The ``WHERE`` clause specifies the relationship between the tables to
   be joined. This relationship represents the *join condition*.
   Typically, the join condition expresses a relationship between rows
   from each table that match on a common attribute.
#. When the tables to be joined have the same column name, the column
   name is prefixed with a table name in order for Texis to know from
   which table the column comes. Texis uses the notation:
   ::
            table-name.column-name
   The table name in front of the column name is referred to as a
   *qualifier*.
#. The common attributes in the join condition need not have the same
   column name, but they should represent the same kind of information.
   For example, where the attribute representing names of people
   submitting resumes was named ``RNAME`` in table 1, and the attribute
   for names of employees was named ``ENAME`` in table 2, you could
   still join the tables on the common character field by specifying:
   ::
            WHERE table-name1.RNAME = table-name2.ENAME
   While the above is true, it is still a good rule of thumb in database
   design to give the same name to all columns referring to data of the
   same type and meaning. Columns which are designed to be a key, and
   intended as the basis for joining tables would normally be given the
   same name.
#. If a row from one of the tables never satisfies the join condition,
   that row will not appear in the joined table.
#. The tables are joined together, and then Texis extracts the data, or
   columns, listed in the ``SELECT`` clause.
#. Although tables can be combined if you omit the ``WHERE`` clause,
   this would result in a table of all possible combinations of rows
   from the tables in the ``FROM`` clause. This output is usually not
   intended, nor meaningful, and can waste much computer processing
   time. Therefore, be careful in forming queries that involve multiple
   tables.
**Example:** The corporate librarian wants to distribute a list of
authors who have contributed reports to the corporate library, along
with the name of that author’s department. To fulfill this request, data
from both the ``REPORT`` table (author) and the ``DEPARTMENT`` table
(department name) are needed.
You would enter this statement:
::
         SELECT   AUTHOR, DNAME
         FROM     REPORT, DEPARTMENT
         WHERE    REPORT.DEPT = DEPARTMENT.DEPT ;
**Syntax Notes:**
-  REPORT and DEPARTMENT indicate the tables to be joined.
-  The ``WHERE`` clause statement defines the condition for the join.
-  The notation “``REPORT.``” in “``REPORT.DEPT``”, and
   “``DEPARTMENT.``” in “``DEPARTMENT.DEPT``” are the qualifiers which
   indicate from which table to find the column.
This statement will result in the following joined table:
::
      AUTHOR                   DNAME
      Jackson, Herbert         Research and Development
      Sanchez, Carla           Product Marketing and Sales
      Price, Stella            Finance and Accounting
      Smith, Roberta           Research and Development
      Aster, John A.           Product Marketing and Sales
      Jackson, Herbert         Research and Development
      Barrington, Kyle         Management and Administration
In this query, we are joining data from the REPORT and the DEPARTMENT
tables. The common attribute in these two tables is the department code.
The conditional expression:
::
         REPORT.DEPT = DEPARTMENT.DEPT
is used to describe how the rows in the two tables are to be matched.
Each row of the joined table is the result of combining a row from the
``REPORT`` table and a row from the ``DEPARTMENT`` table for each
comparison with matching codes.
To further illustrate how the join works, look at the rows in the
``REPORT`` table below where DEPT is “MKT”:
::
      TITLE                    AUTHOR           DEPT FILENAME
      Disappearing Ink         Jackson, Herbert RND  /data/rnd/ink.txt
    > INK PROMOTIONAL CAMPAIGN SANCHEZ, CARLA   MKT  /data/MKT/PROMO.RPT
      Budget for 4Q 92         Price, Stella    FIN  /data/ad/4q.rpt
      Round Widgets            Smith, Roberta   RND  /data/rnd/widge.txt
    > PAPERCLIPS               ASTER, JOHN A.   MKT  /data/MKT/CLIP.RPT
      Color Panorama           Jackson, Herbert RND  /data/rnd/color.txt
      Meeting Schedule         Barrington, Kyle MGT  /data/mgt/when.rpt
Now look at the rows in the ``DEPARTMENT`` table below where DEPT is
“MKT”. These are matching rows since the department code (“MKT”) is the
same.
::
      DEPT DNAME                               DHEAD      DIV  BUDGET
      MGT  Management and Administration       Barrington CORP 22000
      FIN  Finance and Accounting              Price      CORP 26000
      LEG  Corporate Legal Support             Thomas     CORP 28000
      SUP  Supplies and Procurement            Sweet      CORP 10500
      REC  Recruitment and Personnel           Harris     CORP 15000
      RND  Research and Development            Jones      PROD 27500
      MFG  Manufacturing                       Washington PROD 32000
      CSS  Customer Support and Service        Ferrer     PROD 11000
    > MKT  PRODUCT MARKETING AND SALES         BROWN      PROD 25000
      ISM  Information Systems Management      Dedrich    INFO 22500
      LIB  Corporate Library                   Krinski    INFO 18500
      SPI  Strategic Planning and Intelligence Peters     INFO 28500
The matching rows can be conceptualized as combining a row from the
``REPORT`` table with a matching row from the ``DEPARTMENT`` table.
Below is a sample of rows from both tables, matched on the department
code “MKT”:
::
    DEPT DNAME     DHEAD DIV  BUDGET TITLE      AUTHOR  FILENAME
    MKT  Marketing Brown PROD 25000  Ink        Sanchez /data/mkt/promo.rpt
    MKT  Marketing Brown PROD 25000  Paperclips Aster   /data/mkt/clip.rpt
This operation is carried out for all matching rows; i.e., each row in
the ``REPORT`` table is combined, or matched, with a row having the same
department code in the ``DEPARTMENT`` table:
::
    DEPT DNAME      DHEAD DIV  BUDGET TITLE      AUTHOR  FILENAME
    RND  Research   Jones PROD 27500  Ink        Jackson /data/rnd/ink.txt
    MKT  Marketing  Brown PROD 25000  Ink Promo  Sanchez /data/mkt/promo.rpt
    FIN  Finance    Price CORP 26000  Budget     Price   /data/ad/4q.rpt
    RND  Research   Jones PROD 27500  Widgets    Smith   /data/rnd/widge.txt
    MKT  Marketing  Brown PROD 25000  Paperclips Aster   /data/mkt/clip.rpt
    RND  Research   Jones PROD 27500  Panorama   Jackson /data/rnd/color.txt
    MGT  Management Barri CORP 22000  Schedule   Barring /data/mgt/when.rpt
The columns requested in the ``SELECT`` statement determine the final
output for the joined table:
::
      AUTHOR                   DNAME
      Jackson, Herbert         Research and Development
      Sanchez, Carla           Product Marketing and Sales
      Price, Stella            Finance and Accounting
      Smith, Roberta           Research and Development
      Aster, John A.           Product Marketing and Sales
      Jackson, Herbert         Research and Development
      Barrington, Kyle         Management and Administration
Observe that the joined table does not include any data on several
departments from the ``DEPARTMENT`` table, where that department did not
produce any contributing authors as listed in the ``REPORT`` table. The
joined table includes only rows where a match has occurred between rows
in both tables. If a row in either table does not match any row in the
other table, the row is not included in the joined table.
In addition, notice that the DEPT column is not included in the final
joined table. Only two columns are included in the joined table since
just two columns are listed in the ``SELECT`` clause, and DEPT is not
one of them.
The next example illustrates that conditions other than the join
condition can be used in the ``WHERE`` clause. It also shows that even
though the results come from a single table, the solution may require
that data from two or more tables be joined in the ``WHERE`` clause.
**Example:** Assume that you cannot remember the department code for
Research and Development, but you want to know the titles of all reports
submitted from that department.
Enter this statement:
::
         SELECT   TITLE
         FROM     DEPARTMENT, REPORT
         WHERE    DNAME = 'RESEARCH AND DEVELOPMENT'
           AND    REPORT.DEPT = DEPARTMENT.DEPT ;
**Syntax Notes:**
-  The tables to be joined are listed after ``FROM``.
-  The condition for the join operation is specified after AND (as part
   of ``WHERE``).
The results follow:
::
      TITLE
      Innovations in Disappearing Ink
      Improvements in Round Widgets
      Ink Color Panorama
Since you don’t know Research and Development’s department code, you use
the department name found in the ``DEPARTMENT`` table in order to find
the row that stores Research and Development’s code, which is ‘RND’.
Conceptually, visualize the join operation to occur as follows:
#. The conditional expression DNAME = ’RESEARCH AND DEVELOPMENT’
   references one row from the ``DEPARTMENT`` table; i.e., the ‘RND’
   row.
#. Now that the RND code is known, this row in the ``DEPARTMENT`` table
   is joined with the rows in the ``REPORT`` table that have DEPT = RND.
   The joined table represents the titles of the reports submitted by
   authors from the Research and Development department.
As the next example illustrates, more than two tables can be joined
together.
**Example:** Provide a list of salaries paid to those people in the
Product Division who contributed reports to the Corporate Library. The
report should include the author’s name, department name, and annual
salary.
You would enter this statement:
::
         SELECT   AUTHOR, DNAME, SALARY
         FROM     REPORT, DEPARTMENT, EMPLOYEE
         WHERE    DEPARTMENT.DIV = 'PROD'
           AND    REPORT.DEPT = DEPARTMENT.DEPT
           AND    REPORT.DEPT = EMPLOYEE.DEPT ;
**Syntax Notes:**
-  The order of the joins in the ``WHERE`` clause is not important.
-  The three tables to be joined are listed after ``FROM``.
-  The first AND statement (in ``WHERE`` clause) is the condition for
   joining the REPORT and ``DEPARTMENT`` tables.
-  The second AND statement (in ``WHERE`` clause) is the condition for
   joining the REPORT and ``EMPLOYEE`` tables.
-  While department code happens to be a column which all three tables
   have in common, it would be possible to join two tables with a common
   column, and the other two tables with a different common column, such
   as ``ENAME`` in the ``EMPLOYEE`` table and AUTHOR in the REPORT
   table. (The latter would not be as efficient, nor as reliable, so
   department name was chosen instead.)
The results would be:
::
      AUTHOR               DNAME                           SALARY
      Jackson, Herbert     Research and Development        30000
      Sanchez, Carla       Product Marketing and Sales     35000
      Smith, Roberta       Research and Development        25000
      Aster, John A.       Product Marketing and Sales     32000
In this example, data from three tables (REPORT, DEPARTMENT,
``EMPLOYEE``) are joined together.
Conceptually, the ``DEPARTMENT`` table references the rows that contain
PROD; this gives us the departments in the Product Division. The
departments in the Product Division (RND, MFG, CSS, MKT) are matched
against the departments in the DEPT column of the ``REPORT`` table. The
tables are joined for the Research and Development (RND) and Product
Marketing and Sales (MKT) departments. This yields an intermediate table
containing all the columns from both the DEPARTMENT and REPORT tables
for RND and MKT rows.
This intermediate table is joined with the ``EMPLOYEE`` table, based on
the second join condition REPORT.DEPT = EMPLOYEE.DEPT to form a
combination of columns from all 3 tables, for the matching rows.
Finally, the ``SELECT`` clause indicates which columns in the
intermediate joined table that you want displayed. Thus the author,
department name, and annual salary are shown as in the above example.
As a final point, the order in which you place the conditions in the
``WHERE`` clause does not affect the way Texis accesses the data. Texis
contains an “*optimizer*” in its underlying software, which chooses the
best access path to the data based on factors such as index
availability, size of tables involved, number of unique values in an
indexed column, and other statistical information. Thus, the results
would not be affected by writing the same query in the following order:
::
         SELECT   AUTHOR, DNAME, SALARY
         FROM     REPORT, DEPARTMENT, EMPLOYEE
         WHERE    REPORT.DEPT = EMPLOYEE.DEPT
           AND    REPORT.DEPT = DEPARTMENT.DEPT
           AND    DEPARTMENT.DIV = 'PROD' ;

Nesting Queries
~~~~~~~~~~~~~~~
At times you may wish to retrieve rows in one table based on conditions
in a related table. For example, suppose Personnel needed to call in any
employees in the Information Division receiving only partial benefits,
to discuss options for upgrading to the full benefit program. To answer
this query, you have to retrieve the names of all departments in the
Information Division, found in the DEPARTMENT table, and then the
employees with partial benefits in the Information Division departments,
found in the ``EMPLOYEE`` table.
In other situations, you may want to formulate a query from one table
that required you to make two passes through the table in order to
obtain the desired results. For example, you may want to retrieve a list
of staff members earning a salary higher than Jackson, but you don’t
know Jackson’s salary. To answer this query, you first find Jackson’s
salary; then you compare the salary of each staff member to his.
One approach is to develop a *subquery*, which involves embedding a
query (``SELECT``-``\verb``\ FROM“-\ ``WHERE`` block) within the
``WHERE`` clause of another query. This is sometimes referred to as a
“*nested query*”.
The format of a nested query is:
::
         SELECT   column-name1 [,column-name2]
         FROM     table-name
         WHERE    column-name IN
           (SELECT   column-name
            FROM     table-name
            WHERE    search-condition) ;
**Syntax Notes:**
-  The first ``SELECT``-``\verb``\ FROM“-\ ``WHERE`` block is the outer
   query.
-  The second ``SELECT``-``\verb``\ FROM“-\ ``WHERE`` block in
   parentheses is the subquery.
-  The IN operator is normally used if the inner query returns many rows
   and one column.

Command Discussion
""""""""""""""""""
Here are some points concerning the use of nested queries:
#. The above statement contains two ``SELECT``-``FROM``-``WHERE``
   blocks. The portion in parentheses is called the subquery. The
   subquery is evaluated first; then the outer query is evaluated based
   on the result of the subquery. In effect, the nested query can be
   looked at as being equivalent to:
   ::
            SELECT   column-name1 [,column-name2] ...
            FROM     table-name
            WHERE    column-name IN (set of values from the subquery) ;
   where the set of values is determined from the inner
   ``SELECT``-``FROM``-``WHERE`` block.
#. The IN operator is used to link the outer query to the subquery when
   the subquery returns a set of values (one or more). Other comparison
   operators, such as ``<``, ``>``, ``=``, etc., can be used to link an
   outer query to a subquery when the subquery returns a single value.
#. The subquery must have only a single column or expression in the
   ``SELECT`` clause, so that the resulting set of values can be passed
   back to the next outer query for evaluation.
#. You are not limited to one subquery. Though it isn’t advised, there
   could be as many as 16 levels of subqueries, with no fixed limitation
   except limits of memory and disk-space on the machine in use. Any of
   the operators (``IN``, ``=``, ``<``, ``>``, etc.) can be used to link
   the subquery to the next higher level.
**Example:** List the names of all personnel in the Information Division
by entering this statement:
::
         SELECT   ENAME
         FROM     EMPLOYEE
         WHERE    DEPT IN
           (SELECT   DEPT
            FROM     DEPARTMENT
            WHERE    DIV = 'INFO') ;
Parentheses are placed around the subquery, as shown below the outer
``WHERE`` clause.
The results are:
::
      ENAME
      Chapman, Margaret
      Dedrich, Franz
      Krinski, Wanda
      Peters, Robert
To understand how this expression retrieves its results, work from the
bottom up in evaluating the ``SELECT`` statement. In other words, the
subquery is evaluated first. This results in a set of values that can be
used as the basis for the outer query. The innermost ``SELECT`` block
retrieves the following set of department codes, as departments in the
Information (‘INFO’) Division: ISM, LIB, SPI.
In the outermost ``SELECT`` block, the IN operator tests whether any
department code in the ``EMPLOYEE`` table is contained in the set of
department codes values retrieved from the inner ``SELECT`` block; i.e.,
ISM, LIB, or SPI.
In effect, the outer ``SELECT`` block is equivalent to:
::
         SELECT   ENAME
         FROM     EMPLOYEE
         WHERE    DEPT IN ('ISM', 'LIB', 'SPI') ;
where the values in parentheses are values from the subquery.
Thus, the employee names Chapman, Dedrich, Krinski and Peters are
retrieved.
Subqueries can be nested several levels deep within a query, as the next
example illustrates.
**Example:** Acme Industrial’s ink sales are up, and management wishes
to reward everyone in the division(s) most responsible. List the names
of all employees in any division whose personnel have contributed
reports on ink to the corporate library, along with their department and
benefit level.
Use this statement:
::
         SELECT   ENAME, DEPT, BENEFITS
         FROM     EMPLOYEE
         WHERE    DEPT IN
           (SELECT   DEPT
            FROM     DEPARTMENT
            WHERE    DIV IN
              (SELECT   DIV
               FROM     DEPARTMENT
               WHERE    DEPT IN
                 (SELECT   DEPT
                  FROM     REPORT
                  WHERE    TITLE  LIKE 'ink') ) ) ;
IN is used for each subquery since in each case it is possible to
retrieve several values. You could use ‘``=``’ instead where you knew
only one value would be retrieved; e.g. where you wanted only the
division with the greatest number of reports rather than all divisions
contributing reports.
Results of the above nested query are:
::
      ENAME                DEPT   BENEFITS
      Aster, John A.       MKT    FULL
      Jackson, Herbert     RND    FULL
      Sanchez, Carla       MKT    FULL
      Smith, Roberta       MKT    PART
      Jones, David         RND    FULL
      Washington, G.       MFG    FULL
      Ferrer, Miguel       CSS    FULL
      Brown, Penelope      MKT    FULL
Again, remember that a nested query is evaluated from the bottom up;
i.e., from the innermost query to the outermost query. First, a text
search is done (TITLE LIKE ’INK’) of report titles from the ``REPORT``
table. Two such titles are located: “Disappearing Ink” by Herbert
Jackson from Research and Development (RND), and “Ink Promotional
Campaign” by Carla Sanchez from Product Marketing and Sales (MKT). Thus
the results of the innermost query produces a list of two department
codes: RND and MKT.
Once the departments are known, a search is done of the DEPARTMENT
table, to locate the division or divisions to which these departments
belong. Both departments belong to the Product Division (PROD); thus the
results of the next subquery produces one item: PROD.
A second pass is made through the same table, DEPARTMENT, to find all
departments which belong to the Product Division. This search produces a
list of four Product Division departments: MKT, RND, MFG, and CSS,
adding Manufacturing as well as Customer Support and Service to the
list.
This list is passed to the outermost query so that the ``EMPLOYEE``
table may be searched for all employees in those departments. The final
listing is retrieved, as above.
Here is another example specifically designed to illustrate the use of a
subquery making two passes through the same table to find the desired
results.
**Example:** List the names of employees who have salaries greater than
that of Herbert Jackson. Assume you do not know Jackson’s salary.
Enter this statement:
::
         SELECT   ENAME, SALARY
         FROM     EMPLOYEE
         WHERE    SALARY >
           (SELECT   SALARY
            FROM     EMPLOYEE
            WHERE    ENAME = 'Jackson, Herbert') ;
The compare operator ``>`` can be used (as could ``=`` and other compare
operators) where a single value only will be returned from the subquery.
Using the sample information in our ``EMPLOYEE`` table, the results are
as follows:
::
      ENAME              SALARY
      Aster, John A.     32000
      Barrington, Kyle   45000
      Price Stella       42000
      Sanchez, Carla     35000
The subquery searches the ``EMPLOYEE`` table and returns the value
``30000``, the salary listed for Herbert Jackson. Then the outer
``SELECT`` block searches the ``EMPLOYEE`` table again to retrieve all
employees with ``SALARY > 30000``. Thus the above employees with higher
salaries are retrieved.

Forming Complex Queries
~~~~~~~~~~~~~~~~~~~~~~~
The situations covered in this section are more technical than most end
users have need to conceptualize. However, a system administrator may
require such complex query structures to efficiently obtain the desired
results.

Joining a Table to Itself
"""""""""""""""""""""""""
In some situations, you may find it necessary to join a table to itself,
as though you were joining two separate tables. This is referred to as a
*self join*. In the self join, the combined result consists of two rows
from the same table.
For example, suppose that within the ``EMPLOYEE`` table, personnel are
assigned a RANK of “STAFF”, “DHEAD”, and so on. To obtain a list of
employees that includes employee name and the name of his or her
department head requires the use of a self join.
To join a table to itself, the table name appears twice in the ``FROM``
clause. To distinguish between the appearance of the same table name, a
temporary name, called an *alias* or a *correlation name*, is assigned
to each mention of the table name in the ``FROM`` clause. The form of
the ``FROM`` clause with an alias is:
::
         FROM   table-name [alias1] [,table-name [alias2] ] ...
To help clarify the meaning of the query, the alias can be used as a
qualifier, in the same way that the table name serves as a qualifier, in
``SELECT`` and ``WHERE`` clauses.
**Example:** As part of an analysis of Acme’s salary structure, you want
to identify the names of any regular staff who are earning more than a
department head.
Enter this query:
::
         SELECT   STAFF.ENAME, STAFF.SALARY
         FROM     EMPLOYEE DHEAD, EMPLOYEE STAFF
         WHERE    DHEAD.RANK = 'DHEAD' AND STAFF.RANK = 'STAFF'
           AND    STAFF.SALARY > DHEAD.SALARY ;
Using a sampling of information from the ``EMPLOYEE`` table, we would
get these results:
::
      ENAME               SALARY
      Sanchez, Carla      35000
In this query, the ``EMPLOYEE`` table, using the alias feature, is
treated as two separate tables named ``DHEAD`` and ``STAFF``, as shown
here (in shortened form):
::
      DHEAD Table                         STAFF Table
      EID ENAME   DEPT RANK  BEN  SALARY  EID ENAME   DEPT RANK  BEN  SALARY
      101 Aster   MKT  STAFF FULL 32000   101 Aster   MKT  STAFF FULL 32000
      109 Brown   MKT  DHEAD FULL 37500   109 Brown   MKT  DHEAD FULL 37500
      103 Chapman LIB  STAFF PART 22000   103 Chapman LIB  STAFF PART 22000
      110 Krinski LIB  DHEAD FULL 32500   110 Krinski LIB  DHEAD FULL 32500
      106 Sanchez MKT  STAFF FULL 35000   106 Sanchez MKT  STAFF FULL 35000
Now the join operation can be made use of, as if there were two separate
tables, evaluated as follows.
First, using the following compound condition:
::
         DHEAD.RANK = 'DHEAD' AND STAFF.RANK = 'STAFF'
each department head record (Brown, Krinski) in the ``DHEAD`` table is
joined with each staff record (Aster, Chapman, Sanchez) from the
``STAFF`` table to form the following intermediate result:
::
      DHEAD Table                         STAFF Table
      EID ENAME   DEPT RANK  BEN  SALARY  EID ENAME   DEPT RANK  BEN  SALARY
      109 Brown   MKT  DHEAD FULL 37500   101 Aster   MKT  STAFF FULL 32000
      109 Brown   MKT  DHEAD FULL 37500   103 Chapman LIB  STAFF PART 22000
      109 Brown   MKT  DHEAD FULL 37500   106 Sanchez MKT  STAFF FULL 35000
      110 Krinski LIB  DHEAD FULL 32500   101 Aster   MKT  STAFF FULL 32000
      110 Krinski LIB  DHEAD FULL 32500   103 Chapman LIB  STAFF PART 22000
      110 Krinski LIB  DHEAD FULL 32500   106 Sanchez MKT  STAFF FULL 35000
Notice that every department head row is combined with each staff
record.
Next, using the condition:
::
           STAFF.SALARY > DHEAD.SALARY
for each row of the joined table, the salary value from the ``STAFF``
portion is compared with the corresponding salary value from the
``DHEAD`` portion. If ``STAFF.SALARY`` is greater than ``DHEAD.SALARY``,
then ``STAFF.ENAME`` and ``STAFF.SALARY`` are retrieved in the final
table.
The only row in the joined table satisfying this condition of staff
salary being greater than department head salary is the last one, where
Carla Sanchez from Marketing, at a salary of $35,000, is earning more
than Wanda Krinski, as department head for the Corporate Library, at a
salary of $32,500.

Correlated Subqueries
"""""""""""""""""""""
All the previous examples of subqueries evaluated the innermost query
completely before moving to the next level of the query. Some queries,
however, cannot be completely evaluated before the outer, or main, query
is evaluated. Instead, the search condition of a subquery depends on a
value in each row of the table named in the outer query. Therefore, the
subquery is evaluated repeatedly, once for each row selected from the
outer table. This type of subquery is referred to as a *correlated
subquery*.
**Example:** Retrieve the name, department, and salary, of any employee
whose salary is above average for his or her department.
Enter this query:
::
         SELECT   POSSIBLE.ENAME, POSSIBLE.DEPT, POSSIBLE.SALARY
         FROM     EMPLOYEE POSSIBLE
         WHERE    SALARY >
           (SELECT   AVG (SALARY)
            FROM     EMPLOYEE AVERAGE
            WHERE    POSSIBLE.DEPT = AVERAGE.DEPT) ;
**Syntax Notes:**
-  The outer ``SELECT``-``FROM``-``WHERE`` block is the main query.
-  The inner ``SELECT``-``FROM``-``WHERE`` block in parentheses is the
   subquery.
-  POSSIBLE (following ``EMPLOYEE`` in the outer query) and AVERAGE
   (following ``EMPLOYEE`` in the subquery) are alias table names for
   the ``EMPLOYEE`` table, so that the information may evaluated as
   though it comes from two different tables.
It results in:
::
      ENAME               DEPT   SALARY
      Krinski, Wanda      LIB    32500
      Brown, Penelope     MKT    37500
      Sanchez, Carla      MKT    35000
      Jones, David        RND    37500
The column AVERAGE.DEPT correlates with POSSIBLE.DEPT in the main, or
outer, query. In other words, the average salary for a department is
calculated in the subquery using the department of each employee from
the table in the main query (POSSIBLE). The subquery computes the
average salary for this department and then compares it with a row in
the ``POSSIBLE`` table. If the salary in the ``POSSIBLE`` table is
greater than the average salary for the department, then that employee’s
name, department, and salary are displayed.
The process of the correlated subquery works in the following manner.
The department of the first row in POSSIBLE is used in the subquery to
compute an average salary. Let’s take Krinksi’s row, whose department is
the corporate library (LIB). In effect, the subquery is:
::
         SELECT   AVG (SALARY)
         FROM     EMPLOYEE AVERAGE
         WHERE    'LIB' = AVERAGE.DEPT ;
LIB is the value from the first row in POSSIBLE, as alias for
``EMPLOYEE``.
This pass through the subquery results in a value of $27,250, the
average salary for the LIB dept. In the outer query, Krinski’s salary of
$32,500 is compared with the average salary for LIB; since it is
greater, Krinski’s name is displayed.
This process continues; next, Aster’s row in POSSIBLE is evaluated,
where MKT is the department. This time the subquery is evaluated as
follows:
::
         SELECT   AVG (SALARY)
         FROM     EMPLOYEE AVERAGE
         WHERE    'MKT' = AVERAGE.DEPT ;
The results of this pass through the subquery is an average salary of
$34,833 for MKT, the Product Marketing and Sales Department. Since Aster
has a salary of $32,000, a figure lower than the average, this record is
not displayed.
Every department in POSSIBLE is examined in a similar manner before this
subquery is completed.

Subquery Using EXISTS
"""""""""""""""""""""
There may be situations in which you are interested in retrieving
records where there exists at least one row that satisfies a particular
condition. For example, the resume records stored in the ``RESUME``
table may include some individuals who are already employed at Acme
Industrial and so are entered in the ``EMPLOYEE`` table. If you wanted
to know which employees were seeking new jobs at the present time, an
existence test using the keyword ``EXISTS`` can be used to answer such a
query.
This type of query is developed with a subquery. The ``WHERE`` clause of
the outer query is used to test the existence of rows that result from a
subquery. The form of the ``WHERE`` clause that is linked to the
subquery is:
::
         WHERE [NOT] EXISTS (subquery)
This clause is satisfied if there is at least one row that would be
returned by the subquery. If so, the subquery does not return any
values; it just sets an indicator value to true. On the other hand, if
no elements satisfy the condition, or the set is empty, the indicator
value is false.
The subquery should return a single column only.
**Example:** Retrieve a list of Acme employees who have submitted
resumes to personnel for a different job placement.
Enter this query:
::
         SELECT   EID, ENAME
         FROM     EMPLOYEE
         WHERE    EXISTS
           (SELECT RNAME
            FROM   RESUME
            WHERE  EMPLOYEE.ENAME = RESUME.RNAME) ;
The results are:
::
      EID  ENAME
      107  Smith, Roberta
      113  Ferrer, Miguel
In this query, the subquery cannot be evaluated completely before the
outer query is evaluated. Instead, we have a correlated subquery. For
each row in ``EMPLOYEE``, a join of ``EMPLOYEE`` and ``RESUME`` tables
is performed (even though ``RESUME`` is the only table that appears in
the subquery’s ``FROM`` clause) to determine if there is a resume name
in ``RESUME`` that matches a name in ``EMPLOYEE``.
For example, for the first row in the ``EMPLOYEE`` table (ENAME =
’Smith, Roberta’) the subquery evaluates as “true” if at least one row
in the ``RESUME`` table has RNAME = ’Smith, Roberta’; otherwise, the
expression evaluates as “false”. Since there is a row in ``RESUME`` with
RNAME = ’Smith, Roberta’, the expression is true and Roberta Smith’s row
is displayed. Each row in ``EMPLOYEE`` is evaluated in a similar manner.
The following is an example of the interim join (in shortened form)
between the ``EMPLOYEE`` and ``RESUME`` Tables, for the above names
which satisfied the search requirement by appearing in both tables:
::
      EMPLOYEE Table            RESUME Table
      EID ENAME          DEPT   RES_ID  RNAME           JOB       EXISTS
                                                                  (subquery)
      107 Smith, Roberta RND    R406    Smith, Roberta  Engineer  TRUE
      113 Ferrer, Miguel CSS    R425    Ferrer, Miguel  Analyst   TRUE
Note in this example that there is no key ID field connecting the two
tables; therefore the character field for name is being used to join the
two tables, which might have been entered differently and therefore is
not an altogether reliable join. This indicates that such a search is an
unusual rather than a usual action.
Such a search would be a good opportunity to use a Metamorph ``LIKE``
qualifier rather than a straight join on a column as above, where
``ENAME`` must match exactly ``RNAME``. A slightly more thorough way of
searching for names appearing in both tables which were not necessarily
intended to be matched exactly would use Metamorph’s approximate pattern
matcher, indicated by a percent sign ``%`` preceding the name. For
example:
::
         SELECT   EID, ENAME
         FROM     EMPLOYEE
         WHERE    EXISTS
           (SELECT *
            FROM   RESUME
            WHERE  EMPLOYEE.ENAME LIKE '%' + RESUME.RNAME) ;
In this example a name approximately like each ``RNAME`` in the
``RESUME`` table would be compared to each ``ENAME`` in the ``EMPLOYEE``
table, increasing the likelihood of a match. (String concatenation is
used to append the name found in the resume table to the percent sign
(``%``) which signals the approximate pattern matcher XPM.)
Often, a query is formed to test if no rows are returned in a subquery.
In this case, the following form of the existence test is used:
::
         WHERE   NOT EXISTS (subquery)
**Example:** List any authors of reports submitted to the online
corporate library who are not current employees of Acme Industrial. To
find this out we would need to know which authors listed in the
``REPORT`` table are not entered as employees in the ``EMPLOYEE`` table.
Use this query:
::
         SELECT   AUTHOR
         FROM     REPORT
         WHERE    NOT EXISTS
           (SELECT *
            FROM   EMPLOYEE
            WHERE  EMPLOYEE.ENAME = REPORT.AUTHOR) ;
which would likely result in a list of former employees such as:
::
      AUTHOR
      Acme, John Jacob Snr.
      Barrington, Cedrick II.
      Rockefeller, George G.
Again, we have an example of a correlated subquery. Below is illustrated
(in shortened form) how each row which satisfied the search requirement
above in REPORT is evaluated with the records in ``EMPLOYEE`` to
determine which authors are not (or are no longer) Acme employees.
::
      REPORT Table                               EMPLOYEE Table  EXISTS
      TITLE              AUTHOR
      Company Origin     Acme, John Jacob Snr.                   FALSE
      Management Art     Barrington, Cedrick II.                 FALSE
      Financial Control  Rockefeller, George G.                  FALSE
In this example each of the above authors from the REPORT Table are
tested for existence in the ``EMPLOYEE`` Table. When they are not found
to exist there it returns a value of FALSE. Since the query condition in
the ``WHERE`` clause is that it NOT EXISTS, this changes the false value
to true, and these rows are displayed.
For each of the queries shown in this section, there are probably
several ways to obtain the same kind of result. Some correlated
subqueries can also be expressed as joins. These examples are given not
so much as the only definitive way to state these search requests, but
more so as to give a model for what kinds of things are possible.
This chapter has illustrated various complex query constructions
possible with Texis, and has touched on the use of Metamorph in
conjunction with standard SQL queries. The next chapter will explain
Metamorph query language in depth and give examples of its use in
locating relevant narrative text.
.. [1]
   The modulo operator (%) was added in Texis version 8; it is supported
   for integral types.
