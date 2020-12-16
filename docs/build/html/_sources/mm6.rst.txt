REX On Its Own
==============

REX, The Regular Expression Pattern Matcher
-------------------------------------------

**REX** is a very powerful pattern matcher which can be called
internally within the Metamorph Query Language. It can also be used as
an external stand-alone tool. Any pattern or expression which is
outlined herein can be specified from within a Metamorph query by
preceding it with a forward slash (``/``). Similarly, any **REX**
pattern can be entered as Start or End Delimiter expression, or used
otherwise in a Vortex script which calls for ``rex``, there requiring no
forward slash (``/``).

**REX** (for **R**\ egular **EX**\ pression) allows you to search for
any fixed or variable length regular expression, and executes more
efficiently types of patterns you might normally try to look for with
the Unix ``Grep`` family. **REX** puts all these facilities into one
program, along with a Search & Replace capability, easier to learn
syntax, faster execution, ability to set delimiters within which you
want to search (i.e., it can search across lines), and goes beyond what
is possible with other search tools.

It may be somewhat new to those who haven’t previously used such tools,
but you’ll find that if you follow REX’s syntax very literally and
practice searching for simple, then more complex expressions, that it
becomes quite understandable and easy to use. One thing to keep in mind
is that REX looks both forwards and backwards, something quite different
from other types of tools. Therefore, when you construct a REX
expression, make sure it makes sense from a global view of the file;
that is, whether you’d be looking forwards, or looking backwards.

A REX pattern can be constructed from a series of **F-REX’S**
(pronounced “f-rex”), or Fixed length Regular EXpressions, where
repetition operators after each subexpression are used to state how many
of each you are looking for. Unless otherwise delineated, REX assumes
you are looking for one occurrence of each subexpression stated within
the expression, as specified within quotation marks.

To begin learning to use REX, first try some easy F-REX (fixed length)
patterns. For example, on the query line, type in:

::

         /life

This pattern “``life``” is the same as the pattern “``life=``”, and
means that you are asking the pattern matcher REX to look for one
occurrence of the fixed length expression “``life``”.

The equal sign ‘``=``’ is used to designate one occurrence of a fixed
length pattern, and is assumed if not otherwise stated.

The plus sign ‘``+``’ is used to designate one or more occurrences of a
fixed length pattern. If you were to search for “``life+``” REX would
look for one or more occurrences of the word “``life``”: e.g., it would
locate the pattern “``life``” and it would also locate the pattern
“``lifelife``”.

The asterisk ‘``*``’ is used to designate zero or more occurrences of a
fixed length pattern. If you were to try to look for “``life*``”, REX
would be directed to look for zero or more occurrences of the word
``life`` (rather than 1 or more occurrences); therefore, while it could
locate “``life``” or “``lifelife``”, it would also have to look for 0
occurrences of “``life``” which could be every pattern in the file. This
would be an impossible and unsatisfactory search, and so is not a legal
pattern to look for. The rule is that a ‘``*``’ search must be rooted to
something else requiring one or more occurrences.

If you root “``life*``” to a fixed length pattern which must find at
least one occurrence of something, the pattern becomes legal. Therefore,
you could precede “``life*``” with “``love=``”, making the pattern
“``love=life*``”. Now it is rooted to something which definitely can be
found in the file; e.g., one occurrence of the word “``love``”, followed
by 0 or more occurrences of the word “``life``”. Such an expression
“``love=life*``” would locate “``love``”, “``lovelife``”, and
“``lovelifelife``”.

If there is more than one subexpression within a REX pattern, any
designated repetition operator will attach itself to the longest
preceding fixed length pattern. Therefore a pattern preceding a plus
sign ‘``+``’, even if it is made of more than one subexpression, will be
treated as one or more occurrences of the whole preceding pattern. Use
the equal sign ‘``=``’, if necessary, to separate these subexpressions
and prevent an incorrect interpretation.

For example: if you say “``lovelife*``” (rather than “``love=life*``”)
the ‘``*``’ operator will attach itself to the whole preceding
expression, “``lovelife``”, and will therefore be translated to mean 0
or more occurrences of the entire pattern “``lovelife``”, making it an
illegal expression. On the other hand, in the expression
“``love=life*``”, REX will correctly look for 1 occurrence of
“``love``”, followed by 0 or more occurrences of “``life``”.

Use the “``-x``” option in ``REX`` (from the Windows or Unix command
line) to get feedback on how REX translates the syntax you have entered
to what it is really looking for. In this way you can debug your use of
syntax and learn to use REX to its maximum power. Using our same
example, if you enter on the Windows or Unix command line:

::

         rex -x ``love=life*''

you will get the following output back to show you how REX will
interpret your search request:

::

         1 occurrence(s) of : [Ll][Oo][Vv][Ee]
         followed by from 0 to 32000 occurrences of : [Ll][Ii][Ff][Ee]

The brackets above, as: “``[Ll]``”, mean either of the characters shown
inside the brackets, in that position; i.e., a capital or small ‘``l``’
in the 1st character position, a capital or small ‘``o``’ in the 2nd
character position, and so on.

You can find REX syntax for all matters discussed above and delineated
below, by typing in “``REX``” followed by a carriage return on the
Windows or Unix command line.

REX Program Syntax and Options
------------------------------

REX locates and prints lines containing occurrences of regular
expressions. Beginning and end of line are the default beginning and
ending delimiters. If starting (``-s``) and ending (``-e``) delimiters
are specified REX will print from the beginning delimiting expression to
the ending delimiting expression. If so specified (``-p``) REX will
print the expression only. If files are not specified, standard input is
used. If files are specified, the filename will be printed before the
line containing the expression, unless the “``-n``” option (don’t print
the filename) is used.

::

    SYNTAX:  rex [options] expression [files]

     Where:  options are preceded by a hyphen (-)
             expressions may be contained in quotes (" ")
             filename(s) comes last and can contain wildcards

    OPTIONS:
       -c       Don't print control characters, replace with space.
       -C       Count the number of times the expression occurs.
       -l       List file names (only) that contain the expression.
       -E"EX"   Specify and print the ending delimiting expression.
       -e"EX"   Specify the ending delimiting expression.
       -S"EX"   Specify and print the starting delimiting expression.
       -s"EX"   Specify the starting delimiting expression.
       -p       Begin printing at the start of the expression.
       -P       Stop printing at the end of the expression.
       -r"STR"  Replace the expression with "STR" to standard output.
       -R"STR"  Replace the expression with "STR" to original file.
       -t"Fn"   Use "Fn" as the temporary file. (default: "rextmp")
       -f"Fn"   Read the expression(s) from the file "Fn".
       -n       Do not print the file name.
       -O       Generate "FNAME@OFFSET,LEN" entries for mm3 subfile list.
       -x       Translate the expression into pseudo-code (debug).
       -v       Print lines (or delimiters) not containing the expression.

-  Each option must be placed individually on the command line.

-  “``EX``” is a REX expression.

-  “``Fn``” is file name.

-  “``STR``” is a replacement string.

-  “``N``” is a drive name e.g.: ``A,B,C`` …

REX Expression Syntax
---------------------

-  Expressions are composed of characters and operators. Operators are
   characters with special meaning to REX. The following characters have
   special meaning: “``\=+*?{},[]^$.-!``” and must be escaped with a
   ‘``\``’ if they are meant to be taken literally. The string “``>>``”
   is also special and if it is to be matched, it should be written
   “``\>>``”. Not all of these characters are special all the time; if
   an entire string is to be escaped so it will be interpreted
   literally, only the characters “``\=?+*{[^$.!>``” need be escaped.

-  A ‘``\``’ followed by an ‘``R``’ or an ‘``I``’ mean to begin
   respecting or ignoring alphabetic case distinction. (Ignoring case is
   the default.) These switches *do not* apply inside range brackets.

-  A ‘``\``’ followed by an ‘``L``’ indicates that the characters
   following are to be taken literally up to the next ‘``\L``’. The
   purpose of this operation is to remove the special meanings from
   characters.

-  A subexpression following ‘``\F``’ (followed by) or ‘``\P``’
   (preceded by) can be used to root the rest of an expression to which
   it is tied. It means to look for the rest of the expression “as long
   as followed by …” or “as long as preceded by …” the subexpression
   following the ``\F`` or ``\P``, but the designated subexpression will
   be considered excluded from the located expression itself.

-  A ‘``\``’ followed by one of the following ‘``C``’ language character
   classes matches that character class: ``alpha``, ``upper``,
   ``lower``, ``digit``, ``xdigit``, ``alnum``, ``space``, ``punct``,
   ``print``, ``graph``, ``cntrl``, ``ascii``.

-  A ‘``\``’ followed by one of the following special characters will
   assume the following meaning: ``n``\ =newline, ``t``\ =tab,
   ``v``\ =vertical tab, ``b``\ =backspace, ``r``\ =carriage return,
   ``f``\ =form feed, ``0``\ =the null character.

-  A ‘``\``’ followed by ``Xn`` or ``Xnn`` where ``n`` is a hexadecimal
   digit will match that character.

-  A ‘``\``’ followed by any single character (not one of the above)
   matches that character. Escaping a character that is not a special
   escape is not recommended, as the expression could change meaning if
   the character becomes an escape in a future release.

-  The character ‘``^``’ placed anywhere in an expression (except after
   a ‘``[``’) matches the beginning of a line. (same as: ``\x0A`` in
   Unix or ``\x0D\x0A`` in Windows)

-  The character ‘``$``’ placed anywhere in an expression matches the
   end of a line. (``\x0A`` in Unix, ``\x0D\x0A`` in Windows)

-  The character ‘``.``’ matches any character.

-  A single character not having special meaning matches that character.

-  A string enclosed in brackets ``[]`` is a set, and matches any single
   character from the string. Ranges of ASCII character codes may be
   abbreviated as in ``[a-z]`` or ``[0-9]``. A ‘``^``’ occurring as the
   first character of the set will invert the meaning of the set. A
   literal ‘``-``’ must be preceded by a ‘``\``’. The case of alphabetic
   characters is always respected within brackets.

   A double-dash (“``--``”) may be used inside a bracketed set to
   subtract characters from the set; e.g. “``[\alpha--x]``” for all
   alphabetic characters except “``x``”. The left-hand side of a set
   subtraction must be a range, character class, or another set
   subtraction. The right-hand side of a set subtraction must be a
   range, character class, or a single character. Set subtraction groups
   left-to-right. The range operator “``-``” has precedence over set
   subtraction. Set subtraction was added in version 6.

-  The ‘``>>``’ operator in the first position of a fixed expression
   will force REX to use that expression as the “root” expression off
   which the other fixed expressions are matched. This operator
   overrides one of the optimizers in REX. This operator can be quite
   handy if you are trying to match an expression with a ‘``!``’
   operator or if you are matching an item that is surrounded by other
   items. For example: “``x+>>y+z+``” would force REX to find the
   “``y's``” first then go backwards and forwards for the leading
   “``x's``” and trailing “``z's``”.

-  The ‘``!``’ character in the first position of an expression means
   that it is *not* to match the following fixed expression. For
   example: “``start=!finish+``” would match the word “``start``” and
   anything past it up to (but not including the word “``finish``”.
   Usually operations involving the “``!``” operator involve knowing
   what direction the pattern is being matched in. In these cases the
   ‘``>>``’ operator comes in handy. If the ‘``>>``’ operator is used,
   it comes before the ‘``!``’. For example:
   “``>>start=!finish+finish``” would match anything that began with
   “``start``” and ended with “``finish``”. *The “``!``” operator cannot
   be used by itself* in an expression, or as the root expression in a
   compound expression. NOTE: This “``!``” operator “nots” the whole
   expression rather than its sequence of characters, as in earlier
   versions of REX.

   Note that “``!``” expressions match a character at a time, so their
   repetition operators count characters, not expression-lengths as with
   normal expressions. E.g. “``!finish{2,4}``” matches 2 to 4
   characters, whereas “``finish{2,4}``” matches 2 to 4 times the length
   of “``finish``”.

REX Repetition Operators
------------------------

-  A regular expression may be followed by a repetition operator in
   order to indicate the number of times it may be repeated.

   NOTE: Under Windows the operation “``{X,Y}``” has the syntax
   “``{X-Y}``” because Windows will not accept the comma on a command
   line. Also, ``N`` occurrences of an expression implies infinite
   repetitions but in this program ``N`` represents the quantity
   ``32768`` which should be a more than adequate substitute in real
   world text.

-  An expression followed by the operator “``{X,Y}``” indicates that
   from ``X`` to ``Y`` occurrences of the expression are to be located.
   This notation may take on several forms: “``{X}``” means ``X``
   occurrences of the expression, “``{X,}``” means from ``X`` to ``N``
   occurrences of the expression, and “``{,Y}``” means from ``0`` (no
   occurrences) to ``Y`` occurrences of the expression.

-  The ‘``?``’ operator is a synonym for the operation “``{0,1}``”. Read
   as: “Zero or one occurrence.”

-  The ‘``*``’ operator is a synonym for the operation “``{0,}``”. Read
   as: “Zero or more occurrences.”

-  The ‘``+``’ operator is a synonym for the operation “``{1,}``”. Read
   as: “One or more occurrences.”

-  The ‘``=``’ operator is a synonym for the operation “``{1}``”. Read
   as: “One occurrence.”

RE2 Syntax
----------

In Texis version 7.06 and later, on some platforms the search expression
may be given in RE2 syntax instead of REX. RE2 is a Perl-compatible
regular expression library whose syntax may be more familiar to Unix
users than Texis’ REX syntax. An RE2 expression in REX is indicated by
prefixing the expression with “92<re292>”. E.g. “92<re292>92w+” would
search for one or more word characters, as “92w” means word character in
RE2, but not REX.

REX syntax can also be indicated in an expression by prefixing it with
“92<rex92>”. Since the default syntax is already REX, this flag is not
normally needed; it is primarily useful in circumstances where the
syntax has already been changed to RE2, but outside of the expression –
e.g. a Vortex ``<rex>`` statement with the option ``syntax=re2``.

Note that RE2 syntax is not supported on all texis -platform platforms;
where it is unsupported, attempting to invoke an RE2 expression will
result in the error message “REX: RE2 not supported on this platform”.
(Windows, most Linux 2.6 versions except i686-unknown-linux2.6.17-64-32
are supported.)

REX Replacement Strings
-----------------------

NOTE: Any expression may be searched for in conjunction with a replace
operation but the replacement string will always be of fixed size.

An expression will be defined one way when given to REX to search for;
it will be defined another way when used with the ‘``-r``’ or ‘``-R``’
``Replace`` options as the replacement string. The characters
“``?#{}+``” have entirely different meanings when used as part of the
syntax of a replacement string, than when used as part of the syntax of
expressions or repetition operators.

Replacement syntax is different from expression syntax, and is limited
to the following delineated rules.

-  The characters “``?#{}+\``” are special. To use them literally,
   precede them with the escapement character “\ ``\``\ ’.

-  Replacement strings may just be a literal string or they may include
   the “ditto” character “``?``”. The ditto character will copy the
   character in the position specified in the replace string from the
   same position in the located expression.

-  A decimal digit placed within curly braces (e.g. ``{5}``) will place
   that character of the located expression to the output.

-  A “``\``” followed by a decimal number will place that subexpression
   to the output. Subexpressions are numbered starting at 1.

-  The sequence “``\&``” will place the entire expression match (sans
   ``\P`` and ``\F`` portions) to the output. This escape was added in
   Texis version 7.06.

-  A plus character “``+``” will place an incrementing decimal number to
   the output. One purpose of this operator is to number lines.

-  A “``#``” in the replace string will cause the character in that
   position to be printed in hexadecimal form.

-  Any character in the replace string may be represented by the
   hexadecimal value of that character using the following syntax:
   ``\Xdd`` where “``dd``” is the hexadecimal value.

REX Caveats and Commentary
--------------------------

**REX** is a highly optimized pattern recognition tool that has been
modeled after the UNIX family of tools: ``GREP``, ``EGREP``, ``FGREP``,
and ``LEX``. Wherever possible REX’s syntax has been held consistent
with these tools, but there are several major departures that may bite
those who are used to using the ``GREP`` family.

REX uses a combination of techniques that allow it to surpass the speed
of anything similar to it by a very wide margin.

The technique that provides the largest advantage is called “state
anticipation or state skipping” which works as follows …

If we were looking for the pattern:

::

                           ABCDE

in the text:

::

                           AAAAABCDEAAAAAAA

a normal pattern matcher would do the following:

::

                           ABCDE
                            ABCDE
                             ABCDE
                              ABCDE
                               ABCDE
                           AAAAABCDEAAAAAAA

The state anticipation scheme would do the following:

::

                           ABCDE
                               ABCDE
                           AAAAABCDEAAAAAAA

The normal algorithm moves one character at a time through the text,
comparing the leading character of the pattern to the current text
character of text, and if they match, it compares the leading pattern
character +1 to the current text character +1, and so on …

The state anticipation pattern matcher is aware of the length of the
pattern to be matched, and compares the last character of the pattern to
the corresponding text character. If the two are not equal, it moves
over by an amount that would allow it to match the next potential hit.

If one were to count the number of comparison cycles for each pattern
matching scheme using the example above, the normal pattern matcher
would have to perform 13 compare operations before locating the first
occurrence; versus 6 compare operations for the state anticipation
pattern matcher.

One concept to grasp here is: “*The longer the pattern to be found, the
faster the state anticipation pattern matcher will be;*” while a normal
pattern matcher will slow down as the pattern gets longer.

Herein lies the first major syntax departure: REX always applies
repetition operators to the longest preceding expression. It does this
so that it can maximize the benefits of using the state skipping pattern
matcher.

If you were to give ``GREP`` the expression: ``ab*de+``

It would interpret it as: an ‘``a``’ then 0 or more ‘``b's``’ then a
‘``d``’ then 1 or more ‘``e's``’.

``REX`` will interpret this as: 0 or more occurrences of ‘``ab``’
followed by 1 or more occurrences of ‘``de``’.

The second technique that provides REX with a speed advantage is ability
to locate patterns both forwards and backwards indiscriminately.

Given the expression: “``abc*def``”, the pattern matcher is looking for
“``Zero`` to ``N`` occurrences of ‘``abc``’ followed by a ‘``def``’”.

The following text examples would be matched by this expression:

::

         abcabcabcabcdef
         def
         abcdef

But consider these patterns if they were embedded within a body of text:

::

         My country 'tis of abcabcabcabcdef sweet land of def, abcdef.

A normal pattern matching scheme would begin looking for ‘``abc*``’.
Since ‘``abc*``’ is matched by every position within the text, the
normal pattern matcher would plod along checking for ‘``abc*``’ and then
whether it’s there or not it would try to match ‘``def``’. REX examines
the expression in search of the the most efficient fixed length
subpattern and uses it as the root of search rather than the first
subexpression. So, in the example above, REX would not begin searching
for ‘``abc*``’ until it has located a ‘``def``’.

There are many other techniques used in REX to improve the rate at which
it searches for patterns, but these should have no effect on the way in
which you specify an expression.

The three rules that will cause the most problems to experienced
``GREP`` users are:

#. Repetition operators are always applied to the longest expression.

#. There must be at least one subexpression that has one or more
   repetitions.

#. No matched subexpression will be located as part of another.

Rule 1 Example
    : ``abc=def*`` Means one ‘``abc``’ followed by 0 or more ‘``def's``’
    .

Rule 2 Example
    : ``abc*def*`` CANNOT be located because it matches every position
    within the text.

Rule 3 Example
    : ``a+ab`` Is idiosyncratic because ‘``a+``’ is a subpart of
    ‘``ab``’.

Other REX Notes
---------------

-  When using REX external to Metamorph in Windows or Unix, it can
   occasionally occur that a very complicated expression requires more
   memory than is available. In the event of an error message indicating
   not enough memory, you can try the same expression using “``REXL``”,
   another version of ``REX`` compiled for large model. As “``REXL``” is
   slightly slower than “``REX``” it would not be recommended for use in
   most circumstances.

-  The beginning of line (``^``) and end of line (``$``) notation
   expressions for Windows are both identified as a 2 character
   notation; i.e., REX under Windows matches ``\X0d\X0a`` (carriage
   return ``[cr]``, line feed ``[lf]``) as beginning and end of line,
   rather than ``\X0a`` as beginning, and ``\X0d`` as end.
