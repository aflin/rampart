Overview
========

::

    SERVER *openserver(char *hostname,char *port)
    SERVER *closeserver(SERVER *serverhandle)
    int     serveruser(char *username);
    int     servergroup(char *groupname);
    int     serverpass(char *password);

**Hit registration**

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

**Object manipulation**

::

    BLOB *newblob(char *database,char *table,char *buf,int n);
    BLOB *putblob(BLOB *bl,char *buf,int n);
    BLOB *closeblob(BLOB *bl);
    int   getblob(BLOB *bl,char **buf,int *n);
    char *newindirect(char *database,char *table,char *filename);

**Texis control parameters**

::

    int n_setdatabase(SERVER *se,char *database);
    int n_getdatabase(SERVER *se,char *database);

**SQL interface**

::

    int n_texis(SERVER *se,char *queryformat,...);

The Texis API provides the client program with an easy to use interface
to ant Texis server without regard to machine type. This is the
preferred interface and completely unrelated to the ODBC interface which
is documented elswhere.

The programmer is *strongly* encouraged to play with the example progams
provided with the package before attempting their own implementation.

Server Connection Management
    These functions allow you to connect and disconnect from a server.
    The return value from ``openserver()`` is a ``SERVER *``, and will
    be used as the first parameter in all subsequent calls to the
    Server.

Hit registration
    These functions tell the server which client function to “*call
    back*” when it has located information pertinent to the query.

Hit information
    These functions allow you to obtain detailed information about any
    Metamorph queries that may have been used in the query.

Object manipulation
    These functions allow you to manipulate indirect and blob fields
    within a Texis database.

Texis control parameters
    This set of functions is for getting and changing the various
    operational parameters that define how a remote Texis performs.

SQL interface
    This group of operations is for passing a user’s query onto the
    server for processing and subsequently obtaining the results.

Network API common functions
============================

::

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
of the machine on which the reqested service is running. For example:
``thunder.thunderstone.com`` or ``198.49.220.81``. The port number is
also a string, and is the port number assigned to the service when it is
brought up initially on the server machine. The port number may also be
assigned a name of a service that is enumerated in the file
``/etc/services``.

Either ``hostname`` or ``port`` or both may be the empty string (``""``)
indicating that the compiled in default is to be used. The default for
``hostname`` is ``"localhost"`` indicating the same machine that the
client application is running on. The default ``port`` is ``"10000"``
for Metamorph, ``"10001"`` for 3DB and ``"10002"`` for Texis.

The ``serveruser()``, ``servergroup()``, and ``serverpass()`` calls set
the user name, group name, and password, respectively, that
``openserver()`` will use when logging into the server. These functions
will return zero on error. Non-zero is returned on success. If
``servergroup()`` is not used, the user will be logged into their
default group as defined by the server.

The default user name and password are both the empty string (``""``).

::

     SERVER *se;                                  /* network server pointer */
     SRCH   *sr;                                          /* search pointer */

                                                   /* connect to the server */
     if(serveruser("me")         &&
        servergroup("somegroup") &&
        serverpass("mypassword") &&
        (se=openserver("thunder.thunderstone.com","666"))!=SERVERPN)
        {
         n_reghitcb(se,(void *)NULL,mycallback);      /* setup hit callback */
         if((sr=n_setqry(se,"power struggle"))!=SRCHPN)  /* setup the query */
             {
              if(!n_search(se,sr))                         /* find all hits */
                  puts("n_search() failed");
              n_closesrch(se,sr);                      /* cleanup the query */
             }
         closeserver(se);                         /* disconnect from server */
        }

Make sure you are talking to the right port!

``/etc/services``, ``services(4)``

Texis specific functions
========================

::

    #include "tstone.h"
    FLDLST
    {
     int    n;              /* number of items in each of following lists */
     int   *type;                             /* types of field (FTN_xxx) */
     void **data;                                   /* data array pointer */
     int   *ndata;                    /* number of elements in data array */
     char **name;                                        /* name of field */
    };
    int n_regtexiscb(SERVER *se,void *usr,func cb(void *usr,TEXIS *tx,FLDLST *fl));

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

Possible types in ``FLDLST->type`` array:

``FTN_BYTE`` — An 8 bit ``byte``.

``FTN_CHAR`` — A ``char``.

``FTN_DOUBLE`` — A ``double``.

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

``FTN_DATE`` — A ``time_t`` in the same form as that from the ``time()``
system call.

The ``type`` may also be ``|``\ ed with ``FTN_NotNullableFlag``
(formerly ``DDNULLBIT``) and/or ``DDVARBIT``. If ``FTN_NotNullableFlag``
is set, it indicates that the field is not allowed to be NULL.
``DDVARBIT`` indicated that the field is variable length instead of
fixed length. When handling hit results these bits can generally be
ignored.

``n_regtexiscb()`` will return true if the registration was successful,
and will return false (0) on error.

::

    #include "tstone.h"

    #define USERDATA my_data_structure
    USERDATA
    {
     FILE   *outfile;  /* where I will log the hits */
     long   hitcount;  /* just for fun */
    };

    void
    dispnames(ud,fl)
    USERDATA *ud;
    FLDLST *fl;
    {
     int i;

     for(i=0;i<fl->n;i++)                                    /* every field */
        printf("%s ",fl->name[i]);
     putchar('\n');                                      /* end header line */
    }

    void
    dispfields(ud,fl)
    USERDATA *ud;
    FLDLST *fl;
    {
     int i, j;

     for(i=0;i<fl->n;i++)
        {
         int   type=fl->type[i];                       /* the type of field */
         void *p   =fl->data[i];                     /* pointer to the data */
         int   n   =fl->ndata;                         /* how many elements */
         if(i>0) putchar('\t');      /* tab between fields, but not leading */
         switch(type&~(DDNULLBIT|DDVARBIT))/* ignore NULL and VARIABLE bits */
            {
                                /* loop through each element of field via j */
                                        /* cast and print according to type */
             case FTN_BYTE    : for(j=0;j<n;j++)
                                 printf("0x%x ",((byte *)p)[j]);
                                break;
             case FTN_INDIRECT: /*nobreak; just print the filename as a string*/
             case FTN_CHAR    : for(j=0;j<n && ((char *)p)[j]!='\0';j++)
                                 printf("%c"  ,((char *)p)[j]);
                                break;
             case FTN_DOUBLE  : for(j=0;j<n;j++)
                                 printf("%lf ",((double *)p)[j]);
                                break;
             case FTN_DWORD   : for(j=0;j<n;j++)
                                 printf("%lu ",(unsigned long)((dword *)p)[j]);
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
                                 printf("%u " ,(unsigned int)((word *)p)[j]);
                                break;
                 /* assume exactly one element on FTN_DATE for this example */
             case FTN_DATE    : printf("%.25s",ctime(p));   /* human format */
                                break;
             default          : printf("unknowntype(%d)",type);
            }
        }
    }

    int
    hit_handler(usr,tx,fl)
    void *usr;  /* my USERDATA POINTER */
    TEXIS *tx;  /* texis api handle */
    FLDLST *fl; /* The field list data structure */
    {
     USERDATA *ud=(USERDATA *)usr;      /* cast the void into the real type */

     ++ud->hitcount;                          /* add one to the hit counter */

     if(ud->hitcount==1)                            /* before the first hit */
        dispnames(ud,fl);                 /* display all of the field names */
     dispfields(ud,fl);                        /* display all of the fields */

     if(ud->hitcount>=100) /* tell the server to stop if I've seen too many */
        return(0);
     return(1);              /* tell the server to keep giving me more hits */
    }

    int
    main()
    {
     SERVER  *se;
     USERDATA mydata;
     ...
     mydata.outfile=(FILE *)NULL;
     mydata.hitcount=0;
     n_regtexiscb(se,mydata,hit_handler);
     ...
    }

The example program ``netex3.c``.

::

    #include "tstone.h"
    SRCHLST
    {
     int n;                                  /* number of elements in lst */
     SRCH lst[];                            /* list of Metamorph searches */
    };
    SRCHLST *n_getsrchlst(SERVER *se,TEXIS *tx);
    SRCHLST *n_freesrchlst(SERVER *se,SRCHLST *sl);

    SRCHI
    {
     char *what;                                 /* what was searched for */
     char *where;                                       /* what was found */
     int   len;                                 /* length of where buffer */
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

``n_xpminfo()`` and its example.

::

    #include "tstone.h"
    XPMI *n_xpminfo(SERVER *se,SRCH *sr,int index);
    XPMI *n_freexpmi(SERVER *se,XPMI *xi);

These functions may be used within the hit callback function to obtain
detailed information about any search terms that may have used the
approximate pattern matcher (XPM). ``n_xpminfo()`` is called with the
index of the desired XPM.

It returns a structure containing everything about that XPM. It returns
XPMIPN [2]_ if index is out of bounds.

To get all XPM subhits put ``n_xpminfo()`` in a loop with index starting
at zero and incrementing until ``XPMIPN`` is returned.

Each valid structure returned by ``n_xpminfo()`` should be freed by
calling ``n_freexpmi()`` when it is no longer needed.

The XPMI structure contains the following members:

::

    XPMI                                                      /* XPM Info */
    {
     word  thresh;               /* threshold above which a hit can occur */
     word  maxthresh;                            /* exact match threshold */
     word  thishit;                               /* this hit's threshold */
     word  maxhit;                        /* max threshold located so far */
     char *maxstr;                      /* string of highest value so far */
     char *srchs;                              /* string being search for */
    };

Don’t expect ``XPMI.thresh`` to be the percentage entered in the query
passed to ``n_setqry()``. It is an absolute number calculated from that
percentage and the search string.

::


    int
    hit_handler(usr,tx,fl)
    void *usr;  /* my USERDATA POINTER */
    TEXIS *tx;  /* texis api handle */
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
     sl=n_getsrchlst(se,tx);          /* get list of Metamorphs for query */
     if(sl!=SRCHLSTPN)
        {
         for(i=0;i<sl->n;i++)                       /* for each Metamorph */
             {
              SRCH *sr= &sl->lst[i];                             /* alias */
                                                /* loop thru all sub-hits */
                  /* the zero index for n_srchinfo is the whole hit       */
                  /* the one  index for n_srchinfo is the start delimiter */
                  /* the two  index for n_srchinfo is the end delimiter   */
                  /* the remaining indicies are the subhits               */
              for(j=0;(si=n_srchinfo(se,sr,j))!=SRCHIPN;j++)
                  {
                   char *p, *e;                /* scratch buffer pointers */
                   switch(j)
                   {
                    case 0 :printf(" HIT    (%s):%d:",si->what,si->len); break;
                    case 1 :printf(" S-DELIM(%s):%d:",si->what,si->len); break;
                    case 2 :printf(" E-DELIM(%s):%d:",si->what,si->len); break;
                    default:printf(" SUB-HIT(%s):%d:",si->what,si->len); break;
                   }
                   for(p=si->where,e=p+si->len;p<e;p++)
                       if(*p<32 && *p!='\n' && *p!='\t') printf("\\x%02x",*p);
                       else                              putchar(*p);
                   printf("\n");
                   n_freesrchi(se,si);              /* free any mem in si */
                  }
              for(k=0;(xi=n_xpminfo(se,sr,k))!=XPMIPN;k++)/* loop thru all XPMs */
                 {
                  printf("XPM: \"%s\": thresh %u, maxthresh %u, thishit %u\n",
                         xi->srchs,xi->thresh,xi->maxthresh,xi->thishit);
                  printf("   : maxhit %u, maxstr \"%s\"\n",xi->maxhit,xi->maxstr);
                  n_freexpmi(se,xi);                    /* free mem in xi */
                 }
             }
         n_freesrchlst(se,sl);
        }
     ...

     return(1);              /* tell the server to keep giving me more hits */
    }

The example program ``netex1.c``, ``n_reghitcb()``, ``n_getsrchlst()``,
``n_srchinfo()``.

::

    BLOB *newblob(char *database,char *table,char *buf,int n);
    BLOB *putblob(BLOB *bl,char *buf,int n);
    BLOB *closeblob(BLOB *bl);
    int   getblob(BLOB *bl,char **buf,int *n);
    char *url=newindirect(char *database,char *table,char *fn);

These functions allow you to manipulate indirect and blob fields within
a Texis database.

``newblob()`` will create a new blob associated with the specified
database and table, and copy the buffer passed into the blob. The ``n``
parameter says how many bytes from buf to copy. It returns the handle of
the created blob.

``putblob()`` will put the contents of the buffer into the blob. The
``n`` parameter says how many bytes from buf to copy. It returns the
blob where the data was actually put.

**Warning:** The returned ``BLOB`` may be different than that passed to
``putblob()``. If so, the passed ``BLOB`` will then be invalid.

``closeblob()`` cleans up the memory associated with the blob handle.
The blob itself is left intact. It returns ``BLOBPN``.

``getblob()`` retrieves data from a blob. It will read in the data from
the blob, and return an allocated buffer containing the data, and the
number of bytes actually read in. ``*buf`` will be set to the buffer
pointer. ``*n`` will be set to the number of bytes in ``*buf``. It
returns non-0 on success, 0 on failure.

``newindirect()`` generates a url that can be used to store data on the
server. If ``fn`` is ``NULL`` or ``""`` it will create a url which can
be used to store data in that is owned by Texis. If ``fn`` is not
``NULL`` and not ``""`` and is not a url already then it will be made
into a url owned by you. If url is a full path, it will be respected.
Otherwise the standard path of indirect files for the table will be
prepended.

``newindirect()`` returns a url that can be stored into. The url that is
returned is an allocated string that **MUST** be freed by the caller.

The blob handles and urls returned by these functions may then be used
as the field contents of blob and indirect fields respectively.

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

    #include "tstone.h"
    main(argc,argv)
    int argc;
    cahr **argv;
    {
     SERVER *se;
     char buf[80];
     USERDATA mydata;

     ...
     n_regtexiscb(se,mydata,hit_handler);           /* setup hit callback */
     n_setdatabase(se,argv[1]);                 /* set database to search */
     while(gets(buf)!=(char *)NULL)                   /* crude user input */
        if(!n_texis(se,"%s;",buf))       /* add required ';' for the user */
             puts("ERROR in n_texis");
     ...
    }

.. [1]
   ``SERVERPN`` is a synonym for ``(SERVER*)NULL``

.. [2]
   ``XPMIPN`` is a synonym for ``(XPMI*)NULL``
