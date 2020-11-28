Server Properties
=================

There are a number of properties that are settable in the SQL Engine.
They do not need to be changed unless the behavior of the system must be
modified. The properties are set using the following SQL syntax:

::

        SET property = value;

The ``value`` can be one of three types depending on the property:
numeric, boolean or string. A boolean value is either an integer–0 is
false, anything else is true–or one of the following strings: “``on``”,
“``off``”, “``true``”, “``false``”, “``yes``” or “``no``”.

The settings are grouped as follows:

Search and optimization parameters
----------------------------------

These settings affect the way that Texis will process the search. They
include settings which change the meaning of the search, as well as how
the search is performed.

defaultlike
~~~~~~~~~~~
    Defines which sort of search should occur when a ``like`` or
    ``contains`` operator is in the query. The default setting of
    “``like``” behaves in the normal manner. Other settings that can be
    set are “``like3``”, “``likep``”, “``liker``” and “``matches``”. In
    each case the ``like`` operator will act as if the specified
    operator had been used instead.

matchmode
~~~~~~~~~
    Changes the behavior of the ``matches`` clause. The default behavior
    is to use underscore and percent as the single and multi-character
    character wildcards. Setting ``matchmode`` to 1 will change the
    wildcards to question-mark and asterisk.

predopttype
~~~~~~~~~~~
    The Texis engine can reorder the ``where`` clause in an attempt to
    make it faster to evaluate. There are a number of ways this can be
    done; the ``predopttpye`` property controls the way it reorders. The
    values are 0 to not reorder, 1 to evaluate ``and`` first, 2 to
    evaluate ``or`` first. The default is 0.

ignorecase
~~~~~~~~~~
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

textsearchmode
~~~~~~~~~~~~~~
    Sets the APICP ``textsearchmode`` property; see Vortex manual for
    details and important caveats. Added in version 6.

stringcomparemode
~~~~~~~~~~~~~~~~~
    Sets the APICP ``stringcomparemode`` property; see Vortex manual for
    details and important caveats. Added in version 6.

tracemetamorph
~~~~~~~~~~~~~~
    Sets the ``tracemetamorph`` debug property; see Vortex manual for
    details. Added in version 7.00.1375225000 20130730.

tracerowfields
~~~~~~~~~~~~~~
    Sets the ``tracerowfields`` debug property; see Vortex manual for
    details. Added in version 7.02.1406754000 20140730.

tracekdbf
~~~~~~~~~
    Sets the ``tracekdbf`` debug property; see Vortex manual for
    details.

tracekdbffile
~~~~~~~~~~~~~
    Sets the ``tracekdbffile`` debug property; see Vortex manual for
    details.

kdbfiostats
~~~~~~~~~~~
    Sets the ``kdbfiostats`` debug property; see Vortex manual for
    details.

btreecachesize
~~~~~~~~~~~~~~
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

ramrows
~~~~~~~
    When ordering large result sets, the data is initially ordered in
    memory, but if more than ``ramrows`` records are being ordered the
    disk will be used to conserve memory. This does slow down
    performance however. The default is 10000 rows. Setting ``ramrows``
    to 0 will keep the data in memory.

ramlimit
~~~~~~~~
    ``ramlimit`` is an alternative to ``ramrows``. Instead of limiting
    the number of records, the number of bytes of data in memory is
    capped. By default it is 0, which is unlimited. If both ``ramlimit``
    and ``ramrows`` are set then the first limit to be met will trigger
    the use of disk.

bubble
~~~~~~
    Normally Texis will bubble results up from the index to the user.
    That is a matching record will be found in the index, returned to
    the user, then the next record found in the index, and so forth till
    the end of the query. This normally generates the first results as
    quickly as possible. By setting ``bubble`` to 0 the entire set of
    matching record handles will be read from the index first, and then
    each record processed from this list.

optimize,nooptimize
~~~~~~~~~~~~~~~~~~~
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
        table. Added in version 02.06.927235551.

    compoundindex
        Allow the use of compound indexes to resolve searches. For
        example if you create an index on table (field1, field2), and
        then search where field1 = value and field2 = value, it will use
        the index to resolve both portions of this. When disabled it
        would only look for field1 in the index. Added in version
        02.06.929026214.

    countstar
        Use any regular index to determine the number of records in the
        table. If disabled Texis will read each record in the table to
        count them. Added in version 02.06.929026214.

    minimallocking
        Controls whether the table will be locked when doing reads of
        records pointed to by the index used for the query. This is
        enabled by default, which means that read locks will not be
        used. This is the optimal setting for databases which are mostly
        read, with few writes and small records. Added in version 03.00

    groupby
        This setting is enabled by default and will cause the data to be
        read only once to perform a group by operation. The query should
        produce indentical results whether this is enabled or disabled,
        with the performance being the only difference. Added in version
        03.00

    faststats
        When enabled, which is the default, and when the appopriate
        indexes exist Texis will try and resolve aggregate functions
        directly from the index that was used to perform the ``WHERE``
        clause. Added in version 03.00

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
        the benefit. Added in version 03.00

    analyze
        When enabled, which is the default, Texis will analyze the query
        for which fields are needed. This can allow for more efficient
        query processing in most cases. If you are executing a lot of
        different SQL statements that are not helped by the analysis you
        can disable this. Added in version 03.00

    skipahead
        When enabled, which is the default, Texis will skipahead as
        efficiently as possible, typically used with the SKIP parameter
        in Vortex. If disabled Texis will perform full processing on
        each skipped record, and discard the record. Added in version
        03.00

    likewithnots
        When enabled (default), ``LIKE``/``LIKEP``-type searches with
        NOT sets (negated terms) are optimized for speed. Added in
        version 4.02.1041535107 Jan 2 2003.

    shortcuts
        When enabled (default), a fully-indexed ``LIKE``/``LIKEIN``
        clause ``OR``\ ed with another fully-indexed ``LIKE``/``LIKEIN``
        should not cause an unnecessary post-process for the ``LIKE``\ s
        (and entire query). Added in version 4.03.1061229000 20030818 as
        ``optimization18``; in version 7.06.1475000000 20160927, alias
        ``shortcuts`` added.

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

        Added in version 7.06.1475014000 20160927.

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
        the index. Added in version 5.01.1177455498 20070424.

    indexdataonlycheckpredicates
        When enabled (the default), allows the index-data-only
        optimization [1]_ to proceed even if the SELECT columns are
        renamed or altered in expressions. Previously, the columns had
        to be selected as-is with no renaming or expressions. Added in
        version 7.00.1369437000 20130524.

    indexvirtualfields
        When enabled (the default), attempts to reduce memory usage when
        indexing virtual fields (especially with large rows) by freeing
        certain buffers when no longer needed. Currently only applies to
        Metamorph and Metamorph inverted indexes. Added in version
        6.00.1322890000 20111203.

    Example: ``set nooptimize='minimallocking'``

options,nooptions
~~~~~~~~~~~~~~~~~
    Enable or disable certain options. The argument should be a comma
    separated list of options to enable or disable. All options are off
    by default. The available options are:

    triggers
        When on, *disable* the creation of triggers.

    indexcache
        Cache certain Metamorph index search results, so that an
        immediately following Metamorph query with the same ``WHERE``
        clause might be able to re-use the index results without
        re-searching the index. E.g. may speed up a
        ``SELECT field1, field2, ...`` Metamorph query that follows a
        ``SELECT count(*)`` query with the same ``WHERE`` clause.

    ignoremissingfields
        Ignore missing fields during an ``INSERT`` or ``UPDATE``, i.e.
        do not issue a message and fail the query if attempting to
        insert a non-existent field. This may be useful if a SQL
        ``INSERT`` statement is to be used against a table where some
        fields are optional and may not exist.

    Example: ``set options='indexcache'``

ignorenewlist
~~~~~~~~~~~~~
    When processing a Metamorph query you can instruct Texis to ignore
    the unoptimized portion of a Metamorph index by issuing the SQL
    ``set ignorenewlist = 1;``. If you have a continually changing
    dataset, and the index is frequently updated then the default of
    processing the unoptimized portion is probably correct. If the data
    tends to change in large batches, followed by a reoptimization of
    the index then the large batch can cause significant processing
    overhead. In that case it may be wise to enable the
    ``ignorenewlist`` option. If the option is enable then records that
    have been updated in the batch will not be found with Metamorph
    queries until the index has been optimized. Added in version
    02.06.934400000.

indexwithin
~~~~~~~~~~~
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

    The default is 0xf in version 7.06.1525203000 20180501 and later,
    when support for 0x8 was also added. In version 5.01.1153865548
    20060725 up to then, the default was 0x7. The setting was added in
    version 4.04.1075255999 20040127 with a default of 0.

wildoneword
~~~~~~~~~~~
    Whether wildcard expressions in Metamorph queries span a single word
    only, i.e. for multi-substring wildcards. If 0 (false), the query
    “``st*ion``” matches “``stallion``” as well as “stuff an onion”. If
    1 (true), then “``st*ion``” only matches “``stallion``”, and
    linear-dictionary index searches are possible (if enabled), because
    there are no multi-word matches to (erroneously) miss. **Note:**
    prior to version 5.01.1208472000 20080417, this setting did not
    apply to linear searches; linear or post-process searches may have
    experienced different behavior. The default is 1 in version 6 and
    later, 0 in version 5 and earlier. Added in version 4.03.1058230349
    20030714.

wildsufmatch
~~~~~~~~~~~~
    Whether wildcard expressions in Metamorph queries suffix-match their
    trailing substrings to the end of words. If 0 (false), the query
    “``*so``” matches “``also``” as well as “``absolute``”. If 1 (true),
    then “``*so``” only matches “``also``”. Affects what terms are
    matched during linear-dictionary index searches. **Note:** prior to
    version 5.01.1208472000 20080417, this setting did not apply to
    linear searches; linear or post-process searches may have
    experienced different behavior. The default is 1 in version 6 and
    later, 0 in version 5 and earlier. Added in version 4.03.1058230349
    20030714.

wildsingle
~~~~~~~~~~
    An alias for setting ``wildoneword`` and ``wildsufmatch`` together,
    which is usually desired. Added in version 4.03.1058230349 20030714.

allineardict
~~~~~~~~~~~~
    Whether to allow linear-dictionary Metamorph index searches.
    Normally a Metamorph query term is either binary-index searchable
    (fastest), or else must be linear-table searched (slowest). However,
    certain terms, while not binary-index searchable, can be
    linear-dictionary searched in the index, which is slower than
    binary-index, yet faster than linear-table search. Examples include
    leading-prefix wildcards such as “``*tion``”. The default is 0
    (false), since query protection is enabled by default. Note that
    ``wildsingle`` should typically be set true so that wildcard syntax
    is more likely to be linear-dictionary searchable. Added in version
    4.03.1058230349 20030714.

indexminsublen
~~~~~~~~~~~~~~
    The minimum number of characters that a Metamorph index word
    expression must match in a query term, in order for the term to
    utilize the index. A term with fewer than ``indexminsublen``
    indexable characters is assumed to potentially match too many words
    in the index for an index search to be more worthwhile/faster than a
    linear-table search.

    For binary-index searchable terms, ``indexminsublen`` is tested
    against the minimum prefix length; e.g. for query “``test.#@``” the
    length tested is 4 (assuming default index word expression of
    “``\alnum{2,99}``”). For linear-dictionary index searches, the
    length tested is the total of all non-wildcard characters; e.g. for
    query “``ab*cd*ef``” the length tested is 6.

    The default for ``indexminsublen`` is 2. Added in version
    4.03.1058230349 20030714. Note that the query – regardless of index
    or linear search – must also pass the ``qminprelen`` setting.

dropwordmode
~~~~~~~~~~~~
    How to remove words from a query set when too many are present
    (``qmaxsetwords`` or ``qmaxwords`` exceeded) in an index search,
    e.g. for a wildcard term. The possible values are 0 to retain
    suffixes and most common words up to the word limit, or 1 to drop
    the entire term. The default is 0. Added in version 3.00.947633136
    20000111.

metamorphstrlstmode
~~~~~~~~~~~~~~~~~~~
    [‘metamorphstrlstmode’] How to convert a ``strlst`` Metamorph query
    (perhaps generated by Vortex ``arrayconvert``) to a regular string
    Metamorph query. For example, for the ``strlst`` query composed of
    the 3 strings “``one``”, “``two``”, and “``bear arms``”, the various
    modes would convert as follows:

    *  ``allwords``
       Space-separate each string, e.g. “one two bear arms”.

    *  ``anywords``
       Space-separate each string and append “\ ``@0''', e.g. ``\ ‘one
        bear arms @0’’.

    *  ``allphrases``
       Space-separate and double-quote each string, e.g. ““one” “two”
       bear arms””.

    *  ``anywords``
       Space-separate and double-quote each string, and append
       “\ ``@0''', e.g. ``\ ‘“one” “two” “bear arms” @0’’.

    *  ``equivlist``
       Make the string list into a parenthetical comma-separated list,
         e.g. “(one,two,bear arms)”.

    The default is ``equivlist``. Added in version 5.01.1225240000
    20081028. See also the ``varchartostrlstsep`` setting (p. ), which
    affects conversion of ``varchar`` to ``strlst`` in other contexts.

compatibilityversion
~~~~~~~~~~~~~~~~~~~~
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
~~~~~~~~~~~~~~~~~~
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
~~~~~~~~~~
    When set nonzero/true (the default), try to minimize memory usage
    during ``GROUP BY``/``DISTINCT`` operations (e.g. when using an
    index and sorting is not needed). Added in version 7.00.1370039228
    20130531.

legacyversion7orderbyrank
~~~~~~~~~~~~~~~~~~~~~~~~~
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
--------------------

These settings affect the way that text searches are performed. They are
equivalent to changing the corresponding parameter in the profile, or by
calling the Metamorph API function to set them (if there is an
equivalent). They are:

minwordlen
~~~~~~~~~~
    The smallest a word can get due to suffix and prefix removal.
    Removal of trailing vowel or double consonant can make it a letter
    shorter than this. Default 255.

keepnoise
~~~~~~~~~
    Whether noise words should be stripped from the query and index.
    Default off.

suffixproc
~~~~~~~~~~
    Whether suffixes should be stripped from the words to find a match.
    Default on.

prefixproc
~~~~~~~~~~
    Whether prefixes should be stripped from the words to find a match.
    Turning this on is not suggested when using a Metamorph index.
    Default off.

rebuild
~~~~~~~
    Make sure that the word found can be built from the root and
    appropriate suffixes and prefixes. This increases the accuracy of
    the search. Default on.

useequiv
~~~~~~~~
    Perform thesaurus lookup. If this is on then the word and all
    equivalences will be searched for. If it is off then only the query
    word is searched for. Default off. Aka **keepeqvs** in version
    5.01.1171414736 20070213 and later.

inc\_sdexp
~~~~~~~~~~
    Include the start delimiter as part of the hit. This is not
    generally useful in Texis unless hit offset information is being
    retrieved. Default off.

inc\_edexp
~~~~~~~~~~
    Include the end delimiter as part of the hit. This is not generally
    useful in Texis unless hit offset information is being retrieved.
    Default on.

sdexp
~~~~~
    Start delimiter to use: a regular expression to match the start of a
    hit. The default is no delimiter.

edexp
~~~~~
    End delimiter to use: a regular expression to match the start of a
    hit. The default is no delimiter.

intersects
inc\_sdexp
~~~~~~~~~~
    Default number of intersections in Metamorph queries; overridden by
    the ``@`` operator. Added in version 7.06.1530212000 20180628.

hyphenphrase
~~~~~~~~~~~~
    Controls whether a hyphen between words searches for the phrase of
    the two words next to each other, or searches for the hyphen
    literally. The default value of 1 will search for the two words as a
    phrase. Setting it to 0 will search for a single term including the
    hyphen. If you anticipate setting hyphenphrase to 0 then you should
    modify the index word expression to include hyphens.

wordc
~~~~~
    For language or wildcard query terms during linear (non-index)
    searches, this defines which characters in the document consitute a
    word. When a match is found for language/wildcard terms, the hit is
    expanded to include all surrounding word characters, as defined by
    this setting. The resulting expansion must then match the query term
    for the hit to be valid. (This prevents the query “``pond``” from
    inadvertently matching the text “``correspondence``”, for example.)
    The value is specified as a REX character set. The default setting
    is ``[\alpha\']`` which corresponds to all letters and apostrophe.
    For example, to exclude apostrophe and include digits use:
    ``set wordc='[\alnum]'`` Added in version 3.00.942260000. Note that
    this setting is for linear searches: what constitutes a word for
    Metamorph *index* searches is controlled by the index expressions
    (**addexp** property, p. ). Also note that non-language,
    non-wildcard query terms (e.g. ``123`` with default settings) are
    not word-expanded.

langc
~~~~~
    Defines which characters make a query term a language term. A
    language term will have prefix/suffix processing applied (if
    enabled), as well as force the use of **wordc** to qualify the hit
    (during linear searches). Normally **langc** should be set the same
    as **wordc** with the addition of the phrase characters space and
    hyphen. The default is ``[\alpha\' \-]`` Added in version
    3.00.942260000.

withinmode
~~~~~~~~~~
    A space- or comma-separated unit and optional type for the
    “within-\ :math:`N`” operator (e.g. ``w/5``). The unit is one of:

    -  ``char`` for within-\ :math:`N` characters

    -  ``word`` for within-\ :math:`N` words

    The optional type determines what distance the operator measures. It
    is one of the following:

    -  ``radius`` (the default if no type is specified when set)
       indicates all sets must be within a radius :math:`N` of an
       “anchor” set, i.e. there is a set in the match such that all
       other sets are within :math:`N` units right of its right edge or
       :math:`N` units left of its left edge.

    -  ``span`` indicates all sets must be within an :math:`N`-unit span

    Added in version 4.04.1077930936 20040227. The optional type was
    added in version 5.01.1258712000 20091120; previously the only type
    was implicitly ``radius``. In version 5 and earlier the default
    setting was ``char`` (i.e. char radius); in version 6 and later the
    default is word span.

phrasewordproc
~~~~~~~~~~~~~~
    Which words of a phrase to do suffix/wildcard processing on. The
    possible values are ``mono`` to treat the phrase as a monolithic
    word (i.e. only last word processed, but entire phrase counts
    towards **minwordlen**); ``none`` for no suffix/wildcard processing
    on phrases; or ``last`` to process just the last word. Note that a
    phrase is multi-word, i.e. a single word in double-quotes is not
    considered a phrase, and thus **phrasewordproc** does not apply.
    Added in version 4.03.1082000000 20040414. Mode ``none`` supported
    in version 5.01.1127760000 20050926.

mdparmodifyterms
~~~~~~~~~~~~~~~~
    If nonzero, allows the Metamorph query parser to modify search terms
    by compression of whitespace and quoting/unquoting. This is for
    back-compatibility with earlier versions; enabling it will break the
    information from bit 4 of ``mminfo()`` (query offset/lengths of
    sets). Added in version 5.01.1220640000 20080905.

Rank knobs
----------

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

likepproximity
~~~~~~~~~~~~~~
    Controls how important proximity of terms is. The closer the hit’s
    terms are grouped together, the better the rank. The default weight
    is 500.

likepleadbias
~~~~~~~~~~~~~
    Controls how important closeness to document start is. Hits closer
    to the top of the document are considered better. The default weight
    is 500.

likeporder
~~~~~~~~~~
    Controls how important word order is: hits with terms in the same
    order as the query are considered better. For example, if searching
    for “bear arms”, then the hit “arm bears”, while matching both
    terms, is probably not as good as an in-order match. The default
    weight is 500.

likepdocfreq
~~~~~~~~~~~~
    Controls how important frequency in document is. The more
    occurrences of a term in a document, the better its rank, up to a
    point. The default weight is 500.

likeptblfreq
~~~~~~~~~~~~
    Controls how important frequency in the table is. The more a term
    occurs in the table being searched, the *worse* its rank. Terms that
    occur in many documents are usually less relevant than rare terms.
    For example, in a web-walk database the word “``HTML``” is likely to
    occur in most documents: it thus has little use in finding a
    specific document. The default weight is 500.

Other ranking properties
------------------------

These properties affect how ``LIKEP`` and some ``LIKE`` queries are
processed.

likeprows
~~~~~~~~~
    Only the top ``likeprows`` relevant documents are returned by a
    ``LIKEP`` query (default 100). This is an arbitrary cut-off beyond
    which most results would be increasingly useless. It also speeds up
    the query process, because fewer rows need to be sorted during
    ranking. By altering ``likeprows`` this threshold can be changed,
    e.g. to return more results to the user (at the potential cost of
    more search time). Setting this to 0 will return all relevant
    documents (no limit).

    Note that in some circumstances, a ``LIKEP`` query might return more
    than ``likeprows`` results, if for example later processing requires
    examination of all ``LIKEP``-matching rows (e.g. certain ``AND``
    queries). Thus a SQL statement containing ``LIKEP`` may or may not
    be limited to ``likeprows`` results, depending on other clauses,
    indexes, etc.

likepmode
~~~~~~~~~
    Sets the mode for ``LIKEP`` queries. This can be either 0, for
    early, or 1 for late. The default is 1, which is the correct setting
    for almost all cases. Does not apply to most Metamorph index
    searches.

likepallmatch
~~~~~~~~~~~~~
    Setting this to 1 forces ``LIKEP`` to only consider those documents
    containing *all* (non-negated) query terms as matches (i.e. just as
    ``LIKE`` does). By default, since ``LIKEP`` is a ranking operator it
    returns the best results even if only some of the set-logic terms
    (non-``+`` or ``-`` prefix) can be found. (Note that required terms
    – prefixed with a ``+`` – are always required in a hit regardless of
    this setting. Also note that if likepobeyintersects is true, an @
    operator value in the query will override this setting.)

likepobeyintersects
~~~~~~~~~~~~~~~~~~~
    Setting this to 1 forces ``LIKEP`` to obey the intersects operator
    (@) in queries (even when likepallmatch is true). By default
    ``LIKEP`` does not use it, because it is a ranking operator. Setting
    both ``likepallmatch`` and ``likepobeyintersects`` to 1 will make
    ``LIKEP`` respect queries the same as ``LIKE``. (Note: ``apicp``
    ``alintersects`` may have to be enabled in Vortex as well.)

likepinfthresh
~~~~~~~~~~~~~~
    This controls the “infinity” threshold in ``LIKE`` and ``LIKEP``
    queries: if the estimated number of matching rows for a set is
    greater than this, the set is considered infinitely-occurring. If
    all the search terms found in a given document are such infinite
    sets, the document is given an estimated rank. This saves time
    ranking irrelevant but often-occurring matches, at the possible
    expense of rank position. The default is 0, which means infinite (no
    infinite sets; rank all documents).

likepindexthresh
~~~~~~~~~~~~~~~~
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

    Note that setting ``likepindexthresh`` is a tradeoff between speed
    and accuracy: the lower the setting, the faster queries can be
    processed, but the more queries may be dropping potentially
    high-ranking hits.

Indexing properties
-------------------

indexspace
~~~~~~~~~~
    A directory in which to store the index files. The default
    is the empty string, which means use the database directory. This can be
    used to put the indexes onto another disk to balance load or for space
    reasons. If ``indexspace`` is set to a non-default value when a
    Metamorph index is being updated, the new index will be stored in the
    new location.

    When a Metamorph index is created on an indirect field, the indirect
    files are read in blocks. This property allows the size of the block
    used to be redefined.

indexmem 
~~~~~~~~
    When indexes are created Texis will use memory to speed up
    the process. This setting allows the amount of memory used to be
    adjusted. The default is to use 40% of physical memory, if it can be
    determined, and to use 16MB if not. If the value set is less than 100
    then it is treated as a percentage of physical memory. It the number is
    greater than 100 then it is treated as the number of bytes of memory to
    use. Setting this value too high can cause excessive swapping, while
    setting it too low causes unneeded extra merges to disk.

indexmeter
~~~~~~~~~~ 
    Whether to print a progress meter during index
    creation/update. The default is 0 or ``'none'``, which suppresses the
    meter. A value of 1 or ``'simple'`` prints a simple hash-mark meter
    (with no tty control codes; suitable for redirection to a file and
    reading by other processes). A value of 2 or ``'percent'`` or ``'pct'``
    prints a hash-mark meter with a more detailed percentage value (suitable
    for large indexes). Added in version 4.00.998688241 Aug 24 2001.

    A semicolon-separated list of processes to print a progress meter for.
    Syntax:

         {:math:`process`\ [= :math:`type`]}\|\ :math:`type` [; ...]

    A :math:`process` is one of ``index``, ``compact``, or the catch-all
    alias ``all``. A :math:`type` is a progress meter type, one of ``none``,
    ``simple``, ``percent``, ``on`` (same as ``simple``) or ``off`` (same as
    ``none``). The default :math:`type` if not given is ``on``. E.g. to show
    a progress meter for all meterable processes, simply set ``meter`` to
    ``on``. Added in version 6.00.1290500000 20101123.

addexp
~~~~~~
    An additional REX expression to match words to be
    indexed in a Metamorph index. This is useful if there are non-English
    words to be searched for, such as part numbers. When an index is first
    created, the expressions used are stored with it so they will be updated
    properly. The default expression is ``\alnum{2,99}``. **Note:** Only the
    expressions set when the index is initially created (i.e. the first
    CREATE METAMORPH ... statement – later statements are index updates) are
    saved. Expressions set during an update (issuance of “create metamorph
    [inverted] index” or “create fulltext index” on an existent index) will 
    *not* be added.

delexp
~~~~~~
    This removes an index word expression from the list. Expressions can be
    removed either by number (starting with 0) or by expression.

lstexp
~~~~~~
    Lists the current index word expressions. The value specified is ignored
    (but required syntactically).

addindextmp
~~~~~~~~~~~
    Add a directory to the list of directories to use for temporary files
    while creating the index. If temporary files are needed while creating a
    Metamorph index they will be created in one of these directories, the
    one with the most space at the time of creation. If no ``addindextmp``
    dirs are specified, the default list is the index’s destination dir
    (e.g. database or ``indexspace``), and the environment variables ``TMP``
    and ``TMPDIR``.

delindextmp
~~~~~~~~~~~
    Remove a directory from the list of directories to use for temporary
    files while creating a Metamorph index.

lstindextmp
~~~~~~~~~~~
    List the directories used for temporary files while creating Metamorph
    indices. Aka ``listindextmp``.

indexvalues
~~~~~~~~~~~
    Controls how a regular (B-tree) index stores table values.
    If set to splitstrlst (the default), then ``strlst``-type fields are
    split, i.e. a separate (item,recid) tuple is stored for *each*
    (``varchar``) item in the ``strlst``, rather than just one for the whole
    (strlst,recid) tuple. This allows the index to be used for some set-like
    operators that look at individual items in a ``strlst``, such as most
    ``IN``, ``SUBSET`` (p. ) and ``INTERSECT`` (p. ) queries.

    If ``indexvalues`` is set to ``all`` – or the index is not on a
    ``strlst`` field, or is on multiple fields – such splitting does not
    occur, and the index can generally not be used for set-like queries
    (with some exceptions; see p.  for details).

    Note that if index values are split (i.e. ``splitstrlst`` set and index
    is one field which is ``strlst``), table rows with an empty (zero-items)
    ``strlst`` value will not be stored in the index. This means that
    queries that require searching for or listing empty-\ ``strlst`` table
    values cannot use such an index. For example, a subset query with a
    non-empty parameter on the right side and a ``strlst`` table column on
    the left side will not be able to return empty-\ ``strlst`` rows when
    using an index, even though they match. Also, subset queries with an
    empty-\ ``strlst`` or empty-\ ``varchar`` parameter (left or right side)
    must use an ``indexvalues=all`` index instead. Thus if
    empty-\ ``strlst`` subset query parameters are a possibility, both types
    of index (``splitstrlst`` and ``all``) should be created.

    As with ``stringcomparemode``, only the creation-time ``indexvalues``
    value is ever used by an index, not the current value, and the optimizer
    will attempt to choose the best index at search time. The
    ``indexvalues`` setting was added in Texis version 7; previous versions
    effectively had ``indexvalues`` set to ``splitstrlst``. **Caveat:** A
    version 6 Texis will issue an error when encountering an indexvalues=all
    index (as it is unimplemented in version 6), and will refuse to modify
    the index or the table it is on. **A version 5 or earlier Texis,
    however, may silently corrupt an indexvalues=all index during table
    modifications.**

btreethreshold
~~~~~~~~~~~~~~
    This sets a limit as to how much of an index should be used. If a
    particular portion of the query matches more than the given percent of
    the rows the index will not be used. It is often more efficient to try
    and find another index rather than use an index for a very frequent
    term. The default is set to 50, so if more than half the records match,
    the index will not be used. This only applies to ordinary indices.

btreelog
~~~~~~~~
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
~~~~~~~~~
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

maxlinearrows
~~~~~~~~~~~~~
    This set the maximum number of records that should be searched linearly.
    If using the indices to date yield a result set larger than
    ``maxlinearrows`` then the program will try to find more indices to use.
    Once the result set is smaller than ``maxlinearrows``, or all possible
    indices are exhausted, the records will be processed. The default is
    1000.

likerrows
~~~~~~~~~
    How many rows a single term can appear in, and still be returned by
    ``liker``. When searching for multiple terms with ``liker`` and
    ``likep`` one does not always want documents only containing a very
    frequent term to be displayed. This sets the limit of what is considered
    frequent. The default is 1000.

indexaccess
~~~~~~~~~~~
    If this option is turned on then data from an index can be selected as
    if it were a table. When selecting from an ordinary (B-tree) index, the
    fields that the index was created on will be listed. When selecting from
    a Metamorph index a list of words (``Word`` column‘), count of rows
    containing each word (``RowCount``), and – for Metamorph inverted
    indexes – count of all hits in all rows (``OccurrenceCount``) for each
    word will be returned.

dbcleanupverbose
~~~~~~~~~~~~~~~~

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
~~~~~~~~~~
    For debugging: trace index usage, especially during searches, issuing
    informational ``putmsg``\ s. Greater values produce more messages. Note
    that the meaning of values, as well as the messages printed, are subject
    to change without notice. Aka ``traceindex``, ``traceidx``. Added in
    version 3.00.942186316 19991109.

tracerecid
~~~~~~~~~~
    For debugging: trace index usage for this particular recid. Added in
    version 3.01.945660772 19991219.

indexdump
~~~~~~~~~
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

indexmmap
~~~~~~~~~
    Whether to use memory-mapping to access Metamorph index files, instead
    of ``read()``. The value is a bitwise OR of the following flags:

    Bit 0
        for token file

    Bit 1
        for ``.dat`` file

    The default is 1 (i.e. for token file only). Note that memory-mapping
    may not be supported on all platforms.

indexreadbufsz
~~~~~~~~~~~~~~
    Read buffer size, when reading (not memory-mapping) Metamorh index
    ``.tok`` and ``.dat`` files. The default is 64KB; suffixes like “``KB``”
    are respected. During search, actual read block size could be less (if
    predicted) or more (if blocks merged). Also used during index
    create/update. Decreasing this size when creating large indexes can save
    memory (due to the large number of intermediate files), at the potential
    expense of time. Aka ``indexreadbufsize``. Added in version
    4.00.1006398833 20011121.

indexwritebufsz
~~~~~~~~~~~~~~~
    Write buffer size for creating Metamorph indexes. The default is 128KB;
    suffixes like “``KB``” are respected. Aka ``indexwritebufsize``. Added
    in version 4.00.1007509154 20011204.

indexmmapbufsz
~~~~~~~~~~~~~~
    Memory-map buffer size for Metamorph indexes. During search, it is used
    for the ``.dat`` file, if it is memory-mapped (see ``indexmmap``); it is
    ignored for the ``.tok`` file since the latter is heavily used and thus
    fully mapped (if ``indexmmap`` permits it). During index update,
    ``indexmmapbufsz`` is used for the ``.dat`` file, if it is
    memory-mapped; the ``.tok`` file will be entirely memory-mapped if it is
    smaller than this size, else it is read. Aka ``indexmmapbufsize``. The
    default is 0, which uses 25% of RAM. Added in version 3.01.959984092
    20000602. In version 4.00.1007509154 20011204 and later, “``KB``” etc.
    suffixes are allowed.

indexslurp
~~~~~~~~~~
    Whether to enable index “slurp” optimization during Metamorph index
    create/update, where possible. Optimization is always possible for index
    create; during index update, it is possible if the new insert/update
    recids all occur after the original recids (e.g. the table is
    insert-only, or all updates created a new block). Optimization saves
    about 20% of index create/update time by merging piles an entire word at
    a time, instead of word/token at a time. The default is 1 (enabled); set
    to 0 to disable. Added in version 4.00.1004391616 20011029.

indexappend
~~~~~~~~~~~
    Whether to enable index “append” optimization during Metamorph index
    update, where possible. Optimization is possible if the new insert
    recids all occur after the original recids, and there were no
    deletes/updates (e.g. the table is insert-only); it is irrelevant during
    index create. Optimization saves index build time by avoiding original
    token translation if not needed. The default is 1 (enabled); set to 0 to
    disable. Added in version 4.00.1006312820 20011120.

indexwritesplit
~~~~~~~~~~~~~~~
    Whether to enable index “write-split” optimization during Metamorph
    index create/update. Optimization saves memory by splitting the writes
    for (potentially large) ``.dat`` blocks into multiple calls, thus
    needing less buffer space. The default is 1 (enabled); set to 0 to
    disable. Added in version 4.00.1015532186 20020307.

indexbtreeexclusive
~~~~~~~~~~~~~~~~~~~
    Whether to optimize access to certain index B-trees during exclusive
    access. The optimization may reduce seeks and reads, which may lead to
    increased index creation speed on platforms with slow large-file
    ``lseek`` behavior. The default is 1 (enabled); set to 0 to disable.
    Added in version 5.01.1177548533 20070425.

mergeflush
~~~~~~~~~~
    Whether to enable index “merge-flush” optimization during Metamorph
    index create/update. Optimization saves time by flushing in-memory index
    piles to disk just before final merge; generally saves time where
    ``indexslurp`` is not possible. The default is 1 (enabled); set to 0 to
    disable. Added in version 4.00.1011143988 20020115.

indexversion 
~~~~~~~~~~~~
    Which version of Metamorph index to produce or update, when
    creating or updating Metamorph indexes. The supported values are 0
    through 3; the default is 2. Setting version 0 sets the default index
    version for that Texis release. Note that old versions of Texis may not
    support version 3 indexes. Version 3 indexes may use less disk space
    than version 2, but are considered experimental. Added in version
    3.00.954374722 20000329.

indexmaxsingle
~~~~~~~~~~~~~~
    For Metamorph indexes; the maximum number of locations
    that a single-recid dictionary word may have and still be stored solely
    in the ``.btr`` B-tree file (without needing a ``.dat`` entry).
    Single-recid-occurence words usually have their data stored solely in
    the B-tree to save a ``.dat`` access at search time. However, if the
    word occurs many times in that single recid, the data (for a Metamorph
    inverted index) may be large enough to bloat the B-tree and thus negate
    the savings, so if the single-recid word occurs more than
    ``indexmaxsingle`` times, it is stored in the ``.dat``. The default is
    8.

uniqnewlist
~~~~~~~~~~~
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

tablereadbufsz
~~~~~~~~~~~~~~
    Size of read buffer for tables, used when it is possible to buffer table
    reads (e.g. during some index creations). The default is 16KB. When
    setting, suffixes such as “``KB``” etc. are supported. Set to 0 to
    disable read buffering. Added in version 5.01.1177700467 20070427. Aka
    ``tablereadbufsize``.

Locking properties
------------------

These properties affect the way that locking occurs in the database
engine. Setting these properties without understanding the consequences
can lead to inaccurate results, and even corrupt tables.

singleuser
~~~~~~~~~~
    This will turn off locking completely. *This should be used with
    extreme caution*. The times when it is safe to use this option are
    if the database is read-only, or if there is only one connection to
    the database. Default off. This replaces the prior setting of
    ``nolocking``.

lockmode
~~~~~~~~
    This can be set to either manual or automatic. In manual mode the
    person writing the program is responsible for getting and releasing
    locks. In automatic mode Texis will do this itself. Manual mode can
    reduce the number of locks required, or implement specific
    application logic. In manual mode care must be taken that reads and
    writes can not occur at the same time. The two modes can co-exist,
    in that one process can have manual mode, and the other automatic.
    Default automatic.

locksleepmethod
~~~~~~~~~~~~~~~
    Determines whether to use a portable or OS specific method of
    sleeping while waiting for a lock. By default the OS specific method
    is used. This should not need to be changed.

locksleeptime
~~~~~~~~~~~~~
    How long to wait between attempts to check the lock. If this value
    is too small locks will be checked too often, wasting CPU time. If
    it is too high then the process might be sleeping when there is no
    lock, delaying database access. Generally the busier the system the
    higher this setting should be. It is measured in thousandths of a
    second. The default is 20.

locksleepmaxtime
~~~~~~~~~~~~~~~~
    The lock sleep time automatically increments the if unable to get a
    lock to allow other processes an opportunity to get the CPU. This
    sets a limit on how lock to sleep. It is measured in thousandths of
    a second. The default is 100. Added in version 4.00.1016570000.

fairlock
~~~~~~~~
    Whether to be fair or not. A process which is running in fair mode
    will not obtain a lock if the lock which has been waiting longest
    would conflict. A process which is not in fair mode will obtain the
    lock as soon as it can. This can cause a process to wait forever for
    a lock. This typically happens if there are lots of processes
    reading the table, and one trying to write. Setting ``fairlock`` to
    true will guarantee that the writer can obtain the lock as long as
    the readers are getting and releasing locks. Without ``fairlock``
    there is no such guarantee, however the readers will see better
    performance as they will rarely if ever wait for the writer. This
    flag only affects the process which sets the flag. It is not
    possible to force another process to be fair. The default is that it
    operates in fair mode.

lockverbose
~~~~~~~~~~~
    How verbose the lock code should be. The default minimum level of 0
    will report all serious problems in the lock manager, as they are
    detected and corrected. A verbosity level of 1 will also display
    messages about less serious problems, such as processes that have
    exited without closing the lock structure. Level 2 will also show
    when a lock can not be immediately obtained. Level 3 will show every
    lock as it is released. In version 5.01.1160010000 20061004 and
    later, the level can be bitwise OR’d with 0x10 and/or 0x20 to report
    system calls before and after (respectively). Levels 1 and above
    should generally only be used for debugging. In version
    7.07.1565800000 20190814 and later, 0x40 and 0x80 may be set to
    report before and after semaphore locking/unlocking.

debugbreak
~~~~~~~~~~
    Stop in debugger when set. Internal/debug use available in some
    versions. Added in version 4.02.1045505248 Feb 17 2003.

debugmalloc
~~~~~~~~~~~
    Integer; controls debug malloc library. Internal/debug use in some
    versions. Added in version 4.03.1050682062 Apr 18 2003.

Miscellaneous Properties
------------------------

These properties do not fit nicely into a group, and are presented here.

tablespace
~~~~~~~~~~
    Similar to ``indexspace`` above. Sets a directory into which tables
    created will be placed. This property does not stay set across
    invocations. Default is empty string, which means the database
    directory.

datefmt
~~~~~~~
    This is a ``strftime`` format used to format dates for conversion to
    character format. This will affect ``tsql``, as well as attempts to
    retrieve dates in ASCII format. Although the features supported by
    different operating systems will vary, some of the more common
    format codes are:

    -  Output ``%``

    -  abbreviated weekday name

    -  full weekday name

    -  abbreviated month name

    -  full month name

    -  local date and time representation

    -  day of month (01 - 31)

    -  date as ``%m/%d/%y``

    -  day of month ( 1 - 31)

    -  Hour (00 - 23)

    -  Hour (01 - 12)

    -  day of year (001 - 366)

    -  month (01 - 12)

    -  Minute (00 - 59)

    -  AM/PM

    -  Seconds (00 - 59)

    -  Week number (beginning Sunday) (00-53)

    -  Week day (0-6) (0 is Sunday)

    -  Week number (beginning Monday) (00-53)

    -  local date representation

    -  local time representation

    -  two digit year (00 - 99)

    -  Year with century

    -  Time zone name

    Default ``%Y-%m-%d %H:%M:%S``, which can be restored by setting
    datefmt to an empty string. Note that in version 6.00.1300386000
    20110317 and later, the ``stringformat()`` SQL function can be used
    to format dates (and other values) without needing to set a global
    property.

timezone
~~~~~~~~
    Change the default timezone that Texis will use. This should be
    formatted as for the TZ environment variable. For example for US
    Eastern time you should set timezone to ``EST5EDT``. Some systems
    may allow alternate representations, such as ``US/Eastern``, and if
    your operating system accepts them, so will Texis.

locale
~~~~~~
    Can be used to change the locale that Texis uses. This will impact
    the display of dates if using names, as well as the meaning of the
    character classes in REX expressions, so ``\alpha`` will be correct.
    Also with the correct locale set (and OS support), Metamorph will
    work case insensitively correctly (with mono-byte character sets and
    Texis version 5 or earlier; see ``textsearchmode`` for UTF-8/Unicode
    and version 6 or later support).

indirectcompat
~~~~~~~~~~~~~~
    Setting this to 1 sets compatibility with early versions of Texis as
    far as display of indirects go. If set to 1 a trailing ``@`` is
    added to the end of the filename. Default 0.

indirectspace
~~~~~~~~~~~~~
    Controls where indirects are created. The default location is a
    directory called indirects in the database directory. Texis will
    automatically create a directory structure under that directory to
    allow for efficient indirect access. At the top level there will be
    16 directories, 0 through 9 and a through f. When you create the
    directory for indirects you can precreate these directories, or use
    them as mount points. You should make sure that the Texis user has
    permissions to the directories. Added in version 03.00.940520000

triggermode
~~~~~~~~~~~
    This setting changes the way that the command is treated when
    creating a trigger. The default behavior is that the command will be
    executed with an extra arg, which is the filename of the table
    containing the records. If ``triggermode`` is set to 1 then the
    strings ``$db`` and ``$table`` are replaced by the database and
    table in that database containing the records. This allows any
    program which can access the database to retrieve the values in the
    table without custom coding.

paramchk
~~~~~~~~
    Enables or disables the checking of parameters in the SQL statement.
    By default it is enabled, which will cause any unset parameters to
    cause an error. If paramchk is set to 0 then unset parameters will
    not cause an error, and will be ignored. This lets a single complex
    query be given, yet parameter values need only be supplied for those
    clauses that should take effect on the query.

message,nomessage
~~~~~~~~~~~~~~~~~
    Enable or disable messages from the SQL engine. The argument should
    be a comma separated list of messages that you want to enable or
    disable. The known messages are:

    duplicate
        Message Trying to insert duplicate value () in index when an
        attempt is made to insert a record which has a duplicate value
        and a unique index exists. The default is enabled.

varchartostrlstsep
~~~~~~~~~~~~~~~~~~
*FIXME: add json -ajf*
    [‘varchartostrlstsep’] The separator character or mode to use when
    converting a ``varchar`` string into a ``strlst`` list of strings in
    Texis. The default is set by the ``conf/texis.ini`` setting Varchar
    To Strlst Sep (p. ); if that is not set, the “factory” built-in
    default is ``create`` in version 7 (or ``compatibilityversion`` 7)
    and later, or ``lastchar`` in version 6 (or ``compatibilityversion``
    6) and earlier.

    A value of ``create`` indicates that the separator is to be created:
    the entire string is taken intact as the sole item for the resulting
    ``strlst``, [2]_ and a separator is created that is not present in
    the string (to aid re-conversion to ``varchar``). This can be used
    in conjunction with Vortex’s setting to ensure that single-value as
    well as multi-value Vortex variables are converted consistently when
    inserted into a ``strlst`` column: single-value vars by
    ``varchartostrlstsep``, multi-value by ``arrayconvert``.

    The value ``lastchar`` indicates that the last character in the
    source string should be the separator; e.g. “a,b,c,” would be split
    on the comma and result in a ``strlst`` of 3 values: “a”, “b” and
    “c”.

    ``varchartostrlstsep`` may also be a single byte character, in which
    case that character is used as the separator. This is useful for
    converting CSV-type strings e.g. “a,b,c” without having to modify
    the string and append the separator character first (i.e. for
    lastchar mode).

    ``varchartostrlstsep`` may also be set to ``default`` to restore the
    default (``conf/texis.ini``) setting. It may also be set to
    ``builtindefault`` to restore the “factory” built-in default (which
    changes under ``compatibilityversion``, see above); these values
    were added in version 5.01.1231553000 20090109. If no
    ``conf/texis.ini`` value is set, ``default`` is the same as
    ``builtindefault``.

    ``varchartostrlstsep`` was added in version 5.01.1226978000
    20081117. See also the ``metamorphstrlstmode`` setting (p. ), which
    affects conversion of ``strlst`` values into Metamorph queries; and
    the ``convert`` SQL function (p. ), which in Texis version 7 and
    later can take a ``varchartostrlstsep`` mode argument. The
    ``compatibilityversion`` property (p. ), when set, affects
    ``varchartostrlstsep`` as well.

multivaluetomultirow
~~~~~~~~~~~~~~~~~~~~
    [multivaluetomultirow] Whether to split multi-value fields (e.g.
    ``strlst``) into multiple rows (e.g. of ``varchar``) when
    appropriate, i.e. during GROUP BY or DISTINCT on such a field. If
    nonzero/true, a GROUP BY or DISTINCT on a ``strlst`` field will
    split the field into its ``varchar`` members for processing. For
    example, consider the following table:

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

    The ``multivaluetomultirow`` was added in version 5.01.1243980000
    20090602. It currently only applies to ``strlst`` values and only to
    single-column GROUP BY or DISTINCT clauses. A system-wide default
    for this SQL setting can be set in conf/texis.ini with the Multi
    Value To Multi Row setting. If unset, it defaults to true through
    version 6 (or ``compatibilityversion`` 6), and false in version 7
    and later (because in general GROUP BY/DISTINCT are expected to
    return true table rows for results). The ``compatibilityversion``
    property (p. ), when set, affects this property as well.

inmode
~~~~~~
    How the IN operator should behave. If set to
    ``subset``, IN behaves like the SUBSET operator (p. ). If set to
    ``intersect``, IN behaves like the INTERSECT operator (p. ). Added
    in version 7, where the default is ``subset``. Note that in version
    6 (or ``compatibilityversion`` 6) and earlier, IN always behaved in
    an INTERSECT-like manner. The ``compatibilityversion`` property
    (p. ), when set, affects this property as well.

hexifybytes
~~~~~~~~~~~
    Whether conversion of ``byte`` to ``char`` (or vice-versa) should
    encode to (or decode from) hexadecimal. In Texis version 6 (or
    ``compatibilityversion`` 6) and earlier, this always occurred. In
    Texis version 7 (or ``compatibilityversion`` 7) and later, it is
    controllable with the ``hexifybytes`` SQL property: 0 for off/as-is,
    1 for hexadecimal conversion. This property is on by default in
    ``tsql`` (i.e. hex conversion ala version 6 and earlier), so that
    ``SELECT``\ ing from certain system tables that contain ``byte``
    columns will still be readable from the command line. However, the
    property is off by default in version 7 and later non-\ ``tsql``
    programs (such as Vortex), to avoid the hassle of hex conversion
    when raw binary data is needed (e.g. images), and because Vortex
    etc. have more tools for dealing with binary data, obviating the
    need for hex conversion. (The ``hextobin()`` and ``bintohex()`` SQL
    functions may also be useful, p. .) The ``hexifybytes`` property was
    added in version 7. It is also settable in the ``conf/texis.ini``
    config file (p. ). The ``compatibilityversion`` property (p. ), when
    set, affects this property as well.

unalignedbufferwarning
~~~~~~~~~~~~~~~~~~~~~~
    Whether to issue “Unaligned buffer” warning messages when unaligned
    buffers are encountered in certain situations. Messages are issued
    if this setting is true/nonzero (the default). Added in version
    7.00.1366400000 20130419.

unneededrexescapewarning
~~~~~~~~~~~~~~~~~~~~~~~~
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
    setting ``unneededrexescapewarning`` to 0 – at the risk of silent
    behavior change at an upgrade. Added in version 7.06.1465574000
    20160610. Overrides Unneeded REX Escape Warning setting (p. ) in
    conf/texis.ini.

nulloutputstring
~~~~~~~~~~~~~~~~
    The string value to output for SQL NULL values. The default is
    “``NULL``”. Note that this is different than the output string for
    zero-integer ``date`` values, which are also shown as “``NULL``”.
    Added in version 7.02.1405382000 20140714.

validatebtrees
~~~~~~~~~~~~~~
    Bit flags for additional consistency checks on B-trees. Added in
    version 7.04.1449078000 20151202. Overrides Validate Btrees setting
    (p. ) in ``conf/texis.ini``.

.. [1]
   The index-data-only optimization allows Texis to not only use the
   index to resolve the WHERE clause, but also the SELECT clause in
   certain circumstances, potentially avoiding a read of the table
   altogether and speeding up results. One of the prerequisites for this
   optimization is that the SELECT clause only refer to columns
   available in the index.

.. [2]
   In version 7 (or ``compatibilityversion`` 7) and later, note that in
   create mode, an empty source string will result in an empty
   (zero-items) strlst: this helps maintain consistency of empty-string
   meaning empty-set for strlst, as is true in other contexts. In
   version 6 and earlier an empty source string produced a
   one-empty-string-item strlst in create mode.
