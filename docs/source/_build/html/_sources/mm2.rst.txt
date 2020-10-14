Hits, Intersections and Sets
============================

Definition and Background
-------------------------

A “*hit*” is the term used to refer to a portion of text Metamorph
retrieves in response to a specified query or search request. We use the
term “hit” to mean the program has found or “hit” on something it was
looking for. Hits can have varying degrees of relevance or validity,
based on the number of intersections of concept matched within that
delimited block of text.

An “*intersection*” is defined as an occurrence of at least one member
of each of two sets of strings located within a section of text as
demarcated by a specified beginning and ending delimiter.

A “*set*” is the group of possible strings a pattern matcher will look
for, as specified by the search item. A set can be a list of words and
word forms, a range of characters or quantities, or some other class of
possible matches based on which pattern matcher Metamorph uses to
process that item.

Intersection quantity refers to the number of unions of sets existing
within the specified delimiters. The maximum number of intersections
possible for any given query will be the maximum number of designated
sets minus one.

The number of intersections present in any given hit directly determines
how relevant that response will be to the stated query. Therefore, the
designated intersection quantity can modulate the amount of abstraction
or inference you wish to allow away from the original concept as stated
in your query.

Because intersection quantity can be adjusted by the user to determine
how tightly correlated the retrieved responses must be, the term
“correlation threshold level” has in the past been used interchangeably
with “intersection quantity” in earlier writings and versions of
Metamorph. However, intersection quantity is a more precise lexical
concept, and is therefore the preferred and more accurate term.

Intersections, whether calculated automatically by Metamorph or set by
the user, signify the number of intersections of specified sets being
searched for, to be matched anywhere within the delimited block of text;
this could be within a sentence, paragraph or page or some other
designated amount of text.

Metamorph provides a template of variables which go into a search, all
of which can be stored so that one can analyze exactly what produced the
search results and adjust them as desired. The intersection quantity
variable is the most important in terms of designating how closely you
wish to have the retrieved responses (hits) correlated to your search
query.

Basic Operation
---------------

A default Metamorph search will calculate the maximum number of
intersections possible in a hit based on your query.

Metamorph picks out the important elements of your question and creates
a series of sets of words and/or patterns; counting the number of sets,
it does a search for any hit which contains all those elements. If you
ask: “``Was there a power struggle?``” the program will look for hits
containing 1 intersection of the 2 sets “``power``” and “``struggle``”.
In this case the program is executing the search at intersection
quantity 1, as it is looking for text containing 1 union of 2 sets.

When you first enter the query, Metamorph determines how many sets of
concepts it will be searching for. Upon execution, the program will
attempt to find hits containing the maximum number of intersections of
something from each of those sets, within the defined delimiters of a
sentence.

A “sentence” is specified as the block of text from one sentence type
delimiter ``[.?!]`` to the next sentence type delimiter ``[.?!]``. Any
sentence found which contains that maximum number of intersections will
be considered a valid hit, and can be made available for viewing, in the
context of the full text of the file in which it is located.

Intersections and Search Logic
------------------------------

A common and simple search would be to enter a few search items on the
query line, in hopes of locating a sentence which matches the idea of
what you have entered. You want a sentence containing some correlation
to the ideas you have entered. For example, enter on the query line:

::

         property evaluation

This should find a relevant response containing a connection to both
entered keywords. The user would be happy to locate the sentence:

    The broker came to **assess** the current market value of our
    **house**.

Here “``assess``” is linked to “``evaluation``”, and “``house``” is
linked to “``property``”, within the bounds of one sentence. Therefore
it is a hit, and can be brought up for viewing.

Requiring an occurrence of 2 search items is referred to as 1
intersection. The occurrence of 3 search items is 2 intersections. The
default search logic is to look for the maximum number of intersections
possible of all entered search items. If you were searching large
quantities of text, you could add several qualifiers, as follows:

::

         property tax market assessment

The maximum number of intersections would be looked for; i.e., an
occurrence of “``property``”, “``tax``”, “``market``”, and
“``assessment``”, anywhere inside the text unit. This can be thought of
as “and” logic.

To override the default “and” logic, you would enter the desired number
of intersections with ‘``@#``’: as “``@0``” (“at zero intersections”),
“``@1``” (“at one intersection”), “``@2``” (“at two intersections”), and
so on.

To get “or” logic you would enter “``@0``” on the query line with your
search items. In this example:

::

         @0 property tax

“``@0``” (read as: “at zero intersections”) means that zero
intersections are required; therefore you are specifying that you want
an occurrence of either “``property``” or “``tax``” anywhere inside of
the delimited text, or sentence.

To obtain different permutations of logic you can specify a number of
intersections greater than zero but less than an intersection of all
specified items. For example:

::

         @1 property tax value

requires one intersection of any two of the three specified items.
Therefore, the text unit will be retrieved if it contains an occurrence
of “``property``” and “``tax``”, “``property``” and “``value``”, or
“``tax``” and “``value``”.

Any search item not marked with a ‘``+``’ or a minus ‘``-``’ is assumed
to be equally weighted. Each unmarked item could be preceded with
‘``=``’ but it is not required on the query line as it is understood.
Intersection quantities “``@#``” apply to these equally weighted sets
not otherwise marked with ‘``+``’ or ‘``-``’.

Mandatory inclusion ‘``+``’ and mandatory exclusion ‘``-``’ logic can be
assigned, and can be used with combinatorial logic. For example, you
might enter this query:

::

         +tax -federal @1 market assessment value property assets w/page

This query means that within any page (“``w/page``”), you must include
an occurrence of “``tax``” (designated with the plus sign ‘``+``’), but
must exclude the hit if it contains any reference to “``federal``”
(designated with the minus sign ‘``-``’). The “``@1``” means that you
also want 1 intersection of any 2 of the other 5 equally weighted
(unmarked with ‘``+``’ or ‘``-``’) query items: “``market``”,
“``assessment``”, “``value``”, “``property``”, and “``assets``”.

Intersection Quantity Specification Further Explained
-----------------------------------------------------

If you want all possibilities you can find, you will find very adequate
answers to your search requests usually by setting the quantity to 1 or
2. If you don’t get answers at that quantity, drop it lower.

With intersections at 0, the program will retrieve hits containing 0 set
intersections; i.e., it will locate any occurrence of any set element.
The denotation of 0 indicates no intersection of specified sets is
required. In other words, you want Metamorph to locate any hit in which
there is any equivalence to any of the set elements in your question.
This is comparable to an “or” search.

With intersections at 1, the program will retrieve hits containing an
intersection of any 2 specified set elements; for example, an
intersection of “``life``” and “``death``”. With intersections at 2, the
program will retrieve hits containing any 2 intersections of any 3
specified set elements; e.g., “``life``” and “``death``” plus
“``death``” and “``infinity``”. With the intersection variable at 3, the
program might locate an occurrence of “``life``” and “``death``”,
“``death``” and “``infinity``”, plus “``infinity``” and “``money``”. The
higher the number of intersections being sought, the more precise the
retrieved response must be.

The lower you set the intersection quantity, the more hits are likely to
be found in relation to the question asked; but many of them are likely
to be quite abstract. The higher you set the intersection quantity, the
fewer hits are likely to be found in relation to the question asked; but
those hits are likely to be more intelligently and precisely related to
the search query, as they require a higher number of matching sets.

A smaller intersection quantity will also retrieve any larger
intersection quantity responses as a matter of course. However, the
program only searches for the number of intersections of elements it is
directed to search for, so it will only highlight the requested number
of sets.

Conversely, the program cannot locate hits containing more intersections
than are possible. If you have set intersections to 6, but you enter a
question containing only 3 set elements, realize that one pass through
the data will find no answers, as you have designated more intersections
than are possible to find.

How ‘+’ and ‘-’ Logic Affects Intersection Settings
---------------------------------------------------

The assignment of intersection quantity refers to the number of
intersections of sets being sought in the search. This presumes that all
sets are equal to each other in value. A search for:

::

          life  love  death
         =life =love =death  (same as above)

presumes that each set has equal value; if something from each set is
found, it would reflect an intersection quantity of 2, as it contains 2
intersections of “equal” value. In Metamorph terms, this is referred to
as “set logic”.

When you assign a ‘``+``’ (“must include”) logic operator to a set, this
means that you must include something from that designated set in any
hit that is found. When you assign a ‘``-``’ (“not”) logic operator to a
set, this means that you must exclude any hit which contains any element
from that designated set.

Examples of these might be:

::

         +spirit =life =love =death
                   or
         -alien =life =love =death

It doesn’t matter whether the equal sign ‘``=``’ is entered or not. It
also doesn’t matter what sequence these operand sets are entered in, or
how many of each. The only rule is you cannot enter only “not” sets.

In the above two examples, the maximum intersection quantity is still 2.
The “must include” (``+``) set (“and logic”) and the “must exclude”
(``-``) set (“not logic”) are outside the calculation of maximum
intersections possible, as both are special cases. In a search where you
designate something like this:

::

         war peace -Reagan +diplomacy

the maximum intersection quantity is only 1, referring to an
intersection of the “``war``” and the “``peace``” sets. Therefore, if
you wish to definitely find something related to “``diplomacy``”, but to
exclude any references related to “``Reagan``”, and you want to
intersect what is found with either war or peace, set intersections to
0:

::

         @0 war peace -Reagan +diplomacy

The 0 intersection setting applies to either the “``war``” set or the
“``peace``” set (the unmarked equally weighted sets), as both
“``Reagan``” and “``diplomacy``” are special cases and not included in
the intersection quantity calculation.

Modulating Set Size
-------------------

Metamorph can draw upon a vast built-in intelligence comprised of
equivalence sets, as contained in a 2MB+ collection of 250,000+ word
associations. The user has the option of how much of this “mind” he
wants to draw upon at any given time, by regulating to what degree he
wishes to make use of the built-in connective structure in this
Equivalence File.

When enabled via ``keepeqvs``/``Synonyms`` (for the entire query) or the
``~`` operator (for a particular query term), each query term is
expanded by pulling from the Equivalence File the set of known
associations listed with each entry. This lets you draw upon the full
power of all the associations in all of its equivalence sets. Still, you
have the option of quantitatively limiting or expanding this set
expansion, or selectively editing their content.

Limiting Sets to Roots Only
~~~~~~~~~~~~~~~~~~~~~~~~~~~

There may be times when you want to limit set size to root words only
from your search query. This is the default in ``tsql``, Vortex and the
Search Appliance. If using the MM3 API, or if equivalences have been
enabled with ``keepeqvs`` (Vortex/``tsql``) or ``Synonyms`` (Search
Appliance), then equivalences can be excluded for a particular word –
while still retaining morpheme processing – by preceding the word with a
tilde (``~``). This should be done where you wish to look for
intersections of concepts while holding abstraction to a minimum, and
you do not want any automatic expansion of those words into word lists.
If equivalences are not currently enabled via ``keepeqvs``/``Synonyms``,
then the ``~`` operator has the opposite effect: it *enables*
equivalences for that term only. Thus, it always toggles the pre-set
behavior for its word.

Regardless of settings, you can also give explicit equivalences directly
in the query, instead of the thesaurus-provided ones, using parentheses;
see p. .

Where you wish to use morpheme processing on root words only,
restricting concept expansion completely, turn off Equivalence File
access altogether by setting the global APICP flag “``keepeqvs``” off.
Where “``keepeqvs``” is set to off, the tilde (``~``) is not required
(indeed, it would re-enable concept expansion).

Restricting set expansion is useful for proper nouns which you do not
want expanded into abstract concepts (e.g., ``President Bush``),
technical or legal terminology, or simply any precise discrete search.
Selectively cut off the set expansion by designating a tilde (``~``)
preceding the word you are looking for.

Using REX syntax by preceding the word with a forward slash (``/``), can
further delimit the pattern you are looking for, though in a different
manner. Note however, that using non-SPM/PPM pattern matchers such as
REX may slow the query, as Metamorph indexes cannot be used for such
terms.

To check this out for yourself, in an application where Metamorph hit
markup has been set up, compare the results of the following queries
(assuming Vortex or ``tsql`` defaults):

::

         Query 1:    President  Bush
         Query 2:   ~President ~Bush
         Query 3:   "President Bush"
         Query 4:   /President /Bush
         Query 5:   /\RPresident /\RBush

Query 1
    ``President Bush``: In the first example you would get any hit
    containing an occurrence of the word “``President``” and the word
    “``Bush``”, including other related word forms (suffixes etc.). So
    you would get a hit like “***President** **Bush** came to tea.*”; as
    well as “***Bush** attended a conference of corporation
    **presidents***.” There are no equivalences added to the
    “``President``” set, or the “``Bush``” set.

Query 2
    ``~President ~Bush``: In the second example you have elected to keep
    the full set size, so you would obtain references to “``President``”
    and “``Bush``” while also allowing for other abstractions. Since the
    word “``chief``” is associated with “``president``”, and the word
    “``jungle``” is associated with “``bush``”, you would retrieve a
    sentence such as, “*We met the **chief** at his home deep in the
    Amazon **jungle**.*”

Query 3
    ``"President Bush"``: The third example calls for “``President``”
    and “``Bush``” as a two word phrase by putting it in quotes, so that
    it will be treated as one set rather than as two. It has no
    equivalences, because the phrase “``President Bush``” has no
    equivalences known by the Equivalence File; you could add
    equivalences to that phrase if you wished by editing the User Equiv
    File. While you would retrieve the hit “***President Bush** came to
    tea.*”, you would exclude the hit “***Bush** attended a conference
    of corporation **presidents**.*” You would get a hit like “*We
    elected a new **president Bush**.*”

Query 4
    ``/President /Bush``: In the fourth example you have limited the
    root word set in a different way. Signalling REX with the forward
    slash ‘``/``’ means that you will use REX to accomplish a string
    search on whatever comes after the ‘``/``’. Therefore, you can find
    “*Our **president**\ ’s name is **Bush**.*” and “*We planted those
    **bush**\ es near the **President**\ ’s house.*” This search gets
    similar yet different results than Example 1. Look at exactly what
    is highlighted by the Metamorph hit markup to see the difference in
    what was located.

Query 5
    ``/\RPresident /\RBush``: In the fifth example there is better
    reason to use REX syntax, so that you can limit the set even further
    by specifying proper nouns only. The designation ‘``\R``’ means to
    “respect case”, and would retrieve the sentence “***President**
    **Bush** came to tea.*”, but would rule out the sentence “***Bush**
    attended a conference of corporation **president**\ s.*” It would
    also rule out the hit “*We elected a new **president** **Bush**.*”,
    and “*We planted those **bush**\ es near the **President**\ ’s
    house.*”

    NOTE: In the previous example, the “respect case” designation
    (``\R``) must follow a forward slash (``/``) which indicates that
    REX syntax follows. Remember that words in a query are not case
    sensitive unless you so designate, using REX. (See Chapter on REX
    syntax.)

Expanding Sets by Use of See References
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can exponentially increase the denseness of connectivity in the
equivalence file by turning on the option called “See References”. This
greatly increases set size overall.

The concept of a “See” reference is the same as in a dictionary. When
you are looking up the word “``cat``”, you’ll get a description and a
few definitions which try to outline exactly what the concept of “cat”
is all about. Then it may say at the end, “``See also pet``”.

With See References set to on, the equivalence list associated with the
word “``cat``” would be automatically expanded to include all
equivalences associated with the word “``pet``”. Not all equivalences
have “``See also``” notations, but with See References on, all
equivalences associated with any “``See also``” root word will be
included as part of the original. With See References off, only the root
word and its equivalences will be included in that word set.

In the Equivalence File, See References are denoted with the at sign
(``@``). In this way, additional second level references can be added or
deleted from a root word entry.

For some of the more common words like “``take``” and “``life``” and
such, allowing See References can make a set surprisingly huge, so you
should think twice before operating in this mode. It is really intended
to expand the range of opportunity to the user, so that there is more to
pick and choose from when editing the content of the word lists.

If you are using the default equivalence sets it is generally better to
run with the “``See``” option “``off``”. You would not normally want to
be doing searches where the word lists are too large; we suggest
limiting the set size to well under 100 in any case, whether you are
running with See References either on or off.

In a theoretical sense, given no limitations, using endless levels of
See References could eventually create a circle which covered the whole
spectrum of life, and led right back to the word one had started with.
Therefore Metamorph has intentionally built-in limitations on set size
to prevent this connectivity of concept from becoming redundant or out
of hand. Firstly, See References are restricted to only one level of
reference. Secondly, if a word set approaches around 256, Metamorph will
automatically truncate it in the search process, cutting off any words
outside the numerical limit.

To make use of the See References option, set the corresponding APICP
flag “``see``”.

Modulating Set Content
----------------------

The ability to dictate precise content of all word sets passed to the
Metamorph Search Engine is left open to the user on an optional basis.
We have tried to build into the program enough intelligence through its
Equivalence File so that you would never be required to pay any
attention to the lists of equivalences if you didn’t want to.

Even if Metamorph has no known associations with a word it will still
process it with no difficulty, using its built-in morpheme processing
capability. You’ll find that more often than not you are not required to
teach Metamorph anything about your search vocabulary to get
satisfactory results, even when using technical terminology.

When you want to control exactly what associations are made with any or
all the words or expressions in your searches, you can do so by editing
the equivalence set associated with any word already known by the
Equivalence File, or by creating associations for a new or created word
not yet known. This is covered in detail under the Chapter on *Thesaurus
Customization*.

Modulating Hit Size by Adjusting Delimiters
-------------------------------------------

Discussion
~~~~~~~~~~

Delimiters can be defined as the beginning and ending patterns in the
text which define the text unit inside of which your query items will be
located. Concept proximity is adjusted through delimiters.

If you look for the words “``bear``” and “``woods``” within a sentence,
the result will be a tight match to your query. Looking for “``bear``”
and “``woods``” inside a paragraph is less restrictive. Requiring only
that “``bear``” and “``woods``” occur inside the same section might or
might not have much to do with what you are looking for. Knowing that,
you would probably add more qualifying search items when searching by
section; e.g., “``bear``” “``woods``” “``winchester``” “``rifle``”. In
short, the relevance of your search results will differ greatly based on
the type of text unit by which you are searching.

Beginning and ending delimiters are defined by repeating patterns in the
text, technically known as regular expressions. A sentence can be
identified by its ending punctuation (period, question mark, or
exclamation point); a paragraph is often identified by a few new lines
in a row; a page is often identified by the presence of a page
formatting character.

In a Metamorph search, the entered query concepts will be searched for
within the bounds of two such expressions, called delimiters. These
regular expressions are defined with REX syntax for Regular Expressions.
If you know how to write a regular expression using REX you can enter
delimiters of your own design. In lieu of that you can rely upon the
defaults that have been provided.

Since the most common types of search are by line, sentence, paragraph,
page, or whole document, these expressions have been written into the
Metamorph Query Language so that you can signal their use dynamically
from within any query, using English. The expression “``w/delim``” means
“within a …” where 4 characters following “``w/``” are an abbreviation
for a commonly delimited unit of text. If you want to search by
paragraph, you would add “``w/para``” as an item on the query line.

For example:

::

         power struggle w/para

Such a search will look within any paragraph for an occurrence of the
concept “``power``” and the concept “``struggle``”.

You can designate a quantitative proximity, using the same syntax, by
stating how many characters you want before and after the located search
items. For example:

::

     power struggle w/150

Such a search will look within a 300 character range (150 before, 150
after the first item located) for an occurrence of the concept ``power``
and the concept ``struggle``. This is useful for text which doesn’t
follow any particular text pattern, such as source code.

You can also write your own expressions, useful for section heads and/or
tails. To enter delimiters of your own design, create a REX expression
first which works, and enter it following the “``w/``”. For example:

::

         power struggle w/\n\RSECTION

This search uses the pattern “``SECTION``” where it begins on a new line
and is in Caps only, as both beginning and ending delimiters. Thus an
occurrence of the set “``power``” and the set “``struggle``” need only
occur within the same section as so demarcated, which might be several
pages long.

You can figure out such useful section delimiter expressions when
setting up an application, and use the corresponding APICP flag
``sdexp`` (start delimiter expression) and ``edexp`` (end delimiter
expression) in a Vortex script to make use of them.

The following examples of queries would dictate the proximity of the
search items “``power``” and “``struggle``”, by specifying the desired
delimiters. Noting the highlighted search items, you might try these
searches on the demo text files to see the difference in what is
retrieved.

::

      power struggle w/line   within a line (1 new line)
      power struggle w/sent   within a sentence (ending punctuation)
      power struggle w/para   within a paragraph (a new line + some space)
      power struggle w/page   within a page (where format character exists)
      power struggle w/all    within whole document (all of the record)

      power struggle w/500    within a window of 500 characters forward
                              and backwards

      power struggle w/$$$    within user designed expression for a section;
                              where what follows the slash `/' is assumed to
                              be a REX expression.  (In this case, the
                              expression means 3 new lines in a row.)

More often than not the beginning and ending delimiters are the same.
Therefore if you do not specify an ending delimiter (as in the above
example), it will be assumed that the one specified is to be used for
both. If two expressions are specified, the first will be beginning, the
second will be ending. Specifying both would be required most frequently
where special types of messages or sections are used which follow a
prescribed format.

Another factor to consider is whether you want the expression defining
the text unit to be included inside that text unit or not. For example,
the ending delimiter for a sentence (ending punctuation from the located
sentence) obviously belongs with the hit. However, the beginning
delimiter (ending punctuation from the previous sentence) is really the
end of the last sentence, and therefore should be excluded.

Inclusion or exclusion of beginning and ending delimiters with the hit
has been thought out for the defaults provided. However, if you are
designing your own beginning and ending expressions, you may wish to so
specify.

Delimiter Syntax Summary
~~~~~~~~~~~~~~~~~~~~~~~~

::

            w/{abbreviation}
         or
            w/{number}
         or
            w/{expression}
         or
            W/{expression}
         or
            w/{expression} W/{expression}
         or
            W/{expression} w/{expression}
         or
            w/{expression} w/{expression}
         or
            W/{expression} W/{expression}

Rules of Delimiter Syntax
~~~~~~~~~~~~~~~~~~~~~~~~~

-  The above can be anywhere on a query line, and is interpreted as
   “within {the following delimiters}”.

-  Accepted built-in abbreviations following the slash ‘``/``’ are:

   | ``[^\digit\upper][.?!][\space'"]``\ xxx = Meaning ABBREV MEANING
   | ``line`` within a line
   | ``sent`` within a sentence
   | ``para`` within a paragraph
   | ``page`` within a page
   | ``all`` within a whole record

   | ``[^\digit\upper][.?!][\space'"]``\ xxx = Meaning REX EXPRESSION
     MEANING
   | ``$`` 1 new line
   | ``[^\digit\upper][.?!][\space'"]`` not a digit or upper case
     letter, then
   | a period, question, or exclamation point, then
   | any space character, single or double quote
   | ``\x0a=\space+`` a new line + some space
   | ``\x0c`` form feed for printer output
   | ``""`` both start and end delims are empty

-  A number following a slash ‘``/``’ means the number of characters
   before and after the first search item found. Therefore “``w/250``”
   means “within a proximity of 250 characters”. When the first
   occurrence of a valid search item is found, a window of 250
   characters in either direction will be used to determine if it is a
   valid hit. The implied REX expression is: “``.{250}``” meaning “250
   of any character”.

-  If what follows the slash ‘``/``’ is not recognized as a built-in, it
   is assumed that what follows is a REX expression.

-  If one expression only is present, it will be used for both beginning
   and ending delimiter. If two expressions are present, the first is
   the beginning delimiter, the second the ending delimiter. The
   exception is within-\ :math:`N` (e.g. “``w/250``”), which always
   specifies both start and end delimiters, overriding any preceding
   “``w/``”.

-  The use of a small ‘``w``’ means to exclude the delimiters from the
   hit.

-  The use of a capital ‘``W``’ means to include the delimiters in the
   hit.

-  Designate small ‘``w``’ and capital ‘``W``’ to exclude beginning
   delimiter, and include ending delimiter, or vice versa Note that for
   within-\ :math:`N` queries (e.g. “``w/250``”), the “delimiter” is
   effectively always included in the hit, regardless of the case of the
   ``w``.

-  If the same expression is to be used, the expression need not be
   repeated. Example: “``w/[.?!] W/``” means to use an ending
   punctuation character as both beginning and end delimiter, but to
   exclude the beginning delimiter from the hit, and include the end
   delimiter in the hit.
