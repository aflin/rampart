Thesaurus Customization
=======================

Specializing Knowledge
----------------------

You could say that Metamorph’s general knowledge approximates that of
the man on the street without the benefit of specialized knowledge. This
knowledge is stored in a large Thesaurus containing some 250,000 word
associations supplied with the program.

It is not necessary for a query item to be stored in the Thesaurus for
Metamorph to search for it. In fact, for many technical terms you would
want only forms of that exact word rather than any abstract
associations.

If you wanted someone to intelligently correlate information from a
medical, legal, or other specialized or technical data base, you would
expect him to have a working knowledge of the nomenclature used in
discussing those subjects. Therefore, we have created a facility for
teaching Metamorph specialized vocabulary and domain specific inference
sets to draw upon for its concept set expansion.

You are customizing Metamorph for your own use by adding semantic
knowledge to the web of equivalences. In this way, the program can be
made “smarter” and able to take on the “viewpoints” of the user where
required. Metamorph becomes an increasingly powerful retrieval tool
through this process of the user teaching it new language and
associations.

By passing this ability onto the user, and allowing for thesaurus
customization to be an integral part of a search environment, the job of
evolving vocabulary becomes entirely the user’s responsibility and under
his control. We provide a basic foundation of over 250,000 associations
which is adequate to process any query with a reasonable amount of
discretion and intelligence. The job of refining that vocabulary and
keeping it up to date as language evolves is up to you.

Equivalence File Explanation
----------------------------

Metamorph draws upon a large interconnected web of root words and their
equivalence sets called the Equivalence File. For every English word
entered as a search item, a lookup is done in this file, and whatever
list of associations exists therein is included with the specified root
word.

The entered keyword is called the “root”. The associations pulled from
the Equivalence File are called the equivalences, or “equivs”.

Each list of words created from such Equivalence File lookup is passed
to PPM (the Parallel Pattern Matcher) for text searching. Morpheme
processing is done on all equivalences of all root words. Therefore, all
valid word forms of all words in a concept set are searched for.

There are over 250,000 of these word associations contained in the
Equivalence File, making a Metamorph search fairly intelligent without
the need for customization before trying it.

While the Equivalence File is similar to a thesaurus, it is more
accurately comprised of associations of different types, signalling
paths of equivalent weightings through a volume of vocabulary.

Where special nomenclature, slang, acronyms, or technical terminology is
in abundance, one can customize the Equivalence File by making a User
Equivalence File, through which tailored priorities can be ordered and
new entries added.

Editing Concept Sets With Backref
---------------------------------

It is intended that permanent Equivalence editing be done with the whole
group’s needs in mind, rather than to please just one individual user.
Vocabulary additions, specialized nomenclature, slang and acronyms that
would be common to a particular group, subgroup, purpose or project,
would be entered and saved in a User Equivalence File, as named with the
corresponding APICP flag ``ueqprefix``, and pointed to in the managing
program; e.g., a Vortex script.

While it is possible to create several User Equivalence Files for
different groups with differing acronyms and vocabulary, this is not
always necessary. More often than not there is only one User Equivalence
File per system, and multiples are created only to resolve conflicts in
terminology.

Running with See References on produces anywhere from 75 to 200
equivalences associated with a particular word; more than 256 words are
truncated. This is usually much more than is prudent, so it is intended
that with See References on, you would edit such a list down to
correspond more closely to what you really had in mind for the search
question at hand.

To check existing equivalence set for a word, use the BACKREF program,
where the syntax is:

::

         backref -e input_file output_file

where ``input_file`` is your ASCII equiv source filename, and
``output_file`` is the backreferenced and indexed binary file which the
Metamorph engine will use. The default User Equiv source file is
``eqvsusr.lst`` and its binary counterpart is ``eqvsusr``.

When you enter a query like “``power struggle``” in a query input box,
where concept search has been enabled, your search already knows 57
equivalences for ``power``, and 23 equivalences for ``struggle``. Using
the above command for BACKREF, you can see what those sets are composed
of, and you can edit them. When asked to enter a root term, enter
``power``, and the list below would be revealed:

::

    power;n  57 equivalences
      ability;n          intensity;n        primacy;n          might;u
      acquistion;n       jurisdiction;n     regency;n          reign;u
      ascendency;n       justice;n          restraint;n        rule;u
      authority;n        kingship;n         scepter;n          sovereignty;u
      carte blanche;n    leadership;n       skill;n            sway;u
      clutches;n         majesty;n          strength;n         electrify;v
      command;n          mastership;n       suction;n
      control;n          mastery;n          superiority;n
      domination;n       militarism;n       supremacy;n
      dominion;n         monarchy;n         vigor;n
      efficiency;n       nuclear fission;n  weight;n
      electricity;n      omnipotence;n      ability;u
      energy;n           persuasiveness;n   capability;u
      force;n            potency;n          control;u
      hegemony;n         predominance;n     energy;u
      imperialism;n      preponderance;n    faculty;u
      influence;n        pressure;n         function;u

::

    struggle;n  23 equivalences
      battle;n       agonize;v
      combat;n       compete;v
      competition;n  contest;v
      conflict;n     fight;v
      effort;n       flounder;v
      exertion;n     strive;v
      experience;n
      fight;n
      scuffle;n
      strife;n
      attempt;u
      clash;u
      conflict;u
      endeavor;u
      fight;u
      flight;u
      oppose;u

The root entry appears at the top, with its equivalences (each with an
assigned class, or part of speech) listed underneath. The ‘``n``’
following (or preceding) “``power``” stands for “``noun``”, and is the
class to which “``power``” has been assigned. (‘``v``’ means “``verb``”;
‘``u``’ means “``unclassed``”.)

Once editing a root word’s set of equivalences, you’ll have these
choices, offered below:

| ``xxxxxxxxxxxxxxxx``\ xx = Saves all changes made to list to User
  Equivalence File. ``Delete`` Deletes equivalence (named by number).
| ``Add`` Opens word entry line below list to add new equivalence.
| ``Zap`` Deletes entire equivalence list.
| ``Change-class`` Prompts on line below for new word class assignment.
| ``By-class-delete`` Deletes all words in the entered class.
| ``Save Changes`` Saves all changes made to list to User Equiv File.
| ``Undo Changes`` Restores previous root word entry screen.
| ``Redisplay`` Refreshes the list with any changes made (or as it was).

If you choose “``Save Changes``”, any changes made to the list will be
saved to the named User Equivalence File when you quit the program with
``qq``.

When a new word is added, you are prompted to enter its class. When the
new word is added to the list, it will be sorted in alphabetically at
its appropriate place in the list, under the class to which it belongs.
Existing thesaurus entries have been classed according to the standard
parts of speech as described below. However, you may create and assign
any class you like.

In the example above under “``struggle``”, you might want to
delete-by-class all those entries listed as verbs. Doing so would
eliminate with one keystroke all equivalences classed as ‘``v``’:
“``agonize``”, “``compete``”, “``contest``”, “``fight``”,
“``flounder``”, and “``strive``”. The classes in use are as follows:

| Pxxx = Pronoun P Pronoun
| c conjunction
| i interjection
| m modifier
| n noun
| p preposition
| u unclassed
| v verb

You can ``Undo Changes`` anytime while editing, to escape from the
action you are in. This restores the entry screen, which lets you choose
another root word to edit, add to, or delete. When you are finished
editing a word, either ``Save`` or ``Undo`` Changes, then ``qq`` to
Quit. At that point the source file will be indexed into its binary form
usable by the search, if you have saved any changes.

Toggling Equiv Expansion On or Off
----------------------------------

The APICP flag ``keepeqvs`` lets you set the default for concept
expansion. If set on, unless otherwise marked, the search will use any
equivalences found in the Equivalence File associated with any word
entered in a query. This global condition can be selectively reversed by
preceding a word in a query with a ``~`` tilde. If the global setting is
off, the ``~`` will selectively enable concept expansion (i.e.,
equivalence look-up) on that word; if the global setting is on, the
``~`` will turn it off for that word. (Note that equivalences can also
be explicitly specified in parentheses; see p. .)

While use of the Equivalence File is an integral part of the
intelligence of a Metamorph search, in certain kinds of particularly
specialized or technical data such abstraction may not be desired.
Turning the ``keepeqvs`` flag off prevents any automatic lookup in the
Equivalence File, changing the nature of the search so that the emphasis
is rather on intersections of valid word forms of specified English
words, mixed in with special expressions. For example:

::

         What RESEARCH has been done about HEALTH DRINKS?

Morpheme processing will be retained on the important (non-noise) words,
but equivalences will not be included. An example of a sentence this
question would retrieve, would be:

    The company had been **researching** ingredients which would taste
    good in a **drink** while still promoting good **health**.

Where the global setting is on and you wish to selectively restrict
Equivalence Lookup on some but not all words, you use tilde ‘``~``’ in
front of those words where no equivalences are desired. This would be
the preferred method of search for some types of technical material,
such as medical case data.

Although at first glance it would seem that an effective Metamorph
search could not be done on technical data until much specialized
nomenclature was taught to the Equivalence File, this is not always the
case. Often a technical term means only that, and the power is in
intersecting some valid English form of that word with some other
concept set.

An example of a very discrete query that requires no knowledge
engineering beyond what comes “out of the box” with Metamorph might be
as in this query (assuming ``keepeqvs`` turned on):

::

         stomach ~cancer operation

The tilde ‘``~``’ is used to restrict equivalence lookup on “``cancer``”
(toggle the ``keepeqvs`` setting, to off), as references to such things
as “``illness``” rather than “``cancer``” would be too abstract.
However, what you do want is concept expansion on the related words.
Therefore, such a search would retrieve the sentence:

    Suffering severe pains in his **abdomen**, it was first thought to
    be appendicitis; however this led to exploratory **surgery** which
    revealed **cancerous** tissue.

In this example, “``cancerous``” is a valid word form of “``cancer``”
included through the morpheme process; “``abdomen``” was found because
it was in the “``stomach``” equivalence list; “``surgery``” was found
because it was in the “``operation``” equivalence list.

Again, with the ``keepeqvs`` flag set on, use of the tilde (``~``)
reverses its meaning. Therefore, the above example would seek only valid
word forms of the root words “``stomach``” and “``operation``”, but the
tilde preceding “``cancer``” would selectively enable Equivalence Lookup
on that word alone. So, with ``keepeqvs`` off, we might retrieve instead
the following:

    In the midst of the **operation** to remove her appendix, an
    abnormal **growth** was found in the **stomach** area.

This last response is matched because “``operation``” and “``stomach``”
are forms of those root words; “``growth``” is in the equivalence list
for “``cancer``” and was included in the set of possibilities due to the
tilde (``~``) preceding it.

Creating a User Equivalence File By Hand
----------------------------------------

The User Equivalence File is read by Metamorph as an overlay to the Main
Equivalence File. The search looks for matching root entries first in
the User Equivalence File, and then in the Main Equivalence File. The
information found in both places is combined, following certain rules.

When editing equivalences using the BACKREF program as shown, the
changes you make are written to the named User Equivalence File. It is
also possible to hand edit a User Equivalence File if you understand the
syntax which is used when writing directly to it.

In order to precisely deal with issues such as precedence, substitution,
removal, assignment, back referencing, and see references, a strict
format must be adhered to. Any erroneous characters included in the User
Equivalence File could be misinterpreted, causing unseen difficulties.
Therefore, one must take care to ensure the User Equivalence file is
flat ASCII.

If you want to create a User Equivalence file independent of the
``backref -e`` feature, follow these steps:

#. Using an ASCII only editor, open a file called “``eqvsusr.lst``”. If
   you already have a list of words and equivalences you want to add in
   a flat ASCII file, you can edit the entries into the prescribed
   format. Otherwise, simply begin entering root words with their
   equivalences as outlined in the following sections.

#. After creating the above User Equivalence File, it must still be
   indexed by the “``backref``” program supplied with your Texis
   package. ``Backref`` takes an ASCII filename as the first argument,
   and creates a file of the name given in the second argument. For
   example, use this command on your ASCII User Equivalence File:

   ::

            backref eqvsusr.lst eqvsusr

   Where “``eqvsusr.lst``” is the ASCII file containing your User
   Equivalence entries, and “``eqvsusr``” is the file ready for use by
   the Metamorph search engine.

#. If you have not otherwise named a special User Equiv File in some
   managing program such as a vortex script, the indexed User
   Equivalence File must be called “``eqvsusr``”, and must be located in
   the “``morph3``” directory, in order for it to be used by a Metamorph
   search.

User Equivalence File Format
----------------------------

A User Equivalence File is an ASCII file created by the user, which
corresponds to information in the larger 2+ megabyte Main Equivalence
File which comes with the Texis package.

Each root word is listed as a separate entry, on its own line with a
list of known associations or equivalences (equivs).

The root words go down the lefthand side of the file, each one a new
entry; the equivalences go out left to right as separated by commas.
Word class (part of speech) and other optional weighting information may
be stored with each entry.

Here is an example of a User Equivalence File. It contains no special
information beyond root words and their equivalences. Its chief purpose
would be the addition of domain specific vocabulary. Phrases are
acceptable as roots or as equivalences.

::

         chicken,bird,seed,egg,aviary,rooster
         seed,food,feed,sprout
         ranch,farm,pen,hen house,chicken house,pig sty
         Farmer's Almanac,research,weather forecast,book
         rose,flower,thorn,red flower
         water,moisture,dew,dewdrop,Atlantic Ocean
         bee pollen,mating,flower,pollination,Vitamin B
         grow,mature,blossom,ripen

Root word entries should be kept to a reasonable line length; around 80
characters for standard screen display is prudent. In no case should a
root word entry exceed 1K per line. Where more equivalences exist for a
root word than can be fit onto one line, enter multiple entries where
the root word is repeated as the first item. For example:

::

         abort,cancel,cease,destroy,end,fail,kill
         abort,miscarry,nullify,terminate,zap

It is important to remember that these are not just synonyms. They can
be anything you wish to associate with a particular word: i.e.,
identities, generalities, or specifics of the word entry, plus
associated phrases, acronyms, or spelling variations. Even antonyms
could be listed if you wished, although that wouldn’t generally be
advisable.

The word “equivalence” is used in a programming sense, to indicate that
each equivalence will be treated in weight exactly the same as every
other equivalence in that set grouping when a search is executed.

Back Referencing
----------------

Both the Main Equivalence File and the User Equivalence File include a
provision for “back referencing”. That is, where a word is stored as the
equivalence to another root entry, there is an inherent connection
“backwards” to the root, when that equivalence is entered as a keyword
(root) on the Query Line.

For example, imagine the following Equivalence File entry for the root
acronym “``A&R``”:

::

         A&R,automation,robotics,automation and robotics

Metamorph, and so therefore Texis, knows when you enter “``robotics``”
as a root word in a query, that it should back search and associate it
with “``A&R``”. Therefore, when “``robotics``” is used as a keyword,
“``A&R``” will be automatically associated as one of its equivalences.

Back referencing means that the following association is implicitly
understood, and need not be separately entered:

::

         robotics,A&R

Such automatic back searching capability exponentially increases the
density of association and connection within the Equivalence File and
User Equivalence File.

See Referencing
---------------

You can exponentially increase the denseness of connectivity in the
Equivalence File by invoking “See References”, set with the APICP flag
``see``.

The concept of a “``See``” reference is the same as in a dictionary.
When looking up the word “``cat``”, you’ll get a description and a few
definitions for cat. Then it may say at the end “``see also pet``”. With
See Referencing on, the equivalence list associated with the word
“``cat``” is expanded to also include all the equivalences associated
with the word “``pet``”.

See Referencing greatly increases the general size of word sets in use
in any search, increasing the chance for abstraction of concept. One
would not normally invoke See Referencing, and would do so only where
such abstraction was desired.

Not all equivalences have “``See also``” notations; but with See
Referencing on, all equivalences associated with any “``See also``” root
word will be included as part of the original. With See References off,
only the root word and its equivalences will be included in that word
set, regardless of whether a see reference exists or not.

See Referencing is restricted to only one level of reference, to prevent
inadvertent “endless” looping or overlap of concepts. In any case, a
word set will be truncated at the point it approaches 256 equivalences.

See References are denoted with the at sign (``@``). Enter the word
preceded by ‘``@``’. For example:

::

         cowboy,horse,cows,@rancher
         rancher,plains,landowner

In normal usage, the search item “``cowboy``” expands to the set
“``cowboy``”, “``horse``”, “``cows``” and “``rancher``”. With see
referencing invoked, “``cowboy``” expands to “``cowboy``”, “``horse``”,
and “``cows``” as well as “``rancher``”, “``plains``”, and
“``landowner``”.

The see reference ‘``@``’ marking in a User Equivalence file will only
connect entries which are in the User Equivalence file. In the above
example, the entry for “``rancher``” must exist in the User Equivalence
to be so linked.

Word Classes and Parts of Speech
--------------------------------

Part of speech or other word class information can be stored with
equivalence entries. Use the following abbreviations:

::

         P: Pronoun           n: noun
         c: conjunction       p: preposition
         i: interjection      u: unclassed
         m: modifier          v: verb

A part of speech abbreviation follows a semicolon (``;``), where the
part of speech designation applies to that word and to all equivalences
which follow it up to the next part of speech abbreviation on the line.
For example:

::

         wish;n,pie in the sky,dream;v,yearn,long,pine

The above is an entry for the root word “``wish``”. The equivalences
“``pie in the sky``”, and “``dream``” are classed as nouns. The
equivalences “``yearn``”, “``long``”, and “``pine``” are classed as
verbs.

Rules of Precedence and Syntax
------------------------------

A root word and its equivalences are separated by commas. The comma
(``,``) signifies addition of an item to a set.

Where an entry exists in a User Equivalence File and also in the Main
Equivalence File, equivalences found for all entries of that root word
are combined into one set. Example:

::

         constellation,celestial heavens

Phrases are acceptable as roots or as equivalences, and locate matches
as separated by a hyphen or any kind of white space, provided the
separation is only one character long. Use spaces rather than hyphens to
enter normally hyphenated words.

When “``constellation``” is entered as a search item on the Query Line,
“``celestial heavens``” from the User Equivalence File will be added to
the existing set, making the complete concept set:

::

         constellation
         celestial heavens
         configuration of stars
         galaxy
         group of stars
         nebula
         star
         zodiac

Equivs can be removed from a larger set by preceding them with a tilde
(``~``) in the User Equivalence File. For example:

::

         constellation~nebula~zodiac,big dipper

This entry for constellation reads “remove ‘``nebula``’, remove
‘``zodiac``’, and add ‘``big dipper``’”; making the complete concept
set:

::

         constellation
         big dipper
         configuration of stars
         galaxy
         group of stars
         star

A whole equivalence set can be substituted for what is in the Main
Equivalence File with a User Equivalence File entry which uses the equal
sign (``=``) preceding the favored list of equivalences. For example:

::

         constellation=constellation,galaxy,nebula,star

This entry for constellation replaces any entries in the Main
Equivalence File, making the complete concept set:

::

         constellation
         galaxy
         nebula
         star

Don’t forget to include the root word following the equal sign (``=``),
as the substitution is literal for the whole set, and the root word must
be repeated to be included.

To permanently swap one word for another, you could make one entry only,
having the effect of assignment. For example:

::

         constellation=andromeda

Subsequent searches for “``constellation``” where concept searching is
invoked will swap “``constellation``” for “``andromeda``”.

To permanently disable concept expansion for an item, use the equal sign
(``=``) to replace a keyword with itself only. For example:

::

         constellation=constellation

Any equivalences from the Main Equivalence File would be ignored, as the
set is replaced by this entry.

The above rules for substitution apply where what immediately follows
the equal sign (``=``) is alphanumeric. In the special case where the
1st character following the equal operator (``=``) is not alphanumeric,
the entirety of what follows on the line is grabbed as a unit, rather
than as a list of equivalences. Example:

::

         lots=#>100

The root word “``lots``” will be replaced on the Query Line by the NPM
expression which follows the equal sign “``#>100``”, therefore finding
numeric quantities greater than 100, rather than finding English
occurrences of the word “``lots``”.

All root and equivalence entries are case insensitive. If you need case
sensitivity you must so specify with ``REX`` syntax on the Query Line.
``REX``, ``NPM``, ``XPM``, and ``*`` (Wildcard) expressions cannot be
entered as equivalences, as equivalences are sent directly to ``PPM``
which processes lists of English words.

The only way an English word may be linked in this way to a special
expression is through the use of substitution. In this case the
expression which follows an equal sign (``=``) will be substituted for
the root word. Example:

::

         bush=/\RBush

The root word “``bush``” will be replaced on the Query Line by the
``REX`` expression which follows the equal sign “``/\RBush``”; therefore
finding only the proper name “``Bush``”, rather than the common noun
“``bush``” along with any of its equivalences (“``jungle``”,
“``shrub``”, “``hedge``”) as listed in the Main Equivalence File.

Specialized User Equivalence Files
----------------------------------

Where you are creating your own specialized lexicon of terms, such as
for medical, legal, technical, or acronym laden fields, you may be in a
position to obtain your own digitized compilation of such which could be
of some size.

Rather than starting from scratch, take the digitized file, flatten it
to ASCII, then follow the User Equivalence rules as outlined in the
previous sections to edit it into the correct format.

If you have questions on how you might speed up this activity, call
Thunderstone technical support for discussion of details.
