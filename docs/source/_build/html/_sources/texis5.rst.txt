The Texis Network Client API
============================

[Part:V:Chp:tnapi][Part:V:Chp:Embed]

Overview
--------

This chapter provides a reference to the functions that you will need to
use to write your own custom application that talks to Texis. This API
is flexible enough to support both a procedural programming style as
well as an event driven style.

Most applications can be written in Vortex, and executed via ``texis``.
Vortex provides a simple programming language which allows applications
to be written and tested quickly. Since the Vortex code handles the
details of extracting data from the web server, maintaining variable
state, and many of the repetitive tasks in generating user output, as
well as being automatically compiled when the script is changed it
allows for rapid development, deployment, and provides excellent
performance for interactive applications.

The API is provided for applications that may require linking with other
APIs that would not be good candidates for user defined Vortex functions
and for applications where the request/response paradigm of the web is
not appropriate.

**Server Connection Management**

::

    SERVER *openserver(char *hostname,char *port)
    SERVER *closeserver(SERVER *serverhandle)
    int     serveruser(char *username);
    int     servergroup(char *groupname);
    int     serverpass(char *password);

**Texis control parameters**

::

    int n_setdatabase(SERVER *se,char *database);
    str n_getdatabase(SERVER *se);

**SQL interface Version 2**

::

    TSQL   *n_opentsql(SERVER *se);
    TSQL   *n_closetsql(TSQL *ts);
    int     n_settsql(TSQL *ts,char *stmt);
    int     n_exectsql(TSQL *ts,...);
    int     n_gettsql(TSQL *ts,char *format,...);
    int     n_dotsql(TSQL *ts,char *stmt,...);
    int     n_resulttsql(TSQL *ts,int flag);

**SQL interface**

::

    int     n_texis(SERVER *se,char *queryformat,...);
    TX     *n_closetx(SERVER *se,TX *tx);
    TX     *n_opentx(SERVER *se);
    TX     *n_duptx(SERVER *se,TX *tx);
    TX     *n_settx(SERVER *se,TX *tx,char *queryformat,...);
    TX     *n_preptx(SERVER *se,TX *tx,char *queryformat,...);
    TX     *n_exectx(SERVER *se,TX *tx);
    int     n_runtx(SERVER *se,TX *tx);
    FLDLST *n_gettx(SERVER *se,TX *tx);
    FLDLST *freefldlst(FLDLST *fl);
    int     n_settexisparam(SERVER *se,int ipar,void *buf,int *len,
                            int ctype,int sqltype)
    int     n_paramtx(SERVER *se,TX *tx,int ipar,void *buf,int *len,
                            int ctype,int sqltype)
    int n_resetparamtx(SERVER *se, TX *tx);
    int     n_flushtx(SERVER *se,TX *tx);
    int     n_flushtx2(SERVER *se,TX *tx, int nrows, int max);
    MMOFFS *n_offstx(SERVER *se,TX *tx,char *fieldname);

::

    int n_regtexiscb(SERVER *se,void *usr,func cb);
        func cb(void *usr,TEXIS *tx,FLDLST *fl);

**Hit information**

::

    SRCHLST *n_getsrchlst(SERVER *se,TEXIS *tx);
    SRCHLST *n_freesrchlst(SERVER *se,SRCHLST *sl);
    SRCHI   *n_srchinfo(SERVER *se,SRCH *sr,int i);
    SRCHI   *n_freesrchi(SERVER *se,SRCHI *si);
    XPMI    *n_xpminfo(SERVER *se,SRCH *sr,int i);
    XPMI    *n_freexpmi(SERVER *se,XPMI *xi);
    int  n_getindexcount(SERVER *se);

**Object manipulation**

::

    char *n_newindirect(SERVER *se, char *database, char *table,
                        char *filename);

**File transfer**

::

    int n_rcopyto(SERVER *se,char *remotedest,char *localsrc);
    int n_rcopyfrom(SERVER *se,char *localdest,char *remotesrc);

The Texis API provides the client program with an easy to use interface
to any Texis server without regard to machine type. This is the
preferred interface and completely unrelated to the ODBC interface which
is documented elsewhere.

**The network Metamorph API is also contained within the Texis server
and available to client programs.** It is documented separately. Please
see its documentation in Part VI. The programmer is *strongly*
encouraged to play with the example programs provided with the package
before attempting their own implementation. It is also suggested that
you experiment with ``txtoc`` as well, as it can generate an outline to
start from.

Server Connection Management
    These functions allow you to connect and disconnect from a server.
    The return value from ``openserver()`` is a ``SERVER *``, and will
    be used as the first parameter in all subsequent calls to the
    Server.

SQL interface
    This group of operations is for passing a user’s query onto the
    server for processing and subsequently obtaining the results. There
    are two different versions of the SQL interface. Version 2 is a
    higher level interface, which provides the same functionality, but
    simplifies many operations by working directly with your variables.
    This is the preferred interface for most applications.

Hit registration
    These functions tell the server which client function to “*call
    back*” when it has located information pertinent to the query.

Hit information
    These functions allow you to obtain detailed information about any
    Metamorph queries that may have been used in the query.

Object manipulation
    These functions allow you to manipulate indirect fields within a
    Texis database.

File transfer
    These functions provide the ability to transfer entire files between
    the client and server.

Texis control parameters
    This set of functions is for getting and changing the various
    operational parameters that define how a remote Texis performs.

Network API common functions
----------------------------

::

    #include <sys/types.h>
    #include "tstone.h"
    SERVER *openserver(char *hostname,char *port);
    SERVER *closeserver(SERVER *serverhandle);
    int     serveruser(char *username);
    int     servergroup(char *groupname);
    int     serverpass(char *password);

These rather generic sounding functions are for establishing or
disconnecting a communication channel to a Thunderstone Server. The
presumption is made that the host and port you open is the correct one
for the type of calls you will be making.

In general, the ``openserver()`` call returns a handle to the requested
service at the specified ``hostname`` and ``port``. The returned handle
is of the type ``SERVER *`` and will have the value ``SERVERPN``\  [1]_
if there’s a problem.

The ``closeserver()`` call shuts the open communication channel with a
server and frees the allocated memory on both the client and the server
associated with the connection. ``closeserver()`` will return the value
``SERVERPN``.

The ``hostname`` is a string that is the given internet name or number
of the machine on which the requested service is running. For example:
``thunder.thunderstone.com`` or ``198.49.220.81``. The port number is
also a string, and is the port number assigned to the service when it is
brought up initially on the server machine. The port number may also be
assigned a name of a service that is enumerated in the file
``/etc/services``.

Either ``hostname`` or ``port`` or both may be the empty string (``""``)
indicating that the compiled in default is to be used. The default for
``hostname`` is ``"localhost"`` indicating the same machine that the
client application is running on. The default ``port`` is ``"10000"``
for Metamorph and ``"10002"`` for Texis.

The ``serveruser()``, ``servergroup()``, and ``serverpass()`` calls set
the user name, group name, and password, respectively, that
``openserver()`` will use when logging into the server. These functions
will return zero on error. Non-zero is returned on success. If
``servergroup()`` is not used, the user will be logged into their
default group as defined by the server.

The default user name and password are both the empty string (``""``).

If no user name is given then Texis will default to PUBLIC. If a user
name and password is given then Texis will verify them against the users
defined in Texis. You can use ``tsql`` to add users. See
Chapter [Chp:Sec] for an in-depth discussion of security.

::

     SERVER *se;                              /* network server pointer */
     SRCH   *sr;                                      /* search pointer */

                                               /* connect to the server */
     if(serveruser("me")         &&
        servergroup("somegroup") &&
        serverpass("mypassword") &&
        (se=openserver("thunder.thunderstone.com","666"))!=SERVERPN)
        {
         n_reghitcb(se,(void *)NULL,mycallback);  /* setup hit callback */
         if((sr=n_setqry(se,"power struggle"))!=SRCHPN)  /* setup query */
             {
              if(!n_search(se,sr))                     /* find all hits */
                  puts("n_search() failed");
              n_closesrch(se,sr);                  /* cleanup the query */
             }
         closeserver(se);                     /* disconnect from server */
        }

Make sure you are talking to the right port!

``/etc/services``, ``services(4)``

There are numerous other API calls that may be used to control the
behavior of Metamorph ``like`` searches in SQL statements. See section
[napi:mmctrl] for a listing of those functions.

SQL Interface Version 2
-----------------------

All files needed to build Texis clients are installed into the
``/usr/local/morph3/api`` directory. This directory should be added to
your compiler include and linker library paths. This directory also
contains example client source code and a ``makefile`` that can be used
as a model. ``sqlex1.c`` is an example of using this API.

Source code needs to ``#include "tstone.h"``. ``tstone.h`` also requires
``sys/types.h`` if it has not been included already.

Executables need to be linked with ``libntexis.a``, ``libapi3.a``,
``txclnop.o``, and the standard math lib. Also some platforms don’t
include TCP/IP socket calls in the standard libs. These platforms will
need them added to the link list. They are typically called
``libsocket.a``, ``libnsl.a``, and ``libresolv.a``.

::

    cc -I/usr/local/morph3/api -O -c sqlex1.c
    cc -L/usr/local/morph3/api sqlex1.o /usr/local/morph3/api/txclnop.o
       -lntexis -lapi3 -lm -o sqlex1

or

::

    cc -I/usr/local/morph3/api -O -c sqlex1.c
    cc -L/usr/local/morph3/api sqlex1.o /usr/local/morph3/api/txclnop.o
       -lntexis -lapi3 -lm -lsocket -lnsl -lresolv -o sqlex1

or (with the example makefile)

::

    make sqlex1

You should not work directly in the ``/usr/local/morph3/api`` directory.
You should make separate a directory to work in and copy the example
source and makefile to that directory and work there.

::

    #include "tstone.h"

    TSQL *n_opentsql(se)
    SERVER *se;

    TSQL *n_closetsql(ts)
    TSQL   *ts;

``n_opentsql()`` performs the initialization required to perform a Texis
SQL query. It returns a pointer to a structure that will be required by
the ``n_settsql(), n_exectsql(), n_gettsql()`` and ``n_closetsql()``
functions. It takes one argument that is an opened ``SERVER`` pointer
(from ``openserver()``). The ``SERVER`` pointer must remain open as long
as the ``TSQL`` is open. ``n_opentsql()`` returns ``TSQLPN``\  [2]_ on
failure.

All subsequent ``...tsql()`` family calls will take the ``TSQL`` pointer
as their first argument.

``n_closetsql()`` cleans up all data used used by ``n_opentsql()``. It
takes the ``TSQL`` pointer to close as its only argument. This must be
called before shutting the ``SERVER`` connection given to
``n_opentsql()``. ``n_closetsql()`` always returns TSQLPN.

::

    SERVER *se;
    TSQL   *ts;
    char   *database;

       ...
                         /* connect to the local host on the default port */
       if((se=openserver("",""))!=SERVERPN)
       {
          n_setdatabase(se,database);          /* set the database to use */
          if((ts=n_opentsql(se))!=TSQLPN) /* initialize the Texis SQL API */
          {

             /* ... perform SQL processing ... */

             n_closetsql(ts);               /* shutdown the Texis SQL API */
          }
          closeserver(se);                  /* disconnect from the server */
       }

::

    openserver(), n_settsql(), n_gettsql()

::

    #include "tstone.h"

    int n_settsql(ts,stmt)
    TSQL   *ts;
    char   *stmt;

    int n_exectsql(ts,...)
    TSQL   *ts;

``n_settsql()`` takes a SQL statement, parses it, and prepares to
execute it. All SQL statements must end with a semi-colon (``;``). Only
one SQL statement at a time may be included in the ``stmt`` argument.
The statement may contain ``printf`` like formatting codes. These
formatting codes may appear anywhere in the SQL statement that data
constants would otherwise appear (e.g.: as the values for insert or the
data to perform comparisons on in a select). The data for the formatting
codes are provided via the ``n_exectsql()`` function.

The following table summarizes the format codes and their respective
data types. See the **formatting codes** man page for full descriptions.

[tab:efmtcodes]

| \|l\|l\|l\|l\|
| & *Description* & *C Type* & *SQL Type*
| %b & Raw binary data & byte \* & BYTE
| %p%n & Raw binary data & byte \* & BYTE
| %s & Character string & char \* & CHAR
| %lf & Double precision floating point & double & DOUBLE
| %lu & Unix date stored as time\_t & long & DATE
| %f & Single precision floating point & float & FLOAT
| %d & 32/64-bit signed integer & long & INTEGER
| %ld & 32/64-bit signed integer & long & INTEGER
| %hd & 16-bit signed integer & short & SMALLINT
| %w & 16-bit unsigned integer & word & UNSIGNED SMALLINT
| %s & Name of external file & char \* & INDIRECT
| %\ :math:`<` & Name of external file & char \* & INDIRECT
| %dw & 32-bit unsigned integer & dword & UNSIGNED INTEGER
| %64d & 64-bit signed integer & EPI\_INT64 & INT64
| %64u & 64-bit unsigned integer & EPI\_UINT64 & UINT64
| %c & Unique serial number & ft\_counter \* & COUNTER
| %ls & A list of allocated strings & char \*\* & STRLST

``n_exectsql()`` takes a variable argument list that is a list of
pointers to the data to be passed into the query. ``n_exectsql()`` may
be issued as many times as desired for the same statement. When issuing
a statement where you want to get the resultant rows, such as
``SELECT``, you will need to use ``n_gettsql()`` (see its man page).
Variables passed to ``n_exectsql()`` are not modified or ``free()``\ ’d.

``SELECT`` statements will always generate result rows. By default
``INSERT`` and ``DELETE`` statements will not. To enable result rows
from ``INSERT`` and ``DELETE`` see ``n_resulttsql()``.

``n_settsql()`` and ``n_exectsql()`` both return 0 on failure and non-0
on success.

::

    /*
      This example loads records into a table called docs that has the
      following fields:

      Name    Type       Description
      ----    ----       -----------
      ctr     counter    a handy unique key field
      text    varchar    the text ocr'd off the image
      thumb   varbyte    a thumbnail of the original image
      image   indirect   the full size image

      Given a list of images, it OCR's any text, creates a small thumbnail,
      and uploads the original image file to the server.

      NOTE:  This example uses two fictitious calls, ocrimage() and
      shrinkimage(), to OCR images and make thumbnails of them.  We do
      not provide any such calls.  Also, Texis does not know any image
      formats.  Any image or other binary format data may be stored in
      a Texis field or indirect.
    */
    TSQL   *ts;
    char   *text;
    byte   *thumbnail;
    size_t  nthumbnail;
    char  **imagefiles;
    int     i, nimages;

       ...
                  /* setup the insert statement for loading new records */
       if(n_settsql(ts,"insert into docs values (counter, %s, %p%n, %<);"))
       {
          for(i=0;i<nimages;i++)               /* each image to process */
          {
             text=ocrimage(imagefile[i]);              /* OCR the image */
                                                    /* make a thumbnail */
             thumbnail=shrinkimage(imagefiles[i],&thumbnail);
                            /* execute the insert with the current data */
             if(!n_exectsql(ts,text,thumbnail,nthumbnail,imagefiles[i]))
                break;
          }
       }

::

    /*
      This example accesses the same table described in the previous
      example.  Given a Metamorph query, it will retrieve all rows
      that have a match in the text field.
    */
    TSQL   *ts;
    char   *query;

       ...
                                        /* setup the select statement */
       if(n_settsql(ts,
          "select ctr,text,thumb,image from docs where text like %s;"))
       {
              /* execute the select with the supplied Metamorph query */
          if(n_exectsql(ts,query))
          {
             /* the n_gettsql() man page describes how to get results */
          }
       }
       ...

::

    n_opentsql(), n_gettsql(), n_dotsql().

::

    #include "tstone.h"

    int n_gettsql(ts,format,...)
    TSQL   *ts;
    char   *format;

``n_gettsql()`` retrieves resultant rows from execution of a SQL
statement. It takes a format string containing ``scanf`` like conversion
codes. There should not be any characters in the format string except
format codes and optional space separators.

By default, only ``SELECT`` statements will generate result rows. To get
result rows from ``INSERT`` and ``DELETE`` statements see
``n_resulttsql()``.

The following table summarizes the format codes and their respective
data types. See the **formatting codes** man page for full descriptions.

[tab:gfmtcodes]

| \|l\|l\|l\|l\|
| & *Description* & *C Type* & *SQL Type*
| %b & Raw binary data & byte \*\* & BYTE
| %p%n & Raw binary data & byte \*\* & BYTE
| %s & Character string & char \*\* & CHAR
| %lf & Double precision floating point & double \* & DOUBLE
| %lu & Unix date stored as time\_t & long \* & DATE
| %f & Single precision floating point & float \* & FLOAT
| %d & 32/64-bit signed integer & int \* & INTEGER
| %ld & 32/64-bit signed integer & long \* & LONG
| %hd & 16-bit signed integer & short \* & SMALLINT
| %w & 16-bit unsigned integer & word \* & UNSIGNED SMALLINT
| %s & Name of external file & char \*\* & INDIRECT
| %\ :math:`>` & Name of external file & char \*\* & INDIRECT
| %dw & 32-bit unsigned integer & dword \* & UNSIGNED INTEGER
| %64d & 64-bit signed integer & EPI\_INT64 \* & INT64
| %64u & 64-bit unsigned integer & EPI\_UINT64 \* & UINT64
| %c & Unique serial number & ft\_counter \*\*& COUNTER
| %ls & A list of allocated strings & char \*\*\* & STRLST
| %a & All remaining fields as string & char \*\* & —
| %o & Metamorph subhit offsets & MMOFFS \*\* & —

After the format string are the variables to place the retrieved data
into. You must provide the address of each variable, as in ``scanf``, to
set. Each variable will be pointed to an allocated region that must be
released with ``free()`` when you are finished with it except for the
fundamental types ``double, long, float, short, word, dword``. It is not
necessary to get all result rows if you don’t want them all. any
subsequent ``n_settsql()`` will flush any ungotten rows.

``n_gettsql()`` both returns 0 on “end of results” and non-0 otherwise.

::

    /*
      This example accesses the same table described in the n_settsql()
      man page.  Given a Metamorph query, it will retrieve all rows
      that have a match in the text field.
    */
    TSQL       *ts;
    char       *query;
    ft_counter *ctr;
    char       *text;
    byte       *thumbnail;
    size_t      nthumbnail;
    char       *imagefile;
    MMOFFS     *mmo;

       ...
                                          /* setup the select statement */
       if(n_settsql(ts,
          "select ctr,text,thumb,image from docs where text like %s;"))
       {
                /* execute the select with the supplied Metamorph query */
          if(n_exectsql(ts,query))
          {
                                              /* get all resultant rows */
             while(n_gettsql(ts,"%c %s%o %p%n %s",
                             &ctr,&text,&mmo,&thumbnail,&nthumbnail,
                             &imagefile))
             {
                   break;
                ...
                /* do something with the fields (like display them) */
                ...
                free(ctr);
                free(text);
                freemmoffs(mmo);
                free(thumbnail);
                free(imagefile);
             }
          }
       }
       ...

::

    n_settsql() and n_resulttsql()

::

    #include "tstone.h"

    int n_dotsql(ts,stmt,...)
    TSQL   *ts;
    char   *stmt;

``n_dotsql()`` combines the functionality of ``n_settsql()`` and
``n_exectsql()`` into one call. It takes the statement format string as
in ``n_settsql()`` followed by input variable pointers as in
``n_exectsql()``. Any resultant rows are discarded.

It returns the number of rows that were processed on success or ``-1``
on error.

This function is useful for performing one shot statements that don’t
generate any output or you don’t care about the output. Like creating or
dropping a table or inserting a single record.

::

    /*
      This example creates a table called docs with the following fields:

      Name    Type       Description
      ----    ----       -----------
      id      counter    a handy unique key field
      text    varchar    the text ocr'd off the image
      thumb   varbyte    a thumbnail of the original image
      image   indirect   the full size image

      Then it inserts one row into it.
    */
    SERVER *se;
    TSQL   *ts;
    char   *text;
    byte   *thumbnail;
    size_t  nthumbnail;
    char   *imagefile;
    int     nrows;

       ...
       if(n_dotsql(ts,"create table docs(id counter,text varchar(1000),
                          thumbnail varbyte,imagefile indirect);")>=0)
       {
          ...
          nrows=n_dotsql(ts,
                         "insert into docs values (counter, %s, %p%n, %<);",
                         text,thumbnail,nthumbnail,imagefile);
       }
       ...

::

    n_settsql(), n_exectsql()

::

    #include "tstone.h"

    int n_resulttsql(ts,flag)
    TSQL   *ts;
    int     flag;

``n_resulttsql()`` controls the reporting of resultant rows from SQL
``INSERT`` and ``DELETE`` statements. By default ``INSERT`` and
``DELETE`` will not report back the affected rows. Calling this function
with ``flag`` set to ``1`` will enable the reporting of affected rows.
You then must use ``n_gettsql()`` to retrieve rows as with
``SELECT``\ s. ``n_resulttsql()`` must be called before ``n_settsql()``.

Pass ``flag`` as ``0`` to disable report of the affected rows.

``SELECT`` statements always return matching rows, regardless of this
setting. ``n_dotsql()`` is unaffected by this call. Results are always
suppressed.

``n_resulttsql()`` returns the previous ``flag`` setting. There is no
error return.

::

    TSQL   *ts;
    long    when;
    long    date;
    char   *query;

       ...
                                     /* delete rows without seeing them */
       if(n_settsql(ts,"delete from history where Date<%lu;"))
       {
          when=time((time_t *)NULL);                /* get current time */
          when-=7*86400;                             /* subtract 7 days */
          n_exectsql(ts,when);                      /* perform deletion */
       }
       ...
                                            /* delete rows and see them */
       n_resulttsql(ts,1);
       if(n_settsql(ts,"delete from history where Date<%lu;"))
       {
          when=time((time_t *)NULL);                /* get current time */
          when-=7*86400;                             /* subtract 7 days */
          if(n_exectsql(ts,when))                   /* perform deletion */
          {
             while(n_gettsql(ts,"%lu %s",&date,&query);
             {
                printf("%lu %s\n",date,query);
                free(query);
             }
          }
       }
       ...

::

    n_settsql()

The formatting codes placed in the ``n_settsql() stmt`` variable and the
``n_gettsql() format`` variable deal with the same kinds of data. Each
code’s usage for both input and output will be discussed together. The
basic difference between input and output modes is as follows.

Output is from ``n_gettsql()`` to your variables. All output variables
are specified by address (like ``scanf()``) so that they may be
re-pointed to the allocated data retrieved from the server. You must
``free()`` the output variables when you are finished with the data or
your program will grow ever larger with each resultant row until the the
bounds of time and space are reached and the universe begins to tear at
the seams and finally explodes in a spectacularly fiery ball just
because you didn’t bother to free a few variables.

Input is from your variables to ``n_settsql()``. Input variables are
specified directly (like ``printf()``) rather than by address. They are
not modified or freed by ``n_settsql()``.

::

    %b

Communicates SQL byte fields via C byte variables. Provide ``byte **``
for output and ``byte *`` for input. Typically used for raw binary data.
It assumes one byte on input since the length of your data can not be
determined. On output it gets as much data as is in the field. It is up
to you to know the length of it somehow. See ``%p%n`` for more robust
buffer handling.

::

    %p%n

Communicates any SQL field type via C byte variables. Provide
``byte **`` and ``size_t *`` for output and ``byte *`` and ``size_t``
for input. Typically used for raw binary data. Both ``%p`` and ``%n``
must be provided and in that order. Therefore you must supply two
variables. The first is a ``byte`` pointer to the data buffer and the
second is a ``size_t`` that is the number of bytes in the buffer.

::

    %s

Communicates SQL char and indirect fields via C char variables. Provide
``char **`` for output and ``char *`` for input. The data is a standard
``'\0'`` terminated string. Accessing an indirect this way transfers the
name of the indirect file. See ``%<`` and ``%>`` to transfer indirect
file contents.

::

    %lf

Communicates SQL double fields via C double variables. Provide
``double *`` for output and ``double`` for input.

::

    %lu

Communicates SQL date fields via C long variables. Provide ``long *``
for output and ``long`` for input.

::

    %f

Communicates SQL float fields via C float variables. Provide ``float *``
for output and ``float`` for input.

::

    %d or %ld

Communicates SQL integer fields via C long variables. Provide ``long *``
for output and ``long`` for input.

::

    %hd

Communicates SQL smallint fields via C short variables. Provide
``short *`` for output and ``short`` for input.

::

    %w

Communicates SQL unsigned smallint fields via C word (16 bit) variables.
Provide ``word *`` for output and ``word`` for input.

::

    %dw

Communicates SQL unsigned integer fields via C dword (32 bit) variables.
Provide ``dword *`` for output and ``dword`` for input.

::

    %64d

Communicates SQL int64 fields via C EPI\_INT64 (64 bit signed)
variables. Provide ``EPI_INT64 *`` for output and ``EPI_INT64`` for
input. INT64 support was added in Texis version 6.

::

    %64u

Communicates SQL uint64 fields via C EPI\_UINT64 (64 bit unsigned)
variables. Provide ``EPI_UINT64 *`` for output and ``EPI_UINT64`` for
input. UINT64 support was added in Texis version 6.

::

    %<

Communicates SQL indirect fields via C char variables and disk files.
This is for input only. Provide a ``char *`` variable that points to the
name of a file. The file will be uploaded to the server and be stored as
a Texis managed indirect.

::

    %>

Communicates SQL indirect fields via C char variables and disk files.
This is for output only. Provide a ``char **`` variable. The contents of
this variable will be examined before retrieving the contents of the
indirect file from the server.

In all of the following cases your supplied filename will be replaced
with a generated and allocated filename that is where the contents of
the indirect file from the server were downloaded to on the local
machine. The third case is an exception to the allocated value rule.

If the variable points to ``(char *)NULL`` a temporary filename will be
generated with the standard C library call ``tempnam()``.

If the variable points to the name of an existing directory on the local
machine the resulting filename will be that directory with the name of
the file on the server appended to it. (i.e. The file will wind up in
the specified directory).

Otherwise the variable is taken to be the exact name of the file to
place the file from the server in. Anything previously in the specified
file will be lost. In this case the resultant filename will be
untouched.

The contents of your variable will *not* be freed, just overwritten. If
it needs to be freed, you will have to keep another copy of it to free
after the transfer.

::

    %c

Communicates SQL counter fields via C ft\_counter variables. Provide a
``ft_counter **`` for output and a ``ft_counter *`` for input.
``ft_counter`` is a structure that contains two members called ``date``
and ``seq``. ``date`` is a ``long`` that contains the date in seconds
(see std. C ``time()``) that the counter was created. ``seq`` is a
``ulong`` that contains the sequence number for the particular second
described by ``date``. The combination of these values provides a unique
id across every record in every table in a given database.

::

    %ls

Communicates SQL strlst fields via C char variables. Provide a
``char ***`` for output and a ``char **`` for input. The string list is
an allocated array of pointers to allocated strings. The list is
terminated with an allocated empty string (``""``).

::

    %aD

Communicates all remaining resultant fields via a C char variable. This
is for output only. Provide a ``char **``. All fields that have not
already been extracted will be packed together into a C string with the
character ``D`` between each field. Where ``D`` is any single character
except ``\000``. Non-printable characters may be specified with octal
(``\ooo``) or hex (``\xhh``) notation (e.g.: tab would be ``\011`` in
octal and ``\x09`` in hex). Binary data that can be converted to human
readable form will be (e.g.: INTEGER FLOAT COUNTER). Everything after
the ``%aD`` code will be ignored since all the fields are now converted.

::

    %o

Communicates Metamorph hit offset information for the preceding field.
This is for output only. Provide a ``MMOFFS **``. ``MMOFFS`` is the
following structure:

::

       MMOFFS             /* Metamorph hit offsets          */
       {
          int n;          /* number if off's in array       */
          MMOFFSE
          {
             long start;  /* byte offset of start of region */
             long end;    /* one past end of region         */
          } *off;         /* array of subhit offset info    */
          int nhits;      /* number of hit's in in array    */
          MMOFFSE *hit;   /* array of hit offset info       */
       };

``MMOFFS->off`` is an array of start and end offsets of subhits
(individual search terms) within the field. ``MMOFFS->n`` is the number
of entries in the off array. Each ``off`` is made of two members:
``long start`` and ``long end``. Start is the byte offset of the subhit
within the field. End is the byte offset of the end of the subhit within
the field plus one (plus one makes ``for()`` loops easier to write). The
offs array contains subhits from all Metamorphs that may have been
applied to the field. Offsets are sorted in ascending order by start
offset.

Overall hit and delimiter offsets are contained in ``MMOFFS->hit`` in a
manner analogous to subhit info. ``MMOFFS->nhits`` is the number of
entries in the hit array and will always be a multiple of three. For
each Metamorph on a field there will be three entries. The first
contains the offsets of the overall hit (sentence, paragraph, etc...).
The second contains the offsets of the start delimiter. The third
contains the offsets of the end delimiter.

``MMOFFS`` must be freed with ``freemmoffs(MMOFFS *)`` instead of the
normal ``free(void *)``. ``freemmoffs(MMOFFS *)`` can handle MMOFFSPN.
This will be ``MMOFFSPN``\  [3]_ if there is no Metamorph data for the
field. This can happen for a number of reasons. If the search could be
completed in the index without needing to read the record, then there
will be no hit information. Also if you have ordered the output the hit
information can become invalid in the ordering, and the pointer will be
``NULL``.

::

    n_settsql() and n_gettsql()

Texis specific functions
------------------------

::

    #include <sys/types.h>
    #include "tstone.h"
    FLDLST
    {
     int    n;            /* number of items in each of following lists */
     int   *type;                           /* types of field (FTN_xxx) */
     void **data;                                 /* data array pointer */
     int   *ndata;                  /* number of elements in data array */
     char **name;                                      /* name of field */
    };
    int n_regtexiscb(SERVER *se,void *usr,
                     int (*cb)(void *usr,TEXIS *tx,FLDLST *fl) );

This function assigns the callback routine that the server is to call
each time it locates an item that matches the user’s query. The client
program can pass along a pointer to a “user” object to the register
function that will be in turn passed to the callback routine on each
invocation.

The callback routine also gets a ``TEXIS`` handle that can be used to
get further information about the hit (see ``n_getsrchlst()``) and a
``FLDLST`` handle that contains the ``select``\ ed fields.

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

``n_regtexiscb()`` will return true if the registration was successful,
and will return false (0) on error.

::

    #include <sys/types.h>
    #include "tstone.h"

    #define USERDATA my_data_structure
    USERDATA
    {
     FILE   *outfile;  /* where I will log the hits */
     long   hitcount;  /* just for fun */
    };

    void
    dispnames(ud,fl)                         /* display all field names */
    USERDATA *ud;
    FLDLST *fl;
    {
     int i;

     for(i=0;i<fl->n;i++)                                /* every field */
        printf("%s ",fl->name[i]);
     putchar('\n');                                  /* end header line */
    }

    void
    dispfields(ud,fl)                 /* display all fields of any type */
    USERDATA *ud;
    FLDLST *fl;
    {
     int i, j;

     for(i=0;i<fl->n;i++)
        {
         int   type=fl->type[i];                   /* the type of field */
         void *p   =fl->data[i];                 /* pointer to the data */
         int   n   =fl->ndata;                     /* how many elements */
         if(i>0) putchar('\t');  /* tab between fields, but not leading */
         switch (type & DDTYPEBITS) /* ignore NULL and VAR bits */
            {
                            /* loop through each element of field via j */
                                    /* cast and print according to type */
             case FTN_BYTE    : for(j=0;j<n;j++)
                                 printf("0x%x ",((byte *)p)[j]);
                                break;
             case FTN_INDIRECT: /*nobreak;*/
                                /* just print the filename as a string */
             case FTN_CHAR    : for(j=0;j<n && ((char *)p)[j]!='\0';j++)
                                 printf("%c"  ,((char *)p)[j]);
                                break;
             case FTN_DOUBLE  : for(j=0;j<n;j++)
                                 printf("%lf ",((double *)p)[j]);
                                break;
             case FTN_DWORD   : for(j=0;j<n;j++)
                                 printf("%lu ",
                                        (unsigned long)((dword *)p)[j]);
                                break;
             case FTN_FLOAT   : for(j=0;j<n;j++)
                                 printf("%f " ,((float *)p)[j]);
                                break;
             case FTN_INT     : for(j=0;j<n;j++)
                                 printf("%d " ,((int *)p)[j]);
                                break;
             case FTN_INTEGER : for(j=0;j<n;j++)
                                 printf("%d " ,((int *)p)[j]);
                                break;
             case FTN_LONG    : for(j=0;j<n;j++)
                                 printf("%ld ",((long *)p)[j]);
                                break;
             case FTN_SHORT   : for(j=0;j<n;j++)
                                 printf("%d " ,((short *)p)[j]);
                                break;
             case FTN_SMALLINT: for(j=0;j<n;j++)
                                 printf("%d " ,((short *)p)[j]);
                                break;
             case FTN_WORD    : for(j=0;j<n;j++)
                                 printf("%u " ,
                                        (unsigned int)((word *)p)[j]);
                                break;
              /* assume exactly one element on FTN_DATE for this example */
             case FTN_DATE    : printf("%.25s",ctime(p));/* human format */
                                break;
             case FTN_COUNTER : printf("%08lx%08lx",((ft_counter *)p)->date,
                                                    ((ft_counter *)p)->seq);
                                break;
             case FTN_STRLST  : {
                                 size_t nb=((ft_strlst *)p)->nb;
                                 char delim=((ft_strlst *)p)->delim;
                                 char *b=((ft_strlst *)p)->buf;
                                    for(j=0;j<nb;j++)
                                    {
                                       if(b[j]=='\0') putchar(delim);
                                       else           putchar(b[j]);
                                    }
                                }
                                break;
             default          : printf("unknowntype(%d)",type);
            }
        }
    }

    int
    hit_handler(usr,tx,fl)
    void *usr;  /* my user-data pointer */
    TEXIS *tx;  /* Texis API handle */
    FLDLST *fl; /* The field list data structure */
    {
     USERDATA *ud=(USERDATA *)usr;  /* cast the void into the real type */

     ++ud->hitcount;                      /* add one to the hit counter */

     if(ud->hitcount==1)                        /* before the first hit */
        dispnames(ud,fl);             /* display all of the field names */
     dispfields(ud,fl);                    /* display all of the fields */

     if(ud->hitcount>=100)/* tell the server to stop if I've seen enough */
        return(0);
     return(1);          /* tell the server to keep giving me more hits */
    }

    int
    main()
    {
     SERVER  *se;
     USERDATA mydata;
     ...
     mydata.outfile=(FILE *)NULL;
     mydata.hitcount=0;
     n_regtexiscb(se,&mydata,hit_handler);
     ...
    }

The example program ``netex3.c``.

::

    #include <sys/types.h>
    #include "tstone.h"
    SRCHLST
    {
     int n;                                /* number of elements in lst */
     SRCH lst[];                          /* list of Metamorph searches */
    };
    MMOFFS                                  /* Metamorph subhit offsets */
    {
       int n;                               /* number if off's in array */
       MMOFFSE
       {
          long start;                 /* byte offset of start of region */
          long end;                           /* one past end of region */
       } *off;                                  /* array of offset info */
    };
    SRCHLST *n_getsrchlst(SERVER *se,TEXIS *tx);
    SRCHLST *n_freesrchlst(SERVER *se,SRCHLST *sl);
    int      n_fillsrchlst(SERVER *se,TEXIS *tx,FLDLST *fl);

    SRCHI
    {
     char *what;                               /* what was searched for */
     char *where;                                     /* what was found */
     int   len;                               /* length of where buffer */
    };
    SRCHI   *n_srchinfo(SERVER *se,SRCH *sr,int i);
    SRCHI   *n_freesrchi(SERVER *se,SRCHI *si);

These functions may be used within the hit callback function to obtain
detailed information about any Metamorph queries that may have been used
in the query. ``n_getsrchlst()`` takes the TEXIS handle passed to the
hit callback function and returns a list of handles to all Metamorph
searches associated with the query. These handles may then be used in
calls to ``n_srchinfo()``. ``SRCHLSTPN`` is returned on error.
``SRCHLST`` members:

``int n`` — The number of searches contained in ``lst``.

``SRCH lst[]`` — The array of searches.

The ``SRCHLST`` returned by ``n_getsrchlst()`` should be freed by
calling ``n_freesrchlst()`` when it is no longer needed.

``n_fillsrchlst()`` fills in the ``SRCHLST *sl[]`` and
``MMOFFS mmoff[]`` arrays in the supplied ``FLDLST``. This provides the
Metamorph search handles, if any, for each individual field. This
supercedes ``n_getsrchlst()`` because it is more generally useful. It
also provides a list of all subhit offsets for each individual field.
This greatly simplifies hit tagging if all you need is the offset
information about each subhit. ``n_fillsrchlst()`` always returns
non-zero meaning success.

The ``MMOFFS->off`` member is an array of start and end offsets of
subhits within the field. ``MMOFFS->n`` is the number of entries in the
off array. Each ``off`` is made of two members: ``long start`` and
``long end``. Start is the byte offset of the subhit within the field.
End is the byte offset of the end of the subhit within the field plus
one (plus one makes ``for()`` loops easier to write). The offs array
contains subhits from all Metamorphs that may have been applied to the
field. Offsets are sorted in ascending order by start offset. Overall
hit and delimiter offsets are not included in the ``MMOFFS`` list.
``MMOFFS`` contains the offsets that would be returned with indices 3–n
of ``n_srchinfo()``, but sorted.

Many queries do not need to apply Metamorph to the actual field as the
index is sufficient to decide if there is a hit or not, and so will not
return any hit information. If the query orders the results it is
possible that the engine will have finished using the Metamorph engine
before the results are returned to the user, and so the results are no
longer available. If you need accurate hit-offset information it is
suggested that you use the Metamorph API at the client side to search
the field returned.

The memory allocated by ``n_fillsrchlst()`` should not be freed because
it is managed automatically.

``n_srchinfo()`` takes a search handle and the index of the sub-hit to
return information about. It returns a ``SRCHI`` pointer on success or
SRCHIPN on error or if the index is out of range. The index may be
controlled by a loop to get information about all parts of the hit.

Index values and what they return:

| xxxxxx = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx = xxxxxxxxxxxxxxxxxxxxx Index
  ``SRCHI->what`` points to ``SRCHI->where`` contains
| 0 The original query The whole hit
| 1 A regular expression The start delimiter
| 2 A regular expression The end delimiter
| 3-n The “set” being searched for The match
| as listed below

| xxxxxxxxx = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx Set type
  ``SRCHI->what`` points to
| REX A regular expression
| NPM The npm query expression
| PPM The root word of the list of words
| XPM The “approximate” string

Each ``SRCHI`` returned by ``n_srchinfo()`` should be freed by calling
``n_freesrchi()`` when it is no longer needed.

The subhit offsets returned by ``n_srchinfo()`` are *not* sorted.

``n_xpminfo()`` and its example.

::

    #include <sys/types.h>
    #include "tstone.h"
    XPMI *n_xpminfo(SERVER *se,SRCH *sr,int index);
    XPMI *n_freexpmi(SERVER *se,XPMI *xi);

These functions may be used within the hit callback function to obtain
detailed information about any search terms that may have used the
approximate pattern matcher (XPM). ``n_xpminfo()`` is called with the
index of the desired XPM.

It returns a structure containing everything about that XPM. It returns
XPMIPN [4]_ if index is out of bounds.

To get all XPM subhits put ``n_xpminfo()`` in a loop with index starting
at zero and incrementing until ``XPMIPN`` is returned.

Each valid structure returned by ``n_xpminfo()`` should be freed by
calling ``n_freexpmi()`` when it is no longer needed.

The XPMI structure contains the following members:

::

    XPMI                                                    /* XPM Info */
    {
     word  thresh;             /* threshold above which a hit can occur */
     word  maxthresh;                          /* exact match threshold */
     word  thishit;                             /* this hit's threshold */
     word  maxhit;                      /* max threshold located so far */
     char *maxstr;                    /* string of highest value so far */
     char *srchs;                            /* string being search for */
    };

Don’t expect ``XPMI.thresh`` to be the percentage entered in the query
passed to ``n_setqry()``. It is an absolute number calculated from that
percentage and the search string.

::

    int
    hit_handler(usr,tx,fl)
    void *usr;  /* my user-data pointer */
    TEXIS *tx;  /* Texis API handle */
    FLDLST *fl; /* The field list data structure */
    {
     ...
     MYAPP   *ts=(MYAPP *)usr;
     SERVER  *se=ts->se;
     SRCHLST *sl;
     SRCHI   *si;
     XPMI    *xi;
     int      i, j, k;

     ...
     sl=n_getsrchlst(se,tx);        /* get list of Metamorphs for query */
     if(sl!=SRCHLSTPN)
        {
         for(i=0;i<sl->n;i++)                     /* for each Metamorph */
             {
              SRCH *sr= &sl->lst[i];                           /* alias */
                                              /* loop thru all sub-hits */
                /* the zero index for n_srchinfo is the whole hit       */
                /* the one  index for n_srchinfo is the start delimiter */
                /* the two  index for n_srchinfo is the end delimiter   */
                /* the remaining indices are the subhits                */
              for(j=0;(si=n_srchinfo(se,sr,j))!=SRCHIPN;j++)
                  {
                   char *p, *e;              /* scratch buffer pointers */
                   switch(j)
                   {
                    case 0 :printf(" HIT    (%s):%d:",si->what,si->len);
                            break;
                    case 1 :printf(" S-DELIM(%s):%d:",si->what,si->len);
                            break;
                    case 2 :printf(" E-DELIM(%s):%d:",si->what,si->len);
                            break;
                    default:printf(" SUB-HIT(%s):%d:",si->what,si->len);
                            break;
                   }
                   for(p=si->where,e=p+si->len;p<e;p++)
                       if(*p<32 && *p!='\n' && *p!='\t')
                          printf("\\x%02x",*p);
                       else
                          putchar(*p);
                   printf("\n");
                   n_freesrchi(se,si);            /* free any mem in si */
                  }
              for(k=0;(xi=n_xpminfo(se,sr,k))!=XPMIPN;k++)
                 {                                    /* loop thru XPMs */
                  printf("XPM: \"%s\": thresh %u, maxthresh %u, thishit %u",
                         xi->srchs,xi->thresh,xi->maxthresh,xi->thishit);
                  printf("\n   : maxhit %u, maxstr \"%s\"\n",
                         xi->maxhit,xi->maxstr);
                  n_freexpmi(se,xi);                  /* free mem in xi */
                 }
             }
         n_freesrchlst(se,sl);
        }
     ...

     return(1);          /* tell the server to keep giving me more hits */
    }

The example program ``netex3.c``, ``n_reghitcb()``, ``n_getsrchlst()``,
``n_srchinfo()``.

::

    #include <sys/types.h>
    #include "tstone.h"
    int n_getindexcount(SERVER *se);

This function will return the count of rows to be read from the index
for the most recently prepared statement, if available.

The return from this function is only valid under certain circumstances,
which are when the index has been scanned to get a list of potentially
matching records. In a join, the return value will be the number of
matches in the inner table corresponding to the current outer row, not
the number of outer table matches. The actual number of records returned
may be significantly less if post processing is done to resolve some of
the where clause.

The default behaviour of Texis with a single relational operator and an
index on the field is to walk the index as the rows are returned, which
is faster at getting the initial rows out. Since it does not get all the
matching rows from the index first, n\_getindexcount() will return 0.
This behaviour can be changed with the bubble property.

::

::

    char *url=n_newindirect(SERVER *se,char *database,char *table,char *fn);

This functions allow you to manipulate indirect fields within a Texis
database.

``newindirect()`` generates a URL that can be used to store data on the
server. If ``fn`` is ``NULL`` or ``""`` it will create a URL which can
be used to store data in that is owned by Texis. If ``fn`` is not
``NULL`` and not ``""`` and is not a URL already then it will be made
into a URL owned by you. If fn is a full path, it will be respected.
Otherwise the standard path of indirect files for the table will be
prepended.

``newindirect()`` returns a URL that can be stored into. The URL that is
returned is an allocated string that **MUST** be freed by the caller.

The URLs returned by this function may then be used as the field
contents of indirect fields.

``n_rcopyto()``, ``n_rcopyfrom()``

::

    int n_rcopyto(SERVER *se,char *remotedest,char *localsrc);
    int n_rcopyfrom(SERVER *se,char *localdest,char *remotesrc);

These functions provide the ability to copy files between client and
server. They are useful for inserting and retrieving INDIRECT fields. An
indirect field will usually point to a file on the same machine as the
server. So the existing connection may be used to transfer the file.

``n_rcopyto()`` copies a file from the client to the server.
``n_rcopyfrom()`` copies a file from the server to the client. In both
cases the second argument is the name of the file to create and the
third argument is the file to read from.

Both functions return zero on error and non-zero on success.

::

    /* insert a row with an indirect while creating the indirect file */

    SERVER *se;
    char *database, *table;
    char *url, *remotefn, *localfn;
    char *description;

       ...
       database=...
       table=...
       ...
       n_setdatabase(se,database);
       ...
       localfn=...
       description=...
       ...
       url=n_newindirect(se,database,table,(char *)NULL);
       remotefn=urlfn(url);
       if(!n_rcopyto(se,remotefn,localfn))
          puts("error");
       n_texis(se,"insert into %s values('%s','%s');",
               table,description,remotefn);
       free(remotefn);
       free(url);
       ...

::

    /* query a table with an indirect field and download the file */

    int
    hit_handler(usr,tx,fl)
    void *usr;  /* my user-data pointer */
    TEXIS *tx;  /* Texis API handle */
    FLDLST *fl; /* The field list data structure */
    {
     USERDATA *myd=(USERDATA *)usr; /* cast the void into the real type */
     char *description, *remotefn;

          /* I know the resultant data types because I wrote the SELECT */
     description=(char *)fl->data[0];
     remotefn   =(char *)fl->data[1];
     printf("%s:\n",description);              /* print the description */
     if(!n_rcopyfrom(myd->se,"/tmp/scratch",remotefn)) /* get text file */
        puts("error");
     displayfile("/tmp/scratch");/* fictitious function to display a file */
     return(1);          /* tell the server to keep giving me more hits */
    }

    main()
    {
     USERDATA mydata;

       mydata.se=...
       mydata.database=...
       mydata.table=...
       ...
       n_regtexiscb(mydata.se,&mydata,hit_handler);
       n_setdatabase(mydata.se,mydata.database);
       ...
       n_texis(mydata.se,
         "select description,text from %s where text like 'power struggle'",
         mydata.table);
       ...
    }

``n_newindirect()``

::

    int   n_setdefaults(SERVER *se)
    int   n_setdatabase(SERVER *se,str dbname)
    str   n_getdatabase(SERVER *se)

This collection of functions provide the needed control over how a
**Texis** server will behave. They are to be used prior to a call to
``n_texis()``. All of the functions have a common first argument which
is the omnipresent ``SERVER *``. If a ``set`` function returns an
``int``, the value 0 means failure and ``not`` 0 means the operation was
successful. Those functions that have a ``void`` return value return
nothing. If a ``get`` function returns a pointer type, the value
``(type *)NULL`` indicates a problem getting memory. Otherwise the
pointer should be freed when no longer needed.

void n\_setdefaults(SERVER \*se)
    resets all server parameters to their initial state.

int n\_setdatabase(SERVER \*se,str dbname)
    sets ``dbname`` as the name of the **Texis** database that is to be
    queried against.

str n\_getdatabase(SERVER \*se)
    gets the name of the **Texis** database that is to be queried
    against.

::

    int n_texis(SERVER *se,char *queryformat,...);

This function comprises the real work that is to be performed by the
network Texis server. To initiate the actual search the program makes a
call to the ``n_texis()`` function. The server will begin to call the
client’s callback routine that was set in the ``n_regtexiscb()`` call.
The ``n_texis()`` function will return 0 on error or true if all goes
well. *NOTE: It is not considered an error for there to be zero hits
located by a search. A client’s callback routine will never be invoked
in this instance.*

The ``queryformat`` argument is a ``printf()`` style format string that
will be filled in by any subsequent arguments and then executed.

::

    #include <sys/types.h>
    #include "tstone.h"
    main(argc,argv)
    int argc;
    char **argv;
    {
     SERVER *se;
     char buf[80];
     USERDATA mydata;

     ...
     n_regtexiscb(se,mydata,hit_handler);         /* setup hit callback */
     n_setdatabase(se,argv[1]);               /* set database to search */
     while(gets(buf)!=(char *)NULL)                 /* crude user input */
        if(!n_texis(se,"%s;",buf))     /* add required ';' for the user */
             puts("ERROR in n_texis");
     ...
    }

Your system’s ``printf()`` man page for the format string ``%`` codes.

::

    n_settexisparam()

::

    TX *n_opentx(SERVER *se);
    TX *n_duptx(SERVER *se,TX *tx);
    TX *n_closetx(SERVER *se,TX *tx);

These functions provide an alternative to ``n_texis()``. They allow the
same style of SQL statements via ``n_settx()``, but maintain the
connection to the database for performing multiple queries without
constant reopens. This improves the efficiency of executing multiple
statements against the same database.

``n_opentx()`` opens the database specified in the last call to
``n_setdatabase()``. It returns a valid TX pointer on success or
``TXPN`` on failure. ``TXPN`` is an alias for ``(TX *)NULL``.

``n_duptx()`` creates a new TX pointer to the same database as a
currently valid handle. This saves much of the overhead of opening a new
connection to the database. The returned handle is a clean TX handle,
and will not have a copy of the SQL statement from the copied handle. It
returns a valid TX pointer on success or ``TXPN`` on failure. ``TXPN``
is an alias for ``(TX *)NULL``.

``n_closetx()`` closes the previously opened database. It always returns
``TXPN``.

SQL statements are setup and executed with ``n_settx()``, ``n_runtx()``,
and ``n_gettx()``.

::

    n_settx(), n_runtx(), and n_gettx()

::

    TX  *n_settx(SERVER *se,TX *tx,char *queryformat,...);
    int  n_runtx(SERVER *se,TX *tx);

These functions perform SQL statement setup and execution for databases
opened with ``n_opentx()``. ``n_settx()`` takes a ``TX`` pointer from
``n_opentx()``, a printf style format string, and the arguments to fill
in that format string with.

The query will be constructed using the format string and arguments,
parsed, and prepared for execution. ``n_settx()`` will return the same
``TX`` passed to it on success. It will return ``TXPN`` on error.

``n_runtx()`` will execute the statement prepared with ``n_settx()``. At
this point what you said will begin to happen and your callback will be
called as appropriate. When this function returns execution is complete
and another ``n_settx()`` should be performed before running again. It
will return zero on error and non-zero on success.

::

    SERVER *se;
    TX *tx;

       ...
       if((tx=n_opentx())!=TXPN)      /* initialize database connection */
       {
          ...
                                                         /* setup query */
          if(n_settx(se,tx,
                     "select NAME from SYSTABLES where CREATOR!='texis';"
                    )!=TXPN)
             n_runtx(se,tx);                           /* execute query */
          ...
                                                 /* setup another query */
          if(n_settx(se,tx,
                    "select NAME,TYPE from SYSCOLUMNS where TBNAME='image';"
                    )!=TXPN)
             n_runtx(se,tx);                         /* execute a query */
          ...
          n_closetx(tx);                   /* close database connection */
       }
       ...

::

    n_opentx(), n_gettx()

::

    int n_preptx(SERVER *se,TX *tx,char *queryfmt,...);
    int n_exectx(SERVER *p_se,TX *tx);

These functions provide an efficient way to perform the same SQL
statement multiple times with varying parameter data.

``n_preptx()`` will perform SQL statement setup. It takes a ``TX``
pointer from ``n_opentx()``, a printf style format string, and the
arguments to fill in that format string with.

The query will be constructed using the format string and arguments,
parsed, and prepared for execution. ``n_preptx()`` will return non-zero
on success. It will return zero on error.

``n_exectx()`` will begin execution of the SQL statement. It will return
non-zero on success and zero on error. ``n_runtx()`` or ``n_gettx()`` or
``n_flushtx()`` would then be called to handle the results of the
statement as with ``n_settx()``.

Once a SQL statement is prepared with ``n_preptx()`` it may be executed
multiple times with ``n_exectx()``. Typically the parameter data is
changed between executions using the ``n_paramtx()`` function.

::

    SERVER *se;
    TX     *tx;
    long    date;
    char   *title;
    char   *article;
    int     tlen, alen, dlen;

       ...
       if(!n_preptx(se,tx,"insert into docs values(counter,?,?,?);"))
          { puts("n_preptx Failed"); return(0); }
       for( each record to insert )
       {
          ...
          date=...
          dlen=sizeof(date);
          title=...
          tlen=strlen(title);
          article=...
          alen=strlen(article);
          if(!n_paramtx(se,tx,1,&date  ,&dlen,SQL_C_LONG,SQL_DATE       ) ||
             !n_paramtx(se,tx,2,title  ,&tlen,SQL_C_CHAR,SQL_LONGVARCHAR) ||
             !n_paramtx(se,tx,3,article,&alen,SQL_C_CHAR,SQL_LONGVARCHAR));
             { puts("n_paramtx Failed"); return(0); }
          if(!n_exectx(se,tx))
             { puts("n_exectx Failed"); return(0); }
          n_flushtx(se,tx);
       }
       ...

::

    SERVER *se;
    TX     *tx;
    char   *query;
    int     qlen;
    FLDLST *fl;

       ...
       if(!n_preptx(se,tx,
             "select id,Title from docs where Article like ?;"))
          { puts("n_preptx Failed"); return(0); }
       for( each Article query to execute )
       {
          query=...
          qlen=strlen(query);
          if(!n_paramtx(se,tx,1,query,&qlen,SQL_C_CHAR,SQL_LONGVARCHAR))
             { puts("n_paramtx Failed"); return(0); }
          if(!n_exectx(se,tx))
             { puts("n_exectx Failed"); return(0); }
          while((fl=n_gettx(se,tx))!=FLDLSTPN)
          {
             ...
             freefldlst(fl);
          }
       }
       ...

::

    n_paramtx(), n_opentx(), n_gettx()

::

    FLDLST *n_gettx(SERVER *se,TX *tx);
    FLDLST *freefldlst(FLDLST *fl);

This function provides a non-callback version of SQL execution.
``n_gettx()`` returns a ``FLDLST`` pointer which is the same as would
normally be passed to a callback function. You process it just as you
would in a callback.

Continue calling ``n_gettx()`` to get subsequent result rows.
``n_gettx()`` will return ``FLDLSTPN`` when there are no more result
rows.

Each returned ``FLDLST`` must be freed using the ``freefldlst()`` when
it is no longer needed.

::

    SERVER *se;
    TX     *tx;
    FLDLST *fl;

       ...
       if((tx=n_opentx())!=TXPN)      /* initialize database connection */
       {
          ...
                                                         /* setup query */
          if(n_settx(se,tx,
                     "select NAME from SYSTABLES where CREATOR!='texis';"
                    )!=TXPN)
             while((fl=n_gettx(se,tx))!=FLDLSTPN)/* get next result row */
             {
                dispfields(fl);            /* display all of the fields */
                freefldlst(fl);                      /* free the memory */
             }
          ...
          n_closetx(tx);                   /* close database connection */
       }
       ...

::

    n_settx(), n_runtx(), n_regtexiscb()

::

    int n_settexisparam(se, ipar, buf, len, ctype, sqltype)
    SERVER  *se;
    int     ipar;
    void    *buf;
    int     *len;
    int     ctype;
    int     sqltype;

    int n_paramtx(se, tx, ipar, buf, len, ctype, sqltype)
    SERVER  *se;
    TX      *tx;
    int     ipar;
    void    *buf;
    int     *len;
    int     ctype;
    int     sqltype;

    int n_resetparamtx(se, tx)
    SERVER  *se;
    TX  *tx;

These functions allow you to pass arbitrarily large or complex data into
a SQL statement. Sometimes there is data that won’t work in the confines
of the simple C string that comprises an SQL statement. Large text
fields or binary data for example.

Call ``n_settexisparam()`` to setup the parameter data before calling
``n_texis()`` or ``n_settx()`` to prepare the SQL statement. Call
``n_paramtx()`` to setup parameters after ``n_preptx()`` and before
``n_exectx()``. If you have a statement you have already executed once,
and you want to execute again with different data, which may have
parameters unset which were previously unset you can call
``n_resetparamtx()``. This is not neccessary if you will explicitly set
all the parameters. Place a question mark (``?``) in the SQL statement
where you would otherwise place the data.

These are the parameters:

SERVER \*se
    The open server connection.

TX \*tx
    The prepared SQL statement (``n_paramtx()`` only).

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

    SERVER *se;
    TX     *tx;
    char   *description;
    char   *article;
    int     len;

       ...
       description="a really large article";
       article=...
       len=...
       if(!n_settexisparam(se,1,article,&len, SQL_C_CHAR, SQL_LONGVARCHAR))
          puts("n_settexisparam failed");
       else
       if(!n_texis(se,"insert into docs values('%s',?);",description))
          puts("insert failed");
       ...

::

    SERVER *se;
    TX     *tx;
    char   *description;
    char   *article;
    int     lend, lena;

       if(!n_preptx(se,tx,"insert into docs values(?,?);"))
          { puts("n_preptx Failed"); return(0); }
       ...
       description="a really large article";
       lend=strlen(description);
       article=...
       lena=strlen(article);
       if(!n_paramtx(se,tx,1,description,&lend,SQL_C_CHAR,SQL_LONGVARCHAR)||
          !n_paramtx(se,tx,2,article    ,&lena,SQL_C_CHAR,SQL_LONGVARCHAR));
          { puts("n_paramtx Failed"); return(0); }
       if(!n_exectx(se,tx))
          { puts("n_exectx Failed"); return(0); }
       ...

::

    int n_flushtx(se,tx);
    SERVER  *se;
    TX      *tx;

This function flushes any remaining results from the current SQL
statement. Execution of the statement is finished however. This is
useful for ignoring the output of ``INSERT`` and ``DELETE`` statements.

::

    SERVER *se;

       ...
                                                         /* setup query */
       if(n_settx(se,tx,
                  "delete from customer where lastorder<'1990-01-01';"
                 )!=TXPN)
          n_flushtx(se,tx);                        /* ignore result set */
       ...

::

    int n_flushtx2(se,tx,nrows,max);
    SERVER  *se;
    TX      *tx;
    int     nrows;
    int     max;

This function flushes up to nrows results from the specified SQL
statement. This is useful for skipping a number of records from a
``SELECT``. The max is an suggestion to the SQL engine of how many
records you intend to read.

The return will be the number of records actually flushed if successful,
or if an error occurred then the return will be a negative number of (-1
- rowsflushed). Reaching the end of the results is not considered an
error, and will result in a return less than nrows.

::

    SERVER *se;

       ...
                                                         /* setup query */
       if(n_settx(se,tx,
                  "select name from customer where lastorder<'1990-01-01';"
                 )!=TXPN)
          n_flushtx2(se,tx,10,10);           /* ignore first 10 results */
       ...

::

    MMOFFS *n_offstx(se,tx,fieldname);
    SERVER  *se;
    TX      *tx;
    char    *fieldname;

This function returns any and all Metamorph subhit offsets for the named
field. It returns ``MMOFFSPN``\  [5]_ if there are none. See
``n_fillsrchlst()`` for a description of the ``MMOFFS`` structure, and
why there may be no hit information. The returned structure must be
freed with ``freemmoffs()`` when no longer needed. It is safe to pass
``MMOFFSPN`` to ``freemmoffs()``.

::

    SERVER *se;
    TX     *tx;
    FLDLST *fl;
    MMOFFS *mmo;

       ...
                                                         /* setup query */
       if(n_settx(se,tx,
           "select desc,text from docs where text like 'power struggle';",
           )!=TXPN)
          while((fl=n_gettx(se,tx))!=FLDLSTPN)   /* get next result row */
          {
             mmo=n_offstx(se,tx,"text"); /* get Metamorph info for text */
             dispfields(fl,mmo);/* display the fields, hilighting subhits */
             freemmoffs(mmo);                    /* free Metamorph info */
          }
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

.. [1]
   ``SERVERPN`` is a synonym for ``(SERVER*)NULL``

.. [2]
   ``TSQLPN`` is an alias for ``(TSQL *)NULL``.

.. [3]
   ``MMOFFSPN`` is an alias for ``(MMOFFS *)NULL``

.. [4]
   ``XPMIPN`` is a synonym for ``(XPMI*)NULL``

.. [5]
   ``MMOFFSPN`` is an alias for ``(MMOFFS *)NULL``
