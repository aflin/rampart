
Server Properties
-----------------

There are a number of properties that are settable in the SQL Engine.
They do not need to be changed unless a change in the  behavior of 
the system is desired.

Using the command line utility
:ref:`tsql <tsql:Tsql Command Line Utility>`
the properties are set using the following SQL syntax:

.. code-block:: sql

        SET property = value;

In Rampart, this is accomplished by using the 
:ref:`rampart-sql:set()` function:

.. code-block:: javascript

   sql.set({
      property1: value1
      [, property2: value2, ...]
   });

The ``property`` names are case insensitive and may be specified in
*camelCase*, *lower case*, *UPPER CASE* or as you prefer.  

The `Server Properties` set using either 
:ref:`tsql <tsql:Tsql Command Line Utility>`
or :ref:`rampart-sql:set()` are for the most part identical.
However, note below that a few properties (such as ``noiseList``) can only
be set using the :ref:`rampart-sql:set()` function.

In :ref:`tsql <tsql:Tsql Command Line Utility>`
the ``value`` can be one of four types depending on the property: Number,
Boolean, Array or String.  A Boolean value is either an integer  – ``0`` is false,
``1`` is true. Using the :ref:`rampart-sql:set()`
function, Booleans are set using the JavaScript Booleans ``true`` or ``false``.

NOTE:
   In Rampart Javascript, it is possible to use ``sql.exec("SET property = value;")``.  
   However doing so will set the property for every open ``sql`` handle
   opened with ``new Sql.init()`` and not allow settings to be automatically
   reapplied when using multiple ``sql`` handles.  Thus one should always
   use ``sql.set()`` in order to maintain distinct settings per handle.


Search and optimization parameters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These settings affect the way that Texis will process the search. They
include settings which change the meaning of the search, as well as how
the search is performed.


defaultLike
"""""""""""
    Defines which sort of search should occur when a ``like`` or
    ``contains`` operator is in the query. The default setting of
    ``like`` behaves in the normal manner. Other settings that can be
    set are ``like3``, ``likep``, ``liker`` and ``matches``. In
    each case the ``like`` operator will act as if the specified
    operator had been used instead.


matchMode
"""""""""
    Changes the behavior of the ``matches`` clause.  The default behavior is
    to use ``_`` (underscore) and ``%`` (percent) as the single and
    multi-character character wildcards.  Setting ``matchmode`` to 1 will
    change the wildcards to ``?`` (question-mark) and ``*`` (asterisk).


pRedoPtType
"""""""""""
    The Texis engine can reorder the ``where`` clause in an attempt to
    make it faster to evaluate. There are a number of ways this can be
    done; the ``pRedoPtTpye`` property controls the way it reorders. The
    values are ``0`` to not reorder, ``1`` to evaluate ``and`` first, ``2`` to
    evaluate ``or`` first. The default is 0.

.. can be removed?
    ignoreCase
    """"""""""
    **Note:** Deprecated; see ``stringcomparemode`` setting which
    supercedes this. Setting ``ignorecase`` to true will cause string
    comparisons (equals, sorting, etc.) in the SQL engine to ignore
    case, e.g. “``A``” will compare identical to “``a``”. (This is
    distinct from *text* comparisons, e.g. the ``LIKE`` operator, which
    ignore case by default and are unaffected by ``ignorecase``.)
    **Note:** This setting will also affect any indices that are built;
    the value set at index creation will be saved with the index and
    used whenever that index is used. **Note:** In versions prior to
    version 5.01.1208300000 20080415, the value of ``ignorecase`` *must*
    be explicitly set the same when an index is created, when it or its
    table is updated and when it is used in a search, or incorrect
    results and/or corrupt indexes may occur. In later versions, this is
    not necessary; the saved-at-index-creation value will automatically
    be used. In version 6 and later, this setting toggles the
    ``ignorecase`` flag of the ``stringcomparemode`` setting, which
    supercedes it.


textSearchMode 
"""""""""""""" 

   ``textSearchMode`` changes the mode and flags for text searches.  It
   controls case-sensitivity and other character-folding aspects of
   Metamorph text searches.  

   The ``textSearchMode`` default is
   ``unicodemulti, ignorecase, ignorewidth, ignorediacritics, expandligatures``
   (note that UTF-8 text is expected, since iso-8859-1 is not specified in the
   default).

   See `stringCompareMode/textSearchMode parameters`_ below for possible
   settings.


stringCompareMode
"""""""""""""""""
    Mode and flags for the following function and
    properties:

    - :ref:`sql-server-funcs:stringcompare`
    - :ref:`sql-server-funcs:length`
    - :ref:`sql-server-funcs:lower`
    - :ref:`sql-server-funcs:upper`
    - :ref:`sql-server-funcs:initcap`

    The ``stringcomparemode`` parameter specifies string compares (e.g. 
    equals, less-than or greater-than) for the
    :ref:`sql-server-funcs:stringcompare` function.  It also controls the
    default mode for the non-case-style flags/mode for the functions
    :ref:`sql-server-funcs:length`, :ref:`sql-server-funcs:lower`,
    :ref:`sql-server-funcs:upper` and :ref:`sql-server-funcs:initcap`.

    Its value is given in the same format as the `textSearchMode`_ setting, 
    (see `stringCompareMode/textSearchMode parameters`_ below)
    but the default is "``unicodemulti, respectcase``" — i.e. 
    characters must be identical to match, though ISO-8859-1 vs.  UTF-8
    encoding may be ignored.

    A regular (B-tree) index will always use the ``stringCompareMode`` value that
    was set at its creation, not the current value. However, when multiple
    regular indexes exist on the same fields, at search time the Texis optmizer
    will attempt to use the index whose (creation-time) ``stringCompareMode`` is
    closest to the current value. This allows some dynamic flexibility in
    supporting queries with different ``stringCompareMode`` values (e.g.
    case-sensitive vs. insensitive). 

stringCompareMode/textSearchMode parameters
"""""""""""""""""""""""""""""""""""""""""""

   The value consists of a comma-separated list of
   values: a *case-folding style*, zero or more *optional flags*, and a
   *case-folding mode*.  The ``textSearchMode`` setting may be altered
   (instead of cleared and set) by using ``+`` or ``-`` in front of the
   given values to denote adding or removing just those values, rather than
   clearing the whole setting first.  This makes it easier to alter just the
   desired parts, without having to specify the remainder of the setting. 
   For example, ``+respectcase, ignorewidth, -expandligatures`` sets the
   case style to case-sensitive, turns on ignorewidth and turns off ligature
   expansion, without changing other flags such as ``ignoreDiacritics``. 

   Note that all option values are case-insensitive (e.g. ``ignoreDiacritics`` 
   is the same as ``ignorediacritics``).

   Note also that negation (``-``) can only be used with values that are "on/off",
   (the *optional flags*).  *Case-folding style* and *case-folding mode* cannot be
   negated.  ``+`` and ``-`` remain in effect for following values, until another
   ``+``, ``-`` or ``=`` (clear the setting first) is given.

   The *case-folding style* determines the result of the case folding operation.
   It is exactly oneof:

      *  ``respectCase`` aka ``preserveCase`` aka ``caseSensitive`` -  Do not
         change case at all, for case-sensitive searches.

      *  ``ignoreCase`` aka ``igncase`` aka ``caseInsensitive`` - Fold case for
         caseless (case-insensitive) matching; this is the default style for
         ``textSearchMode``.  This typically (but not always) means characters are folded
         to their lowercase equivalents.

   .. these appear to make no difference for any rampart or sql server functions

      *  ``upperCase`` - Fold to uppercase. Note: This style is for functions that actually
         return a string, e.g. <strfold>; it should not be used in comparison
         situations such as indexes and searches as its comparison behavior is
         undefined. See the stringcomparemode setting, here.

      *  ``lowerCase`` - Fold to lower-case. Note: This style is for functions that
         actually return a string, e.g. <strfold>; it should not be used in
         comparison situations such as indexes and searches as its comparison
         behavior is undefined. See the stringcomparemode setting, here.

      *  ``titleCase`` - Fold to title-case. Titlecase means the first character of a word
         is uppercased, while the rest of the word is lowercased. Note: This style is
         for functions that actually return a string, e.g. <strfold>; it should not
         be used in comparison situations such as indexes and searches as its
         comparison behavior is undefined. See the `stringCompareMode`_ setting.

   Any combination of zero or more of the following *optional flags* may be given in
   addition to a case style:

      *  ``iso-8859-1`` aka ``iso88591`` - Interpret text as ISO-8859-1 encoded. This should
         only be used if all text is known to be in this character set. Only
         codepoints U+0001 through U+00FF can be supported. Any UTF-8 text will be
         misinterpreted.

         If this flag is disabled (the default), text is interpreted as UTF-8, and
         invalid bytes (if any) are interpreted as ISO-8859-1. This supports all
         UTF-8 characters, as well as most typical ISO-8859-1 data, if any happens to
         be accidentally mixed in.

         Typically, this flag is left disabled, and text is stored in UTF-8, since it
         supports a broader range of characters. Any other character set besides
         UTF-8 or ISO-8859-1 is not supported, and should be mapped to UTF-8.

      *  ``utf-8`` aka ``utf8`` - Alias for negating iso-8859-1. Specifying this disables
         the ``iso-8859-1`` flag.

      *  ``expandDiacritics`` aka ``expdiacritics`` - Expand certain phonological diacritics:
         umlauts over ``a``, ``o``, ``u`` expand to the vowel plus ``e`` (for German, e.g.
         ``für`` matches ``fuer``); circumflexes over ``e`` and ``o`` expand to the vowel
         plus ``s`` (for French, e.g. ``hôtel`` matches ``hostel``). The expanded ``e`` or
         ``s`` is optional-match - e.g. ``für`` also matches ``fur`` - but only against a
         non-optional char; i.e. ``hôtel`` does not match ``hötel`` (the ``e`` and ``s``
         collide), and ``für`` does not match ``füer`` (both optional ``e`` s must match
         each other). Also, neither the vowel nor the ``e``/``s`` will match an
         ignorediacritics-stripped character; this prevents ``für`` from matching
         ``fu'er``.

      *  ``ignoreDiacritics`` aka ``igndiacritics`` - Ignore diacritic marks - Unicode
         non-starter or modifier symbols resulting from NFD decomposition - e.g.
         diaeresis, umlaut, circumflex, grave, acute, tilde etc.

      *  ``expandLigatures`` aka ``expligatures`` - Expand ligatures, e.g. "œ" (U+0153) will
         match "oe". Note that even with this flag off, certain ligatures may still
         be expanded if necessary for case-folding under ignorecase with case mode
         unicodemulti (see below).

      *  ``ignoreWidth`` aka ``ignwidth`` - Ignore half- and full-width differences, e.g. for
         katakana and ASCII.

   Due to interactions between flags, they are applied in the order specified
   above, followed by case folding according to the case style (upper/lower
   etc.). E.g. expanddiacritics is applied before ignorediacritics, because
   otherwise the latter would strip the characters that the former expands.

   A *case-folding mode* may also be given in addition to the above; this
   determines how the case-folding style (e.g. upper/lower/title) is actually
   applied. It is one of the following:

      *  ``unicodemulti`` - Use the builtin Unicode 5.1.0 1-to-N-character folding tables.
         All locale-independent Unicode characters with the appropriate case
         equivalent are folded. A single character may fold to up to 3 characters, if
         needed; e.g. ``ß`` (the German es-zett character; U+00DF) will match "ss" and
         vice-versa under ignorecase. Note that additional ligature expansions may
         happen if expandligatures is set.  ``unicodemulti`` is the default mode.

      *  ``unicodemono`` - Use the builtin Unicode 5.1.0 1-to-1-character folding tables.
         All locale-independent Unicode characters with the appropriate case
         equivalent are folded. Note that even though this mode is 1-to-1-character,
         it is not necessarily 1-to-1-byte, i.e. a UTF-8 string may still change its
         byte length when folded, even though the Unicode character count will remain
         the same.

      *  ``ctype`` - Use the C ctype.h functions. Case folding will be OS and
         locale dependent (a locale should be set with the SQL `locale`_ property). Only
         codepoints U+0001 through U+00FF can be folded; e.g. most Western European
         characters are folded, but Cyrillic, Greek etc. are not. Note that while
         this mode is 1-to-1-character, it is not necessarily 1-to-1-byte, unless the
         iso-8859-1 flag is also in effect.

   In addition to the above styles, flags and modes, several aliases may be
   used, and mixed with flags. The aliases have the form:

   ::

      [stringCompareMode|textSearchMode][default|builtin]

   ``stringcomparemode`` or ``textsearchmode`` refers to that setting's value (if
   not given: the setting being modified). ``default`` refers to the default value
   (modifiable with texis.ini) and ``builtin`` refers to the builtin factory
   default (if not given: the the alias refers to the current setting value).
   Example: ``stringcomparemodedefault,+ignorecase`` would obtain the default
   stringcomparemode setting (from texis.ini if available), but set the case
   style to ignorecase.

   A Metamorph index always uses the textsearchmode value that was set at its
   initial creation, not the current value. However, when multiple Metamorph
   indexes exist on the same fields, at search time the Texis optimizer will
   attempt to use the index whose (creation-time) textsearchmode is closest to
   the current value.



.. todo: find out if these are applicable ..
   tracemetamorph
   """"""""""""""
       Sets the ``tracemetamorph`` debug property; see Vortex manual for
       details. Added in version 7.00.1375225000 20130730.


   tracerowfields
   """"""""""""""
       Sets the ``tracerowfields`` debug property; see Vortex manual for
       details. Added in version 7.02.1406754000 20140730.


   tracekdbf
   """""""""
       Sets the ``tracekdbf`` debug property; see Vortex manual for
       details.


   tracekdbffile
   """""""""""""
       Sets the ``tracekdbffile`` debug property; see Vortex manual for
       details.


   kdbfiostats
   """""""""""
       Sets the ``kdbfiostats`` debug property; see Vortex manual for
       details.


btreeCacheSize
""""""""""""""
    Index pages are cached in memory while the index is used. The size
    of the memory cache can be adjusted to improve performance. The
    default is 20, which means that 20 index pages can be cached. This
    can be increased to allow more pages to be cached in memory. This
    will only help performance if the pages will be accessed in random
    order, more than 20 will be accessed, and the same page is likely to
    be accessed at different times. This is most likely to occur in a
    join, when a large number of keys are looked up in the index.
    Increasing the size of the cache when not needed is likely to hurt
    performance, due to the extra overhead of managing a larger cache.
    The cache size should not be decreased below the default of 20, to
    allow room for all pages which might need to be accessed at the same
    time.


ramRows
"""""""
    When ordering large result sets, the data is initially ordered in
    memory, but if more than ``ramrows`` records are being ordered the
    disk will be used to conserve memory. This does slow down
    performance however. The default is 10000 rows. Setting ``ramRows``
    to 0 will keep the data in memory.


ramLimit
""""""""
    ``ramlimit`` is an alternative to ``ramrows``. Instead of limiting
    the number of records, the number of bytes of data in memory is
    capped. By default it is 0, which is unlimited. If both ``ramLimit``
    and ``ramRows`` are set then the first limit to be met will trigger
    the use of disk.


bubble
""""""
    Normally Texis will bubble results up from the index to the user.  This
    means that a matching record will be found in the index, returned to the
    user, then the next record found in the index, and so forth till the end
    of the query.  This normally generates the first results as quickly as
    possible.  By setting ``bubble`` to 0 the entire set of matching record
    handles will be read from the index first, and then each record
    processed from this list.


optimize,noOptimize
"""""""""""""""""""
    Enable or disable optimizations. The argument should be a comma
    separated list of optimizations that you want to enable or disable.
    The available optimizations are:

    join
        Optimize join table order. The default is enabled. When enabled
        Texis will arrange the order of the tables in the ``FROM``
        clause to improve the performance of the join. This can be
        disabled if you believe that Texis is optimizing incorrectly. If
        it is disabled then Texis will process the tables in the left to
        right order, with the first table specified being the driving
        table.

    compoundindex
        Allow the use of compound indexes to resolve searches. For
        example if you create an index on table (field1, field2), and
        then search where field1 = value and field2 = value, it will use
        the index to resolve both portions of this. When disabled it
        would only look for field1 in the index.

    countstar
        Use any regular index to determine the number of records in the
        table. If disabled Texis will read each record in the table to
        count them.

    minimallocking
        Controls whether the table will be locked when doing reads of
        records pointed to by the index used for the query. This is
        enabled by default, which means that read locks will not be
        used. This is the optimal setting for databases which are mostly
        read, with few writes and small records.

    groupby
        This setting is enabled by default and will cause the data to be
        read only once to perform a group by operation. The query should
        produce indentical results whether this is enabled or disabled,
        with the performance being the only difference.

    faststats
        When enabled, which is the default, and when the appopriate
        indexes exist Texis will try and resolve aggregate functions
        directly from the index that was used to perform the ``WHERE``
        clause.

    readlock
        When enabled, which is the default, Texis will use readlocks
        more efficiently if there are records that are scanned, but
        don’t match the query. Texis will hold the read lock until a
        matching record is found, rather than getting and releasing a
        read lock for every record read. If you are suffering from lock
        contention problems, with writes waiting, then this can be
        disabled, which will allow more opportunity for the write locks
        to be granted. This is not normally suggested, as the work
        required to grant and release the locks would typically negate
        the benefit.

    analyze
        When enabled, which is the default, Texis will analyze the query
        for which fields are needed. This can allow for more efficient
        query processing in most cases. If you are executing many
        different SQL statements that are not helped by the analysis you
        can disable this.

    skipahead
        When enabled, which is the default, Texis will skipahead as
        efficiently as possible, typically used with the ``skip`` parameter
        in ``sql.exec()``. If disabled Texis will perform full processing on
        each skipped record, and discard the record. Note that this will
        have no effect on a ``delete`` statement (skipped rows are still
        deleted, but their values are not returned).

    likewithnots
        When enabled (default), ``LIKE``/``LIKEP``-type searches with
        NOT sets (negated terms) are optimized for speed.

    shortcuts
        When enabled (default), a fully-indexed ``LIKE``/``LIKEIN``
        clause ``OR``\ ed with another fully-indexed ``LIKE``/``LIKEIN``
        should not cause an unnecessary post-process for the ``LIKE``\ s
        (and entire query).

    likehandled
        When enabled (default), a fully-indexed ``LIKE``/``LIKEIN``
        clause ``OR``\ ed with another fully-indexed
        non-\ ``LIKE``/``LIKEIN`` clause should not cause an unnecessary
        post-process for the ``LIKE`` (and entire query).

        Also, linear and post-process ``LIKE``/``LIKEIN`` operations
        caused not by the Metamorph query itself, but by the presence of
        another ``OR``\ ed/\ ``AND``\ ed clause, do not check
        ``allinear`` nor ``alpostproc`` when this optimization is
        disabled (i.e. they will perform the linear or post-process
        regardless of settings, silently). E.g. fully-indexed ``LIKE``
        ``OR``\ ed with linear clause, or two fully-indexed ``LIKE``\ s
        ``AND``\ ed (where the first’s results are under
        ``maxlinearrows``), could cause linear search or
        post-processing, respectively, of an otherwise fully-indexable
        Metamorph query.

    indexbatchbuild
        When enabled, indexes are built as a batch, i.e. the table is
        read-locked continuously. When disabled (the default), the table
        is read-locked intermittently if possible (e.g. Metamorph
        index), allowing table modifications to proceed even during
        index creation. A continuous read lock allows greater read
        buffering of the table, possibly increasing index build speed
        (especially on platforms with slow large-file ``lseek``
        behavior), at the expense of delaying table updates until after
        the index is nearly built, which may be quite some time. Note
        that non-Metamorph indexes are *always* built with a continuous
        read lock – regardless of this setting – due to the nature of
        the index.

    indexdataonlycheckpredicates
        When enabled (the default), allows the index-data-only
        optimization [1]_ to proceed even if the SELECT columns are
        renamed or altered in expressions. Previously, the columns had
        to be selected as-is with no renaming or expressions.

    indexvirtualfields
        When enabled (the default), attempts to reduce memory usage when
        indexing virtual fields (especially with large rows) by freeing
        certain buffers when no longer needed.  Currently this only applies
        to Metamorph and Metamorph inverted ("text") indexes.

    Example: ``sql.set({nooptimize:"minimallocking"});``


options,noOptions
"""""""""""""""""
    Enable or disable certain options. The argument should be a comma
    separated list of options to enable or disable. All options are off
    by default. The available options are:

    triggers
        When on, *disable* the creation of triggers.

    indexCache
        Cache certain Metamorph index search results, so that an
        immediately following Metamorph query with the same ``WHERE``
        clause might be able to re-use the index results without
        re-searching the index. E.g. may speed up a
        ``SELECT field1, field2, ...`` Metamorph query that follows a
        ``SELECT count(*)`` query with the same ``WHERE`` clause.

    ignoreMissingFields
        Ignore missing fields during an ``INSERT`` or ``UPDATE``, i.e.
        do not issue a message and fail the query if attempting to
        insert a non-existent field. This may be useful if a SQL
        ``INSERT`` statement is to be used against a table where some
        fields are optional and may not exist.

    Example: ``sql.set({options:"indexCache"});``


ignoreNewList
"""""""""""""
    When processing a Metamorph query you can instruct Texis to ignore the
    unoptimized portion of a Metamorph index by issuing the SQL ``set
    ignorenewlist = 1;`` or ``sql.set({ignoreNewList:true});``.  If you have
    a continually changing dataset, and the index is frequently updated then
    the default of processing the unoptimized portion is probably correct. 
    If the data tends to change in large batches, followed by a
    reoptimization of the index then the large batch can cause significant
    processing overhead.  In that case it may be wise to enable the
    ``ignoreNewList`` option.  If the option is enable then records that
    have been updated in the batch will not be found with Metamorph queries
    until the index has been optimized.


indexWithin
"""""""""""
    How to use the Metamorph index when processing “within :math:`N`”
    (w/\ :math:`N`) ``LIKE``-type queries. It is an integer combination
    of bit flags:

    0x01
        : Use index for w/\ :math:`N` searches when ``withinmode`` is
        “``char [span]``”

    0x02
        : Use index for w/\ :math:`N` searches when ``withinmode`` is
        “``word [span]``”

    0x04
        : Optimize within-chars window down

    0x08
        : Do not scale up intervening (non-query) words part of window
        to account for words matching multiple index expressions, which
        rarely occur; this reduces false (too wide) hits from the index.
        Also do not require post-processing if multiple index
        expressions. In rare cases valid hits may be missed if an
        intervening word does index-match multiply; the :math:`N` value
        can simply be increased in the query to return these.

    The default is 0xf.


wildOneWord
"""""""""""
    Whether wildcard expressions in Metamorph queries span a single word
    only, i.e. for multi-substring wildcards. If 0 (false), the query
    “``st*ion``” matches “``stallion``” as well as “stuff an onion”. If
    1 (true), then “``st*ion``” only matches “``stallion``”, and
    linear-dictionary index searches are possible (if enabled), because
    there are no multi-word matches to (erroneously) miss.

    The default is 1 (true).


wildSufMatch
""""""""""""
    Whether wildcard expressions in Metamorph queries suffix-match their
    trailing substrings to the end of words. If 0 (false), the query
    “``*so``” matches “``also``” as well as “``absolute``”. If 1 (true),
    then “``*so``” only matches “``also``”. Affects what terms are
    matched during linear-dictionary index searches.

    The default is 1 (true)


wildSingle
""""""""""
    An alias for setting `wildOneWord`_ and `wildSufMatch`_ together,
    which is usually desired.


alLinearDict
""""""""""""
    Whether to allow linear-dictionary Metamorph index searches.
    Normally a Metamorph query term is either binary-index searchable
    (fastest), or else must be linear-table searched (slowest). However,
    certain terms, while not binary-index searchable, can be
    linear-dictionary searched in the index, which is slower than
    binary-index, yet faster than linear-table search. Examples include
    leading-prefix wildcards such as “``*tion``”. The default is 0
    (false), since query protection is enabled by default. Note that
    ``wildSingle`` should typically be set true so that wildcard syntax
    is more likely to be linear-dictionary searchable.


indexMinSublen
""""""""""""""
    The minimum number of characters that a Metamorph index word
    expression must match in a query term, in order for the term to
    utilize the index. A term with fewer than ``indexMinSublen``
    indexable characters is assumed to potentially match too many words
    in the index for an index search to be more worthwhile/faster than a
    linear-table search.

    For binary-index searchable terms, ``indexMinSublen`` is tested
    against the minimum prefix length; e.g. for query “``test.#@``” the
    length tested is 4 (assuming default index word expression of
    “``\alnum{2,99}``”). For linear-dictionary index searches, the
    length tested is the total of all non-wildcard characters; e.g. for
    query “``ab*cd*ef``” the length tested is 6.

    The default for ``indexminsublen`` is 2.

    Note that the query – regardless of index or linear search – must also
    pass the `qMinPrelen`_ setting.


dropWordMode
""""""""""""
    How to remove words from a query set when too many are present
    (`qMaxSetWords`_ or `qMaxWords`_ exceeded) in an index search,
    e.g. for a wildcard term. The possible values are 0 to retain
    suffixes and most common words up to the word limit, or 1 to drop
    the entire term. The default is 0.


metamorphStrlstMode
"""""""""""""""""""
    How to convert a ``strlst`` Metamorph query to a regular string
    Metamorph query.  For example, for the ``strlst`` query composed of the
    3 strings “``one``”, “``two``”, and “``bear arms``”, the various modes
    would convert as follows:

    *    ``allwords``
         Space-separate each string, e.g. “one two bear arms”.

    *    ``anywords``
         Space-separate each string and append ``@0``, e.g. 
         ``\ ‘one two bear arms @0``.

    *    ``allphrases``
         Space-separate and double-quote each string, e.g. ``"one" "two" "bear arms"``.

    *    ``anyphrases``
         Space-separate and double-quote each string, and append
         \ ``@0``, e.g. ``"one" "two" "bear arms" @0``.

    *    ``equivlist``
         Make the string list into a parenthetical comma-separated list,
         e.g. “(one,two,bear arms)”.

    The default is ``equivlist``.

.. probably don't want these ones included
    compatibilityversion
    """"""""""""""""""""
    [SqlPropertyCompatibilityVersion]

    Sets the Texis compatibility version – the version to attempt to
    behave as – to the given string, which is a Texis version of the
    form “:math:`major`\ [.:math:`minor`\ [.:math:`release`]]”, where
    :math:`major` is a major version integer, :math:`minor` is a minor
    version integer, and :math:`release` is a release integer. Added in
    version 7. See the ``<vxcp compatibilityversion>`` setting in Vortex
    for details. See also the Compatibility Version setting (p. ) in
    texis.ini, which the ``compatibilityversion`` setting defaults to.

    failifincompatible
    """"""""""""""""""
    Whenever set nonzero/true, and the most recent
    ``compatibilityversion`` setting attempt failed, then all future SQL
    statements will fail with an error message. Since there is no
    conditional (“if”) statement in SQL, this allows a SQL script to
    essentially abort if it tries to set a Texis compatibility version
    that is unsupported, rather than continue with possibly undesired
    side effects. Added in version 7. See also
    ``<vxcp compatibilityversion>`` in Vortex, which obviates the need
    for this setting, as it has a checkable error return.


groupbymem
""""""""""
    When set ``true`` (the default), try to minimize memory usage
    during ``GROUP BY``/``DISTINCT`` operations (e.g. when using an
    index and sorting is not needed).

..  don't need this one either

    legacyversion7orderbyrank
    """""""""""""""""""""""""
    [SqlPropertyLegacyVersion7OrderByRank]

    If on, an ORDER BY $rank (or $rank-containing expression) uses
    legacy version 7 behavior, i.e. typically orders in numerically
    descending order, but may change to ascending (and have other
    idiosyncrasies) depending on index, expression and ``DESC`` flag
    use. If disabled, such ORDER BYs are consistent with others:
    numerically ascending unless ``DESC`` flag given (which would
    typically be given, to maintain descending-numerical-rank order).

    The default is the value of the Legacy Version 7 Order By Rank
    setting (p. ) in conf/texis.ini, which is off by default with
    ``compatibilityversion`` 8 and later, on in earlier versions
    (``compatibilityversion`` defaults to Texis Version). Added in
    version 7.06.1508871000 20171024.

    Note that this setting may be removed in a future release, as its
    enabled behavior is deprecated. Its existence is only to ease
    transition of old code when upgrading to Texis version 8, and thus
    should only be used temporarily. Old code should be updated to
    reflect version 8 default behavior – and this setting removed – soon
    after upgrading.


Metamorph parameters
~~~~~~~~~~~~~~~~~~~~

These settings affect the way that text searches are performed. They are
equivalent to changing the corresponding parameter in the profile, or by
calling the Metamorph API function to set them (if there is an
equivalent). They are:


minWordLen
""""""""""
    The smallest a word can get due to suffix and prefix removal.  Removal
    of trailing vowel or double consonant can make it a letter shorter than
    this.  Default ``255`` (effectively turning suffix and prefix removal
    off; a reasonable value for prefix and suffix processing would be a
    value close to ``5``, depending on the application).  Note that this is
    different from qminwordlen, which is the minimum word length allowed in
    a query.

keepNoise
"""""""""
    Whether noise words should be used to resolve queries and to build text
    indexes.  Default is ``false`` (filter out noise words).

suffixProc
""""""""""
    Whether suffixes should be stripped from the words to find a match. 
    Default ``true``.  Note that ``minwordlen`` must be set to an
    appropriate size as well.


prefixProc
""""""""""
    Whether prefixes should be stripped from the words to find a match.
    Turning this on is not suggested when using a Metamorph index.
    Default ``false``.  Note that ``minwordlen`` must be set to an
    appropriate size as well.

rebuild
"""""""
    Make sure that the word found can be built from the root and
    appropriate suffixes and prefixes. This increases the accuracy of
    the search. Default ``false``.

useEquiv
""""""""
    AKA ``keepEqvs``.  Perform thesaurus lookup on unaltered terms.  Negates
    the meaning of ``~``.  If set ``true`` then the word and all
    equivalences will be searched for unless the term is preceded with a
    ``~``.  If it is ``false`` then only the query word is searched for
    (unless the term is preceded with a ``~``).  Default is ``false``.  Note
    `alEquivs`_ must be set ``true`` for any thesaurus lookup to occur.

.. possibly include this later or in a more appropriate section
    inc\_sdexp
    """"""""""
        Include the start delimiter as part of the hit. This is not
        generally useful in Texis unless hit offset information is being
        retrieved. Default off.

    inc\_edexp
    """"""""""
        Include the end delimiter as part of the hit. This is not generally
        useful in Texis unless hit offset information is being retrieved.
        Default on.

    sdexp
    """""
        Start delimiter to use: a regular expression to match the start of a
        hit. The default is no delimiter.

    edexp
    """""
        End delimiter to use: a regular expression to match the start of a
        hit. The default is no delimiter.

intersects
""""""""""
    Default number of intersections in Metamorph queries; overridden by
    the ``@`` operator. Note that this is generally not needed for a
    ``likep`` search.

hyphenPhrase
""""""""""""
    Controls whether a hyphen between words searches for the phrase of the
    two words next to each other, or searches for the hyphen literally.  The
    default value of ``true`` will search for the two words as a phrase. 
    Setting it to ``false`` will search for a single term including the
    hyphen.  If you anticipate setting hyphenphrase to 0 then you should
    modify the index word expression to include hyphens.

wordc
"""""
    For language or wildcard query terms during linear (non-index) searches,
    this defines which characters in the document consitute a word.  When a
    match is found for language/wildcard terms, the hit is expanded to
    include all surrounding word characters, as defined by this setting. 
    The resulting expansion must then match the query term for the hit to be
    valid.  (This prevents the query “``pond``” from inadvertently matching
    the text “``correspondence``”, for example.) The value is specified as a
    REX character set.  The default setting is ``[\alpha\']`` which
    corresponds to all letters and apostrophe.  For example, to exclude
    apostrophe and include digits use: ``set wordc='[\alnum]'`` or
    ``sql.set({wordc:"[\\alnum]"});`` Note that this setting is for linear
    searches: what constitutes a word for Metamorph *index* searches is
    controlled by the index expressions (`addexp`_ property.  Also note that
    non-language, non-wildcard query terms (e.g.  ``123`` with default
    settings) are not word-expanded.


langc
"""""
    Defines which characters make a query term a language term. A
    language term will have prefix/suffix processing applied (if
    enabled), as well as force the use of ``wordc`` to qualify the hit
    (during linear searches). Normally ``langc`` should be set the same
    as ``wordc`` with the addition of the phrase characters space and
    hyphen. The default is ``[\alpha\' \-]``.

withinMode
""""""""""
    A space or comma separated unit and optional type for
    the "within-N" operator (e.g. ``w/5``). The unit is one of:

   *  ``char`` for within-N characters
   *  ``word`` for within-N words

   The optional type determines what distance the operator measures.  It is
   one of the following:

   *  ``radius`` (the default if no type specified when set) indicates all sets must
      be within a radius N of an "anchor" set, i.e. there is a set in the match
      such that all other sets are within N units right of its right edge or N
      units left of its left edge.

   *  ``span`` indicates all sets must be within an N-unit span.

    Example: ``sql.set({withinmode: "char, span"});``.

phrasewordproc
""""""""""""""
    Which words of a phrase to do suffix/wildcard processing on. The
    possible values are:

    * ``mono`` to treat the phrase as a monolithic
      word (i.e. only last word processed, but entire phrase counts
      towards ``minwordlen``).  

    * ``none`` for no suffix/wildcard processing on phrases.

    * ``last`` to process just the last word.  Note that a phrase is
      multi-word, i.e.  a single word in double-quotes is not considered a
      phrase, and thus ``phrasewordproc`` does not apply.

    * ``all`` to process all words in the phrase.  Only applicable for
      searches against a text index and not applicable to linear searches. 

    The default value is ``last``.

.. skip for now

    mdparmodifyterms
    """"""""""""""""
        If nonzero, allows the Metamorph query parser to modify search terms
        by compression of whitespace and quoting/unquoting. This is for
        back-compatibility with earlier versions; enabling it will break the
        information from bit 4 of ``mminfo()`` (query offset/lengths of
        sets). Added in version 5.01.1220640000 20080905.

defSuffRm
"""""""""
    AKA ``defsufrm``.  Whether to remove a trailing vowel, or one of a
    trailing double consonant pair, after normal suffix processing, and if
    the word is still ``minwordlen`` or greater.  This only has effect if
    suffix processing is enabled (``suffixProc`` set ``true`` and the
    original word is at least minwordlen long).  Default value is ``true``.

eqPrefix
""""""""
    AKA ``equivsFile`` when used from ``sql.set()``.  The name of the
    equivalence file.  Default is "builtin", which uses the built-in
    :ref:`equivalence list <mm3:Thesaurus Customization>`.

exactPhrase 
"""""""""""
    Whether to exactly resolve the noise words in phrases.

    * ``true`` - a phrase such as "state of the art" will only match those
      exact words; however this may require post-processing to resolve the
      noise words "of the" (potentially slower).

    * ``false`` - any word is permitted in place of the noise words, and
      no post-processing is done: faster but potentially less accurate.

    * ``"ignorewordposition"`` - the same as off, but non-noise words are
      permitted in any order or position; essentially emulates behavior of a
      non-inverted Metamorph index with no post-processing, but on a
      Metamorph inverted index too.

    The default is ``false``.

.. skip for now
    inced (boolean, on by default) Whether to include the end delimiters in
    hits. Ignored for w/N (within N chars or words) delimiters.

    incsd (boolean, off by default) Whether to include the start delimiters in
    hits. Ignored for w/N (within N chars or words) delimiters.

noiseList
"""""""""
    The noise word list used during query processing. An array of strings.  The default
    noise list is:

    ::

       [
          "a",          "about",     "after",       "again",    "ago",       "all",
          "almost",     "also",      "always",      "am",       "an",        "and",
          "another",    "any",       "anybody",     "anyhow",   "anyone",    "anything",
          "anyway",     "are",       "as",          "at",       "away",      "back",
          "be",         "became",    "because",     "been",     "before",    "being",
          "between",    "but",       "by",          "came",     "can",       "cannot",
          "come",       "could",     "did",         "do",       "does",      "doing",
          "done",       "down",      "each",        "else",     "even",      "ever",
          "every",      "everyone",  "everything",  "for",      "from",      "front",
          "get",        "getting",   "go",          "goes",     "going",     "gone",
          "got",        "gotten",    "had",         "has",      "have",      "having",
          "he",         "her",       "here",        "him",      "his",       "how",
          "i",          "if",        "in",          "into",     "is",        "isn't",
          "it",         "just",      "last",        "least",    "left",      "less",
          "let",        "like",      "make",        "many",     "may",       "maybe",
          "me",         "mine",      "more",        "most",     "much",      "my",
          "myself",     "never",     "no",          "none",     "not",       "now",
          "of",         "off",       "on",          "one",      "onto",      "or",
          "our",        "ourselves", "out",         "over",     "per",       "put",
          "putting",    "same",      "saw",         "see",      "seen",      "shall",
          "she",        "should",    "so",          "some",     "somebody",  "someone",
          "something",  "stand",     "such",        "sure",     "take",      "than",
          "that",       "the",       "their",       "them",     "then",      "there",
          "these",      "they",      "this",        "those",    "through",   "till",
          "to",         "too",       "two",         "unless",   "until",     "up",
          "upon",       "us",        "very",        "was",      "we",        "went",
          "were",       "what",      "what's",      "whatever", "when",      "where",
          "whether",    "which",     "while",       "who",      "whoever",   "whom",
          "whose",      "why",       "will",        "with",     "within",    "without",
          "won't",      "would",     "wouldn't",    "yet",      "you",       "your"
       ]


   This setting can only be set using ``sql.set()``.

listNoise
"""""""""
    If not set to ``false``, the return object of ``sql.set()`` will include
    the property ``noiseList``, which will be set to an array containing the
    current noise list. 

    This setting can only be used via ``sql.set()``.


.. skip
    olddelim (boolean, off by default) Whether to emulate "old" delimiter
    behavior. If turned on, it is possible for a hit to occur outside dissimilar
    start and end delimiters, such as in this example text:

    start-delim ... end-delim ... hit ... start-delim ... end-delim
    Here the hit is "within" the outermost start and end delimiters, but it's
    not within the nearest delimiters. With olddelim off (the default), this hit
    now does not match: it would have to occur within the nearest delimiters,
    which would have to be in the correct order. (Added in version 3.0.950300000
    20000211. Previous versions behave as if olddelim were on.)



suffixList
""""""""""
    The suffix list used for suffix processing (if enabled) during
    search. An array of strings. The default suffix list is:

    ::

         [
             "'",       "able",   "age",     "aged",   "ager",
             "ages",    "al",     "ally",    "ance",   "anced",
             "ancer",   "ances",  "ant",     "ary",    "at",
             "ate",     "ated",   "ater",    "atery",  "ates",
             "atic",    "ed",     "en",      "ence",   "enced",
             "encer",   "ences",  "end",     "ent",    "er",
             "ery",     "es",     "ess",     "est",    "ful",
             "ial",     "ible",   "ibler",   "ic",     "ical",
             "ice",     "iced",   "icer",    "ices",   "ics",
             "ide",     "ided",   "ider",    "ides",   "ier",
             "ily",     "ing",    "ion",     "ious",   "ise",
             "ised",    "ises",   "ish",     "ism",    "ist",
             "ity",     "ive",    "ived",    "ives",   "ize",
             "ized",    "izer",   "izes",    "less",   "ly",
             "ment",    "ncy",    "ness",    "nt",     "ory",
             "ous",     "re",     "red",     "res",    "ry",
             "s",       "ship",   "sion",    "th",     "tic",
             "tion",    "ty",     "ual",     "ul",     "ward"
         ] 

    This setting can only be set using ``sql.set()``.

    See also `suffixProc`_.

listSuffix
""""""""""
    If not set to ``false``, the return object of ``sql.set()`` will include
    the property ``suffixList``, which will be set to an array containing the
    current suffix list. 

    This setting can only be used via ``sql.set()``.

suffixEquivsList
""""""""""""""""
    The suffix list used for suffix processing during
    equivalence lookup. The default suffixeq list is:

    ::

         [ "'",  "ies",  "s" ]

    This setting can only be set using ``sql.set()``.

listSuffixEquivs
""""""""""""""""
    If not set to ``false``, the return object of ``sql.set()`` will include
    the property ``suffixListEquivs``, which will be set to an array containing the
    current suffix list. 

    This setting can only be used via ``sql.set()``.

prefixList
""""""""""
    The prefix list used for prefix processing (if enabled) during
    search. An array of strings. The default prefix list is:

    ::

         [
             "ante",      "anti",          "arch",           "auto",
             "be",        "bi",            "counter",        "de",
             "dis",       "em",            "en",             "ex",
             "extra",     "fore",          "hyper",          "in",
             "inter",     "mis",           "non",            "post",
             "pre",       "pro",           "re",             "semi",
             "sub",       "super",         "ultra",          "un"
         ] 

    This setting can only be set using ``sql.set()``.

    See also `prefixProc`_.

listPrefix
""""""""""
    If not set to ``false``, the return object of ``sql.set()`` will include
    the property ``prefixList``, which will be set to an array containing the
    current prefix list. 

    This setting can only be used via ``sql.set()``.

uEqPrefix
"""""""""
    AKA ``userEquivsFile`` when set from ``sql.set()``.  
    The name of the user equivalence file. Default is empty.

withinProc
""""""""""
   Whether to process the w/ operator in queries.  The default is ``true``.


Rank knobs
~~~~~~~~~~

The following properties affect the document ranks from ``likep`` and
``like`` queries, and hence the order of returned documents for
``likep``. Each property controls a factor used in the rank. The
property’s value is the relative importance of that factor in computing
the rank. The properties are settable from 0 (factor has no effect at
all) to 1000 (factor has maximum relative importance).

It is important to note that these property weights are relative to the
sum of all weights. For example, if ``likepleadbias`` is set to 1000 and
the remaining properties to 0, then a hit’s rank will be based solely on
lead bias. If ``likepproximity`` is then set to 1000 as well, then lead
bias and proximity each determine 50% of the rank.


likepProximity
""""""""""""""
    Controls how important proximity of terms is. The closer the hit’s
    terms are grouped together, the better the rank. The default weight
    is ``500``.


likepLeadBias
"""""""""""""
    Controls how important closeness to document start is. Hits closer
    to the top of the document are considered better. The default weight
    is ``500``.


likepOrder
""""""""""
    Controls how important word order is: hits with terms in the same
    order as the query are considered better. For example, if searching
    for “bear arms”, then the hit “arm bears”, while matching both
    terms, is probably not as good as an in-order match. The default
    weight is ``500``.


likepDocFreq
""""""""""""
    Controls how important frequency in document is. The more
    occurrences of a term in a document, the better its rank, up to a
    point. The default weight is ``500``.


likepTblFreq
""""""""""""
    Controls how important frequency in the table is. The more a term
    occurs in the table being searched, the *worse* its rank. Terms that
    occur in many documents are usually less relevant than rare terms.
    For example, in a web-walk database the word “``HTML``” is likely to
    occur in most documents: it thus has little use in finding a
    specific document. The default weight is ``500``.


Other Ranking Properties
~~~~~~~~~~~~~~~~~~~~~~~~

These properties affect how ``LIKEP`` and some ``LIKE`` queries are
processed.


likepRows
"""""""""
    Only the top ``likeprows`` relevant documents are returned by a
    ``LIKEP`` query (default ``100``). This is an arbitrary cut-off beyond
    which most results would be increasingly useless. It also speeds up
    the query process, because fewer rows need to be sorted during
    ranking. By altering ``likeprows`` this threshold can be changed,
    e.g. to return more results to the user (at the potential cost of
    more search time). Setting this to ``0`` will return all relevant
    documents (no limit).

    Note that in some circumstances, a ``LIKEP`` query might return more
    than ``likepRows`` results, if for example later processing requires
    examination of all ``LIKEP``-matching rows (e.g. certain ``AND``
    queries). Thus a SQL statement containing ``LIKEP`` may or may not
    be limited to ``likepRows`` results, depending on other clauses,
    indexes, etc.


likepMode
"""""""""
    Sets the mode for ``LIKEP`` queries.  This can be either ``0``, for
    early, or ``1`` for late.  The default is ``1``, which is the correct
    setting for almost all cases.  Does not apply to most Metamorph index
    searches.


likepAllMatch
"""""""""""""
    Setting this to 1 forces ``LIKEP`` to only consider those documents
    containing *all* (non-negated) query terms as matches (i.e. just as
    ``LIKE`` does). By default, since ``LIKEP`` is a ranking operator it
    returns the best results even if only some of the set-logic terms
    (non-``+`` or ``-`` prefix) can be found. (Note that required terms
    – prefixed with a ``+`` – are always required in a hit regardless of
    this setting. Also note that if ``likepObeyIntersects`` is ``true``, an @
    operator value in the query will override this setting.)


likepObeyIntersects
"""""""""""""""""""
    Setting this to 1 forces ``LIKEP`` to obey the intersects operator
    (@) in queries (even when likepallmatch is true). By default
    ``LIKEP`` does not use it, because it is a ranking operator. Setting
    both ``likepAllMatch`` and ``likepObeyIntersects`` to 1 will make
    ``LIKEP`` respect queries the same as ``LIKE``. (Note:
    `alIntersects`_ may have to be enabled as well.)


likepInfThresh
""""""""""""""
    This controls the “infinity” threshold in ``LIKE`` and ``LIKEP``
    queries: if the estimated number of matching rows for a set is
    greater than this, the set is considered infinitely-occurring. If
    all the search terms found in a given document are such infinite
    sets, the document is given an estimated rank. This saves time
    ranking irrelevant but often-occurring matches, at the possible
    expense of rank position. The default is ``0``, which means infinite (no
    infinite sets; rank all documents).


likepIndexThresh
""""""""""""""""
    Controls the maximum number of matching documents to examine
    (default infinite) for ``LIKEP`` and ``LIKE``. After this many
    matches have been found, stop and return the results obtained so
    far, even if more hits exist. Typically this would be set to a high
    threshold (e.g. 100000): a query that returns more than that many
    hits is probably not specific enough to produce useful results, so
    save time and don’t process the remaining hits. (It’s also a good
    bet that something useful was already found in the initial results.)
    This helps keep such noisy queries from loading a server, by
    stopping processing on them early. A more specific query that
    returns fewer hits will fall under this threshold, so all matches
    will be considered for ranking.

    Note that setting ``likepIndexThresh`` is a tradeoff between speed
    and accuracy: the lower the setting, the faster queries can be
    processed, but the more queries may be dropping potentially
    high-ranking hits.


Indexing properties
~~~~~~~~~~~~~~~~~~~


indexSpace
""""""""""
    A directory in which to store the index files. The default
    is the empty string, which means use the database directory. This can be
    used to put the indexes onto another disk to balance load or for space
    reasons. If ``indexspace`` is set to a non-default value when a
    Metamorph index is being updated, the new index will be stored in the
    new location.

indexBlock
""""""""""
    When a Metamorph index is created on an indirect field, the indirect
    files are read in blocks. This property allows the size of the block
    used to be redefined.


indexMem
""""""""
    When indexes are created Texis will use memory to speed up
    the process. This setting allows the amount of memory used to be
    adjusted. The default is to use 40% of physical memory, if it can be
    determined, and to use 16MB if not. If the value set is less than 100
    then it is treated as a percentage of physical memory. It the number is
    greater than 100 then it is treated as the number of bytes of memory to
    use. Setting this value too high can cause excessive swapping, while
    setting it too low causes unneeded extra merges to disk.

indexMeter
"""""""""" 
    Whether to print a progress meter during index
    creation/update. The default is 0 or ``'none'``, which suppresses the
    meter. A value of ``1`` or ``'simple'`` prints a simple hash-mark meter
    (with no tty control codes; suitable for redirection to a file and
    reading by other processes). A value of ``2`` or ``'percent'`` or ``'pct'``
    prints a hash-mark meter with a more detailed percentage value (suitable
    for large indexes).

meter
"""""
    A semicolon-separated list of processes to print a progress meter for.
    Syntax:

         {:math:`process`\ [= :math:`type`]}\|\ :math:`type` [; ...]

    A :math:`process` is one of ``index``, ``compact``, or the catch-all
    alias ``all``. A :math:`type` is a progress meter type, one of ``none``,
    ``simple``, ``percent``, ``on`` (same as ``simple``) or ``off`` (same as
    ``none``). The default :math:`type` if not given is ``on``. E.g. to show
    a progress meter for all meterable processes, simply set ``meter`` to
    ``on``.

addExp
""""""

    AKA ``addExpressions`` in ``sql.set()``.  A single additional, or an
    array of additional REX expression to match words to be indexed in a
    Metamorph index.  This is useful if there are non-English words to be
    searched for, such as part numbers.  When an index is first created, the
    expressions used are stored with it so they will be updated properly. 
    The default expression is ``\alnum{2,99}``.  **Note:** Only the
    expressions set when the index is initially created (i.e.  the first
    CREATE METAMORPH ...  statement – later statements are index updates)
    are saved.  Expressions set during an update (issuance of “create
    metamorph [inverted] index” or “create fulltext index” on an existent
    index) will *not* be added.


delExp
""""""

    AKA ``deleteExpressions`` in ``sql.set()``.  A single value or an array
    of values.  This removes an index word expression from the list. 
    Expressions can be removed either by number (starting with 0) or by
    expression.  *Note* avoid using numbers in an array as the index
    numbering changes with each delete.


lstExp
""""""

    AKA ``listExpressions`` in ``sql.set()``.  If not set ``false``, the
    return object of ``sql.set()`` will include the property
    ``expressionsList`` which will be set to an array with the current list
    of word expressions.

    Example:

    .. code-block:: javascript

       /* delete the default "\alnum{2,99}" expression,
          add two expressions and list.                  */

       var lists = sql.set({
          deleteExpressions: 0,              // delete the default at pos 0
          addExpressions: [ 
             "[\\alnum\\x80-\\xff]+",        // letters and numbers
             "[\\alnum\\$\\%\\@\\-\\_\\+]+"  // letters, numbers and additional chars
          ],
          listExpressions: true
       });
       
       console.log(JSON.stringify(lists,null,3));

       /* expected output
       {
          "expressionsList": [
             "\\alnum\\x80-\\xff]+",
             "[\\alnum\\$\\%\\@\\-\\_\\+]+"
          ]
       }
       */



addIndexTmp
"""""""""""

    AKA ``addIndexTemp`` in ``sql.set()``.  A string or array of strings. 
    Add a directory or directories to the list of directories to use for
    temporary files while creating the index.  If temporary files are needed
    while creating a Metamorph index they will be created in one of these
    directories, the one with the most space at the time of creation.  If no
    ``addIndexTmp`` dirs are specified, the default list is the index’s
    destination dir (e.g.  database or ``indexSpace``), and the environment
    variables ``TMP`` and ``TMPDIR``.


delIndexTmp
"""""""""""

    AKA ``deleteIndexTemp`` in ``sql.set()``.  A single value or an array of
    values.  Remove a directory from the list of directories to use for
    temporary files while creating a Metamorph index.  Expressions can be
    removed either by number (starting with 0) or by expression.  *Note*
    avoid using numbers in an array as the index numbering changes with each
    delete.



lstIndexTmp
"""""""""""
    AKA ``listIndexTemp`` in ``sql.set()``.  If not set ``false``, the
    return object of ``sql.set()`` will include the property
    ``indexTempList`` which will be set to an array with the current list
    of temporary directories.

    Example:

    .. code-block:: javascript

       sql.set({
          addIndexTemp: ["/tmp","/var/tmp","/usr/tmp"]
       });

       /* do some stuff here */

       var lists = sql.set({
          deleteIndexTemp: 1,
          listIndexTemp: true
       });

       console.log(JSON.stringify(lists,null,3));

       /* expected output:
       {  
          "indexTempList": [
             "/tmp",
             "/usr/tmp"
          ]
       }
       */


indexValues
"""""""""""
    Controls how a regular (B-tree) index stores table values.
    If set to ``splitStrlst`` (the default), then ``strlst``-type fields are
    split, i.e. a separate (item,recid) tuple is stored for *each*
    (``varchar``) item in the ``strlst``, rather than just one for the whole
    (strlst,recid) tuple. This allows the index to be used for some set-like
    operators that look at individual items in a ``strlst``, such as most
    ``IN``, ``SUBSET`` and ``INTERSECT`` queries.

    If ``indexValues`` is set to ``all`` – or the index is not on a
    ``strlst`` field, or is on multiple fields – such splitting does not
    occur, and the index can generally not be used for set-like queries
    (with some exceptions; see :ref:`sql1:Searches Using SUBSET`  for details).

    Note that if index values are split (i.e. ``splitStrlst`` set and index
    is one field which is ``strlst``), table rows with an empty (zero-items)
    ``strlst`` value will not be stored in the index. This means that
    queries that require searching for or listing empty-\ ``strlst`` table
    values cannot use such an index. For example, a subset query with a
    non-empty parameter on the right side and a ``strlst`` table column on
    the left side will not be able to return empty-\ ``strlst`` rows when
    using an index, even though they match. Also, subset queries with an
    empty-\ ``strlst`` or empty-\ ``varchar`` parameter (left or right side)
    must use an ``indexValues=all`` index instead. Thus if
    empty-\ ``strlst`` subset query parameters are a possibility, both types
    of index (``splitStrlst`` and ``all``) should be created.

    As with ``stringCompareMode``, only the creation-time ``indexValues``
    value is ever used by an index, not the current value, and the optimizer
    will attempt to choose the best index at search time.


btreeThreshold
""""""""""""""
    This sets a limit as to how much of an index should be used. If a
    particular portion of the query matches more than the given percent of
    the rows the index will not be used. It is often more efficient to try
    and find another index rather than use an index for a very frequent
    term. The default is set to ``50``, so if more than half the records match,
    the index will not be used. This only applies to ordinary indices.

.. need to test in rampart first
   btreeLog
   """"""""
    Whether to log operations on a particular B-tree, for debugging.
    Generally enabled only at the request of tech support. The value syntax
    is:

        :math:`[`\ ``on=``\ :math:`|`\ ``off=``\ :math:`][`\ ``/dir/``\ :math:`]`\ ``file``\ :math:`[`\ ``.btr``\ :math:`]`

    Prefixing ``on=`` or ``off=`` turns logging on or off, respectively; the
    default (if no prefix) is on. Logging applies to the named B-tree file;
    if a relative path is given, logging applies to the named B-tree in any
    database accessed.

    The logging status is also saved in the B-tree file itself, if the index
    is opened for writing (e.g. at create or update). This means that once
    logging is enabled and saved, *every* process that accesses the B-tree
    will log operations, not just ones that have ``btreelog`` explicitly
    set. This is critical for debugging, as every operation must be logged.
    Thus, ``btreelog`` can just be set once (e.g. at index create), without
    having to modify (and track down) every script that might use the
    B-tree. Logging can be disabled later, by setting “``off=file``” and
    accessing the index for an update.

    Operations are logged to a text file with the same name as the B-tree,
    but ending in “``.log``” instead of “``.btr``”. The columns in the log
    file are as follows; most are for tech support analysis, and note that
    they may change in a future Texis release:

    -  **Date** Date

    -  **Time** Time (including microseconds)

    -  **Script and line** Vortex script and line number, if known

    -  **PID** Process ID

    -  **DBTBL handle** ``DBTBL`` handle

    -  **Read locks** Number of read locks (``DBTBL.nireadl``)

    -  **Write locks** Number of write locks (``DBTBL.niwrite``)

    -  **B-tree handle** ``BTREE`` handle

    -  **Action** What action was taken:

       -  ``open`` B-tree open: **Recid** is root page offset

       -  ``create`` B-tree create

       -  ``close`` B-tree close

       -  ``RDroot`` Read root page

       -  ``dump`` B-tree dump

       -  ``WRhdr`` Write B-tree header: **Recid** is root page offset

       -  ``WRdd`` Write data dictionary: **Recid** is ``DD`` offset. (Read
          ``DD`` at open is not logged.)

       -  ``delete`` Delete key: **Recid** is for the key

       -  ``append`` Append key

       -  ``insert`` Insert key

       -  ``search`` Search for key

       -  ``RDpage`` Read page: **Recid** is for the page

       -  ``WRpage`` Write page

       -  ``CRpage`` Create page

       -  ``FRpage`` Free page

       -  ``FRdbf`` Free DBF block

    -  **Result** Result of action:

       -  ``ok`` Success

       -  ``fail`` Failure

       -  ``dup`` Duplicate (e.g. duplicate insert into unique B-tree)

       -  ``hit`` Search found the key

       -  ``miss`` Search did not find the key

    -  **Search mode** Search mode:

       -  ``B`` Find before

       -  ``F`` Find

       -  ``A`` Find after

    -  **Index guarantee** ``DBTBL.indguar`` flag (``1`` if no post-process
       needed)

    -  **Index type** Index type:

       -  ``N`` ``DBIDX_NATIVE`` (bubble-up)

       -  ``M`` ``DBIDX_MEMORY`` (RAM B-tree)

       -  ``C`` ``DBIDX_CACHE`` (RAM cache)

    -  **Recid** Record id; see notes for **Action** column

    -  **Key size** Key size (in bytes)

    -  **Key flags** Flags for each key value, separated by commas:

       -  ``D`` ``OF_DESCENDING``

       -  ``I`` ``OF_IGN_CASE``

       -  ``X`` ``OF_DONT_CARE``

       -  ``E`` ``OF_PREFER_END``

       -  ``S`` ``OF_PREFER_START``

    -  **Key** Key, i.e. value being inserted, deleted etc.; multiple values
       separated with commas

    Unavailable or not-applicable fields are logged with a dash. Note that
    enabling logging can produce a large log file quickly; free disk space
    should be monitored. The ``btreelog`` setting was added in version
    5.01.1134028000 20051208.


   btreedump
   """""""""
    Dump B-tree indexes, for debugging. Generally enabled only at the
    request of tech support. The value is an integer whose bits are defined
    as follows:

    Bits 0-15 define what to dump. Files are created that are named after
    the B-tree, with a different extension:

    -  0: Issue a ``putmsg`` about where dump file(s) are

    -  1: ``.btree`` file: Copy of in-mem ``BTREE`` struct

    -  2: ``.btrcopy`` file: Copy of ``.btr`` file

    -  3: ``.cache`` file: Page cache from ``BCACHE``, ``BPAGE``

    -  4: ``.his`` file: History from ``BTRL``

    -  5: ``.core`` file: ``fork()`` and dump core

        Bits 16+ define when to dump:

    -  16: At “Cannot insert value” messages

    -  17: At “Cannot delete value” messages

    -  18: At “Trying to insert duplicate value” messages

    The files are for tech support analysis. Formats and bits subject to
    change in future Texis releases. The ``btreedump`` setting was added in
    version 5.01.1131587000 20051109.


maxLinearRows
"""""""""""""
    This set the maximum number of records that should be searched linearly.
    If using the indices to date yield a result set larger than
    ``maxLinearRows`` then the program will try to find more indices to use.
    Once the result set is smaller than ``maxLinearRows``, or all possible
    indices are exhausted, the records will be processed. The default is
    ``1000``.


likerRows
"""""""""
    How many rows a single term can appear in, and still be returned by
    ``liker``. When searching for multiple terms with ``liker`` and
    ``likep`` one does not always want documents only containing a very
    frequent term to be displayed. This sets the limit of what is considered
    frequent. The default is ``1000``.


indexAccess
"""""""""""
    If this option is turned on then data from an index can be selected as
    if it were a table. When selecting from an ordinary (B-tree) index, the
    fields that the index was created on will be listed. When selecting from
    a Metamorph index a list of words (``Word`` column‘), count of rows
    containing each word (``RowCount``), and – for Metamorph inverted
    indexes – count of all hits in all rows (``OccurrenceCount``) for each
    word will be returned.

    This may be useful for applications such as an AJAX type-ahead suggestion.

    Example:

    .. code-block:: javascript

       var Sql=require("rampart-sql");

       var db=process.scriptPath + '/path/to/my/wikidb';

       var sql=new Sql.init(db);

       /* allow access to index as a table */
       sql.set({
           indexAccess: true
       });

       /* a sample typeahead request */
       var typeahead="qu"

       /* find the 10 most used terms that start with 'qu' 
          in the metamorph inverted index (i.e. fulltext index) 
          "wikitext_Doc_mmix"                              */
       var res=sql.exec(
         "select Word from wikitext_Doc_mmix where Word matches ? order by RowCount DESC",
         [typeahead+'%'],
         {returnType: "array"}
       );

       /* flatten to a single array */
       res=[].concat.apply([], res.rows);

       /* sample return to application */
       console.log(JSON.stringify({words:res},null,3));

       /* expected output:
       {
          "words": [
             "quickly",
             "queen",
             "quality",
             "quite",
             "quarter",
             "question",
             "qualified",
             "questions",
             "qualifying",
             "quebec"
          ]
       }
       */


.. exclude for now
   dbcleanupverbose
   """"""""""""""""

   *FIXME:ASK THUNDERSTONE ABOUT THIS -ajf*
    Integer whose bit flags control some tracing messages about database
    cleanup housekeeping (e.g. removal of unneeded temporary or deleted
    indexes and tables). A bit-wise OR of the following values:

    -  ``0x01``: Report successful removal of temporary/deleted
       indexes/tables.

    -  ``0x02``: Report failed removal of such indexes/tables.

    -  ``0x04``: Report on in-use checks of temporary indexes/tables.

    The default is 0 (i.e. no messages). Note that these cleanup actions may
    also be handled by the Database Monitor; see also the DB Cleanup Verbose
    setting in conf/texis.ini. Added in version 6.00.1339712000 20120614.


   indextrace
   """"""""""
    For debugging: trace index usage, especially during searches, issuing
    informational ``putmsg``\ s. Greater values produce more messages. Note
    that the meaning of values, as well as the messages printed, are subject
    to change without notice. Aka ``traceindex``, ``traceidx``. Added in
    version 3.00.942186316 19991109.


   tracerecid
   """"""""""
    For debugging: trace index usage for this particular recid. Added in
    version 3.01.945660772 19991219.


   indexdump
   """""""""
    For debugging: dump index recids during search/usage. Value is a bitwise
    OR of the following flags:

    Bit 0
        for new list

    Bit 1
        for delete list

    Bit 2
        for token file

    Bit 3
        for overall counts too

    The default is 0.


indexMmap
"""""""""
    Whether to use memory-mapping to access Metamorph index files, instead
    of ``read()``. The value is a bitwise OR of the following flags:

    Bit 0
        for token file

    Bit 1
        for ``.dat`` file

    The default is 1 (i.e. for token file only). Note that memory-mapping
    may not be supported on all platforms.


indexReadBufSz
""""""""""""""
    Read buffer size, when reading (not memory-mapping) Metamorh index
    ``.tok`` and ``.dat`` files. The default is 64KB; suffixes like “``KB``”
    are respected. During search, actual read block size could be less (if
    predicted) or more (if blocks merged). Also used during index
    create/update. Decreasing this size when creating large indexes can save
    memory (due to the large number of intermediate files), at the potential
    expense of time. AKA ``indexReadBufSize``.

indexWriteBufSz
"""""""""""""""
    Write buffer size for creating Metamorph indexes. The default is 128KB;
    suffixes like “``KB``” are respected. Aka ``indexWriteBufSize``.

indexMmapBufSz
""""""""""""""
    Memory-map buffer size for Metamorph indexes. During search, it is used
    for the ``.dat`` file, if it is memory-mapped (see ``indexmmap``); it is
    ignored for the ``.tok`` file since the latter is heavily used and thus
    fully mapped (if ``indexMmap`` permits it). During index update,
    ``indexMmapBufSz`` is used for the ``.dat`` file, if it is
    memory-mapped; the ``.tok`` file will be entirely memory-mapped if it is
    smaller than this size, else it is read. AKA ``indexMmapBufSize``. The
    default is 0, which uses 25% of RAM. “``KB``” etc. suffixes are allowed.


indexSlurp
""""""""""

    Whether to enable index “slurp” optimization during Metamorph index
    create/update, where possible.  Optimization is always possible for
    index create; during index update, it is possible if the new
    insert/update recids all occur after the original recids (e.g.  the
    table is insert-only, or all updates created a new block).  Optimization
    saves about 20% of index create/update time by merging piles an entire
    word at a time, instead of word/token at a time.  The default is
    ``true`` (enabled); set to 0 to disable.


indexAppend
"""""""""""

    Whether to enable index “append” optimization during Metamorph index
    update, where possible.  Optimization is possible if the new insert
    recids all occur after the original recids, and there were no
    deletes/updates (e.g.  the table is insert-only); it is irrelevant
    during index create.  Optimization saves index build time by avoiding
    original token translation if not needed.  The default is ``true``
    (enabled); set to ``false`` to disable.


indexWriteSplit
"""""""""""""""

    Whether to enable index “write-split” optimization during Metamorph
    index create/update.  Optimization saves memory by splitting the writes
    for (potentially large) ``.dat`` blocks into multiple calls, thus
    needing less buffer space.  The default is ``true`` (enabled); set to
    ``false`` to disable.


indexBtreeExclusive
"""""""""""""""""""

    Whether to optimize access to certain index B-trees during exclusive
    access.  The optimization may reduce seeks and reads, which may lead to
    increased index creation speed on platforms with slow large-file
    ``lseek`` behavior.  The default is ``true`` (enabled); set to ``false``
    to disable.


mergeFlush
""""""""""

    Whether to enable index “merge-flush” optimization during Metamorph
    index create/update.  Optimization saves time by flushing in-memory
    index piles to disk just before final merge; generally saves time where
    ``indexslurp`` is not possible.  The default is ``true`` (enabled); set
    to ``false`` to disable.


indexVersion 
""""""""""""
    Which version of Metamorph index to produce or update, when
    creating or updating Metamorph indexes. The supported values are 0
    through 3; the default is 2. Setting version 0 sets the default index
    version for that Texis release. Note that old versions of Texis may not
    support version 3 indexes. Version 3 indexes may use less disk space
    than version 2, but are considered experimental.


indexMaxSingle
""""""""""""""
    For Metamorph indexes; the maximum number of locations
    that a single-recid dictionary word may have and still be stored solely
    in the ``.btr`` B-tree file (without needing a ``.dat`` entry).
    Single-recid-occurence words usually have their data stored solely in
    the B-tree to save a ``.dat`` access at search time. However, if the
    word occurs many times in that single recid, the data (for a Metamorph
    inverted index) may be large enough to bloat the B-tree and thus negate
    the savings, so if the single-recid word occurs more than
    ``indexMaxSingle`` times, it is stored in the ``.dat``. The default is
    ``8``.

.. skip this
  uniqnewlist
  """""""""""
    Whether/how to unique the new list during Metamorph index searches.
    Works around a potential bug in old versions of Texis; not generally
    set. The possible values are:

    0
        : do not unique at all

    1
        : unique auxillary/compound index new list only

    2
        : unique all new lists

    3
        : unique all new lists and report first few duplicates

    The default is 0.


tableReadBufSz
""""""""""""""
    Size of read buffer for tables, used when it is possible to buffer table
    reads (e.g. during some index creations). The default is 16KB. When
    setting, suffixes such as “``KB``” etc. are supported. Set to ``0`` to
    disable read buffering. Aka ``tableReadBufSize``.

Miscellaneous Properties
~~~~~~~~~~~~~~~~~~~~~~~~
These properties do not fit nicely into a group, and are presented here.


tableSpace
""""""""""
    Similar to `indexSpace`_ above. Sets a directory into which tables
    created will be placed. This property does not stay set across
    invocations. Default is empty string, which means the database
    directory.


dateFmt
"""""""
    This is a ``strftime`` format used to format dates for conversion to
    character format. This will affect ``tsql``, as well as attempts to
    retrieve dates in ASCII format. Although the features supported by
    different operating systems will vary, some of the more common
    format codes are:

    ``%%`` -  Output ``%``

    ``%a`` -  abbreviated weekday name

    ``%A`` -  full weekday name

    ``%b`` -  abbreviated month name

    ``%B`` -  full month name

    ``%c`` -  local date and time representation

    ``%d`` -  day of month (01 - 31)

    ``%D`` -  date as ``%m/%d/%y``

    ``%e`` -  day of month ( 1 - 31)

    ``%H`` -  Hour (00 - 23)

    ``%I`` -  Hour (01 - 12)

    ``%j`` -  day of year (001 - 366)

    ``%m`` -  month (01 - 12)

    ``%M`` -  Minute (00 - 59)

    ``%p`` -  AM/PM

    ``%s`` -  Seconds (00 - 59)

    ``%U`` -  Week number (beginning Sunday) (00-53)

    ``%w`` -  Week day (0-6) (0 is Sunday)

    ``%W`` -  Week number (beginning Monday) (00-53)

    ``%x`` -  local date representation

    ``%X`` -  local time representation

    ``%y`` -  two digit year (00 - 99)

    ``%Y`` -  Year with century

    ``%z`` -  Time zone name

    Default ``%Y-%m-%d %H:%M:%S``, which can be restored by setting
    ``dateFmt`` to an empty string.


timeZone
""""""""
    Change the default timezone that Texis will use. This should be
    formatted as for the TZ environment variable. For example for US
    Eastern time you should set timezone to ``EST5EDT``. Some systems
    may allow alternate representations, such as ``US/Eastern``, and if
    your operating system accepts them, so will Texis.


locale
""""""
    Can be used to change the locale that Texis uses.  This will impact the
    display of dates if using names, as well as the meaning of the character
    classes in REX expressions, so ``\alpha`` will be correct.  Also with
    the correct locale set (and OS support), Metamorph will work case
    insensitively correctly (see ``textsearchmode`` for UTF-8/Unicode).

.. skip

   indirectCompat
   """"""""""""""
    Setting this to 1 sets compatibility with early versions of Texis as
    far as display of indirects go. If set to 1 a trailing ``@`` is
    added to the end of the filename. Default 0.


indirectSpace
"""""""""""""
    Controls where indirects are created. The default location is a
    directory called indirects in the database directory. Texis will
    automatically create a directory structure under that directory to
    allow for efficient indirect access. At the top level there will be
    16 directories, 0 through 9 and a through f. When you create the
    directory for indirects you can precreate these directories, or use
    them as mount points. You should make sure that the current user has
    permissions to the directories.


triggerMode
"""""""""""

    This setting changes the way that the command is treated when creating a
    trigger.  The default behavior is that the command will be executed with
    an extra argument, which is the filename of the table containing the
    records.  If ``triggermode`` is set to 1 then the strings ``$db`` and
    ``$table`` are replaced by the database and table in that database
    containing the records.  This allows any program which can access the
    database to retrieve the values in the table without custom coding.


paramChk
""""""""
    Enables or disables the checking of parameters in the SQL statement.
    By default it is enabled, which will cause any unset parameters to
    cause an error. If paramchk is set to ``false`` then unset parameters will
    not cause an error, and will be ignored. This lets a single complex
    query be given, yet parameter values need only be supplied for those
    clauses that should take effect on the query.

    Example:

    .. code-block:: javascript

         var Sql = require("rampart-sql");

         var sql = new Sql.init("./mytestdb");

         sql.exec("create table kvs (Keys varchar(8), Vals varchar(8));");

         var data = [
             {key: "key1", val: "val1"},
             {key: "key2", val: "val2"},
             {key: "key3", val: "val1"}
         ];

         for (var i=0; i<data.length; i++)
             sql.exec("insert into kvs values (?key, ?val);", data[i]);

         var selectors = [
             {key: "key2", val: "val2"},
             {val: "val1"}
         ];

         try {
             for (i=0; i<selectors.length; i++)
             {
                 var res = sql.exec("select * from kvs where Keys=?key and Vals=?val", selectors[i]);
                 console.log(res.rows);
             }
         } catch(e) {
             console.log(e);
         }
         /* expected output:
            [{Keys:"key2",Vals:"val2"}]
            Error: sql exec error: 000 SQLExecute() failed with 99: Needed parameters not supplied in the function: texis_execute
         */


         sql.set({"paramchk":false}); //ignore the absence of "key" in given parameters

         for (i=0; i<selectors.length; i++)
         {
             var res = sql.exec("select * from kvs where Keys=?key and Vals=?val", selectors[i]);
             console.log(res.rows);
         }

         /* expected output:
            [{Keys:"key2",Vals:"val2"}]
            [{Keys:"key1",Vals:"val1"},{Keys:"key3",Vals:"val1"}]
         */



message,nomessage
"""""""""""""""""
    Enable or disable messages from the SQL engine. The argument should
    be a comma separated list of messages that you want to enable or
    disable. The known messages are:

    duplicate
        Message Trying to insert duplicate value () in index when an
        attempt is made to insert a record which has a duplicate value
        and a unique index exists. The default is enabled.

varcharToStrlstMode
"""""""""""""""""""
    AKA ``varcharToStrlstSep``. The separator character or mode to use when
    converting a ``varchar`` string into a ``strlst`` list of strings in
    Texis. In Rampart,the default is set to ``json`` regardless of the 
    ``conf/texis.ini`` setting.  Using ``tsql``, it is set to ``create``, or
    as set in ``conf/texis.ini``.

    *  ``json`` - expect a JSON array of each string.  Example:
       ``sql.exec("update myTable set myStrchrField = ?;",[ [1,2,3] ]);``

    *  ``create`` - indicates that the separator is to be created:
       the entire string is taken intact as the sole item for the resulting
       ``strlst``, [2]_ and a separator is created that is not present in
       the string (to aid re-conversion to ``varchar``).

    *  ``lastchar`` indicates that the last character in the
       source string should be the separator; e.g. “a,b,c,” would be split
       on the comma and result in a ``strlst`` of 3 values: “a”, “b” and
       “c”.

    *  a single character - ``varcharToStrlstMode`` may also be a single byte
       character, in which case that character is used as the separator.  This
       is useful for converting CSV-type strings e.g.  “a,b,c” without having
       to modify the string and append the separator character first (i.e.  for
       lastchar mode).
    
    See also the `metamorphStrLstMode`_ setting, which
    affects conversion of ``strlst`` values into Metamorph queries; and
    the :ref:`sql-server-funcs:convert` SQL function, which
    can take a ``varcharToStrlstMode`` mode argument.

strlstToVarcharMode
"""""""""""""""""""
    The mode for converting a ``strlst`` to a ``varchar`` in Texis. In
    Rampart,the default is set to ``json`` regardless of the 
    ``conf/texis.ini`` setting.  Using ``tsql``, it is set to
    ``delimited``, or as set in ``conf/texis.ini``.

    *  ``json`` - convert to a JSON string.
    *  ``delimited`` - convert to a list of strings delimited by the last
       character.



.. skip

    ``varcharToStrlstSep`` may also be set to ``default`` to restore the
    default (``conf/texis.ini``) setting. It may also be set to
    ``builtindefault`` to restore the “factory” built-in default (which
    changes under ``compatibilityversion``, see above); these values
    were added in version 5.01.1231553000 20090109. If no
    ``conf/texis.ini`` value is set, ``default`` is the same as
    ``builtindefault``.




multiValueToMultiRow
""""""""""""""""""""

    Whether to split multi-value fields (e.g.``strlst``) into multiple rows
    (e.g.  of ``varchar``) when appropriate, i.e.  during GROUP BY or
    DISTINCT on such a field.  If nonzero/true, a GROUP BY or DISTINCT on a
    ``strlst`` field will split the field into its ``varchar`` members for
    processing.  For example, consider the following table:

    ::

            create table test(Colors strlst);
            insert into test(Colors)
              values(convert('red,green,blue,', 'strlst', 'lastchar'));
            insert into test(Colors)
              values(convert('blue,orange,green,', 'strlst', 'lastchar'));
          

    With ``multivaluetomultirow`` set true, the statement:

    ::

            select count(Colors) Count, Colors from test group by Colors;
          

    generates the following output:

    ::

                  Count       Colors
            ------------+------------+
                       2 blue
                       2 green
                       1 orange
                       1 red
          

    Note that the ``strlst`` values have been split, allowing the two
    ``blue`` and ``green`` values to be counted individually. This also
    results in the returned ``Colors`` type being ``varchar`` instead of
    its declared ``strlst``, and the sum of ``Count`` values being
    greater than the number of rows in the table. Note also that merely
    ``SELECT``\ ing a ``strlst`` will not cause it to be split: it must
    be specified in the GROUP BY or DISTINCT clause.

    The ``multivaluetomultirow`` currently only applies to ``strlst`` values
    and only to single-column GROUP BY or DISTINCT clauses.  A system-wide
    default for this SQL setting can be set in conf/texis.ini with the Multi
    Value To Multi Row setting.  If unset, it defaults to ``false``
    (because in general GROUP BY/DISTINCT are expected to return true
    table rows for results).

inMode
""""""
    How the IN operator should behave. If set to
    ``subset``, IN behaves like the 
    :ref:`SUBSET <sql1:Searches Using SUBSET>` operator. If set to
    ``intersect``, IN behaves like the 
    :ref:`INTERSECT <sql1:Searches Using INTERSECT>`
    operator. The default is ``subset``.

hexifyBytes
"""""""""""

    Whether conversion of ``byte`` to ``char`` (or vice-versa) should encode
    to (or decode from) hexadecimal.  Set to ``false`` for off/as-is,
    ``true`` for hexadecimal conversion.  This property is on by default in
    ``tsql`` so that ``SELECT``\ ing from certain system tables that contain
    ``byte`` columns will still be readable from the command line.  However,
    the property is off by default in Rampart to avoid the hassle of hex
    conversion when raw binary data is needed (e.g.  images), and because
    Rampart JavaScript has buffers and functions for dealing with binary
    data, obviating the need for hex conversion.

unalignedBufferWarning
""""""""""""""""""""""

    Whether to issue “Unaligned buffer” warning messages when unaligned
    buffers are encountered in certain situations. Messages are issued
    if this setting is true/nonzero (the default).

unneededRexEscapeWarning
""""""""""""""""""""""""
    Whether to issue “REX: Unneeded escape sequence ...” warnings when a
    REX expression uses certain unneeded escapes. An unneeded escape is
    when a character is escaped that has no special meaning in the
    current context in REX, either alone or escaped. Such escapes are
    interpreted as just the literal character alone (respect-case); e.g
    “``\w``” has no special meaning in REX, and is taken as “``w``”.

    While such escapes have no meaning currently, some may take on a
    specific new meaning in a future Texis release, if REX syntax is
    expanded. Thus using them in an expression now may unexpectedly (and
    silently) result in their behavior changing after a Texis update;
    hence the warning message. Expressions using such escapes should
    thus have them changed to the unescaped literal character.

    If updating the code is not feasible, the warning may be silenced by
    setting ``unneededRexEscapeWarning`` to ``false`` – at the risk of silent
    behavior change at an upgrade.
    Overrides Unneeded REX Escape Warning setting in ``conf/texis.ini`` and
    is set ``false`` regardless in Rampart by default.


nullOutputString
""""""""""""""""

    The string value to output for SQL NULL values. The default is
    “``NULL``”. Note that this is different than the output string for
    zero-integer ``date`` values, which are also shown as “``NULL``”.

validateBtrees
""""""""""""""
    Bit flags for additional consistency checks on B-trees.
    Overrides Validate Btrees setting in ``conf/texis.ini``.

    *  ``0x0001`` - validate tree on open   
    *  ``0x0002`` - validate page on read   
    *  ``0x0004`` - validate page on write   
    *  ``0x0008`` - validate page on release   
    *  ``0x0010`` - other page-release errors   
    *  ``0x0020`` - more stringent limits   
    *  ``0x0040`` - validate on page manipulation   
    *  ``0x1000`` - attempt to fix bad pages if possible   
    *  ``0x2000`` - overwrite freed pages in memory

.. [1]
   The index-data-only optimization allows Texis to not only use the
   index to resolve the WHERE clause, but also the SELECT clause in
   certain circumstances, potentially avoiding a read of the table
   altogether and speeding up results. One of the prerequisites for this
   optimization is that the SELECT clause only refer to columns
   available in the index.

.. [2]
   Note that in create mode, an empty source string will result in an empty
   (zero-items) strlst: this helps maintain consistency of empty-string
   meaning empty-set for strlst, as is true in other contexts.

Query Protection
~~~~~~~~~~~~~~~~ 

The following settings alter the set of query syntax and features that
are allowed. Metamorph has a powerful search syntax, but if improperly or
inadvertently used, it can take a long time to resolve poorly constructed
queries. In a high-load environment such as a Web search engine this can bog
down a server, slowing all users for the sake of one bad search.

Therefore, use of Texis in Rampart is by default highly restrictive of the
queries it will allow, denying some specialized features for the sake of
quicker resolution of all queries.  By altering these settings, script
authors can "open up" Texis and Metamorph to allow more powerful searches,
at the risk of higher load for special searches.

alEquivs 
""""""""
  Boolean, ``false`` by default.  If ``true``, allows equivalences in queries.  If
  ``false``, only the actual terms in a query will be searched for; no
  equivalences will be used.  This is regardless of ``~`` usage or the
  setting of `useEquiv`_.  Note that the equivalence file will still be used to
  check for phrases in the query, however.  Turning this on allows greater
  search flexibility, as equivalent words to a term can be searched for, but
  decreases search speed.

alIntersects
""""""""""""
   Boolean, ``false`` by default. If ``true``, allow use of the ``@``
   (intersections) operator in queries. Queries with few or no intersections
   (e.g. @0) may be slower, as they can generate a copious number of hits.

alLinear 
""""""""
   Boolean, ``false`` by default. If ``true``, an all-linear query-one without
   any indexable "anchor" words-is allowed. A query like "/money #million"
   where all the terms use unindexable pattern matchers (REX, NPM or XPM) is an
   example. Such a query requires that the entire table be linearly searched,
   which can be very slow for a table of significant size.
   If allinear is ``false``, all queries must have at least one term that can be
   resolved with the text index, and a text index must exist on the
   field. Under such circumstances, other unindexable terms in the query can
   generally be resolved quickly, if the "anchor" term limits the linear search
   to a tiny fraction of the table. The error message "Query would require
   linear search" may be generated by linear queries if allinear is off.

   Note that an otherwise indexable query like "rocket" may become linear if
   there is no text index on its field, or if an index for another part of
   the SQL query is favored instead by Texis. For example, with the SQL query
   "select Title from Books where Date > 'May 1998' and Title like 'gardening'"
   Texis may use a Date index rather than a Title text index for speed. In
   such a case it may be necessary to enable linear processing for a
   complicated query to proceed-since part of the table is being linearly
   searched.

alNot
"""""
   Boolean, ``true`` by default.  If ``true``, allows "NOT" logic (e.g.  the
   ``-`` operator) in a query.  

alPostProc 
""""""""""

   Boolean, ``false`` by default If ``true``, post-processing of queries is
   allowed when needed after an index lookup, e.g.  to resolve unindexable
   terms like REX expressions, or like queries with a non-inverted Metamorph
   index.  If ``false``, some queries are faster, but may not be as accurate
   if they aren't completely resolved.  The error message "Query would
   require post-processing" may be generated by such queries if
   ``alPostProc`` is ``false``.

alWild 
""""""
   Boolean, ``true`` by default. If ``true``, wildcards are
   allowed in queries.  Wildcards can slow searches because potentially many
   words must be looked for.

alWithin
""""""""
   Boolean, ``false`` by default.  If ``true``, "within" operators (``w/``)
   are allowed.  These generally require a post-process to resolve, and
   hence can slow searches.  If off, the error message "'delimiters' not
   allowed in query" will be generated if the within operator is used in a
   query.

.. skip
   builtindefaults Restore all settings to builtin Thunderstone factory
   defaults, ignoring any texis.ini [Apicp] changes. Added in Texis version 6.

   defaults Restore all settings to defaults set in the texis.ini) [Apicp]
   section (or builtin defaults for settings not set there).

denyMode
""""""""
   String or Integer, ``warning`` by default. What action to take when a
   disallowed query is attempted:

   *  ``silent`` or ``0`` - Silently remove the offending set or operation.
   *  ``warning`` or ``1`` - Remove the term and warn about it in
      ``sql.errMsg``.
   *  ``error`` or ``2`` - Fail the query.

   A message such as "'delimiters' not allowed in query" may be generated when
   a disallowed query is attempted and ``denyMode`` is not ``silent``.

qMaxSets
""""""""
   Integer, ``100`` by default. The maximum number of sets (terms)
   allowed in a query.

qMaxSetWords
""""""""""""
   Integer, ``500`` by default,  ``unlimited`` by default in ``tsql``.  The
   maximum number of search words allowed per set (term), after equivalence and
   wildcard expansion. Some wildcard searches can potentially match thousands
   of distinct words in an index, many of which may be garbage or typos but
   still have to be looked up, slowing a query. If this limit is exceeded, a
   message such as "Max words per set exceeded at word 'xyz*' in query 'xyz*
   abc'" is generated, and the entire set is considered a noise word and not
   looked up in the index. A value of 0 means unlimited.

   Note the set may only be partially dropped (with the message "Partially
   dropping term 'xyz*' in query 'xyz* abc'") depending on the setting of
   `dropWordMode`_.  If `dropWordMode`_ is ``false`` (the default), the root word,
   valid suffixes, and more-common words are still searched, up to the
   ``qMaxSetWords`` limit if possible; the remaining wildcard matches are
   dropped.  If `dropWordMode`_ is ``true``, the entire set is dropped as if a
   noise word.

   Note that qmaxsetwords is the max number of search words, not the number of
   matching hits after the search. Thus a single but often-occurring word like
   "html" counts as one word in this context. Note: In tsql version 5 and
   earlier the default was unlimited.

qMaxWords
"""""""""

   Integer, ``1100`` by default.  The maximum number of words allowed in the
   entire query, after equivalence and wildcard expansion.  If this limit is
   exceeded, a message such as "Max words per query exceeded at word 'xyz*'
   in query 'xyz* abc'" is generated, and the query cannot be resolved. 
   ``0`` means unlimited.  Like ``qMaxSetWords``, this is distinct search words,
   not hits.  `dropWordMode`_ also applies here.

qMinPrelen
""""""""""

   Integer, ``2`` by default. The minimum allowed length of the prefix
   (non-``*`` part) of a wildcard term. Short prefixes (e.g. "a*") may match many
   words and thus slow the search.

qMinWordLen 
"""""""""""

   Integer, ``2`` by default. The minimum allowed length of a word in
   a query. Note that this is different from `minWordLen`_, the minimum word
   length for prefix/suffix processing to occur.

querySettings 
"""""""""""""
   Container for changing all or a group of
   settings to a certain mode. (Explicit texis.ini [Apicp] settings still
   apply, as with all non-builtin "...defaults" settings). The argument may be
   one of the following:

   *  ``defaults`` - Set Rampart defaults:

      The following are set ``false``: 

      ``prefixProc``, ``keepNoise``, ``keepEqvs``/``useEquivs``, ``alPostProc``,
      ``alLinear``, ``alWithin``, ``alIntersects``, ``alEquivs`` and
      ``alExactphrase``.

      The following are set ``true``:

      ``alwild`` and ``alnot``.

      The following are set as listed:

      *  ``qMinWordLen``, ``qminprelen`` -  2
      *  ``minWordLen`` - 255
      *  ``eqPrefix`` - "builtin"
      *  ``uEqPrefix`` - "eqvsusr"
      *  ``denymode`` - "warning"
      *  ``qMaxSets`` - 100
      *  ``qMaxSetWords`` - 500

   *  ``protectionOff`` - turn off all `Query Protection`_ settings 
      (e.g. all ``al``\ .. setting are set ``true``).

.. this was removed above
   sdexp/edexp are empty

Restoring Defaults
~~~~~~~~~~~~~~~~~~

sql.reset()
"""""""""""

   Reset all settings (including `querySettings`_:"defaults" above) to their original values.

Example:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   var sql = new Sql.init("/path/to/my/db");

   ...

   sql.set({...});  //settings changed in script

   ...

   sql.reset(); //reset all to default
