The Texis Client API
====================

[Part:V:Chp:tcapi][Part:V:Chp:Embed]

Overview
--------

This chapter provides a reference to the functions that you will need to
use to write your own custom application that talks to Texis.

**SQL interface**

::

    TEXIS *texis_open(char *database, char *user, char *password);
    TEXIS *texis_open_options(void *, void *, void *, char *database, char *user,
                              char *group, char *password);
    int texis_set(TEXIS *, char *, char *);
    int texis_prepare(TEXIS *, char *sqlQuery);
    int texis_execute(TEXIS *);
    int texis_param(TEXIS *, int paramNum, void *data, long *dataLen,
                    int cType, int sqlType);
    int texis_resetparams(TEXIS *);
    int texis_cancel(TEXIS *);
    FLDLST *texis_fetch(TEXIS *, int stringsFrom);
    int texis_flush(TEXIS *tx);
    int texis_flush_scroll(TEXIS *tx, int nrows);

    char **texis_getresultnames(TEXIS *tx);
    int *texis_getresultsizes(TEXIS *tx);
    int texis_getrowcount(TEXIS *tx);
    int texis_getCountInfo(TEXIS *tx, TXCOUNTINFO *countInfo);
    TEXIS *texis_close(TEXIS *);
    FLDLST *freefldlst(FLDLST *fl);

The Texis API provides the client program with an easy to use interface
to any Texis server.

The programmer is *strongly* encouraged to play with the example
programs provided with the package before attempting their own
implementation.

SQL interface
    This group of operations is for passing a user’s query to the
    database for processing and subsequently obtaining the results.

::

      typedef struct FLDLST
      {
         int      n;                 /* number of items in arrays */
         int      type [FLDLSTMAX];  /* data type */
         void    *data [FLDLSTMAX];  /* pointer to data */
         int      ndata[FLDLSTMAX];  /* number of els in data */
         char    *name [FLDLSTMAX];  /* name of field */
         int      ondata[FLDLSTMAX]; /* 2002-07-05 number of els in schema */
      } FLDLST;

A ``FLDLST`` is a structure containing parallel arrays of information
about the selected fields and a count of those fields. ``FLDLST``
members:

``int n`` — The number of fields in the following lists.

``int *type`` — An array of types of the fields. Each element will be
one of the ``FTN_xxxx`` macros.

``void **data`` — An array of data pointers. Each element will point to
the data for the field. The data for a field is an array of ``type``\ s.

``int *ndata`` — An array of data counts. Each element says how many
elements are in the ``data`` array for this field.

``char **name`` — An array of strings containing the names of the
fields.

``SRCHLST *sl`` — An array of SRCHLSTs containing information Metamorph
searches, if any. Filled in, on request only, by ``n_fillsrchlst()``.

``MMOFFS mmoff`` — An array of Metamorph subhit offsets and lengths from
Metamorph searches, if any. Filled in, on request only, by
``n_fillsrchlst()``.

Possible types in ``FLDLST->type`` array:

``FTN_BYTE`` — An 8 bit ``byte``.

``FTN_CHAR`` — A ``char``.

``FTN_DOUBLE`` — A ``double``.

``FTN_DATE`` — A ``long`` in the same form as that from the ``time()``
system call.

``FTN_DWORD`` — A 32 bit ``dword``.

``FTN_FLOAT`` — A ``float``.

``FTN_INT`` — An ``int``.

``FTN_INTEGER`` — An ``int``.

``FTN_LONG`` — A ``long``.

``FTN_INT64`` — An ``int64``.

``FTN_UINT64`` — A ``uint64``.

``FTN_SHORT`` — A ``short``.

``FTN_SMALLINT`` — A ``short``.

``FTN_WORD`` — A 16 bit ``word``.

``FTN_INDIRECT`` — A ``char`` string URL indicating the file that the
data for this field is stored in.

``FTN_COUNTER`` — A ``ft_counter`` structure containing a unique serial
number.

``FTN_STRLST`` — A delimited list of strings.

The ``type`` may also be ``|``\ ed with ``FTN_NotNullableFlag``
(formerly ``DDNULLBIT``) and/or ``DDVARBIT``. If ``FTN_NotNullableFlag``
is set, it indicates that the field is not allowed to be NULL.
``DDVARBIT`` indicated that the field is variable length instead of
fixed length. When handling result rows these bits can generally be
ignored.

::

      typedef struct TXCOUNTINFO
      {
        EPI_HUGEINT rowsMatchedMin;
        EPI_HUGEINT rowsMatchedMax;
        EPI_HUGEINT rowsReturnedMin;
        EPI_HUGEINT rowsReturnedMax;
        EPI_HUGEINT indexCount;
      } TXCOUNTINFO;

The ``TXCOUNTINFO`` struct contains information about the min/max number
of table rows matching the query. The ``rowsMatchedMin`` and
``rowsMatchedMax`` members are before

-  GROUP BY

-  likeprows

-  aggregates (count(\*))

-  multivaluetomultirow

If the number is unknown the result will be less than 0. If
``rowsMatchedMin`` and ``rowsMatchedMax`` are different then the exact
count is unknown.

The ``rowsReturnedMin`` and ``rowsReturnedMax`` members indicate the
number of rows that would be returned by ``texis_fetch`` and are after

-  GROUP BY

-  likeprows

-  aggregates (count(\*))

-  multivaluetomultirow

If the number is unknown the result will be less than 0. If
``rowsReturnedMin`` and ``rowsReturnedMax`` are different then the exact
count is unknown.

::

    #include "texisapi.h"
    int texis_getCountInfo(TEXIS *tx, TXCOUNTINFO *countinfo);

This function will return the count of rows to be read from the index
for the prepared statement, if available.

This function populates a ``TXCOUNTINFO`` structure with information
about the number of rows expected to be returned. A range of rows may be
provided if rows may be filtered as rows are returned, for example a
combination of indexed and unindexed clauses.

The lower and upper bounds of the array will be updated as rows are
returned and they may converge to the same number.

If the bound is negative that means it is unknown, for example if no
index is being used.

::

    TEXIS *texis_open(char *database, char *user, char *password);
    TEXIS *texis_dup(TEXIS *tx);
    TEXIS *texis_close(TEXIS *tx);

``texis_open()`` opens the database. It returns a valid TEXIS pointer on
success or ``TEXISPN`` on failure. ``TEXISPN`` is an alias for
``(TEXIS *)NULL``.

``texis_dup()`` creates a new TEXIS pointer to the same database as a
currently valid handle. This saves much of the overhead of opening a new
connection to the database. The returned handle is a clean TEXIS handle,
and will not have a copy of the SQL statement from the copied handle. It
returns a valid TEXIS pointer on success or ``TEXISPN`` on failure.
``TEXISPN`` is an alias for ``(TEXIS *)NULL``.

``texis_close()`` closes the previously opened database. It always
returns ``TEXISPN``.

SQL statements are setup and executed with ``texis_prepare()``,
``texis_execute()``, and ``texis_fetch()``.

::

    texis_prepare(), texis_execute(), and texis_fetch()

::

    int texis_set(TEXIS *, char *property, char *value);

Equivalent to the the SQL SET statement for setting server properties
(p. )

::

    int texis_prepare(TEXIS *, char *sqlQuery);
    int texis_prepexec(TEXIS *, char *sqlQuery);
    int texis_execute(TEXIS *);

These functions perform SQL statement setup and execution for databases
opened with ``texis_open()``. ``texis_prepare()`` takes a ``TEXIS``
pointer from ``texis_open()`` and a SQL statement.

These functions provide an efficient way to perform the same SQL
statement multiple times with varying parameter data.
``texis_prepare()`` will return 1 on success, 0 on failure.

``texis_execute()`` and ``texis_fetch()`` or ``texis_flush()`` would
then be called to handle the results of the statement as with
``texis_prepare()``.

Once a SQL statement is prepared with ``texis_prepare()`` it may be
executed multiple times with ``texis_execute()``. Typically the
parameter data is changed between executions using the ``texis_param()``
function.

``texis_execute()`` will start to execute the statement prepared with
``texis_prepare()``. ``texis_execute()`` will return 1 on success, 0 on
failure.

Parameters should be set between ``texis_prepare()`` and
``texis_execute()``. If there are no parameters you can also use
``texis_prepexec()``, which will call ``texis_prepare()`` and
``texis_execute()`` in one call.

::

    SERVER *se;
    TEXIS     *tx;
    long    date;
    char   *title;
    char   *article;
    int     tlen, alen, dlen;

       ...
       if(!texis_prepare(tx,"insert into docs values(counter,?,?,?);"))
          { puts("texis_prepare Failed"); return(0); }
       for( each record to insert )
       {
          ...
          date=...
          dlen=sizeof(date);
          title=...
          tlen=strlen(title);
          article=...
          alen=strlen(article);
          if(!texis_param(tx,1,&date  ,&dlen,SQL_C_LONG,SQL_DATE       ) ||
             !texis_param(tx,2,title  ,&tlen,SQL_C_CHAR,SQL_LONGVARCHAR) ||
             !texis_param(tx,3,article,&alen,SQL_C_CHAR,SQL_LONGVARCHAR));
             { puts("texis_param Failed"); return(0); }
          if(!texis_execute(tx))
             { puts("texis_execute Failed"); return(0); }
          texis_flush(tx);
       }
       ...

::

    TEXIS     *tx;
    char   *query;
    int     qlen;
    FLDLST *fl;

       ...
       if(!texis_prepare(tx,
             "select id,Title from docs where Article like ?;"))
          { puts("texis_prepare Failed"); return(0); }
       for( each Article query to execute )
       {
          query=...
          qlen=strlen(query);
          if(!texis_param(tx,1,query,&qlen,SQL_C_CHAR,SQL_LONGVARCHAR))
             { puts("texis_param Failed"); return(0); }
          if(!texis_execute(tx))
             { puts("texis_execute Failed"); return(0); }
          while((fl=texis_fetch(tx,-1))!=FLDLSTPN)
          {
             ...
          }
       }
       ...

::

    texis_open(), texis_fetch()

::

    FLDLST *texis_fetch(TEXIS *tx, int stringsFrom);

``texis_fetch()`` returns a ``FLDLST`` pointer to the result set. The
``stringsFrom`` paramter lets you control if results are automatically
converted to text. An argument value of -1 will not convert any result,
other numbers will start converting from that result column, so passing
0 will cause all fields to be converted.

Continue calling ``texis_fetch()`` to get subsequent result rows.
``texis_fetch()`` will return ``FLDLSTPN`` when there are no more result
rows.

::

    TEXIS     *tx;
    FLDLST *fl;

       ...
       if((tx=texis_open())!=TEXISPN)      /* initialize database connection */
       {
          ...
                                                         /* setup query */
          if(texis_prepare(tx,
                     "select NAME from SYSTABLES where CREATOR!='texis';"
                    )!=TEXISPN)
             while((fl=texis_fetch(tx))!=FLDLSTPN)/* get next result row */
             {
                dispfields(fl);            /* display all of the fields */
             }
          ...
          texis_close(tx);                   /* close database connection */
       }
       ...

::

    texis_prepare(), texis_execute()

::

    int texis_param(tx, ipar, buf, len, ctype, sqltype)
    TEXIS   *tx;
    int     ipar;
    void    *buf;
    int     *len;
    int     ctype;
    int     sqltype;

    int texis_resetparams(tx)
    TEXIS   *tx;

These functions allow you to pass arbitrarily large or complex data into
a SQL statement. Sometimes there is data that won’t work in the confines
of the simple C string that comprises an SQL statement. Large text
fields or binary data for example.

Call ``texis_param()`` to setup parameters after ``texis_prepare()`` and
before ``texis_execute()``. If you have a statement you have already
executed once, and you want to execute again with different data, which
may have parameters unset which were previously set you can call
``texis_resetparams()``. This is not neccessary if you will explicitly
set all the parameters. Place a question mark (``?``) in the SQL
statement where you would otherwise place the data.

These are the parameters:

TEXIS \*tx
    The prepared SQL statement.

int ipar
    The number of the parameter, starting at 1.

void \*buf
    A pointer to the data to be transmitted.

int \*len
    A pointer to the length of the data. This can be ``(int *)NULL`` to
    use the default length, which assumes a ``'\0'`` terminated string
    for character data.

int ctype
    The type of the data. For text use SQL\_C\_CHAR.

int sqltype
    The type of the field being inserted into. For varchar use
    SQL\_LONGVARCHAR.

+--------------+--------------------+-------------------+---------------+
| Field type   | sqltype            | ctype             | C type        |
+==============+====================+===================+===============+
| varchar      | SQL\_LONGVARCHAR   | SQL\_C\_CHAR      | char          |
+--------------+--------------------+-------------------+---------------+
| varbyte      | SQL\_BINARY        | SQL\_C\_BINARY    | byte          |
+--------------+--------------------+-------------------+---------------+
| date         | SQL\_DATE          | SQL\_C\_LONG      | long          |
+--------------+--------------------+-------------------+---------------+
| integer      | SQL\_INTEGER       | SQL\_C\_INTEGER   | long          |
+--------------+--------------------+-------------------+---------------+
| smallint     | SQL\_SMALLINT      | SQL\_C\_SHORT     | short         |
+--------------+--------------------+-------------------+---------------+
| float        | SQL\_FLOAT         | SQL\_C\_FLOAT     | float         |
+--------------+--------------------+-------------------+---------------+
| double       | SQL\_DOUBLE        | SQL\_C\_DOUBLE    | double        |
+--------------+--------------------+-------------------+---------------+
| varind       | SQL\_LONGVARCHAR   | SQL\_C\_CHAR      | char          |
+--------------+--------------------+-------------------+---------------+
| counter      | SQL\_COUNTER       | SQL\_C\_COUNTER   | ft\_counter   |
+--------------+--------------------+-------------------+---------------+

::

    TEXIS  *tx;
    char   *description;
    char   *article;
    int     lend, lena;

       if(!texis_prepare(tx,"insert into docs values(?,?);"))
          { puts("texis_prepare Failed"); return(0); }
       ...
       description="a really large article";
       lend=strlen(description);
       article=...
       lena=strlen(article);
       if(!texis_param(tx,1,description,&lend,SQL_C_CHAR,SQL_LONGVARCHAR)||
          !texis_param(tx,2,article    ,&lena,SQL_C_CHAR,SQL_LONGVARCHAR));
          { puts("texis_param Failed"); return(0); }
       if(!texis_execute(tx))
          { puts("texis_execute Failed"); return(0); }
       ...

::

    int texis_cancel(tx);
    TEXIS      *tx;

This function is used to cancel a TEXIS statement. It can be called in a
signal handler or thread to flag that the currently executing call on
the TEXIS handle should terminate at the next possible opportunity.

Once the thread has returned the TEXIS handle should be closed.

::

    int texis_flush(tx);
    TEXIS      *tx;

This function flushes any remaining results from the current SQL
statement. Execution of the statement is finished however. This is
useful for ignoring the output of ``INSERT`` and ``DELETE`` statements.

::

    SERVER *se;

       ...
                                                         /* setup query */
       if(texis_prepare(tx,
                  "delete from customer where lastorder<'1990-01-01';"
                 )!=TEXISPN)
          texis_flush(tx);                        /* ignore result set */
       ...

::

    int texis_flush_scroll(tx,nrows);
    TEXIS      *tx;
    int     nrows;

This function flushes up to nrows results from the specified SQL
statement. This is useful for skipping a number of records from a
``SELECT``.

The return will be the number of records actually flushed if successful,
or if an error occurred then the return will be a negative number of (-1
- rowsflushed). Reaching the end of the results is not considered an
error, and will result in a return less than nrows.

::

    SERVER *se;

       ...
                                                         /* setup query */
       if(texis_prepare(tx,
                  "select name from customer where lastorder<'1990-01-01';"
                 )!=TEXISPN)
          texis_flush_scroll(tx,10);           /* ignore first 10 results */
       ...

Modifying the server
--------------------

::

    #include <sys/types.h>
    #include "dbquery.h"

    void adduserfuncs(fo)
    FLDOP *fo;

This section describes how to add user defined types and functions to
the texis server. The file aufex.c in the api directory provides an
outline of how to do this. The server calls the function adduserfuncs
which gives you the opportunity to add data types, operators and
functions to the server. There are three functions that are used for
adding to the server. They are dbaddtype, foaddfuncs and fosetop.

Once you have created your own version of aufex.c you need to compile
and link this file with the rest of the daemon objects. You must make
sure that this file is linked before the libraries to make sure your
function gets called. An example makefile is also included in the api
directory which shows the needed objects and include paths to make a new
daemon. After a new daemon has been created, make sure that it is
running and not the standard daemon. See the documentation on texisd to
see how to invoke it.

::

    int dbaddtype(name, type, size)
    char *name;
    int type;
    int size;

name
    the new name for the type. It should not start with the string
    “var”, as that is reserved for declaring the variable size form of
    the datatype.

type
    an integer which is used to refer to the type in functions etc. For
    a user added type this number should be between 32 and 63 inclusive.
    This number should be unique.

size
    the size of one element of the datatype. When one item of this type
    is written to the database, at least size bytes will be transferred.

The function will return 0 if successful, and -1 if there is no room for
more datatypes, or if a type with a different name already exists with
the same type number.

::

    typedef struct tagTIMESTAMP
    {
       short           year;
       unsigned short  month;
       unsigned short  day;
       unsigned short  hour;
       unsigned short  minute;
       unsigned short  second;
       unsigned long   fraction;
    } TIMESTAMP;

    dbaddtype("timestamp", 32, sizeof(TIMESTAMP);

::

    #include <sys/types.h>
    #include "dbquery.h"

    int foaddfuncs(fo, ff, n)
    FLDOP *fo;
    FLDFUNC *ff;
    int n;

Foaddfuncs adds a function to the math unit of Texis. The function can
take up to five arguments, and returns a single argument. The function
will be called with pointers to FLD structures. The return values should
be stuffed into the pointer to the first argument.

The math unit takes care of all the required stack manipulation, so no
stack manipulation is required in the function. The math unit will
always pass the maximum number of arguments to the function.

Foaddfuncs takes an array of function descriptions as one of its
arguments. The functions description function looks like

::

    struct {
       char *name;                                  /* name of function */
       int (*func)();                                        /* handler */
       int   minargs;                 /* minimum # of arguments allowed */
       int   maxargs;                 /* maximum # of arguments allowed */
       int   rettype;                                    /* return type */
       int   types[MAXFLDARGS];   /* argument types, 0 means don't care */
    } FLDFUNC;

**Parameters**

fo
    The math unit to add to.

ff
    Array of function descriptions to be added.

n
    The number of functions being added.

::

    int
    fsqr(f)
    FLD *f;
    {
       int     x;
       size_t  sz;

       x = *(ft_int *)getfld(f, &sz);      /* Get the number */
       x = x * x ;                              /* Square it */
       putfld(f, x, 1);                    /* Put the result */
       return 0;
    }

    static FLDFUNC  dbfldfuncs[]=
    {
       {"sqr",   fsqr, 1, 1, FTN_INT, FTN_INT, 0, 0, 0, 0 },
    };
    #define NFLDFUNCS (sizeof(dbfldfuncs)/sizeof(dbfldfuncs[0]))

    foaddfuncs(fo, dbfldfuncs, NFLDFUNCS);

This will add a function to square an integer.

::

    #include <sys/types.h>
    #include "dbquery.h"

    int fosetop(fo, type1, type2, func, ofunc)
    FLDOP *fo;
    int type1;
    int type2;
    fop_type func;
    fop_type *ofunc;

Fosetop changes a binary operator in the math unit of Texis. The
function func will be called for all operations on (type1, type2). If
the function does not know how to handle the specific operation, and
ofunc is not NULL, ofunc can be called to hande the operation.

The function being called should look like

::

    int
    func(f1, f2, f3, op)
    FLD *f1;
    FLD *f2;
    FLD *f3;
    int op;
    {
    }

**Parameters**

fo
    The math unit to change.

type1
    The type of the first operand.

type2
    The type of the second operand.

func
    Pointer to the function to perform the operation.

ofunc
    Pointer to pointer to function. The pointer to function will be
    stuffed with the old function to perform the operation. This can be
    used to add some cases, and keep the old functionality in others.
    The return value should be 0 for success. The result of the
    operation should be put in f3.

::

    #include <sys/types.h>
    #include "dbquery.h"

    fop_type o_ftich;          /* the pointer to the previous handler */

    int
    n_ftich(f1, f2, f3, op)
    FLD     *f1;
    FLD     *f2;
    FLD     *f2;
    int     op;
    {
            TIMESTAMP       *ts;
            double          d;
            int             n;

            if (op != FOP_ASN)      /* We only know about assignment. */
                    if (o_ftich)
                            return ((*o_ftich)(f1, f2, f3, op));
                    else
                            return FOP_EINVAL;

            /* Set up the return field. */
            f3->type = FTN_TIMESTAMP;
            f3->elsz = sizeof(TIMESTAMP);
            f3->size = sizeof(TIMESTAMP);
            f3->n = 1;
            if(sizeof(TIMESTAMP) > f3->alloced)
            {
                    void *v;

                    v = malloc(sizeof(TIMESTAMP));
                    setfld(f2, v, sizeof(TIMESTAMP));
                    f3->alloced = sizeof(TIMESTAMP);
            }

    /* First 0 out all the elements */
            ts = getfld(f1, NULL);
            ts->year = 0;
            ts->month = 0;
            ts->day = 0;
            ts->hour = 0;
            ts->minute = 0;
            ts->second = 0;
            ts->fraction = 0;

    /* Now read in the values */
            n = sscanf(getfld(f2, NULL), "%hu/%hu/%hd %hu:%hu:%hu%lf",
                    &ts->month, &ts->day, &ts->year,
                    &ts->hour, &ts->minute, &ts->second, &d);

    /* Convert any fractional seconds into the appropriate number
       of billionths of a second.
    */
            if (n == 7)
                    ts->fraction = d * 1000000000 ;
    }

     .
     .
     .
            fosetop(fo, FTN_TIMESTAMP, FTN_CHAR, n_ftich, &o_ftich);
     .
     .
     .

This example adds an operator to allow the assignment of a TIMESTAMP
field from a character string. See dbaddtype for the definition of
TIMESTAMP.
