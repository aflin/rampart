The rampart-treesitter module
=============================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-treesitter module is built on the
`Tree-sitter <https://tree-sitter.github.io/tree-sitter/>`__ parser
generator and incremental parsing library, originally created by
Max Brunsfeld.  The authors of Rampart extend our thanks to the
Tree-sitter authors and contributors and to the individual
maintainers of each bundled grammar.

The module bundles the following language grammars, sourced from
the official `tree-sitter <https://github.com/tree-sitter>`__
organization where available and well-maintained community
repositories otherwise:

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Language
     - Repository
     - Notes
   * - JavaScript
     - tree-sitter/tree-sitter-javascript
     - Also linked into the rampart binary for the transpiler
   * - C
     - tree-sitter/tree-sitter-c
     -
   * - C++
     - tree-sitter/tree-sitter-cpp
     -
   * - Python
     - tree-sitter/tree-sitter-python
     -
   * - Java
     - tree-sitter/tree-sitter-java
     -
   * - Go
     - tree-sitter/tree-sitter-go
     -
   * - Rust
     - tree-sitter/tree-sitter-rust
     -
   * - TypeScript / TSX
     - tree-sitter/tree-sitter-typescript
     - Two grammars in a single repository
   * - C#
     - tree-sitter/tree-sitter-c-sharp
     -
   * - Ruby
     - tree-sitter/tree-sitter-ruby
     -
   * - Bash
     - tree-sitter/tree-sitter-bash
     -
   * - Kotlin
     - fwcd/tree-sitter-kotlin
     - Community-maintained
   * - PHP
     - tree-sitter/tree-sitter-php
     - The ``php_only`` variant for pure-PHP files
   * - Swift
     - alex-pinkus/tree-sitter-swift
     - Community-maintained
   * - Lua
     - tree-sitter-grammars/tree-sitter-lua
     -
   * - Dart
     - UserNobody14/tree-sitter-dart
     - Community-maintained
   * - Scala
     - tree-sitter/tree-sitter-scala
     -
   * - Haskell
     - tree-sitter/tree-sitter-haskell
     -
   * - OCaml
     - tree-sitter/tree-sitter-ocaml
     - The ``ocaml`` (``.ml``) implementation grammar
   * - CSS
     - tree-sitter/tree-sitter-css
     -
   * - YAML
     - ikatyang/tree-sitter-yaml
     - Parse-only (no ``extractSymbols`` support)
   * - TOML
     - tree-sitter-grammars/tree-sitter-toml
     - Parse-only
   * - Markdown
     - tree-sitter-grammars/tree-sitter-markdown
     - Parse-only (block + inline sub-grammars)
   * - Elixir
     - elixir-lang/tree-sitter-elixir
     - Parse-only

License
~~~~~~~

The Tree-sitter library is licensed under the
`MIT License <https://github.com/tree-sitter/tree-sitter/blob/master/LICENSE>`_\ .
Individual grammars are licensed under MIT or comparable permissive
licenses by their respective authors.  See the ``LICENSE`` files
under each ``extern/tree-sitter/tree-sitter-<lang>/`` directory in
the Rampart source tree.

The rampart-treesitter module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-treesitter module exposes language-aware source code
parsing to Rampart JavaScript.  It supports twenty-six languages
out of the box and provides two access patterns:

*  A high-level ``extractSymbols`` function that returns a flat list
   of top-level definitions (functions, classes, structs, type
   declarations, and similar constructs) for a given source file.
   This is the primary tool for building searchable code indexes,
   navigation tooling, and language-aware recall systems.

*  A lower-level ``parse`` function that returns the entire abstract
   syntax tree as a nested JavaScript object.  Callers may walk this
   tree to perform custom static analysis, lint checks, refactoring
   transforms, or any work that does not fit the flat
   symbol-extraction model.

How does it work?
~~~~~~~~~~~~~~~~~

Tree-sitter is a parser-generator framework that produces a concrete
syntax tree from source code.  Unlike line-oriented or
regex-based parsers, Tree-sitter produces a structured tree that
preserves the grammatical relationships in the source.  The library
is fault-tolerant: it returns a usable tree even when the source
contains syntax errors, marking the affected regions with ``ERROR``
nodes rather than aborting.

Each supported language is implemented as a compiled grammar that
Tree-sitter links against its runtime.  The grammars are bundled
inside ``rampart-treesitter.so`` itself, so users of the module need
no external installation.

The ``extractSymbols`` function dispatches to a per-language table
of "definition node types" (for example ``function_declaration``,
``class_definition``, ``struct_specifier``) and walks the parsed
tree, emitting one record for every match.

The ``parse`` function performs no such filtering and returns the
full tree as a nested object.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()``
function:

.. code-block:: javascript

    var ts = require("rampart-treesitter");


Properties
----------

languages
~~~~~~~~~

    A read-only :green:`Array` of :green:`String` values listing every
    bundled grammar by name.  Any name in this array may be passed as
    the ``language`` argument to ``parse``.  The subset for which
    ``extractSymbols`` is supported is documented per grammar in
    the Acknowledgment table above; passing a parse-only name (such
    as ``"yaml"``) to ``extractSymbols`` will throw.

    Example:

    .. code-block:: javascript

        var ts = require("rampart-treesitter");
        console.log(ts.languages);
        /* ["javascript", "c", "cpp", "python", "java", "go", "rust",
            "typescript", "tsx", "csharp", "ruby", "bash", "kotlin",
            "php", "swift", "lua", "dart", "scala", "haskell", "ocaml",
            "css", "yaml", "toml", "markdown", "markdown_inline",
            "elixir"] */


Functions
---------

extractSymbols
~~~~~~~~~~~~~~

    The ``extractSymbols`` function parses a source string with the
    named grammar and returns a flat list of top-level definitions
    (functions, classes, methods, struct declarations, type aliases,
    and other language-specific constructs).

    Usage:

    .. code-block:: javascript

        var ts = require("rampart-treesitter");

        var result = ts.extractSymbols(source, language[, options]);

    Where:

    *  ``source`` is a :green:`String`, the source code to be parsed.

    *  ``language`` is a :green:`String`, one of the supported
       language names returned in ``ts.languages``.  Passing a
       grammar that is bundled for ``parse`` only (``"yaml"``,
       ``"toml"``, ``"markdown"``, ``"markdown_inline"``,
       ``"elixir"``) will throw a descriptive error.

    *  ``options`` is an :green:`Object` with the following optional
       property:

       *  ``strict`` - A :green:`Boolean`.  When ``true``, throws an
          error if the tree contains any ``ERROR`` node anywhere.
          Default ``false``: the function returns whatever symbols
          could be extracted from the partial tree, and the caller
          may inspect the returned ``hasErrors`` flag to know
          whether the parse was clean.

    Return Value:
        An :green:`Object` with two properties:

        *  ``symbols`` - An :green:`Array` of :green:`Object` values,
           one per extracted definition.  Each entry has the
           following shape:

           *  ``name`` - A :green:`String`, the identifier of the
              symbol.  Falls back to ``"(anonymous)"`` when no name
              can be resolved (for example, an anonymous struct in
              a C ``typedef``).

           *  ``kind`` - A :green:`String`, the Tree-sitter node
              type that produced the match (for example,
              ``"function_definition"``, ``"class_declaration"``,
              ``"trait_item"``).

           *  ``line`` - A :green:`Number`, the 1-based line on
              which the symbol begins.

           *  ``column`` - A :green:`Number`, the 1-based column.

           *  ``signature`` - A :green:`String`, the source text
              covering the symbol's node.  Nodes of 256 bytes or fewer
              are returned in full; longer nodes are cut at the last
              ``{`` before the 256-byte limit (or hard-cut at the limit
              if there is none) and suffixed with ``...``.
              Suitable for display and for indexing.

           *  ``startByte`` - A :green:`Number`, the byte offset
              into ``source`` where the symbol begins.

           *  ``endByte`` - A :green:`Number`, the byte offset
              where the symbol ends.

        *  ``hasErrors`` - A :green:`Boolean`, ``true`` if the
           parsed tree contains any ``ERROR`` node.  When ``true``,
           the symbol list may be incomplete; tree-sitter's
           error-recovery may have skipped sections of the source.

        *  ``extract`` - A :green:`Function`.  A convenience method
           that returns the source text of a symbol without the
           caller re-slicing ``source`` by hand.  It is called as
           ``result.extract(which)`` where ``which`` is either:

           *  A :green:`Number`, an index into the ``symbols`` array;
              or
           *  A :green:`String`, a symbol name.  The **first** symbol
              with a matching ``name`` is used.

           It returns a :green:`String`, the exact bytes
           ``[startByte, endByte)`` of the chosen symbol - equivalent
           to
           ``rampart.utils.readFile(file, symbol.startByte, symbol.endByte - symbol.startByte, true)``
           but sliced directly from the already-parsed source, so it
           does not re-read the file and is immune to source/offset
           drift.  It throws if the index is out of range or no symbol
           has the given name.

           The retained source is held internally by reference (a
           refcounted, interned string - not a copy), and is not an
           enumerable property of the result.

parse
~~~~~

    The ``parse`` function returns the entire abstract syntax tree
    for the given source as a nested JavaScript object.  Use this
    for custom analyses that fall outside the flat symbol-extraction
    model.

    Usage:

    .. code-block:: javascript

        var ts = require("rampart-treesitter");

        var tree = ts.parse(source, language[, options]);

    Where:

    *  ``source`` is a :green:`String`, the source code to be parsed.

    *  ``language`` is a :green:`String`, one of the supported
       language names returned in ``ts.languages``.  Unlike
       ``extractSymbols``, ``parse`` accepts every bundled grammar,
       including the parse-only ones.

    *  ``options`` is an :green:`Object` with the following optional
       properties:

       *  ``strict`` - A :green:`Boolean`.  When ``true``, throws an
          error if the tree contains any ``ERROR`` node.  Default
          ``false``.

       *  ``includeText`` - A :green:`Boolean`.  When ``true``,
          attaches the source slice spanned by each node as a
          ``text`` property.  Default ``false``: callers may
          reproduce the slice using ``source.substring(node.startByte,
          node.endByte)``.  Omitted by default to keep the result
          compact for large files.

       *  ``includeUnnamed`` - A :green:`Boolean`.  When ``true``,
          includes anonymous nodes (keywords, punctuation, operators)
          as children.  Default ``false``: only named (semantic)
          nodes are returned, which is what most callers want.

    Return Value:
        An :green:`Object` representing the root node, recursively
        containing its children.  Each node has the following shape:

        *  ``type`` - A :green:`String`, the Tree-sitter node type
           (for example, ``"program"``, ``"function_definition"``,
           ``"identifier"``).

        *  ``line`` - A :green:`Number`, the 1-based starting line.

        *  ``column`` - A :green:`Number`, the 1-based starting
           column.

        *  ``startByte`` - A :green:`Number`, the byte offset where
           the node begins.

        *  ``endByte`` - A :green:`Number`, the byte offset where
           the node ends.

        *  ``isError`` - A :green:`Boolean`, ``true`` if this
           specific node is an ``ERROR`` node.

        *  ``hasError`` - A :green:`Boolean`, ``true`` if the
           subtree rooted at this node contains any ``ERROR`` node.

        *  ``children`` - An :green:`Array` of node :green:`Object`
           values, the named children of this node (or all children
           if ``includeUnnamed`` is set).

        *  ``text`` - A :green:`String`, the full source slice
           covering this node.  Present only when ``includeText``
           is ``true``.

Examples
--------

Extracting Symbols
~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    var ts = require("rampart-treesitter");

    var src =
        "function add(a, b) {\n" +
        "    return a + b;\n" +
        "}\n" +
        "class Greeter {\n" +
        "    greet(name) { return \"hi \" + name; }\n" +
        "}\n";

    var result = ts.extractSymbols(src, "javascript");

    console.log("hasErrors:", result.hasErrors);
    result.symbols.forEach(function(s) {
        console.log(s.kind, "'" + s.name + "' at line", s.line);
    });

    /* expected output:
    hasErrors: false
    function_declaration 'add' at line 1
    class_declaration 'Greeter' at line 4
    method_definition 'greet' at line 5
    */

Extracting a Symbol's Source
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``extract`` method on the result returns the full source text of a
symbol, selected by index or by name.  This is convenient for pulling a
function or class body straight out of a large file after locating it:

.. code-block:: javascript

    var ts = require("rampart-treesitter");
    rampart.globalize(rampart.utils);

    var src = readFile("/path/to/module.js", true);
    var result = ts.extractSymbols(src, "javascript");

    /* by index */
    console.log(result.extract(0));

    /* by name - returns the first symbol so named */
    console.log(result.extract("greet"));

    /* equivalent to slicing the source yourself: */
    var s = result.symbols[0];
    var same = result.extract(0) ===
               src.substring(s.startByte, s.endByte);   /* true */

Inspecting the AST
~~~~~~~~~~~~~~~~~~

.. code-block:: javascript

    var ts = require("rampart-treesitter");

    var tree = ts.parse("int add(int a, int b) { return a + b; }", "c");

    console.log("root type:", tree.type);
    console.log("root spans bytes", tree.startByte, "to", tree.endByte);

    /* Walk the tree and print every named node */
    function walk(node, depth) {
        var pad = "";
        for (var i = 0; i < depth; i++) pad += "  ";
        console.log(pad + node.type +
                    " @ " + node.line + ":" + node.column);
        node.children.forEach(function(c) { walk(c, depth + 1); });
    }
    walk(tree, 0);

Handling Parse Errors
~~~~~~~~~~~~~~~~~~~~~

By default, both functions accept broken input and surface what
they could parse, setting ``hasErrors`` (or ``hasError`` on
``parse``'s root) so the caller can detect the partial result:

.. code-block:: javascript

    var ts = require("rampart-treesitter");

    var src = "function foo() { /* missing close brace */";
    var result = ts.extractSymbols(src, "javascript");

    if (result.hasErrors) {
        console.log("warning: source has parse errors; results may be incomplete.");
    }
    console.log("Got", result.symbols.length, "symbols.");

Use ``strict: true`` when the caller needs to reject malformed
input rather than continue with a partial parse:

.. code-block:: javascript

    var ts = require("rampart-treesitter");

    try {
        ts.extractSymbols("function foo() { /* broken */", "javascript",
                          { strict: true });
    } catch (e) {
        console.log("caught:", e.message);
    }

Parsing a Configuration File
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The parse-only grammars (``yaml``, ``toml``, ``markdown``,
``markdown_inline``, ``elixir``) are not exposed through
``extractSymbols`` but are fully usable through ``parse``.  This
example extracts the top-level keys of a YAML document:

.. code-block:: javascript

    var ts = require("rampart-treesitter");

    var doc =
        "name: example\n" +
        "version: 1.0\n" +
        "tags:\n" +
        "  - foo\n" +
        "  - bar\n";

    var tree = ts.parse(doc, "yaml", { includeText: true });
    /* tree.type === "stream" ; walk into block_node → block_mapping
       to find the document's top-level keys */
