
Server functions
----------------

The Texis server has a number of functions built into it which can
operate on fields. This can occur anywhere an expression can occur in a
SQL statement. It is possible that the server at your site has been
extended with additional functions. Each of the arguments can be either
a single field name, or another expression.


General Functions
~~~~~~~~~~~~~~~~~


exec
""""

Execute an external command. The syntax is

.. code-block:: sql

       exec(commandline[, INPUT[, INPUT[, INPUT[, INPUT]]]]);

Allows execution of an external command. The first argument is the
command to execute. Any subsequent arguments are written to the standard
input of the process. The standard output of the command is read as the
return from the function.

This function allows unlimited extensibility of Texis, although if a
particular function is being used often then it should be linked into
the Texis server to avoid the overhead of invoking another process.

For example this could be used to OCR text. If you have a program which
will take an image filename on the command line, and return the text on
standard out you could issue SQL as follows:

.. code-block:: sql

         UPDATE     DOCUMENTS
         SET        TEXT = exec('ocr '+IMGFILE)
         WHERE      TEXT = '';

Another example would be if you wanted to print envelopes from names and
addresses in a table you might use the following SQL:

.. code-block:: sql

         SELECT exec('envelope ', FIRST_NAME+' '+LAST_NAME+'
         ', STREET + '
         ', CITY + ', ' + STATE + ' ' + ZIP)
         FROM ADDRESSES;

Notice in this example the addition of spaces and line-breaks between
the fields. Texis does not add any delimiters between fields or
arguments itself.


mminfo
""""""

This function lets you obtain Metamorph info. You have the choice of
either just getting the portions of the document which were the hits, or
you can also get messages which describe each hit and subhits.

The SQL to use is as follows:

.. code-block:: sql

        SELECT mminfo(query,data[,nhits,[0,msgs]]) from TABLE
               [where CONDITION];

query
    Query should be a string containing a metamorph query.

data
    The text to search. May be literal data or a field from the table.

nhits
    The maximum number of hits to return. If it is 0, which is the
    default, you will get all the hits.

msgs
    An integer; controls what information is returned. A bit-wise OR of
    any combination of the following values:

    -  1 to get matches and offset/length information

    -  2 to suppress text from ``data`` which matches; printed by
       default

    -  4 to get a count of hits (up to ``nhits``)

    -  8 to get the hit count in a numeric parseable format

    -  16 to get the offset/length in the original query of each search
       set

    Set offset/length information (value 16) is of the form:

    ::

        Set N offset/len in query: setoff setlen

    Where ``N`` is the set number (starting with 1), ``setoff`` is the
    byte offset from the start of the query where set ``N`` is, and
    ``setlen`` is the length of the set.

    Hit offset/length information is of the form:

    ::

        300 <Data from Texis> offset length suboff sublen [suboff sublen]..
        301 End of Metamorph hit

    Where:

    -  offset is the offset within the data of the overall hit context
       (sentence, paragraph, etc...)

    -  length is the length of the overall hit context

    -  suboff is the offset within the hit of a matching term

    -  sublen is the length of the matching term

    -  suboff and sublen will be repeated for as many terms as are
       required to satisfy the query.



    Example:

    ::

       select mminfo('power struggle @0 w/.',Body,0,0,1) inf from html
              where Title\Meta\Body like 'power struggle';

    Would give something of the form:

::

    300 <Data from Texis> 62 5 0 5
    power
    301 End of Metamorph hit
    300 <Data from Texis> 2042 5 0 5
    power
    301 End of Metamorph hit
    300 <Data from Texis> 2331 5 0 5
    POWER
    301 End of Metamorph hit
    300 <Data from Texis> 2892 8 0 8
    STRUGGLE
    301 End of Metamorph hit


convert
"""""""

The convert function allows you to change the type of an expression. The
syntax is

.. code-block:: sql

       CONVERT(expression, 'type-name'[, 'mode'])

The type name should in general be in lower case.

This can be useful in a number of situations. Some cases where you might
want to use convert are

-  The display format for a different format is more useful. For example
   you might want to convert a field of type COUNTER to a DATE field, so
   you can see when the record was inserted, for example:

   ::

           SELECT convert(id, 'date')
           FROM   LOG;

   ::

           CONVERT(id, 'date')
           1995-01-27 22:43:48

*  If you have an application which is expecting data in a particular
   type you can use convert to make sure you will receive the correct
   type.

* The optional third argument given to ``convert()`` is a
  :ref:`sql-set:varcharToStrlstMode` mode value.  This third argument may
  only be supplied when converting to type ``strlst`` or ``varstrlst``.  It
  allows the separator character or mode to be conveniently specified
  locally to the conversion, instead of having to alter the global
  :ref:`sql-set:varcharToStrlstMode` mode.

* Note that ``convert()``\ ing data
  from/to ``varbyte``/``varchar`` does not convert the data to/from
  hexadecimal by default in programs
  other than ``tsql``; it is preserved as-is (though truncated at nul
  for ``varchar``). See the `bintohex`_\ () and `hextobin`_\ () functions
  for hexadecimal conversion, and the :ref:`sql-set:hexifyBytes` SQL property
  for controlling automatic hex conversion.

seq
"""

Returns a sequence number. The number can be initialized to any value,
and the increment can be defined for each call. The syntax is:

.. code-block:: sql

        seq(increment [, init])

If init is given then the sequence number is initialized to that value,
which will be the value returned. It is then incremented by increment.
If init is not specified then the current value will be retained. The
initial value will be zero if init has not been specified.

Examples of typical use:

.. code-block:: sql

         SELECT  NAME, seq(1)
         FROM    SYSTABLES

The results are:

::

      NAME                seq(1)
     SYSTABLES               0
     SYSCOLUMNS              1
     SYSINDEX                2
     SYSUSERS                3
     SYSPERMS                4
     SYSTRIG                 5
     SYSMETAINDEX            6

.. code-block:: sql

         SELECT  seq(0, 100)
         FROM    SYSDUMMY;

         SELECT  NAME, seq(1)
         FROM    SYSTABLES

The results are:

::

      seq(0, 100)
         100

      NAME                seq(1)
     SYSTABLES             100
     SYSCOLUMNS            101
     SYSINDEX              102
     SYSUSERS              103
     SYSPERMS              104
     SYSTRIG               105
     SYSMETAINDEX          106


random
""""""

Returns a random int. The syntax is:

.. code-block:: sql

        random(max [, seed])

If seed is given then the random number generator is seeded to that
value. The random number generator will only be seeded once in each
session, and will be randomly seeded on the first call if no seed is
supplied. The seed parameter is ignored in the second and later calls to
random in a process.

The returned number is always non-negative, and never larger than the
limit of the C lib’s random number generator (typically either 32767 or
2147483647). If max is non-zero, then the returned number will also be
less than max.

This function is typically used to either generate a random number for
later use, or to generate a random ordering of result records by adding
random to the ORDER BY clause.

Examples of typical use:

.. code-block:: sql

         SELECT  NAME, random(100)
         FROM    SYSTABLES

The results might be:

::

      NAME                random(100)
     SYSTABLES               90
     SYSCOLUMNS              16
     SYSINDEX                94
     SYSUSERS                96
     SYSPERMS                 1
     SYSTRIG                 84
     SYSMETAINDEX            96

.. code-block:: sql

         SELECT  ENAME
         FROM    EMPLOYEE
         ORDER BY random(0);

The results would be a list of employees in a random order.


bintohex
""""""""

Converts a binary (``varbyte``) value into a hexadecimal string.

.. code-block:: sql

        bintohex(varbyteData[, 'stream|pretty'])

A string (``varchar``) hexadecimal representation of the ``varbyteData``
parameter is returned. This can be useful to visually examine binary
data that may contain non-printable or nul bytes. The optional second
argument is one of the following flags:

-  ``stream``: Use the default output mode: a continuous stream of
   hexadecimal bytes.

-  ``pretty``: Return a “pretty” version of the data: print 16 byte per
   line, space-separate the hexadecimal bytes, and print an ASCII dump
   on the right side.

.. already mentioned above
   Caveat: Note that `convert()``\ ing data from/to
   ``varbyte``/``varchar`` converts the data to/from hexadecimal
   by default when using ``tsql``; otherwise it is preserved as-is 
   (though truncated at nul for ``varchar``). See the
   :ref:`sql-set:hexifyBytes` SQL property to change this.


hextobin
""""""""

Converts a hexadecimal stream to its binary representation.

.. code-block:: sql

        hextobin(hexString[, 'stream|pretty'])

The hexadecimal ``varchar`` string ``hexString`` is converted to its
binary representation, and the ``varbyte`` result returned. The optional
second argument is one of the following flags:

-  ``stream``: Only accept the ``stream`` format of ``bintohex()``, i.e.
   a stream of hexadecimal bytes, the same format that
   ``bintohex(varbyteData, 'stream')`` returns.
   Whitespace is acceptable, but only between
   (not within) hexadecimal bytes. Case-insensitive. Non-conforming data
   will result in an error message and the function failing.

-  ``pretty``: Accept either ``stream`` or ``pretty`` `bintohex`_ formatted data; 
   if the latter, only the hexadecimal bytes are parsed (e.g. ASCII column
   is ignored). Parsing is more liberal, but may be confused if the data
   deviates significantly from either format.

.. already mentioned in convert
   The ``hextobin()`` function was added in Texis version 7. Caveat: Note
   that in version 7 and later, ``convert()``\ ing data from/to
   ``varbyte``/``varchar`` no longer converts the data to/from hexadecimal
   by default (as was done in earlier versions) in programs other than
   ``tsql``; it is now preserved as-is (though truncated at nul for
   ``varchar``). See the ``hexifybytes`` SQL property (p. ) to change this.


identifylanguage
""""""""""""""""

Tries to identify the predominant language of a given string. By
returning a probability in addition to the identified language, this
function can also serve as a test of whether the given string is really
natural-language text, or perhaps binary/encoded data instead. Syntax:

.. code-block:: sql

        identifylanguage(text[, language[, samplesize]])

The return value is a two-element ``strlst``: a probability and a
language code. The probability is a value from 0.000 to 1.000 that the
text argument is composed in the language named by the returned language
code. The language code is a two-letter ISO-639-1 code.

If an ISO-639-1 code is given for the optional language argument, the
probability for that particular language is returned, instead of for the
highest-probability language of the known/built-in languages (currently
de, es, fr, ja, pl, tr, da, en, eu, it, ko, ru).

The optional third argument samplesize is the initial integer size in
bytes of the text to sample when determining language; it defaults to
16384. 

Note that since a ``strlst`` value is returned, the probability is
returned as a ``strlst`` element, not a ``double`` value, and thus
should be cast to ``double`` during comparisons.

The ``identifylanguage()`` function is experimental, and its behavior,
syntax, name and/or existence are subject to change.


lookup
""""""

By combining the ``lookup()`` function with a ``GROUP BY``, a column may
be grouped into bins or ranges – e.g. for price-range grouping – instead
of distinct individual values. Syntax:

.. code-block:: sql

        lookup(keys, ranges[, names])

The keys argument is one (or more, e.g. ``strlst``) values to look up;
each is searched for in the ranges argument, which is one (or more, e.g.
``strlst``) ranges. All range(s) that the given key(s) match will be
returned. If the names argument is given, the corresponding names
value(s) are returned instead; this allows ranges to be renamed into
human-readable values. If names is given, the number of its values must
equal the number of ranges.

Each range is a pair of values (lower and upper bounds) separated by
“..” (two periods). The range is optionally surrounded by square (bound
included) or curly (bound excluded) brackets. E.g.:

::

    [10..20}

denotes the range 10 to 20: including 10 (“[”) but not including (“}”)
20. Both an upper and lower bracket must be given if either is present
(though they need not be the same type). The default if no brackets are
given is to include the lower bound but exclude the upper bound; this
makes consecutive ranges non-overlapping, if they have the same upper
and lower bound and no brackets (e.g. “0..10,10..20”). Either bound may
be omitted, in which case that bound is unlimited. Each range’s lower
bound must not be greater than its upper bound, nor equal if either
bound is exclusive.

If a ranges value is not varchar/char, or does not contain “..”, its
entire value is taken as a single inclusive lower bound, and the
exclusive upper bound will be the next ranges value’s lower bound (or
unlimited if no next value). E.g. the ``varint`` lower-bound list:

::

    0,10,20,30

is equivalent to the ``strlst`` range list:

::

    [0..10},[10..20},[20..30},[30..]

By using the ``lookup()`` function in a ``GROUP BY``, a column may be
grouped into ranges. For example, given a table Products with the
following SKUs and ``float`` prices:

::

        SKU    Price
        ------------
        1234   12.95
        1235    5.99
        1236   69.88
        1237   39.99
        1238   29.99
        1239   25.00
        1240   50.00
        1241   -2.00
        1242  499.95
        1243   19.95
        1244    9.99
        1245  125.00

they may be grouped into price ranges (with most-products first) with
this SQL:

.. code-block:: sql

    SELECT   lookup(Price, convert('0..25,25..50,50..,', 'strlst', 'lastchar'),
         convert('Under $25,$25-49.99,$50 and up,', 'strlst', 'lastchar'))
           PriceRange, count(SKU) NumberOfProducts
    FROM Products
    GROUP BY lookup(Price, convert('0..25,25..50,50..,', 'strlst', 'lastchar'),
         convert('Under $25,$25-49.99,$50 and up,', 'strlst', 'lastchar'))
    ORDER BY 2 DESC;

or this in Rampart JavaScript:

.. code-block:: javascript

    var Sql=require("rampart-sql");

    var sql=new Sql.init("/path/to/database");
    
    var range=['0..25','25..50','50..'];
    var rangenames=['Under $25','$25-$49','$50 and up'];
    var res = sql.exec(
        "SELECT lookup( Price, convert(?,'strlst','json'), convert(?,'strlst','json') ) PriceRange,"+
        "count(SKU) NumberOfProducts FROM Products " +
        "GROUP BY lookup(Price, convert(?,'strlst','json'), convert(?,'strlst','json') )" +
        "ORDER BY 2 DESC",
        [range,rangenames,range,rangenames],
        {returnType:"array"}
    );

    rows=res.results;
    cols=res.columns;
    for (var i=0;i<rows.length;i++) {

            if (!i) {
                rampart.utils.printf("%-12s %16s\n", cols[0] , cols[1]);
                rampart.utils.printf("------------+----------------\n");
            }

            rampart.utils.printf("%-12s %16s\n", rows[i][0], rows[i][1]);

    }



which would give these results:

::

   PriceRange   NumberOfProducts
   ------------+----------------
   $50 and up                  4
   Under $25                   4
   $25-$49                     3
                               1

Note that:

* In the ``tsql`` example, the trailing commas in the ``PriceRange`` values are used
  to converted to ``strlst`` values via the ``convert(.., .., 'lastchar')``
  function.  In the Rampart JavaScript version, the array of strings are
  converted into a ``strlst`` using ``convert(.., .., 'json')`` function.
  See `convert`_ () mode above for details.

* The empty PriceRange for the fourth row: the -2 Price matched no ranges, and hence an
  empty PriceRange was returned for it.


See also: `lookupCanonicalizeRanges`_, `lookupParseRange`_


lookupCanonicalizeRanges
""""""""""""""""""""""""

The ``lookupCanonicalizeRanges()`` function returns the canonical
version(s) of its ranges argument, which is zero or more ranges of the
syntaxes acceptable to :ref:`lookup() <function:lookup>`:

.. code-block:: sql

        lookupCanonicalizeRanges(ranges, keyType)

The canonical version always includes both a lower and upper
inclusive/exclusive bracket/brace, both lower and upper bounds (unless
unlimited), the “..” range operator, and is independent of other ranges
that may be in the sequence.

The keyType parameter is a ``varchar`` string denoting the SQL type of
the key field that would be looked up in the given range(s). This
ensures that comparisons are done correctly. E.g. for a ``strlst`` range
list of “0,500,1000”, keyType should be “integer”, so that “500” is not
compared alphabetically with “1000” and considered invalid (greater
than).

This function can be used to verify the syntax of a range, or to
transform it into a standard form for `lookupParseRange`_\ ().

For an implicit-upper-bound range, the upper bound is determined by the
*next* range’s lower bound. Thus the full list of ranges (if multiple)
should be given to ``lookupCanonicalizeRanges()`` – even if only one
range needs to be canonicalized – so that each range gets its proper
bounds.

See also: `lookup`_, `lookupParseRange`_


lookupParseRange
""""""""""""""""

The ``lookupParseRange()`` function parses a single :ref:`lookup() <function:lookup>`-style
range into its constituent parts, returning them as strings in one
``strlst`` value. This can be used by scripts to edit a range.
Syntax:

.. code-block:: sql

        lookupParseRange(range, parts)

The parts argument is zero or more of the following part tokens as
strings:

-  ``lowerInclusivity``: Returns the inclusive/exclusive operator for
   the lower bound, e.g. “{” or “”

If a requested part is not present, an empty string is returned for that
part. The concatenation of the above listed parts, in the above order,
should equal the given range. Non-string range arguments are not
supported.

.. code-block:: sql

        lookupParseRange('10..20', 'lowerInclusivity')

would return a single empty-string ``strlst``, as there is no
lower-bound inclusive/exclusive operator in the range “10..20”.

.. code-block:: sql

        lookupParseRange('10..20', 'lowerBound')

would return a ``strlst`` with the single value “10”.

For an implicit-upper-bound range, the upper bound is determined by the
*next* range’s lower bound. Since ``lookupParseRange()`` only takes one
range, passing such a range to it may result in an incorrect (unlimited)
upper bound. Thus the full list of ranges (if multiple) should always be
given to `lookupCanonicalizeRanges`_\ () first, and only then the desired
canonicalized range passed to ``lookupParseRange()``.

See also: `lookup`_, `lookupCanonicalizeRanges`_


.. unneeded
   hasFeature
   """"""""""

   Returns 1 if given feature is supported, 0 if not (or unknown). The
   syntax is:

   .. code-block:: sql

           hasFeature(feature)

   where feature is one of the following ``varchar`` tokens:

   -  ``RE2`` For RE2 regular expression support in REX

   This function is typically used in Vortex scripts to test if a feature
   is supported with the current version of Texis, and if not, to work
   around that fact if possible. For example:

   ::

            <if hasFeature( "RE2" ) = 1>
              ... proceed with RE2 expressions ...
            <else>
              ... use REX instead ...
            </if>



ifNull
""""""

Substitute another value for NULL values. Syntax:

.. code-block:: sql

       ifNull(testVal, replaceVal)

If ``testVal`` is a SQL NULL value, then ``replaceVal`` (cast to the
type of ``testVal``) is returned; otherwise ``testVal`` is returned.
This function can be used to ensure that NULL value(s) in a column are
replaced with a non-NULL value, if a non-NULL value is required:

.. code-block:: sql

        SELECT ifNull(myColumn, 'Unknown') FROM myTable;



isNull
""""""

Tests a value, and returns a ``long`` value of 1 if NULL, 0 if not.
Syntax:

.. code-block:: sql

       isNull(testVal)

.. code-block:: sql

        SELECT isNull(myColumn) FROM myTable;

Note that Texis ``isNull`` behavior differs from some other SQL implementations; see
also `ifNull`_.

.. not in this version
   xmlTreeQuickXPath
   """""""""""""""""

   Extracts information from an XML document.

   .. code-block:: sql

           xmlTreeQuickXPath(string xmlRaw, string xpathQuery
               [, string[] xmlns)

   Parameters:

   -  ``xmlRaw`` - the plain text of the xml document you want to extract
      information from

   -  ``xpathQuery`` - the XPath expression that identifies the nodes you
      want to extract the data from

   -  ``xmlns`` *(optional)* - an array of ``prefix=URI`` namespaces to use
      in the XPath query

   Returns:

   -  String values of the node from the XML document ``xmlRaw`` that match
      ``xpathQuery``

   ``xmlTreeQuickXPath`` allows you to easily extract information from an
   XML document in a one-shot function. It is intended to be used in SQL
   statements to extract specific information from a field that contains
   XML data.

   If the ``xmlData`` field of a table has content like this:

   ::

       <extraInfo>
           <price>8.99</price>
           <author>John Doe</author>
           <isbn>978-0-06-051280-4</isbn>
       </extraInfo>

   Then the following SQL statement will match that row:

   .. code-block:: sql

       SELECT * from myTable where xmlTreeQuickXPath(data,
       '/extraInfo/author') = 'John Doe'

File functions
~~~~~~~~~~~~~~


fromfile, fromfiletext
""""""""""""""""""""""

The ``fromfile`` and ``fromfiletext`` functions read a file. The syntax
is

.. code-block:: sql

       fromfile(filename[, offset[, length]])
       fromfiletext(filename[, offset[, length]])

These functions take one required, and two optional arguments. The first
argument is the filename. The second argument is an offset into the
file, and the third argument is the length of data to read. If the
second argument is omitted then the file will be read from the
beginning. If the third argument is omitted then the file will be read
to the end. The result is the contents of the file. This can be used to
load data into a table. For example if you have an indirect field and
you wish to see the contents of the file you can issue SQL similar to
the following.

The difference between the two functions is the type of data that is
returned. ``fromfile`` will return varbyte data, and ``fromfiletext``
will return varchar data. If you are using the functions to insert data
into a field you should make sure that you use the appropriate function
for the type of field you are inserting into.

.. code-block:: sql

         SELECT  FILENAME, fromfiletext(FILENAME)
         FROM    DOCUMENTS
         WHERE   DOCID = 'JT09113' ;

The results are:

.. code-block:: sql

      FILENAME            fromfiletext(FILENAME)
      /docs/JT09113.txt   This is the text contained in the document
      that has an id of JT09113.

.. not available
   totext
   """"""

   Converts data or file to text. The syntax is

   .. code-block:: sql

          totext(filename[, args])
          totext(data[, args])

   This function will convert the contents of a file, if the argument given
   is an indirect, or else the result of the expression, and convert it to
   text. It does this by calling the program ``anytotx``, which must be in
   the path. The ``anytotx`` program (obtained from Thunderstone) will
   handle ``PDF`` as well as many other file formats.

   The ``totext`` command will take an
   optional second argument which contains arguments to the ``anytotx``
   program. See the documentation for ``anytotx`` for details on its
   arguments.

   .. code-block:: sql

            SELECT  FILENAME, totext(FILENAME)
            FROM    DOCUMENTS
            WHERE   DOCID = 'JT09113' ;

   The results are:

   ::

         FILENAME            totext(FILENAME)
         /docs/JT09113.pdf   This is the text contained in the document
         that has an id of JT09113.


toind
"""""

Create a Texis managed indirect file. The syntax is

.. code-block:: sql

       toind(data)

This function takes the argument, stores it into a file, and returns the
filename as an ``indirect`` type. This is most often used in combination
with ``fromfile`` to create a Texis managed file. For example:

.. code-block:: sql

         INSERT  INTO DOCUMENTS
         VALUES('JT09114', toind(fromfile('srcfile')))

The database will now contain a pointer to a copy of ``srcfile``, which
will remain searchable even if the original is changed or removed. An
important point to note is that any changes to ``srcfile`` will not be
reflected in the database, unless the table row’s ``indirect`` column is
modified (even to the save value, this just tells Texis to re-index it).


canonpath
"""""""""

Returns canonical version of a file path, i.e. fully-qualified and
without symbolic links:

.. code-block:: sql

      canonpath(path[, flags])

The optional ``flags`` is a set of bit flags: bit 0 set if error
messages should be issued, bit 1 set if the return value should be empty
instead of ``path`` on error.


pathcmp
"""""""

File path comparison function; like C function ``strcmp()`` but for
paths:

.. code-block:: sql

      pathcmp(pathA, pathB)

Returns an integer indicating the sort order of ``pathA`` relative to
``pathB``: 0 if ``pathA`` is the same as ``pathB``, less than 0 if
``pathA`` is less than ``pathB``, greater than 0 if ``pathA`` is greater
than ``pathB``. Paths are compared case-insensitively if and only if the
OS is case-insensitive for paths, and OS-specific alternate directory
separators are considered the same (e.g. “``\``” and “``/``” in
Windows). Multiple consecutive directory separators are considered the
same as one. A trailing directory separator (if not also a leading
separator) is ignored. Directory separators sort lexically before any
other character.

Note that the paths are only compared lexically: no attempt is made to
resolve symbolic links, “..” path components, etc. Note also that no
inference should be made about the magnitude of negative or positive
return values: greater magnitude does not necessarily indicate greater
lexical “separation”, nor should it be assumed that comparing the same
two paths will always yield the same-magnitude value in future versions.
Only the sign of the return value is significant.


basename
""""""""

Returns the base filename of a given file path.

.. code-block:: sql

      basename(path)

The basename is the contents of ``path`` after the last path separator.
No filesystem checks are performed, as this is a text/parsing function;
thus “``.``” and “``..``” are not significant.


dirname
"""""""

Returns the directory part of a given file path.

.. code-block:: sql

      dirname(path)

The directory is the contents of ``path`` before the last path separator
(unless it is significant – e.g. for the root directory – in which case
it is retained). No filesystem checks are performed, as this is a text/parsing function;
thus “``.``” and “``..``” are not significant.


fileext
"""""""

Returns the file extension of a given file path.

.. code-block:: sql

      fileext(path)

The file extension starts with and includes a dot. The file extension is
only considered present in the basename of the path, i.e. after the last
path separator.


joinpath
""""""""

Joins one or more file/directory path arguments into a merged path,
inserting/removing a path separator between arguments as needed. Takes
one to 5 path component arguments. E.g.:

.. code-block:: sql

      joinpath('one', 'two/', '/three/four', 'five')

yields

::

      one/two/three/four/five


Redundant path separators internal to an argument are not removed, 
nor are “.” and “ ..” path components removed.


joinpathabsolute
""""""""""""""""

Like ``joinpath``, except that a second or later argument that is an
absolute path will overwrite the previously-merged path. E.g.:

.. code-block:: sql

      joinpathabsolute('one', 'two', '/three/four', 'five')

yields

::

      /three/four/five

Under Windows, partially absolute path arguments – e.g. “ /dir”
where the drive or dir is still relative – are considered
absolute for the sake of overwriting the merge.

Redundant path separators
internal to an argument are not removed, nor are “.” and “..” path
components removed.

String Functions
~~~~~~~~~~~~~~~~


abstract
""""""""

Generate an abstract of a given portion of text. The syntax is

.. code-block:: sql

       abstract(text[, maxsize[, style[, query]]])

The abstract will be less than ``maxsize`` characters long, and will
attempt to end at a word boundary. If ``maxsize`` is not specified (or
is less than or equal to 0) then a default size of 230 characters is
used.

The ``style`` argument is a string or integer, and allows a choice
between several different ways of creating the abstract. Note that some
of these styles require the ``query`` argument as well, which is a
Metamorph query to look for:

* ``dumb`` (0) - Start the abstract at the top of the document.

* ``smart`` (1) - This style will look for the first meaningful 
  chunk of text, skipping over any headers at the top of the text.  This
  is the default if neither ``style`` nor ``query`` is given.

* ``querysingle`` (2) -
  Center the abstract contiguously on the best occurence of ``query``
  in the document.

* ``querymultiple`` (3) -
  Like ``querysingle``, but also break up the abstract into multiple
  sections (separated with “``...``”) if needed to help ensure all
  terms are visible. Also take care with URLs to try to show the
  start and end.

* ``querybest`` -
  An alias for the best available query-based style; currently the
  same as ``querymultiple``. Using ``querybest`` in a script ensures
  that if improved styles become available in future releases, the
  script will automatically “upgrade” to the best style.

If no ``query`` is given for the ``query``\ :math:`...` modes, they fall
back to ``dumb`` mode. If a ``query`` is given with a
*non-*\ ``query``\ :math:`...` mode (``dumb``/``smart``), the mode is
promoted to ``querybest``. The current locale and index expressions also
have an effect on the abstract in the ``query``\ :math:`...` modes, so
that it more closely reflects an index-obtained hit.

.. code-block:: sql

         SELECT     abstract(STORY, 0, 1, 'power struggle')
         FROM       ARTICLES
         WHERE      ARTID = 'JT09115' ;

See also the Rampart JavaScript :ref:`rampart-sql:abstract()` function.


text2mm
"""""""

Generate ``LIKEP`` query. The syntax is

.. code-block:: sql

       text2mm(text[, maxwords])

This function will take a text expression, and produce a list of words
that can be given to ``LIKER`` or ``LIKEP`` to find similar documents.
``text2mm`` takes an optional second argument which specifies how many
words should be returned. If this is not specified then 10 words are
returned. Most commonly ``text2mm`` will be given the name of a field.
If it is an ``indirect`` field you will need to call ``fromfile`` as
shown below:

.. code-block:: sql

         SELECT     text2mm(fromfile(FILENAME))
         FROM       DOCUMENTS
         WHERE      DOCID = 'JT09115' ;

You may also call it as ``texttomm()`` instead of ``text2mm()`` .


keywords
""""""""

Generate list of keywords. The syntax is

.. code-block:: sql

       keywords(text[, maxwords])

keywords is similar to text2mm but produces a list of phrases, with a
linefeed separating them.  The difference between text2mm and keywords is
that keywords will maintain the phrases.  The keywords function also takes
an optional second argument which indicates how many words or phrases should
be returned.


length
""""""

Returns the length in characters of a ``char`` or ``varchar``
expression, or number of strings/items in other types. The syntax is

.. code-block:: sql

      length(value[, mode])

For example:

.. code-block:: sql

         SELECT  NAME, length(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                length(NAME)
     SYSTABLES               9
     SYSCOLUMNS             10
     SYSINDEX                8
     SYSUSERS                8
     SYSPERMS                8
     SYSTRIG                 7
     SYSMETAINDEX           12

The optional ``mode`` argument is a :ref:`sql-set:stringCompareMode`-style compare
mode to use. If ``mode`` is not given, the current apicp 
:ref:`sql-set:stringCompareMode`
is used. Currently the only pertinent ``mode`` flag is “iso-8859-1”,
which determines whether to interpret ``value`` as ISO-8859-1 or UTF-8.
This can alter how many characters long the string appears to be, as
UTF-8 characters are variable-byte-sized, whereas ISO-8859-1 characters
are always mono-byte.

Note that if given a ``strlst``
type ``value``, ``length()`` returns the number of string values in the
list. For other types, it returns the number of values (e.g. for
``varint`` it returns the number of integer values).



lower
"""""

Returns the text expression with all letters in lower-case. The syntax
is

.. code-block:: sql

      lower(text[, mode])

For example:

.. code-block:: sql

         SELECT  NAME, lower(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                lower(NAME)
     SYSTABLES            systables
     SYSCOLUMNS           syscolumns
     SYSINDEX             sysindex
     SYSUSERS             sysusers
     SYSPERMS             sysperms
     SYSTRIG              systrig
     SYSMETAINDEX         sysmetaindex

The optional ``mode`` argument is a string-folding mode in the same
format as ; see the Vortex manual for details on the syntax and default.
If ``mode`` is unspecified, the current apicp :ref:`sql-set:stringCompareMode` 
setting – with “+lowercase” aded – is used.

upper
"""""

Returns the text expression with all letters in upper-case. The sytax is

.. code-block:: sql

      upper(text[, mode])

For example:

.. code-block:: sql

         SELECT  NAME, upper(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                upper(NAME)
     SYSTABLES            SYSTABLES
     SYSCOLUMNS           SYSCOLUMNS
     SYSINDEX             SYSINDEX
     SYSUSERS             SYSUSERS
     SYSPERMS             SYSPERMS
     SYSTRIG              SYSTRIG
     SYSMETAINDEX         SYSMETAINDEX

The optional ``mode`` argument is a string-folding mode in the same
format as ; see the Vortex manual for details on the syntax and default.
If ``mode`` is unspecified, the current apicp :ref:`sql-set:stringCompareMode`
setting – with “+uppercase” added – is used.

initcap
"""""""

Capitalizes text. The syntax is

.. code-block:: sql

      initcap(text[, mode])

Returns the text expression with the first letter of each word in title
case (i.e. upper case), and all other letters in lower-case. For
example:

.. code-block:: sql

         SELECT  NAME, initcap(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                initcap(NAME)
     SYSTABLES            Systables
     SYSCOLUMNS           Syscolumns
     SYSINDEX             Sysindex
     SYSUSERS             Sysusers
     SYSPERMS             Sysperms
     SYSTRIG              Systrig
     SYSMETAINDEX         Sysmetaindex


The optional ``mode`` argument is a string-folding mode in the same
format as :ref:`sql-set:stringCompareMode`.
If ``mode`` is unspecified, the current :ref:`sql-set:stringCompareMode` setting
– with “+titlecase” added – is used.



sandr
"""""

Search and replace text.

.. code-block:: sql

       sandr(search, replace, text)

Returns the text expression with the search REX expression replaced with
the replace expression. See the Rampart Sql.\ :ref:`rampart-sql:rex()`  and 
the Rampart Sql.\ :ref:`rampart-sql:sandr()` function documentation for 
complete syntax of the search and replace expressions.

.. code-block:: sql

         SELECT  NAME, sandr('>>=SYS=', 'SYSTEM TABLE ', NAME) DESCR
         FROM    SYSTABLES

The results are:

::

      NAME                DESCR
     SYSTABLES            SYSTEM TABLE TABLES
     SYSCOLUMNS           SYSTEM TABLE COLUMNS
     SYSINDEX             SYSTEM TABLE INDEX
     SYSUSERS             SYSTEM TABLE USERS
     SYSPERMS             SYSTEM TABLE PERMS
     SYSTRIG              SYSTEM TABLE TRIG
     SYSMETAINDEX         SYSTEM TABLE METAINDEX


separator
"""""""""

Returns the separator character from its ``strlst`` argument, as a
``varchar`` string:

.. code-block:: sql

       separator(strlstValue)

This can be used in situations where the ``strlstValue`` argument may
have a nul character as the separator, in which case simply converting
``strlstValue`` to ``varchar`` and looking at the last character would
be incorrect.


stringcompare
"""""""""""""

Compares its string (``varchar``) arguments ``a`` and ``b``, returning
-1 if ``a`` is less than ``b``, 0 if they are equal, or 1 if ``a`` is
greater than ``b``:

.. code-block:: sql

      stringcompare(a, b[, mode])

The strings are compared using the optional ``mode`` argument, which is
a string-folding mode in the same format as ; see the Vortex manual for
details on the syntax and default. If ``mode`` is unspecified, the
current apicp :ref:`sql-set:stringCompareMode` setting is used.


stringformat
""""""""""""

Returns its arguments formatted into a string (``varchar``), like the
equivalent Vortex function ``<strfmt>`` (based on the C function
``sprintf()``):

.. code-block:: sql

      stringformat(format[, arg[, arg[, arg[, arg]]]])

The ``format`` argument is a ``varchar`` string that describes how to
print the following argument(s), if any.


Math functions
~~~~~~~~~~~~~~

The following basic math functions are available in Texis: ``acos``,
``asin``, ``atan``, ``atan2``, ``ceil``, ``cos``, ``cosh``, ``exp``,
``fabs``, ``floor``, ``fmod``, ``log``, ``log10``, ``pow``, ``sin``,
``sinh``, ``sqrt``, ``tan``, ``tanh``.

All of the above functions call the ANSI C math library function of the
same name, and return a result of type ``double``. ``pow``, ``atan2``
and ``fmod`` take two double arguments, the remainder take one double
argument.

In addition, the following math-related functions are available:

-  ``isNaN(x)``
   Returns 1 if ``x`` is a float or double NaN (Not a Number) value, 0
   if not. This function should be used to test for NaN, rather than
   using the equality operator (e.g. ``x = 'NaN'``), because the IEEE
   standard defines ``NaN == NaN`` to be false, not true as might be
   expected.

Date functions
~~~~~~~~~~~~~~

The following date functions are available in Texis: ``dayname``,
``month``, ``monthname``, ``dayofmonth``, ``dayofweek``, ``dayofyear``,
``quarter``, ``week``, ``year``, ``hour``, ``minute``, ``second``.

All the functions take a date as an argument. ``dayname`` and
``monthname`` will return a string with the full day or month name based
on the current locale, and the others return a number.

The ``dayofweek`` function returns 1 for Sunday. The quarter is based on
months, so April 1st is the first day of quarter 2. Week 1 begins with
the first Sunday of the year.

The ``monthseq``, ``weekseq`` and ``dayseq`` functions will return the number of
months, weeks and days since an arbitrary past date. These can be used
when comparing dates to see how many months, weeks or days separate
them.

Bit manipulation functions
~~~~~~~~~~~~~~~~~~~~~~~~~~

These functions are used to manipulate integers as bit fields. This can
be useful for efficient set operations (e.g. set membership,
intersection, etc.). For example, categories could be mapped to
sequential bit numbers, and a row’s category membership stored compactly
as bits of an ``int`` or ``varint``, instead of using a string list.
Category membership can then be quickly determined with ``bitand`` on
the integer.

In the following functions, bit field arguments ``a`` and ``b`` are
``int`` or ``varint`` (32 bits per integer, all platforms). Argument
``n`` is any integer type. Bits are numbered starting with 0 as the
least-significant bit of the first integer. 31 is the most-significant
bit of the first integer, 32 is the least-significant bit of the second
integer (if a multi-value ``varint``), etc.

- ``bitand(a, b)``
  Returns the bit-wise AND of ``a`` and ``b``. If one argument is
  shorter than the other, it will be expanded with 0-value integers.

- ``bitor(a, b)``
  Returns the bit-wise OR of ``a`` and ``b``. If one argument is
  shorter than the other, it will be expanded with 0-value integers.

- ``bitxor(a, b)``
  Returns the bit-wise XOR (exclusive OR) of ``a`` and ``b``. If one
  argument is shorter than the other, it will be expanded with
  0-value integers.

- ``bitnot(a)``
  Returns the bit-wise NOT of ``a``.

- ``bitsize(a)``
  Returns the total number of bits in ``a``, i.e. the highest bit
  number plus 1.

- ``bitcount(a)``
  Returns the number of bits in ``a`` that are set to 1.

- ``bitmin(a)``
  Returns the lowest bit number in ``a`` that is set to 1. If none
  are set to 1, returns -1.

- ``bitmax(a)``
  Returns the highest bit number in ``a`` that is set to 1. If none
  are set to 1, returns -1.

- ``bitlist(a)``
  Returns the list of bit numbers of ``a``, in ascending order, that
  are set to 1, as a ``varint``. Returns a single -1 if no bits are
  set to 1.

- ``bitshiftleft(a, n)``
  Returns ``a`` shifted ``n`` bits to the left, with 0s padded for
  bits on the right. If ``n`` is negative, shifts right instead.

- ``bitshiftright(a, n)``
  Returns ``a`` shifted ``n`` bits to the right, with 0s padded for
  bits on the left (i.e. an unsigned shift). If ``n`` is negative,
  shifts left instead.

- ``bitrotateleft(a, n)``
  Returns ``a`` rotated ``n`` bits to the left, with left
  (most-significant) bits wrapping around to the right. If ``n`` is
  negative, rotates right instead.

- ``bitrotateright(a, n)``
  Returns ``a`` rotated ``n`` bits to the right, with right
  (least-significant) bits wrapping around to the left. If ``n`` is
  negative, rotates left instead.

- ``bitset(a, n)``
  Returns ``a`` with bit number ``n`` set to 1. ``a`` will be padded
  with 0-value integers if needed to reach ``n`` (e.g.
  ``bitset(5, 40)`` will return a ``varint(2)``).

- ``bitclear(a, n)``
  Returns ``a`` with bit number ``n`` set to 0. ``a`` will be padded
  with 0-value integers if needed to reach ``n`` (e.g.
  ``bitclear(5, 40)`` will return a ``varint(2)``).

- ``bitisset(a, n)``
  Returns 1 if bit number ``n`` is set to 1 in ``a``, 0 if not.

Internet/IP address functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following functions manipulate IP network and/or host addresses;
most take ``inet`` style argument(s). This is an IPv4 address string,
optionally followed by a netmask.

For IPv4, the format is dotted-decimal, i.e.
:math:`N`\ [.\ :math:`N`\ [.\ :math:`[N`\ .\ :math:`N`]]] where
:math:`N` is a decimal, octal or hexadecimal integer from 0 to 255. If
:math:`x < 4` values of :math:`N` are given, the last :math:`N` is taken
as the last :math:`5-x` bytes instead of 1 byte, with missing bytes
padded to the right. E.g. 192.258 is valid and equivalent to 192.1.2.0:
the last :math:`N` is 2 bytes in size, and covers 5 - 2 = 3 needed
bytes, including 1 zero pad to the right. Conversely, 192.168.4.1027 is
not valid: the last :math:`N` is too large.

An IPv4 address may optionally be followed by a netmask, either of the
form /\ :math:`B` or :\ :math:`IPv4`, where :math:`B` is a decimal,
octal or hexadecimal netmask integer from 0 to 32, and :math:`IPv4` is a
dotted-decimal IPv4 address of the same format described above. If an
:\ :math:`IPv4` netmask is given, only the largest contiguous set of
most-significant 1 bits are used (because netmasks are contiguous). If
no netmask is given, it will be calculated from standard IPv4 class
A/B/C/D/E rules, but will be large enough to include all given bytes of
the IP. E.g. 1.2.3.4 is Class A which has a netmask of 8, but the
netmask will be extended to 32 to include all 4 given bytes.

- ``inetabbrev(inet)``
  Returns a possibly shorter-than-canonical representation of
  ``$inet``, where trailing zero byte(s) of an IPv4 address may be
  omitted. All bytes of the network, and leading non-zero bytes of
  the host, will be included. E.g. returns 192.100.0/24. The
  /\ :math:`B` netmask is included, except if the network is host-only
  (i.e.netmask is the full size of the IP address). Empty string is
  returned on error.

- ``inetcanon(inet)``
  Returns canonical representation of ``$inet``. For IPv4, this is
  dotted-decimal with all 4 bytes. The /\ :math:`B` netmask is
  included, except if
  the network is host-only (i.e. netmask is the full size of the IP
  address). Empty string is returned on error.

- ``inetnetwork(inet)``
  Returns string IP address with the network bits of ``inet``, and
  the host bits set to 0. Empty string is returned on error.

- ``inethost(inet)``
  Returns string IP address with the host bits of ``inet``, and the
  network bits set to 0. Empty string is returned on error.

- ``inetbroadcast(inet)``
  Returns string IP broadcast address for ``inet``, i.e. with the
  network bits, and host bits set to 1. Empty string is returned on
  error.

- ``inetnetmask(inet)``
  Returns string IP netmask for ``inet``, i.e. with the network bits
  set to 1, and host bits set to 0. Empty string is returned on
  error.

- ``inetnetmasklen(inet)``
  Returns integer netmask length of ``inet``. -1 is returned on
  error.

- ``inetcontains(inetA, inetB)``
  Returns 1 if ``inetA`` contains ``inetB``, i.e. every address in
  ``inetB`` occurs within the ``inetA`` network. 0 is returned if
  not, or -1 on error.

- ``inetclass(inet)``
  Returns class of ``inet``, e.g. A, B, C, D, E or classless if a
  different netmask is used (or the address is IPv6). Empty string is
  returned on error.

- ``inet2int(inet)``
  Returns integer representation of IP network/host bits of ``$inet``
  (i.e. without netmask); useful for compact storage of address as
  integer(s) instead of string. Returns -1 is returned on error (note
  that -1 may also be returned for an all-ones IP address, e.g.
  255.255.255.255).

- ``int2inet(i)``
  Returns ``inet`` string for 1- or 4-value varint ``$i`` taken as an
  IP address. Since no netmask can be stored in the integer form of
  an IP address, the returned IP string will not have a netmask.
  Empty string is returned on error.


urlcanonicalize
"""""""""""""""

Canonicalize a URL. Usage:

.. code-block:: sql

       urlcanonicalize(url[, flags])

Returns a copy of ``url``, canonicalized according to case-insensitive
comma-separated ``flags``, which are zero or more of:

- ``lowerProtocol``
  Lower-cases the protocol.

- ``lowerHost``
  Lower-cases the hostname.

- ``removeTrailingDot``
  Removes trailing dot(s) in hostname.

- ``reverseHost``
  Reverse the host/domains in the hostname. E.g.
  http://host.example.com/ becomes http://com.example.host/. This can
  be used to put the most-significant part of the hostname leftmost.

- ``removeStandardPort``
  Remove the port number if it is the standard port for the protocol.

- ``decodeSafeBytes``
  URL-decode safe bytes, where semantics are unlikely to change. E.g.
  “``%41``” becomes “``A``”, but “``%2F``” remains encoded, because
  it would decode to “``/``”.

- ``upperEncoded``
  Upper-case the hex characters of encoded bytes.

- ``lowerPath``
  Lower-case the (non-encoded) characters in the path. May be used
  for URLs known to point to case-insensitive filesystems, e.g.
  Windows.

- ``addTrailingSlash``
  Adds a trailing slash to the path, if no path is present.

Default flags are all but ``reverseHost``, ``lowerPath``. A flag may be
prefixed with the operator ``+`` to append the flag to existing flags;
``-`` to remove the flag from existing flags; or ``=`` (default) to
clear existing flags first and then set the flag. Operators remain in
effect for subsequent flags until the next operator (if any) is used.

Geographical coordinate functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The geographical coordinate functions allow for efficient processing of
latitude / longitude operations. They allow for the conversion of a
latitude/longitude pair into a single “geocode”, which is a single
``long`` value that contains both values. This can be used to easily
compare it to other geocodes (for distance calculations) or for finding
other geocodes that are within a certain distance.


azimuth2compass
"""""""""""""""

.. code-block:: sql

      azimuth2compass(double azimuth [, int resolution [, int verbosity]])

The ``azimuth2compass`` function converts a numerical azimuth value
(degrees of rotation from 0 degrees north) and converts it into a
compass heading, such as ``N`` or ``Southeast``. The exact text returned
is controlled by two optional parameters, ``resolution`` and
``verbosity``.

``Resolution`` determines how fine-grained the values returned are.
There are 4 possible values:

-  ``1`` - Only the four cardinal directions are used (N, E, S, W)

-  ``2`` *(default) - Inter-cardinal directions (N, NE, E, etc.)*

-  ``3`` - In-between inter-cardinal directions (N, NNE, NE, ENE, E,
   etc.)

-  ``4`` - “by” values (N, NbE, NNE, NEbN, NE, NEbE, ENE, EbN, E, etc.)

``Verbosity`` affects how verbose the resulting text is. There are two
possible values:

-  ``1`` *(default) - Use initials for direction values (N, NbE, NNE,
   etc.)*

-  ``2`` - Use full text for direction values (North, North by east,
   North-northeast, etc.)

For an azimuth value of ``105``, here are some example results of
``azimuth2compass``:

::

    azimuth2compass(105): E
    azimuth2compass(105, 3): ESE
    azimuth2compass(105, 4): EbS
    azimuth2compass(105, 1, 2): East
    azimuth2compass(105, 3, 2): East-southeast
    azimuth2compass(105, 4, 2): East by south


azimuthgeocode
""""""""""""""

.. code-block:: sql

      azimuthgeocode(geocode1, geocode2 [, method])

The ``azimuthgeocode`` function calculates the directional heading going
from one geocode to another. It returns a number between 0-360 where 0
is north, 90 east, etc., up to 360 being north again.

The third, optional ``method`` parameter can be used to specify which
mathematical method is used to calculate the direction. There are two
possible values:

-  ``greatcircle`` *(default)* - The “Great Circle” method is a highly
   accurate tool for calculating distances and directions on a sphere.
   It is used by default.

-  ``pythagorean`` - Calculations based on the pythagorean method can
   also be used. They’re faster, but less accurate as the core formulas
   don’t take the curvature of the earth into consideration. Some
   internal adjustments are made, but the values are less accurate than
   the ``greatcircle`` method, especially over long distances and with
   paths that approach the poles.



azimuthlatlon
"""""""""""""

.. code-block:: sql

      azimuthlatlon(lat1, lon1, lat2, lon2, [, method])

The ``azimuthlatlon`` function calculates the directional heading going
from one latitude-longitude point to another. It operates identically to
`azimuthgeocode`_, except azimuthlatlon takes its parameters in a pair
of latitude-longitude points instead of geocode values.

The third, optional ``method`` parameter can be used to specify which
mathematical method is used to calculate the direction. There are two
possible values:

-  ``greatcircle`` *(default)* - The “Great Circle” method is a highly
   accurate tool for calculating distances and directions on a sphere.
   It is used by default.

-  ``pythagorean`` - Calculations based on the pythagorean method can
   also be used. They’re faster, but less accurate as the core formulas
   don’t take the curvature of the earth into consideration. Some
   internal adjustments are made, but the values are less accurate than
   the ``greatcircle`` method, especially over long distances and with
   paths that approach the poles.

.. _dms-dec:


dms2dec, dec2dms
""""""""""""""""

.. code-block:: sql

      dms2dec(dms)
      dec2dms(dec)

The ``dms2dec`` and ``dec2dms`` functions are for changing back and
forth between the “degrees minutes seconds”
(DMS) format (west-positive) and “decimal degree” format for latitude
and longitude coordinates. All SQL geographical functions expect decimal
degree parameters.

DMS values are of the format :math:`DDDMMSS`. For example,
3515’ would be represented as 351500.

In decimal degrees, a degree is a whole digit, and minutes & seconds are
represented as fractions of a degree. Therefore, 3515’ would be 35.25 in
decimal degrees.

Note that the Texis DMS format has *west*-positive longitudes
(unlike ISO 6709 DMS format), and decimal degrees have *east*-positive
longitudes. It is up to the caller to flip the sign of longitudes where
needed.


distgeocode
"""""""""""

.. code-block:: sql

      distgeocode(geocode1, geocode2 [, method] )

The ``distgeocode`` function calculates the distance, in miles, between
two given geocodes. It uses the “Great Circle” method for calculation by
default, which is very accurate. A faster, but less accurate,
calculation can be done with the Pythagorean theorem. It is not designed
for distances on a sphere, however, and becomes somewhat inaccurate at
larger distances and on paths that approach the poles. To use the
Pythagorean theorem, pass a third string parameter, “``pythagorean``”,
to force that method. “``greatcircle``” can also be specified as a
method.

For example:

-  New York (JFK) to Cleveland (CLE), the Pythagorean method is off by
   .8 miles (.1%)

-  New York (JFK) to Los Angeles (LAX), the Pythagorean method is off by
   22.2 miles (.8%)

-  New York (JFK) to South Africa (PLZ), the Pythagorean method is off
   by 430 miles (5.2%)

See Also: `distlatlon`_


distlatlon
""""""""""

.. code-block:: sql

      distlatlon(lat1, lon1, lat2, lon2 [, method] )

The ``distlatlon`` function calculates the distance, in miles, between
two points, represented in latitude/longitude pairs in decimal degree
format.

Like `distgeocode`_, it uses the “Great Circle” method by default, but
can be overridden to use the faster, less accurate Pythagorean method if
“``pythagorean``” is passed as the optional ``method`` parameter.

For example:

-  New York (JFK) to Cleveland (CLE), the Pythagorean method is off by
   .8 miles (.1%)

-  New York (JFK) to Los Angeles (LAX), the Pythagorean method is off by
   22.2 miles (.8%)

-  New York (JFK) to South Africa (PLZ), the Pythagorean method is off
   by 430 miles (5.2%)

See Also: `distgeocode`_

.. _latlon2x:


latlon2geocode, latlon2geocodearea
""""""""""""""""""""""""""""""""""

.. code-block:: sql

      latlon2geocode(lat[, lon])
      latlon2geocodearea(lat[, lon], radius)

The ``latlon2geocode`` function encodes a given latitude/longitude
coordinate into one ``long`` return value. This encoded value – a
“geocode” value – can be indexed and used with a special variant of
Texis’ ``BETWEEN`` operator for bounded-area searches of a geographical
region.

The ``latlon2geocodearea`` function generates a bounding area centered
on the coordinate. It encodes a given latitude/longitude coordinate into
a *two-* value ``varlong``. The returned geocode value pair represents
the southwest and northeast corners of a square box centered on the
latitude/longitude coordinate, with sides of length two times ``radius``
(in decimal degrees). This bounding area can be used with the Texis
``BETWEEN`` operator for fast geographical searches.

The ``lat`` and ``lon`` parameters are ``double``\ s in the decimal
degrees format. (To pass :math:`DDDMMSS` “degrees minutes seconds” (DMS)
format values, convert them first with :ref:`dms2dec <dms-dec>` or
`parselatitude, parselongitude`_.). Negative numbers represent
south latitudes and west longitudes, i.e. these functions are
east-positive, and decimal format.

Valid values for latitude are -90 to 90 inclusive. Valid values for
longitude are -360 to 360 inclusive. A longitude value less than -180
will have 360 added to it, and a longitude value greater than 180 will
have 360 subtracted from it. This allows longitude values to continue to
increase or decrease when crossing the International Dateline, and thus
avoid a non-linear “step function”. Passing invalid ``lat`` or ``lon``
values to ``latlon2geocode`` will return -1.

The ``lon`` parameter is optional: both latitude and longitude (in that
order) may be given in a single space- or comma-separated text (``varchar``)
value for ``lat``. Also, a ``N``/``S`` suffix (for latitude) or ``E``/``W``
suffix (for longitude) may be given; ``S`` or ``W`` will negate the value.

The latitude and/or longitude may also have just about any of the formats
supported by `parselatitude, parselongitude`_, provided they are
disambiguated (e.g. separate parameters; or if one parameter, separated
by a comma and/or fully specified with degrees/minutes/seconds).

.. code-block:: sql

      -- Populate a table with latitude/longitude information:
      create table geotest(city varchar(64), lat double, lon double, geocode long);
      insert into geotest values('Cleveland, OH, USA', 41.4,  -81.5,  -1);
      insert into geotest values('San Francisco, CA, USA',   37.78, -122.42,  -1);
      insert into geotest values('Davis, Ca, USA',    38.55, -121.74, -1);
      insert into geotest values('New York, NY, USA',  40.81, -73.96,  -1);

      -- Prepare for geographic searches:
      update geotest set geocode = latlon2geocode(lat, lon);
      create index xgeotest_geocode on geotest(geocode);

      -- Search for cities within a 3-degree-radius "circle" (box)
      -- of Cleveland, nearest first:
      select city, lat, lon, distlatlon(41.4, -81.5, lat, lon) MilesAway from geotest
         where geocode between (select latlon2geocodearea(41.4, -81.5, 3.0)) order by 4 asc;


The geocode values returned by ``latlon2geocode`` and
``latlon2geocodearea`` are platform-dependent in format and accuracy,
and should not be copied across platforms. On platforms with 32-bit
``long``\ s a geocode value is accurate to about 32 seconds (around half
a mile, depending on latitude). ``-1`` is returned for invalid input values.

See Also: `geocode2lat, geocode2lon`_


geocode2lat, geocode2lon
""""""""""""""""""""""""

.. code-block:: sql

      geocode2lat(geocode)
      geocode2lon(geocode)

The ``geocode2lat`` and ``geocode2lon`` functions decode a geocode into
a latitude or longitude coordinate, respectively. The returned
coordinate is in the decimal degrees format. An invalid geocode value
(e.g. -1) will return NaN (Not a Number).

If you want :math:`DDDMMSS` “degrees minutes seconds” (DMS) format, you
can use :ref:`dec2dms <dms-dec>` to convert it.

.. code-block:: sql

      select city, geocode2lat(geocode), geocode2lon(geocode) from geotest;

As with :ref:`latlon2geocode <latlon2x>`, the ``geocode`` value is platform-dependent
in accuracy and format, so it should not be copied across platforms, and
the returned coordinates from ``geocode2lat`` and ``geocode2lon`` may
differ up to about half a minute from the original coordinates (due to
the finite resolution of a ``long``). An invalid geocode value (e.g. -1)
will return ``NaN`` (Not a Number).

See Also: :ref:`latlon2geocode <latlon2x>`


parselatitude, parselongitude
"""""""""""""""""""""""""""""

.. code-block:: sql

      parselatitude(latitudeText)
      parselongitude(longitudeText)

The ``parselatitude`` and ``parselongitude`` functions parse a text
(``varchar``) latitude or longitude coordinate, respectively, and return
its value in decimal degrees as a ``double``. The coordinate should be
in one of the following forms (optional parts in square brackets):

* [:math:`H`] :math:`nnn` [:math:`U`] [:] [:math:`H`] [:math:`nnn` 
  [:math:`U`] [:] [:math:`nnn` [:math:`U`]]] [:math:`H`]

* :math:`DDMM`\ [:math:`.MMM`...]

* :math:`DDMMSS`\ [:math:`.SSS`...]

where the terms are:

- :math:`nnn`
  A number (integer or decimal) with optional plus/minus sign. Only
  the first number may be negative, in which case it is a south
  latitude or west longitude. Note that this is true even for
  :math:`DDDMMSS` (DMS) longitudes – i.e. the ISO 6709 east-positive
  standard is followed, not the deprecated Texis/Vortex west-positive
  standard.

- :math:`U`
  A unit (case-insensitive):

   -  ``d``

   -  ``deg``

   -  ``deg.``

   -  ``degrees``

   -  ``'`` (single quote) for minutes

   -  ``m``

   -  ``min``

   -  ``min.``

   -  ``minutes``

   -  ``"`` (double quote) for seconds

   -  ``s`` (iff ``d``/``m`` also used for degrees/minutes)

   -  ``sec``

   -  ``sec.``

   -  ``seconds``

   -  Unicode degree-sign (U+00B0), in ISO-8559-1 or UTF-8

   If no unit is given, the first number is assumed to be degrees, the
   second minutes, the third seconds. Note that “``s``” may only be used
   for seconds if “``d``” and/or “``m``” was also used for an earlier
   degrees/minutes value; this is to help disambiguate “seconds” vs.
   “southern hemisphere”.

- :math:`H`
  A hemisphere (case-insensitive):

   -  ``N``

   -  ``north``

   -  ``S``

   -  ``south``

   -  ``E``

   -  ``east``

   -  ``W``

   -  ``west``

   A longitude hemisphere may not be given for a latitude, and
   vice-versa.

- :math:`DD`
  A two- or three-digit degree value, with optional sign. Note that
  longitudes are east-positive ala ISO 6709, not west-positive like
  the deprecated Texis standard.

- :math:`MM`
  A two-digit minutes value, with leading zero if needed to make two
  digits.

- :math:`.MMM`...
  A zero or more digit fractional minute value.

- :math:`SS`
  A two-digit seconds value, with leading zero if needed to make two
  digits.

- :math:`.SSS`...
  A zero or more digit fractional seconds value.

Whitespace is generally not required between terms in the first format.
A hemisphere token may only occur once. Degrees/minutes/seconds numbers
need not be in that order, if units are given after each number. If a
5-integer-digit :math:`DDDMM`\ [:math:`.MMM`...] format is given and the
degree value is out of range (e.g. more than 90 degrees latitude), it is
interpreted as a :math:`DMMSS`\ [:math:`.SSS`...] value instead. To
force :math:`DDDMMSS`\ [:math:`.SSS`...] for small numbers, pad with
leading zeros to 6 or 7 digits.

.. code-block:: sql

    insert into geotest(lat, lon)
      values(parselatitude('54d 40m 10"'),
             parselongitude('W90 10.2'));

An invalid or unparseable latitude or longitude value will return
``NaN`` (Not a Number). Extra unparsed/unparsable text may be allowed
(and ignored) after the coordinate in most instances. Out-of-range
values (e.g. latitudes greater than 90 degrees) are accepted; it is up
to the caller to bounds-check the result.

JSON functions
~~~~~~~~~~~~~~

The JSON functions allow for the manipulation of ``varchar`` fields and
literals as JSON objects.


JSON Path Syntax
""""""""""""""""

The JSON Path syntax is standard Javascript object access, using ``$``
to represent the entire document. If the document is an object the path
must start ``$.``, and if an array ``$[``.


JSON Field Syntax
"""""""""""""""""

In addition to using the JSON functions it is possible to access
elements in a ``varchar`` field that holds JSON as if it was a field
itself. This allows for creation of indexes, searching and sorting
efficiently. Arrays can also be fetched as ``strlst`` to make use of
those features, e.g.
``SELECT Json.$.name FROM tablename WHERE 'SQL' IN Json.$.skills[*];``


isjson
""""""

.. code-block:: sql

      isjson(JsonDocument)

The ``isjson`` function returns 1 if the document is valid JSON, 0
otherwise.

.. code-block:: sql

    isjson('{ "type" : 1 }'): 1
    isjson('{}'): 1
    isjson('json this is not'): 0


json_format
"""""""""""

.. code-block:: sql

      json_format(JsonDocument, FormatOptions)

The ``json_format`` formats the ``JsonDocument`` according to
``FormatOptions``. Multiple options can be provided either space or
comma separated.

Valid ``FormatOptions`` are:

-  COMPACT - remove all unnecessary whitespace

-  INDENT(N) - print the JSON with each object or array member on a new
   line, indented by N spaces to show structure

-  SORT-KEYS - sort the keys in the object. By default the order is
   preserved

-  EMBED - omit the enclosing ``{}`` or ``[]`` is using the snippet in
   another object

-  ENSURE\_ASCII - encode all Unicode characters outside the ASCII range

-  ENCODE\_ANY - if not a valid JSON document then encode into a JSON
   literal, e.g. to encode a string.

-  ESCAPE\_SLASH - escape forward slash ``/`` as ``\/``


json_type
"""""""""

.. code-block:: sql

      json_type(JsonDocument)

The ``json_type`` function returns the type of the JSON object or
element. Valid responses are:

-  OBJECT

-  ARRAY

-  STRING

-  INTEGER

-  DOUBLE

-  NULL

-  BOOLEAN

Assuming a field ``Json`` containing:

::

   {"items":
     [ 
      {"myNum":1, "myText": "Some text", "myBool": true},
      {"myNum":2.0, "myText": "Some more text", "myBool": false},
      null
     ]
   }

.. code-block:: sql

    json_type(Json): OBJECT
    json_type(Json.$.items[0]): OBJECT
    json_type(Json.$.items): ARRAY
    json_type(Json.$.items[0].myNum): INTEGER
    json_type(Json.$.items[1].myNum): DOUBLE
    json_type(Json.$.items[0].myText): STRING
    json_type(Json.$.items[0].myBool): BOOLEAN
    json_type(Json.$.items[2]): NULL


json_value
""""""""""

.. code-block:: sql

      json_value(JsonDocument, Path)

The ``json_value`` extracts the value identified by ``Path`` from
``JsonDocument``. ``Path`` is a varchar in the JSON Path Syntax. This
will return a scalar value. If ``Path`` refers to an array, object, or
invalid path no value is returned.

Assuming the same Json field from the previous examples:

::

    json_value(Json, '$'):
    json_value(Json, '$.items[0]'):
    json_value(Json, '$.items'):
    json_value(Json, '$.items[0].myNum'): 1
    json_value(Json, '$.items[1].myNum'): 2.0
    json_value(Json, '$.items[0].myText'): Some Text
    json_value(Json, '$.items[0].myBool'): true
    json_value(Json, '$.items[2]'):


json_query
""""""""""

.. code-block:: sql

      json_query(JsonDocument, Path)

The ``json_query`` extracts the object or array identified by ``Path``
from ``JsonDocument``. ``Path`` is a varchar in the JSON Path Syntax.
This will return either an object or an array value. If ``Path`` refers
to a scalar no value is returned.

Assuming the same Json field from the previous examples:

::

  json_query(Json, '$')
  ---------------------
  {"items":[{"myNum":1,"myText":"Some text","myBool":true},{"myNum":2.0,"myText":"Some more text","myBool":false},null]}

  json_query(Json, '$.items[0]')
  ------------------------------
  {"myNum":1,"myText":"Some text","myBool":true}

  json_query(Json, '$.items')``
  ---------------------------
  [{"myNum":1,"myText":"Some text","myBool":true},{"myNum":2.0,"myText":"Some more text","myBool":false},null]

The following will return an empty string as they refer to scalars or
non-existent keys.

::

    json_query(Json, '$.items[0].myNum')
    json_query(Json, '$.items[1].myNum')
    json_query(Json, '$.items[0].myText')
    json_query(Json, '$.items[0].myBool')
    json_query(Json, '$.items[2]')


json_modify
"""""""""""

.. code-block:: sql

      json_modify(JsonDocument, Path, NewValue)

The ``json_modify`` function returns a modified version of JsonDocument
with the key at Path replaced by NewValue.

If ``Path`` starts with append then the NewValue is appended to the
array referenced by Path. It is an error it Path refers to anything
other than an array.

::

    json_modify('{}', '$.foo', 'Some "quote"')
    ------------------------------------------
    {"foo":"Some \"quote\""}

    json_modify('{ "foo" : { "bar": [40, 42] } }', 'append $.foo.bar', 99)
    ----------------------------------------------------------------------
    {"foo":{"bar":[40,42,99]}}

    json_modify('{ "foo" : { "bar": [40, 42] } }', '$.foo.bar', 99)
    ---------------------------------------------------------------
    {"foo":{"bar":99}}


json_merge_patch
""""""""""""""""

.. code-block:: sql

      json_merge_patch(JsonDocument, Patch)

The ``json_merge_patch`` function provides a way to patch a target JSON
document with another JSON document. The patch function conforms to
:rfc:`7386`\ .


Keys in ``JsonDocument`` are replaced if found in ``Patch``. If the
value in ``Patch`` is ``null`` then the key will be removed in the
target document.

.. code-block:: sql

    json_merge_patch('{"a":"b"}', '{"a":"c"}')
    ------------------------------------------
    {"a":"c"}

    json_merge_patch('{"a": [{"b":"c"}]}', '{"a": [1]}')
    ----------------------------------------------------
    {"a":[1]}

    json_merge_patch('[1,2]', '{"a":"b", "c":null}')
    ------------------------------------------------
    {"a":"b"}


json_merge_preserve
"""""""""""""""""""

.. code-block:: sql

      json_merge_preserve(JsonDocument, Patch)

The ``json_merge_preserve`` function provides a way to patch a target
JSON document with another JSON document while preserving the content
that exists in the target document.

Keys in ``JsonDocument`` are merged if found in ``Patch``. If the same
key exists in both the target and patch file the result will be an array
with the values from both target and patch.

If the value in ``Patch`` is ``null`` then the key will be removed in
the target document.

::

    json_merge_preserve('{"a":"b"}', '{"a":"c"}')
    ---------------------------------------------
    {"a":["b","c"]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": [1]}')
    -------------------------------------------------------
    {"a":[{"b":"c"},1]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": 1}')
    -----------------------------------------------------
    {"a":[{"b":"c"},1]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": [1,2]}')
    ---------------------------------------------------------
    {"a":[{"b":"c"},1,2]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": {"d":1,"e":2} }')
    ------------------------------------------------------------------
    {"a":[{"b":"c"},{"d":1,"e":2}]}

    json_merge_preserve('{"a": {"b":"c"}}', '{"a": {"d":1, "e":2} }')
    -----------------------------------------------------------------
    {"a":{"b":"c","d":1,"e":2}}

    json_merge_preserve('[1,2]', '{"a":"b", "c":null}')
    ---------------------------------------------------
    [1,2,{"a":"b","c":null}]
