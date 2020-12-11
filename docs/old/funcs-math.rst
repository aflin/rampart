Math functions
--------------

The following basic math functions are available in Texis: ``acos``,
``asin``, ``atan``, ``atan2``, ``ceil``, ``cos``, ``cosh``, ``exp``,
``fabs``, ``floor``, ``fmod``, ``log``, ``log10``, ``pow``, ``sin``,
``sinh``, ``sqrt``, ``tan``, ``tanh``.

All of the above functions call the ANSI C math library function of the
same name, and return a result of type ``double``. ``pow``, ``atan2``
and ``fmod`` take two double arguments, the remainder take one double
argument.

In addition, the following math-related functions are available:

-  | ``isNaN(x)``
   | Returns 1 if ``x`` is a float or double NaN (Not a Number) value, 0
     if not. This function should be used to test for NaN, rather than
     using the equality operator (e.g. ``x = 'NaN'``), because the IEEE
     standard defines ``NaN == NaN`` to be false, not true as might be
     expected.
