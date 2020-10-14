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
done with Control-D, and on Windows with Control-Z.

Tsql also provides facilities for doing many administrative tasks.

**Options**

To aid in the use of tsql from scripts or as parts of a chain of
commands there are many options available. The options are

-h
    Don’t display the column headers.

-q
    Don’t display the SQL prompt and copyright statement.

-c
    Change the format to one field per line.

-w width
    Make the field headings width characters long. 0 means use the
    longest heading’s width.

-l rows
    Limit output to the number of rows specified.

-u username
    Login as username. If the -p option is not specified then you will
    be prompted for a password. The user must have been previously added
    to the system. This forces usage of the Texis permission scheme. If
    this is not specified then the name defaults to PUBLIC. See
    Chapter [Chp:Sec] for more information.

-p password
    Use password to log in.

-P password
    \_SYSTEM password to log in for admin.

-d database
    Specify the location of the database.

-m
    Create the named database. See creatdb.

-i file
    Read SQL commands from file instead of stdin. Commands read this way
    will echo to stdout, whereas commands read from input redirection
    would not.

-r
    Read the default profile file.

-R profile
    Read the specified profile file.

-f
    Specify a format. One or two characters can follow with the
    following meanings.

    t
        same as ``-c`` option

    c
        default behaviour

    any other character
        is a field separator character. This can be followed by q to
        suppress the quotes around the fields. To get quoted comma
        separated values you would use ``-f ,``

-?
    Print a command summary.
