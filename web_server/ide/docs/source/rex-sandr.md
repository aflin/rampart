# Rex and Sandr Reference for Rampart

## Overview

Rex is a high-performance pattern matching engine available in Rampart via `Sql.rex()`,
`Sql.rexFile()`, and `Sql.sandr()`. It is **not** Perl-compatible regex. It has its own
syntax optimized for speed — on x86 hardware it can be up to 3x faster than grep.

Rex is also available as a standalone command-line tool: `rex [options] expression [files]`.

```javascript
var Sql = require("rampart-sql");

// Search
var matches = Sql.rex(expression, data [, options] [, callback]);

// Search and Replace
var result = Sql.sandr(expression, replacement, data);
var result = Sql.sandr([[expr1, repl1], [expr2, repl2], ...], data);

// Search in file (memory-efficient for large files)
var matches = Sql.rexFile(expression, filename [, options] [, callback]);
```

## The Three Rules

These are the most important things to understand about rex. They differ from
grep/Perl regex and will cause confusion if not internalized.

### Rule 1: Repetition operators apply to the longest preceding expression (string-based)

Rex is **string-based**, not character-based. A repetition operator applies to the
entire preceding sub-expression, not just the last character.

```
abc=def*    means: one "abc" followed by zero or more "def"
                   NOT: "ab" then one "c" then "de" then zero or more "f"

hello+      means: one or more occurrences of "hello"
                   NOT: "hell" then one or more "o"
```

This is the opposite of grep/Perl where `abc*` means "ab then zero or more c".

### Rule 2: Every sub-expression needs a repetition operator

Every sub-expression must have exactly one of: `=`, `+`, `*`, or `{n,m}`.

```
=    exactly one occurrence (also captures as a submatch)
+    one or more occurrences
*    zero or more occurrences
?    zero or one occurrence (synonym for {0,1})
{x,y}  from x to y occurrences
{x}    exactly x occurrences
{x,}   x or more occurrences
{,y}   zero to y occurrences
```

An expression like `abc*def*` **cannot** be located because it matches every
position in the text (both parts can be zero occurrences).

### Rule 3: No matched sub-expression will be located as part of another

`a+ab` is idiosyncratic because `a+` is a subpart of `ab`.

## Sub-expressions and Submatches

The `=` operator is special: it means "exactly one occurrence" AND it captures
that occurrence as a **submatch**. Submatches are numbered starting at 1 in
replacement strings, or starting at 0 in the `submatches` array returned by rex.

```javascript
// Each = creates a submatch
// Pattern: >>=hello=.=!world*world=
// Submatches:  \1    \2  \3    \4

var r = Sql.rex('>>hello= =world=', 'hello world', {submatches:true});
// r[0].submatches == ["hello", " ", "world"]
//                      [0]    [1]    [2]     (0-based in array)
// In sandr replacement: \\1   \\2    \\3     (1-based)
```

Other repetition operators (`+`, `*`, `{n,m}`) also produce submatches. For `*`
or `{0,y}`, the submatch may be an empty string.

## Operators

### `>>` — Root Expression and Start-of-String Anchor

The `>>` operator serves two purposes:

1. **Root selection**: Forces rex to use the following sub-expression as the
   "root" — the starting point for the search. Rex normally picks the most
   efficient sub-expression automatically, but `>>` lets you override this.

2. **String anchoring**: `>>=` anchors to the start or end of the **string**
   (not line — rex is not bound by newlines; use `^` and `$` for line-based
   anchoring).
   - `>>=` at the **beginning** of the expression → anchor to start of string
   - `>>=` at the **end** of the expression → anchor to end of string

```
>>hello=!world*world=    root on "hello", match forward to "world"
>>=!search*search=       anchor at start of string, match up to "search"
.*there=>>=              anchor at end of string (match backwards from end)
x+>>y+z+                 find y first, then match x backward and z forward
```

### `!` — NOT Operator

Matches characters that are NOT the following fixed expression. The `!` operator
matches **one character at a time**, so repetition operators count characters.

```
!finish{2,4}    matches 2 to 4 characters (that aren't the start of "finish")
finish{2,4}     matches 2 to 4 times the string "finish"
```

**Cannot be used by itself** in an expression without `>>` or another anchor.
However, with `>>` it works fine as the root:

```
>>=!search*search=    anchor at start, match chars up to "search", then "search"
>>start=!end*end=     match "start", then any chars up to "end", then "end"
```

Note: `>>=.=!search*search=` also works (the `=.=` matches one any-char first)
but the `=.=` is not required — `>>=!search*search=` is sufficient.

### `\P` and `\F` — Preceded By / Followed By (Lookaround)

These are "zero-width" assertions. The matched sub-expression following `\P` or `\F`
is excluded from the match result.

```
\P=prefix=content+    match "content" only if preceded by "prefix"
content+\F=suffix=    match "content" only if followed by "suffix"
```

### `\R` and `\I` — Case Sensitivity

```
\R    begin respecting case (until end of sub-expression)
\I    begin ignoring case (default)
```

Case is always respected inside `[]` bracket sets.

### `\L` — Literal Mode

```
\L....\L    characters between \L pairs are taken literally (no special meanings)
```

### Character Classes

```
\alpha   \upper   \lower   \digit   \xdigit
\alnum   \space   \punct   \print   \graph
\cntrl   \ascii
```

### Unicode (UTF-8) Character Classes

```
\ualpha   Unicode letters (includes ASCII a-z, A-Z)
\udigit   Unicode decimal digits (includes ASCII 0-9)
\ualnum   \ualpha plus \udigit
\umark    combining marks (accents, Arabic harakat, Indic matras)
\uspace   Unicode spaces (includes ASCII whitespace)
\upunct   Unicode punctuation and symbols (includes ASCII punctuation)
\uword    \ualpha plus \udigit plus \umark — word characters, for
          tokenizing text in any script
```

These match whole UTF-8 characters: a repetition such as `[\uword]{2,99}`
matches two to ninety-nine word characters of any script, counting
characters rather than bytes.  Invalid or truncated UTF-8 never matches a
\u class, so matches break cleanly at malformed bytes in dirty data.

Zero-width joiners (ZWNJ/ZWJ), soft hyphens and other invisible format
characters are deliberately in no class, so `[\uword]+` breaks words at
them — the correct tokenization for scripts that use them (e.g. Persian
ZWNJ).

Note that these classes identify word *characters*, not word
*boundaries*: scripts written without spaces between words — Chinese,
Japanese, Thai, Lao, Khmer, Burmese — come out as one `[\uword]+`
match per punctuation-bounded run, not as individual words.  Proper
word segmentation for those languages requires dictionary or n-gram
tokenization, which rex does not provide.

Restrictions: a \u class must be a sub-expression of its own — it may
carry a repetition operator and extra explicit bytes (`[\uword\X5F]+`
also matches `_`), but it cannot be part of a longer fixed set sequence,
inverted with `[^...]`, subtracted with `--`, negated with `!`, or used
with the `{x*}` repetition operator.

### `\bound` — Word Boundary

```
\bound    the zero-width position between a word character and a
          non-word character; the start and end of the text count
          as boundaries
```

`\bound` matches no text of its own, and is written at the start or the
end of a sub-expression.  A word character here is a `\uword` character
(letter, digit or mark), so the boundary works in any script.

A *trailing* `\bound` shortens the repetition it is attached to, one
character at a time, until the boundary is satisfied.  That is what
keeps trailing punctuation out of a match:

```
rex -n -p -P '\bound[\alnum.\-]+\bound'

  as found in section 6.5-4.   ->  6.5-4      (closing period given back)
  NASA-STD-5001B, for x        ->  NASA-STD-5001B
  see 4.10.3, and 12.4.15-2b.  ->  4.10.3  12.4.15-2b
```

The give-back applies only to the repetition `\bound` is attached to;
rex does not otherwise backtrack.

Note that `\b` by itself is still a backspace — only the whole keyword
is the boundary.

Restrictions: `\bound` must be attached to a pattern that matches at
least once, and cannot be used inside a set (`[\bound]`), in the middle
of a sub-expression, with `!`, or with a zero-minimum repetition such as
`*`, `?` or `{0,9}`.

### Special Characters

```
\n    newline       \t    tab          \v    vertical tab
\b    backspace     \r    carriage return
\f    form feed     \0    null character
\Xnn  hex character (e.g., \X0A for newline)

\b is the backspace character; the word boundary is the keyword \bound
```

### Anchors

```
^     beginning of line
$     end of line
.     any single character
```

### Bracket Sets

```
[abc]       match any of a, b, c
[a-z]       range
[^abc]      NOT any of a, b, c (inverted set)
[\alpha--x] all alphabetic except x (set subtraction with --)
```

Case is always respected inside brackets.

## Escaping in JavaScript Strings

Since JavaScript interprets `\` before rex sees it, you must double-escape:

```javascript
// To get rex to see \{\{ (literal braces), write:
'\\{\\{'

// To get rex to see \digit, write:
'\\digit'

// To get rex to see \n (newline), write:
'\\n'

// To get rex to see \P (preceded by), write:
'\\P'
```

**Characters that need escaping in rex:** `\ = ? + * { } , [ ] ^ $ . - ! >`

The string `>>` is special. To match a literal `>>`, write `\\>>`.

## Sql.rex() — Search

```javascript
var Sql = require("rampart-sql");
var ret = Sql.rex(expr, data [, options] [, callback]);
```

**Parameters:**

- `expr` — String or Array of Strings (rex expressions)
- `data` — String, Buffer, or Array of Strings/Buffers
- `options` — Object:
  - `exclude`: `"none"` | `"overlap"` | `"duplicate"` (default)
  - `submatches`: `true` | `false` (default false without callback, true with)
- `callback` — `function(match, submatchInfo, index)` (when submatches:true)
  or `function(match, index)` (when submatches:false)

**Return value:**

- Without callback, no submatches: Array of matching strings
- Without callback, with submatches: Array of objects:
  ```javascript
  [{match: "...", expressionIndex: 0, submatches: ["...", "..."]}, ...]
  ```
- With callback: Number (count of matches)

**Callback return:** Return `false` from callback to stop matching early.

**Exclude modes** (for multiple expressions that might overlap):

- `"none"` — return all matches, even overlapping
- `"overlap"` — remove shorter match when two overlap
- `"duplicate"` — (default) remove shorter when one encompasses another

## Sql.sandr() — Search and Replace

```javascript
var result = Sql.sandr(expr, replace, data);
var result = Sql.sandr([[expr1, repl1], [expr2, repl2], ...], data);
```

**Parameters:**

- `expr` — String or Array of Strings (search expressions)
- `replace` — String or Array of Strings (replacements; padded with "" if fewer)
- `data` — String or Array of Strings

**Replacement string syntax:**

- `?` — ditto: copy the character at this position from the matched text
- `{5}` — the 5th character of the matched text
- `\1`, `\2`, ... — the Nth submatch (1-based numbering)
- `\&` — the entire match (excluding `\P`/`\F` portions)
- `+` — incrementing decimal number (for numbering)
- `#N` — Nth submatch in hexadecimal
- `\xHH` — literal hex character
- `\\` — literal backslash
- `\?`, `\#`, `\{`, `\}`, `\+` — literal special characters

**Array form** processes pairs in order, each pair applied to the result of the previous:

```javascript
var result = Sql.sandr([
    ["search1", "replace1"],
    ["search2", "replace2"]
], data);
```

## Sql.rexFile() — Search in File

Same as `Sql.rex()` but takes a filename instead of data string. Memory-efficient
for large files.

```javascript
var ret = Sql.rexFile(expr, filename [, options] [, callback]);
```

Additional option:
- `delimiter` — expression for end of read buffer. Default is `$` (end of line).
  Set this if your pattern crosses lines, to ensure matches aren't split at
  buffer boundaries.

## Sql.sandr2() / Sql.re2() / Sql.re2File()

These are identical to their rex counterparts but use **Perl-compatible regular
expressions** (RE2 syntax) instead of rex syntax. Useful when you need Perl regex
features, but generally slower than rex.

## Practical Patterns for Wikitext Processing

### Finding patterns (rex)

```javascript
// All {{...}} template blocks (non-nested)
Sql.rex('>>\\{\\{=!\\}\\}*\\}\\}=', text)

// All [[...]] internal links
Sql.rex('>>\\[\\[=!\\]\\]*\\]\\]=', text)

// Bold '''...''' markup
Sql.rex(">>'''=!'''*'''=", text)

// Italic ''...'' markup
Sql.rex(">>''=!''*''=", text)

// HTML comments <!-- ... -->
Sql.rex('>><!--=!-->*-->=', text)

// Everything from start of string up to first occurrence of "search"
Sql.rex('>>=!search*search=', text)
```

### Search and replace (sandr)

```javascript
// Strip bold: '''text''' -> text
Sql.sandr(">>'''=!'''*'''=", "\\2", text)

// Strip italic: ''text'' -> text
Sql.sandr(">>''=!''*''=", "\\2", text)

// Piped links: [[target|label]] -> label
Sql.sandr(">>\\[\\[=[^|\\]]+|=[^\\]]+\\]\\]=", "\\4", text)
// Submatches: \1=[[  \2=target  \3=|  \4=label  \5=]]

// Simple links: [[target]] -> target
Sql.sandr(">>\\[\\[=!\\]\\]*\\]\\]=", "\\2", text)

// Strip HTML comments
Sql.sandr(">><!--=!-->*-->=", "", text)

// Strip <ref>...</ref>
Sql.sandr(">><=ref=!</ref*</ref>=", "", text)

// Strip self-closing <ref ... />
Sql.sandr(">><=ref =!\\/>*\\/>=", "", text)

// HTML entity decoding
Sql.sandr([
    ["&amp;=", "&"],
    ["&lt;=", "<"],
    ["&gt;=", ">"],
    ["&quot;=", "\""],
    ["&nbsp;=", " "]
], text)

// Full cleanup pipeline (applied in order)
Sql.sandr([
    [">><!--=!-->*-->=", ""],                              // comments
    [">><=ref=!</ref*</ref>=", ""],                        // <ref>...</ref>
    [">><=ref =!\\/>*\\/>=", ""],                          // <ref ... />
    [">>'''=!'''*'''=", "\\2"],                            // bold
    [">>''=!''*''=", "\\2"],                               // italic
    [">>\\[\\[=[^|\\]]+|=[^\\]]+\\]\\]=", "\\4"],         // piped links
    [">>\\[\\[=!\\]\\]*\\]\\]=", "\\2"],                   // simple links
    ["&amp;=", "&"],                                        // entities
    ["&lt;=", "<"],
    ["&gt;=", ">"],
    ["&quot;=", "\""],
    ["&nbsp;=", " "]
], text)
```

### Submatches with callback

```javascript
// Extract template names from wikitext
Sql.rex('>>\\{\\{=!\\}\\}*\\}\\}=', text, {submatches:true},
    function(match, info, index) {
        var inner = info.submatches[1]; // content between {{ and }}
        var pipePos = inner.indexOf('|');
        var name = pipePos >= 0 ? inner.substring(0, pipePos) : inner;
        printf("Template: %s\n", name);
    }
);
```

### rexFile with delimiter for cross-line matching

```javascript
// Match <doc>...</doc> blocks spanning multiple lines
Sql.rexFile(
    '>><doc id\\="=\\digit+!title*title\\="\\P=[^"]+[^>]+>=!</doc+',
    filename,
    {delimiter: '</doc>'},
    function(match, info, i) {
        // process each doc block
    }
);
```

## Key Differences from Perl Regex

| Feature | Perl Regex | Rex |
|---|---|---|
| Repetition applies to | last character | longest preceding expression |
| `ab*` means | "a" then 0+ "b" | 0+ "ab" |
| Character match | `.` | `.=` (need repetition) |
| Submatch capture | `(group)` | `=` operator on sub-expression |
| Backreference | `\1` in pattern | `\1` only in replacement |
| Alternation | `a|b` | use Array of expressions |
| Lookahead | `(?=...)` | `\F=...` |
| Lookbehind | `(?<=...)` | `\P=...` |
| NOT | `[^...]` for chars | `!expr` for strings |
| Case insensitive | `/i` flag | `\I` (default) / `\R` |
| Literal mode | `\Q...\E` | `\L...\L` |
| Word boundary | `\b` | `\bound` (in rex `\b` is a backspace) |
| Non-boundary | `\B` | none |
| Word character | `\w` = letters, digits, `_` | `\uword` = letters, digits, marks; no `_` |
| Unicode | needs the `/u` flag | always UTF-8 aware |
| Anchors | `^` and `$` are zero-width | `^` and `$` match a newline character |

### `\bound` vs Perl's `\b`

Perl's `\b` is defined by `\w`, which includes the underscore; rex's `\bound`
is defined by `\uword`, which does not.  A match may still *contain* an
underscore, since nothing requires a boundary inside a match, but one at
either end is not part of the token:

```
"foo_bar _lead trail_"

perl  \b\w+\b                     ->  foo_bar   _lead   trail_
rex   \bound[\uword\X5F]+\bound   ->  foo_bar   lead    trail
```

Perl asserts `\b` and backtracks the whole pattern to satisfy it.  Rex does
not backtrack: a trailing `\bound` shortens only the repetition it is attached
to, down to that repetition's minimum.  For ordinary token patterns the two
agree -- `\b[\w.-]+\b` and `\bound[\alnum.\-]+\bound` both take `4.10.3` out
of `see section 4.10.3.` -- but rex has no alternation to backtrack into, so
there is no equivalent of `\b(?:foo|bar)\b`.

Placement differs as well.  Perl allows `\b` anywhere in a pattern; `\bound`
must sit at the start or the end of a sub-expression, attached to a pattern
that matches at least once.  `[\bound]` is an error, where Perl's `[\b]` is a
backspace.

"The text" also means something different.  Perl's boundary is relative to the
subject string; rex's is relative to the buffer being matched -- a file chunk
for the CLI, or the field being indexed -- so a token spanning a chunk refill
can see a boundary at the seam.  Rex matches bytes, so malformed UTF-8 is
simply not a word character and breaks a token there, where Perl in UTF-8 mode
rejects an invalid subject outright.

## Performance Notes

- Rex uses "state-anticipation" (Boyer-Moore-like) matching: longer patterns are
  faster to find, not slower
- The `>>` operator helps performance by telling rex which sub-expression to search
  for first
- Rex automatically picks the most efficient fixed-length sub-expression as the
  search root, and matches other parts forward and backward from there
- `Sql.rexFile()` is preferred over `Sql.rex()` for large files — it streams
  rather than loading the entire file into memory
- `Sql.sandr()` with the array-of-pairs form applies all replacements in a single
  pass through the data, which is more efficient than calling sandr repeatedly

## Limitations

- Cannot handle **nested** delimiters. `>>\\{\\{=!\\}\\}*\\}\\}=` on
  `{{outer|{{inner}}}}` will match `{{outer|{{inner}}` (stops at first `}}`).
  Nested brace matching requires a stack-based algorithm outside of rex.
- No alternation within a single expression — use an Array of expressions instead
- No backreferences within the search pattern (only in replacement strings)
- The `!` NOT operator cannot be used by itself, but works as root with `>>`
