File functions
--------------

fromfile, fromfiletext
~~~~~~~~~~~~~~~~~~~~~~

The ``fromfile`` and ``fromfiletext`` functions read a file. The syntax
is

::

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

::

         SELECT  FILENAME, fromfiletext(FILENAME)
         FROM    DOCUMENTS
         WHERE   DOCID = 'JT09113' ;

The results are:

::

      FILENAME            fromfiletext(FILENAME)
      /docs/JT09113.txt   This is the text contained in the document
      that has an id of JT09113.

totext
~~~~~~

Converts data or file to text. The syntax is

::

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

::

         SELECT  FILENAME, totext(FILENAME)
         FROM    DOCUMENTS
         WHERE   DOCID = 'JT09113' ;

The results are:

::

      FILENAME            totext(FILENAME)
      /docs/JT09113.pdf   This is the text contained in the document
      that has an id of JT09113.

toind
~~~~~

Create a Texis managed indirect file. The syntax is

::

       toind(data)

This function takes the argument, stores it into a file, and returns the
filename as an ``indirect`` type. This is most often used in combination
with ``fromfile`` to create a Texis managed file. For example:

::

         INSERT  INTO DOCUMENTS
         VALUES('JT09114', toind(fromfile('srcfile')))

The database will now contain a pointer to a copy of ``srcfile``, which
will remain searchable even if the original is changed or removed. An
important point to note is that any changes to ``srcfile`` will not be
reflected in the database, unless the table row’s ``indirect`` column is
modified (even to the save value, this just tells Texis to re-index it).

canonpath
~~~~~~~~~

Returns canonical version of a file path, i.e. fully-qualified and
without symbolic links:

::

      canonpath(path[, flags])

The optional ``flags`` is a set of bit flags: bit 0 set if error
messages should be issued, bit 1 set if the return value should be empty
instead of ``path`` on error.

pathcmp
~~~~~~~

File path comparison function; like C function ``strcmp()`` but for
paths:

::

      pathcmp(pathA, pathB)

Returns an integer indicating the sort order of ``pathA`` relative to
``pathB``: 0 if ``pathA`` is the same as ``pathB``, less than 0 if
``pathA`` is less than ``pathB``, greater than 0 if ``pathA`` is greater
than ``pathB``. Paths are compared case-insensitively if and only if the
OS is case-insensitive for paths, and OS-specific alternate directory
separators are considered the same (e.g. “``\``” and “``/``” in
Windows). Multiple consecutive directory separators are considered the
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
~~~~~~~~

Returns the base filename of a given file path.

::

      basename(path)

The basename is the contents of ``path`` after the last path separator.
No filesystem checks are performed, as this is a text/parsing function;
thus “``.``” and “``..``” are not significant.

dirname
~~~~~~~

Returns the directory part of a given file path.

::

      dirname(path)

The directory is the contents of ``path`` before the last path separator
(unless it is significant – e.g. for the root directory – in which case
it is retained). No filesystem checks are performed, as this is a text/parsing function;
thus “``.``” and “``..``” are not significant.

fileext
~~~~~~~

Returns the file extension of a given file path.

::

      fileext(path)

The file extension starts with and includes a dot. The file extension is
only considered present in the basename of the path, i.e. after the last
path separator.

joinpath
~~~~~~~~

Joins one or more file/directory path arguments into a merged path,
inserting/removing a path separator between arguments as needed. Takes
one to 5 path component arguments. E.g.:

::

      joinpath('one', 'two/', '/three/four', 'five')

yields

::

      one/two/three/four/five


Redundant path separators internal to an argument are not removed, 
nor are “.” and “ ..” path components removed.

joinpathabsolute
~~~~~~~~~~~~~~~~

Like ``joinpath``, except that a second or later argument that is an
absolute path will overwrite the previously-merged path. E.g.:

::

      joinpathabsolute('one', 'two', '/three/four', 'five')

yields

::

      /three/four/five

Under Windows, partially absolute path arguments – e.g. “ /dir”
where the drive or dir is still relative – are considered
absolute for the sake of overwriting the merge.

Redundant path separators
internal to an argument are not removed, nor are “.” and “..” path
components removed.
