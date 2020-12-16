Date functions
--------------

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
