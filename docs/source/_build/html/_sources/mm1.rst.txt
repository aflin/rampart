Metamorph: The Program Inside the Program
=========================================

Background
----------

Deep inside of Texis stands Metamorph, the original incarnation of
Thunderstone’s text retrieval methodology. While Metamorph has grown
into Texis, its intelligent text query language has remained intact. It
carries with it a whole philosophy of concept based text retrieval which
sets a layer of assumptions about how text, particularly English, should
be processed to retain its meaning.

Metamorph is made reference to in many places throughout Thunderstone’s
program documentation. While originally it was a stand-alone program, it
lives on as functions which are used at all levels of the search,
chiefly thought about only in relation to the formulation of queries
themselves, and attendant processing of the vocabulary base of the
language itself.

Most of what you need to know about Metamorph is included at the
appropriate places in our documentation. This section is devoted to some
of the aspects of Metamorph which were documented early on in the
Research & Development of our product line, but may still be of interest
for context rich applications or just to the curious user.

Original Design Mandate
-----------------------

Based on their past experience, many people have become accustomed to
certain fixed ways of viewing the information dilemma; i.e., how to deal
with too much available stored information. However, most of these
accepted solutions contain inherent flaws which can be readily seen and
solved once one realizes that there are choices now which we did not
have not too long ago.

Thunderstone took a fresh approach to text analysis and data search
problems from the very core; that is, in the methods of pattern matching
itself. It has always been our premise that if we could develop pattern
matching algorithms fast and precise enough, we could take a different
approach to data research which would provide more accurate and reliable
results, while being less labor intensive in the processing of the raw
data.

Different algorithms are most efficient for different tasks; therefore
we have learned how to make use of the best tool for the best job by
integrating a number of smaller programs through a senior program. The
Metamorph Search Engine calls upon the right pattern matcher for the
right job, encompassing several different pattern matchers: PPM
(Parallel Pattern Matcher), SPM (Single Pattern Matcher), Wildcard ’\*’
matcher, REX (Regular EXpression pattern matcher), XPM (ApproXimate
Pattern Matcher), and NPM (Numeric Pattern Matcher). Each pattern
matcher handles a certain class of search problems and is optimized for
a particular type of task, making the overall search environment as fast
and efficient as possible.

In light of the rapid advances being made in hardware configurations, we
have maintained that the best approach to the creation of all data
manipulation tools must be totally software solutions. In this way we
can as required modify our software to take advantage of new hardware
breakthroughs, rather than be tied to outdated hardware systems. We
believe this represents a more cost effective solution to our clients so
that only their hardware budget concerns need be weighty, knowing our
software will be flexible enough to be portable to the hardware
configuration of their choice.

By way of example, we are in fact operational on more than 25 different
Unix platforms, evidencing more portability than any other software
company around. Using our Application Program Interfaces (API’s), we can
extend portability of the Search Engine to almost any platform. And now
that Metamorph is inside Texis, a complete and multi-faceted Relational
DataBase Management System, its use can be extended truly to any
application that might be desired, now in this exciting global village
of totally connected Internet and World Wide Web applications.

Some hardware pattern matching solutions have surfaced along the way,
some quite impressive. However, there are certain inherent problems with
these techniques no matter how fast they appear to be. What can be
located is limited, and there is limited portability. If the host
configuration is greatly changed, an entirely new piece of hardware will
in most cases be required. Metamorph on the other hand, being entirely
software, always has retained the potential of being moved to a new
hardware environment. Our technology has shown flexibility over time in
DOS, Unix, MVS, OS/2, MACH, Macintosh, Windows, and NT environments,
spanning micro, mini and mainframe applications.

Tokenization and Inverted Files
-------------------------------

Two leading techniques in text retrieval that have heretofore been used
have been file inversion and tokenization. The dominant problem with
both of these techniques is that they require modification of the
original data to be searched in order for it to be accessible to the
data retrieval tool. The second problem, which has deeper ramifications,
is that in order to perform file inversion or tokenization such programs
make certain predisposed determinations about the lexical items that
will be later identified.

How good such programs are will depend in great part upon their ability
to identify and then locate specified lexical items. In most cases the
set of lexical items identified by the inversion or tokenization
routines is simply insufficient to guarantee the retrieval of all things
that one might want to search for. But even where the set of
identifiable lexical items is reasonably good, one always has a certain
basic limitation to contend with: one will never be able to locate a
superset of the lexical item listed in the look up table.

It is for these reasons that when we make use of indexing techniques, we
supplement them with a final linear text read where required. Many
content oriented definitive searches must contain a linear read of
context to make accurate determinations involving relevance.

In systems where a lookup table exists containing either file pointers
or tokens, context is missing. You cannot search for something next to
something else, as no adequate record of related locations of items is
contained in the lookup table. You may yet find what you are looking
for, but you may have to convert the file to its original form before
you can do so.

It will be hard to find a program which stores any, let alone all,
possible combinations of words making up phrases, idiomatic expressions,
and acronyms in the lookup table. While you can look for “Ming”, or you
can look for “Ling”, you cannot directly look for “Ming-Ling”. Another
tricky category is that of regular expressions involving combinations of
lexical items. If you are searching the Bible by verse, you want to find
a pattern of “digit, digit, colon, digit”. This cannot be done when
occurrences of digits are stored separately from the occurrences of
colons.

Our own database tool Texis has a modifiable lexical analyzer, which
goes further than other indexing programs to attend to this problem.
However, a linear free text scan still gives the maximum flexibility for
looking for any type of pattern in relation to another pattern, and
therefore has been included in Texis as part of the search technique
used to qualify hits.

Metamorph is the search engine inside of Texis which contains maximum
capability for identification of a diverse variety of lexical items. No
other program has such an extended capability to recognize these items.
We can look for special expressions by themselves or in proximity to
sets of concepts. Logical set operators ’and’, ’or’, and ’not’ are
applied to whole sets of lexical items, rather than just single,
specified lexical items. Because Metamorph is so fast, benchmarked even
in its very early years of development as searching up to 4.5 megabytes
per second in some Unix environments, we can read text live where
required and get extremely impressive results.

Where stream text is involved, such as for a message analysis system
where large amounts of information are coming in at a steady rate, or a
news alert profiling system, you could not practically speaking tokenize
all the data as it was coming in before reading it to find out if there
was anything worth attending to in that data. Using a tokenized system
to search incoming messages at a first pass would be very unwieldy, as
well as inefficient and lacking in discretion. Indexes are more
appropriately useful when searching large amounts of archived data.
Texis makes use of Metamorph search engines internally where required to
read the stream text in all of its rich context but without losing speed
of efficiency.

Where Texis sends out a Metamorph query, it is fast and thorough in its
search and its retrieval. A parser picks out phrases without having to
mark them as such, along with any known associations. Search questions
are automatically expanded to sets of concepts without you having to
tell it what those sets are, or having to knowledge engineer connections
in the files. Regular expressions can be located which other systems
might miss. Numeric quantities entered as text and misspellings and
typos can be found. Intersections of all the permutations of the defined
sets can be located, within a defined unit of text defined by the user.
No other search program is capable of this.

Even were you to find a comparable Database Management System with which
to manage your text (which we challenge you to do!), at the bottom line,
you could not find all the specific things that the Metamorph search
engine would let you find. In Texis we now have a completely robust
body; inside, it yet retains the heart and soul of Metamorph.

Metamorph Query Language Highlights
-----------------------------------

Metamorph allows you to search for intersections of sets of lexical
items, while also performing prefix and suffix morpheme processing. Once
your target is found the question arises: what rules govern proximity of
the items you wish to find? In traditional searching tools, this has
been done only on a line by line basis, or by using some quantitative
proximity range. Metamorph can search by an intelligent textual unit, a
sentence. Whether searching by paragraph, page, chart entry, or memo, in
all respects it is intended that the user may define real qualitative
units of communication inside of which the concepts he is interested in
connecting are located.

The user can specify right within his or her query the delimiters of
choice: i.e., he can look within a sentence, paragraph, a proximity of
500 characters, or a specially defined textual unit such as a memo. To
the degree that lexical items can be defined and located as beginning
and end delimiters, your intersections will be located within those
parameters.

REX, Metamorph’s Regular EXpression pattern matcher, can be used outside
Texis as a special text processing tool. REX can locate uniquely
repeated patterns in files, such as headers, footers, captions, diagram
references, and so on. If the existing patterns aren’t adequate to your
needs, you can put them into your files rather easily. For example,
using REX’s incrementing counter and its search and replace facility,
one could locate paragraph starts and number them. Such pattern
identification can be made use of by other applications.

Metamorph allows for editing word sets, by hand or using the Backref
program. This means that you may select which associations you would
like in connection to any search; you can create your own concept sets
permanently for future use. You can fine tune the search to use
associations of only a certain part of speech. You can enter all known
spelling variations of any particular search word in the same way. You
can generally customize the program to include your own nomenclature and
vocabulary, making it increasingly intelligent the longer it is in use.

You can call up the ApproXimate Pattern Matcher (XPM) and tell it to
look for a certain percentage of proximity to an entered string, finding
misspelled names and typos. You can also look for numeric quantities
entered as text with the Numeric Pattern Matcher (NPM), finding “four
score and seven years ago” in the Gettysburg address when searching for
events 80 to 100 years ago.

The Metamorph Query Language was designed so that the text searcher can
get rudimentary satisfaction of result right away without needing to
know much of anything. At the same time, a more complex query can be
written with just a little self-training time on the advanced search
syntax possibilities. We like to say that there’s *nothing* that can’t
be found with a Metamorph query. This flexibility enhancing Texis, means
the system designer setting up the search environment and wanting to
customize it to certain applications can accomplish all his goals.

Texis, with Metamorph inside it, is intended to be a modular set of
tools to attack the formidable problem of how to get at and deal with
large quantities of information, when you don’t really know what you
want to know or where to find it; and in the most dynamic, efficient,
and pragmatic way possible. It is intended for discrete analysis where
the human supplies the final cognition.

Your Basic Metamorph Search
---------------------------

Metamorph has often been classified as a form of Artificial Intelligence
since its functions fall into the categories of knowledge acquisition,
natural language processing, and intelligent text retrieval.

The software attempts in its own way to understand your question,
represent its understanding to the data in the files, and come up with
relevant responses as retrieved portions of full text information which
best correspond to your questions.

Metamorph’s vocabulary is around 250,000+ word connections, constructed
in a dense web of associations and equivalences. Search parameters can
be adjusted to dynamically dictate surface and deep inference. The
program’s responses can be controlled so that they are direct or
abstract in relation to your questions. Proximity of concept can be fine
tuned so as to qualify degree of relevance, providing matches which are
sometimes concrete, sometimes abstract.

Think of your text as a field of information which was put together by a
human being for a stated purpose; the Equivalence File acts as an
intelligent language filter through which relevant associations
occurring as common denominators can be located and retrieved out of the
information in those files.

Metamorph retrieves data as a match to queries for response from any
text. Metamorph can search files which are not flat ASCII, but it is the
ASCII characters which will be recognized.

To help you get started, some demo text files are supplied with the
Texis package. These are files in the “``c:\morph3\text``” directory if
you installed on Drive C in DOS or Windows; or in the
“``/usr/local/morph3/text``” directory if you are on Unix:

| **Filenamexxx** **Description**
| alien science fiction excerpts
| constn the US Constitution
| declare the US Declaration of Independence
| garden descriptive prose
| kids children’s adventure stories
| liberty Patrick Henry’s “liberty or death” speech
| events downloaded news information from mid 1990
| qadhafi magazine interview with Mohamar Qadhafi
| socrates summary from Plato’s Republic about Socrates.

Let’s say you have set things up one way or another to be searching
these demo text files, and have Texis set up to type in a query. The
easiest thing to do is think of a few concepts or keywords you’d like to
see matched near each other in a sentence. For example, “power” and
“struggle”. This can be entered on the query line as
“``power struggle``”, where your default proximity is set to search by
sentence. Or if it has not, you can enter your query as
“``power struggle w/sent``”. If you have enabled Metamorph hit markup,
you might retrieve a passage of text which could be set up to look
something like this:

::

    File: c:\morph3\text\events
    PageNo: 11
    Query: power struggle

     million whites and 28 million blacks.  []Charges range from using
     excessive FORCE on antigovernment protests and torture of detainees
     to openly backing the ANC's rival Zulu movement Inkatha in bloody
     CLASHES in Natal province.[]
         "It's a Frankenstein which has been created and inherited by a
     racist set-up," Slovo told a news conference, noting the ANC
     "reserves the right" to resume the armed struggle "should the
     government fail to carry out its undertakings."

In this example, this section of text was selected because the sentence
marked by [] blocks at beginning and end matched the search request;
i.e.:

    Charges range from using excessive **force** on anti-government
    protests and torture of detainees to openly backing the ANC’s rival
    Zulu movement Inkatha in bloody **clashes** in Natal province.

This sentence was selected as a hit because it contained a concept match
to both concepts entered on the query line: i.e., “``force``” matched
“``power``”, and “``clashes``” matched “``struggle``”.

More Complex Query Syntax
-------------------------

As you learn more about how the Metamorph Query Language works, your
queries can become more complex, if so desired. Two key factors which
are part of any query, along with a statement of the search items you
are looking for, are intersection quantity and delimited text unit.

Search items can be weighted, by marking them for inclusion with a plus
sign ‘``+``’, or for exclusion with a minus sign ‘``-``’. All other
search items are considered equally weighted.

It is understood that the maximum number of unmarked search items will
always be looked for, unless a designation follows those sets of
‘``@#``’; i.e., the at sign ‘``@``’ followed by the desired number of
intersections; i.e., ``0-9+``. Designating “``@0``” would mean “zero
intersections required”.

For example, one might enter this query:

::

         +Near East @1 military political economic involvement

This query would locate any sentences which definitely contained a
reference to one of the countries in the Near East set, and also
contained at least one intersection of 2 of the 4 specified search sets:
“``military``”, “``political``”, “``economic``”, and “``involvement``”.
Thus it would retrieve the following sentence, where the words in curly
braces indicate the set to which the preceding word member belongs:

    Troops in **Turkey** *{Near East}* became **engaged**
    *{involvement}* in a heated **battle** *{military}* when a training
    exercise was misinterpreted as a hostile initiative.

You can further qualify the type of search results you are after by
changing the delimiters which dictate the proximity of entered concepts.
This can be done by adding to the query line “``w/delim``” where
“``delim``” is either “``line``”, “``sent``”, “``para``”, or “``page``”.
(NOTE: “``page``” only works where page formatting characters exist in
the text.) Or you can just choose an arbitrary number of characters to
search within; like: “``w/250``” to indicate a window of 500 characters
(250 forward, 250 back) around the first search item found.

Therefore you could change the nature of the above query:

::

         +Near East @1 military political economic involvement w/para

By adding a delimiter specification “``w/para``”, you are instructing
Metamorph to search by paragraph rather than by sentence. The required
proximity of concept is now much broader. More paragraphs will be found
to fit all the search requirements than sentences. But the sentences are
likely to be closer matches to the query, as the correlation of concept
had to be a closer match.

In addition to entering English questions and keywords, Metamorph has
several special pattern matchers which allow the user to search for
practically any type of expression. Any such expression is a valid
search item, which can be assigned a logic operator, and searched for in
proximity to other keywords, concepts, or expressions, as outlined in
the previous section.

All the above things are covered elsewhere in detail. Their strength can
be drawn upon where very specific types of results are desired.

Metamorph lets you create new and varied viewpoints as to what the
originator of the files might have intended, without hours of
preprocessing or knowledge engineering. These new views and impressions
can be informative, educational, and useful, enhancing content analysis
and data correlation.
