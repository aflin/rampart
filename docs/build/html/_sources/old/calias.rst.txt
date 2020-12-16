Column Aliasing
===============

Similar to the abililty to alias the name of a table in the from clause
it is also possible to alias column names. An alias can have up to 35
characters (case is significant).

This has several possible uses. One is simply to produce a more
informative report, for example:

::

        SELECT COUNT(*) EMPLOYEES
        FROM   EMPLOYEES;

might produce the following output

::

        EMPLOYEES
           42

Another important use is when using the create table as select
statement. This allows you to rename a field, or to name a calculated
field.

::

        CREATE TABLE INVENTORY AS
        SELECT PROD_ID, SALES * 3 MAX_LEVEL, SALES MIN_LEVEL
        FROM   SALES;

Would create a new table with three fields, PROD\_ID, MAX\_LEVEL, and
MIN\_LEVEL.
