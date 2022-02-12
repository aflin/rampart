The rampart-sql Command Line Utilities
--------------------------------------

Several command line utilities are included with the Rampart SQL module.
They include ``tsql``, ``kdbfchk``, ``addtable``, ``rex`` and ``metamorph``.

Note that the ``texislockd`` is used by tsql/rampart-sql to coordinate locks
and is run automatically.  It should stay in the provided directory or
otherwise be available in the systems ``PATH``.


The tsql Command Line Utility
=============================

The tsql utility is the main program provided for interactive use of a
Rampart sql database.  It should either be executed the database directory,
or else specified on the command line as the -d <database> option.

If a query is present on the command line then tsql will execute that
statement, and display the results on stdout (the screen).  If no query is
present then queries will be accepted on stdin (the keyboard) in an
interactive SQL shell. Queries on stdin must be terminated by a semicolon. 
To exit from tsql you should produce EOF on it’s stdin.  On Unix systems
this is usually done with Control-D (a Control-C should also exit the
program).

The tsql utility also provides facilities for doing many administrative tasks.

::

    Usage: tsql [-a command] [-c [-w width]] [-l rows] [-hmqrv?]
                [-d database] [-u username] [-p password] [-i file]
                [-R profile] sql-statement

    Options:
      --install-dir[-force]{=| }dir    Alternate installation dir
                                   (default is `/usr/local/morph3')
      --texis-conf{=| }file            Alternate conf/texis.ini file
      -a command       Enter Admin mode; respects -d -u -p
          Commands:
             (A)dd     add a user
             (C)hange  change a password
             (D)elete  delete a user
      -h               Suppress column headings
      -v               Display inserted/deleted rows
      -n               Display total row count with -v
      -q               Suppress SQL> prompt
      -c               Format one field per line
      -w width         Make field headings width characters long
                       (0: align to longest heading)
      -l rows          Limit output to rows
      -s rows          Skip rows of output
      -u username      Login as username
                       (if -p not given, password will be prompted for)
      -p password      Login using password
      -P password      _SYSTEM password for admin
      -d database      Use database as the data dictionary
      -m               Create the database named with -d
      -i file          Read SQL commands from file instead of the keyboard
      -r               Read default profile
      -R profile       Read specified profile
      -f delim         Specify a delimiter or format; 1 or 2 chars:
         t             same as -c option
         c             default behavior
         other char    field separator e.g. `-f ,' for CSV
                       (follow with q/n to suppress quotes/newlines)
      -t               Show timing information
      -V               Increase Texis verbosity (may be used multiple times)
      -x               Debug: do not capture ABEND etc. signals
      --show-counts    Show min/max counts of rows matched and returned
      --lockverbose n  Set lockverbose (tracing) level n
      --timeout n[.n]  Set timeout of n[.n] seconds (-1 none; may cause corruption)
      -?               Show this help

The kdbfchk Command Line Utility
================================

The kdbfchk utility scans database tables (files ending in ``.tbl``) for
errors, and optionally writes a repaired file which may be added or readded
to the Database using ``addtable`` below.

::

    Usage:  kdbfchk [options] <infile> [<infile> ...]

    Default action is to scan given <infile>s.

    Other actions (offset/size values are decimal or 0x hex):
      -q                  Quick fix: skip orphan scan, just repair tail of file
      -d <off> [<totsz>]  Delete/free block at offset <off>; <totsz> forces write
                          of total size (including header) <totsz> (caution!).
      -p <off> [<totsz>]  Print block data at offset <off>.  <totsz> forces size.
      -r <hexfile> <off> [<totsz>]  Replace block data at offset <off> with hex
                          dump data from <hexfile> (e.g. edited -p output).
                          <totsz> forces total size and raw write (no KDBF header).
      -l [<off>]          List data blocks' offsets and data sizes, optionally
                          starting at valid offset <off> (default start of file)
      -L [<off>]          Scan for any headers, starting at offset <off> (default
                          start of file).  Lower -v values print less info.
    Options:
      --install-dir[-force]{=| }<dir> Alternate installation <dir>
                          (default is `')
      --texis-conf{=| }<file>         Alternate conf/texis.ini <file>
      -o <outfile>        Output repaired file to <outfile>
      -O                  Overwrite <infile> (required for -q, -d, -r)
      -s                  Save truncated data blocks instead of deleting
      -k                  Assume file is KDBF even if it doesn't look like KDBF
      -i                  Ignore orphaned free blocks in scan (i.e. assume bad)
      -f <file>           Print non-data (free) blocks info to <file>
      -b <file>           Print bad blocks info to <file>:
         -n               Don't list orphaned free blocks/pages
         -m <n>           Limit info to <n> messages (default 1000, 0 = no limit)
      -t <dir>            Use <dir> as temporary directory for internal tree
      -bufsz <n>          Use disk buffer size <n> (default 128K)
      -a                  Align hex dumps on 1-byte instead of 16-byte boundary
      -v <n>              Set verbosity level <n> (default 2):
         0                No output except severe errors
         1                Also print current filename and all corruption info
         2                Also print progress meter
      -M none|simple|pct  Meter type to print
      -version            Print version information
      -h                  Print this message
    Exit codes:
       0                  File checks ok
      23                  Incorrect usage
      26                  File is not KDBF
      27                  Internal error
      28                  Unknown error
      29                  File is corrupt
      45                  Cannot write to file

The addtable Command Line Utility
=================================

A table file (created with tsql or the SQL module and ending in ``.tbl``)
from another database or as repaired using ``kdbfchk`` above may be added to
the database usin the ``addtable`` command.  Note that the table must have
been created on a similar system (32 vs 64 bit).  Note also that after
adding a table, any indexes which existed on the original table (either from
another database, or as repaired by ``kdbfchk``) will need to be recreated.

::

   Usage: addtable [-d database] [-l tablename] [-c comment] [-u user] [-p password] [-b bits] filename
        --install-dir[-force]{=| }dir   Alternate installation dir
                        (default is `')
        --texis-conf{=| }file           Alternate conf/texis.ini file
        -h              This help summary.
        -d database     Database to add table to.
        -l tablename    Name of table within Texis.
        -c comment      Comment to put in SYSTABLES.
        -u user         Username.
        -p password     Password.
        -b bits File size bits file created with (e.g. 32).
        <filename>      File to add.

The rex Command Line Utility
============================

::

   The rex utility locates and prints lines containing occurrences of
   regular expressions.  If files are not specified, standard input is used. 
   If files are specified, the filename will be printed before the line
   containing the expression if the "-n" option is not used.

   SYNTAX

       rex [options] expression [files]

   OPTIONS
       -c       Do not print control characters; replace with space.
       -C       Count the number of times the expression occurs.
       -l       List file names that contain the expression.
       -E"EX"   Specify and print the ending delimiting expression.
       -e"EX"   Specify the ending delimiting expression.
       -S"EX"   Specify and print the starting delimiting expression.
       -s"EX"   Specify the starting delimiting expression.
       -p       Begin printing at the start of the expression.
       -P       Stop printing at the end of the expression.
       -r"STR"  Replace the expression with "STR" to standard output.
       -R"STR"  Replace the expression with "STR" to original file.
       -t"Fn"   Use "Fn" as the temporary file (default: "rextmp").
       -f"Fn"   Read the expression(s) from the file "Fn".
       -n       Do not print the file name.
       -O       Generate "FNAME@OFFSET,LEN" entries for mm3 subfile list.
       -x       Translate the expression into pseudo-code (debug).
       -v       Print lines (or delimiters) not containing the expression.

    o  Each option must be placed individually on the command line.

    o  "EX"  is a REX expression.

    o  "Fn"  is a file name.

    o  "STR" is a replacement string.

See :ref:`the rex function <rampart-sql:rex()>` and 
:ref:`the sandr function <rampart-sql:sandr()>` for details.

The metamorph Command Line Utility
==================================

The metamorph command performs full texis searches against files.  It is
similar to the :ref:`searchFile <rampart-sql:searchFile()>` command.

::

   Usage:
        metamorph [-option=value [...]] "query" filename(s)
   Where:
        "query" is any valid Metamorph query
        filename is the name of the file(s) to be searched. (default stdin)

