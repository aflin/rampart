
Intelligent Text Search Queries
-------------------------------

[Chp:MMLike]

This manual has concentrated so far on the manipulation of fields within
a relational database. As a text information server Texis provides all
the capabilities one would expect from a traditional RDBMS.

Texis was also designed to incorporate large quantities of narrative
full text stored within the database. This is evidenced by its data
types as presented in Chapter [chp:TabDef], *Table Definition*. When
textual content becomes the focus of the search, the emphasis shifts
from one of document management to one of research. Texis has embodied
within it special programs geared to accomplish this.

Metamorph was originally a stand-alone program designed to meet
intelligent full text retrieval needs on full text files. Since 1986 it
has been used in a variety of environments where concept oriented text
searching is desired. Now within the ``LIKE`` clause, all of what is
possible on full text information with Metamorph is possible with Texis,
within the framework of a relational database.

Metamorph is covered in a complete sense in its own section in this
manual and can be studied of itself. Please refer to the *Metamorph
Intelligent Text Query Language* sections for a full understanding of
all Metamorph’s theory and application.

This chapter deals with the use of Metamorph within the ``LIKE`` portion
of the ``WHERE`` clause. Texis can accomplish any Metamorph search
through the construction of a ``SELECT``-``FROM``-``WHERE`` block, where
the Metamorph query is enclosed in single quotes ``'query'`` following
``LIKE``.


Types of Text Query
~~~~~~~~~~~~~~~~~~~

There are two primary types of text query that can be performed by
Texis. The first form is used to find those records which match the
query, and is used when you only care if the record does or does not
match. The second form is used when you want to rank the results and
produce the best answers first.

The first type of query is done with ``LIKE``, or ``LIKE3`` to avoid
post processing if the index can not fully resolve the query. The
ranking queries are done with ``LIKEP`` or ``LIKER``. ``LIKER`` is a
faster, and less precise ranking figure than the one returned by
``LIKEP``. The ranking takes into account how many of the query terms
are present, as well as their weight, and for ``LIKEP``, how close
together the words are, and where in the document they occur. Most
queries will use ``LIKE`` of ``LIKEP``, with ``LIKE3`` and ``LIKER``
used in special circumstances when you want to avoid the post-processing
that would fully resolve the query.

There are also two forms of Metamorph index, the Metamorph inverted
index and the Metamorph index. The inverted form contains additional
information which allows phrases to be resolved and ``LIKEP`` rankings
to be calculated entirely using the index. This improved functionality
comes at a cost in terms of space.

If your queries are single word ``LIKE`` queries then the Metamorph
index has all the information needed, so the additional overhead of the
inverted index is not needed.


Definition of Metamorph Terms
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Query:
    A Metamorph Query is the question or statement of search items to be
    matched in the text within specified beginning and ending
    delimiters. A Query is comprised of one or more search items which
    can be of different types, and a “within delimiter” specification
    which is either understood or expressed. In Texis the Metamorph
    Query refers to what is contained within single quotes ``'query'``
    following ``LIKE`` in the ``WHERE`` clause.

Hit:
    A Hit is the text Metamorph retrieves in response to a query, whose
    meaning matches the Query to the degree specified.

Search Item:
    A Search Item is an English word or a special expression inside a
    Metamorph Query. A word is automatically processed using certain
    linguistic rules. Special searches are signaled with a special
    character leading the item, and are governed respectively by the
    rules of the pattern matcher invoked.

Set:
    A Set is the group of possible strings a pattern matcher will look
    for, as specified by the Search Item. A Set can be a list of words
    and word forms, a range of characters or quantities, or some other
    class of possible matches based on which pattern matcher Metamorph
    uses to process that item.

Intersection:
    A portion of text where at least one member of two Sets each is
    matched.

Delimiters:
    Delimiters are repeating patterns in the text which define the
    bounds within which search items are found in proximity to each
    other. These patterns are specified as regular expressions. A within
    operator is used to specify delimiters in a Metamorph Query.

Intersection Quantity:
    The number of unions of sets existing within the specified
    Delimiters. The maximum number of Intersections possible for any
    given Query is the maximum number of designated Sets minus one.

Hits can have varying degrees of relevance based on the number of set
intersections occurring within the delimited block of text, definition
of proximity bounds, and weighting of search items for inclusion or
exclusion.

Intersection quantity, Delimiter bounds, and Logic weighting can be
adjusted by the user as part of Metamorph Query specification.


Adjusting Linguistic Controls
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Concept sets can be edited to include special vocabulary, acronyms, and
slang. There is sufficient vocabulary intelligence off the shelf so that
editing is not required to make good use of the program immediately upon
installation. However, such customization is encouraged to keep online
research in rapport with users’ needs, especially as search routines and
vocabulary evolve.

A word need not be “known” by Metamorph for it to be processed. The fact
of a word having associations stored in the Thesaurus makes abstraction
of concept possible, but is not required to match word forms. Such word
stemming knowledge is inherent. And, any string of characters can be
matched exactly as entered.

You can edit the special word lists Metamorph uses to process English if
you wish. As it may not be immediately apparent to what degree these
word lists may affect general searching, it is cautioned that such
editing be used sparingly and with the wisdom of experience. Even so,
what Metamorph deems to be Noise, Prefixes, and Suffixes is all under
user control.

See the Metamorph portion of this manual for a complete explanation of
all these terms and other background information.


Constructing a Metamorph Query
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following types of searches are all possible within a Metamorph
Query, as contained in single quotes following the ``LIKE`` portion of
the ``WHERE`` clause, in any ``SELECT``-``FROM``-``WHERE`` block.


Keyword Search
""""""""""""""

Your search can be as simple as a single word or string. If you want
references to do with dogs, type in the word “dog”.

**Example:** Let’s say Corporate Legal Support maintains a table called
CODES which includes full text of the local ordinances of the town in
which Acme Industrial has its headquarters. The full text field of each
ordinance is stored in a column called BODY.

To find ordinances containing references to dogs, the ``WHERE`` clause
takes this form:

::

         WHERE column-name LIKE 'metamorph-query'

You can put any Metamorph query in the quotes (``'metamorph-query'``)
although you would need to escape a literal ``'`` with another ``'`` by
typing ``''``, if you want the character ``'`` to be part of the query.

Using Metamorph’s defaults, a sentence in the body of the ordinance text
will be sought which contains a match to the query. Whatever is dictated
in the ``SELECT`` portion of the statement is what will be displayed.
All outer logic applies, so that multiple queries can be sought through
use of AND, OR, NOT, and so on. See Chapter [chp:Quer], especially the
section, *Additional Comparison Operators*, for a complete understanding
of how the ``LIKE`` clause fits into a ``SELECT``-``FROM``-``WHERE``
statement.

In this example, the ``WHERE`` clause would look like this:

::

         WHERE BODY LIKE 'dog'

When Texis executes the search, ordinances whose bodies contain matching
sentences would be retrieved. An example of a qualifying sentence would
be:

::

         DOG:  any member of the canine family.

And this sentence:

::

         It shall be unlawful and a nuisance for any DOG owner to
         permit a dog to be free of restraint in the city.

An English word entered in a Metamorph Query retrieves occurrences of
forms of that word in both lower and upper case, regardless of how it
was entered; i.e., the default keyword search is case insensitive.

Each matched sentence is called a *HIT*. Metamorph locates all such
*hits* containing “dog” and any other “dog” word forms adhering to the
linguistic settings in place. There would normally be quite a few hits
for a common keyword query like this.


Refining a Query
""""""""""""""""

To refine a query, thereby further qualifying what is judged a hit, add
any other keywords or concepts which should appear within the same
concept grouping.

**Example:**

::

         WHERE BODY LIKE 'dog fine'

Fewer hits will be retrieved than when only one search item is entered
(i.e., “dog”), as you are requiring both “dog” and “fine” to occur in
the same sentence. This sentence would qualify:

::

         The owner of any DOG who permits such a dog to be free of
         restraint in violation of Section 4.2 of this article shall
         pay a FINE of not less than twenty-five dollars.

You may enter as many query items as you wish, to qualify the hits to be
found.

**Example:**

::

         WHERE BODY LIKE 'dog owner vaccination city'

Such a query locates this sentence:

::

         Every veterinarian who VACCINATES any cat or DOG within the
         CITY limits shall issue a certification of vaccination to
         such OWNER.

You needn’t sift through references which seem too broad or too
numerous. Refine your query so it produces only what you judge to be
relevant to the goal of your search.


Adjusting Proximity Range by Specifying Delimiters
""""""""""""""""""""""""""""""""""""""""""""""""""

By default Texis considers the entire field to be a hit when the full
text is retrieved.

If you want your search items to occur within a more tightly constrained
proximity range this can be adjusted. If you are using Vortex you will
need to allow within operators which are disabled by default due to the
extra processing required.

Add a “within” operator to your query syntax; “``w/line``” indicates a
line; “``w/para``” indicates a paragraph; “``w/sent``” indicates a
sentence; “``w/all``” incdicates the entire field; “``w/#``” indicates
``#`` characters. The default proximity is “``w/all``”.

**Example:** Using the legal ordinance text, we are searching the full
text bodies of those ordinances for controls issued about dogs. The
following query uses sentence proximity to qualify its hits.

::

         WHERE BODY LIKE 'dog control w/sent'

This sentence qualifies as a hit because “control” and “dogs” are in the
same sentence.

::

         Ordinances provide that the animal CONTROL officer takes
         possession of DOGS which are free of restraint.

Add a within operator to the Metamorph query to indicate both stated
search items must occur within a single line of text, rather than within
a sentence.

::

         WHERE BODY LIKE 'dog control w/line'

The retrieved concept group has changed from a sentence to a line, so
“dog” and “control” must occur in closer proximity to each other. Now
the line, rather than the sentence, is the hit.

::

         CONTROL officer takes possession of DOGS

Expanding the proximity range to a paragraph broadens the allowed
distance between located search words.

::

         WHERE BODY LIKE 'dog control w/para'

The same query with a different “within” operator now locates this whole
paragraph as the hit:

::

         The mayor, subject to the approval of the city council,
         shall appoint an animal CONTROL officer who is qualified to
         perform the duties of an animal control officer under the
         laws of this state and the ordinances of the city.  This
         officer shall take possession of any DOG which is free of
         restraint in the city.

The words “control” and “dog” span different lines and different
sentences, but are within the same paragraph.

These “within” operators for designating proximity are also referred to
as delimiters. Any delimiter can be designed by creating a regular
expression using REX syntax which follows the “``w/``”. Anything
following “``w/``” that is not one of the previously defined special
delimiters is assumed to be a REX expression. For example:

::

         WHERE BODY LIKE 'dog control w/\RSECTION'

What follows the ‘``w/``’ now is a user designed REX expression for
sections. This would work on text which contained capitalized headers
leading with “``SECTION``” at the beginning of each such section of
text.

Delimiters can also be expressed as a number of characters forward and
backwards from the located search items. For example:

::

         WHERE BODY LIKE 'dog control w/500'

In this example “dog” and “control” must occur within a window of 500
characters forwards and backwards from the first item located.

More often than not the beginning and ending delimiters are the same.
Therefore if you do not specify an ending delimiter (as in the above
example), it will be assumed that the one specified is to be used for
both. If two expressions are specified, the first will be beginning, the
second will be ending. Specifying both would be required most frequently
where special types of messages or sections are used which follow a
prescribed format.

Another factor to consider is whether you want the expression defining
the text unit to be included inside that text unit or not. For example,
the ending delimiter for a sentence obviously belongs with the hit.
However, the beginning delimiter is really the end of the last sentence,
and therefore should be excluded.

Inclusion or exclusion of beginning and ending delimiters with the hit
has been thought out for the defaults provided with the program.
However, if you are designing your own beginning and ending expressions,
you may wish to specify this.


Delimiter Syntax Summary
""""""""""""""""""""""""

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
"""""""""""""""""""""""""

-  The above can be anywhere in a Metamorph query, and is interpreted as
   “within {the following delimiters}”.

-  Accepted built-in abbreviations following the slash ‘``/``’ are:

   [tab:within]

   | ``[^\digit\upper][.?!][\space'"]``\ xxx = Meaning Abbreviation
     Meaning
   | ``line`` within a line
   | ``sent`` within a sentence
   | ``para`` within a paragraph
   | ``page`` within a page
   | ``all`` within a field
   | ``NUMBER`` within NUMBER characters

   | ``[^\digit\upper][.?!][\space'"]``\ xxx = Meaning REX Expression
     Meaning
   | ``$`` 1 new line
   | ``[^\digit\upper][.?!][\space'"]`` not a digit or upper case
     letter, then
   | a period, question, or exclamation point, then
   | any space character, single or double quote
   | ``\x0a=\space+`` a new line + some space
   | ``\x0c`` form feed for printer output

-  A number following a slash ‘``/``’ means the number of characters
   before and after the first search item found. Therefore “``w/250``”
   means “within a proximity of 250 characters”. When the first
   occurrence of a valid search item is found, a window of 250
   characters in either direction will be used to determine if it is a
   valid hit. The implied REX expression is: “``.{,250}``” meaning “250
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
   delimiter, and include ending delimiter, or vice versa. Note that for
   within-\ :math:`N` queries (e.g. “``w/250``”), the “delimiter” is
   effectively always included in the hit, regardless of the case of the
   ``w``.

-  If the same expression is to be used, the expression need not be
   repeated. Example: “``w/[.?!] W/``” means to use an ending
   punctuation character as both beginning and end delimiter, but to
   exclude the beginning delimiter from the hit, and include the end
   delimiter in the hit.


Using Set Logic to Weight Search Items
""""""""""""""""""""""""""""""""""""""


Set Logic and Intersections Defined
"""""""""""""""""""""""""""""""""""

Any search item entered in a query can be weighted for determination as
to what qualifies as a hit.

All search items indicate to the program a set of possibilities to be
found. A keyword is a set of valid derivations of that word’s root
(morpheme). A concept set includes a list of equivalent meaning words. A
special expression includes a range of strings that could be matched.

Therefore, whatever weighting applies to a search item applies to the
whole set, and is referred to as “set logic”.

The most usual logic in use is “AND” logic. Where no other weighting is
given, it is understood that all entered search items have equal weight,
and you want each one to occur in the targeted hit.

Here is an example of a typical query, where no special weighting has
been assigned:

::

         WHERE BODY LIKE 'mayor powers duties city'

The query equally weights each item, and searches for a sentence
containing “mayor” and “powers” and “duties” and “city” anywhere within
it, finding this sentence:

::

         In the case of absence from the CITY or the failure,
         inability or refusal of both the MAYOR and mayor pro tempore
         to perform the DUTIES of mayor, the city council may elect
         an acting mayor pro tempore, who shall serve as mayor with
         all the POWERS, privileges, and duties.

Only those words required to qualify the sentence as a hit are located
by the program, for maximum search efficiency.

In this example, there are several occurrences of the search items
“mayor”, “duties”, and “city”. It was only necessary to locate each item
once to confirm validity of the hit. Such words may be found by the
search program in any order.

The existence of more than one matched search item in a hit is called an
intersection. Specifying two keywords in a query indicates you want both
keywords to occur, or intersect, in the sentence.

A 2 item search is common, and can be thought of as 1 intersection of 2
sets.

**Example:**

::

         WHERE BODY LIKE '~alcohol ~consumption'

In the above example, the tilde (``~``) preceding “alcohol” and
preceding “consumption” enables concept expansion on both words, thereby
including the set of associations listed for each word in the Thesaurus.

Where something from the concept set “alcohol” and something from the
concept set “consumption” meet within a sentence, there is a hit. This
default set logic finds a 1 intersection sentence:

::

         It shall be unlawful to USE the city swimming pool or enter
         the enclosure in which it is located when a person is
         INTOXICATED or under the influence of illegal drugs.

“Use” is in the “consumption” concept set; “intoxicated” is in the
“alcohol” concept set.

These two sets have herein intersected, forcing the context of the set
members to be relevant to the entered query.


Maximum Intersections Possible (“AND”)
""""""""""""""""""""""""""""""""""""""

Adding a search item dictates stricter relevance requirements. Here, a
sentence has to contain 2 intersections of 3 search items to be deemed a
valid hit.

**Example:**

::

         WHERE BODY LIKE '~alcohol ~sweets ~consumption'

Such a 2 intersection search finds this hit:

::

         any public sleeping or EATING place, or any place or vehicle
         where food or DRINK is manufactured, prepared, stored, or
         any manufacturer or vendor of CANDIES or manufactured
         sweets.

Default intersection logic is to find the maximum number of set
intersections possible in the stated query; that is, an “and” search
where an intersection of all search items is required.


Specifying Fewer Intersections
""""""""""""""""""""""""""""""

The casual user can use the “AND” logic default. Even so, here is
another way to write the above 2 intersection query, where the number of
desired intersections (2) is preceded by the at sign (``@``):

**Example:**

::

         WHERE BODY LIKE '~alcohol ~sweets ~consumption @2'

The “``@2``” designation is redundant as it is understood by the program
to be the default maximum number of intersections possible, but it would
yield the same results.

It is possible to find different permutations of which items must occur
inside a hit. Even where the maximum number of intersections possible is
being sought, this is still seen as a permutation, and is referred to as
*permuted* logic.

The meaning of “*permuted*” takes on more significance when fewer
intersections of items are desired.

If you wanted only one intersection of these three items, it would
create an interesting range of possibilities. You might find an
intersection of any of the following combinations:

::

         alcohol (AND) sweets
         alcohol (AND) consumption
         sweets  (AND) consumption

Specify one intersection only (``@1``), while listing the 3 possible
items.

**Example:**

::

         WHERE BODY LIKE '~alcohol ~sweets ~consumption @1'

This 1 intersection search finds the following, where any 2 occurrences
from the 3 specified sets occur within the hit. Hits for a higher
intersection number (``@2``) as shown above also appear.

::

      ~consumption (and) ~alcohol @1
         It shall be unlawful to USE the city swimming pool or enter
         the enclosure in which it is located when a person is
         INTOXICATED or under the influence of illegal drugs.

      ~consumption (and) ~sweets @1
         any public sleeping or EATING place, or any place or vehicle
         where food or drink is manufactured, prepared, stored, or
         any manufacturer or vendor of CANDIES or manufactured
         sweets.

      ~sweets (and) ~alcohol @1
         subject to inspection are:  Bakery or CONFECTIONERY shop
         (retail), Beverage sale ALCOHOLIC ...

      ~consumption (and) ~alcohol @1
         A new EATING and DRINKING establishment shall be one which
         is newly erected or constructed at a given location.

      ~alcohol (and) ~consumption @1
         involving the sale of spirituous, vinous, or malt LIQUORS,
         including beer in unbroken packages for off-premises
         CONSUMPTION.

The “``@#``” intersection quantity designation is not position
dependent; it can be entered anywhere in the Metamorph query.

Any number of intersections may be specified, provided that number does
not exceed the number of intersections possible for the entered number
of search items.


Specifying No Intersections (“OR”)
""""""""""""""""""""""""""""""""""

Using this intersection quantity model, what is commonly understood to
be an “OR” search is any search which requires no (zero) intersections
at all. In an “or” search, any occurrence of any item listed qualifies
as a hit; the item need not intersect with any other item.

Designate an “or” search using the same intersection quantity syntax,
where zero (``0``) indicates no intersections are required (``@0``):

**Example:**

::

         WHERE BODY LIKE '~alcohol ~sweets ~consumption @0'

In addition to the hits listed above for a higher number of
intersections, the following 0 intersection hits would be found, due to
the presence of only one item (a or b or c) required:

::

      ~alcohol @0
         Every person licensed to sell LIQUOR, wine or beer or mixed
         beverages in the city under the Alcoholic Beverage Code
         shall ...

      ~sweets @0
         An establishment preparing and selling at retail on the
         premises, cakes, pastry, CANDIES, breads and similar food
         items.

      ~consumption @0
         To regulate the disposal and prohibit the BURNING of garbage
         and trash; ...

All such items are considered *permuted*, at intersection number zero
(0).


Weighting Items for Precedence (+)
""""""""""""""""""""""""""""""""""

Intersection logic treats all search items as equal to each other,
regardless of the number of understood or specified intersections. You
can indicate a precedence for a particular search item which falls
outside the intersection quantity setting.

A common example is where you are interested chiefly in one subject, but
you want to see occurrences of that subject in proximity to one or more
of several specified choices. This would be an “or” search in
conjunction with one item marked for precedence. You definitely want A,
along with either B, or C, or D.

Use the plus sign (``+``) to mark search items for mandatory inclusion.
Use ``@0`` to signify no intersections are required of the unmarked
permuted items. The number of intersections required as specified by
‘``@#``’ will apply to those permuted items remaining.

**Example:**

::

         WHERE BODY LIKE '+license plumbing alcohol taxes @0'

This search requires (``+``) the occurrence of “license”, which must be
found in the same sentence with either “plumbing”, “alcohol”, or
“taxes”.

The 0 intersection designation applies only to the unmarked permuted
sets. Since “license” is weighted with a plus (``+``), the “``@0``”
designation applies to the other search items only.

This query finds the following hits:

::

      +license (and) @0 alcohol
         Every person licensed to sell liquor, wine or beer or mixed
         beverages in the city under the ALCOHOLIC Beverage Code
         shall pay to the city a LICENSE fee equal to the maximum
         allowed as provided for in the Alcoholic Beverage Code.

      +license (and) @0 plumbing
         Before any person, firm or corporation shall engage in the
         PLUMBING business within the city, he shall be qualified as
         set forth herein, and a LICENSE shall be obtained from the
         State Board of Plumbing Examiners as required.

      +license (and) @0 taxes
         The city may assess, levy and collect any and all character
         of TAXES for general and special purposes on all subjects or
         objects, including occupation taxes, LICENSE taxes and
         excise taxes.

More than one search item may be marked with a plus (``+``) for
inclusion, and any valid intersection quantity (``@#``) may be used to
refer to the other unmarked items. Any search item, including phrases
and special expressions, may be weighted for precedence in this fashion.


Marking Items for Exclusion (“NOT”) (-)
"""""""""""""""""""""""""""""""""""""""

You can exclude a hit due to the presence of one or more search items.
Such mandatory exclusion logic for a particular search item falls
outside the intersection quantity setting, as does inclusion, and
applies to the whole set in the same manner. This is sometimes thought
of as “NOT” logic, designated with a minus sign (``-``).

A common example is where one item is very frequently used in the text,
so you wish to rule out any hits where it occurs. You want an
intersection of A and B, but not if C is present.

Use the minus sign (``-``) to mark search items for exclusion. Default
or specified intersection quantities apply to items not marked with a
plus (``+``) or minus (``-``). The number of intersections required will
apply to the remaining permuted items.

**Example:**

::

         WHERE BODY LIKE 'license ~alcohol -drink'

This search has the goal of finding licensing issues surrounding
alcohol. However, the presence of the word “drink” might incorrectly
produce references about restaurants that do not serve alcohol.

Excluding the hit if it contains “drink” retrieves these hits:

::

     license (and) ~alcohol -drink
         Every person licensed to sell LIQUOR, wine or beer or mixed
         beverages in the city shall pay to the city a LICENSE fee
         equal to the maximum allowed ...

     license (and) ~alcohol -drink
         State law reference(s)--Authority to levy and collect
         LICENSE fee, V.T.C.A., ALCOHOLIC Beverage Code 11.38, 61.36.

But excludes this hit:

::

     license (and) ~alcohol -drink  {Excluded Hit}
         The city council shall have the power to regulate, LICENSE
         and inspect persons, firms, or associations operating,
         managing, or conducting any place where food or DRINK is
         manufactured, prepared, or otherwise handled within city
         limits.

More than one search item may be marked with a minus (``-``) for
exclusion, along with items marked with plus (``+``) for inclusion, and
any valid intersection quantity (``@#``) specification. Any search item,
including phrases and special expressions, may be marked for exclusion
in this fashion.


Combinatorial Logic
"""""""""""""""""""

Weighting search items for inclusion (``+``) or exclusion (``-``) along
with an intersection specification which is less than the maximum
quantity possible (the default “AND” search) can be used in any
combination.

Default logic is adequate for the casual user to get very satisfactory
search results with no special markings, so this extra syntax need not
be learned. However, such syntax is available to the more exacting user
if desired.

A rather complicated but precise query might make use of weighting for
inclusion, exclusion, and also a specified intersection quantity, as
follows.

**Example:**

::

         WHERE BODY LIKE '+officer @1 law power pay duties -mayor'

The above query makes these requirements:

-  “Officer” must be present, plus …

-  1 intersection of any 2 of the unmarked search items “law”, “power”,
   “pay”, “duties”, but …

-  Not if “mayor” is present (i.e., exclude it).

This query retrieves the following hits, while excluding hits containing
“mayor”.

::

      power, duties +officer (but not) -mayor
         The city council shall have POWER from time to time to
         require other further DUTIES of all OFFICERS whose duties
         are herein prescribed.

      law, duties +officer (but not) -mayor
         Proof shall be made before some OFFICER authorized by the
         LAW to administer oaths, and filed with the person
         performing the DUTIES of city secretary.

      law, power +officer (but not) -mayor
         In case of any irreconcilable conflict between the
         provisions of this Charter and any superior LAW, the POWERS
         of the city and its OFFICERS shall be defined in such
         superior laws.

      duties, law +officer (but not) -mayor
         The plan must be designed to enable the records management
         OFFICER to carry out his or her DUTIES prescribed by state
         LAW and this article effectively.

      pay, duties +officer (but not) -mayor
         PAYMENT of firefighting OFFICIAL performing DUTIES outside
         of territorial limits of city.

      power, duties +officer (but not) -mayor
         POWERS and DUTIES of OFFICIAL assigned to assist in the
         city.

Any search item, including keywords, wildcards, concept searches,
phrases, and special expressions, can be weighted for inclusion,
exclusion, and combinatorial set logic.


Combinatorial Logic and LIKER
"""""""""""""""""""""""""""""

When using ``LIKER`` the default weighting of the terms has two factors.
The first is based on the words location in the query, with the most
important term first. The second is based on word frequency in the
document set, where common words have a lesser importance attached to
them. The logic operators (+ and -) remove the second factor.
Additionally the not operator (-) negates the first factor.


Metamorph Logic Rules Summary
"""""""""""""""""""""""""""""

Logic operators apply to any entered search item.

#. Precedence (mandatory inclusion) is expressed by a plus sign (``+``)
   preceding the concept or expression. A plus (``+``) item is
   “Required”.

#. Exclusion (“not” logic) is expressed by a minus sign (``-``)
   preceding the concept or expression. A minus (``-``) item is
   “Excluded”.

#. Equivalence (“and” logic) may be expressed by an equal sign (``=``)
   preceding the concept or expression. An equal sign is assumed where
   no other logic operator is assigned. An equal (``=``) item is
   “Permuted”.

#. Search items not marked with (``+``) or (``-``) are considered to be
   equally weighted. Intersection quantity logic (``@#``) applies to
   these unmarked (``=``) permuted sets only.

#. Where search items are not otherwise marked, the default set logic in
   use is “And” logic. The maximum number of intersections possible is
   sought.

#. Designate “Or” logic with zero intersections (``@0``), applying to
   any unmarked permuted search items.

#. Logic operators and intersection quantity settings can be used in
   combination with each other, referred to as combinatorial logic.

Metamorph’s use of logic should not be confused with Boolean operators.
Metamorph deals with these logic operators as sets rather than single
strings, a different methodology.


Other Metamorph Features
~~~~~~~~~~~~~~~~~~~~~~~~

Metamorph contains many special kinds of searches. Again, any Metamorph
search can be constructed as part of the ``LIKE`` clause. See the
Metamorph section of this manual for a complete treatment of this
subject.
