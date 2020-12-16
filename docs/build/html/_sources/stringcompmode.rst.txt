String Compare Mode
-------------------

stringcomparemode parameter
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``stringcomparemode`` - mode and flags for the following function:

- :ref:`funcs-string:length`
- :ref:`funcs-string:lower`
- :ref:`funcs-string:upper`
- :ref:`funcs-string:initcap`
- :ref:`funcs-string:stringcompare`

The ``stringcomparemode`` parameter specifies string compares (e.g. equals,
less-than or greater-than) for the :ref:`funcs-string:stringcompare` function. It also controls
the default mode for the non-case-style flags/mode for the functions :ref:`funcs-string:length`, 
:ref:`funcs-string:lower`, :ref:`funcs-string:upper` and :ref:`funcs-string:initcap`. The default is "unicodemulti, respectcase" - i.e. 
characters must be identical to match, though ISO-8859-1vs. UTF-8 encoding may be 
ignored.


(Note that negation ("-") can only be used with values that are "on/off", i.e. the
flags; case style and case mode cannot be negated.) "+" and "-" remain in effect for
following values, until another "+", "-" or "=" (clear the setting first) is given.

The case-folding style determines what case to fold to; it is exactly one of:

- ``respectcase`` aka ``preservecase`` aka ``casesensitive`` - Do not change case at
  all, for case-sensitive searches.
- ``ignorecase`` aka ``igncase`` aka ``caseinsensitive`` - Fold case for caseless
  (case-insensitive) matching; this is the default style for textsearchmode. This typically
  (but not always) means characters are folded to their lowercase equivalents.

Any combination of zero or more of the following flags may be given in addition to a case style:

- ``iso-8859-1`` aka ``iso88591`` - Interpret text as ISO-8859-1 encoded. This should only 
  be used if all text is known to be in this character set. Only codepoints U+0001 through U
  +00FF can be supported. Any UTF-8 text will be misinterpreted.
- If this flag is disabled (the default), text is interpreted as UTF-8, and invalid bytes 
  (if any) are interpreted as ISO-8859-1. This supports all UTF-8 characters, as well as  
  most typical ISO-8859-1 data, if any happens to be accidentally mixed in.

Typically, this flag is left disabled, and text is stored in UTF-8, since it supports a 
broader range of characters. Any other character set besides UTF-8 or ISO-8859-1 is not 
supported, and should be mapped to UTF-8.

utf-8 aka utf8 Alias for negating iso-8859-1, ie, specifying this disables the iso-8859-1 
flag.
expanddiacritics aka expdiacritics Expand certain phonological diacritics: umlauts over 
"a", "o", "u" expand to the vowel plus "e" (for German, e.g. "für" matches "fuer"); 
circumflexes over "e" and "o" expand to the vowel plus "s" (for French, e.g. "hôtel" 
matches "hostel"). The expanded "e" or "s" is optional-match - e.g. "f"ur" also matches 
"fur" - but only against a non-optional char; i.e. "hôtel" does not match "hötel" (the "e" 
and "s" collide), and "für" does not match "füer" (both optional "e"s must match each 
other. Also, neither the vowel nor the "e"/"s" will match an ignorediacritics-stripped 
character; this prevents "für" from matching "fu'er".
ignorediacritics aka igndiacritics Ignore diacritic marks - Unicode non-starter or modifier 
symbols resulting from NFD decomposition - e.g. diaeresis, umlaut, circumflex, grave, 
acute, tilde etc.
expandligatures aka expligatures Expand ligatures, e.g. "œ" (U+0153) will match "oe". Note 
that even with this flag off, certain ligatures may still be expanded if necessary for 
case-folding under ignorecase with case mode unicodemulti; see below.
ignorewidth aka ignwidth Ignore half- and full-width differences, e.g. for katakana and 
ASCII.
Due to interactions between flags, they are applied in the order specified above, followed 
by case folding according to the case style (upper/lower etc.). E.g. expanddiacritics is 
applied before ignorediacritics, because otherwise the latter would strip the characters 
that the former expands.


A case-folding mode may also be given in addition to the above; this determines how the 
case-folding style (e.g. upper/lower/title) is actually applied. It is one of the following:

unicodemulti Use the builtin Unicode 5.1.0 1-to-N-character folding tables. All locale-independent Unicode characters with the appropriate case equivalent are folded. A single character may fold to up to 3 characters, if needed; e.g. the German es-zett character (U+00DF) will match "ss" and vice-versa under ignorecase. Note that additional ligature expansions may happen if expandligatures is set.
unicodemono Use the builtin Unicode 5.1.0 1-to-1-character folding tables. All locale-independent Unicode characters with the appropriate case equivalent are folded. Note that even though this mode is 1-to-1-character, it is not necessarily 1-to-1-byte, i.e. a UTF-8 string may still change its byte length when folded, even though the Unicode character count will remain the same.
ctype Use the C ctype.h functions. Case folding will be OS- and locale-dependent; a locale should be set with the SQL locale property. Only codepoints U+0001 through U+00FF can be folded; e.g. most Western European characters are folded, but Cyrillic, Greek etc. are not. Note that while this mode is 1-to-1-character, it is not necessarily 1-to-1-byte, unless the iso-8859-1 flag is also in effect. This mode was part of the default in version 5 and earlier.
The default case-folding mode is unicodemulti; see below for the version 5 and earlier default, and important caveats.

In addition to the above styles, flags and modes, several aliases may be used, and mixed with flags. The aliases have the form:

[stringcomparemode|textsearchmode][default|builtin]
where stringcomparemode or textsearchmode refers to that setting's value (if not given: the setting being modified). default refers to the default value (modifiable with texis.ini); builtin refers to the builtin factory default; no suffix refers to the current setting value. E.g. "stringcomparemodedefault,+ignorecase" would obtain the default stringcomparemode setting (from texis.ini if available), but set the case style to ignorecase.
A Metamorph index always uses the textsearchmode value that was set at its initial creation, not the current value. However, when multiple Metamorph indexes exist on the same fields, at search time the Texis optimizer will attempt to use the index whose (creation-time) textsearchmode is closest to the current value.

