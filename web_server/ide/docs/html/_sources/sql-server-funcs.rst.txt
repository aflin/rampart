
Server Functions
----------------

The SQL module and engine has a number of functions built into it which can
operate on fields within a SQL statement.

The functions below may be called anywhere that a field or an expression
normally occurs in a SQL statement.  Arguments to a function can be either a
single field name, another function or another expression.

Vector Function
~~~~~~~~~~~~~~~

vecdist
"""""""

Calculate the distance/score of two vectors.

.. code-block:: sql

    vecdist(vec1, vec2[, metric [, type]])


If not specified, default metric is ``dot`` and default type is ``f16``

Performs the same distance calculation as :ref:`rampart-vector:Distance Function`
with the exception that ``type`` must be a buffer type and cannot be ``numbers``.
Note that when the column is a typed ``varvec*``, the ``type`` argument is
redundant and is ignored; the column's own type is used.

With semantic embedding vectors, it can be used to rerank likep results.

.. code-block:: javascript

    /*
      do a likep search on query for 100 rows, rerank on the embedding vector of query (compvec).
      1 is first selected column (score).
      ORDER BY 1 DESC because higher dot product scores = more similar.
    */
    var res = sql.exec(
        `select vecdist(VecCol, ?) score, Title, Text from mytable
            where Text likep ? order by 1 DESC`,
        {maxRows: 100},
        [compvec, query]
    );
    /* display top 10 rows in res here */

embed
"""""

Compute a semantic embedding vector for a piece of text using the
connection's currently-loaded embedding model
(:ref:`llamaEmbed <sql-set:llamaEmbed>` — a ``llama.cpp`` GGUF —
:ref:`onnxEmbed <sql-set:onnxEmbed>` — an ONNX model directory — or
:ref:`clipEmbed <sql-set:clipEmbed>` — a CLIP model, which can also
embed **images**).

.. code-block:: sql

    embed(text [, dtype] [, kind [, title]])

* ``text`` is a :green:`String` column, bound parameter, literal, or any
  expression that evaluates to text.  ``NULL`` and the empty string
  produce ``NULL``.  With ``kind`` = ``'image'`` it is instead an image
  **file path** or a :green:`Buffer` of image bytes.
* ``dtype`` is an optional :green:`String` naming the result vec dtype.
  One of ``'f16'`` (default), ``'f32'``, ``'f64'``, or ``'bf16'``.
  ``''`` and ``'auto'`` are accepted placeholders for the default —
  useful in the three-argument ``chunkembed()``/``chunkavg()`` forms
  where a later argument follows the dtype.  The
  named dtype propagates to the result column type: ``embed(?, 'f32')``
  returns a ``varvecF32``, ``embed(?)`` returns a ``varvecF16``.
* ``kind`` is an optional :green:`String` saying **what the value is**.
  It may be given directly as the second argument
  (``embed(?, 'query')``) or after a dtype (``embed(?, 'f32', 'query')``).

  ``'query'``, ``'document'`` and ``'raw'`` select a model's asymmetric
  retrieval prompt (see
  :ref:`Retrieval prompts <sql-set:Retrieval prompts>`).  ``'text'`` is
  an explicit synonym for ``'raw'`` — text content, no prompt — and
  exists so the modality reads symmetrically against ``'image'``.
  ``embed(text)`` embeds verbatim, exactly as before prompts existed;
  with no prompts configured, every one of these embeds verbatim.

  ``'image'`` selects a different **encoder** rather than a prompt: the
  value is an image file path or a :green:`Buffer` of image bytes, and
  it is embedded by the vision tower of a
  :ref:`clipEmbed <sql-set:clipEmbed>` model into the same vector space
  as that model's text output.  Using ``'image'`` on a text-only engine
  is an error — embedding the path *as text* would store a plausible but
  meaningless vector.

  All kind words are accepted by every engine (``'text'`` under an ONNX
  model simply means "no prompt"), so a statement keeps parsing when a
  connection's engine changes.
* ``title`` is an optional :green:`String`, valid only with
  ``'document'``: ``embed(?, 'document', ?)`` folds the title into the
  document prompt (models whose prompt has a title slot use it; others
  get prompt + title + newline + text).

With an asymmetric model loaded, single-vector inserts should use
``embed(?, 'document')`` so stored vectors match the query side —
``LIKEV`` with a string automatically applies the query prompt, and
``chunkembed()`` automatically applies the document prompt.

With a :ref:`clipEmbed <sql-set:clipEmbed>` model, images and text share
one vector space, so images are stored and then found by text:

.. code-block:: sql

    insert into images values (?, ?, embed(?, 'image'));
    select Path from images where Vec likev 'a dog catching a frisbee';

``chunkembed()`` and friends are **not** available with a CLIP model
(one vector per image; nothing to chunk).

The model is configured via :ref:`llamaEmbed <sql-set:llamaEmbed>` or
:ref:`onnxEmbed <sql-set:onnxEmbed>`; llama.cpp concurrency behavior is
controlled by :ref:`llamaEmbedPerThread <sql-set:llamaEmbedPerThread>`
(ONNX sessions are concurrent by nature).  The output dimensionality is
determined by the model — for example, ``all-MiniLM-L6-v2`` produces
384-dim vectors; ``bge-m3`` produces 1024-dim.  Vectors are
L2-normalized; norm ≈ 1.0 within dtype quantization noise.

Long text is not truncated: the engine splits it into chunks
(structure-aware, sized to the model's token window), embeds every
chunk, and returns the L2-normalized **mean** of the chunk vectors.  To
keep the individual chunk vectors instead, use
:ref:`chunkembed() <sql-server-funcs:chunkembed>` below.

Results are cached per model (see
:ref:`likevCache <sql-set:likevCache>`): any of the embedding scalars
applied to the same text — in one statement, across statements, or
across threads — runs the model once.

``embed()`` requires the engine's module (``rampart-llamacpp``,
``rampart-onnx`` or ``rampart-clip``) to be installed.  ``rampart-sql``
auto-loads it on the first ``sql.set({llamaEmbed: ...})`` /
``sql.set({onnxEmbed: ...})`` / ``sql.set({clipEmbed: ...})``
call, using the canonical module name — which the installer points at
the installed build variant.  To
force a specific variant, ``require()`` it first.  See
:ref:`Generating embeddings <rampart-sql:Generating embeddings>` for
the full dependency and install notes.

If no model has been loaded, ``embed()`` raises a runtime error.  If
``dtype`` names something unknown (e.g. ``'q4_0'``), the SQL prepare
fails with a clear "Unknown dtype" message.

Use in SELECT projections, INSERT values, or as the right-hand side of
``LIKEV`` / ``vecdist()``:

.. code-block:: sql

    -- Inline embedding at INSERT time
    INSERT INTO docs (id, text, v) VALUES (?, ?, embed(?));

    -- Top-K similarity search (a string on the right-hand side of LIKEV
    -- is also auto-coerced to embed())
    SELECT id, $rank FROM docs WHERE v LIKEV embed(?) ORDER BY 2 DESC;

    -- Distance/score projection, used to re-rank an index result
    SELECT id, vecdist(v, embed(?)) AS d
      FROM docs WHERE v LIKEV embed(?)
     ORDER BY 2 DESC;

    -- Standalone, useful for debugging / probing model output
    SELECT embed('chocolate cake recipe', 'f32');

See :ref:`Vector Search <rampart-sql:Vector Search>` for the end-to-end
pipeline (model setup → table schema → INSERT → vector index →
``LIKEV``).

chunkembed
""""""""""

Embed a whole document, keeping **all** of its chunk vectors.

.. code-block:: sql

    chunkembed(text [, dtype [, prefix]])
    chunkembed(textList [, dtype [, prefix [, spans]]])

Like :ref:`embed() <sql-server-funcs:embed>`, but where ``embed()``
returns the single combined (mean) vector, ``chunkembed()`` returns the
document's per-chunk vectors concatenated back to back — a
``k × dim``-cell value in the same ``varvec*`` types (dtype argument and
default as in ``embed()``).  The embedding engine does the chunking:
paragraph boundaries first, then the model's token window; a short text
comes back as ``k = 1``, byte-identical to a plain single vector.

The optional third argument is a **per-chunk prefix** — typically the
document's title.  It is prepended to every chunk's *embedding input*
(each chunk embeds as if it read ``<prefix> <chunk text>``), so
name-seeking queries ("who plays X in Y?") can match any chunk of the
document by its name, not just chunks that happen to repeat it.  The
prefix never changes the chunk **boundaries**, the chunk **count**, or
the text spans :ref:`abstract <sql-server-funcs:abstract>` computes —
only what the model sees.  (A chunk that already fills the model window
gives up its last few tail tokens of embedding input to make room.)

To pass a prefix without naming a dtype, use ``''`` or ``'auto'`` as
the dtype placeholder (both mean "the default"):

.. code-block:: sql

    -- title-prefixed chunks, default dtype:
    INSERT INTO docs (id, title, doc, v)
        VALUES (?, ?, ?, chunkembed(?, '', ?));   -- params: ..., doc, title

Search-side queries need no change — the query string embeds without a
title; the stored chunk vectors simply carry the document's name in
their semantics.

With an asymmetric model's :ref:`retrieval prompts
<sql-set:Retrieval prompts>` in effect, ``chunkembed()`` folds the
model's **document** prompt around this title automatically — a model
whose prompt has a ``{title}`` slot fills it, others get
``<document prompt><title>`` as the per-chunk prefix — and ``LIKEV``'s
string auto-embedding applies the **query** prompt.  Both sides stay
consistent with no change to the SQL.

Stored in a vector-indexed column, a chunked value gives ``LIKEV``
*best-chunk* ranking over long documents — every chunk is indexed under
its row, and the row's ``$rank`` is its best chunk's score.  The vector
index over a chunked column requires the ``vec_dim`` WITH-option.  See
:ref:`Chunked documents (multi-vector rows)
<rampart-sql:Chunked documents (multi-vector rows)>` for the full
pattern, including best-chunk snippets via
:ref:`abstract <sql-server-funcs:abstract>`.

.. code-block:: sql

    -- one row per document; k chunk vectors in v
    INSERT INTO docs (id, doc, v) VALUES (?, ?, chunkembed(?));

**Caller-supplied chunks.**  The first argument may instead be a
``strlst`` — a list of strings, each of which becomes **exactly one
chunk** in list order (N elements always produce N vectors, so the
caller's own chunk bookkeeping stays positional).  An element that
fits the model window gets its exact vector; an oversized element
gets its ``embed()``-style *combined* vector — the normalized mean
over its sub-window vectors — rather than extra vectors or a
truncation.  Empty elements are skipped.  This replaces the
built-in chunker with your own splitting, mirroring the
``split: function`` option of the JavaScript embedding handles.  From
rampart, pass a :green:`Array` of :green:`Strings` wrapped in
``Sql.list()`` (a bare array parameter is bound as JSON text, not a
list):

.. code-block:: javascript

    var myChunks = mySplitter(docText);    // -> [ "chunk 1 ...", ... ]
    sql.exec("insert into docs values (?, ?, chunkembed(?, 'f16'))",
             [id, docText, Sql.list(myChunks)]);

The prefix argument still applies to every listed chunk.

**Spans (4th argument).**  A numeric list of ``{start, end}`` byte
offsets into the stored document — two numbers per list element, in
element order — tells :ref:`abstract <sql-server-funcs:abstract>`
where each custom chunk came from, so the 5-argument vec form can
seed **best-chunk snippets for custom chunkings** exactly as it does
for the built-in chunker:

.. code-block:: javascript

    // splitter returns [{text, start, end}, ...]
    var parts = mySplitter(docText), texts = [], spans = [];

    for (var i = 0; i < parts.length; i++)
    {
        texts.push(parts[i].text);
        spans.push(parts[i].start, parts[i].end);   // two per element
    }

    sql.exec("insert into docs values (?, ?, chunkembed(?, 'f16', ?, ?))",
             [id, docText, Sql.list(texts), title, Sql.list(spans)]);

The offsets are UTF-8 **bytes**, not JavaScript string indices.  A JS
string position counts *characters*, so offsets taken from
``.indexOf()``/``.length`` arithmetic are only correct for pure-ASCII
documents and silently drift on any multi-byte text.  Compute byte
positions instead (e.g. sum the UTF-8 byte lengths of the pieces).
Mis-computed spans never error: ``abstract()`` clamps them to the text
and aligns its cuts to whole UTF-8 characters — the snippet simply
comes from the wrong part of the document.

Without spans, custom-chunked rows still rank normally under
``LIKEV``; their abstracts fall back to plain keyword snippets.

**The value header.**  Every ``chunkembed()`` result carries a small
self-describing header ahead of its cells recording the chunk count,
dimension, element type and (when known) the chunk spans — for the
built-in chunker the spans are recorded automatically.  The header
is invisible in SQL and in rampart (a selected vector reports its
true cell ``dim``, plus a ``chunkSpans`` property when spans are
present), survives storage in ``varbyte`` columns, and is stripped
by cell-converting ``convert()`` calls.  Values without a header
(``embed()``, user-computed vectors, pre-existing tables) behave
exactly as before.  Because the winning chunk's span now travels
*inside the value*, ``abstract()`` no longer re-runs the chunker at
query time, and the serving side needs no chunker/model agreement
for snippets — only the query embedding still uses the model.
Tables written with headers require correspondingly current
binaries (older builds would misread the header bytes as cells).

chunkavg
""""""""

The document's single combined vector, from the same model run as
``chunkembed()``.

.. code-block:: sql

    chunkavg(text [, dtype [, prefix]])

Returns the L2-normalized mean of the document's chunk vectors — the
same value ``embed(text)`` returns (when no prefix is given) — exposed
as its own scalar so a
table can store **both** the chunk array (``chunkembed()``, fine
per-passage ranking) and the combined vector (``chunkavg()``, a second
independently-indexable column for coarse first-stage search) from one
model run:

.. code-block:: sql

    -- the shared result cache makes this ONE model run:
    INSERT INTO docs (id, avgv, v) VALUES (?, chunkavg(?), chunkembed(?));

The optional ``prefix`` argument is the per-chunk prefix described under
``chunkembed()``; when storing prefixed chunk vectors, pass the **same**
prefix to ``chunkavg()`` (and ``chunkcoherence()``) — the trio then still
shares one cached model run, and the average stays consistent with the
chunks it summarizes.

chunkcoherence
""""""""""""""

How single-topic a document is, from its chunk vectors.

.. code-block:: sql

    chunkcoherence(text [, prefix])

Returns a :green:`Number` (SQL ``double``) in ``[0, 1]``: the average
pairwise cosine similarity between the document's chunk vectors.  ``1.0``
for a single-chunk document or one that stays tightly on topic; values
near ``0`` indicate a diffuse, multi-topic document.  Useful as a stored
signal for deciding whether a document's combined vector
(``chunkavg()``) is a faithful summary or whether per-chunk search
(``chunkembed()``) is needed.

Shares the model run with the other embedding scalars via the result
cache (:ref:`likevCache <sql-set:likevCache>`).


General Functions
~~~~~~~~~~~~~~~~~

exec
""""

Execute an external command. The syntax is

.. code-block:: sql

       exec(commandline[, INPUT[, INPUT[, INPUT[, INPUT]]]]);

Allows execution of an external command. The first argument is the
command to execute. Any subsequent arguments are written to the standard
input of the process. The standard output of the command is read as the
return from the function.

For example this could be used to extract text from a pdf.

.. code-block:: sql

         UPDATE     DOCUMENTS
         SET        TEXT = exec('pdftotext '+PDFFILE)
         WHERE      TEXT = '';

mminfo
""""""

This function lets you obtain Metamorph info. You have the choice of
either just getting the portions of the document which were the hits, or
you can also get messages which describe each hit and subhits.

The SQL to use is as follows:

.. code-block:: sql

        SELECT mminfo(query,data[,nhits,[0,msgs]]) from TABLE
               [where CONDITION];

query
    Query should be a string containing a Metamorph query.

data
    The text to search. May be literal data or a field from the table.

nhits
    The maximum number of hits to return. If it is 0, which is the
    default, you will get all the hits.

msgs
    An integer; controls what information is returned. A bit-wise OR of
    any combination of the following values:

    -  1 to get matches and offset/length information

    -  2 to suppress text from ``data`` which matches; printed by
       default

    -  4 to get a count of hits (up to ``nhits``)

    -  8 to get the hit count in a numeric parseable format

    -  16 to get the offset/length in the original query of each search
       set

    Set offset/length information (value 16) is of the form:

    ::

        Set N offset/len in query: setoff setlen

    Where ``N`` is the set number (starting with 1), ``setoff`` is the
    byte offset from the start of the query where set ``N`` is, and
    ``setlen`` is the length of the set.

    Hit offset/length information is of the form:

    ::

        300 <Data from Texis> offset length suboff sublen [suboff sublen]..
        301 End of Metamorph hit

    Where:

    -  offset is the offset within the data of the overall hit context
       (sentence, paragraph, etc...)

    -  length is the length of the overall hit context

    -  suboff is the offset within the hit of a matching term

    -  sublen is the length of the matching term

    -  suboff and sublen will be repeated for as many terms as are
       required to satisfy the query.



    Example:

    ::

       select mminfo('power struggle @0 w/.',Body,0,0,1) inf from html
              where Title\Meta\Body like 'power struggle';

    Would give a result similar to the following:

::

    300 <Data from Texis> 62 5 0 5
    power
    301 End of Metamorph hit
    300 <Data from Texis> 2042 5 0 5
    power
    301 End of Metamorph hit
    300 <Data from Texis> 2331 5 0 5
    POWER
    301 End of Metamorph hit
    300 <Data from Texis> 2892 8 0 8
    STRUGGLE
    301 End of Metamorph hit


convert
"""""""

The convert function allows you to change the type of an expression. The
syntax is

.. code-block:: sql

       CONVERT(expression, 'type-name'[, 'mode'])

The type name should in general be in lower case.

This can be useful in a number of situations. Some cases where you might
want to use convert are

-  The display format for a different format is more useful. For example
   you might want to convert a field of type COUNTER to a DATE field, so
   you can see when the record was inserted, for example:

   ::

           SELECT convert(id, 'date')
           FROM   LOG;

   ::

           CONVERT(id, 'date')
           2015-01-27 22:43:48

*  If you have an application which is expecting data in a particular
   type you can use convert to make sure you will receive the correct
   type.

* The optional third argument given to ``convert()`` is a
  :ref:`sql-set:varcharToStrlstMode` mode value.  This third argument may
  only be supplied when converting to type ``strlst`` or ``varstrlst``.  It
  allows the separator character or mode to be conveniently specified
  locally to the conversion, instead of having to alter the global
  :ref:`sql-set:varcharToStrlstMode` mode.

* Note that ``convert()``\ ing data
  from/to ``varbyte``/``varchar`` does not convert the data to/from
  hexadecimal by default in programs
  other than ``tsql``; it is preserved as-is (though truncated at nul
  for ``varchar``). See the `bintohex`_\ () and `hextobin`_\ () functions
  for hexadecimal conversion, and the :ref:`sql-set:hexifyBytes` SQL property
  for controlling automatic hex conversion.

seq
"""

Returns a sequence number. The number can be initialized to any value,
and the increment can be defined for each call. The syntax is:

.. code-block:: sql

        seq(increment [, init])

If init is given then the sequence number is initialized to that value,
which will be the first value returned. It is then incremented by increment.
If init is not specified then the current value will be retained. The
initial value will be zero if init has not been specified.

Examples of typical use:

.. code-block:: sql

         SELECT  NAME, seq(1)
         FROM    SYSTABLES

The results are:

::

      NAME                seq(1)
     SYSTABLES               0
     SYSCOLUMNS              1
     SYSINDEX                2
     SYSUSERS                3
     SYSPERMS                4
     SYSTRIG                 5
     SYSMETAINDEX            6

.. code-block:: sql

         SELECT  seq(0, 100)
         FROM    SYSDUMMY;

         SELECT  NAME, seq(1)
         FROM    SYSTABLES

The results are:

::

      seq(0, 100)
         100

      NAME                seq(1)
     SYSTABLES             100
     SYSCOLUMNS            101
     SYSINDEX              102
     SYSUSERS              103
     SYSPERMS              104
     SYSTRIG               105
     SYSMETAINDEX          106


random
""""""

Returns a random int. The syntax is:

.. code-block:: sql

        random(max [, seed])

If seed is given then the random number generator is seeded to that
value. The random number generator will only be seeded once in each
session, and will be randomly seeded on the first call if no seed is
supplied. The seed parameter is ignored in the second and later calls to
random in a process.

The returned number is always non-negative, and never larger than the
limit of the C lib’s random number generator (typically either 32767 or
2147483647). If max is non-zero, then the returned number will also be
less than max.

This function is typically used to either generate a random number for
later use, or to generate a random ordering of result records by adding
random to the ORDER BY clause.

Examples of typical use:

.. code-block:: sql

         SELECT  NAME, random(100)
         FROM    SYSTABLES

The results might be:

::

      NAME                random(100)
     SYSTABLES               90
     SYSCOLUMNS              16
     SYSINDEX                94
     SYSUSERS                96
     SYSPERMS                 1
     SYSTRIG                 84
     SYSMETAINDEX            96

.. code-block:: sql

         SELECT  ENAME
         FROM    EMPLOYEE
         ORDER BY random(0);

The results would be a list of employees in a random order.


bintohex
""""""""

Converts a binary (``varbyte``) value into a hexadecimal string.

.. code-block:: sql

        bintohex(varbyteData[, 'stream|pretty'])

A string (``varchar``) hexadecimal representation of the ``varbyteData``
parameter is returned. This can be useful to visually examine binary
data that may contain non-printable or nul bytes. The optional second
argument is one of the following flags:

-  ``stream``: Use the default output mode: a continuous stream of
   hexadecimal bytes.

-  ``pretty``: Return a “pretty” version of the data: print 16 bytes per
   line, space-separate the hexadecimal bytes, and print an ASCII dump
   on the right side.

.. already mentioned above
   Caveat: Note that `convert()``\ ing data from/to
   ``varbyte``/``varchar`` converts the data to/from hexadecimal
   by default when using ``tsql``; otherwise it is preserved as-is 
   (though truncated at nul for ``varchar``). See the
   :ref:`sql-set:hexifyBytes` SQL property to change this.


hextobin
""""""""

Converts a hexadecimal stream to its binary representation.

.. code-block:: sql

        hextobin(hexString[, 'stream|pretty'])

The hexadecimal ``varchar`` string ``hexString`` is converted to its
binary representation, and the ``varbyte`` result returned. The optional
second argument is one of the following flags:

-  ``stream``: Only accept the ``stream`` format of ``bintohex()``, i.e.
   a stream of hexadecimal bytes, the same format that
   ``bintohex(varbyteData, 'stream')`` returns.
   Whitespace is acceptable, but only between
   (not within) hexadecimal bytes. Case-insensitive. Non-conforming data
   will result in an error message and the function failing.

-  ``pretty``: Accept either ``stream`` or ``pretty`` `bintohex`_ formatted data; 
   if the latter, only the hexadecimal bytes are parsed (e.g. ASCII column
   is ignored). Parsing is more liberal, but may be confused if the data
   deviates significantly from either format.

.. already mentioned in convert
   The ``hextobin()`` function was added in Texis version 7. Caveat: Note
   that in version 7 and later, ``convert()``\ ing data from/to
   ``varbyte``/``varchar`` no longer converts the data to/from hexadecimal
   by default (as was done in earlier versions) in programs other than
   ``tsql``; it is now preserved as-is (though truncated at nul for
   ``varchar``). See the ``hexifybytes`` SQL property (p. ) to change this.


identifylanguage
""""""""""""""""

Tries to identify the predominant language of a given string. By
returning a probability in addition to the identified language, this
function can also serve as a test of whether the given string is really
natural-language text, or perhaps binary/encoded data instead. Syntax:

.. code-block:: sql

        identifylanguage(text[, language[, samplesize]])

The return value is a two-element ``strlst``: a probability and a
language code. The probability is a value from 0.000 to 1.000 that the
text argument is composed in the language named by the returned language
code. The language code is a two-letter ISO-639-1 code.

If an ISO-639-1 code is given for the optional language argument, the
probability for that particular language is returned, instead of for the
highest-probability language of the known/built-in languages (currently
de, es, fr, ja, pl, tr, da, en, eu, it, ko, ru).

The optional third argument samplesize is the initial integer size in
bytes of the text to sample when determining language; it defaults to
16384. 

Note that since a ``strlst`` value is returned, the probability is
returned as a ``strlst`` element, not a ``double`` value, and thus
should be cast to ``double`` during comparisons.

The ``identifylanguage()`` function is experimental, and its behavior,
syntax, name and/or existence are subject to change.

Example:

.. code-block:: javascript

   var Sql = require("rampart-sql");

   var sql = new Sql.connection("./testdb", true);

   // ignore error if table doesn't exist
   try {
      sql.exec("drop table idtext;");
   } catch(e){}

   // (re)create the table
   sql.exec("create table idtext (text varchar(64));");


   var texts = [
      `Now we are engaged in a great civil war, testing whether that nation, or
      any nation so conceived and so dedicated, can long endure.  We are met on
      a great battle-field of that war.  We have come to dedicate a portion of that
      field, as a final resting place for those who here gave their lives that
      that nation might live.  It is altogether fitting and proper that we should
      do this.`,

      `Maintenant, nous sommes engagés dans une grande guerre civile, testant
      si cette nation, ou toute autre nation ainsi conçue et si dévouée, peut
      durer longtemps.  Nous sommes rencontrés sur un grand champ de bataille
      de cette guerre.  Nous sommes venus pour consacrer une partie de ce
      champ, comme lieu de repos final pour ceux qui ici ont donné leur vie
      pour que cette nation puisse vivre.  Il est tout à fait approprié et
      approprié que nous fassions cela.`,

      `Ahora estamos inmersos en una gran guerra civil, probando si esa nación,
      o cualquier nación así concebida y dedicada, puede durar mucho tiempo. 
      Nos encontramos en un gran campo de batalla de esa guerra.  Hemos venido
      a dedicar una porción de ese campo, como lugar de descanso final para
      quienes aquí dieron su vida para que viviera esa nación.  Es totalmente
      apropiado y apropiado que hagamos esto.`,

      `Jetzt sind wir in einen großen Bürgerkrieg verwickelt, in dem geprüft
      wird, ob diese Nation oder eine so konzipierte und engagierte Nation
      lange bestehen kann.  Wir treffen uns auf einem großen Schlachtfeld
      dieses Krieges.  Wir sind gekommen, um einen Teil dieses Feldes als
      letzte Ruhestätte für diejenigen zu widmen, die hier ihr Leben gegeben
      haben, damit diese Nation leben kann.  Es ist durchaus angemessen und
      richtig, dass wir dies tun.`
   ];

   // insert text
   for (var i=0;i<texts.length;i++) {
      sql.exec("insert into idtext values (?);", [texts[i]]);
   }

   //identify text
   var res = sql.exec("select identifylanguage(text) from idtext;");

   rampart.utils.printf("%3J\n", res.rows);
   /* expected output:
   [
      {
         "identifylanguage(text)": [
            "0.602603",
            "en"
         ]
      },
      {
         "identifylanguage(text)": [
            "0.612079",
            "fr"
         ]
      },
      {
         "identifylanguage(text)": [
            "0.626431",
            "es"
         ]
      },
      {
         "identifylanguage(text)": [
            "0.614251",
            "de"
         ]
      }
   ]
   */

lookup
""""""

By combining the ``lookup()`` function with a ``GROUP BY``, a column may
be grouped into bins or ranges – e.g. for price-range grouping – instead
of distinct individual values. Syntax:

.. code-block:: sql

        lookup(keys, ranges[, names])

The keys argument is one (or more, e.g. ``strlst``) values to look up;
each is searched for in the ranges argument, which is one (or more, e.g.
``strlst``) ranges. All range(s) that the given key(s) match will be
returned. If the names argument is given, the corresponding names
value(s) are returned instead; this allows ranges to be renamed into
human-readable values. If names is given, the number of its values must
equal the number of ranges.

Each range is a pair of values (lower and upper bounds) separated by
“..” (two periods). The range is optionally surrounded by square (bound
included) or curly (bound excluded) brackets. E.g.:

::

    [10..20}

denotes the range 10 to 20: including 10 (“[”) but not including (“}”)
20. Both an upper and lower bracket must be given if either is present
(though they need not be the same type). The default if no brackets are
given is to include the lower bound but exclude the upper bound; this
makes consecutive ranges non-overlapping, if they have the same upper
and lower bound and no brackets (e.g. “0..10,10..20”). Either bound may
be omitted, in which case that bound is unlimited. Each range’s lower
bound must not be greater than its upper bound, nor equal if either
bound is exclusive.

If a ranges value is not varchar/char, or does not contain “..”, its
entire value is taken as a single inclusive lower bound, and the
exclusive upper bound will be the next ranges value’s lower bound (or
unlimited if no next value). E.g. the ``varint`` lower-bound list:

::

    0,10,20,30

is equivalent to the ``strlst`` range list:

::

    [0..10},[10..20},[20..30},[30..]

By using the ``lookup()`` function in a ``GROUP BY``, a column may be
grouped into ranges. For example, given a table Products with the
following SKUs and ``float`` prices:

::

        SKU    Price
        ------------
        1234   12.95
        1235    5.99
        1236   69.88
        1237   39.99
        1238   29.99
        1239   25.00
        1240   50.00
        1241   -2.00
        1242  499.95
        1243   19.95
        1244    9.99
        1245  125.00

they may be grouped into price ranges (with most-products first) with
this SQL:

.. code-block:: sql

    SELECT   lookup(Price, convert('0..25,25..50,50..,', 'strlst', 'lastchar'),
         convert('Under $25,$25-49.99,$50 and up,', 'strlst', 'lastchar'))
           PriceRange, count(SKU) NumberOfProducts
    FROM Products
    GROUP BY lookup(Price, convert('0..25,25..50,50..,', 'strlst', 'lastchar'),
         convert('Under $25,$25-49.99,$50 and up,', 'strlst', 'lastchar'))
    ORDER BY 2 DESC;

Full example in Rampart JavaScript:

.. code-block:: javascript

   var Sql=require("rampart-sql");

   var sql=new Sql.connection("./testdb");

   // ignore error if table doesn't exist
   try {
      sql.exec("drop table Products;");
   } catch(e){}

   // (re)create the table
   sql.exec("create table Products (SKU int, Price double);");

   var skus = [ 1234, 1235, 1236, 1237, 1238, 1239, 1240,
                1241, 1242, 1243, 1244, 1245 ];
   var prices = [ 12.95, 5.99, 69.88, 39.99, 29.99, 25.00, 50.00,
                  -2.00, 499.95, 19.95, 9.99, 125.00 ];

   for (var i=0;i<skus.length; i++)
       sql.exec("insert into Products values (?,?)", [skus[i],prices[i]]);
     
   var range=['0..25','25..50','50..'];
   var rangenames=['Under $25','$25-$49','$50 and up'];
   var res = sql.exec(
       "SELECT lookup( Price, convert(?,'strlst','json'), convert(?,'strlst','json') ) PriceRange,"+
       "count(SKU) NumberOfProducts FROM Products " +
       "GROUP BY lookup(Price, convert(?,'strlst','json'), convert(?,'strlst','json') )" +
       "ORDER BY 2 DESC",
       [range,rangenames,range,rangenames],
       {returnType:"array"}
   );

   rows=res.rows;
   cols=res.columns;
   for (i=0;i<rows.length;i++) {

           if (!i) {
               rampart.utils.printf("%-12s %16s\n", cols[0] , cols[1]);
               rampart.utils.printf("------------+----------------\n");
           }

           rampart.utils.printf("%-12s %16s\n", rows[i][0][0], rows[i][1]);

   }
   /* expected output :
   PriceRange   NumberOfProducts
   ------------+----------------
   $50 and up                  4
   Under $25                   4
   $25-$49                     3
                               1
   */


Note that:

* In the ``tsql`` example, the trailing commas in the ``PriceRange`` values are used
  to converted to ``strlst`` values via the ``convert(.., .., 'lastchar')``
  function.  In the Rampart JavaScript version, the array of strings are
  converted into a ``strlst`` using ``convert(.., .., 'json')`` function.
  See `convert`_ () mode above for details.

* The empty PriceRange for the fourth row: the -2 Price matched no ranges, and hence an
  empty PriceRange was returned for it.


See also: `lookupCanonicalizeRanges`_, `lookupParseRange`_


lookupCanonicalizeRanges
""""""""""""""""""""""""

The ``lookupCanonicalizeRanges()`` function returns the canonical
version(s) of its ranges argument, which is zero or more ranges of the
syntaxes used in `lookup`_\ () function above:

.. code-block:: sql

        lookupCanonicalizeRanges(ranges, keyType)

The canonical version always includes both a lower and upper
inclusive/exclusive bracket/brace, both lower and upper bounds (unless
unlimited), the “..” range operator, and is independent of other ranges
that may be in the sequence.

The keyType parameter is a ``varchar`` string denoting the SQL type of
the key field that would be looked up in the given range(s). This
ensures that comparisons are done correctly. E.g. for a ``strlst`` range
list of “0,500,1000”, keyType should be “integer”, so that “500” is not
compared alphabetically with “1000” and considered invalid (greater
than).

This function can be used to verify the syntax of a range, or to
transform it into a standard form for `lookupParseRange`_\ ().

For an implicit-upper-bound range, the upper bound is determined by the
*next* range’s lower bound. Thus the full list of ranges (if multiple)
should be given to ``lookupCanonicalizeRanges()`` – even if only one
range needs to be canonicalized – so that each range gets its proper
bounds.

See also: `lookup`_, `lookupParseRange`_


lookupParseRange
""""""""""""""""

The ``lookupParseRange()`` function parses a single `lookup`_\ () style
range into its constituent parts, returning them as strings in one
``strlst`` value. This can be used by scripts to edit a range.
Syntax:

.. code-block:: sql

        lookupParseRange(range, parts)

The parts argument is zero or more of the following part tokens as
strings:

-  ``lowerInclusivity``: Returns the inclusive/exclusive operator for
   the lower bound, e.g. “{” or “”

-  ``lowerBound``: Returns the lower bound of the range, e.g. “10”

-  ``rangeOperator``: Returns the range operator, e.g. “..”

-  ``upperBound``: Returns the upper bound of the range, e.g. “20”

-  ``upperInclusivity``: Returns the inclusive/exclusive operator for
   the upper bound, e.g. “}” or “”

If a requested part is not present, an empty string is returned for that
part. The concatenation of the above listed parts, in the above order,
should equal the given range. Non-string range arguments are not
supported.

.. code-block:: sql

        lookupParseRange('10..20', 'lowerInclusivity')

would return a single empty-string ``strlst``, as there is no
lower-bound inclusive/exclusive operator in the range “10..20”.

.. code-block:: sql

        lookupParseRange('10..20', 'lowerBound')

would return a ``strlst`` with the single value “10”.

For an implicit-upper-bound range, the upper bound is determined by the
*next* range’s lower bound. Since ``lookupParseRange()`` only takes one
range, passing such a range to it may result in an incorrect (unlimited)
upper bound. Thus the full list of ranges (if multiple) should always be
given to `lookupCanonicalizeRanges`_\ () first, and only then the desired
canonicalized range passed to ``lookupParseRange()``.

See also: `lookup`_, `lookupCanonicalizeRanges`_


.. unneeded
   hasFeature
   """"""""""

   Returns 1 if given feature is supported, 0 if not (or unknown). The
   syntax is:

   .. code-block:: sql

           hasFeature(feature)

   where feature is one of the following ``varchar`` tokens:

   -  ``RE2`` For RE2 regular expression support in REX

   This function is typically used in Vortex scripts to test if a feature
   is supported with the current version of Texis, and if not, to work
   around that fact if possible. For example:

   ::

            <if hasFeature( "RE2" ) = 1>
              ... proceed with RE2 expressions ...
            <else>
              ... use REX instead ...
            </if>



ifNull
""""""

Substitute another value for NULL values. Syntax:

.. code-block:: sql

       ifNull(testVal, replaceVal)

If ``testVal`` is a SQL NULL value, then ``replaceVal`` (cast to the
type of ``testVal``) is returned; otherwise ``testVal`` is returned.
This function can be used to ensure that NULL value(s) in a column are
replaced with a non-NULL value, if a non-NULL value is required:

.. code-block:: sql

        SELECT ifNull(myColumn, 'Unknown') FROM myTable;



isNull
""""""

Tests a value, and returns a ``long`` value of 1 if NULL, 0 if not.
Syntax:

.. code-block:: sql

       isNull(testVal)

.. code-block:: sql

        SELECT isNull(myColumn) FROM myTable;

Note that Texis ``isNull`` behavior differs from some other SQL
implementations: ordinary columns are non-nullable (an unset ``varchar``
is an empty string, not NULL, and a ``NULL`` literal is not accepted in
``INSERT``), so ``isNull`` returns 1 only for values that are genuinely
SQL NULL, which rarely arise from Rampart JavaScript.  See also
`ifNull`_.

.. not in this version
   xmlTreeQuickXPath
   """""""""""""""""

   Extracts information from an XML document.

   .. code-block:: sql

           xmlTreeQuickXPath(string xmlRaw, string xpathQuery
               [, string[] xmlns)

   Parameters:

   -  ``xmlRaw`` - the plain text of the xml document you want to extract
      information from

   -  ``xpathQuery`` - the XPath expression that identifies the nodes you
      want to extract the data from

   -  ``xmlns`` *(optional)* - an array of ``prefix=URI`` namespaces to use
      in the XPath query

   Returns:

   -  String values of the node from the XML document ``xmlRaw`` that match
      ``xpathQuery``

   ``xmlTreeQuickXPath`` allows you to easily extract information from an
   XML document in a one-shot function. It is intended to be used in SQL
   statements to extract specific information from a field that contains
   XML data.

   If the ``xmlData`` field of a table has content like this:

   ::

       <extraInfo>
           <price>8.99</price>
           <author>John Doe</author>
           <isbn>978-0-06-051280-4</isbn>
       </extraInfo>

   Then the following SQL statement will match that row:

   .. code-block:: sql

       SELECT * from myTable where xmlTreeQuickXPath(data,
       '/extraInfo/author') = 'John Doe'

File functions
~~~~~~~~~~~~~~


fromfile, fromfiletext
""""""""""""""""""""""

The ``fromfile`` and ``fromfiletext`` functions read a file. The syntax
is

.. code-block:: sql

       fromfile(filename[, offset[, length]])
       fromfiletext(filename[, offset[, length]])

These functions take one required, and two optional arguments. The first
argument is the filename. The second argument is an offset into the
file, and the third argument is the length of data to read. If the
second argument is omitted then the file will be read from the
beginning. If the third argument is omitted then the file will be read
to the end. The result is the contents of the file. This can be used to
load data into a table. For example if you have an indirect field and
you wish to see the contents of the file you can issue SQL similar to
the following.

The difference between the two functions is the type of data that is
returned. ``fromfile`` will return varbyte data, and ``fromfiletext``
will return varchar data. If you are using the functions to insert data
into a field you should make sure that you use the appropriate function
for the type of field you are inserting into.

.. code-block:: sql

         SELECT  FILENAME, fromfiletext(FILENAME)
         FROM    DOCUMENTS
         WHERE   DOCID = 'JT09113' ;

The results are:

.. code-block:: sql

      FILENAME            fromfiletext(FILENAME)
      /docs/JT09113.txt   This is the text contained in the document
      that has an id of JT09113.

.. not available
   totext
   """"""

   Converts data or file to text. The syntax is

   .. code-block:: sql

          totext(filename[, args])
          totext(data[, args])

   This function will convert the contents of a file, if the argument given
   is an indirect, or else the result of the expression, and convert it to
   text. It does this by calling the program ``anytotx``, which must be in
   the path. The ``anytotx`` program (obtained from Thunderstone) will
   handle ``PDF`` as well as many other file formats.

   The ``totext`` command will take an
   optional second argument which contains arguments to the ``anytotx``
   program. See the documentation for ``anytotx`` for details on its
   arguments.

   .. code-block:: sql

            SELECT  FILENAME, totext(FILENAME)
            FROM    DOCUMENTS
            WHERE   DOCID = 'JT09113' ;

   The results are:

   ::

         FILENAME            totext(FILENAME)
         /docs/JT09113.pdf   This is the text contained in the document
         that has an id of JT09113.


toind
"""""

Create a Texis managed indirect file. The syntax is

.. code-block:: sql

       toind(data)

This function takes the argument, stores it into a file, and returns the
filename as an ``indirect`` type. This is most often used in combination
with ``fromfile`` to create a Texis managed file. For example:

.. code-block:: sql

         INSERT  INTO DOCUMENTS
         VALUES('JT09114', toind(fromfile('srcfile')))

The database will now contain a pointer to a copy of ``srcfile``, which
will remain searchable even if the original is changed or removed. An
important point to note is that any changes to ``srcfile`` will not be
reflected in the database, unless the table row’s ``indirect`` column is
modified (even to the same value, this just tells Texis to re-index it).


canonpath
"""""""""

Returns canonical version of a file path, i.e. fully-qualified and
without symbolic links:

.. code-block:: sql

      canonpath(path[, flags])

The optional ``flags`` is a set of bit flags: bit 0 set if error
messages should be issued, bit 1 set if the return value should be empty
instead of ``path`` on error.


pathcmp
"""""""

File path comparison function; like C function ``strcmp()`` but for
paths:

.. code-block:: sql

      pathcmp(pathA, pathB)

Returns an integer indicating the sort order of ``pathA`` relative to
``pathB``: 0 if ``pathA`` is the same as ``pathB``, less than 0 if
``pathA`` is less than ``pathB``, greater than 0 if ``pathA`` is greater
than ``pathB``. 

.. nope 
   Paths are compared case-insensitively if and only if the
   OS is case-insensitive for paths, and OS-specific alternate directory
   separators are considered the same (e.g. “``\``” and “``/``” in
   Windows). 

Multiple consecutive directory separators are considered the
same as one. A trailing directory separator (if not also a leading
separator) is ignored. Directory separators sort lexically before any
other character.

Note that the paths are only compared lexically: no attempt is made to
resolve symbolic links, “..” path components, etc. Note also that no
inference should be made about the magnitude of negative or positive
return values: greater magnitude does not necessarily indicate greater
lexical “separation”, nor should it be assumed that comparing the same
two paths will always yield the same-magnitude value in future versions.
Only the sign of the return value is significant.


basename
""""""""

Returns the base filename of a given file path.

.. code-block:: sql

      basename(path)

The basename is the contents of ``path`` after the last path separator.
No filesystem checks are performed, as this is a text/parsing function;
thus “``.``” and “``..``” are not significant.


dirname
"""""""

Returns the directory part of a given file path.

.. code-block:: sql

      dirname(path)

The directory is the contents of ``path`` before the last path separator
(unless it is significant – e.g. for the root directory – in which case
it is retained). No filesystem checks are performed, as this is a text/parsing function;
thus “``.``” and “``..``” are not significant.


fileext
"""""""

Returns the file extension of a given file path.

.. code-block:: sql

      fileext(path)

The file extension starts with and includes a dot. The file extension is
only considered present in the basename of the path, i.e. after the last
path separator.


joinpath
""""""""

Joins one or more file/directory path arguments into a merged path,
inserting/removing a path separator between arguments as needed. Takes
one to 5 path component arguments. E.g.:

.. code-block:: sql

      joinpath('one', 'two/', '/three/four', 'five')

yields

::

      one/two/three/four/five


Redundant path separators internal to an argument are not removed, 
nor are “.” and “ ..” path components removed.


joinpathabsolute
""""""""""""""""

Like ``joinpath``, except that a second or later argument that is an
absolute path will overwrite the previously-merged path. E.g.:

.. code-block:: sql

      joinpathabsolute('one', 'two', '/three/four', 'five')

yields

::

      /three/four/five

.. nope
   Under Windows, partially absolute path arguments – e.g. “ /dir”
   where the drive or dir is still relative – are considered
   absolute for the sake of overwriting the merge.

Redundant path separators
internal to an argument are not removed, nor are “.” and “..” path
components removed.

String Functions
~~~~~~~~~~~~~~~~


abstract
""""""""

Generate an abstract of a given portion of text. The syntax is

.. code-block:: sql

       abstract(text[, maxsize[, style[, query[, vecColumn]]]])

The abstract text will be cut to ``maxsize`` bytes, and will
attempt to end at a word boundary (a multi-byte UTF-8 character is
never split, so the cut always falls on a whole character). If
``maxsize`` is not specified (or is less than or equal to 0) then a
default size of 230 bytes is used.  Note that when text is cut, a
trailing ``" ..."`` is appended, so the returned string may be up to
four bytes longer than ``maxsize`` (a truncated default-size abstract
is 233 bytes).

The ``style`` argument is a string or integer, and allows a choice
between several different ways of creating the abstract. Note that some
of these styles require the ``query`` argument as well, which is a
Metamorph query to look for:

* ``dumb`` (0) - Start the abstract at the top of the document.

* ``smart`` (1) - This style will look for the first meaningful 
  chunk of text, skipping over any headers at the top of the text.  This
  is the default if neither ``style`` nor ``query`` is given.

* ``querysingle`` (2) -
  Center the abstract contiguously on the best occurrence of ``query``
  in the document.

* ``querymultiple`` (3) -
  Like ``querysingle``, but also break up the abstract into multiple
  sections (separated with “``...``”) if needed to help ensure all
  terms are visible. Also take care with URLs to try to show the
  start and end.

* ``querysingleoffset`` (4) -
  Like ``querysingle``, but the abstract is prefixed with ``@start: ``
  — the **byte** offset in ``text`` at which the abstract begins,
  e.g. ``@980: housekeeping data downlink…``.  Intended for making a
  search result clickable: the offset locates the passage in the
  original document.

  Only the start is given, deliberately.  ``abstract()`` collapses
  whitespace runs and long repeated-byte runs (``-------``) as it
  renders, so its output is **not** a verbatim substring of ``text``
  and no length would let a caller reproduce it by slicing.  When
  exact spans are needed, use
  :ref:`excerpt() <sql-server-funcs:excerpt>`\ 's ``offsets=1``
  option, whose passages are copied verbatim and carry
  ``@start+length: ``.

  Works with the 5th (vector column) argument as well, where the
  offset names the start of the best-chunk snippet.

* ``querybest`` -
  An alias for the best available query-based style; currently the
  same as ``querymultiple``. Using ``querybest`` in a script ensures
  that if improved styles become available in future releases, the
  script will automatically “upgrade” to the best style.

If no ``query`` is given for the ``query``\ :math:`...` modes, they fall
back to ``dumb`` mode. If a ``query`` is given with a
*non-*\ ``query``\ :math:`...` mode (``dumb``/``smart``), the mode is
promoted to ``querybest``. The current locale and index expressions also
have an effect on the abstract in the ``query``\ :math:`...` modes, so
that it more closely reflects an index-obtained hit.

.. code-block:: sql

         SELECT     abstract(STORY, 0, 1, 'power struggle')
         FROM       ARTICLES
         WHERE      ARTID = 'JT09115' ;

**Vector (best-chunk) snippets** — the optional 5th argument names the
row's vector column.  When the column holds *chunked* values
(:ref:`chunkembed() <sql-server-funcs:chunkembed>`), the abstract is
seeded from the text span of the row's **best-matching chunk**: with a
non-empty ``query`` string, ``abstract()`` embeds the query itself
(a cache hit when the surrounding query's ``LIKEV`` used the same
string) and scores the row's chunks with the same scorer ``LIKEV``
uses — so it works in any query shape, including per-row ``id = ?``
lookups with no ``LIKEV`` at all.  With an empty ``query`` string it
instead uses the winning chunk recorded by a ``LIKEV`` in the same
query's ``WHERE`` clause:

.. code-block:: sql

    SELECT Title, abstract(Doc, 200, 'smart', ?, Vec) snip, $rank
      FROM docs
     WHERE Vec LIKEV ? ;

With an empty ``query`` string (``''``) the snippet is centered purely
on the winning chunk; with a real query string, keyword hits are merged
with the chunk span (hybrid).  The winning chunk's byte span is read
directly from the vector value: ``chunkembed()`` records the spans in
the value's header — automatically for the built-in chunker, and from
the caller-supplied 4th argument for custom chunkings — so no chunker
runs at query time and any embedding engine may serve the query.  Only
for older, *headerless* values is the span recomputed from the text by
the loaded embedding model's chunker; for those rows the same engine
(:ref:`llamaEmbed <sql-set:llamaEmbed>` /
:ref:`onnxEmbed <sql-set:onnxEmbed>`) must be set at query time.  See
:ref:`Chunked documents (multi-vector rows)
<rampart-sql:Chunked documents (multi-vector rows)>`.

The vec form works directly in a hybrid rank-fused query
(``Doc LIKEP ? OR Vec LIKEV ?`` — see :ref:`Hybrid keyword + vector
queries (rank fusion)
<hybrid-rank-fusion>`), where
best-chunk snippets seed normally even for rows only the keyword side
contributed (abstract scores that row's chunks itself).  Do not add an
``ORDER BY`` to such a query — see the performance note in the hybrid
section.

See also the Rampart JavaScript :ref:`rampart-sql:abstract()` function.


excerpt
"""""""

Return the passages of a document most relevant to a query — the
retrieval-side companion to ``abstract()``, sized for feeding an LLM
(RAG) rather than for display.  The syntax is

.. code-block:: sql

       excerpt(text, maxsize, query, vecColumn[, window])

``vecColumn`` names the row's *chunked* vector column
(:ref:`chunkembed() <sql-server-funcs:chunkembed>`).  Every chunk is
scored against the embedded ``query`` (the same scorer ``LIKEV`` uses;
embedding the query is a cache hit when the surrounding query used the
same string), and the best-scoring chunks are returned **verbatim**,
assembled in *document order* within ``maxsize`` bytes.  Discontiguous
passages are joined with a ``...`` line; chunks adjacent in the
document (nothing but whitespace between their spans) merge into one
contiguous passage.  A ``maxsize`` of 0 or less means no limit.

The best chunk (plus its neighbors, see ``window``) is always
included, truncated at a word boundary if it alone exceeds
``maxsize``; further chunks are added best-first, only if they fit
whole.  A bare number as the 5th argument sets ``window``; an options
string sets any of the selection options (case-insensitive keys,
underscores in keys ignored — ``kw_hit`` and ``kwhit`` are the same
key):

*   ``window`` (default 0) — each selected chunk brings this many
    neighboring chunks per side along with it, favoring longer
    contiguous context over covering more separate regions.

*   ``kw_hit`` (default 0) — with ``kw_hit=1``, the chunk containing
    the best *keyword* hit of ``query`` is also guaranteed a place
    (budget permitting).  Semantic scoring can prefer a chunk that
    discusses the topic over the one holding a literal fact — a
    number, a name — that the query asks for; this pulls the literal
    match in as well.  No effect when the query has no keyword hit in
    the document.

*   ``lead`` (default 0) — with ``lead=1``, the document's first chunk
    is also guaranteed a place.  Useful for corpora whose opening
    sentences define the subject (encyclopedia articles), grounding
    the excerpt's mid-document passages.

*   ``minsim`` (unset by default) — a score floor: chunks scoring
    below it are never selected on score, even with budget to spare.
    When nothing clears the floor the result is the **empty string** —
    the signal that the document has no relevant chunk.  Guaranteed
    picks (``kw_hit``, ``lead``) are exempt from the floor.

*   ``offsets`` (default 0) — with ``offsets=1``, each passage is
    prefixed with its location in the document as ``@start+length: ``,
    e.g. ``@931+230: ``.  Both numbers are **byte** offsets into
    ``text``, measured after the whitespace trim, so they name exactly
    the bytes that follow the prefix — ``substr(Doc, start, length)``
    reproduces the passage.  Note this is a byte count, not a character
    count: a passage of 504 bytes is 464 characters if it contains
    multi-byte UTF-8, so a caller measuring the returned string in
    characters must not use that figure to index the document.

    The prefixes are **not** counted against ``maxsize`` — that budget
    bounds the excerpt text, so the returned string is longer than
    ``maxsize`` by whatever the prefixes add.  A passage truncated to
    fit the budget reports its truncated length, not its original one.

.. code-block:: sql

    excerpt(Doc, 2000, ?, Vec, 'window=1, kw_hit=1')

Chunk byte spans are located exactly as in ``abstract()``'s vec form
above: from the vector value's own header, with headerless values
falling back to the loaded embedding model's chunker.  When chunks
cannot be scored or located at all — no embedding engine, an empty
``query``, a non-chunked value — the start of the document is returned
instead.

Works in any query shape, including the hybrid rank-fused ``LIKEP OR
LIKEV`` statement, where rows found only by the keyword side still get
semantically selected excerpts:

.. code-block:: sql

    SELECT Id, Title, excerpt(Doc, 2000, ?, Vec) context
      FROM docs
     WHERE Doc LIKEP ? OR Vec LIKEV ? ;


text2mm
"""""""

Generate ``LIKEP`` query. The syntax is

.. code-block:: sql

       text2mm(text[, maxwords])

This function will take a text expression, and produce a list of words
that can be given to ``LIKER`` or ``LIKEP`` to find similar documents.
``text2mm`` takes an optional second argument which specifies how many
words should be returned. If this is not specified then 10 words are
returned. Most commonly ``text2mm`` will be given the name of a field.
If it is an ``indirect`` field you will need to call ``fromfile`` as
shown below:

.. code-block:: sql

         SELECT     text2mm(fromfile(FILENAME))
         FROM       DOCUMENTS
         WHERE      DOCID = 'JT09115' ;

You may also call it as ``texttomm()`` instead of ``text2mm()`` .


keywords
""""""""

Generate list of keywords. The syntax is

.. code-block:: sql

       keywords(text[, maxwords])

keywords is similar to text2mm but produces a list of phrases, with a
linefeed separating them.  The difference between text2mm and keywords is
that keywords will maintain the phrases.  The keywords function also takes
an optional second argument which indicates how many words or phrases should
be returned.


length
""""""

Returns the length in characters of a ``char`` or ``varchar``
expression, or number of strings/items in other types. The syntax is

.. code-block:: sql

      length(value[, mode])

For example:

.. code-block:: sql

         SELECT  NAME, length(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                length(NAME)
     SYSTABLES               9
     SYSCOLUMNS             10
     SYSINDEX                8
     SYSUSERS                8
     SYSPERMS                8
     SYSTRIG                 7
     SYSMETAINDEX           12

The optional ``mode`` argument is a :ref:`sql-set:stringCompareMode`-style compare
mode to use. If ``mode`` is not given, the current apicp 
:ref:`sql-set:stringCompareMode`
is used. Currently the only pertinent ``mode`` flag is “iso-8859-1”,
which determines whether to interpret ``value`` as ISO-8859-1 or UTF-8.
This can alter how many characters long the string appears to be, as
UTF-8 characters are variable-byte-sized, whereas ISO-8859-1 characters
are always mono-byte.

Note that if given a ``strlst``
type ``value``, ``length()`` returns the number of string values in the
list. For other types, it returns the number of values (e.g. for
``varint`` it returns the number of integer values).



lower
"""""

Returns the text expression with all letters in lower-case. The syntax
is

.. code-block:: sql

      lower(text[, mode])

For example:

.. code-block:: sql

         SELECT  NAME, lower(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                lower(NAME)
     SYSTABLES            systables
     SYSCOLUMNS           syscolumns
     SYSINDEX             sysindex
     SYSUSERS             sysusers
     SYSPERMS             sysperms
     SYSTRIG              systrig
     SYSMETAINDEX         sysmetaindex

The optional ``mode`` argument is a string-folding mode.
If ``mode`` is unspecified, the current apicp :ref:`sql-set:stringCompareMode` 
setting – with "+lowercase" added – is used.

upper
"""""

Returns the text expression with all letters in upper-case. The syntax is

.. code-block:: sql

      upper(text[, mode])

For example:

.. code-block:: sql

         SELECT  NAME, upper(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                upper(NAME)
     SYSTABLES            SYSTABLES
     SYSCOLUMNS           SYSCOLUMNS
     SYSINDEX             SYSINDEX
     SYSUSERS             SYSUSERS
     SYSPERMS             SYSPERMS
     SYSTRIG              SYSTRIG
     SYSMETAINDEX         SYSMETAINDEX

The optional ``mode`` argument is a string-folding mode.
If ``mode`` is unspecified, the current apicp :ref:`sql-set:stringCompareMode`
setting – with “+uppercase” added – is used.

initcap
"""""""

Capitalizes text. The syntax is

.. code-block:: sql

      initcap(text[, mode])

Returns the text expression with the first letter of each word in title
case (i.e. upper case), and all other letters in lower-case. For
example:

.. code-block:: sql

         SELECT  NAME, initcap(NAME)
         FROM    SYSTABLES

The results are:

::

      NAME                initcap(NAME)
     SYSTABLES            Systables
     SYSCOLUMNS           Syscolumns
     SYSINDEX             Sysindex
     SYSUSERS             Sysusers
     SYSPERMS             Sysperms
     SYSTRIG              Systrig
     SYSMETAINDEX         Sysmetaindex


The optional ``mode`` argument is a string-folding mode in the same
format as :ref:`sql-set:stringCompareMode`.
If ``mode`` is unspecified, the current :ref:`sql-set:stringCompareMode` setting
– with “+titlecase” added – is used.



sandr
"""""

Search and replace text.

.. code-block:: sql

       sandr(search, replace, text)

Returns the text expression with the search REX expression replaced with
the replace expression. See the Rampart Sql.\ :ref:`rampart-sql:rex()`  and 
the Rampart Sql.\ :ref:`rampart-sql:sandr()` function documentation for 
complete syntax of the search and replace expressions.

.. code-block:: sql

         SELECT  NAME, sandr('>>=SYS=', 'SYSTEM TABLE ', NAME) DESCR
         FROM    SYSTABLES

The results are:

::

      NAME                DESCR
     SYSTABLES            SYSTEM TABLE TABLES
     SYSCOLUMNS           SYSTEM TABLE COLUMNS
     SYSINDEX             SYSTEM TABLE INDEX
     SYSUSERS             SYSTEM TABLE USERS
     SYSPERMS             SYSTEM TABLE PERMS
     SYSTRIG              SYSTEM TABLE TRIG
     SYSMETAINDEX         SYSTEM TABLE METAINDEX


separator
"""""""""

Returns the separator character from its ``strlst`` argument, as a
``varchar`` string:

.. code-block:: sql

       separator(strlstValue)

This can be used in situations where the ``strlstValue`` argument may
have a nul character as the separator, in which case simply converting
``strlstValue`` to ``varchar`` and looking at the last character would
be incorrect.


stringcompare
"""""""""""""

Compares its string (``varchar``) arguments ``a`` and ``b``, returning
-1 if ``a`` is less than ``b``, 0 if they are equal, or 1 if ``a`` is
greater than ``b``:

.. code-block:: sql

      stringcompare(a, b[, mode])

The strings are compared using the optional ``mode`` argument.
If ``mode`` is unspecified, the current apicp 
:ref:`sql-set:stringCompareMode` setting is used.


stringformat
""""""""""""

Returns its arguments formatted into a string (``varchar``), like the
equivalent Sql.\ :ref:`rampart-sql:stringFormat()` (based on the C function
``sprintf()``):

.. code-block:: sql

      stringformat(format[, arg[, arg[, arg[, arg]]]])

The ``format`` argument is a ``varchar`` string that describes how to
print the following argument(s), if any.


Math functions
~~~~~~~~~~~~~~

The following basic math functions are available in Texis: ``acos``,
``asin``, ``atan``, ``atan2``, ``ceil``, ``cos``, ``cosh``, ``exp``,
``fabs``, ``floor``, ``fmod``, ``log``, ``log10``, ``pow``, ``sin``,
``sinh``, ``sqrt``, ``tan``, ``tanh``.

All of the above functions call the ANSI C math library function of the
same name, and return a result of type ``double``. ``pow``, ``atan2``
and ``fmod`` take two double arguments, the remainder take one double
argument.

In addition, the following math-related functions are available:

-  ``isNaN(x)``
   Returns 1 if ``x`` is a float or double NaN (Not a Number) value, 0
   if not. This function should be used to test for NaN, rather than
   using the equality operator (e.g. ``x = 'NaN'``), because the IEEE
   standard defines ``NaN == NaN`` to be false, not true as might be
   expected.

Date functions
~~~~~~~~~~~~~~

The following date functions are available in Texis: ``dayname``,
``month``, ``monthname``, ``dayofmonth``, ``dayofweek``, ``dayofyear``,
``quarter``, ``week``, ``year``, ``hour``, ``minute``, ``second``.

All the functions take a date as an argument. ``dayname`` and
``monthname`` will return a string with the full day or month name based
on the current locale, and the others return a number.

The ``dayofweek`` function returns 1 for Sunday. The quarter is based on
months, so April 1st is the first day of quarter 2. Week 1 begins with
the first Sunday of the year.

The ``monthseq``, ``weekseq`` and ``dayseq`` functions will return the number of
months, weeks and days since an arbitrary past date. These can be used
when comparing dates to see how many months, weeks or days separate
them.

Bit manipulation functions
~~~~~~~~~~~~~~~~~~~~~~~~~~

These functions are used to manipulate integers as bit fields. This can
be useful for efficient set operations (e.g. set membership,
intersection, etc.). For example, categories could be mapped to
sequential bit numbers, and a row’s category membership stored compactly
as bits of an ``int`` or ``varint``, instead of using a string list.
Category membership can then be quickly determined with ``bitand`` on
the integer.

In the following functions, bit field arguments ``a`` and ``b`` are
``int`` or ``varint`` (32 bits per integer, all platforms). Argument
``n`` is any integer type. Bits are numbered starting with 0 as the
least-significant bit of the first integer. 31 is the most-significant
bit of the first integer, 32 is the least-significant bit of the second
integer (if a multi-value ``varint``), etc.

- ``bitand(a, b)``
  Returns the bit-wise AND of ``a`` and ``b``. If one argument is
  shorter than the other, it will be expanded with 0-value integers.

- ``bitor(a, b)``
  Returns the bit-wise OR of ``a`` and ``b``. If one argument is
  shorter than the other, it will be expanded with 0-value integers.

- ``bitxor(a, b)``
  Returns the bit-wise XOR (exclusive OR) of ``a`` and ``b``. If one
  argument is shorter than the other, it will be expanded with
  0-value integers.

- ``bitnot(a)``
  Returns the bit-wise NOT of ``a``.

- ``bitsize(a)``
  Returns the total number of bits addressable in ``a`` -- that is, the
  width of the underlying value, not the position of its highest set bit.
  For an ordinary integer this is ``32`` regardless of the value (so
  ``bitsize(0)``, ``bitsize(1)`` and ``bitsize(5)`` all return ``32``),
  growing as higher bits are set (e.g. ``bitsize(bitset(5,40))`` returns
  ``64``).  For the position of the highest set bit, see ``bitmax()``
  below.

- ``bitcount(a)``
  Returns the number of bits in ``a`` that are set to 1.

- ``bitmin(a)``
  Returns the lowest bit number in ``a`` that is set to 1. If none
  are set to 1, returns -1.

- ``bitmax(a)``
  Returns the highest bit number in ``a`` that is set to 1. If none
  are set to 1, returns -1.

- ``bitlist(a)``
  Returns the list of bit numbers of ``a``, in ascending order, that
  are set to 1, as a ``varint``. Returns a single -1 if no bits are
  set to 1.  Note that a multi-value ``varint`` displays as its first
  element when selected directly; use
  ``convert(bitlist(a),'varchar')`` to see the whole list (e.g.
  ``"0,2"`` for ``bitlist(5)``).

- ``bitshiftleft(a, n)``
  Returns ``a`` shifted ``n`` bits to the left, with 0s padded for
  bits on the right. If ``n`` is negative, shifts right instead.

- ``bitshiftright(a, n)``
  Returns ``a`` shifted ``n`` bits to the right, with 0s padded for
  bits on the left (i.e. an unsigned shift). If ``n`` is negative,
  shifts left instead.

- ``bitrotateleft(a, n)``
  Returns ``a`` rotated ``n`` bits to the left, with left
  (most-significant) bits wrapping around to the right. If ``n`` is
  negative, rotates right instead.

- ``bitrotateright(a, n)``
  Returns ``a`` rotated ``n`` bits to the right, with right
  (least-significant) bits wrapping around to the left. If ``n`` is
  negative, rotates left instead.

- ``bitset(a, n)``
  Returns ``a`` with bit number ``n`` set to 1. ``a`` will be padded
  with 0-value integers if needed to reach ``n`` (e.g.
  ``bitset(5, 40)`` will return a ``varint(2)``).  Note that a
  multi-integer ``varint`` result retrieved from JavaScript currently
  yields only the first integer; convert in SQL (e.g. to ``varchar``)
  to see the full value.

- ``bitclear(a, n)``
  Returns ``a`` with bit number ``n`` set to 0. ``a`` will be padded
  with 0-value integers if needed to reach ``n`` (e.g.
  ``bitclear(5, 40)`` will return a ``varint(2)``).

- ``bitisset(a, n)``
  Returns 1 if bit number ``n`` is set to 1 in ``a``, 0 if not.

Internet/IPV4 address functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following functions manipulate IP network and/or host addresses;
most take ``inet`` style argument(s). This is an IPv4 address string,
optionally followed by a netmask.

For IPv4, the format is dotted-decimal, i.e.
:math:`N`\ [.\ :math:`N`\ [.\ :math:`[N`\ .\ :math:`N`]]] where
:math:`N` is a decimal, octal or hexadecimal integer from 0 to 255. If
:math:`x < 4` values of :math:`N` are given, the last :math:`N` is taken
as the last :math:`5-x` bytes instead of 1 byte, with missing bytes
padded to the right. E.g. 192.258 is valid and equivalent to 192.1.2.0:
the last :math:`N` is 2 bytes in size, and covers 5 - 2 = 3 needed
bytes, including 1 zero pad to the right. Conversely, 192.168.4.1027 is
not valid: the last :math:`N` is too large.

An IPv4 address may optionally be followed by a netmask, either of the
form /\ :math:`B` or :\ :math:`IPv4`, where :math:`B` is a decimal,
octal or hexadecimal netmask integer from 0 to 32, and :math:`IPv4` is a
dotted-decimal IPv4 address of the same format described above. If an
:\ :math:`IPv4` netmask is given, only the largest contiguous set of
most-significant 1 bits are used (because netmasks are contiguous). If
no netmask is given, it will be calculated from standard IPv4 class
A/B/C/D/E rules, but will be large enough to include all given bytes of
the IP. E.g. 1.2.3.4 is Class A which has a netmask of 8, but the
netmask will be extended to 32 to include all 4 given bytes.

- ``inetabbrev(inet)``
  Returns a possibly shorter-than-canonical representation of
  ``$inet``, where trailing zero byte(s) of an IPv4 address may be
  omitted. All bytes of the network, and leading non-zero bytes of
  the host, will be included. E.g. returns 192.100.0/24. The
  /\ :math:`B` netmask is included, except if the network is host-only
  (i.e.netmask is the full size of the IP address). Empty string is
  returned on error.

- ``inetcanon(inet)``
  Returns canonical representation of ``$inet``. For IPv4, this is
  dotted-decimal with all 4 bytes. The /\ :math:`B` netmask is
  included, except if
  the network is host-only (i.e. netmask is the full size of the IP
  address). Empty string is returned on error.

- ``inetnetwork(inet)``
  Returns string IP address with the network bits of ``inet``, and
  the host bits set to 0. Empty string is returned on error.

- ``inethost(inet)``
  Returns string IP address with the host bits of ``inet``, and the
  network bits set to 0. Empty string is returned on error.

- ``inetbroadcast(inet)``
  Returns string IP broadcast address for ``inet``, i.e. with the
  network bits, and host bits set to 1. Empty string is returned on
  error.

- ``inetnetmask(inet)``
  Returns string IP netmask for ``inet``, i.e. with the network bits
  set to 1, and host bits set to 0. Empty string is returned on
  error.

- ``inetnetmasklen(inet)``
  Returns integer netmask length of ``inet``. -1 is returned on
  error.

- ``inetcontains(inetA, inetB)``
  Returns 1 if ``inetA`` contains ``inetB``, i.e. every address in
  ``inetB`` occurs within the ``inetA`` network. 0 is returned if
  not, or -1 on error.

- ``inetclass(inet)``
  Returns class of ``inet``, e.g. A, B, C, D, E or classless if a
  different netmask is used (or the address is IPv6). Empty string is
  returned on error.  Note that a bare host address with no explicit
  netmask (e.g. ``1.2.3.4``) has its netmask auto-extended to ``/32``
  and therefore returns ``classless``; to get the class of the network,
  include the classful netmask (e.g. ``1.2.3.4/8`` returns ``A``).

- ``inet2int(inet)``
  Returns integer representation of IP network/host bits of ``$inet``
  (i.e. without netmask); useful for compact storage of address as
  integer(s) instead of string. Returns -1 is returned on error (note
  that -1 may also be returned for an all-ones IP address, e.g.
  255.255.255.255).

- ``int2inet(i)``
  Returns ``inet`` string for 1- or 4-value varint ``$i`` taken as an
  IP address. Since no netmask can be stored in the integer form of
  an IP address, the returned IP string will not have a netmask.
  Empty string is returned on error.


urlcanonicalize
"""""""""""""""

Canonicalize a URL. Usage:

.. code-block:: sql

       urlcanonicalize(url[, flags])

Returns a copy of ``url``, canonicalized according to case-insensitive
comma-separated ``flags``, which are zero or more of:

- ``lowerProtocol``
  Lower-cases the protocol.

- ``lowerHost``
  Lower-cases the hostname.

- ``removeTrailingDot``
  Removes trailing dot(s) in hostname.

- ``reverseHost``
  Reverse the host/domains in the hostname. E.g.
  http://host.example.com/ becomes http://com.example.host/. This can
  be used to put the most-significant part of the hostname leftmost.

- ``removeStandardPort``
  Remove the port number if it is the standard port for the protocol.

- ``decodeSafeBytes``
  URL-decode safe bytes, where semantics are unlikely to change. E.g.
  “``%41``” becomes “``A``”, but “``%2F``” remains encoded, because
  it would decode to “``/``”.

- ``upperEncoded``
  Upper-case the hex characters of encoded bytes.

- ``lowerPath``
  Lower-case the (non-encoded) characters in the path. May be used
  for URLs known to point to case-insensitive filesystems, e.g.
  Windows.

- ``addTrailingSlash``
  Adds a trailing slash to the path, if no path is present.

Default flags are all but ``reverseHost``, ``lowerPath``. A flag may be
prefixed with the operator ``+`` to append the flag to existing flags;
``-`` to remove the flag from existing flags; or ``=`` (default) to
clear existing flags first and then set the flag. Operators remain in
effect for subsequent flags until the next operator (if any) is used.

Geographical coordinate functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The geographical coordinate functions allow for efficient processing of
latitude / longitude operations. They allow for the conversion of a
latitude/longitude pair into a single “geocode”, which is a single
``long`` value that contains both values. This can be used to easily
compare it to other geocodes (for distance calculations) or for finding
other geocodes that are within a certain distance.


azimuth2compass
"""""""""""""""

.. code-block:: sql

      azimuth2compass(double azimuth [, int resolution [, int verbosity]])

The ``azimuth2compass`` function converts a numerical azimuth value
(degrees of rotation from 0 degrees north) and converts it into a
compass heading, such as ``N`` or ``Southeast``. The exact text returned
is controlled by two optional parameters, ``resolution`` and
``verbosity``.

``Resolution`` determines how fine-grained the values returned are.
There are 4 possible values:

-  ``1`` - Only the four cardinal directions are used (N, E, S, W)

-  ``2`` *(default) - Inter-cardinal directions (N, NE, E, etc.)*

-  ``3`` - In-between inter-cardinal directions (N, NNE, NE, ENE, E,
   etc.)

-  ``4`` - “by” values (N, NbE, NNE, NEbN, NE, NEbE, ENE, EbN, E, etc.)

``Verbosity`` affects how verbose the resulting text is. There are two
possible values:

-  ``1`` *(default) - Use initials for direction values (N, NbE, NNE,
   etc.)*

-  ``2`` - Use full text for direction values (North, North by east,
   North-northeast, etc.)

For an azimuth value of ``105``, here are some example results of
``azimuth2compass``:

::

    azimuth2compass(105): E
    azimuth2compass(105, 3): ESE
    azimuth2compass(105, 4): EbS
    azimuth2compass(105, 1, 2): East
    azimuth2compass(105, 3, 2): East-southeast
    azimuth2compass(105, 4, 2): East by south


azimuthgeocode
""""""""""""""

.. code-block:: sql

      azimuthgeocode(geocode1, geocode2 [, method])

The ``azimuthgeocode`` function calculates the directional heading going
from one geocode to another. It returns a number between 0-360 where 0
is north, 90 east, etc., up to 360 being north again.

The third, optional ``method`` parameter can be used to specify which
mathematical method is used to calculate the direction. There are two
possible values:

-  ``greatcircle`` *(default)* - The “Great Circle” method is a highly
   accurate tool for calculating distances and directions on a sphere.
   It is used by default.

-  ``pythagorean`` - Calculations based on the Pythagorean method can
   also be used. They’re faster, but less accurate as the core formulas
   don’t take the curvature of the earth into consideration. Some
   internal adjustments are made, but the values are less accurate than
   the ``greatcircle`` method, especially over long distances and with
   paths that approach the poles.



azimuthlatlon
"""""""""""""

.. code-block:: sql

      azimuthlatlon(lat1, lon1, lat2, lon2, [, method])

The ``azimuthlatlon`` function calculates the directional heading going
from one latitude-longitude point to another. It operates identically to
`azimuthgeocode`_, except azimuthlatlon takes its parameters in a pair
of latitude-longitude points instead of geocode values.

The third, optional ``method`` parameter can be used to specify which
mathematical method is used to calculate the direction. There are two
possible values:

-  ``greatcircle`` *(default)* - The “Great Circle” method is a highly
   accurate tool for calculating distances and directions on a sphere.
   It is used by default.

-  ``pythagorean`` - Calculations based on the Pythagorean method can
   also be used. They’re faster, but less accurate as the core formulas
   don’t take the curvature of the earth into consideration. Some
   internal adjustments are made, but the values are less accurate than
   the ``greatcircle`` method, especially over long distances and with
   paths that approach the poles.

.. _dms-dec:


dms2dec, dec2dms
""""""""""""""""

.. code-block:: sql

      dms2dec(dms)
      dec2dms(dec)

The ``dms2dec`` and ``dec2dms`` functions are for changing back and
forth between the “degrees minutes seconds”
(DMS) format (west-positive) and “decimal degree” format for latitude
and longitude coordinates. All SQL geographical functions expect decimal
degree parameters.

DMS values are of the format :math:`DDDMMSS`. For example,
3515’ would be represented as 351500.

In decimal degrees, a degree is a whole digit, and minutes & seconds are
represented as fractions of a degree. Therefore, 3515’ would be 35.25 in
decimal degrees.

Note that the Texis DMS format has *west*-positive longitudes
(unlike ISO 6709 DMS format), and decimal degrees have *east*-positive
longitudes. It is up to the caller to flip the sign of longitudes where
needed.


distgeocode
"""""""""""

.. code-block:: sql

      distgeocode(geocode1, geocode2 [, method] )

The ``distgeocode`` function calculates the distance, in miles, between
two given geocodes. It uses the “Great Circle” method for calculation by
default, which is very accurate. A faster, but less accurate,
calculation can be done with the Pythagorean theorem. It is not designed
for distances on a sphere, however, and becomes somewhat inaccurate at
larger distances and on paths that approach the poles. To use the
Pythagorean theorem, pass a third string parameter, “``pythagorean``”,
to force that method. “``greatcircle``” can also be specified as a
method.

For example:

-  New York (JFK) to Cleveland (CLE), the Pythagorean method is off by
   .8 miles (.1%)

-  New York (JFK) to Los Angeles (LAX), the Pythagorean method is off by
   22.2 miles (.8%)

-  New York (JFK) to South Africa (PLZ), the Pythagorean method is off
   by 430 miles (5.2%)

See Also: `distlatlon`_


distlatlon
""""""""""

.. code-block:: sql

      distlatlon(lat1, lon1, lat2, lon2 [, method] )

The ``distlatlon`` function calculates the distance, in miles, between
two points, represented in latitude/longitude pairs in decimal degree
format.

Like `distgeocode`_, it uses the “Great Circle” method by default, but
can be overridden to use the faster, less accurate Pythagorean method if
“``pythagorean``” is passed as the optional ``method`` parameter.

For example:

-  New York (JFK) to Cleveland (CLE), the Pythagorean method is off by
   .8 miles (.1%)

-  New York (JFK) to Los Angeles (LAX), the Pythagorean method is off by
   22.2 miles (.8%)

-  New York (JFK) to South Africa (PLZ), the Pythagorean method is off
   by 430 miles (5.2%)

See Also: `distgeocode`_

.. _latlon2x:


latlon2geocode, latlon2geocodearea
""""""""""""""""""""""""""""""""""

.. code-block:: sql

      latlon2geocode(lat[, lon])
      latlon2geocodearea(lat[, lon], radius)

The ``latlon2geocode`` function encodes a given latitude/longitude
coordinate into one ``long`` return value. This encoded value – a
“geocode” value – can be indexed and used with a special variant of
Texis’ ``BETWEEN`` operator for bounded-area searches of a geographical
region.

The ``latlon2geocodearea`` function generates a bounding area centered
on the coordinate. It encodes a given latitude/longitude coordinate into
a *two-* value ``varlong``. The returned geocode value pair represents
the southwest and northeast corners of a square box centered on the
latitude/longitude coordinate, with sides of length two times ``radius``
(in decimal degrees). This bounding area can be used with the Texis
``BETWEEN`` operator for fast geographical searches.

The ``lat`` and ``lon`` parameters are ``double``\ s in the decimal
degrees format. (To pass :math:`DDDMMSS` “degrees minutes seconds” (DMS)
format values, convert them first with :ref:`dms2dec <dms-dec>` or
`parselatitude, parselongitude`_.). Negative numbers represent
south latitudes and west longitudes, i.e. these functions are
east-positive, and decimal format.

Valid values for latitude are -90 to 90 inclusive. Valid values for
longitude are -360 to 360 inclusive. A longitude value less than -180
will have 360 added to it, and a longitude value greater than 180 will
have 360 subtracted from it. This allows longitude values to continue to
increase or decrease when crossing the International Dateline, and thus
avoid a non-linear “step function”. Passing invalid ``lat`` or ``lon``
values to ``latlon2geocode`` will return -1.

The ``lon`` parameter is optional: both latitude and longitude (in that
order) may be given in a single space- or comma-separated text (``varchar``)
value for ``lat``. Also, a ``N``/``S`` suffix (for latitude) or ``E``/``W``
suffix (for longitude) may be given; ``S`` or ``W`` will negate the value.

The latitude and/or longitude may also have just about any of the formats
supported by `parselatitude, parselongitude`_, provided they are
disambiguated (e.g. separate parameters; or if one parameter, separated
by a comma and/or fully specified with degrees/minutes/seconds).

.. code-block:: sql

      -- Populate a table with latitude/longitude information:
      create table geotest(city varchar(64), lat double, lon double, geocode long);
      insert into geotest values('Cleveland, OH, USA', 41.4,  -81.5,  -1);
      insert into geotest values('San Francisco, CA, USA',   37.78, -122.42,  -1);
      insert into geotest values('Davis, Ca, USA',    38.55, -121.74, -1);
      insert into geotest values('New York, NY, USA',  40.81, -73.96,  -1);

      -- Prepare for geographic searches:
      update geotest set geocode = latlon2geocode(lat, lon);
      create index xgeotest_geocode on geotest(geocode);

      -- Search for cities within a 3-degree-radius "circle" (box)
      -- of Cleveland, nearest first:
      select city, lat, lon, distlatlon(41.4, -81.5, lat, lon) MilesAway from geotest
         where geocode between (select latlon2geocodearea(41.4, -81.5, 3.0)) order by 4 asc;


The geocode values returned by ``latlon2geocode`` and
``latlon2geocodearea`` are platform-dependent in format and accuracy,
and should not be copied across platforms. On platforms with 32-bit
``long``\ s a geocode value is accurate to about 32 seconds (around half
a mile, depending on latitude). ``-1`` is returned for invalid input values.

See Also: `geocode2lat, geocode2lon`_


geocode2lat, geocode2lon
""""""""""""""""""""""""

.. code-block:: sql

      geocode2lat(geocode)
      geocode2lon(geocode)

The ``geocode2lat`` and ``geocode2lon`` functions decode a geocode into
a latitude or longitude coordinate, respectively. The returned
coordinate is in the decimal degrees format. An invalid geocode value
(e.g. -1) will return NaN (Not a Number).  Note that when a NaN result
is retrieved from JavaScript (or serialized as JSON, e.g. with ``%J``),
it surfaces as ``null``; use ``isNaN()`` in SQL to test for it.

If you want :math:`DDDMMSS` “degrees minutes seconds” (DMS) format, you
can use :ref:`dec2dms <dms-dec>` to convert it.

.. code-block:: sql

      select city, geocode2lat(geocode), geocode2lon(geocode) from geotest;

As with :ref:`latlon2geocode <latlon2x>`, the ``geocode`` value is platform-dependent
in accuracy and format, so it should not be copied across platforms, and
the returned coordinates from ``geocode2lat`` and ``geocode2lon`` may
differ up to about half a minute from the original coordinates (due to
the finite resolution of a ``long``). An invalid geocode value (e.g. -1)
will return ``NaN`` (Not a Number).

See Also: :ref:`latlon2geocode <latlon2x>`


parselatitude, parselongitude
"""""""""""""""""""""""""""""

.. code-block:: sql

      parselatitude(latitudeText)
      parselongitude(longitudeText)

The ``parselatitude`` and ``parselongitude`` functions parse a text
(``varchar``) latitude or longitude coordinate, respectively, and return
its value in decimal degrees as a ``double``. The coordinate should be
in one of the following forms (optional parts in square brackets):

* [:math:`H`] :math:`nnn` [:math:`U`] [:] [:math:`H`] [:math:`nnn` 
  [:math:`U`] [:] [:math:`nnn` [:math:`U`]]] [:math:`H`]

* :math:`DDMM`\ [:math:`.MMM`...]

* :math:`DDMMSS`\ [:math:`.SSS`...]

where the terms are:

- :math:`nnn`
  A number (integer or decimal) with optional plus/minus sign. Only
  the first number may be negative, in which case it is a south
  latitude or west longitude. Note that this is true even for
  :math:`DDDMMSS` (DMS) longitudes – i.e. the ISO 6709 east-positive
  standard is followed, not the deprecated Texis west-positive
  standard.

- :math:`U`
  A unit (case-insensitive):

   -  ``d``

   -  ``deg``

   -  ``deg.``

   -  ``degrees``

   -  ``'`` (single quote) for minutes

   -  ``m``

   -  ``min``

   -  ``min.``

   -  ``minutes``

   -  ``"`` (double quote) for seconds

   -  ``s`` (iff ``d``/``m`` also used for degrees/minutes)

   -  ``sec``

   -  ``sec.``

   -  ``seconds``

   -  Unicode degree-sign (U+00B0), in ISO-8559-1 or UTF-8

   If no unit is given, the first number is assumed to be degrees, the
   second minutes, the third seconds. Note that “``s``” may only be used
   for seconds if “``d``” and/or “``m``” was also used for an earlier
   degrees/minutes value; this is to help disambiguate “seconds” vs.
   “southern hemisphere”.

- :math:`H`
  A hemisphere (case-insensitive):

   -  ``N``

   -  ``north``

   -  ``S``

   -  ``south``

   -  ``E``

   -  ``east``

   -  ``W``

   -  ``west``

   A longitude hemisphere may not be given for a latitude, and
   vice-versa.

- :math:`DD`
  A two- or three-digit degree value, with optional sign. Note that
  longitudes are east-positive ala ISO 6709, not west-positive like
  the deprecated Texis standard.

- :math:`MM`
  A two-digit minutes value, with leading zero if needed to make two
  digits.

- :math:`.MMM`...
  A zero or more digit fractional minute value.

- :math:`SS`
  A two-digit seconds value, with leading zero if needed to make two
  digits.

- :math:`.SSS`...
  A zero or more digit fractional seconds value.

Whitespace is generally not required between terms in the first format.
A hemisphere token may only occur once. Degrees/minutes/seconds numbers
need not be in that order, if units are given after each number. If a
5-integer-digit :math:`DDDMM`\ [:math:`.MMM`...] format is given and the
degree value is out of range (e.g. more than 90 degrees latitude), it is
interpreted as a :math:`DMMSS`\ [:math:`.SSS`...] value instead. To
force :math:`DDDMMSS`\ [:math:`.SSS`...] for small numbers, pad with
leading zeros to 6 or 7 digits.

.. code-block:: sql

    insert into geotest(lat, lon)
      values(parselatitude('54d 40m 10"'),
             parselongitude('W90 10.2'));

An invalid or unparseable latitude or longitude value will return
``NaN`` (Not a Number). Extra unparsed/unparsable text may be allowed
(and ignored) after the coordinate in most instances. Out-of-range
values (e.g. latitudes greater than 90 degrees) are accepted; it is up
to the caller to bounds-check the result.

JSON functions
~~~~~~~~~~~~~~

The JSON functions allow for the manipulation of ``varchar`` fields and
literals as JSON objects.


JSON Path Syntax
""""""""""""""""

The JSON Path syntax is standard Javascript object access, using ``$``
to represent the entire document. If the document is an object the path
must start ``$.``, and if an array ``$[``.


JSON Field Syntax
"""""""""""""""""

In addition to using the JSON functions it is possible to access
elements in a ``varchar`` field that holds JSON as if it was a field
itself. This allows for creation of indexes, searching and sorting
efficiently. Arrays can also be fetched as ``strlst`` to make use of
those features, e.g.
``SELECT Json.$.name FROM tablename WHERE 'SQL' IN Json.$.skills[*];``


isjson
""""""

.. code-block:: sql

      isjson(JsonDocument)

The ``isjson`` function returns 1 if the document is valid JSON, 0
otherwise.

.. code-block:: sql

    isjson('{ "type" : 1 }'): 1
    isjson('{}'): 1
    isjson('json this is not'): 0


json_format
"""""""""""

.. code-block:: sql

      json_format(JsonDocument, FormatOptions)

The ``json_format`` formats the ``JsonDocument`` according to
``FormatOptions``. Multiple options can be provided either space or
comma separated.

Valid ``FormatOptions`` are:

-  COMPACT - remove all unnecessary whitespace

-  INDENT(N) - print the JSON with each object or array member on a new
   line, indented by N spaces to show structure

-  SORT\_KEYS - sort the keys in the object. By default the order is
   preserved

-  EMBED - omit the enclosing ``{}`` or ``[]`` is using the snippet in
   another object

-  ENSURE\_ASCII - encode all Unicode characters outside the ASCII range

-  ENCODE\_ANY - if not a valid JSON document then encode into a JSON
   literal, e.g. to encode a string.

-  ESCAPE\_SLASH - escape forward slash ``/`` as ``\/``


json_type
"""""""""

.. code-block:: sql

      json_type(JsonDocument)

The ``json_type`` function returns the type of the JSON object or
element. Valid responses are:

-  OBJECT

-  ARRAY

-  STRING

-  INTEGER

-  DOUBLE

-  NULL

-  BOOLEAN

Assuming a field ``Json`` containing:

::

   {"items":
     [ 
      {"myNum":1, "myText": "Some text", "myBool": true},
      {"myNum":2.0, "myText": "Some more text", "myBool": false},
      null
     ]
   }

.. code-block:: sql

    json_type(Json): OBJECT
    json_type(Json.$.items[0]): OBJECT
    json_type(Json.$.items): ARRAY
    json_type(Json.$.items[0].myNum): INTEGER
    json_type(Json.$.items[1].myNum): DOUBLE
    json_type(Json.$.items[0].myText): STRING
    json_type(Json.$.items[0].myBool): BOOLEAN
    json_type(Json.$.items[2]): NULL


json_value
""""""""""

.. code-block:: sql

      json_value(JsonDocument, Path)

The ``json_value`` extracts the value identified by ``Path`` from
``JsonDocument``. ``Path`` is a varchar in the JSON Path Syntax. This
will return a scalar value. If ``Path`` refers to an array, object, or
invalid path no value is returned.

Assuming the same Json field from the previous examples:

::

    json_value(Json, '$'):
    json_value(Json, '$.items[0]'):
    json_value(Json, '$.items'):
    json_value(Json, '$.items[0].myNum'): 1
    json_value(Json, '$.items[1].myNum'): 2.0
    json_value(Json, '$.items[0].myText'): Some Text
    json_value(Json, '$.items[0].myBool'): true
    json_value(Json, '$.items[2]'):


json_query
""""""""""

.. code-block:: sql

      json_query(JsonDocument, Path)

The ``json_query`` extracts the object or array identified by ``Path``
from ``JsonDocument``. ``Path`` is a varchar in the JSON Path Syntax.
This will return either an object or an array value. If ``Path`` refers
to a scalar no value is returned.

Assuming the same Json field from the previous examples:

::

  json_query(Json, '$')
  ---------------------
  {"items":[{"myNum":1,"myText":"Some text","myBool":true},{"myNum":2.0,"myText":"Some more text","myBool":false},null]}

  json_query(Json, '$.items[0]')
  ------------------------------
  {"myNum":1,"myText":"Some text","myBool":true}

  json_query(Json, '$.items')``
  ---------------------------
  [{"myNum":1,"myText":"Some text","myBool":true},{"myNum":2.0,"myText":"Some more text","myBool":false},null]

The following will return an empty string as they refer to scalars or
non-existent keys.

::

    json_query(Json, '$.items[0].myNum')
    json_query(Json, '$.items[1].myNum')
    json_query(Json, '$.items[0].myText')
    json_query(Json, '$.items[0].myBool')
    json_query(Json, '$.items[2]')


json_modify
"""""""""""

.. code-block:: sql

      json_modify(JsonDocument, Path, NewValue)

The ``json_modify`` function returns a modified version of JsonDocument
with the key at Path replaced by NewValue.

If ``Path`` starts with append then the NewValue is appended to the
array referenced by Path. It is an error it Path refers to anything
other than an array.

::

    json_modify('{}', '$.foo', 'Some "quote"')
    ------------------------------------------
    {"foo":"Some \"quote\""}

    json_modify('{ "foo" : { "bar": [40, 42] } }', 'append $.foo.bar', 99)
    ----------------------------------------------------------------------
    {"foo":{"bar":[40,42,99]}}

    json_modify('{ "foo" : { "bar": [40, 42] } }', '$.foo.bar', 99)
    ---------------------------------------------------------------
    {"foo":{"bar":99}}


json_merge_patch
""""""""""""""""

.. code-block:: sql

      json_merge_patch(JsonDocument, Patch)

The ``json_merge_patch`` function provides a way to patch a target JSON
document with another JSON document. The patch function conforms to
:rfc:`7386`\ .


Keys in ``JsonDocument`` are replaced if found in ``Patch``. If the
value in ``Patch`` is ``null`` then the key will be removed in the
target document.

.. code-block:: sql

    json_merge_patch('{"a":"b"}', '{"a":"c"}')
    ------------------------------------------
    {"a":"c"}

    json_merge_patch('{"a": [{"b":"c"}]}', '{"a": [1]}')
    ----------------------------------------------------
    {"a":[1]}

    json_merge_patch('[1,2]', '{"a":"b", "c":null}')
    ------------------------------------------------
    {"a":"b"}


json_merge_preserve
"""""""""""""""""""

.. code-block:: sql

      json_merge_preserve(JsonDocument, Patch)

The ``json_merge_preserve`` function provides a way to patch a target
JSON document with another JSON document while preserving the content
that exists in the target document.

Keys in ``JsonDocument`` are merged if found in ``Patch``. If the same
key exists in both the target and patch file the result will be an array
with the values from both target and patch.

If the value in ``Patch`` is ``null`` then the key will be removed in
the target document.

::

    json_merge_preserve('{"a":"b"}', '{"a":"c"}')
    ---------------------------------------------
    {"a":["b","c"]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": [1]}')
    -------------------------------------------------------
    {"a":[{"b":"c"},1]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": 1}')
    -----------------------------------------------------
    {"a":[{"b":"c"},1]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": [1,2]}')
    ---------------------------------------------------------
    {"a":[{"b":"c"},1,2]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": {"d":1,"e":2} }')
    ------------------------------------------------------------------
    {"a":[{"b":"c"},{"d":1,"e":2}]}

    json_merge_preserve('{"a": {"b":"c"}}', '{"a": {"d":1, "e":2} }')
    -----------------------------------------------------------------
    {"a":{"b":"c","d":1,"e":2}}

    json_merge_preserve('[1,2]', '{"a":"b", "c":null}')
    ---------------------------------------------------
    [1,2,{"a":"b","c":null}]

Full Example Using Json
"""""""""""""""""""""""

JSON fields can be operated on with database functions and SQL statements in
the same manner as normal fields.  Here is the 
:ref:`sql.exec() example <rampart-sql:Exec Full Example>` using a JSON
varchar field in the place of multiple columns:

.. code-block:: javascript

    var Sql = require("rampart-sql");

    /* create database if it does not exist */
    var sql = new Sql.connection("./mytestdb",true);

    /* check if table exists */
    var res = sql.exec(
        "select * from SYSTABLES where NAME='employees'",
        {"returnType":"novars"} /* we only need the count */
    );

    if(res.rowCount) /* 1 if the table exists */
    {
        /* drop table from previous test run of this script */
        res=sql.exec("drop table employees");
    }

    /* (re)create the table */
    sql.exec(
            "create table employees (Classification varchar(8), " +
            "Name varchar(16), EmpData varchar(256) );",
            {"returnType":"novars"}
    );

    /* populate variables for insertion */
    var emp1 = {
      cl:     "principal",
      name:   "Debbie Dreamer",
      empdata: {
        age:    63,
        title:  "Chief Executive Officer",
        start:  '1999-12-31',
        salary: 250000,
        bio:    "Born and raised in Manhattan, New York. U.C. Berkeley graduate. " +
                  "Loves to skydive. Built Company from scratch. Still uses word-perfect."
      }
    }

    var emp2 = {
      cl:     "principal",
      name:   "Rusty Grump",
      empdata: {
        age:    58,
        title:  "Chief Financial Officer",
        start:  '1999-12-31', // Strings are converted to local time
        salary: 250000,
        bio:    "Born in Switzerland, raised in South Dakota. Columbia graduate. " +
                  "Financed operation with inheritance. Has no sense of humor."
      }
    }

    var emp3 = {
      cl:     "salary",
      name:   "Georgia Geek",
      empdata: {
        age:    44,
        title:  "Lead Programmer",
        start:  '2001-3-15',
        salary: 100000,
        bio:    "Stanford graduate. Enjoys pizza and beer. Proficient in Perl, COBOL," +
                "FORTRAN and IBM System/360"
      }
    }

    var emp4 = {
      cl:     "salary",
      name:   "Sydney Slacker",
      empdata: {
        age:    44,
        title:  "Programmer",
        start:  new Date('2002-5-12T00:00:00.0-0800'), // Dates are UTC unless offset is given.
        salary: 100000,
        bio:    "DeVry University graduate. Enjoys a good nap. Proficient in Python, " +
                "Perl and JavaScript"
      }
    }

    var emp5 = {
      cl:     "hourly",
      name:   "Pat Particular",
      empdata: {
        age:    32,
        title:  "Systems Administrator",
        start:  new Date('2003-7-14'),
        salary: 80000,
        bio:    "Lincoln High School graduate. Self taught Linux and windows administration skills. Proficient in " +
              "Bash and GNU utilities. Capable of crashing or resurrecting machines with a single ping.",
      }
    }

    var emp6 = {
      cl:     "intern",
      name:   "Billie Barista",
      empdata: {
        age:    22,
        title:  "Intern",
        start:  new Date('2020-3-18'),
        salary: 0,
        bio:    "Harvard graduate, full ride scholarship, top of class.  Proficient in C, C++, " +
                "Rust, Haskell, Node, Python. Into skydiving. Makes a mean latte."
      }
    }

    var employees = [ emp1, emp2, emp3, emp4, emp5, emp6 ];

    /* insert rows */
    for (var i=0; i<employees.length; i++)
    {
        // empdata:{} is automatically converted to JSON
        sql.exec(
            "insert into employees values(?cl,?name,?empdata)",
            employees[i]
        );
    }



    /* create text index */
    sql.exec("create fulltext index employees_Bio_text on employees( EmpData.$.bio );");

    /* perform some queries */
    res=sql.exec("select Name, EmpData.$.age Age from employees");

    rampart.utils.printf('%3J\n%s\n', res,sql.errMsg);
    /* expected output:
       {
           "columns": [
               "Name",
               "Age"
           ],
           "rows": [
               {
                   "Name": "Debbie Dreamer",
                   "Age": 63
               },
               {
                   "Name": "Rusty Grump",
                   "Age": 58
               },
               {
                   "Name": "Georgia Geek",
                   "Age": 44
               },
               {
                   "Name": "Sydney Slacker",
                   "Age": 44
               },
               {
                   "Name": "Pat Particular",
                   "Age": 32
               },
               {
                   "Name": "Billie Barista",
                   "Age": 22
               }
           ],
           "rowCount": 6
       }
    */

    res=sql.exec(
        "select Name, EmpData.$.age  Age from employees",
        {returnType:'array', maxRows:2, includeCounts:true}
    );
    rampart.utils.printf('%3J\n', res);
    /* expected output:
       {
           "columns": [
               "Name",
               "Age"
           ],
           "rows": [
               [
                   "Debbie Dreamer",
                   63
               ],
               [
                   "Rusty Grump",
                   58
               ]
           ],
           "countInfo": {
               "indexCount": -1,
               "rowsMatchedMin": -1,
               "rowsMatchedMax": -2,
               "rowsReturnedMin": -1,
               "rowsReturnedMax": -2
           },
           "rowCount": 2
       }
                 Note that countInfo values are all negative since no
                 text search was performed.
    */
    res=sql.exec(
        "select Name from employees where EmpData.$.bio likep 'proficient' and convert(EmpData.$.salary, 'float') > 50000",
         {includeCounts:true}
    );
    rampart.utils.printf('%3J\n', res);

    /* expected output:
       {
           "columns": [
               "Name"
           ],
           "rows": [
               {
                   "Name": "Georgia Geek"
               },
               {
                   "Name": "Sydney Slacker"
               },
               {
                   "Name": "Pat Particular"
               }
           ],
           "countInfo": {
               "indexCount": 4,
               "rowsMatchedMin": 0,
               "rowsMatchedMax": 4,
               "rowsReturnedMin": 0,
               "rowsReturnedMax": 4
           },
           "rowCount": 3
       }
       Note that indexCount is the count before "Salary > 50000" filter
    */

    /* skydive => skydiving */
    sql.set({
        minwordlen: 5,
        suffixproc: true
    });

    nrows=sql.exec(
        "select Name, EmpData.$.salary Salary from employees where EmpData.$.bio likep 'skydive' " +
          "order by convert(EmpData.$.salary, 'float')  desc",
        {returnType:"array", includeCounts:true},
        function (row, i, coln, cinfo) {
            if(!i) {
                console.log(
                   "Total approximate number of matches in db: " +
                   cinfo.indexCount
                );
                console.log("-", coln);
            }
            console.log(i+1,row);
        }
    );
    console.log("Total: " + nrows); // 2

    /* expected output:
       Total approximate number of matches in db: 2
       - ["Name","Salary"]
       1 ["Debbie Dreamer",250000]
       2 ["Billie Barista",0]
       Total: 2
    */

Note that it is more efficient and less cumbersome to place values in
dedicated columns.  However, when the table may need to accomodate future
fields, or where fields vary per row, using JSON fields can allow for
greater flexibility.

Note also that within SQL, JSON virtual field values (and any index made
on one) are treated as text or a :green:`String`.  A bare comparison or
``ORDER BY`` on a numeric JSON field compares lexicographically, so
numbers will not sort properly or be selectable based on range.  To
compare or sort numerically, wrap the field in
:ref:`convert <sql-server-funcs:convert>` (e.g.
``where convert(EmpData.$.salary, 'float') > 50000``), as shown in the
example above.  Values returned to JavaScript from a bare ``select`` of
a JSON field keep their JSON types (numbers arrive as :green:`Numbers`,
booleans as :green:`Booleans`, etc.).
