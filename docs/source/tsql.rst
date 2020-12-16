Tsql Command Line Utility
=========================
::

    tsql [-a command] [-c [-w width]] [-l rows] [-hqm?] [-d database]
    [-u username] [-p password] [-i file] ["SQL statement"]

Tsql is the main program provided for interactive use of a Texis
database. You should either be in the database directory, or else
specify it on the command line as the -d <database> option.

If a query is present on the command line than tsql will execute that
statement, and display the results on stdout (the screen). If no query
is present then queries will be accepted on stdin (the keyboard).
Queries on stdin must be terminated by a semicolon. To exit from tsql
you should produce EOF on it’s stdin. On Unix systems this is usually
done with Control-D (a Control-C should also exit the program).

Tsql also provides facilities for doing many administrative tasks.

**Options**

To aid in the use of tsql from scripts or as parts of a chain of
commands there are many options available, which may be viewed by
executing ``tsql -help`` or ``tsql -?`` on the command line:

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
