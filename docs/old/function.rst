Server functions
================

The Texis server has a number of functions built into it which can
operate on fields. This can occur anywhere an expression can occur in a
SQL statement. It is possible that the server at your site has been
extended with additional functions. Each of the arguments can be either
a single field name, or another expression.

Other Functions
---------------

exec
~~~~

Execute an external command. The syntax is

::

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

::

         UPDATE     DOCUMENTS
         SET        TEXT = exec('ocr '+IMGFILE)
         WHERE      TEXT = '';

Another example would be if you wanted to print envelopes from names and
addresses in a table you might use the following SQL:

::

         SELECT exec('envelope ', FIRST_NAME+' '+LAST_NAME+'
         ', STREET + '
         ', CITY + ', ' + STATE + ' ' + ZIP)
         FROM ADDRESSES;

Notice in this example the addition of spaces and line-breaks between
the fields. Texis does not add any delimiters between fields or
arguments itself.

mminfo
~~~~~~

This function lets you obtain Metamorph info. You have the choice of
either just getting the portions of the document which were the hits, or
you can also get messages which describe each hit and subhits.

The SQL to use is as follows:

::

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
    ``setlen`` is the length of the set. This information is available
    in version 5.01.1220640000 20080905 and later.

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

::

    Example:
       select mminfo('power struggle @0 w/.',Body,0,0,1) inf from html
              where Title\Meta\Body like 'power struggle';
    Would give something of the form:

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
~~~~~~~

The convert function allows you to change the type of an expression. The
syntax is

::

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

-  If you have an application which is expecting data in a particular
   type you can use convert to make sure you will receive the correct
   type.

Caveat: Note that in Texis version 7 and later, ``convert()``\ ing data
from/to ``varbyte``/``varchar`` no longer converts the data to/from
hexadecimal by default (as was done in earlier versions) in programs
other than ``tsql``; it is now preserved as-is (though truncated at nul
for ``varchar``). See the ``bintohex()`` and ``hextobin()`` functions
(p. ) for hexadecimal conversion, and the ``hexifybytes`` SQL property
(p. ) for controlling automatic hex conversion.

Also in Texis version 7 and later, an optional third argument may be
given to ``convert()``, which is a ``varchartostrlstsep`` mode value
(p. ). This third argument may only be supplied when converting to type
``strlst`` or ``varstrlst``. It allows the separator character or mode
to be conveniently specified locally to the conversion, instead of
having to alter the global ``varchartostrlstsep`` mode.

seq
~~~

Returns a sequence number. The number can be initialized to any value,
and the increment can be defined for each call. The syntax is:

::

        seq(increment [, init])

If init is given then the sequence number is initialized to that value,
which will be the value returned. It is then incremented by increment.
If init is not specified then the current value will be retained. The
initial value will be zero if init has not been specified.

Examples of typical use:

::

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

::

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
~~~~~~

Returns a random int. The syntax is:

::

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

::

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

::

         SELECT  ENAME
         FROM    EMPLOYEE
         ORDER BY random(0);

The results would be a list of employees in a random order.

bintohex
~~~~~~~~

Converts a binary (``varbyte``) value into a hexadecimal string.

::

        bintohex(varbyteData[, 'stream|pretty'])

A string (``varchar``) hexadecimal representation of the ``varbyteData``
parameter is returned. This can be useful to visually examine binary
data that may contain non-printable or nul bytes. The optional second
argument is a comma-separated string of any of the following flags:

-  ``stream``: Use the default output mode: a continuous stream of
   hexadecimal bytes, i.e. the same format that
   ``convert(varbyteData, 'varchar')`` would have returned in Texis
   version 6 and earlier.

-  ``pretty``: Return a “pretty” version of the data: print 16 byte per
   line, space-separate the hexadecimal bytes, and print an ASCII dump
   on the right side.

The ``bintohex()`` function was added in Texis version 7. Caveat: Note
that in version 7 and later, ``convert()``\ ing data from/to
``varbyte``/``varchar`` no longer converts the data to/from hexadecimal
by default (as was done in earlier versions) in programs other than
``tsql``; it is now preserved as-is (though truncated at nul for
``varchar``). See the ``hexifybytes`` SQL property (p. ) to change this.

hextobin
~~~~~~~~

Converts a hexadecimal stream to its binary representation.

::

        hextobin(hexString[, 'stream|pretty'])

The hexadecimal ``varchar`` string ``hexString`` is converted to its
binary representation, and the ``varbyte`` result returned. The optional
second argument is a comma-separated string of any of the following
flags:

-  ``stream``: Only accept the ``stream`` format of ``bintohex()``, i.e.
   a stream of hexadecimal bytes, the same format that
   ``convert(varbyteData, 'varchar')`` would have returned in Texis
   version 6 and earlier. Whitespace is acceptable, but only between
   (not within) hexadecimal bytes. Case-insensitive. Non-conforming data
   will result in an error message and the function failing.

-  ``pretty``: Accept either ``stream`` or ``pretty`` format data; if
   the latter, only the hexadecimal bytes are parsed (e.g. ASCII column
   is ignored). Parsing is more liberal, but may be confused if the data
   deviates significantly from either format.

The ``hextobin()`` function was added in Texis version 7. Caveat: Note
that in version 7 and later, ``convert()``\ ing data from/to
``varbyte``/``varchar`` no longer converts the data to/from hexadecimal
by default (as was done in earlier versions) in programs other than
``tsql``; it is now preserved as-is (though truncated at nul for
``varchar``). See the ``hexifybytes`` SQL property (p. ) to change this.

identifylanguage
~~~~~~~~~~~~~~~~

Tries to identify the predominant language of a given string. By
returning a probability in addition to the identified language, this
function can also serve as a test of whether the given string is really
natural-language text, or perhaps binary/encoded data instead. Syntax:

::

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
16384. The samplesize parameter was added in version 7.01.1382113000
20131018.

Note that since a ``strlst`` value is returned, the probability is
returned as a ``strlst`` element, not a ``double`` value, and thus
should be cast to ``double`` during comparisons.

The ``identifylanguage()`` function is experimental, and its behavior,
syntax, name and/or existence are subject to change.

lookup
~~~~~~

By combining the ``lookup()`` function with a ``GROUP BY``, a column may
be grouped into bins or ranges – e.g. for price-range grouping – instead
of distinct individual values. Syntax:

::

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

::

    SELECT   lookup(Price, convert('0..25,25..50,50..,', 'strlst', 'lastchar'),
         convert('Under $25,$25-49.99,$50 and up,', 'strlst', 'lastchar'))
           PriceRange, count(SKU) NumberOfProducts
    FROM Products
    GROUP BY lookup(Price, convert('0..25,25..50,50..,', 'strlst', 'lastchar'),
         convert('Under $25,$25-49.99,$50 and up,', 'strlst', 'lastchar'))
    ORDER BY 2 DESC;

or this in Rampart javascript:

::

    var Sql=require("rampart-sql");

    var sql=new Sql.init("/path/to/database");

    var range=['0..25','25..50','50..'];
    var rangenames=['Under $25','$25-$49','$50 and up'];
    res = sql.exec("SELECT lookup( convert(?,'strlst','json'), convert(?,'strlst','json') ) PriceRange"+
        "count(SKU) NumberOfProducts FROM Products " +
    	"GROUP BY lookup(convert(?,'strlst','json'), convert(?,'strlst','json') )" +
        "ORDER BY 2 DESC",
        [range,rangenames,range,rangenames],
        {returnType:"arrayh"}
    )

    rows=res.results;
    for (var i=0;i<rows.length;i++) {
            rampart.utils.printf("%-10s %10s\n", rows[i][0]+',', rows[i][1]);

            if (!i)
                rampart.utils.printf("----------+----------\n");
    }


which would give these results:

::

      PriceRange NumberOfProducts
    ------------+------------+
    Under $25,              4
    $50 and up,             4
    $25-49.99,              3
                            1

The trailing commas in PriceRange values are due to them being
``strlst`` values, for possible multiple ranges. Note the empty
PriceRange for the fourth row: the -2 Price matched no ranges, and hence
an empty PriceRange was returned for it.


See also: `lookupCanonicalizeRanges`_, `lookupParseRange`_

lookupCanonicalizeRanges
~~~~~~~~~~~~~~~~~~~~~~~~

The ``lookupCanonicalizeRanges()`` function returns the canonical
version(s) of its ranges argument, which is zero or more ranges of the
syntaxes acceptable to :ref:`lookup() <function:lookup>`:

::

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
transform it into a standard form for ``lookupParseRange()`` (p. ).

For an implicit-upper-bound range, the upper bound is determined by the
*next* range’s lower bound. Thus the full list of ranges (if multiple)
should be given to ``lookupCanonicalizeRanges()`` – even if only one
range needs to be canonicalized – so that each range gets its proper
bounds.

See also: `lookup`_, `lookupParseRange`_

lookupParseRange
~~~~~~~~~~~~~~~~

The ``lookupParseRange()`` function parses a single :ref:`lookup() <function:lookup>`-style
range into its constituent parts, returning them as strings in one
``strlst`` value. This can be used by Vortex scripts to edit a range.
Syntax:

::

        lookupParseRange(range, parts)

The parts argument is zero or more of the following part tokens as
strings:

-  ``lowerInclusivity``: Returns the inclusive/exclusive operator for
   the lower bound, e.g. “{” or “”

If a requested part is not present, an empty string is returned for that
part. The concatenation of the above listed parts, in the above order,
should equal the given range. Non-string range arguments are not
supported.

::

        lookupParseRange('10..20', 'lowerInclusivity')

would return a single empty-string ``strlst``, as there is no
lower-bound inclusive/exclusive operator in the range “10..20”.

::

        lookupParseRange('10..20', 'lowerBound')

would return a ``strlst`` with the single value “10”.

For an implicit-upper-bound range, the upper bound is determined by the
*next* range’s lower bound. Since ``lookupParseRange()`` only takes one
range, passing such a range to it may result in an incorrect (unlimited)
upper bound. Thus the full list of ranges (if multiple) should always be
given to ``lookupCanonicalizeRanges()`` first, and only then the desired
canonicalized range passed to ``lookupParseRange()``.

See also: `lookup`_, `lookupCanonicalizeRanges`_

hasFeature
~~~~~~~~~~

Returns 1 if given feature is supported, 0 if not (or unknown). The
syntax is:

::

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
~~~~~~

Substitute another value for NULL values. Syntax:

::

       ifNull(testVal, replaceVal)

If ``testVal`` is a SQL NULL value, then ``replaceVal`` (cast to the
type of ``testVal``) is returned; otherwise ``testVal`` is returned.
This function can be used to ensure that NULL value(s) in a column are
replaced with a non-NULL value, if a non-NULL value is required:

::

        SELECT ifNull(myColumn, 'Unknown') FROM myTable;


isNull
~~~~~~

Tests a value, and returns a ``long`` value of 1 if NULL, 0 if not.
Syntax:

::

       isNull(testVal)

::

        SELECT isNull(myColumn) FROM myTable;

Note that Texis ``isNull`` behavior differs from some other SQL implementations; see
also `ifNull`_.

xmlTreeQuickXPath
~~~~~~~~~~~~~~~~~

Extracts information from an XML document.

::

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

::

    SELECT * from myTable where xmlTreeQuickXPath(data,
    '/extraInfo/author') = 'John Doe'
