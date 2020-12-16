Types of Searches in Metamorph
==============================

Multiple Search Algorithms
--------------------------

The Metamorph Query Language allows for several methods of searching.
You can enter a natural language question. You can specify which words,
phrases, or regular expressions you wish to look for, and your search
request will be processed accordingly. To accomplish all this, several
different search algorithms are used which go about pattern matching in
different ways. These are called up internally by Metamorph. The chief
pattern matchers in use are:

SPM
    Metamorph’s Single Pattern Matcher (includes wildcarding, the ‘\*’
    operator)

PPM
    Metamorph’s Parallel Pattern Matcher

REX
    Metamorph’s Regular EXpression Pattern Matcher

XPM
    Metamorph’s ApproXimate Pattern Matcher

NPM
    Metamorph’s Numeric Pattern Matcher

When you enter the most common kind of Metamorph search, a normal
English word, Metamorph calls ``SPM`` or ``PPM``. ``SPM`` handles the
morpheme processing for root words which have no equivalences; ``PPM``
handles the root words with their lists of equivalences expanded into
sets of words. Where there is only a single word in a list (i.e., a root
word which has no equivalences) ``SPM`` is used instead so as to
optimize search speed. ``PPM`` searches for every occurrence of every
valid word form for each item in a list in parallel, and will handle the
multiple lists of words created from a routine query.

``PPM`` and ``SPM`` make it possible to routinely execute such searches
at tremendous speed, locating hits containing all combinations of all
items from each of these lists.

Entering words in English calls ``PPM`` or ``SPM``; this is the default
and no special denotation is necessary. You can make use of a wildcard
(``*``) operator with English words if you wish. Entering
“``gorb*yelt``” would locate “``Gorbachev had a meeting with Yeltsin``”.
The asterisk (``*``) will locate up to 80 characters per asterisk noted.

``REX`` makes it possible to look for fixed or variable length regular
expressions of any kind and is integrated into the Metamorph search
routine so that you can mix and match words and regular expressions. You
signal ``REX`` by putting a a forward slash (``/``) in front of the word
or expression, and ``REX`` will be called by Metamorph to handle that
string, utilizing all the rules of ``REX`` syntax.

``XPM`` allows you to specify an “almost right” pattern which you are
unsure of, so that you can find approximately what you have specified.
``XPM`` is also integrated into the search procedure and can be mixed in
with ``PPM`` word searches and ``REX`` regular expressions; you signal
``XPM`` with a percent sign (``%``) denoting the percentage of proximity
to the entered pattern you desire.

``NPM`` allows you to look for numeric quantities in text which may have
been expressed in English. ``NPM`` does number crunching through all
possible numbers found in the text to locate those numbers which are in
the specified range of desired numbers. It is generally used in
combination with some other search item, such as a unit. ``NPM`` is
signalled with a pound sign (``#``) preceding the numeric quantity you
wish to match.

The heart of Metamorph’s ability to encompass so many functions and
subroutines so effectively, in a way which produces quick results for
the user in acceptable response time, is its exceedingly fast search
algorithms. Other bodies of technology have attempted to create small
replicas of a few of the functions in Metamorph, but none of this can be
successful if it cannot be done fast enough to get plentiful and
accurate search results.

Metamorph on its own has been benchmarked on some fast Unix machines at
around 4.5 million characters per second internal throughput rate. The
speed and accuracy of the pattern matching techniques employed make
possible Metamorph’s versatile and flexible operation.

The Natural Language Question
-----------------------------

The natural language question is an easy way to phrase a search query,
where the APICP flag ``keepnoise`` has been left off. Type in a question
which contains the elements you are looking for in the text. There are
really only these few rules to keep in mind.

-  Use only the important words in your query. Metamorph assumes every
   important concept in your question must be matched to qualify it as a
   hit.

-  Pronouns are meaningless to Metamorph; rephrase them as the nouns
   that they actually represent.

-  Idiomatic expressions should be avoided if possible, unless they are
   legitimately important to the sense of the question. Metamorph may or
   may not be aware of the true meaning of such a phrase.

An example of a “good” question to ask would be:

::

         Have there been any corporate takeovers?

An example of a less desirable question would be:

::

         What information is there on corporate takeovers?

The reason is that in this second question, the concept
“``information``” must be stated in the text along with the concept
“``corporate``” and “``takeover``” to be considered a match, probably
excluding relevant responses; any hit from a news article is implicitly
understood to be information without so stating. All you really need are
the two important concepts “``corporate``” and “``takeover``”.

Another “good” question might be:

::

         Were there any corporate takeovers in Germany?

or

::

         Have there been any power struggles in the Near East?

Where idiomatic expressions or phrases exist as entries in the
Equivalence File, they will be meaningfully processed as a whole,
whether so marked or not. The question parser will check for phrase
entries in an entered query. If the word grouping is not known to be a
phrase, the words will be processed separately, according to their
individual associations. If it is important to you that the words of
such an expression or phrase be processed as one unit and Metamorph does
not recognize it as such, mark it as a phrase by putting it in quotes.

Once your question is entered, the question parser will select the
important words and denote them as root words, to be expanded into sets
from the known associations in the Equivalence File. These words and all
their equivalences are in the main passed to ``PPM`` to be searched for
simultaneously while determining where answers to your question might
lie.

As a rule, unless escaped with a backslash (``\``), hyphens are stripped
from the words on the query line before expanded to include their
associations in the Equivalence File and sent to the search engine. Once
passed to ``PPM`` or ``SPM`` occurrences of those words as separated by
either hyphens or white space will be located.

Single/Multiple Items, Keywords, Phrases, Wildcard Patterns (\*)
----------------------------------------------------------------

It isn’t required that you ask a question. Any search item can be
entered on the query line. The simplest search would be to enter one
keyword, like “``bear``”. All matches containing just the word ``bear``
(subject to morpheme processing, if turned on) will be retrieved.

Equivalences (from the thesaurus) for query words may also be searched
for, in one of two ways. First, equivalences can be turned on for *all*
terms by setting ``keepeqvs`` (Vortex/``tsql``) or ``Synonyms`` (Search
Appliance) on. Second, equivalences can be toggled (reverse the
``keepeqvs`` setting) for *individual* query terms with the tilde
(“``~``”) operator. [1]_

For example, with default settings [2]_ (``keepeqvs`` off), the query
“``~bear arms``” will find all equivalences to the word “``bear``” –
i.e. “``cub``”, “``endure``”, “``carry``” etc. – but only the single
term “``arms``”. If we turned ``keepeqvs`` on, the exact same query
would find only the single word “``bear``” (tilde now toggles equivs to
off) but all equivalences for the word “``arms``”.

[‘MetamorphParenSet’] To look for a specific set of equivalences for a
keyword – instead of equivalences derived from the thesaurus – enter
them in parentheses, separated by commas (with no spaces). E.g.
“(bolt,fastener,screw)” would find any of “``bolt``”, “``fastener``”, or
“``screw``”. Note that wildcards (see below) are disabled in
parenthetical lists, however morpheme processing is still done if turned
on.

Entering more than one keyword on the query line will be interpreted as
2 search items, as delimited by a space character, unless it is a phrase
known by the Equivalence File. To link any words together as a phrase
you need only put it in quotes. For example,
“``Alabama Representative``” must find those two words in that sequence,
as a phrase. Such a phrase can be entered as a new entry in the
Equivalence File, and specific names of Alabama Representatives could be
associated as a set. Thereafter the quotes would not be required on the
query line for it to be processed as a single search item.

A wildcard ‘``*``’ can be used along with an English word to extend a
rooted pattern by up to 80 characters per asterisk ‘``*``’. For example,
“``Pres*Bush``” would locate “``President George Bush``”. More than one
asterisk ‘``*``’ may be used. Such an item which includes an asterisk is
matched by a special operator which is part of ``SPM``, the Single
Pattern Matcher which looks for single items.

A wildcarded item can be searched for in intersection with other search
items as well. For example: “``Pres*Reagan campaign``” would locate the
sentence “***President Ronald Reagan** won the **election** in
November.*”

A wildcard operator ‘``*``’ means just that: “anything” before of after
the string to which it is rooted. If you occasionally find that the
morpheme processing rules for a given word are not treating it
correctly, you can substitute a wildcard to locate the word in a
different way. Even though “``property``” will also find
“``properties``” through morpheme processing, “``prop*``” will find the
word “``properties``” for different reasons. “``prop*``” will also find
“``proper``” and “``propane``”, which morpheme processing would
intelligently exclude.

Intelligent Intersection Search
-------------------------------

The intelligent intersection search is used to locate logical
intersections of equivalence sets within defined textual delimiters, a
sentence being a good default to use for a context weighted application.

Where the APICP flag ``keepnoise`` has been turned on, Metamorph is
directed to keep all words entered as part of the query whether noise or
not. With noise words being retained as search words, remember not to
use extraneous words or punctuation unless it is meant to be part of a
designated search item. The same type of search is being used where
noise is being filtered out, but applied only to the non-noise words.

Specify the words you want to look for in your query, separated by
space. No punctuation or other designation is required. In this query:

::

         life death disease

you will get hits containing intersections of occurrences from the
“``life``”, “``death``”, and “``disease``” equivalence sets as well as
the morphological constructs connected with those words.

You are signalling the program to look for intersections of each of the
sets you have specified on the query line. If not otherwise marked this
indicates a ``PPM`` search (or ``SPM`` search if there are no
equivalences) where Metamorph’s set logic, morpheme processing,
equivalence handling, and so forth is called for, for each of the words
you enter on the line as delimited by spaces. Preceding a word with a
tilde (``~``) signifies you want the root word only without
equivalences, calling ``SPM``.

You can also specify a ``REX`` expression by preceding it with a forward
slash (``/``), or an ``XPM`` expression by preceding it with a percent
sign (``%``), or an ``NPM`` expression by preceding it with a pound sign
(``#``), or a phrase by putting it in quotes, or a wildcard pattern by
appending or preceding a word with an asterisk (``*``). Each such entry,
as well as words, terms or acronyms, as delimited by spaces, will be
understood as a separate set for which intelligent intersections will be
looked for. Logic operators plus (``+``) or minus (``-``) can be
assigned to any of these search item sets.

String Search (Literal Pattern Match)
-------------------------------------

To get Metamorph to do a literal string (pattern matching) search type a
slash ‘``/``’ preceding the string. If you want to enter a whole line to
be viewed as one string, put it in quotes, with the forward slash inside
the quotes. Example:

::

         "/Uncle Sam's soldiers"

This query will go and get each place in the textfiles being queried
where that phrase is located, exactly as entered. Anything to the right
of the slash, including a space before the word if you enter it so, will
be considered part of the string; so don’t enter a period or a space
unless you want to look for one.

In the above example, “``Uncle Sam's soldiers``”, you would get the same
result whether a slash was entered or not, since there are no known
equivalences for the phrase “``Uncle Sam's soldiers``”. However, if you
compare the following:

::

         "/frame of mind"   (as compared to)  "frame of mind"

you will see that the Equivalence File has some equivalences associated
with the phrase “``frame of mind``”. To cut off those equivalences and
just look for the pattern “``frame of mind``” you could insert the
forward slash (``/``) as the first character of the phrase inside the
quotes. (You could accomplish the same thing more efficiently by
preceding the phrase with a tilde ``~``.)

When you denote a slash (``/``), remember that you’re signalling the
Metamorph Engine to use ``REX``, bypassing the usual English word
processing that goes on where ``PPM`` is the algorithm most often in
use. ``REX`` can sometimes be more direct when such a task is all that
is required.

While you can use a forward slash (``/``) in front of any fixed length
pattern as is herein discussed, ``REX`` has many more uses which involve
special characters. If such characters are part of your string and are
therefore being inappropriately interpreted, use the backslash (``\``)
to escape those characters inside your string; e.g., “``43\.4``” would
indicate a literal decimal point (``.``), rather than its special
meaning inside a ``REX`` expression.

Fixed and Variable Length Regular Expressions (REX)
---------------------------------------------------

REX Description
~~~~~~~~~~~~~~~

REX stands for Regular EXpression Pattern Matcher. ``REX`` gives you the
ability to match ranges of characters, symbols, and numbers, as well as
to selectively designate how many of each you wish to look for. By
combining such pattern designations into what is called a “Regular
Expression” you can look for such things as phone numbers, chemical
formulas, social security numbers, dates, accounting amounts, names
entered in more than one form, ranges of years, text formatting
patterns, and so on.

As REX is also supplied with the Texis package as a separate utility,
the next chapter is devoted to a detailed account of all of ``REX's``
syntax and features, and can be studied to learn how to designate
complex regular expressions and how to use it to accomplish tasks
outside a normal search envirnoment.

While a complete understanding is not required for the casual searcher,
the better you understand how to describe expressions using ``REX``
syntax, the more you will be able to make use of it, in or outside Texis
or a Metamorph search application.

``REX`` can be used in the following ways, where the same rules of
syntax apply to all:

-  ``REX`` expressions can be entered as query items, following a
   forward slash ``/``.

-  ``REX`` expressions can be entered in a query following a ``w/``
   designation to dynamically define a special pattern to delimit your
   Metamorph query.

-  ``REX`` expressions can be used as part of functions in a Vortex
   program, in the use of the functions ``rex`` and ``sandr`` (search
   and replace) and the start and end delimiter expressions ``sdexp``
   and ``edexp``.

-  ``REX`` can be used as a stand-alone utility outside of a Texis or
   Metamorph application, to do such things as change file formats with
   Search and Replace, search through excerpted report files to pull out
   specific items of interest, or to create lists of headers.

While this is not intended as a complete list, by way of example, some
of the ranges of characters one can delineate with ``REX's`` syntax
follow.

| ``\alpha``\ xx = Matches any alpha character; ``[A-Z]`` or ``[a-z]``.
  ``\alpha`` Matches any alpha character; ``[A-Z]`` or ``[a-z]``.
| ``\upper`` Matches any upper case alpha character; ``[A-Z]``.
| ``\lower`` Matches any lower case alpha character; ``[a-z]``.
| ``\digit`` Matches any numeric character; ``[0-9]``.
| ``\alnum`` Any alpha or any numeric character; ``[A-Z]`` or ``[a-z]``
  or ``[0-9]``.
| ``\space`` Any space character;
  [space,return,linefeed,tab,formfeed,vertical-tab].
| ``\punct`` Any punctuation; [not control and not space and not
  alphanumeric].
| ``\print`` Any printable character; [all of the above].
| ``\cntrl`` Any control character.
| ``\R`` Respect case.
| ``\I`` Ignore case.
| ``\Xnn`` Matches hexadecimals.

Examples
~~~~~~~~

-  ``"/\alpha+-=\alpha+"``: Looks for one or more occurrences of a
   letter (i.e., any word) followed by one occurrence of a hyphen
   (designated by the equal sign (``=``), followed by one or more
   occurrences of a letter (i.e., a word); and as such, can be used to
   locate hyphenated words.

-  ``"/cost=\space+of=\space+living="``: Looks for the word “``cost``”
   followed by one or more of any space character (i.e., a space or a
   carriage return), followed by the word “``of``”, followed by one or
   more of any space character, followed by the word “``living``”; and
   as such, would locate the phrase “``cost of living``”, regardless of
   how it had been entered or formatted in terms of space characters.

-  ``"/\digit{1,6}\.=\digit{2}"``: Looks for from 1 to 6 digits followed
   by a decimal point. ‘``.``’ is a special character in ``REX`` syntax
   and so must be preceded with a backward slash in order to be taken
   literally), followed by 2 digits; and as such would locate dollar
   type amounts and numbers with a decimal notation of 2 places.

Examples of Some Useful REX Expressions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  To locate phone numbers:

   ::

       1?\space?(?\digit\digit\digit?)?[\-\space]?\digit{3}-\digit{4}

-  To locate social security numbers:

   ::

       \digit{3}-\digit{2}-\digit{4}

-  To locate text between parentheses:

   ::

       (=[^()]+)      <- without direction specification
            or
       >>(=!)+)       <- with direction specification

-  To locate paragraphs delimited by an empty line and 5 spaces:

   ::

       >>\n\n\space{5}=!\n\n\space{5}+\n\n\space{5}

-  To locate numbers in scientific notation; e.g., “``-3.14 e -21``”:

   ::

       [+\-]?\space?\digit+\.?\digit*\space?e?\space?[+\-]?\space?\digit+

You can formulate patterns of things to look for using these types of
patterns. You can look for a ``REX`` expression by itself, or in
proximity to another search item. Such a search could combine a ``REX``
expression in union with an intelligent concept search. For example, you
could enter the following in a query input box:

::

         "/\digit{2}%" measurement

The ``REX`` expression indicates 2 occurrences of any digit followed by
a percent sign. “``Measurement``” will be treated as an English root
word with its list of equivalences, and passed to ``PPM``.

In this search, Metamorph will look for an intersection of both elements
inside the specified delimiters, and may come up with a hit such as:

    They **estimated** that only **65%** of the population showed up to
    vote.

where “``estimated``” was associated with “``measurement``”, and
“``65%``” was associated with the pattern “``\digit{2}%``”.

Misspellings, Typos, and Pattern Approximations (XPM)
-----------------------------------------------------

Searching for Approximations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In any search environment there is always a fine line between relevance
and irrelevance. Any configuration aims to allow just enough abstraction
to find what one is looking for, but not so much that unwanted hits
become distracting. Speed is also an important consideration; one does
not want to look for so many possibilities that the search is overly
burdened and therefore too slow in response time.

If a spelling checker were run into every Metamorph search, not only
would the general search time be greatly impeded, but a lot of what can
be referred to as “noise” would deflect the accuracy, or relevancy of
the search results. The aim of Metamorph is to allow maximum user
control and direction of the search. Since there is no requirement to
conform to any spelling standard, Metamorph is able to accept completely
unknown words and process them accordingly: this includes slang,
acronyms, code, or technical nomenclature. Even so, this does not deal
with the issue of misspellings or typos.

Metamorph thoroughly handles this problem through the use of ``XPM``,
Metamorph’s ApproXimate Pattern Matcher. The intent behind ``XPM`` is
that you haven’t found what you believe you should have found, and are
therefore willing to accept patterns which deviate from your entered
pattern by a specified percentage. The percentage entered on the query
line is the percentage of proximity to the entered pattern (rather than
the percent of deviation).

XPM Rules of Syntax
~~~~~~~~~~~~~~~~~~~

-  The syntax for ``XPM`` is to enter a percent sign (``%``) followed by
   a two digit number from ``01`` to ``99`` representing percentage of
   proximity, followed by the pattern you wish, as: ``%72Raygun``, to
   find “``Reagan``” . If no numbers are listed after the percent sign,
   the default of ``80%`` will be used; as, ``%lithwania``, looks for an
   ``80%`` match and will find “``Lithuania``”.

-  ``XPM`` is not a stand-alone tool; it can only be used from within a
   Metamorph query. It can be used in intersection with other designated
   words, expressions, or ``XPM`` patterns.

-  You can designate a logic operator in front of an ``XPM`` pattern
   ‘``+``’ or ‘``-``’; do not leave a space between the operator and the
   percent sign; as: ``+%72Raygun``. It does not need to be put in
   quotes.

-  ``XPM`` is case insensitive just as are other Metamorph special
   searches.

-  There is no way to search for an approximated ``REX`` expression;
   either ``REX`` or ``XPM`` will be called by Metamorph. Use ``XPM`` in
   front of words or otherwise fixed length patterns not inclusive of
   ``REX`` syntax.

Let us say you are looking for something that happened in Reykjavik last
week, and you are almost certain it is in your file somewhere. There is
such a reference in the demo news file which is shipped with the
program, so you can try this. Perhaps you specified on the query line
“``event Rakechavick``” but got no hit, as you were not spelling the
name correctly. You can enter the same search, but call up ``XPM`` for
the word you don’t know how to spell. For example:

::

         event %64Rakechavick

This query will look for an intersection of the word “``event``” (plus
all its equivalences) and a ``64%`` approximation to the entered pattern
“``Rakechavick``”. This will in fact successfully locate a hit which
discusses “``events``” which occurred at “``Reykjavik``”.

When looking for this sort of thing, you can keep lowering the specified
percentage until you find what you want. You’ll notice that the lower
the specified proximity, the more “noise” you allow; meaning that in
this case you will allow many patterns in addition to “``Reykjavik``”,
as you are telling the program to look for anything at all which
approximates ``64%`` of the entered pattern.

Such a facility has many applications. Probably the most common use for
``XPM`` is when looking for proper nouns that have either unknown or
multiple spellings. “``Qadhafi``” is an example of a name which is in
the news often, and has several different completely accepted spellings.
Someone for whom English is a second language can much more successfully
search for things he cannot spell by calling up ``XPM`` when necessary
with the percent (``%``) sign. And in instances where there are file
imperfections such as human typos or OCR scanning oddities, ``XPM`` will
call up the full range of possibilities, which can be very useful in
catch-all batchmode searching, or otherwise.

Numeric Quantities Entered as Text (NPM)
----------------------------------------

``NPM``, the Numeric Pattern Matcher, is one of several pattern matchers
that can be called by the user in sending out a Metamorph query. It is
signified by a pound sign ‘``#``’ in the starting position, in the same
way that the tilde ‘``~``’ calls ``SPM``, a percent sign ‘``%``’ calls
``XPM``, a forward slash ‘``/``’ calls ``REX``, and no special character
in the first position (where there are equivalences) calls ``PPM`` or
``SPM``.

There are still many numeric patterns that are best located with a
``REX`` expression to match the range of characters desired. However,
when you need the program to interpret your query as a numeric quantity,
use ``NPM``. ``NPM`` does number crunching through all possible numbers
found in the text to locate those numbers which are in the specified
range of desired numbers. Therefore where a lot of numeric searching is
being done you may find that a math co-processor can speed up such
searches.

Since all numbers in the text become items to be checked for numeric
validity, one should tie down the search by specifying one or more other
items along with the ``NPM`` item. For example you might enter on the
query line:

::

         cosmetic sales $ #>1000000

Such a search would locate a sentence like:

    **Income** produced from **lipstick** brought the company
    **$**\ **4,563,000** last year.

In this case “``income``” is located by ``PPM`` as a match to
“``sales``”, “``lipstick``” is located by ``PPM`` as a match to
“``cosmetic``”, the English character “``$``” signifying “``dollars``”
is located by ``SPM`` as a match to “``$``”, and the numeric quantity
represented in the text as “``4,563,000``” is located by ``NPM`` as a
match to “``#>1000000``” (a number greater than one million). Another
example:

::

         cosmetic sales $ #>million

Even though one can locate the same sentence by entering the above
query, it is strongly recommended that searches entered on the query
line are entered as precise numeric quantities. The true intent of
``NPM`` is to make it possible to locate and treat as a numeric value
information in text which was not entered as such.

You would find the above sentence even without specifying the string
“``$``”, but realize that the dollar sign (``$``) in the text is not
part of the numeric quantity located by ``NPM``. There may be cases
where it is important to specify both the quantity and the unit. For
example, if you are looking for quantities of coal, you wouldn’t want to
find coal pricing information by mistake. Compare these two searches:

::

         Query1:  Australia coal tons #>500
         Query2:  Australia coal $ #>500

The first would locate the sentence:

    Petroleum Consolidated mined **1200** **tons** of **coal** in
    **Australia**.

The second would locate the sentence:

    From dividends paid out of the **$**\ **3.5** million profit in the
    **coal** industry, they were able to afford a vacation in
    **Australia**.

Some units, such as kilograms, milliliters, nanoamps, and such, are
understood by ``NPM`` to be their true value; that is, in the first
case, ``1000 grams``. Use ``NPMP`` to find out which units are
understood and how they will be interpreted. The carrot mark (``^``)
shows where the parser stops understanding valid numeric quantities.
Note that an abbreviation such as “``kg``” is not understood as a
quantity, but only a unit; therefore, “``5 kilograms``” has a numeric
quantity of ``5000`` (grams), where “``5 kg``” has a numeric quantity of
``5`` (kg’s).

Beware of entering something that doesn’t make sense. For example, a
quantity cannot be less than 6 and greater than 10 at the same time, and
therefore “``#<6>10``” will make the controlfile sent to the engine
unable to be processed.

Do not enter ambiguity on the query line; ``NPM`` is intended to deal
with ambiguity in the text, not in the query. The safest way to enter
``NPM`` searches is by specifying the accurate numeric quantity desired.
Example:

::

         date #>=1980<=1989

This query will locate lines containing a date specification and a year,
where one wants only those entries from the 1980’s. It would also locate
dates in legal documents which are spelled out. Example:

::

         retirement benefits age #>50<80

This query will locate references about insurance benefits which
reference age 54, 63, and so on. Reflecting the truer intent of ``NPM``,
a sentence like the following could also be retrieved.

    At **fifty-five** one is **awarded** the company’s special
    **Golden** **Age** program.

In the event that a numeric string contains a space, it must be in
quotes to be interpreted correctly. So, although it is strongly not
recommended, one could enter the following:

::

         revenue "#>fifty five"

With this, you can locate references like the following example.

    Their corporate gross **income** was $\ **1.4 million** before they
    merged with Acme Industrial.

Keep in mind that an ``NPM`` Search done within the context of Metamorph
relies upon occurrences of intersections of found items inside the
specified text delimiters, just as any Metamorph search. It is still not
a database tool. The Engine will retrieve any hit which satisfies all
search requirements including those which contain additional numeric
information beyond what was called for.

In an application where Metamorph Hit Markup has been enabled, exactly
what was found will be highlighted. This is the easiest way to get
feedback on what was located to satisfy search requirements. If there
are any questions about results, review basic program theory and compare
to the other types of searches as given elsewhere in this chapter.

Designating Logic Operators (+) and (-)
---------------------------------------

Searches can be weighted by indicating those sets you “must include”
with a plus sign (``+``) and those sets “not to include” with a minus
sign (``-``). Those sets not so marked have the default designation of
an equal sign (``=``), which means all such sets have an equal weight.
The must include (``+``) and must not include (``-``) designations are
outside the intersection quantity count; intersections are calculated
based on the number of intersections of unmarked or equal (``=``) sets
you are looking for.

In Metamorph terms we refer to an equally weighted set (``=``) as “set
logic”; a “must include” set (``+``) as “and logic”; and a “must not
include” set (``-``) as “not logic”. These definitions should not be
confused with Boolean terms, as although the definitions overlap, they
are not identical. Traditional “or” logic can be assigned by using the
“``@0``” designation on the query line, denoting zero intersections of
the unmarked sets.

When a (``+``) or (``-``) set is designated, remember that it applies to
the whole set; not just the word you have marked. Example:

::

         @1 disease blood skin +infection -bandaid

The above query specifying intersections at one (``@1``) means that you
are looking for one intersection (``@1``) of anything from the set of
words associated with “``disease``”, “``blood``”, and “``skin``”; and of
those hits, you only want those containing something from the set of
words associated with “``infection``”; but you would rule the hit out if
it contained anything from the set of words associated with
“``bandaid``”.

You can designate any set entered on the query line as ‘``+``’ or
‘``-``’; therefore this applies as much to wildcard (``*``), ``REX``,
``XPM``, ``NPM`` expressions, and macros, as it does to words. Example:

::

         power struggle -%70Raygun

This finds all references to ``power`` and ``struggle`` (and their
equivalences) but filters out any references to ``70%`` approximation to
the pattern “``Raygun``” (i.e., it would omit references to hits
containing the word “``Reagan``”).

The important rule to remember about assigning ‘``+``’ or ‘``-``’
operators is that you cannot look for only minuses (``-``).

This chapter has attempted to cover the types of items which comprise a
Metamorph query. Logic operators can be used to add special weighting to
any of those things which will be viewed as single sets. Therefore you
can assign a ‘``+``’ or ‘``-``’ to any of the types of query items that
are described herein; and realize that with no other such marking, any
search item is understood to be given an equal ‘``=``’ weighting.

.. [1]
   Both of these actions are subject to enabling by the ``alequivs``
   setting in Vortex/\ ``tsql``.

.. [2]
   In the Metamorph API (unlike Vortex, ``tsql`` and the Search
   Appliance) equivalences are on by default, so the following actions
   would be the opposite of what is described.
