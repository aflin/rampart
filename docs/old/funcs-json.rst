JSON functions
--------------

The JSON functions allow for the manipulation of ``varchar`` fields and
literals as JSON objects.

JSON Path Syntax
~~~~~~~~~~~~~~~~

The JSON Path syntax is standard Javascript object access, using ``$``
to represent the entire document. If the document is an object the path
must start ``$.``, and if an array ``$[``.

JSON Field Syntax
~~~~~~~~~~~~~~~~~

In addition to using the JSON functions it is possible to access
elements in a ``varchar`` field that holds JSON as if it was a field
itself. This allows for creation of indexes, searching and sorting
efficiently. Arrays can also be fetched as ``strlst`` to make use of
those features, e.g.
``SELECT Json.$.name FROM tablename WHERE 'SQL' IN Json.$.skills[*];``

isjson
~~~~~~

::

      isjson(JsonDocument)

The ``isjson`` function returns 1 if the document is valid JSON, 0
otherwise.

::

    isjson('{ "type" : 1 }'): 1
    isjson('{}'): 1
    isjson('json this is not'): 0

json\_format
~~~~~~~~~~~~

::

      json_format(JsonDocument, FormatOptions)

The ``json_format`` formats the ``JsonDocument`` according to
``FormatOptions``. Multiple options can be provided either space or
comma separated.

Valid ``FormatOptions`` are:

-  COMPACT - remove all unnecessary whitespace

-  INDENT(N) - print the JSON with each object or array member on a new
   line, indented by N spaces to show structure

-  SORT-KEYS - sort the keys in the object. By default the order is
   preserved

-  EMBED - omit the enclosing ``{}`` or ``[]`` is using the snippet in
   another object

-  ENSURE\_ASCII - encode all Unicode characters outside the ASCII range

-  ENCODE\_ANY - if not a valid JSON document then encode into a JSON
   literal, e.g. to encode a string.

-  ESCAPE\_SLASH - escape forward slash ``/`` as ``\/``

json\_type
~~~~~~~~~~

::

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

Assuming a field ``Json`` containing: “items” : [ “Num” : 1, “Text” :
“The Name”, “First” : true , “Num” : 2.0, “Text” : “The second one”,
“First” : false , null ]

::

    json_type(Json): OBJECT
    json_type(Json.$.items[0]): OBJECT
    json_type(Json.$.items): ARRAY
    json_type(Json.$.items[0].Num): INTEGER
    json_type(Json.$.items[1].Num): DOUBLE
    json_type(Json.$.items[0].Text): STRING
    json_type(Json.$.items[0].First): BOOLEAN
    json_type(Json.$.items[2]): NULL

json\_value
~~~~~~~~~~~

::

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
    json_value(Json, '$.items[0].Num'): 1
    json_value(Json, '$.items[1].Num'): 2.0
    json_value(Json, '$.items[0].Text'): The Name
    json_value(Json, '$.items[0].First'): true
    json_value(Json, '$.items[2]'):

json\_query
~~~~~~~~~~~

::

      json_query(JsonDocument, Path)

The ``json_query`` extracts the object or array identified by ``Path``
from ``JsonDocument``. ``Path`` is a varchar in the JSON Path Syntax.
This will return either an object or an array value. If ``Path`` refers
to a scalar no value is returned.

Assuming the same Json field from the previous examples:

| ``json_query(Json, '$')``
| ``---------------------``
| ``{"items":[{"Num":1,"Text":"The Name","First":true},``\ ``{"Num":2.0,"Text":"The second one","First":false},null]}``

| ``json_query(Json, '$.items[0]')``
| ``------------------------------``
| ``{"Num":1,"Text":"The Name","First":true}``

| ``json_query(Json, '$.items')``
| ``---------------------------``
| ``[{"Num":1,"Text":"The Name","First":true},``\ ``{"Num":2.0,"Text":"The second one","First":false},null]``

The following will return an empty string as they refer to scalars or
non-existent keys.

::

    json_query(Json, '$.items[0].Num')
    json_query(Json, '$.items[1].Num')
    json_query(Json, '$.items[0].Text')
    json_query(Json, '$.items[0].First')
    json_query(Json, '$.items[2]')

json\_modify
~~~~~~~~~~~~

::

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

json\_merge\_patch
~~~~~~~~~~~~~~~~~~

::

      json_merge_patch(JsonDocument, Patch)

The ``json_merge_patch`` function provides a way to patch a target JSON
document with another JSON document. The patch function conforms to
:rfc:`7386`
(href=https://tools.ietf.org/html/rfc7386) RFC 7386

Keys in ``JsonDocument`` are replaced if found in ``Patch``. If the
value in ``Patch`` is ``null`` then the key will be removed in the
target document.

::

    json_merge_patch('{"a":"b"}',          '{"a":"c"}'
    --------------------------------------------------
    {"a":"c"}

    json_merge_patch('{"a": [{"b":"c"}]}', '{"a": [1]}'
    ---------------------------------------------------
    {"a":[1]}

    json_merge_patch('[1,2]',              '{"a":"b", "c":null}'
    ------------------------------------------------------------
    {"a":"b"}

json\_merge\_preserve
~~~~~~~~~~~~~~~~~~~~~

::

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

    json_merge_preserve('{"a":"b"}',          '{"a":"c"}'
    -----------------------------------------------------
    {"a":["b","c"]}

    json_merge_preserve('{"a": [{"b":"c"}]}', '{"a": [1]}'
    ------------------------------------------------------
    {"a":[{"b":"c"},1]}

    json_merge_preserve('[1,2]',              '{"a":"b", "c":null}'
    ---------------------------------------------------------------
    [1,2,{"a":"b","c":null}]
