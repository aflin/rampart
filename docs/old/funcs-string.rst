String Functions
----------------

abstract
~~~~~~~~

Generate an abstract of a given portion of text. The syntax is

::

       abstract(text[, maxsize[, style[, query]]])

The abstract will be less than ``maxsize`` characters long, and will
attempt to end at a word boundary. If ``maxsize`` is not specified (or
is less than or equal to 0) then a default size of 230 characters is
used.

The ``style`` argument is a string or integer, and allows a choice
between several different ways of creating the abstract. Note that some
of these styles require the ``query`` argument as well, which is a
Metamorph query to look for:

-  | ``dumb`` (0)
   | Start the abstract at the top of the document.

-  | ``smart`` (1)
   | This style will look for the first meaningful chunk of text,
     skipping over any headers at the top of the text. This is the
     default if neither ``style`` nor ``query`` is given.

-  | ``querysingle`` (2)
   | Center the abstract contiguously on the best occurence of ``query``
     in the document.

-  | ``querymultiple`` (3)
   | Like ``querysingle``, but also break up the abstract into multiple
     sections (separated with “``...``”) if needed to help ensure all
     terms are visible. Also take care with URLs to try to show the
     start and end.

-  | ``querybest``
   | An alias for the best available query-based style; currently the
     same as ``querymultiple``. Using ``querybest`` in a script ensures
     that if improved styles become available in future releases, the
     script will automatically “upgrade” to the best style.

If no ``query`` is given for the ``query``\ :math:`...` modes, they fall
back to ``dumb`` mode. If a ``query`` is given with a
*non-*\ ``query``\ :math:`...` mode (``dumb``/``smart``), the mode is
promoted to ``querybest``. The current locale and index expressions also
have an effect on the abstract in the ``query``\ :math:`...` modes, so
that it more closely reflects an index-obtained hit.

::

         SELECT     abstract(STORY, 0, 1, 'power struggle')
         FROM       ARTICLES
         WHERE      ARTID = 'JT09115' ;

text2mm
~~~~~~~

Generate ``LIKEP`` query. The syntax is

::

       text2mm(text[, maxwords])

This function will take a text expression, and produce a list of words
that can be given to ``LIKER`` or ``LIKEP`` to find similar documents.
``text2mm`` takes an optional second argument which specifies how many
words should be returned. If this is not specified then 10 words are
returned. Most commonly ``text2mm`` will be given the name of a field.
If it is an ``indirect`` field you will need to call ``fromfile`` as
shown below:

::

         SELECT     text2mm(fromfile(FILENAME))
         FROM       DOCUMENTS
         WHERE      DOCID = 'JT09115' ;

You may also call it as ``texttomm()`` instead of ``text2mm()`` .

keywords
~~~~~~~~

Generate list of keywords. The syntax is

::

       keywords(text[, maxwords])

keywords is similar to text2mm but produces a list of phrases, with a
linefeed separating them. The difference between text2mm and keywords is
that keywords will maintain the phrases. keywords also takes an optional
second argument which indicates how many words or phrases should be
returned.

length
~~~~~~

Returns the length in characters of a ``char`` or ``varchar``
expression, or number of strings/items in other types. The syntax is

::

      length(value[, mode])

For example:

::

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

The optional ``mode`` argument is a :doc:`stringcomparemode <stringcompmode>`-style compare
mode to use; see the Vortex manual on for details on syntax and the
default. If ``mode`` is not given, the current apicp :doc:`stringcomparemode <stringcompmode>`
is used. Currently the only pertinent ``mode`` flag is “iso-8859-1”,
which determines whether to interpret ``value`` as ISO-8859-1 or UTF-8.
This can alter how many characters long the string appears to be, as
UTF-8 characters are variable-byte-sized, whereas ISO-8859-1 characters
are always mono-byte. The ``mode`` argument was added in version 6.

In version 5.01.1226622000 20081113 and later, if given a ``strlst``
type ``value``, ``length()`` returns the number of string values in the
list. For other types, it returns the number of values, e.g. for
``varint`` it returns the number of integer values.


lower
~~~~~

Returns the text expression with all letters in lower-case. The syntax
is

::

      lower(text[, mode])

For example:

::

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

Added in version 2.6.932060000.

The optional ``mode`` argument is a string-folding mode in the same
format as ; see the Vortex manual for details on the syntax and default.
If ``mode`` is unspecified, the current apicp :doc:`stringcomparemode <stringcompmode>` setting
– with “+lowercase” aded – is used. The ``mode`` argument was added in
version 6.

upper
~~~~~

Returns the text expression with all letters in upper-case. The sytax is

::

      upper(text[, mode])

For example:

::

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

Added in version 2.6.932060000.

The optional ``mode`` argument is a string-folding mode in the same
format as ; see the Vortex manual for details on the syntax and default.
If ``mode`` is unspecified, the current apicp :doc:`stringcomparemode <stringcompmode>` setting
– with “+uppercase” added – is used. The ``mode`` argument was added in
version 6.

initcap
~~~~~~~

Capitalizes text. The syntax is

::

      initcap(text[, mode])

Returns the text expression with the first letter of each word in title
case (i.e. upper case), and all other letters in lower-case. For
example:

::

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

Added in version 2.6.932060000.

The optional ``mode`` argument is a string-folding mode in the same
format as ; see the Vortex manual for details on the syntax and default.
If ``mode`` is unspecified, the current apicp :doc:`stringcomparemode <stringcompmode>` setting
– with “+titlecase” added – is used. The ``mode`` argument was added in
version 6.

sandr
~~~~~

Search and replace text.

::

       sandr(search, replace, text)

Returns the text expression with the search REX expression replaced with
the replace expression. See the REX documentation and the Vortex sandr
function documentation for complete syntax of the search and replace
expressions.

::

         SELECT  NAME, sandr('>>=SYS=', 'SYSTEM TABLE ', NAME) DESC
         FROM    SYSTABLES

The results are:

::

      NAME                DESC
     SYSTABLES            SYSTEM TABLE TABLES
     SYSCOLUMNS           SYSTEM TABLE COLUMNS
     SYSINDEX             SYSTEM TABLE INDEX
     SYSUSERS             SYSTEM TABLE USERS
     SYSPERMS             SYSTEM TABLE PERMS
     SYSTRIG              SYSTEM TABLE TRIG
     SYSMETAINDEX         SYSTEM TABLE METAINDEX

Added in version 3.0

separator
~~~~~~~~~

Returns the separator character from its ``strlst`` argument, as a
``varchar`` string:

::

       separator(strlstValue)

This can be used in situations where the ``strlstValue`` argument may
have a nul character as the separator, in which case simply converting
``strlstValue`` to ``varchar`` and looking at the last character would
be incorrect. Added in version 5.01.1226030000 20081106.

stringcompare
~~~~~~~~~~~~~

Compares its string (``varchar``) arguments ``a`` and ``b``, returning
-1 if ``a`` is less than ``b``, 0 if they are equal, or 1 if ``a`` is
greater than ``b``:

::

      stringcompare(a, b[, mode])

The strings are compared using the optional ``mode`` argument, which is
a string-folding mode in the same format as ; see the Vortex manual for
details on the syntax and default. If ``mode`` is unspecified, the
current apicp :doc:`stringcomparemode <stringcompmode>` setting is used. Function added in
version 6.00.1304108000 20110429.

stringformat
~~~~~~~~~~~~

Returns its arguments formatted into a string (``varchar``), like the
equivalent Vortex function ``<strfmt>`` (based on the C function
``sprintf()``):

::

      stringformat(format[, arg[, arg[, arg[, arg]]]])

The ``format`` argument is a ``varchar`` string that describes how to
print the following argument(s), if any.

