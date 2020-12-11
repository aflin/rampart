Bit manipulation functions
--------------------------

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

-  | ``bitand(a, b)``
   | Returns the bit-wise AND of ``a`` and ``b``. If one argument is
     shorter than the other, it will be expanded with 0-value integers.

-  | ``bitor(a, b)``
   | Returns the bit-wise OR of ``a`` and ``b``. If one argument is
     shorter than the other, it will be expanded with 0-value integers.

-  | ``bitxor(a, b)``
   | Returns the bit-wise XOR (exclusive OR) of ``a`` and ``b``. If one
     argument is shorter than the other, it will be expanded with
     0-value integers.

-  | ``bitnot(a)``
   | Returns the bit-wise NOT of ``a``.

-  | ``bitsize(a)``
   | Returns the total number of bits in ``a``, i.e. the highest bit
     number plus 1.

-  | ``bitcount(a)``
   | Returns the number of bits in ``a`` that are set to 1.

-  | ``bitmin(a)``
   | Returns the lowest bit number in ``a`` that is set to 1. If none
     are set to 1, returns -1.

-  | ``bitmax(a)``
   | Returns the highest bit number in ``a`` that is set to 1. If none
     are set to 1, returns -1.

-  | ``bitlist(a)``
   | Returns the list of bit numbers of ``a``, in ascending order, that
     are set to 1, as a ``varint``. Returns a single -1 if no bits are
     set to 1.

-  | ``bitshiftleft(a, n)``
   | Returns ``a`` shifted ``n`` bits to the left, with 0s padded for
     bits on the right. If ``n`` is negative, shifts right instead.

-  | ``bitshiftright(a, n)``
   | Returns ``a`` shifted ``n`` bits to the right, with 0s padded for
     bits on the left (i.e. an unsigned shift). If ``n`` is negative,
     shifts left instead.

-  | ``bitrotateleft(a, n)``
   | Returns ``a`` rotated ``n`` bits to the left, with left
     (most-significant) bits wrapping around to the right. If ``n`` is
     negative, rotates right instead.

-  | ``bitrotateright(a, n)``
   | Returns ``a`` rotated ``n`` bits to the right, with right
     (least-significant) bits wrapping around to the left. If ``n`` is
     negative, rotates left instead.

-  | ``bitset(a, n)``
   | Returns ``a`` with bit number ``n`` set to 1. ``a`` will be padded
     with 0-value integers if needed to reach ``n`` (e.g.
     ``bitset(5, 40)`` will return a ``varint(2)``).

-  | ``bitclear(a, n)``
   | Returns ``a`` with bit number ``n`` set to 0. ``a`` will be padded
     with 0-value integers if needed to reach ``n`` (e.g.
     ``bitclear(5, 40)`` will return a ``varint(2)``).

-  | ``bitisset(a, n)``
   | Returns 1 if bit number ``n`` is set to 1 in ``a``, 0 if not.
