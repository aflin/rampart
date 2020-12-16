Tailoring Metamorph’s Linguistics
=================================

[chp:mmling]

Editing Special Word Lists
--------------------------

There are special word lists which are called upon at various points in
the Metamorph search routine and influence the way your search requests
are processed, as well as which hits are deemed valid and presented for
viewing. These lists can be tailored to user specification if desired.

While it is encouraged that you edit equivalences as often as you wish
to reflect what you have in mind to search for, it should be clearly
understood that editing equivalences that go with search words is a very
different activity from editing the special word lists which govern
Metamorph’s approach to linguistically processing the English language.

The default content of the special lists is the result of much research
on the part of the program developers, and reflects what has been locked
inside Metamorph for a long time for effective use in most situations.
Still, these lists are open to editing to conform to program purpose:
that just about everything that is related to how Metamorph processes
language is subject to modification.

The special word lists are what the program classifies as: Noise (which
also includes Pronouns, Is-associations, and Question words), Prefixes,
Suffixes, and Equiv-Suffixes.

For example, here is the default Pronoun list:

::

      anybody             ourselves         you
      anyone              she               your
      anything            somebody
      each                someone
      everyone            something
      everything          their
      he                  them
      her                 they
      him                 this
      his                 us
      i                   we
      me                  whatever
      mine                who
      my                  whoever
      myself              whom
      our                 whose

The content of this list is stored in ASCII format in a Metamorph
profile, and can be edited there, if you have an application set up
which can read a special profile. Otherwise trust the internal defaults
of the program, and override them with an inline APICP directive if you
need to change them, as shown under *Question Parsing*.

Question Parsing
----------------

A Metamorph query will by default be parsed for noise before the main
words are sent out to the executed search. The intent is that a user can
phrase his search request in a manner which reflects how he is thinking
about it, without requiring him to categorize his thoughts into a
syntactical procedure.

You do have the option to keep the noise words as significant words in
the query. This choice can be invoked by setting the APICP flag
``keepnoise`` to ``on``. If off, as is the Metamorph default used by
Texis, the question parser is invoked and allowed to exercise discretion
in determining what is most important about your query. This selection
process determines what should be treated as a search word, and
therefore included in the set count and intersection quantity assigned
to each significant search item.

A need to edit these lists would be most likely to come up if one were
searching language from a different era or with a vastly different
language style; perhaps for example the Bible. Where “``thee's``” and
“``thou's``” replace “``you``” and “``yours``”, certain things should be
filtered out from the query that might not be entered in the list. These
categories can be defined as follows, where all of these categories are
grouped for APICP programming purposes under “noise”.

Noise
    Small, common, relational words which appear frequently in a
    particular language and refine and fine tune specific
    communications, but do not majorly affect the larger concepts under
    discussion; e.g., about, in, on, whether.

Pronouns
    Words indicating person, number, and gender which are used in place
    of other nouns; e.g., her, his, we, them, its.

Is
    Words indicating state of being or existence, or used as auxiliary
    verbs as assistive in defining tense and use; e.g., is, being, was.

Questions
    Words indicating a question is being asked and requires an answer or
    response; e.g., what, where, when.

Syntax for setting a custom list inside a Vortex script is as follows,
where what you write in would override the default internal noise list:

::

      <$noiselist = "am" "are" "was" "were" "thee" "thou" "has" "hast"
         "from" "hence">
      <apicp noise $noiselist>

Morpheme Processing
-------------------

Metamorph’s whole search process is built around its ability to deal
with root morphemes in words, and in fact the program was originally
christened “Metamorph” based on that ability. An inherent part of the
search process is to take search words, strip them of suffixes and
prefixes down to a recognizable morpheme, search for the morpheme, find
a possible match, then go through the process again with the possible
match to see if it is indeed what you were looking for.

The elements which affect morpheme processing are very subtle as they
affect all language and therefore all of any text you are searching.
Therefore, any changes entered into the defaults which dictate how or
whether morpheme processing is done, will affect broadly the nature of
your search results.

The options which affect morpheme processing are the following:

-  Content of the Equiv-Suffixes List;

-  Content of the Prefix List;

-  Content of the Suffix List;

-  Prefix processing on/off toggle;

-  Suffix processing on/off toggle;

-  Morpheme rebuild on/off toggle;

-  Minimum word length setting.

The content of the ``Equiv-Suffix`` List determines which suffixes will
be stripped before doing lookup for a matching entry in the Equivalence
File.

The content of the ``Prefix`` and ``Suffix`` Lists determines precisely
what will be stripped from a search word to obtain the morpheme which is
passed to the Search Engine. Specifically this relates to all words in
all equivalence sets.

To understand why the program is doing what it is doing you need to know
more than just what is contained in the lists; you must also understand
the sequence of the Morpheme Stripping process. If you understand these
rules, you will have better judgment on how to edit the prefix and
suffix lists, what happens when you turn off prefix and/or suffix
processing altogether, what happens if you turn off the morpheme rebuild
step, and where the minimum word length setting comes in.

Morpheme Stripping Routine
~~~~~~~~~~~~~~~~~~~~~~~~~~

This routine is done by the Engine as a preliminary step before actually
executing any search, using the content of the Prefix and Suffix Lists.
This routine is used to get words from the Equivalence File, and only
does the suffix stripping part, using the content of the Equiv-Suffix
List.

#. When it is time to execute a search, the suffix and prefix lists are
   each sorted by descending size and ascending alphabetical order. The
   reason for and importance of descending order is so that suffixes and
   prefixes can be stripped largest to smallest. There is no particular
   reason for alphabetical order except to provide a predictable
   ordering sequence.

#. Get the word to be checked from entered query; e.g.,
   “``antidisestablishmentarianism``”.

#. Check the word’s length to see if it is greater than or equal to the
   length set in minimum word length. The default in Texis is 255. A
   setting of 5 normally produces the best results with the default
   suffix list. If so, carry on. If not, there is no need to morpheme
   strip the word; it would just get searched for as is; e.g.,
   “``red``”.

#. Check the word found against the list of suffixes to see if there is
   a match, starting from largest suffix on the list.

#. If a match is found strip it from the word. Note: This is why
   ordering by size is so important: because you want to remove suffixes
   (or prefixes) by the largest first, so as not to miss multiple
   suffixes, where one suffix may be a subset of another.

#. Continue checking against the list for the next match. Follow steps
   4-5 until no more matches found. In the case of our example above,
   based on the default suffix list, we would be left with the following
   morpheme before prefix processing: “``antidisestablishmentarian``”.
   Note the following things:

   -  The suffix “``ism``” was on the list and was stripped.

   -  Neither “``an``”, “``ian``”, nor “``arian``” was on the suffix
      list, so it was not stripped.

   -  The suffix “``ment``” is on the suffix list, but it was not left
      at the end of the word at any point, and therefore was not
      removed.

   -  If “``arian``” and “``ian``” were both entered on the suffix list,
      “``arian``” would be removed first, so as not to remove “``ian``”
      and be left with “``ar``” at the end of the word which would not
      be strippable.

#. If suffix checking (only), remove any trailing vowels, or 1 of any
   double trailing consonants. This handles things like “``strive``”,
   which would be correctly stripped down to “``striv``” so that it
   won’t miss matches for “``striving``”, etc. (trailing vowel). And
   things like “``travelling``” would be stripped to “``travell``”; you
   have to strip the second ‘``l``’ so that you wouldn’t miss the word
   “``travel``” (trailing double consonant). Note: this is only done for
   suffix checking, not prefix checking.

#. Now repeat Steps 4-6 for prefix stripping against the prefix list. In
   our example, “``antidisestablishmentarian``” would get stripped down
   to “``establishmentarian``”. This is what you have left and is what
   goes to the pattern matcher.

#. When something is found, the pattern matcher builds it back up again
   to make sure it is truly a match to what you were looking for. This
   prevents things like taking “``pressure``” when you were really
   looking for “``president``”, “``restive``” when you were really
   looking for “``restaurant``”, and other such oddities.

Suffix List Editing Notes
~~~~~~~~~~~~~~~~~~~~~~~~~

It should be noted that the Suffix List used for Equivalence Lookup is
different than the Suffix List used for matching words in the text. One
should make changes in the Suffix List with great care, as it is so
basic to what can be found in the text.

While caution should also be applied to editing the Equiv-Suffix List,
doing so has a different effect upon the search results. The default
Equiv-Suffix List is quite short by comparison, containing only 3
suffixes. This is because different forms of words tend to have
different sets of concepts, as contained in the Equivalence File.

If you feel the suffix list is not broad enough, you might wish to
carefully expand it. An example of a valid suffix to add might be
“``al``”. Doing so would mean that typing in a query using the word
“``environmental``” would pull up the equivalences listed for
“``environment``”.

If an exact entry for your word exists, only its equivalences will be
retrieved from the Equivalence File. You want “``monumental``” to match
“``colossal``”, but you would probably be unhappy if it matched
“``tombstone``” from the set associated with “``monument``”. If no such
exact entry existed, you would have to be prepared for “``monumental``”
to dutifully retrieve the word “``obelisk``” if it were linked to
“``monument``”.

Keeping all these English nuances in mind, you do in fact have complete
control over exactly what you can make the program retrieve.

Toggling Prefix/Suffix Processing On/Off
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

[set:presuf]

You can toggle prefix and suffix processing individually on and off. If
you don’t like the way a particular word is being suffix or prefix
processed, the easiest thing to do is simply turn off suffix or prefix
processing temporarily and search for the word exactly as you want it.
This is done by setting the corresponding APICP flag for ``suffixproc``
or ``prefixproc``.

When one or both (suffix or prefix) morpheme processes are off, none of
that respective routine (or routines) will be done at all. If both are
off, you get a search for exactly the word you were looking for with no
removal at all, and no check once found; in other words, the word you
enter is passed directly from the search line to the ``PPM`` (Parallel
Pattern Matcher) search algorithm.

Note that prefix processing is not supported by the Metamorph index, and
thus enabling it will require a linear search (slower). This is why
prefix processing is off by default in Vortex, ``tsql`` and the Search
Appliance.

Toggling Morpheme Rebuild Step On/Off
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can also turn off the Morpheme Rebuild step if you want to, by
setting it with the corresponding APICP flag ``rebuild``. The Rebuild
step, referenced in Step 9 above, is intended as a check to rebuild
morphemes back up into words once found, to verify hits. If not
otherwise set, the default is “``on``”; i.e., that this step is done.

It may happen however, that you either miss a hit you feel you should
have gotten, or are getting a hit that does not make sense to you. If
so, you can try turning off the rebuild step, do your search, go into
context and see exactly what was retrieved (as it will be highlighted)
and why. This will at least serve to explain to you exactly what is
going on in the search process.

Remember that the English language is based on more irregularities than
regularities. Metamorph is designed to process the English language as
best as it can, in a regular fashion. We leave it to specific
applications to adjust these language toggles to one’s own satisfaction,
and expect that everything will not be set in exactly the same way every
time.

Setting Minimum Word Length
~~~~~~~~~~~~~~~~~~~~~~~~~~~

[set:minwrdlen]

Minimum word length is the approximate number of significant characters
the program will deal with at a morpheme level. You increase it to
obtain more exactness to the search pattern entered, and decrease it for
less exactness to that pattern. The smaller the minimum word length, the
slower the search will be, although the difference may be imperceptible.

Vortex scripts, ``tsql`` and the Search Appliance use a default minimum
word length of 255, which essentially turns off morpheme stripping and
allows for exact searching in locating documents. To use morpheme
processing on a content oriented, English-smart application, set the
APICP flag for ``minwordlen`` to 5. From years of experience we have
established this as the best place to start, and really do not advise
changing this setting arbitrarily.

As applies to the Morpheme Stripping Routine, note the following: in
general (about 90% of the time) these rules are followed exactly, and
the word would never be stripped smaller than the set length. But in
certain cases to take into account certain overlapping rules and/or
idiosyncrasies as it sees fit, it will sometimes strip down further than
minimum word length; but never more than 1 character.

Warning to Linguistic Fiddlers
------------------------------

The program defaults and guidelines as described herein pass on to the
user years of experience in what works best in terms of all these
linguistic elements to accomplish in the main generally satisfactory
search results. If you are not getting good results for some reason, or
have allowed various situation specific choices at some points of your
program, restore the defaults before continuing. This can be done by
setting the APICP flag ``defaults``, which restores the Vortex defaults
to the next search.

APICP Metamorph Control Parameters Summary
------------------------------------------

You can modify these APICP settings by adjusting these flags to modify
the Metamorph control parameters below. These are covered elsewhere in
relation to their use in Texis, Vortex, or the Metamorph API; see the
appropriate manual. The settings are:

-  : Whether to do suffix processing or not.

-  : Whether to do suffix processing or not.

-  : Whether to do word rebuilding.

-  : Include start ``w/`` delimiters in hits (always on for ``w/N``).

-  : Include end ``w/`` delimiters in hits (always on for ``w/N``).

-  : Whether to respect the within (``w/``) operator.

-  : Minimum word length for morpheme processing.

-  : Default number of intersections (if no ``@``).

-  : Whether to look up “see also” references.

-  : Whether to keep equivalences for words/phrases.

-  : Whether to keep noise words in the query.

-  : Default start delimiter REX expression.

-  : Default end delimiter REX expression.

-  : Main equivalence file name.

-  : User equivalence file name.

-  : Suffix list for suffix processing during search.

-  : Suffix list for suffix processing during equivalence lookup.

-  : Prefix list for prefix processing during search.

-  : Noise word list.

-  : Restore defaults for all APICP settings.

Note that the default values for these (and other) settings may vary
between Vortex, the Search Appliance, ``tsql`` and the Metamorph API.
See the section on “Differences Between Vortex, tsql and Metamorph API”
in the Vortex manual for a list of differences.

The easiest way to make use of these settings is in a Vortex script,
e.g.:

::

    <apicp suffixproc 1>

In ``tsql``, they can be altered with a SQL ``set`` statement:

::

    tsql "set suffixproc=1; select ..."

The various C APIs have their own calls, e.g. ``n_setsuffixproc()``.

Some of the settings can also be changed inline with the query, for
example: horse @minwordlen=3 cat. Such inline settings will apply to all
terms after the setting.

You can also create thesaurus entries that apply specific settings, for
example the thesaurus entry:

::

    battery={@suffixproc=0,battery,batteries}

could be used to give exact word forms for just a specific word in a
query. I.e. only the words battery and batteries will match for battery:
no suffix processing for other suffixes will be done for that term,
though such processing may be done for other terms. The { and } curly
braces are required to enable inline settings in an equiv list. Also,
the settings should be given first in the list, so that they apply to
all the words in the list.

To use inline settings in a query-specified equiv list, you will need to
escape the = which is special, e.g.:

::

    "cell phone" ({@suffixproc\=0,battery,batteries})

The following settings may be specified inline:

-  
-  
-  
-  
-  
-  
