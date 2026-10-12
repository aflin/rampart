Rampart Vector Functions
========================

Preface
-------

License
~~~~~~~

The rampart.vector functions are built into the rampart executable
and as such are covered under the same MIT license.

Acknowledgement
~~~~~~~~~~~~~~~

The ``rampart.vector.distance()`` function uses
`SimSIMD <https://github.com/ashvardanian/SimSIMD>`_
(`Apache license <https://github.com/ashvardanian/SimSIMD/blob/main/LICENSE>`_\ )
to dispatch to the fastest available distance calculation function.  The authors
of Rampart express their appreciation for this capable library.

What does it do?
~~~~~~~~~~~~~~~~

The rampart.vector functions provides utility functions to pack, convert and
compare vectors from within JavaScript.  Note that the vector functions in Rampart
are designed to aid semantic search, and are not a robust general purpose set of vector
functions. Additional functionality may be added in the future.

Vectors in General
------------------

Why use Vectors?
~~~~~~~~~~~~~~~~

Vectors can provide a numerical representation of content that captures semantic
meaning rather than relying on exact text matches.  This makes them
effective for identifying related concepts, even when different terms are
used.

Embedding models convert text, images, or other data into high-dimensional
vectors where similar items cluster naturally.  This enables semantic
search, recommendation, and cross-modal matching with simple distance
calculations.

These properties make vectors useful for tasks such as classification,
deduplication, recommendation, and retrieval-augmented generation.  The
rampart.vector functions support fast similarity operations and flexible
conversions for efficient storage and processing.

Vector Distance Primer
~~~~~~~~~~~~~~~~~~~~~~

Different distance metrics emphasize different aspects of vector similarity.
The most common are:

Dot Product / Inner Product
^^^^^^^^^^^^^^^^^^^^^^^^^^^

This method multiplies matching components and sums the results.  It
reflects both magnitude and directional alignment.  With normalized vectors,
it becomes equivalent to `Cosine Similarity` and is widely used for semantic
search, recommendations, and attention mechanisms.  When used with `L2-Normalized vectors`,
it produces a similarity number with range 1.0 (most
similar), 0 (orthogonal) and -1.0 (opposite).  It is widely used for
semantic search, recommendations, and attention mechanisms.

Cosine Distance
^^^^^^^^^^^^^^^

This method measures the angle between vectors, ignoring magnitude.  `Cosine
Distance` is defined as ``1 – cosineSimilarity`` and range from 0 (closest)
to 2 (furthest).  When vectors are `L2-Normalized`, `Cosine Similarity` becomes a
simple dot product, so `Cosine Distance` becomes ``1 – dotProduct``.

Euclidean Distance (L2)
^^^^^^^^^^^^^^^^^^^^^^^

This method measures straight-line distance.  It reflects both
magnitude and direction and is useful when vector scale carries information.
Common in k-NN, clustering, and anomaly detection.  Many embedding models,
however, work best with `Cosine Distance` or `Dot Product` similarity.

L2-Normalization
^^^^^^^^^^^^^^^^

`L2-Normalization` Scales a vector to unit length,placing all vectors on a
unit hypersphere (i.e.  if vectors are three dimensional, each vector would
be on a sphere with radius of 1).  This removes magnitude effects and allows
`Cosine Similarity` to be computed as a simple `Dot Product`.  Many systems
normalize embeddings to improve search speed and consistency.

Common Use In Semantic Search
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A typical workflow is to produce vectors from text using an embedding model,
`L2-Normalize` each and store them.  At search time one can use the `Dot
Product` method to compare a query vector with stored vectors, and sort/order descending
to get the closest matching vectors along with their corresponding texts or other content.

Typed Vectors in Rampart
~~~~~~~~~~~~~~~~~~~~~~~~


Vector Representations in Rampart
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rampart supports a variety of vector formats and functions
to convert between them in a typed vector or in raw form (as a buffer):

* ``Numbers``: a :green:`Array` of :green:`Numbers` - Useful for
  manipulating values in JavaScript. This is the standard JavaScript array
  format where each element is a full-precision 64-bit floating point number.
  While this format offers maximum flexibility for mathematical operations and
  is easiest to work with in JavaScript code, it consumes the most memory and
  may not be optimal for storage or large-scale vector operations.

* ``f64``: a :green:`Buffer` - Holds a ``double *`` c array.  Equivalent
  to ``Numbers`` in precision, but suitable for efficient storage in a database.
  Each element is stored as a 64-bit (8-byte) double-precision floating point value,
  providing the highest accuracy for vector operations. This format is ideal when
  precision is critical and storage space is not a primary concern. It maintains
  full numerical precision during conversions and calculations.

* ``f32``: a :green:`Buffer` - Holds a ``float *`` c array. Each element is
  stored as a 32-bit (4-byte) single-precision floating point value. This format
  reduces storage requirements by 50% compared to ``f64`` while maintaining
  sufficient precision for most machine learning and similarity search applications.
  It is widely used in neural networks and embedding models, offering an excellent
  balance between memory efficiency and numerical accuracy.

* ``f16``: a :green:`Buffer` - Holds a ``uint16_t *`` c array. Each element is
  stored as a 16-bit (2-byte) half-precision floating point value following the
  IEEE 754 standard. This format reduces storage by 75% compared to ``f64``,
  making it suitable for applications where memory is limited or when working
  with very large vector databases. While precision is reduced, ``f16`` is often
  sufficient for similarity calculations and is increasingly supported by modern
  hardware accelerators.

* ``bf16``: a :green:`Buffer` - Holds a ``uint16_t *`` c array. Each element is
  stored as a 16-bit (2-byte) Brain Floating Point value. Unlike ``f16``, ``bf16``
  maintains the same exponent range as ``f32`` but with reduced mantissa precision.
  This format was developed by Google Brain and is particularly well-suited for
  machine learning applications, as it preserves the dynamic range of ``f32`` while
  using half the storage. It's especially effective for gradient calculations and
  model training workflows.

* ``u8``: a :green:`Buffer` - Holds a ``uint8_t *`` c array. Each element is
  stored as an 8-bit (1-byte) unsigned integer with values ranging from 0 to 255.
  This format achieves an 87.5% reduction in storage compared to ``f64``, making it
  ideal for very large-scale vector databases where storage and memory bandwidth are
  critical constraints. Vectors must be quantized (scaled and rounded) to fit in this
  range, but for many similarity search applications, the trade-off between precision
  and efficiency is worthwhile.

* ``i8``: a :green:`Buffer` - Holds a ``int8_t *`` c array. Each element is
  stored as an 8-bit (1-byte) signed integer with values ranging from -127 to 127.
  Like ``u8``, this format provides maximum storage efficiency but with support
  for negative values. It's commonly used for quantized neural network weights
  and embeddings where the distribution is centered around zero, allowing for
  efficient computation while maintaining acceptable accuracy for similarity
  comparisons.


Typed Vectors
-------------

Rampart vectors are opaque :green:`Objects` which hold a vector in a
:green:`Buffer` along with metadata such as number of dimensions and
vector type.  A vector can be created or initialized from an :green:`Array`
of :green:`Numbers` or from an existing raw :green:`Buffer` using the
``new rampart.vector()`` call.

new rampart.vector()
~~~~~~~~~~~~~~~~~~~~

Create an empty or initialize a new `Vector Object` from an :green:`Array`
of :green:`Numbers` or :green:`Buffer`.

Usage:

.. code-block:: javascript

   var vec = new rampart.vector(type, [ndim|rawbuf|numbarr]);

Where:

   * ``type`` is a :green:`String`, one of ``f64``, ``f32``, ``f16``,
     ``bf16``, ``i8``, ``u8`` or ``b8`` (also spelled ``bit``, a packed
     1-bit-per-dimension binary vector -- see `Binary Vectors (b8)`_ below);

   * ``ndim`` is a positive :green:`Number`, the dimensionality (number of elements)
     for a new zero-filled vector.

   * ``rawbuf`` is a :green:`Buffer`, the raw binary data holding a vector (i.e. ``double *``, ``float *``
     ``uint16_t *`` arrays in c).  Number of elements is calculated from the type and length of the vector.
     For a ``b8`` vector the buffer holds packed bits and the dimension is ``8 ×`` its byte length.

   * ``numbarr`` is an :green:`Array` of :green:`Numbers`, with each :green:`Number` being an element of the
     vector.  For a ``b8`` vector the numbers are binarized (see `Binary Vectors (b8)`_), with an
     optional third argument giving the cutoff.

Return Value from new rampart.vector()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Several constants and methods will be available as properties of the resulting `Vector Object`.

.. code-block:: javascript

   var n = [0,1,2,3,4,5,6,7];
   var v = new rampart.vector('f32',n);

   rampart.utils.printf("%3J\n", v);
   /* expected output:
      {
         "type": "f32",
         "dim": 8
      }
   */

Note that only the two constants are serialized by ``%3J``; the methods are
present on the object but are not rendered by JSON.  The full set of property
names for the example above is:

.. code-block:: javascript

   Object.getOwnPropertyNames(v);
   /* [ "type", "dim", "toF64", "toF32", "toF16", "toBf16", "toI8", "toU8",
        "toNumbers", "toBit", "l2Normalize", "toRaw", "byteLength",
        "resize", "copy", "split", "distance" ] */


Vector Object Conversion Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each `Vector Object` will have several methods to convert the underlying
vector to other types.

Note that not every vector type can be directly converted to another, so a
given `Vector Object` only has the conversion methods that apply to it.  The
current limitations are:

   *  ``toF64()``, ``toF32()`` and ``toNumbers()`` are available for **all**
      types.
   *  ``toF16()`` is available for every type **except** ``bf16``.
   *  ``toBf16()`` is available only from ``f64``, ``f32`` and ``bf16``
      (its own type).
   *  ``toI8()`` is available from ``f64``, ``f32``, ``f16``, ``i8`` and
      ``u8`` (not ``bf16``).  See `u8 → i8 rebase`_ for the ``u8`` case.
   *  ``toU8()`` is available from ``f64``, ``f32``, ``f16`` and ``u8`` (not
      ``bf16`` or ``i8``).

In short, ``bf16`` only inter-converts with ``f64``/``f32`` (and itself), and
the two 8-bit integer types (``i8``/``u8``) do not convert directly to each
other.  To bridge an unsupported pair, convert through ``f32`` (or ``f64``)
first.  Calling a conversion method that is not present throws.

Also note that each type has a conversion method to its own type (i.e.  an
``f64`` `Vector Object` will have a ``.toF64()`` method).  These are null
operations and return the same `Vector Object`, while other methods return a new
`Vector Object` or :green:`Array`.

Available methods:

   *  ``toF64()`` - Convert to a F64 (``double *``) vector.
   *  ``toF32()`` - Convert to a F32 (``float *``) vector.
   *  ``toF16()`` - Convert to a F16 (half precision) vector.
   *  ``toBf16()`` - Convert to a Brain Float vector.
   *  ``toI8()`` - Convert to a quantized 8 bit signed (``int8_t *``) vector.
   *  ``toU8()`` - Convert to a quantized 8 bit unsigned (``uint8_t *``) vector.
   *  ``toBit([cutoff])`` - Binarize to a ``b8`` (1-bit-per-dimension) vector.
      See `Binary Vectors (b8)`_.
   *  ``toNumbers()`` - Convert to an :green:`Array` of :green:`Numbers`.  For a
      ``b8`` vector this returns an :green:`Array` of ``0``/``1`` values.

Note that a ``b8`` `Vector Object` has a reduced method set: in addition to
``toNumbers``, ``toRaw``, ``byteLength``, ``copy``, ``split`` and
``distance``, it provides
the *reconstruction* methods ``toF64``/``toF32``/``toF16``/``toBf16``/``toI8``/
``toU8`` (see `Binary Vectors (b8)`_) -- but these are lossy expansions of the
bits, **not** a round-trip to the original vector.

Note that these methods take no arguments, except ``toU8`` and ``toI8``
may take an optional ``(scale[, zeroPoint]))``. See Raw Conversions below
for more detail.

Information Constants
~~~~~~~~~~~~~~~~~~~~~

   * ``type`` - type of underlying vector in the `Vector Object`.
   * ``dim``  - the number of elements in the underlying vector.

Utility Functions
~~~~~~~~~~~~~~~~~

   * ``l2Normalize()`` - perform an in-place `L2-Normalization` of the
     vector and return the same `Vector Object`.  Available on the float
     types (``f64``, ``f32``, ``f16``) only; it is not present on
     ``bf16``, ``i8``, ``u8`` or ``b8``.
   * ``toRaw()`` - return the underlying :green:`Buffer`.
   * ``copy()`` - Copy the underlying :green:`Buffer` and return a new
     `Vector Object`.
   * ``resize(n)`` - Copy and grow or truncate the underlying :green:`Buffer`
     so the vector contains ``n`` elements.  Return a new `Vector Object`.
   * ``split(n)`` - Split the vector into an :green:`Array` of new
     `Vector Objects` of ``n`` elements each.  ``dim`` must be an exact
     multiple of ``n`` (a vector whose ``dim`` equals ``n`` returns a
     one-element :green:`Array`); each part is an independent copy of the
     same type.  This is the JavaScript-side accessor for *multi-vector*
     column values such as those produced by the SQL ``chunkembed()``
     function, which concatenates a document's ``k`` chunk vectors into a
     single ``k × n``-element value — see
     :ref:`rampart-sql:Chunked documents (multi-vector rows)`.  For ``b8``
     vectors ``n`` is a bit count and must be a multiple of 8.
   * ``byteLength()`` - Return the length of the underlying :green:`Buffer` in bytes.

Example — splitting a multi-vector value (fragment; assumes an open
``sql`` connection and a query vector ``queryVec``):

.. code-block:: javascript

   // a chunkembed() column value: k chunk vectors of 384 elements each
   var row = sql.one("select Vec from docs");
   var chunks = row.Vec.split(384);      // Array of row.Vec.dim/384 vectors

   for (var i = 0; i < chunks.length; i++)
       printf("chunk %d: dim=%d best=%f\n", i, chunks[i].dim,
              chunks[i].distance(queryVec, 'dot'));

Distance Function
~~~~~~~~~~~~~~~~~

Works the same as `Raw Vector Distance Function`_
except that type is derived and not specified.

The vectors must be of the same type and have the
same number of elements.  If not, a conversion
must be performed to make the two vectors match.

Example:

.. code-block:: javascript

   var n1 = [0,1,2,3,4,5,6,7];
   var v1 = new rampart.vector('f32',n1);

   var n2 = [0,-1,-2,-3,-4,-5,-6,-7];
   var v2 = new rampart.vector('f64',n2);

   v1.l2Normalize();
   v2.l2Normalize();

   // cannot pass a vector of a different type
   try {
      var score = v1.distance(v2, 'dot');
   } catch(e) {
      // e.message == "vector.distance() - vectors must be the same type, convert one first"
   }

   // convert v2 to f32, then compare
   var score = v1.distance(v2.toF32(), 'dot');
   /* score ~= -1.0 */

   // OR convert v1 to f64, then compare
   var score = v1.toF64().distance(v2, 'dot');
   /* score ~= -1.0 */

Raw Vector Conversion Functions
-------------------------------

Unlike the `Vector Object` methods above, the following functions work
directly on raw :green:`Buffer` representations of vectors (the same type that
are produced by ``.toRaw()`` above).  As such, care must
be taken that input vectors are of the expected type.

rampart.vector.raw.numbersToF64
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert an :green:`Array` of :green:`Numbers` to a :green:`Buffer` holding a
``double *`` array.

Usage:

.. code-block:: javascript

   rampart.vector.raw.numbersToF64(myarr);

Where ``myarray`` is an :green:`Array` of :green:`Numbers`.

Return Value:
   A :green:`Buffer` holding a ``double *`` array.

rampart.vector.raw.numbersToF32
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert an :green:`Array` of :green:`Numbers` to a :green:`Buffer` holding a
``float *`` array.

Usage:

.. code-block:: javascript

   rampart.vector.raw.numbersToF32(myarr);

Where ``myarr`` is an :green:`Array` of :green:`Numbers`.

Return Value:
   A :green:`Buffer` holding a ``float *`` array.

rampart.vector.raw.numbersToF16
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert an :green:`Array` of :green:`Numbers` to a :green:`Buffer` holding a
``uint16_t *`` array (IEEE 754 half-precision floating point).

Usage:

.. code-block:: javascript

   rampart.vector.raw.numbersToF16(myarr);

Where ``myarr`` is an :green:`Array` of :green:`Numbers`.

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.

rampart.vector.raw.numbersToBf16
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert an :green:`Array` of :green:`Numbers` to a :green:`Buffer` holding a
``uint16_t *`` array (Brain Floating Point 16).

Usage:

.. code-block:: javascript

   rampart.vector.raw.numbersToBf16(myarr);

Where ``myarr`` is an :green:`Array` of :green:`Numbers`.

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.

rampart.vector.raw.numbersToI8
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert an :green:`Array` of :green:`Numbers` to a :green:`Buffer` holding an ``int8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.numbersToI8(myarr [, scale [, zeroPoint]]);

Where:

* ``myarr`` is an :green:`Array` of :green:`Numbers`.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional :green:`Number` (``-128`` - ``127``). Default is ``0``.

Return Value:
  A :green:`Buffer` holding an ``int8_t *`` array.


rampart.vector.raw.numbersToU8
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert an :green:`Array` of :green:`Numbers` to a :green:`Buffer` holding a ``uint8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.numbersToU8(myarr [, scale [, zeroPoint]]);

Where:

* ``myarr`` is an :green:`Array` of :green:`Numbers`.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional positive :green:`Number` (``0`` - ``255``). Default is ``0``.



rampart.vector.raw.f64ToNumbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``double *`` array to an :green:`Array` of :green:`Numbers`.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f64ToNumbers(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``double *`` array.

Return Value:
   An :green:`Array` of :green:`Numbers`.


rampart.vector.raw.f32ToNumbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``float *`` array to an :green:`Array` of :green:`Numbers`.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f32ToNumbers(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``float *`` array.

Return Value:
   An :green:`Array` of :green:`Numbers`.


rampart.vector.raw.f16ToNumbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point) to an :green:`Array` of :green:`Numbers`.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f16ToNumbers(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.

Return Value:
   An :green:`Array` of :green:`Numbers`.


rampart.vector.raw.bf16ToNumbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (Brain Floating Point 16) to an :green:`Array` of :green:`Numbers`.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.bf16ToNumbers(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.

Return Value:
   An :green:`Array` of :green:`Numbers`.


rampart.vector.raw.u8ToNumbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint8_t *`` array to an :green:`Array` of :green:`Numbers`.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.u8ToNumbers(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``uint8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/255``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   An :green:`Array` of :green:`Numbers`.


rampart.vector.raw.i8ToNumbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding an ``int8_t *`` array to an :green:`Array` of :green:`Numbers`.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.i8ToNumbers(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``int8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/127.0``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   An :green:`Array` of :green:`Numbers`.



rampart.vector.raw.f64ToF32
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``double *`` array to a :green:`Buffer` holding a ``float *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f64ToF32(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``double *`` array.

Return Value:
   A :green:`Buffer` holding a ``float *`` array.


rampart.vector.raw.f64ToF16
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``double *`` array to a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point).

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f64ToF16(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``double *`` array.

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.


rampart.vector.raw.f64ToBf16
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``double *`` array to a :green:`Buffer` holding a ``uint16_t *`` array (Brain Floating Point 16).

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f64ToBf16(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``double *`` array.

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.


rampart.vector.raw.f64ToI8
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``double *`` array to a :green:`Buffer` holding an ``int8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f64ToI8(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``double *`` array.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional :green:`Number` (``-128`` - ``127``). Default is ``0``.

Return Value:
   A :green:`Buffer` holding an ``int8_t *`` array.


rampart.vector.raw.f64ToU8
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``double *`` array to a :green:`Buffer` holding a ``uint8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f64ToU8(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``double *`` array.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional positive :green:`Number` (``0`` - ``255``). Default is ``0``.

Return Value:
   A :green:`Buffer` holding a ``uint8_t *`` array.


rampart.vector.raw.f32ToF64
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``float *`` array to a :green:`Buffer` holding a ``double *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f32ToF64(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``float *`` array.

Return Value:
   A :green:`Buffer` holding a ``double *`` array.


rampart.vector.raw.f16ToF64
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point) to a :green:`Buffer` holding a ``double *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f16ToF64(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.

Return Value:
   A :green:`Buffer` holding a ``double *`` array.


rampart.vector.raw.bf16ToF64
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (Brain Floating Point 16) to a :green:`Buffer` holding a ``double *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.bf16ToF64(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.

Return Value:
   A :green:`Buffer` holding a ``double *`` array.


rampart.vector.raw.i8ToF64
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding an ``int8_t *`` array to a :green:`Buffer` holding a ``double *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.i8ToF64(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding an ``int8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/127.0``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   A :green:`Buffer` holding a ``double *`` array.


rampart.vector.raw.u8ToF64
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint8_t *`` array to a :green:`Buffer` holding a ``double *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.u8ToF64(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``uint8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/255``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   A :green:`Buffer` holding a ``double *`` array.


rampart.vector.raw.f32ToF16
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``float *`` array to a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point).

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f32ToF16(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``float *`` array.

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.


rampart.vector.raw.f32ToBf16
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``float *`` array to a :green:`Buffer` holding a ``uint16_t *`` array (Brain Floating Point 16).

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f32ToBf16(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``float *`` array.

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.


rampart.vector.raw.f32ToI8
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``float *`` array to a :green:`Buffer` holding an ``int8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f32ToI8(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``float *`` array.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional :green:`Number` (``-128`` - ``127``). Default is ``0``.

Return Value:
   A :green:`Buffer` holding an ``int8_t *`` array.


rampart.vector.raw.f32ToU8
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``float *`` array to a :green:`Buffer` holding a ``uint8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f32ToU8(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``float *`` array.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional positive :green:`Number` (``0`` - ``255``). Default is ``0``.

Return Value:
   A :green:`Buffer` holding a ``uint8_t *`` array.



rampart.vector.raw.f16ToF32
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point) to a :green:`Buffer` holding a ``float *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f16ToF32(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.

Return Value:
   A :green:`Buffer` holding a ``float *`` array.


rampart.vector.raw.bf16ToF32
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (Brain Floating Point 16) to a :green:`Buffer` holding a ``float *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.bf16ToF32(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.

Return Value:
   A :green:`Buffer` holding a ``float *`` array.


rampart.vector.raw.u8ToF32
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint8_t *`` array to a :green:`Buffer` holding a ``float *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.u8ToF32(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``uint8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/255``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   A :green:`Buffer` holding a ``float *`` array.


rampart.vector.raw.i8ToF32
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding an ``int8_t *`` array to a :green:`Buffer` holding a ``float *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.i8ToF32(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding an ``int8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/127.0``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   A :green:`Buffer` holding a ``float *`` array.


rampart.vector.raw.f16ToI8
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point) to a :green:`Buffer` holding an ``int8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f16ToI8(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional :green:`Number` (``-128`` - ``127``). Default is ``0``.

Return Value:
   A :green:`Buffer` holding an ``int8_t *`` array.


rampart.vector.raw.f16ToU8
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point) to a :green:`Buffer` holding a ``uint8_t *`` array.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.f16ToU8(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default: auto calculate)
* ``zeroPoint`` is an optional positive :green:`Number` (``0`` - ``255``). Default is ``0``.

Return Value:
   A :green:`Buffer` holding a ``uint8_t *`` array.



rampart.vector.raw.i8ToF16
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding an ``int8_t *`` array to a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point).

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.i8ToF16(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding an ``int8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/127.0``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.


rampart.vector.raw.u8ToF16
~~~~~~~~~~~~~~~~~~~~~~~~~~

Convert a :green:`Buffer` holding a ``uint8_t *`` array to a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point).

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.u8ToF16(mybuff [, scale [, zeroPoint]]);

Where:

* ``mybuff`` is a :green:`Buffer` holding a ``uint8_t *`` array.
* ``scale`` is an optional positive :green:`Number` (default ``1.0/255``)
* ``zeroPoint`` is an optional :green:`Number` (default is ``0``).

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array.

rampart.vector.raw.l2NormalizeNumbers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Perform an in-place L2 normalization on an :green:`Array` of :green:`Numbers`, scaling the vector to unit length.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.l2NormalizeNumbers(myarr);

Where ``myarr`` is an :green:`Array` of :green:`Numbers`.

Return Value:
   An :green:`Array` of :green:`Numbers` with normalized values.

Note:
   All L2 normalization functions are in-place, transforming the input vector and returning it.

rampart.vector.raw.l2NormalizeF64
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Perform an in-place L2 normalization on a :green:`Buffer` holding a ``double *`` array, scaling the vector to unit length.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.l2NormalizeF64(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``double *`` array.

Return Value:
   A :green:`Buffer` holding a ``double *`` array with normalized values.

Note:
   All L2 normalization functions are in-place, transforming the input vector and returning it.

rampart.vector.raw.l2NormalizeF32
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Perform an in-place L2 normalization on a :green:`Buffer` holding a ``float *`` array, scaling the vector to unit length.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.l2NormalizeF32(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``float *`` array.

Return Value:
   A :green:`Buffer` holding a ``float *`` array with normalized values.

Note:
   All L2 normalization functions are in-place, transforming the input vector and returning it.

rampart.vector.raw.l2NormalizeF16
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Perform an in-place L2 normalization on a :green:`Buffer` holding a ``uint16_t *`` array (IEEE 754 half-precision floating point), scaling the vector to unit length.

Usage:

.. code-block:: javascript

   var res = rampart.vector.raw.l2NormalizeF16(mybuff);

Where ``mybuff`` is a :green:`Buffer` holding a ``uint16_t *`` array.

Return Value:
   A :green:`Buffer` holding a ``uint16_t *`` array with normalized values.

Note:
   All L2 normalization functions are in-place, transforming the input vector and returning it.

Raw Vector Distance Function
----------------------------

The ``rampart.vector.raw.distance()`` function measures the distance or score by comparing two vectors.

.. code-block:: javascript

   var dist = rampart.vector.raw.distance(myvec, myvec2 [, metric [, vecType]]);

Where:

* ``myvec`` is one of the supported vector types above.
* ``myvec2`` is a vector matching ``myvec`` in type and dimensions (number of elements/Numbers in the array).
* ``metric`` is a :green:`String`. Default is ``dot``. The supported metrics are:

   * ``dot`` (aliases ``ip``, ``inner``) - inner product / similarity.
   * ``cosine`` (alias ``cos``) - cosine distance, ``0`` .. ``2``.
   * ``euclidean`` (alias ``l2``) - true L2 distance (square root of the sum of squared differences).
   * ``l2sq`` (alias ``sqeuclidean``) - squared L2 distance (faster; ranks identically to ``euclidean``).
   * ``hamming``, ``jaccard`` - binary metrics, valid only for the ``b8`` vecType (see `Binary Vectors (b8)`_).
   * ``kl`` (aliases ``kld``, ``kullback_leibler``), ``js`` (alias ``jensen_shannon``) - Kullback-Leibler and
     Jensen-Shannon divergences, for floating-point vectors that represent probability distributions.

* ``vecType`` is a :green:`String`, one of ``numbers``, ``f64``, ``f32``, ``f16``, ``bf16``, ``i8``, ``u8`` or
  ``b8`` specifying the type of ``myvec`` and ``myvec2``.   Default is ``f16``.

Return Value:
   * For ``euclidean`` and ``cosine`` a measure of the distance between the two vectors (with 0 being the closest).
   * For ``dot`` the `Cosine Similarity` between two `L2-Normalized` vectors (with 1.0 being exact match and -1.0 being the opposite).

Note:
  * ``euclidean`` is a true distance function (the square root of the sum of squared
    differences), taking into account angle and magnitude. The return distance range
    depends on the magnitude of the input vectors.
  * ``l2sq`` (alias ``sqeuclidean``) returns the **squared** Euclidean distance -- the
    same value as ``euclidean`` before the square root.  It is slightly faster (no
    ``sqrt``) and ranks identically to ``euclidean``, so it is preferable when you only
    need relative ordering rather than an actual distance.
  * ``dot``, assuming L2 Normalized vectors are given to it, will return a similarity score of ``-1.0`` to ``1.0`` with ``1.0`` being
    an exact match and ``-1.0`` being the exact opposite.
  * For the floating point types (``f64``, ``f32``, ``f16``, ``bf16``) ``dot`` is the
    plain inner product, which equals the cosine similarity only when the inputs are
    already L2 Normalized -- skipping the normalization step is what makes ``dot`` faster.
  * For the quantized integer types (``i8`` and ``u8``) a unit-length vector cannot be
    represented, so ``dot`` instead returns the scale-invariant normalized similarity
    (equivalent to ``1 - cosineDistance``).  This keeps ``dot`` in the same ``-1.0`` to
    ``1.0`` range as the float types, so switching a vector's storage type (e.g. ``f16``
    to ``i8``) does not change the meaning or range of the score.
  * ``cosine`` computes distance by dividing by vector magnitudes (effectively normalizing). It returns distance of ``0`` to ``2.0``.
  * ``1 - cosineScore == dotDistance`` if the vectors are L2 Normalized.  However, the ``dot`` calculation is simpler and faster for
    vectors that are already L2 Normalized.
  *  The distance functions assumes ``dot`` and L2 normalized ``f16`` vectors, as these settings provides gains in terms
     of memory and speed while retaining a high level of accuracy.

Binary Vectors (b8)
-------------------

A ``b8`` (also spelled ``bit``) vector stores **one bit per dimension**, packed
8 bits to a byte.  This is *binary quantization* -- the most aggressive form of
quantization, giving roughly **32× less storage than f32** (1 bit vs 32 bits per
dimension).  Binary vectors are compared with the very fast ``hamming`` or
``jaccard`` metrics (a ``popcount`` of the XOR), which makes them ideal as the
cheap first stage of a two-stage search: shortlist with ``hamming`` on bit
vectors, then rescore the top candidates with full-precision (``f16``/``f32``)
vectors.

Binarization sets bit ``i`` to ``1`` when element ``i`` is **greater than a
cutoff**.  The default cutoff is ``0`` (the sign bit) for the floating point and
``i8`` types, and ``128`` for ``u8`` (the symmetric-quantization zero point).
The cutoff may be overridden.

The functions below produce a packed :green:`Buffer` (``ceil(dim/8)`` bytes;
padding bits in the final byte are ``0``):

   * ``rampart.vector.raw.numbersToBit(array[, cutoff])``
   * ``rampart.vector.raw.f64ToBit(buffer[, cutoff])``
   * ``rampart.vector.raw.f32ToBit(buffer[, cutoff])``
   * ``rampart.vector.raw.f16ToBit(buffer[, cutoff])``
   * ``rampart.vector.raw.bf16ToBit(buffer[, cutoff])``
   * ``rampart.vector.raw.i8ToBit(buffer[, cutoff])``
   * ``rampart.vector.raw.u8ToBit(buffer[, cutoff])`` (default ``cutoff`` ``128``)

The typed `Vector Object` has the equivalent ``toBit([cutoff])`` method, which
returns a ``b8`` `Vector Object`.  A ``b8`` vector can also be created directly
with ``new rampart.vector('b8', rawbuf | ndim)`` or
``new rampart.vector('bit', numbersArray[, cutoff])``.

Binary vectors are compared with two metrics (passed to
`Raw Vector Distance Function`_ as ``vecType`` ``b8``, or used directly on a
``b8`` `Vector Object` whose ``distance()`` defaults to ``hamming``):

   * ``hamming`` - the number of differing bits (lower is closer).
   * ``jaccard`` - the Jaccard/Tanimoto distance, ``1 - |intersection|/|union|``
     of the set bits (``0`` identical .. ``1`` disjoint).

Example:

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   // binarize two f32 vectors by sign and compare with hamming
   var a = new rampart.vector('f32', [ 1, 1, 1, 1,-1,-1,-1,-1]).toBit();
   var b = new rampart.vector('f32', [ 1, 1,-1,-1,-1,-1, 1, 1]).toBit();

   printf("%d\n", a.distance(b));            /* 4  (hamming is the b8 default) */
   printf("%d\n", a.distance(a));            /* 0  (identical) */

   // raw form
   var ra = rampart.vector.raw.f32ToBit(rampart.vector.raw.numbersToF32([1,1,1,1,-1,-1,-1,-1]));
   var rb = rampart.vector.raw.f32ToBit(rampart.vector.raw.numbersToF32([1,1,-1,-1,-1,-1,1,1]));
   printf("%d\n", rampart.vector.raw.distance(ra, rb, 'hamming', 'b8'));  /* 4 */

Reconstructing b8 to a wider type (asymmetric scoring)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Binarization is one-way -- the magnitude is gone -- but a b8 vector can be
*reconstructed* to a wider type by expanding each bit back to a sign value.
This is useful for **asymmetric scoring**: comparing a full-precision query
against binary-quantized documents (keeping the query precise recovers ranking
quality that symmetric ``hamming`` discards).  Each bit expands as follows:

   * **float targets** (``f64``/``f32``/``f16``/``bf16``): bit 1 → ``+1/√D``,
     bit 0 → ``−1/√D`` (where ``D`` is the dimension), so the result is a
     **unit-length** vector -- a true ``dot`` against a normalized query then
     lands in ``-1 .. 1``.
   * **integer targets**: bit 1 → ``+127``, bit 0 → ``−127``, offset by an
     optional ``zeroPoint`` (default ``0`` for ``i8``, ``127`` for ``u8`` →
     bytes ``{254, 0}``).  The ``±127`` is the full-scale ``±1`` *direction*;
     it is intentionally **not** ``√D``-scaled, because the integer metrics are
     magnitude-invariant (see below) and a ``√D`` factor would only cost
     quantization headroom.

The single rule behind both rows: reconstruct so an asymmetric score comes out
**calibrated to ``-1 .. 1``**.  Floats reach that with ``√D`` under a true
``dot``; ``i8``/``u8`` reach it because their ``dot`` is routed through
``cosine`` (which is magnitude-invariant) -- so the integer direction needs no
``√D``.

The reconstruction is available as `Vector Object` methods on a ``b8`` vector
(``b8vec.toF32()``, ``b8vec.toI8([zeroPoint])``, etc.) and as raw functions:

   * ``rampart.vector.raw.bitToF64(buf)`` / ``bitToF32`` / ``bitToF16`` / ``bitToBf16``
   * ``rampart.vector.raw.bitToI8(buf[, zeroPoint])`` / ``bitToU8(buf[, zeroPoint])``

(for the raw functions ``D`` is taken as ``byteLength × 8``; use the typed
`Vector Object` if the dimension is not a multiple of 8).

u8 → i8 rebase
~~~~~~~~~~~~~~

``rampart.vector.raw.u8ToI8(buf[, zeroPoint])`` (and the ``u8`` `Vector
Object`'s ``toI8([zeroPoint])`` method) convert a signed ``u8`` vector to
``i8`` by subtracting the ``zeroPoint`` (default ``127``): ``i8 = clamp(u8 −
zeroPoint, −128, 127)``.

.. note::

   You no longer need this rebase just to *compare* ``u8`` vectors: the
   distance function rebases ``u8`` internally (by the symmetric ``zeroPoint``
   ``128``) for ``cosine``/``dot``, so ``u8`` compares the same as ``i8`` and
   ``f32`` out of the box (see `Cross-type distance consistency`_).  ``u8ToI8``
   remains useful when you want an explicit ``i8`` value, or to control the
   ``zeroPoint`` for asymmetrically-quantized data.

Example (asymmetric):

.. code-block:: javascript

   rampart.globalize(rampart.utils);

   var query = new rampart.vector('f32', [0.6,-0.8,0.5,-0.3,0.1,-0.9,0.4,-0.2]);
   query.l2Normalize();

   var doc = new rampart.vector('b8', [1,0,1,0,1,0,1,1]);   // a stored binary doc

   // reconstruct the binary doc to a unit ±1/√D vector and score with cosine
   var score = query.distance(doc.toF32(), 'cosine');

Cross-type distance consistency
-------------------------------

A core guarantee of ``rampart.vector``: you can store a vector in one type,
convert it to another, and compute a distance, and get the **same result
within the bounds of the conversion's quantization error**.  Which metrics
honor this depends on whether the metric cares about magnitude.

**Scale-invariant metrics -- ``cosine`` and ``dot`` -- are consistent across
every type.**  Converting the same underlying vector to ``f64``, ``f32``,
``f16``, ``bf16``, ``i8`` or ``u8`` and comparing gives the same similarity to
within quantization error (typically ``< 1e-3`` at moderate dimension; larger
for low dimension).  Notes:

   * ``dot`` on ``i8``/``u8`` is computed as the cosine similarity (a true
     integer inner product would be a large, scale-dependent number); this
     keeps ``dot`` in the same ``-1 .. 1`` range across all types.
   * ``u8`` is rebased internally by the symmetric ``zeroPoint`` ``128`` before
     ``cosine``/``dot``, so it "just works" without a manual ``u8ToI8``.

**Scale-sensitive metrics -- ``euclidean``/``l2`` and ``l2sq`` -- are
consistent only among the float types** (``f64``/``f32``/``f16``/``bf16``).
Integer and binary quantization deliberately store *direction at a normalized
scale and discard absolute magnitude*, so a raw L2 distance on ``i8``/``u8``
(or a reconstructed ``b8``) is **not** comparable to the float result -- it is
off by the quantization scale, and no rebase recovers it.  This is inherent to
quantization, which is why vector databases pair quantized/binary types with
``cosine``/``dot`` rather than raw L2.  For **normalized** vectors L2 is anyway
determined by cosine -- ``|a − b|² = 2 − 2·cos`` -- so staying normalized and
using ``cosine`` gives L2-consistent ranking for free.

**Binary (``b8``).**  The asymmetric path -- a full-precision (or ``i8``) query
against a ``b8`` document reconstructed with ``toF32``/``toI8`` -- is consistent
with the float reference to within the binarization error (``cosine`` error on
the order of ``1e-4``).  A fully-binary ``b8`` × ``b8`` comparison is lossier
by design (1 bit per dimension); use ``hamming``/``jaccard`` for it, optionally
followed by a reconstruction-based or original-vector rescore.

.. note::

   The internal ``u8`` rebase assumes the **symmetric** quantization
   convention (``zeroPoint`` ``128``), which is what normalized data produces.
   ``f32ToU8`` with default arguments uses *asymmetric* per-vector calibration
   (``zeroPoint = round(−min / scale)``); for symmetric/normalized inputs this
   converges to ``128`` and the comparison is exact, but for strongly skewed
   data dequantize to a float type first (``toF32``) before comparing.

Recommended metric by type:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - vector type
     - use for cross-type comparison
   * - ``f64`` / ``f32`` / ``f16`` / ``bf16``
     - ``cosine``, ``dot``, or the L2 family
   * - ``i8`` / ``u8``
     - ``cosine`` or ``dot`` (not raw L2)
   * - ``b8``
     - ``hamming`` / ``jaccard``; or reconstruct for asymmetric ``cosine`` / ``dot``
