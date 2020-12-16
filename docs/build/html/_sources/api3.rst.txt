Metamorph Application Program Interface Overview
================================================

*“Build the power of Metamorph into your application”*

Metamorph is the only REAL concept based text retrieval product on the
market today. In its stand-alone form it acts as a research assistant
that provides its user with an easy query interface while under the hood
it is performing some of the most complex text search techniques in the
field.

*What is a Metamorph Search?*

Metamorph searches for some combination of “lexical sets” within the
bounds of two lexical delimiters. The idea of using set logic is very
important to the way the software works.

When people communicate to each other they rarely use exactly the same
vocabulary when trying to communicate a common idea. Typically, concepts
are communicated by stringing combinations of abstract meanings together
to form a concise idea. For example, if you were trying communicate the
idea of a “nice person” to someone else you might use any of the
following forms:

-  nice guy

-  pleasing chap

-  agreeable character

-  excellent human being

-  exceptionally fine person

While there are subtle differences in each of these phrases, the
underlying concept is the same. It is also worth noting that by
themselves the individual words in each phrase carry little indication
of the whole idea. In a much larger sense, people string these types of
concepts to form heuristically larger and more complex communications.
One ordering of heuristic classifications might be as follows:

-  morpheme (a small token that can be built into a word)

-  word

-  phrase

-  clause

-  sentence

-  paragraph

-  chapter

-  book

-  collection

If you are searching for a concept within a body of text, you are
actually searching for an intersection in meaning of your idea of what
you are searching for with/and the body of information you are
searching. Metamorph performs this search operation for you
automatically.

Within Metamorph if you perform the query: *Are there power struggles in
the Near East?*

Metamorph will find the individual “important” terms within your query.
Then, it will look these terms up in a thesaurus that contains over
250,000 associations and it will expand each term to the set of things
that mean approximately the same thing:

power:
    ability, jurisdiction, regency, sovereignty, ascendency, justice,
    restraint, sway, authority, kingship, scepter, electrify, carte
    blanche, leadership, skill, clutches, majesty, strength, command,
    mastership, suction, control, mastery, superiority, domination,
    militarism, supremacy, dominion, monarchy, vigor, efficiency,
    nuclear, fission, weight, electricity, omnipotence, acquisition,
    energy, persuasiveness, capability, force, potency, faculty,
    hegemony, predominance, function, imperialism, preponderance, might,
    influence, pressure, reign,

struggle:
    battle, contest, combat, flounder, competition, strive, conflict,
    effort, exertion, experience, fight, scuffle, strife, attempt, cash,
    endeavor, flight, oppose, agonize, compete

Near East:
    Israel, Jordan, Lebanon, Saudi Arabia, Syria, Turkey, Kuwait,
    Iran, Iraq

After it has built these sets, it will search through the text you have
designated for a place in the text that has all three of the concepts
present within some defined boundary (i.e.; sentence, line, paragraph,
page, chapter, etc.). So, if it was instructed to search by sentence it
would be able to retrieve the following:

*Iraq’s Sadam Hussein is being pressured by the U.N. to suspend his
endeavors to annex Kuwait.*

Please Note that Metamorph recognizes that “Near East” is a phrase that
means the countries in the Near East and not the concepts of “near” and
“east” individually. Also, it is not only looking for each of the words
in the lists, but it is also looking for every word-form of the words in
each of the lists.

Technical Description
---------------------

Within Metamorph a “set” can be any one of four different types of text
data:

-  The set of words or phrases that mean the same thing.

-  The set of text patterns that match a regular-expression.

-  The set of text patterns that are approximately the same.

-  The set of quantities that are within some range.

There are three types of operations that can be used in conjunction with
any set:

| INCLUSION The set must be present.
| EXCLUSION The set must not be present.
| PERMUTATION X out of Y sets must be present.

The set logic operations are performed within two boundaries:

-  A starting delimiter (e.g.; the beginning of a sentence).

-  An ending delimiter (e.g,; the end of a sentence).

Each type of set plays an important role in the real-world use of a text
retrieval tool:

-  The word-list pattern matcher can locate any word form of an entire
   list of English words and/or phrases.

-  The regular-expression pattern matcher allows the user to search for
   things like dates, part numbers, social security numbers, and product
   codes.

-  The approximate pattern matcher can search for things like
   misspellings, typos, and names or addresses that are similar.

-  The numeric/quantity pattern matcher can look for numeric values that
   are present in the text in almost any form and allows the user to
   search for them generically by their value.

The Metamorph search engine will always optimize the search operations
performed so that it will minimize the amount of CPU utilization and
maximize the throughput search rate. At the heart of the Metamorph
search engine lie seven of the most efficient pattern matchers there are
for locating items within text. With the exception of the Approximate
Pattern Matcher, all of these pattern matchers use a proprietary
algorithmic technique that is guaranteed to out-perform any other
published pattern matching algorithm (including those described by
Boyer-Moore-Gosper and Knuth-Pratt-Morris).

Providing the user with set-logic to manipulate combinations of these
set-types gives them the ability to search for just about anything that
they might want to find in their textual information. The query tool in
general can be as simple or sophisticated as the user wishes, with the
simplest query being a simple natural-language question.

Some Search Examples and Explanations
-------------------------------------

Example 1:

Let’s say that we want to search for any occurrence of An Intel 80X86
processor on the same line with the concept of “speed” or “benchmark” as
long as the string “Motorola” is not present.

The query is: ``+/80=[1-4]?86  -/motorola speed benchmark``

Explanation:

A leading ``'+'`` means “this must be present”.

A leading ``'-'`` means “this must not be present”.

The ``'/'`` signals the use of a regular-expression.

``'/80=[1-4]?86'`` will locate an ``'80'`` followed by an optional
``('1' or '2' or '3' or '4')`` followed by an ``'86'``. This will
locate: ``8086, 80186, 80286, 80386 or 80486.``

``'/motorola'`` will locate ``'MOTOROLA'`` or ``'Motorola'`` or
``'motorola'`` (or any other combination of alphabetic cases).

``'speed'`` will locate any word that means “speed”.

``'benchmark'`` will locate any word that means “benchmark”.

The beginning and ending delimiting expressions would be defined as
``'\n'`` (meaning a new-line character).

The Metamorph search engine will now optimize this search and will
perform the following actions:

A.  Search for any pattern that matches ``'/80=[1-4]?86'``. When it is located do item (B).

B.  Search backwards for the start delimiter ``'\n'`` (or begin of file/record whichever comes first).

C.  Search forwards for the ending delimiter ``'\n'`` (or end of file/record whichever comes first).

D.  Search for the pattern ``'/motorola'`` between the start and end delimiters. If it is *not* located do item (E), otherwise go to item (A).

E.  Search for the set of words that mean “benchmark”. If a member is located do item (G), otherwise, do item (F).

F.  Search for the set of words that mean “speed”. If a member is located do item (G), otherwise, go to item (A).

G.  Inform the user that a hit has been located.

Example 2:

Let’s say we are searching an address and phone number list trying to
find an entry for a person whose name has been apparently entered
incorrectly.

The query: ``"%60 Jane Plaxton"  "%60 234 rhoads dr."  /OH  /49004``

Because our database is large, we want to enter as much as possible
about what we know about Ms. Plaxton so that we decrease the number of
erroneous hits. The actual address in our database looks as follows:

::

    Jane Plxaton
    243 Roads Dr.
    Middle Town OH 49004

This is a little exaggerated for reasons of clarity, but what has
happened is that the data-entry operator has transposed the ’x’ and the
’a’ in ’Plaxton’ as well as the ’4’ and ’3’ and has also misspelled
’Rhodes’.

The query we performed has four sets:

- A 60% approximation of: “Jane Plaxton”
- A 60% approximation of: “234 rhoads dr.”
- The state string : OH
- The zip code string : 49004

The database records are separated by a blank line, therefore our start
and end delimiters will be ``'\n\n'`` (two new-line characters).

The Approximate pattern matcher will be looking for the name and street
address information and will match anything that comes within 60matcher
will default to 80regular-expression pattern matcher will be looking for
the state and zip-code strings. We are searching for three intersections
of the four sets (this is the default action).

Example 3:

We are reading the electronic version of the Wall Street Journal and we
are interested in locating any occurrence of profits and/or losses that
amount to more than a million dollars.

The query: ``+#>1,000,000  +dollar @0 profit loss gain``

The ``'+'`` symbol in front of the first two terms indicates that they
must be present in the hit. The ``'@0``\ ’ tells Metamorph to find zero
intersections of the following sets. Put another way, only one of the
remaining sets needs to be located.

The sets:

-  Mandatory (because of the ``'+'`` symbol):

   -  Any quantity in the text that is greater than one million.

   -  Any word (or string) that means “dollar”.

-  Permutation (because of the ``'@0'``):

   -  Anything that means “profit”.

   -  Anything that means “loss”.

   -  Anything that means “gain”.

We would probably define the delimiters to be either a sentence or a
paragraph.

The following would qualify as hits to this query:

-  Congress has spent 2.5 billion dollars on the stealth bomber.

-  Lockheed Corp. has taken a four million dollar contract from Boeing.

-  The Lottery income from John Q. Public last week was One Million Two
   Hundred and Fifty Thousand dollars and twenty five cents.

Potential Applications
----------------------

The nature of our API makes it possible to use Metamorph as a generic
text searching tool no matter where the text resides. Given the quantity
of text that exists on most computers the potential variants are
boundless, but here are some ideas:

-  A method of searching the text field information in databases
   (relational or otherwise).

-  Document management and control systems.

-  Document/record classification systems.

-  Real time text analysis.

-  E-Mail services.

-  Image classification/retrieval databases.

-  Message traffic management.

-  Educational/instructional aids.

-  Executive information systems.

-  Research analysis tools.

The Programmers’ Interface Overview
===================================

There are thousands of stand-alone Metamorph programs in the field
today, and over time we have received many requests by application
developers who would like to be able to embed our searching technology
inside their particular application. It has taken us a long time to
figure out how to provide a simple and clean method to provide a
solution to their problems. We have tried to make it as easy as possible
while providing the maximum power and flexibility.

All of the code that comprises Metamorph has been written in ANSI
compliant ’C’ Language. The source code to the API (only) is provided to
the programmer for reference and modification. Metamorph has currently
been compiled and tested on 22 different UNIX platforms, MS-DOS, and IBM
MVS. The API can be ported by Thunderstone to almost any Machine/OS that
has an ANSI compliant ’C’ compiler.

The set of calls in the API are structured in a fashion similar to
``fopen(), fclose(), ftell()``, and ``gets()``, standard library
functions. And just like you can have multiple files open at the same
time, you can open as many simultaneous Metamorph queries as needed.
(One reason you might do this is to have a different search in effect
for two different fields of the same record.)

The API itself allows the software engineer to conduct a Metamorph
search through any buffer or file that might contain text. There are two
data structures that are directly involved with the API:

::

    APICP     /* this structure contains all the control parameters */
    MMAPI       /* this structure is passed around to the API calls */

The APICP structure contains all the default parameters required by the
API. It is separate from the MMAPI structure so that its contents can be
easily manipulated by the developer. An APICP contains the following
information:

-  A flag telling Metamorph to do suffix processing

-  A flag telling it do prefix processing

-  A flag that says whether or not to perform word derivations

-  The minimum size a word may be processed down to

-  The list of suffixes to use in suffix processing

-  The list of prefixes to use in prefix processing

-  A start delimiter expression

-  An end delimiter expression

-  A flag indicating to include the starting delimiter in the hit

-  A flag indicating to include the ending delimiter in the hit

-  A list of high frequency words/phrases to ignore

-  The default names of the Thesaurus files

-  Two optional, user-written, Thesaurus list editing functions

-  The list of suffixes to use in equivs lookup

-  A flag indicating to look for the within operator (w/)

-  A flag indicating to lookup see references

-  A flag indicating to keep equivalences

-  A flag indicating to keep noise words

-  A user data pointer

Usually the developer will have no need to modify the contents of this
structure more than one time to tailor it to their application, but in
some applications it will be very desirable to be able to modify its
contents dynamically. Two calls are provided that handle the
manipulation of this structure:

::

    APICP * openapicp(void)             /* returns an APICP pointer */

    APICP * closeapicp(APICP *cp)  /* always returns a NULL pointer */

The ``openapicp()`` function creates a structure that contains a set of
default parameters and then returns a pointer to it. The ``closapicp()``
function cleans up and releases the memory allocated by the
``openapicp()`` function. Between these two calls the application
developer may modify any of the contents of the APICP structure.

There are five function calls that are associated with the actual API
retrieval function; they are as follows:

::

    MMAPI *openmmapi(char *query,APICP *cp)

    int   setmmapi(MMAPI *mm,char *query)

    char  *getmmapi(MMAPI *mm, char *buf, char *endofbuf, int operation)

    int   infommapi(MMAPI *mm, int index, char **what, char **where,
                    int *size)

    MMAPI *closemmapi(MMAPI *mm)

The ``openmmapi()`` function takes the set of default parameters from
the APICP structure and builds an MMAPI structure that is ready to be
manipulated by the other four functions. It returns a pointer to this
structure.

The ``setmmapi()`` function is passed a standard Metamorph query (see
examples) and does all the processing required to get the API ready to
perform a search that will match the query. If the application program
wishes to, it can define a function that will be called by the
``setmmapi()`` function to perform editing of the word lists and query
items before the initialization is completed (this is not required).

The ``getmmapi()`` function performs the actual search of the data. All
that is required is to pass the ``getmmapi()`` function the beginning
and ending locations of the data to be searched. There are two
operations that may be performed with the ``getmmapi()`` call;
``SEARCHNEWBUF`` and ``CONTINUESEARCH``. Because there may be multiple
hits within a single buffer, the ``search-new-buf`` command tells the
API to locate the first hit, and then by using successive calls with the
command ``continue-search`` you will locate all the remaining hits in
the buffer.

The ``infommapi()`` function returns information about a hit to the
caller; it will give the following information:

-  Where the hit is located within the buffer.

-  The overall length of the hit.

-  For each set in the search that was matched:

   #. The query set searched for and located.

   #. The location of the set item.

   #. The length of the set item.

-  The location and length of the start and end delimiters.

The ``closemmapi()`` function cleans up and releases the memory
allocated by the ``openmmapi()`` call.

The last of the important calls in the API is the function that reads
data in from files. While your application may not require this
function, if files are being read in as text streams the use of this
function is mandated.

::

    int rdmmapi(char *buf,int n,FILE *fh,MMAPI *mm)

This function works very much like ``fread()`` with one important
exception; it guarantees that a hit will not be broken across a buffer
boundary. The way it works is as follows:

-  A normal ``fread(``) for the number of requested bytes is performed.

-  ``rdmmapi()`` searches backwards from the end of the buffer for an
   occurrence of the ending delimiter regular-expression.

-  The data that is beyond the last occurrence of an ending delimiter is
   pushed back into the input stream. (The method that is used depends
   on whether an ``fseek()`` can be performed or not.)

The Metamorph 3 API Package
---------------------------

The Metamorph 3 API package consists of the following files:

::

    api3.doc   - this documentation
    lapi3.lib  - MS-DOS, for Microsoft 'C' large model
                 library containing all api functions
    libapi3.a  - Unix library containing all api functions
    api3.h     - header to be included by any program using Metamorph 3 api
    api3i.h    - header automatically included by api3.h
    mmsg.h     - header automatically included by api3.h
    api3.c     - source code to the top level api calls
    apimmsg.c  - source code to the default message handler
    mmex1.c    - example source implementing a text search interface
    mmex2.c    - example source implementing a database search interface
    mmex2.dat  - example database for mmex2.c
    readme.doc - system specific and installation notes

The Metamorph 3 API uses bytes and strings of bytes for most of its
character manipulations. “Byte” is defined, in the API header, as an
unsigned 8 bit quantity (unsigned char) and is used to allow greater
latitude in string contents.

All byte pointers are normal ``'C'`` strings (pointer to an array of
bytes, terminated by ``'\0'``).

All byte pointer lists are arrays of pointers to normal ``'C'`` strings.
Each list is terminated with an empty string ``((byte *)"")``.

**WARNING:** All APICP strings, string list members, and pointer arrays
will be freed by ``closeapicp`` if they are ``!=NULL``. This includes
the terminator ``("")`` in string lists.

The Metamorph API provides the following functions:

::

    closeapicp() - control parameters interface
    closemmapi() - cleanup
    closemmsg()  - close the message file
    fixmmsgfh()  - message control
    getmmapi()   - search routine
    infommapi()  - hit information
    openapicp()  - control parameters interface
    openmmapi()  - initialization
    putmsg()     - message handler
    rdmmapi()    - synchronized read
    setmmapi()   - reinitialization

The minimum set of function calls you will use is:

::

    closeapicp()
    closemmapi()
    getmmapi()
    openapicp()
    openmmapi()

The Metamorph 3 API needs 3K of stack space in addition to whatever the
calling program uses.

Metamorph 3 API functions
=========================

::

    #include <stdio.h>
    #include "api3.h"

    APICP * openapicp(void)

    APICP * closeapicp(cp)
    APICP * cp;

``Openapicp`` returns a pointer to a structure that contains all of the
default parameters needed by the Metamorph API. Each of the members of
the structure are initialized in a manner that will allow for simple
modification of its contents by the calling program. ``Closeapicp``
frees all the memory allocated by ``openapicp`` and returns an
``APICP *)NULL``.

The following describes how to modify each of the variable types within
the ``APICP`` structure:

::

    (byte)    : Direct assignment
                eg:  cp->suffixproc=(byte)1;

    (int)     : Direct assignment
                eg: cp->minwordlen=2;

    (byte *)  : Free original pointer and assign new allocated pointer
                eg: free(cp->sdexp);
                    cp->sdexp=(byte *)malloc(strlen("string")+1);
                    strcpy(cp->sdexp,"string");

    (byte **) : Free original pointers and assign new allocated pointers
                eg: #define MYLISTSZ 3
                    static char *mylist[MYLISTSZ]={"new","list",""};
                    int i;
                    for(i=0;*cp->noise[i]!='\0';i++)
                         free(cp->noise[i]);
                    free(cp->noise[i]);     /* free empty string at end */
                    free(cp->noise);          /* free the array pointer */
                    cp->noise=(byte **)calloc(MYLISTSZ,sizeof(byte *));
                    for(i=0;i<MYLISTSZ;i++)
                         {
                          cp->noise[i]=(byte *)malloc(strlen(mylist[i])+1);
                          strcpy(cp->noise[i],mylist[i]);
                         }

    int (*)() : Direct assignment
                eg:   cp->eqedit=myeditfunction;

**WARNING:** The ``closeapicp()`` will free all variable pointers. Do
not assign static data pointers or attempt to free any pointers placed
in the ``APICP`` structure.

APICP Variable Definitions
--------------------------

The following fields are defined in the ``APICP`` structure:

-  | ``suffixproc``
   | Do suffix stripping processing

-  | ``prefixproc``
   | Do prefix stripping processing

-  | ``rebuild``
   | Perform the morpheme rebuild check

-  | ``incsd``
   | Include the start delimiter in the hit

-  | ``inced``
   | Include the end delimiter in the hit

-  | ``withinproc``
   | Look for within operator (``w/..``)

-  | ``suffixrev``
   | Internal Thunderstone use: Strings in suffix list are reversed

-  | ``minwordlen``
   | Minimum remaining length of a pre/suffix stripped word

-  | ``intersects``
   | Number of intersections to be located in the hit

-  | ``sdexp``
   | The start delimiter expression

-  | ``edexp``
   | The end delimiter expression

-  | ``query``
   | Query from user

-  | ``set``
   | Array of sets of things being searched for, in equiv format; sets
     are in original query order

-  | ``suffix``
   | The list of suffixes

-  | ``suffixeq``
   | The list of suffixes for equivalence lookup

-  | ``prefix``
   | The list of prefixes

-  | ``noise``
   | The list of words that constitute “noise”

-  | ``eqprefix``
   | The Path-filename of the main equiv file

-  | ``ueqprefix``
   | The Path-filename of the user equiv file

-  | ``see``
   | Lookup “see also” references

-  | ``keepeqvs``
   | Keep equivalences

-  | ``keepnoise``
   | Keep noise words

-  | ``eqedit``
   | A user programmable equiv edit function

-  | ``eqedit2``
   | A user programmable equiv edit function A user settable data
     pointer

-  | ``denymode``
   | ``API3DENY``... mode: how to deny query-protection-forbidden
     actions

-  | ``al...``
   | Flags for allowing/denying query-protection actions

-  | ``qmin``..., ``qmax``...
   | Query-protection limits

-  | ``defsuffrm``
   | Whether to remove a trailing vowel, or one of a trailing double
     consonant pair, after normal suffix processing, and if the word is
     still ``minwordlen`` or greater. This only has effect if suffix
     processing is enabled (``suffixproc`` on and the original word is
     at least ``minwordlen`` long)

-  | ``reqsdelim``
   | Flag indicating start delimiter must be present

-  | ``reqedelim``
   | Flag indicating end delimiter must be present

-  | ``olddelim``
   | Flag indicating old delimiter behavior should be used

-  | ``withincount``
   | Value of integer ``N`` if within operator was “``w/N``”

-  | ``phrasewordproc``
   | Phrase word processing mode (``API3PHRASEWORD``... value)

-  | ``textsearchmode``
   | The ``TXCFF`` mode for text searches

-  | ``stringcomparemode``
   | The ``TXCFF`` mode for string comparisons

-  | ``setqoffs``
   | List of offsets into original user query, corresponding to
     ``set``\ s

-  | ``setqlens``
   | List of lengths in original user query, corresponding to ``set``\ s

-  | ``originalPrefixes``
   | List of set-logic, tilde, open-parenthesis, pattern-matcher
     character prefixes in original query, corresponding to ``set``\ s;
     NULL-terminated

-  | ``sourceExprLsts``
   | Each ``sourceExprLists`` item corresponds to a ``set`` item, and is
     a list of source expressions/terms (before equivalence etc.
     processing) from original query for that set; NULL-terminated

**NOTE:** See Metamorph chapter [chp:mmling] for detailed descriptions
of what many of these variables do.

Application Notes
-----------------

Generally speaking, the user program will have little need to modify the
contents of the ``APICP`` structure returned by ``openapicp()``. If the
user wishes to permanently modify one or more of the default parameters
it is far easier to directly edit and recompile the ``api3.c`` file.

The user ``eqedit`` and ``eqedit2`` functions are intended for those
applications that wish to process the results of the command
line/thesaurus lookup process before the remainder of the
``open/setmmapi()`` processing occurs. This has a similar role to the
````\ ‘EDIT’’‘ knob inside the Metamorph user interface. For more
information see the ``openmmapi()`` and ``setmmapi()`` documentation.

::

    #include <stdio.h>
    #include "api3.h"

    MMAPI * openmmapi(query,cp)
    char  * query;
    APICP * cp;

    MMAPI * closemmapi(mm)
    MMAPI * mm;

``Openmmapi`` performs the initialization required to perform a
Metamorph query. It returns a pointer to a structure that will be
required by the ``getmmapi, setmmapi,`` and ``closemmapi`` functions.

``Openmmapi`` requires two parameters. The first parameter is the user’s
query. The query is a ``'\0'`` terminated string which has exactly the
same syntax as a query would have within the Metamorph User Interface
with the exception that there is no macro facility. Internally
``openmmapi`` calls ``setmmapi`` if the query is not ``(char *)NULL``.
If the query is ``(char *)NULL`` it is up to the programmer to call
``setmmapi`` before calling ``getmmapi``. The second parameter is the
``APICP`` pointer returned by a successful call to ``openapicp()``.

::

    setmmapi(), openapicp()

::

    #include <stdio.h>
    #include "api3.h"

    MMAPI * setmmapi(mm,query)
    MMAPI * mm;
    char  * query;

``setmmapi()`` takes a pointer to an open ``MMAPI`` and a query string.
The query is a ``'\0'`` terminated string which has exactly the same
syntax as a query would have within the Metamorph User Interface with
the exception that there is no macro facility.

The query will be parsed using the ``APICP`` variables from the
``openmmapi()`` call, and following the rules described under “Query
processing and Equivalence lookup”.

``setmmapi()``, or ``openmmapi()`` with a ``non-(char *)NULL`` query,
must be called before making calls to ``getmmapi()``.

``setmmapi()`` returns the mm pointer passed if successful or
``MMAPIPN`` if there was an error.

``openmmapi()``

::

    #include <stdio.h>
    #include "api3.h"

    char  * getmmapi(mm,buf_start,buf_end,operation)
    MMAPI * mm;
    char  * buf_start;
    char  * buf_end;
    int     operation;

The ``getmmapi()`` is passed the ``MMAPI *`` returned by ``openmmapi()``
function and performs the actual search of the data pointed to by the
``buf_start`` and ``buf_end`` pointers. The operation parameter can
``buf_end``. Successive calls to getmmapi() with the operation be one of
two values: ``SEARCHNEWBUF`` or ``CONTINUESEARCH``. ``getmmapi()`` will
return a ``(char *)NULL`` if it does not locate a hit within the buffer.
If a hit is located it will return a pointer to the beginning of the
hit.

If ``getmmapi()`` is called with the operation parameter set to
``SEARCHNEWBUF``, it will begin its search at ``buf_start`` and search
through the buffer until it locates a hit or until it reaches
``buf_end``. Successive calls to ``getmmapi()`` with the operation
parameter set to ``CONTINUESEARCH`` will locate all remaining hits
within the bounds set by ``buf_start`` and ``buf_end``.

Typically the sequence of events would look as follows:

::

    {
     char *hit;
     char *my_buffer;
     int   my_buf_size;

     MMAPI *mm;

     ...

     for(hit=getmmapi(mm,my_buffer,my_buffer+my_buf_size,SEARCHNEWBUF);
         hit!=(char *)NULL;
         hit=getmmapi(mm,my_buffer,my_buffer+my_buf_size,CONTINUESEARCH)
        )
        {
         /* process the hit here */
        }
     ...

    }

::

    infommapi()

::

    #include <stdio.h>
    #include "api3.h"

    int     infommapi(mm, index, what, where, size)
    MMAPI  *mm;
    int     index;
    char  **what;
    char  **where;
    int    *size;

After a hit has been located by the ``getmmapi()`` function, the calling
program may get information about objects contained within the hit by
passing the ``MMAPI *`` to the ``infommapi()`` function. This call can
provide the following information:

-  Location and length of the entire hit.

-  Location and length of the start delimiter.

-  Location and length of the end delimiter.

-  For each set in the search that was matched:

   -  The query set searched for and located.

   -  The location of the set item.

   -  The length of the set item.

The idea behind ``infommapi()`` is to provide the caller with a
structured method for obtaining information about a hit that was located
with the ``getmmapi()`` call. The index parameter and the return code
are used to “walk” through the items that were located. Information
about each item is placed into the variables pointed to by the what,
where and size parameters. A return value of ``-1`` indicates a usage
error, ``0`` indicates that the index is out of range, and ``1``
indicates that the index was in range and the data is valid.

Index values and what they return:

::

    infommapi(mm, 0, &what, &where, &size)
    what : Will be set to the query that was passed to the openmmapi()
           call.
    where: Will point to the location of the hit within the buffer being
           searched.
    size : Will be the overall length in bytes of the located hit.

    infommapi(mm, 1, &what, &where, &size)
    what : Will be set to the start delimiter expression in use.
    where: Will point to the location of start delimiter.
    size : Will be the overall length in bytes of the located delimiter.
           size will be 0 and where will be (char *)NULL if the hit is at
           the beginning of the buffer or immediately after the previous
           hit.

    infommapi(mm, 2, &what, &where, &size)
    what : Will be set to the end delimiter expression in use.
    where: Will point to the location of end delimiter.
    size : Will be the overall length in bytes of the located delimiter.
           size will be 0 and where will be (char *)NULL if the hit is at
           the end of the search buffer and no end delimiter was found in
           the buffer.

    infommapi(mm, [3...n], &what, &where, &size)
    what : Will point to the first "set" being searched for;

    set type    what points to
    --------    --------------------------
    REX         A regular expression
    NPM         The npm query expression
    PPM         The root word of the list of words
    XPM         The "approximate" string

    where: Will point to the buffer location of the set-element.
    size : Will be the overall length in bytes of the located set-element.

::

    {
     MMAPI  *mm;
     char   *what, *where;
     int    size, index;

     ...

      for (index = 0;
           infommapi(mm, index, &what, &where, &size) == 1;
           index++)
          {
           switch (index)
               {
                case 0 :
                   printf("The Query: %s\n", what);
                   printf("The hit  :");
                   for( ; size > 0; size--, where++) putchar(*where);
                   putchar('\n');
                   break;
                case 1 :
                   printf("The start delimiter expression: %s\n", what);
                   printf("The start delimiter located   :");
                   for( ; size > 0; size--, where++) putchar(*where);
                   putchar('\n');
                   break;
                case 2 :
                   printf("The end delimiter expression: %s\n", what);
                   printf("The end delimiter located   :");
                   for( ; size > 0; size--, where++) putchar(*where);
                   putchar('\n');
                   break;
                default:
                   printf("set %d expression: %s\n", index - 2, what);
                   printf("The set located  :");
                   for( ; size > 0; size--, where++) putchar(*where);
                   putchar('\n');
                   break;
               }
          }
     ...

    }

::

    getmmapi()

::

    int    rdmmapi(buf,bufsize,fp,mp)
    char  *buf;
    int    bufsize;
    FILE  *fp;
    MMAPI *mp;

    bool freadex_strip8;

::

    buf            where to put the data
    bufsize        the maximum number of bytes that will fit in buf
    fp             the file to read from which must be opened binary ("rb")
    mp             the Metamorph 3 API to synchronize for

    freadex_strip8 controls whether the high bit will be stripped from
                   incoming data

This function works very much like ``fread()`` with one important
exception; it guarantees that a hit will not be broken across a buffer
boundary. The way it works is as follows:

#. A normal ``fread()`` for the number of requested bytes is performed.

#. ``rdmmapi()`` searches backwards from the end of the buffer for an
   occurrence of the ending delimiter regular expression.

#. The data that is beyond the last occurrence of an ending delimiter is
   pushed back into the input stream. (The method that is used depends
   on whether an ``fseek()`` can be performed or not.)

If the ``freadex_strip8`` global variable is non-zero the 8th bit will
be stripped off all of the incoming data. This is useful for reading
WordStar(C) and other files that set the high bit. Setting
``freadex_strip8`` incurs a speed penalty because every byte read gets
stripped. Don’t use this flag unless it is absolutely necessary.

``rdmmapi()`` should be used any time you are doing delimited searches.
An unsynchronized read can cause hits to be missed.

``rdmmapi()`` returns the number of bytes actually read into ``buf`` or
``(-1)`` if there was an error.

::

    #include <stdio.h>
    #include ``api3.h''
    #include ``mmsg.h''
    #include ``cgi.h''

    int  putmsg(msgn,fn,fmt,...)
    int  msgn;
    char *fn;
    char *fmt;

    FILE *mmsgfh;
    char *mmsgfname;

::

    msgn  is the number of the message or (-1).
    fn    is the name of the function issuing the message or (char *)NULL.
    fmt   is the htpf() format (similar to printf() but extended).
    ...   is the argument list for fmt if necessary.

These functions handle all output from the Metamorph API. The API
reports its status periodically at points of interest. Each message has
a number associated with it that indicates what type of message it is.
Left alone the Metamorph API will generate message file output just like
the Metamorph 3 product.

Messages consist of four basic parts:

#. the message number followed by a space

#. the text of the message

#. the name of the function issuing the message

#. a newline.

Message numbers are broken into various levels or types. The levels are
grouped in hundreds. The levels are:

::

    000-099  messages indicate total failure of the process
    100-199  messages indicate potential failure or hazard to the process
    200-299  messages are informative messages on the operation of a process
    300-399  messages are hit information coming from a Metamorph 3 engine
    400-499  messages are non-error messages coming from a mindex engine
    500-599  messages about query/hit logic
    600-699  query information/debugging info
    700-999  undefined as yet (reserved)

**Output formatting:**

``putmsg()`` will output msgn formatted with ``%03d`` if ``msgn!=(-1)``,
followed by the results of fmt and its arguments if
``fmt!=(char *)NULL``, followed by fn formatted with “in the function:
``%s``” if ``fn!=(char *)NULL``, followed by a newline. The output
buffer is flushed to disk after every message so that any process
reading the message file will always be able to get the latest messages.

**Summary of formatting control:**

::

    to suppress msgn : pass -1
    to suppress fn   : pass (char *)NULL
    to suppress fmt  : pass (char *)NULL

**Output destination:**

``mmsgfh`` and ``mmsgfname`` control where ``putmsg(``) will send its
output. Each time ``putmsg()`` or ``datamsg()`` is called they will
attempt to make ``mmsgfh`` point to a legal file, named by
``mmsgfname``, and send their output there. Setting ``mmsgfh`` is
accomplished by the function ``fixmmsgfh()`` (See ````\ ‘putmsg()‘
extensions’’). How it works is described below.

If ``mmsgfh`` becomes ``(FILE *)NULL`` or the name pointed to by
``mmsgfname`` changes ``mmsgfh`` will be closed, if it was not
``(FILE *)NULL``, and reopened for binary append with the new
``mmsgfname``. If the open fails ``mmsgfname`` will be set to point to
``""``,the empty string, ``mmsgfh`` will be set to ``stderr``, and a
warning message will be be issued via ``putmsg()``. Only the first 127
characters in ``mmsgfname`` will be remembered between calls, so changes
beyond that point will not be noticed.

If you want to set ``mmsgfh`` yourself and not have it changed set
``mmsgfname`` to ``(char *)NULL``. This will preempt the checks
described above. ``mmsgfh`` will, however, be checked for
``(FILE *)NULL`` and will be reset to ``stderr`` if it is.

The initial setting for ``mmsgfh`` is (``FILE *)NULL`` and the initial
setting for ``mmsgfname`` is ``(char *)NULL``. This will, by default,
cause all output to go to ``stderr``.

::

    call:
       putmsg(MERR,"parse expression","invalid escapement");
    output:
       000 invalid escapement in the function parse expression\n

    call:
       putmsg(-1,"parse expression","invalid escapement");
    output:
       invalid escapement in the function parse expression\n

    call:
       char *filename="myfile";
       putmsg(MERR+FOE,(char *)NULL,"can't open file %s",filename);
    output:
       002 can't open file myfile\n

``putmsg()`` returns ``0`` for success or ``-1`` if there was an error
writing the output file. If there was an error the standard library
variable errno may be checked for the reason. The output file will *not*
be closed if there is an error.

``putmsg()`` may be overridden by writing your own function with the
same name, arguments and return value so that messages can be handled in
an application specific manner.

When ``putmsg()`` outputs a newline it will be the correct type for the
host operating system (CRLF on MS-DOS, LF on Unix).

**MS-DOS applications:**

MS-DOS does not allow real multi-tasking so the contents of the message
file will not become available to another program until the message file
is closed (this is an MS-DOS limitation). To read the message file while
a search is in progress you must access ``mmsgfh`` directly. If you move
the ``mmsgfh`` file position by seeking, remember to reposition it to
the end with ``fseek(mmsgfh,0L,SEEK_SET)`` before allowing the Metamorph
3 API to continue.

::

    apimmsg.c, apimmsg.h for specific message numbers and their macros
    putmsg() extensions
    putmsg() replacement

::

    void closemmsg(void)

    void fixmmsgfh(void)

These are some useful extensions to the ``putmsg()`` family of functions
that provide more flexibility to programmers. They are not used directly
by the Metamorph 3 API.

``closemmsg()`` closes the message file and should be called before
exiting any application that uses ``putmsg()`` or the Metamorph 3 API.
It may also be called in the middle of an application to flush the
message file buffers to disk or force the message file to be reopened on
the next call to ``putmsg()``. If mmsgfh is ``stderr``, it will not be
closed, but the next ``putmsg()`` call will still force a reopen. It is
safe to call ``closemmsg()`` at any time because it will not attempt to
close a file that is already closed or has never been opened.

``fixmmsgfh()`` is called by ``putmsg()`` before any output is
attempted. It guarantees that ``mmsgfh`` points somewhere legal based on
``mmsgfname``. See “Output destination” in the ``putmsg()`` description.
``fixmmsgfh()`` will probably not be needed by API users because
``putmsg()`` supplies ample output functionality.

If you use any of these functions and replace ``putmsg()`` you will have
to write your own replacements for the extensions. All of the
``putmsg()`` functions are in the same object module within the library.
Therefore, calling any one of the functions will cause all of them to be
brought in by the linker, which will then cause a clash if you have your
own version of ``putmsg()``.

::

    putmsg()
    putmsg() replacement

Normally all Metamorph 3 API output goes to ``stderr`` or a disk file.
But, depending on your application, this may not be desirable. You may
wish to send all messages to a special alternate window under a
graphical environment or process the messages as they occur and take
immediate action based on the type of message. You may also want to
filter the messages so that only errors and warnings get displayed.
Whatever your reason, ``putmsg()`` may be replaced.

Since ``putmsg()`` takes a variable number of arguments it must be
written using ``vararg``\ s or the ANSI ``stdarg``, if you prefer (see
your ’C’ manual). Only the v\ ``arargs`` method will be documented here.
The core is the same either way; the only variation is how you go about
getting the function arguments.

There are three arguments that are always present. The first argument is
a message number of type ``(int)``. The second argument is a function
name of type ``(char *)``. The third argument is a ``htpf()`` format
string of type ``(char *)``. ``htpf()`` is a Thunderstone function
similar to ``printf()``, but with extended flags and codes: the ``fmt``
argument should always be printed with an ``htpf()``-family function and
not ``printf()``-family because some messages may utilize these extended
flags and codes. Any remaining arguments are as required by the
``htpf()`` format string.

``putmsg()`` returns whether there was an error outputting the message
or not. A return of ``0`` means there was not an error. A return of
``non-0`` means there was an error.

All of the macros needed for ``putmsg()`` are in the header
``"mmsg.h"``. ``"api3.h"`` automatically #include’s ``"mmsg.h"``. If you
put ``putmsg()`` in its own source file just use ``"mmsg.h"``. If you
put ``putmsg()`` in the same file as Metamorph API calls or call the API
from ``putmsg()`` use ``"api3.h"``.

::

    /*
    ** This implementation will *ONLY* output errors(MERR) and
    ** warnings(MWARN).
    ** It will output "ERROR:" or "WARNING:" instead of a message number.
    ** It will always send its output to stderr.
    ** Function names will not be printed.
    */

    #include <stdio.h>
    #include <varargs.h>        /* for variable argument list handling */
    #include "mmsg.h"                                   /* or "api3.h" */
    #include "cgi.h"                        /* for htvfpf()  prototype */

    int
    putmsg(va_alist)                    /* args: msgn,funcname,fmt,... */
    va_dcl       /* no semicolon allowed! - just the way varargs works */
    {
    va_list args;       /* for variable argument list handling         */
    int        n;       /* the message number (may be -1)              */
    char     *fn;       /* the function name (may be NULL)             */
    char    *fmt;       /* the htpf type format string (may be NULL) */
    int level;                               /* message hundreds level */

                                            /* get the fixed arguments */
       va_start(args);      /* initialize variable argument list usage */
       n  =va_arg(args,int   );                      /* message number */
       fn =va_arg(args,char *);                      /* function name  */
       fmt=va_arg(args,char *);                      /* htpf format    */

       if(n>=0){                          /* is there a message number */
          level=n-(n%100);           /* clear the tens and ones places */
                                     /* to get the hundreds level      */
          if(level==MERR || level==MWARN){ /* only do error or warning */
             if(level==MERR) fputs("ERROR: "  ,stderr);
             else            fputs("WARNING: ",stderr);
             if(fmt!=(char *)NULL){           /* is there message text */
                htvfpf(stderr,fmt,args);  /* print the message content */
                                          /* using the varargs version */
                                          /* of htpf(): htvfpf()       */
             }
             fputc('\n',stderr);                 /* print the new line */
          }
       }
       va_end(args);         /* terminate variable argument list usage */
       return(0);                                         /* return OK */
    }

The ``putmsg()`` extensions need not be replaced if you are not going to
use them because the API does not use them. If you do use any extensions
you must also replace the ones that you use to avoid linker clashes.

::

    putmsg()
    apimmsg.c, mmsg.h

Query processing and Equivalence lookup
---------------------------------------

Query processing and equivalence lookup occur in ``setmmapi()`` and
``openmmapi()`` if ``query!=(byte *)NULL``.

Control query parsing and equivalence lookup with the following APICP
variables:

::

    byte  *query
      : The user query interpret and get equivalences for.

    byte  *eqprefix
      : The main equivalence file name.

    byte  *ueqprefix
      : The user equivalence file name.

    byte   see
      : Flag that says whether to lookup see references or not.

    byte   keepeqvs
      : Flag that says whether to keep equivalences or not.

    byte   keepnoise
     : Flag that says whether to keep noise words or not.

    byte   withinproc
     : Flag that says whether to process the within operator (w/) or not.

    byte   suffixproc
     : Flag that says whether to do suffix processing or not.

    int    minwordlen
     : The smallest a word is allowed to get through suffix stripping.

    byte **suffixeq
     : The list of suffixes.

    byte **noise
     : The list of noise words.

    int  (*eqedit)(APICP *)
     : Equivalence editor function.

    int  (*eqedit2)(APICP *,EQVLST ***)
     : Equivalence editor function.

    void  *usr
     : An arbitrary user data pointer.

**NOTE:** Also see Metamorph chapter [chp:mmling] for further
descriptions of these variables.

::

    byte *query:

query is a pointer to a Metamorph query. This string typically comes
directly from user input, but may be constructed or preprocessed by your
program. All rules of a Metamorph query apply.

-  REX patterns are prefixed by ``'/'``.

-  XPM patterns are prefixed by ``'%'``.

-  NPM patterns are prefixed by ``'#'``.

-  Required sets are prefixed by ``'+'``.

-  Exclusive sets are prefixed by ````\ ’-’‘.

-  Normal sets are prefixed by ``'='`` or nothing.

-  Intersection quantities are prefixed by ``'@'``.

-  Equivalence lookup may be prevented/forced on an individual word or
   phrase by prefixing it with ``'~'``.

-  Commas will be treated as whitespace except when part of a pattern
   (REX, XPM, or NPM).

-  Phrases or patterns with spaces in them that should be treated as a
   unit are surrounded by double quotes (``'"'``).

-  Noise stripping is controlled by the keepnoise flag (see below).

-  Equivalence lookup may be completely turned off by setting eqprefix
   to ``(byte *)NULL`` (see below). Turning off equiv lookup does not
   affect query parsing as described above.

-  New delimiters may be specified using the within operator (w/).

::

    byte *eqprefix:

This string contains the name of the main equivalence file. This
typically includes the full path but may have a relative path or no path
at all. The equivs may be relocated or even renamed.

Default ``eqprefix`` ``"builtin"`` which refers to a compiled in equiv
file.

This default may be permanently adjusted by changing the macro
``API3EQPREFIX`` in the ``api3.h`` header file and recompiling
``api3.c`` and replacing the resultant object file in the library.

Equivalence lookup may be completely turned off by setting ``eqprefix``
to ``(byte *)NULL``. Sometimes it is not appropriate to get the
associations from the equiv file or you may want to run your application
without the disk space overhead of the equiv file which is very large
(around 2 megabytes). Turning off equiv lookup does not affect query
parsing as described previously.

::

    byte *ueqprefix:

This string contains the name of the user equivalence file. This
typically includes the full path but may have a relative path or no path
at all. The equivs may be relocated or even renamed.

Default ``ueqprefix`` for Unix :``"/usr/local/morph3/eqvsusr"`` Default
``ueqprefix`` for MS-DOS:\ ``"c:\morph3\eqvsusr"``

This default may be permanently adjusted by changing the macro
``API3UEQPREFIX`` in the ``api3.h`` header file and recompiling
``api3.c`` and replacing the resultant object file in the library.

Equivalences in the user equiv file edit and/or override those in the
main equiv file.

::

    byte withinproc:

Process the within operator ``(w/)``. The within operator allows
changing the start and end delimiters from the query line. The argument
of the within operator may be one of the built in names, a number
indicating character proximity, or a REX expression. The built in names
are:

::

    Name    Meaning     Expression
    sent    Sentence    \verb`[^\digit\upper][.?!][\space'"]`
    para    Paragraph   \verb`\x0a=\space+  `
    line    Line        \verb`$`
    page    Page        \verb`x0c`
    \#      Proximity   \verb`.{,#}`(where \# is the number of characters)

Any other string following the ``"w/"`` is considered a REX expression.
When using a REX expression with the within operator both start and
delimiters are set to the expression to set the end delimiter to a
different expression specify another within operator and expression.
e.g. `` "power w/tag:  w/$"`` will set the start delimiter to
``"tag:" `` and the end delimiter to ``"$"``.

By default both delimiters will be excluded from the hit when using a
REX with the within operator. To specify inclusion use a ``"W/"``
instead of ``"w/"``. You may specify different inclusion/exclusion for
the end delimiter without repeating the expression if you wish to use
the same expression for both. Simply use the ``"W/"`` or ``"w/"`` by
itself for the end delimiter. e.g. ``"power w/$$ W/"`` will set both
delimiters to ``"$$"`` but will exclude the start delimiter and include
the end delimiter.

The default value for ``withinproc`` is ``1``. This default may be
adjusted by changing the macro ``API3WITHINPROC`` in the ``api3.h``
header file and recompiling ``api3.c``.

See also the section “Reprogramming the Within Operator”.

::

    byte see:

Lookup “see” references in the equiv file. The equiv file has “see”
references much as a dictionary or thesaurus has. With this flag off
“see” references are left in the word list as is. With it on, those
references will be looked up and their equiv lists added to the list for
the original word. This can greatly increase the number of equivs and
abstraction for a given word. This is not needed in most cases.

The default value for see is ``0``. This default may be adjusted by
changing the macro ``API3SEE`` in the ``api3.h`` header file and
recompiling ``api3.c``.

::

    byte keepnoise:

Keep noise words. With this flag off any word in query, that is not part
of a larger phrase, that is also found in the noise array will be
removed from the list.

The default value for ``keepnoise`` is ``1``. This default may be
adjusted by changing the macro ``API3KEEPNOISE`` in the ``api3.h``
header file and recompiling ``api3.c``.

::

    byte keepeqvs:

Invert normal meaning of ``~`` . With this flag on words will not
normally have equivs. To get the equivs for a word use the ``~`` prefix.

The default value for ``keepeqvs`` is ``1``. This default may be
adjusted by changing the macro ``API3KEEPEQVS`` in the ``api3.h`` header
file and recompiling ``api3.c``.

Setting ``keepeqvs`` to\ `` 0`` does not eliminate looking for the equiv
file. See the ``eqprefix`` variable for how to eliminate the equiv file
completely.

::

    byte suffixproc:

This is a flag that, if not set to ``0``, will cause the equiv lookup
process to strip suffixes from query words and words from the equiv file
to find the closest match if there is not an exact match. Words will not
be stripped smaller than the ``minwordlen`` value (see below). This flag
has a similar effect on the search process (see Metamorph section
[set:presuf]).

The default value for ``suffixproc`` is ``1``. This default may be
adjusted by changing the macro ``API3SUFFIXPROC`` in the ``api3.h``
header file and recompiling ``api3.c``.

::

    byte **suffixeq:

This is the list of word endings used by the suffix processor if
``suffixproc`` is on (see the description of lists). The suffix
processor also has some permanent built in rules for stripping. This is
the default list:

::

    '   s  ies

The default may be changed by editing the ``suffixeq[]`` array in the
function ``openapicp()`` in the file ``api3.c`` and recompiling.

::

    int minwordlen:

This only applies if ``suffixproc`` is on. It is the smallest that a
word is allowed to get before suffix stripping will stop and give up.

The default value for ``minwordlen`` is ``5``. This default may be
adjusted by changing the macro ``API3MINWORDLEN`` in the ``api3.h``
header file and recompiling api3.c. This flag has a similar effect on
the search process (see Metamorph section [set:minwrdlen]).

::

    byte **noise:

This is the default noise list:

+-------------+---------------+-----------+--------------+--------------+-------------+
| a           | between       | got       | me           | she          | upon        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| about       | but           | gotten    | mine         | should       | us          |
+-------------+---------------+-----------+--------------+--------------+-------------+
| after       | by            | had       | more         | so           | very        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| again       | came          | has       | most         | some         | was         |
+-------------+---------------+-----------+--------------+--------------+-------------+
| ago         | can           | have      | much         | somebody     | we          |
+-------------+---------------+-----------+--------------+--------------+-------------+
| all         | cannot        | having    | my           | someone      | went        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| almost      | come          | he        | myself       | something    | were        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| also        | could         | her       | never        | stand        | what        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| always      | did           | here      | no           | such         | whatever    |
+-------------+---------------+-----------+--------------+--------------+-------------+
| am          | do            | him       | none         | sure         | what’s      |
+-------------+---------------+-----------+--------------+--------------+-------------+
| an          | does          | his       | not          | take         | when        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| and         | doing         | how       | now          | than         | where       |
+-------------+---------------+-----------+--------------+--------------+-------------+
| another     | done          | i         | of           | that         | whether     |
+-------------+---------------+-----------+--------------+--------------+-------------+
| any         | down          | if        | off          | the          | which       |
+-------------+---------------+-----------+--------------+--------------+-------------+
| anybody     | each          | in        | on           | their        | while       |
+-------------+---------------+-----------+--------------+--------------+-------------+
| anyhow      | else          | into      | one          | them         | who         |
+-------------+---------------+-----------+--------------+--------------+-------------+
| anyone      | even          | is        | onto         | then         | whoever     |
+-------------+---------------+-----------+--------------+--------------+-------------+
| anything    | ever          | isn’t     | or           | there        | whom        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| anyway      | every         | it        | our          | these        | whose       |
+-------------+---------------+-----------+--------------+--------------+-------------+
| are         | everyone      | just      | ourselves    | they         | why         |
+-------------+---------------+-----------+--------------+--------------+-------------+
| as          | everything    | last      | out          | this         | will        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| at          | for           | least     | over         | those        | with        |
+-------------+---------------+-----------+--------------+--------------+-------------+
| away        | from          | left      | per          | through      | within      |
+-------------+---------------+-----------+--------------+--------------+-------------+
| back        | front         | less      | put          | till         | without     |
+-------------+---------------+-----------+--------------+--------------+-------------+
| be          | get           | let       | putting      | to           | won’t       |
+-------------+---------------+-----------+--------------+--------------+-------------+
| became      | getting       | like      | same         | too          | would       |
+-------------+---------------+-----------+--------------+--------------+-------------+
| because     | go            | make      | saw          | two          | wouldn’t    |
+-------------+---------------+-----------+--------------+--------------+-------------+
| been        | goes          | many      | see          | unless       | yet         |
+-------------+---------------+-----------+--------------+--------------+-------------+
| before      | going         | may       | seen         | until        | you         |
+-------------+---------------+-----------+--------------+--------------+-------------+
| being       | gone          | maybe     | shall        | up           | your        |
+-------------+---------------+-----------+--------------+--------------+-------------+

The default may be changed by editing the ``noise[]`` array in the
function ``openapicp()`` in the file ``api3.c`` and recompiling.

::

    void *usr:

This is a pointer that the application programmer my use as a method of
passing arbitrary application specific information to the callback
functions ``(*eqedit)()`` and ``(*eqedit2)()``. This pointer is entirely
under the control of the programmer. The Metamorph API does not
reference it in any way except to set it to ``(void *)NULL`` in
``openapicp()``.

Equivalence editing callbacks
-----------------------------

During query processing, ``setmmapi()`` will call two user callback
functions to perform editing on the query terms. The processing sequence
is as follows:

#. parse the query and lookup terms in equiv file.

#. build eqvlist for eqedit2.

#. \* call (\*eqedit2)().

#. check for empty or NULL list.

#. check for and remove duplication in set lists.

#. set intersections if not already set (<0).

#. build formatted sets for (\*eqedit)() from eqvlist.

#. free eqvlist.

#. \* call (\*eqedit)().

#. perform rest of internal setup.

#. return to caller.

``(*eqedit2)()`` is the recommended method for implementing on the fly
equiv editing because it is easier to use. ``(*eqedit)()`` is available
for backwards compatibility.

::

    int (*eqedit2)(APICP *,EQVLST ***):

This function is always called after a successful equiv lookup and
before the search begins. It is called with the current ``APICP``
pointer and a pointer to the list of equivs generated by the query (see
the description of lists). The list pointer may be reassigned as needed.

The return value from ``(*eqedit2)()`` determines whether to go ahead
with the search or not. A return value of ``0`` means OK, go ahead with
the search. A return value of anything else means ``ERROR``, don’t do
search. An ``ERROR`` return from ``(*eqedit2)()`` will then cause
``setmmapi()`` or ``openmmapi()``, depending on where it was called
from, to return an error. A ``NULL`` list from ``(*eqedit2)()`` is also
considered an error.

There is one ``EQVLST`` for each term in the query. The array of
``EQVLSTs`` is terminated by an ``EQVLST`` with the words member set to
``(char *)NULL`` (all other members of the terminator are ignored). The
``EQVLST`` structure contains the following members:

::

    char   logic: the logic for this set
    char **words: the list of terms including the root term
    char **clas : the list of classes for `words'
    int    sz   : the allocated size of the `words' and `clas' arrays
    int    used : the number used (populated) of the `words' and `clas'
                  arrays, including the terminating empty string ("")
    int    qoff : the offset into user's query for this set (-1 if unknown)
    int    qlen : the length in user's query for this set (-1 if unknown)
    char   *originalPrefix:  set logic/tilde/open-paren/pattern-matcher
    char   **sourceExprs: NULL-terminated list of source expressions for set

The ``words`` and ``clas`` arrays are allocated lists like everything
else in the ``APICP``, and are terminated by empty strings. The ``sz``
and ``used`` fields are provided so that editors may manage the lists
more efficiently.

The ``words`` and ``clas`` lists are parallel. They are exactly the same
length and for every item, ``words[i]``, its classification is
``clas[i]``.

The ``originalPrefix`` field (added in Texis version 6) contains the set
logic (“``+``”, “``-``”, “``=``”), tilde (“``~``”), open-parenthesis,
and/or pattern-matcher characters (“``/``” for REX, “``%``” for XPM,
“``#``” for NPM) present in the original query for this set, if any. It
can be used in reconstructing the original query, e.g. if the terms are
to be modified but set logic etc. should be preserved as given.

The ``sourceExprs`` field (added in Texis version 6) contains a list of
the source expressions or terms for the set, i.e. as given in the
original query. For SPM queries, this will be a single word or phrase.
For PPM queries given as parenthetical lists, this will be a list of the
individual terms or phrases. For REX/NPM/XPM queries, this will be the
expression (sans “``/``”/“``#``”/”``%``\ ”). For single terms that are
expanded by equivalence lookup, this will be the original single term,
*not* the expanded list (as ``words`` will be) – because ``sourceExprs``
is from the source (original query), not post-equivalence-processing.
Note also that ``sourceExprs`` is ``NULL`` (not empty-string)
terminated. The ``sourceExprs`` array can be used in reconstructing or
modifying queries.

The default function is the function ``nulleqedit2`` in ``api3.c`` which
does nothing and returns ``0`` for OK.

::

    int (*eqedit)(APICP *):

This function is always called after a successful equiv lookup and
before the search begins. It is called with the current ``APICP``
pointer with the “set” list in the APICP structure set to the list of
equivs generated by the query (see the description of lists).

The return value from ``(*eqedit)()`` determines whether to go ahead
with the search or not. A return value of ``0`` means OK, go ahead with
the search. A return value of anything else means ``ERROR``, don’t do
search. An ``ERROR`` return from ``(*eqedit)()`` will then cause
``setmmapi()`` or ``openmmapi()``, depending on where it was called
from, to return an error.

The format of the sets is:

::

    {-|+|=}word[;class][,equiv][...]

Or:

::

    {-|+|=}{/|%99|#}word

Where:

::

    []    surround optional sections.
    {}    surround required items to be chosen from.
    |     separates mutually exclusive items between {}.
    9     represents a required decimal digit (0-9).
    word  is the word, phrase, or pattern from the query.
    equiv is an equivalent for word.
    class is a string representing the classification for the
          following words.
    ...   means any amount of the previous item.

Classifications in the default thesaurus (case is significant):

::

    P = Pronoun         c = Conjunction
    i = Interjection    m = Modifier
    n = Noun            p = Preposition
    v = Verb            u = Unknown/Don't care

Words and phrases will be in the first format. Patterns will be in the
second format.

::

    =struggle;n,battle,combat,competition,conflict,compete;v,contest,strive
         battle, combat, competition, and conflict are nouns
         compete, contest, and strive are verbs
         struggle can be a noun or verb

    =status quo;n,average,normality
         status quo, average, and normality are nouns

    +Bush;P
         Bush is a pronoun

    -/19\digit{2}
         a REX pattern to find "19" followed by 2 digits

    =%80qadafi
         an XPM pattern to find qadafi within 80%

    =#>500
         an NPM pattern to find numbers greater than 500

Remember that each of the “set” strings is allocated. So if you replace
a set you must free the old one, to prevent memory loss, and use an
allocated pointer for the replacement because it will get freed in
``closeapicp()``, unless it is ``(byte *)NULL``.

The “set” format must be totally correct for the search process to work.

The default is the function ``nulleqedit`` in ``api3.c`` which does
nothing and returns ``0`` for OK.

User equivalence file maintenance
---------------------------------

A user equivalence file contains equivalences in a manner similar to the
main equiv file. The user equiv contains equivs that edit and/or replace
equivs in the main equiv. It may also contain new equivs.

Make a user equiv file by creating an ASCII file containing your desired
equiv edits. Then index that source file with backref program.

The user equiv source file has the following format:

-  The root word or phrase is the first thing on the line.

-  Hyphenated words should be entered with a space instead of a hyphen.

-  Subsequent words/phrases (equivs) follow on the same line prefixed
   with edit commands (see below).

-  Add optional classification information by appending a semicolon (;)
   and the class to the word to apply it to. Any specified
   classification is carried onto subsequent words until a new
   classification is entered.

-  Lines should be kept to a reasonable length; around 80 characters for
   standard screen display is prudent. In no case should a line exceed
   1K. Where more equivs exist for a root word than can be fit onto one
   line, enter multiple entries where the root word is repeated as the
   first item.

-  There should not be any blank lines. Lines should not have any
   leading or trailing spaces. Words or phrases also should not have any
   leading or trailing spaces.

-  A user equiv file may “chain” to another user equiv file by placing
   the key string “;chain;” followed by the name of the equiv source
   file to chain to on the first line of the source file. e.g.
   ``";chain;c:\morph3\eqvsusr"`` Equivs are looked up in the chained
   file before the current file so that a user may, for example,
   override system wide settings. Chains are resolved when the source
   file gets backreferenced.

Edit commands:
--------------

Comma ``(,)`` means add this word/phrase to the list of equivs for this
root.

Tilde ``(~)`` means delete this word from the list of equivs for this
root.

Equals ``(=)`` only applies for the first equiv specified for a root. It
means replace this entry with the following entry. The first word after
the equals is taken as the new root and the rest of the words are its
equivs. If the equals is followed by a non-alphanumeric character the
entire rest of the line is taken literally as a replacement for the
original entry. This is a macro like facility that allows you to make a
word mean a regular expression or other Metamorph special pattern match.

Once you have a user equiv source file index it with the backref
command. This syntax is:

::

    backref source_file indexed_file

Where ``source_file`` is the ASCII file you created. And
``indexed_file`` is the backreferenced and indexed file that you specify
in the ``ueqprefix`` variable. To just index the source file without
backreferencing it use the ``-l1`` option:

::

    backref -l1 source_file indexed_file

By convention the source file should have the same path and filename as
the indexed file with an extension of ``".lst"``. This is what the
Metamorph user interface expects. For example: the source file for
``"c:\morph3\eqvsusr"`` would be ``"c:\morph3\eqvsusr.lst"``

Sample user equiv file:

::

    chicken,bird,seed,egg,aviary,rooster
    seed;n,food,feed,sprout
    ranch,farm,pen,hen house,chicken house,pig sty
    Farmer's Almanac,research,weather forecast,book
    rose,flower,thorn,red flower
    water,moisture,dew,dewdrop,Atlantic Ocean
    bee pollen,mating,flower,pollination,Vitamin B
    grow,mature,blossom,ripen
    abort,cancel,cease,destroy,end,fail,kill
    abort,miscarry,nullify,terminate,zap
    wish;n,pie in the sky,dream;v,yearn,long,pine
    constellation~nebula~zodiac,big dipper
    abandon=abandon,leave
    galaxy=andromeda
    slice=slice
    lots=#>100
    bush=/\RBush

Reprogramming the Within Operator
---------------------------------

**NOTE:** The mechanism described here may be replaced with something
different in a future version of the Metamorph API.

The symbolic expressions that the within operator knows about may be
reprogrammed by the application developer. The within processor
maintains two lists of symbolic expressions: a “standard” list and a
“user” list. By default the standard list contains the symbols described
elsewhere in this document (line/page/etc). The user list is empty by
default.

The within processor processes the string after the ``"w/"`` in the
following order:

-  If the first character is a digit its a proximity count.

-  If it matches something in the user list, use its expression.

-  If it matches something in the standard list, use its expression.

-  Otherwise it’s taken literally as a rex expression.

Each list is made up of an array of ``MDPDLM`` structures

::

    MDPDLM {
       char *name;
       char *expr;
       int   incsd;
       int   inced;
    };

Where:

``name`` is the name used in the query with the ``"w/"`` operator.
``expr`` is the rex expression associated with name. ``incsd`` is a flag
indicating whether to include the start delimiter. ``inced`` is a flag
indicating whether to include the end delimiter.

The array is terminated with a ``MDPDLM`` with the name member set to
``(char *)NULL``.

The lists are manipulated with the ``mdpstd()`` and ``mdpusr()``
functions to control the standard and user lists respectively.

::

    MDPDLM *mdpstd(MDPDLM *);
    MDPDLM *mdpusr(MDPDLM *);

These functions set their respective lists to those provided by the
argument. They return the previous lists. Any list may be ``MDPDLMPN``
to suppress its processing. The list pointers are kept in a static
global variable within the api library, so all subsequent within
operators will be effected by any changes. The table is not copied, so
the pointers passed must remain valid for the duration of all api usage.

Comparisons to name need only match for the length of name, thus
allowing abbreviations. e.g. The following will both match for a name of
“mess”: w/message, w/messy.

::

    static MDPDLM mydelims[]={
       { "mess","^From:"         ,1,0 },           /* add a new name */
       { "page","-- \\digit+ --:",0,1 },/* override an existing name */
       { CHARPN }
    };
       ...
       /*
          you could call
             mdpstd(MDPDLMPN);
          to suppress the standard names so that only the usr names
          would be recognized.
       */
       mdpusr(mydelims);
       ...
       setmmapi(mm,query);
       ...

Low level Pattern Matchers
==========================

***Programmers note: Do not use these functions unless you are sure you
know why you need them. Thunderstone will *not* provide technical
support involving the ``((ab=normal ?)|mis)use`` of these functions.***

::

    FFS   *openrex(byte *s);
    FFS   *closerex(FFS *fs);
    FFS   *mknegexp(FFS *fs);
    byte  *getrex(FFS *fs,byte *buf,byte *end,int operation);
    int    rexsize(FFS *ex);

**XPM - Approximate Pattern Matcher**

::

    XPMS *openxpm(byte  *s,int threshold); /* 0 < threshhold < 101 */
    XPMS *closexpm(XPMS *xs);
    byte *getxpm(XPMS *xs,byte *buf,byte *end,int operation);

**PPM - Parallel String Pattern Matcher**

::

    PPMS *closeppm(PPMS *ps);
    PPMS *openppm(byte **sl);
    byte *getppm(PPMS *ps,byte *buf,byte *end,int operation);

**SPM - Single String Pattern Matcher**

::

    SPMS *openspm(char *s);
    SPMS *closespm(SPMS *fs);
    byte *getspm(SPMS *fs,byte *buf,byte *end,int operation);
    int   spmhitsz(SPMS *fs);

**NPM - Numeric Pattern Matcher**

::

    NPMS *opennpm(char *s);
    NPMS *closenpm(NPMS *);
    byte *getnpm(NPMS *,byte *,byte *,int);

These pattern matchers represent the core search algorithms that are
present in the **Metamorph** program and API. Usage and syntax details
are not presented here as they are described in great detail in the
**Metamorph User Manual**. The programmer is encouraged to understand
their purpose and usage before implementing their own software using
these functions.

All of the pattern matchers behave in a similar fashion from a
programming perspective. The ``open___()`` call initializes the matcher
and returns a “handle” to it, and the ``close___()`` call ``free()s``
the memory associated with that object’s “handle”. If the ``open___()``
call fails for any reason it will return a cast pointer to ``NULL``.

The arguments passed to the ``open___()`` call are as follows:

| xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxCall
  Parameters Type(s) Example
| ``openrex()`` Rex expression ``"\\x0d\\x0a+"``
| ``openxpm()`` any string, threshold ``"abc widgets",80``
| ``openppm()`` a list of strings ``{"abc","def","ghi",""};``
| ``opennpm()`` a numeric expression ``">1000<=million"``
| ``openspm()`` a string expression ``"abc*def"``

In all cases, the ``open___()`` call is computationally intensive, as
each algorithm makes extensive use of dynamic programming techniques. It
is generally considered that the pattern matcher will be processing a
great deal of information between it’s creation and destruction so that
the creation overhead is justified by the dramatic increase in search
rates.

If the pattern matchers are to be used in conjunction with one another
then the programmer should optimize usage by dispatching the pattern
matchers in order of relative speed. The following table enumerates the
relative search rates:

| xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxPattern
  Types Governing factors
| SPM Fastest longer strings are faster
| REX . longer expressions are usually faster
| PPM . shorter lists with the longest minimum strlen()
| XPM . shorter strings are faster
| NPM Slowest nothing makes a difference

The ``get___()`` call is responsible for the location of *hits* within a
buffer of information. All of the pattern matchers share a common
parameter set for this operation:

**(object pointer, byte \*buf , byte \*end, int operation)**

object pointer
    The structure pointer that was returned by the ``open___()`` call.

byte \*buf
    A pointer to the first character of the buffer to be searched  [1]_.

byte \*end
    A pointer to one character past the last character in the buffer to
    be searched. ( Usually obtained by the expression ``buf+bufsize``).

int operation
    This will be one of the four possible operation codes:
    ``SEARCHNEWBUF``,\ ``CONTINUESEARCH``, ``BSEARCHNEWBUF``, or
    ``BCONTINUESEARCH``.

The ``SEARCHNEWBUF`` to locate the first occurrence of a hit within the
delineated buffer, and the ``CONTINUESEARCH`` operation code indicates
that the pattern matcher should locate the next (or succesive)
occurrence. A pointer to the matched pattern will be returned or, a
``(byte *)NULL`` if no match was located within the buffer for the given
call.

The operation codes ``BSEARCHNEWBUF`` and ``BCONTINUESEARCH`` are only
understood by the REX pattern matcher, and are used to search backwards
from the ``end`` pointer towards the ``buf`` pointer. A non ``NULL``
return value will point to the beginning of the matched pattern.

Some information about the matched hits for each of the pattern matchers
may be obtained by looking into that pattern matcher’s structure. The
following structure members are the only valid ones for an API
programmer to use:

::

    NPMS /* Numeric Pattern Matcher */
    {
     int hitsz;  /* the length of the matched quantity */
     double hy;  /* the maximum numeric value of the matched string */
     double hx;  /* the minimum numeric value of the matched string */
    };

::

    PPMS
    {
     byte **slist;  /* the strings being sought */
     int sn;        /* the index of the located string within slist */
    };

::

    XPMS
    {
     word thresh;           /* threshold above which a hit can occur */
     word maxthresh;        /* the max possible value a match may have */
     word thishit;          /* the value of this match */
     word maxhit;           /* max value located so far */
     byte maxstr[DYNABYTE]; /* the string with highest match value */
    };

::

    SPMS
    {
     /* no usable members */
    };

::

    FFS  /* aka REX */
    {
     /* no usable members */
    };

Hit length information for REX and SPM is available through calls to
``rexsize()`` and ``spmhitsz()`` respectively. Each of these functions
return the length of the last hit located by a call to ``get___()``. The
reason there are not similar calls available for the othe pattern
matchers is because their length is obtainable via the structure.

::

    #include "api3.h"

           /* this code breaks some rules in the interest of brevity */

    main(argc,argv)
    int    argc;
    char **argv;
    {
     void *pm=(void *)NULL;
     int   i;
     void (*close)();                    /* pointer to the close function */
     void (*get)();                        /* pointer to the get function */
     char ln[80];

     switch(*argv[1])      /* determine search type via leading character */
        {
         case '/' : pm=(void *)openrex(argv[1]+1);
                    get=getrex;
                    close=closerex;
                    break;
         case '#' : pm=(void *)opennpm(argv[1]+1);
                    get=getnpm;
                    close=closenpm;
                    break;
         case '%' : pm=(void *)openxpm(argv[1]+1,80);
                    get=getxpm;
                    close=closexpm;
                    break;
        }
     if(pm==(void *)NULL)                    /* check to see if it opened */
        exit(1);

     while(gets(ln)!=NULL)
        {                                /* see if there hit on this line */
         if((*get)(pm,(byte *)ln,(byte *)(ln+strlen(ln)),SEARCHNEWBUF)
                        !=(byte *)NULL)
             puts(ln);
        }
     (*close)(pm);
     exit(0);
    }

Windows Addendum
================

Those using non-Microsoft compilers should also read ``MSFOPEN.H``.

The Metamorph Windows API is provided in the form of two DLLs and their
associated export libraries:

::

    MORPH.DLL      MORPH.LIB
    MORPHMEM.DLL   MORPHMEM.LIB

Both DLLs must be available for Metamorph applications at run time.
``MORPHMEM.DLL`` contains all memory handling functions.

The entire Metamorph API was compiled large model with the PASCAL
calling convention. All function calls are therefore PASCAL unless
declared otherwise. ``Putmsg()`` uses the C calling convention since it
has a variable number of arguments. All data passed to and from API
functions must be ``FAR``.

``Putmsg()`` is handled a little differently under Windows since the
version in the DLL can not be effectively replaced. The default behavior
of `` putmsg()`` is to write to file handle 2. To change the output file
call ``setmmsgfname()`` with the name of the file to write to. To change
the output file to an already opened file call ``setmmsgfh()`` with the
opened file handle. To change the message handling function completely
call ``setmmsg()`` with a pointer to your message handling function. See
the descriptions of these functions for more details.

Message handling should be setup before calling any Metamorph API
functions that could fail so that messages will go to a known place.

See ``MMEXW.C`` for working examples. ``MMEXW.MAK`` is a Quick C 1.00
project file for ``MMEXW``.

::

    #include "windows.h"
    #include "stdio.h"
    #include "api3.h"
    #include "mmsg.h"

    char FAR * FAR setmmsgfname(newfname)
    char FAR *newfname;

``Setmmsgfname()`` will change the file that ``putmsg`` writes messages
to. It returns its argument. The default is to write messages to file
handle 2 ``(stderr)``.

::

    APICP *acp;

    ...

    setmmsgfname("msg.001");    /* set message file name to msg.001 */

    ...

    acp=openapicp();              /* open mm api control parameters */

::


    #include "windows.h"
    #include "stdio.h"
    #include "api3.h"
    #include "mmsg.h"

    int FAR setmmsgfh(newfhandle)
    int newfhandle;

``Setmmsgfh()`` will change the file handle that ``putmsg`` writes
messages to. It returns its argument. The default is to write messages
to file handle 2 ``(stderr)``.

::

    int fh;
    OFSTRUCT mmsginfo;
    APICP *acp;

    ...

                                  /* open file msg.001 in "wb" mode */
    fh=OpenFile("msg.001",&mmsginfo,
                (OF_CANCEL|OF_CREATE|OF_WRITE|OF_SHARE_DENY_NONE));

    if(fh!=(-1)) setmmsgfh(fh);    /* set message file handle if ok */

    ...

    acp=openapicp();              /* open mm api control parameters */

::

    #include "windows.h"
    #include "stdio.h"
    #include "stdarg.h"
    #include "api3.h"
    #include "mmsg.h"

    void FAR setmmsg(newfunction)
    int (FAR *newfunction)(int msgn, char FAR *fn, char FAR *fmt,
                           va_list args);

``Setmmsg()`` will set the function to call to handle messages from
``putmsg()``. The handler function will receive four arguments:

::

    1: int msgn;     the message number       (same as putmsg())
    2: char *fn;     the function name        (same as putmsg())
    3: char *fmt;    the htpf format string   (same as putmsg())
    4: va_list args; the stdarg argument list as derived in putmsg()
    by the va_start() call.

The ``args`` variable may be used like any ``va_list`` that has been
``va_start()'d``. e.g. in a call to ``WVSPRINTF()``. Do *not* call
``va_end()`` on ``args``.

The handler function pointer must be the result of the Windows
``MakeProcInstance()`` call so that it can be called correctly from
within the Metamorph API DLL. See your Windows SDK manual for details on
``MakeProcInstance()``. You should not use ``MakeProcInstance()`` more
than once for any given function if possible. Each call to it will use a
little memory.

The return value of the handler function will be returned by
``putmsg()`` to the original caller.

::

    ...

                                         /* a custom message handler */
    int FAR PASCAL
    msghandler(int n,char FAR *fn,char FAR *fmt,va_list args)
    {
    static char buf[256];                     /* place to sprintf to */
    char        *d;

       if(n>=0 && n<100)        strcpy(buf,"ERROR: ");
       else if(n>=100 && n<200) strcpy(buf,"WARNING: ");
       else                     strcpy(buf,"FYI: ");
       if(fmt!=(char *)NULL){
          d = buf + strlen(buf);
          htsnpf(d, (buf + sizeof(buf)) - d, fmt, args);
       }
       if(fn!=(char *)NULL){
          strcat(buf," In the function: ");
          strcat(buf,fn);
       }
                /* display message in a standard windows message box */
       MessageBox(GetFocus(),(LPSTR)buf,(LPSTR)"Metamorph 3 Message",MB_OK);
       return(0);
    }

    ...

    HANDLE hInst;                              /* current instance */
    FARPROC m;
    APICP *acp;

       ...

       putmsg(MINFO,(char *)NULL,"Default handler active");

       m=MakeProcInstance(msghandler,hInst);

       if(m!=(FARPROC)NULL){

          setmmsg(m);

          putmsg(MINFO,(char *)NULL,"My custom handler active");

       }

       ...

       acp=openapicp();          /* open mm api control parameters */

Windows programs are generally case insensitive pascal calling sequence.
Using the Metamorph API under Borland C with case sensitivity and C
calling sequence as defaults requires a little adjustment.

**Borland C options:**

#. Windows large model exe.

#. define\_WINDOWS.

#. ``Borland C++ source`` (detect ``C/C++`` by extension).

#. Case sensitive exports as well as case sensitive link.

**api3.h modifications:**

#. Insert the “pascal” keyword before each function name in its
   prototype.

#. Redefine all functions as uppercase before their prototypes. (e.g.
   ``"#define rdmmapi RDMMAPI"``)

.. [1]
   byte is defined as an unsigned char
