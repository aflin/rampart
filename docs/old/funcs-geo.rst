Geographical coordinate functions
---------------------------------

The geographical coordinate functions allow for efficient processing of
latitude / longitude operations. They allow for the conversion of a
latitude/longitude pair into a single “geocode”, which is a single
``long`` value that contains both values. This can be used to easily
compare it to other geocodes (for distance calculations) or for finding
other geocodes that are within a certain distance.

azimuth2compass
~~~~~~~~~~~~~~~

::

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
~~~~~~~~~~~~~~

::

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

-  ``pythagorean`` - Calculations based on the pythagorean method can
   also be used. They’re faster, but less accurate as the core formulas
   don’t take the curvature of the earth into consideration. Some
   internal adjustments are made, but the values are less accurate than
   the ``greatcircle`` method, especially over long distances and with
   paths that approach the poles.


azimuthlatlon
~~~~~~~~~~~~~

::

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

-  ``pythagorean`` - Calculations based on the pythagorean method can
   also be used. They’re faster, but less accurate as the core formulas
   don’t take the curvature of the earth into consideration. Some
   internal adjustments are made, but the values are less accurate than
   the ``greatcircle`` method, especially over long distances and with
   paths that approach the poles.

.. _dms-dec:

dms2dec, dec2dms
~~~~~~~~~~~~~~~~

::

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
~~~~~~~~~~~

::

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
~~~~~~~~~~

::

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
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

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

::

      -- Populate a table with latitude/longitude information:
      create table geotest(city varchar(64), lat double, lon double,
                           geocode long);
      insert into geotest values('Cleveland, OH, USA', 41.4,  -81.5,  -1);
      insert into geotest values('San Francisco, CA, USA',   37.78, -122.42,  -1);
      insert into geotest values('Davis, Ca, USA',    38.55, -121.74, -1);
      insert into geotest values('New York, NY, USA',  40.81, -73.96,  -1);
      -- Prepare for geographic searches:
      update geotest set geocode = latlon2geocode(lat, lon);
      create index xgeotest_geocode on geotest(geocode);
      -- Search for cities within a 3-degree-radius "circle" (box)
      -- of Cleveland, nearest first:
      select city, lat, lon, distlatlon(41.4, -81.5, lat, lon) MilesAway
      from geotest
      where geocode between (select latlon2geocodearea(41.4, -81.5, 3.0))
      order by 4 asc;


The geocode values returned by ``latlon2geocode`` and
``latlon2geocodearea`` are platform-dependent in format and accuracy,
and should not be copied across platforms. On platforms with 32-bit
``long``\ s a geocode value is accurate to about 32 seconds (around half
a mile, depending on latitude). ``-1`` is returned for invalid input values.

See Also: `geocode2lat, geocode2lon`_

geocode2lat, geocode2lon
~~~~~~~~~~~~~~~~~~~~~~~~

::

      geocode2lat(geocode)
      geocode2lon(geocode)

The ``geocode2lat`` and ``geocode2lon`` functions decode a geocode into
a latitude or longitude coordinate, respectively. The returned
coordinate is in the decimal degrees format. An invalid geocode value
(e.g. -1) will return NaN (Not a Number).

If you want :math:`DDDMMSS` “degrees minutes seconds” (DMS) format, you
can use :ref:`dec2dms <dms-dec>` to convert it.

::

      select city, geocode2lat(geocode), geocode2lon(geocode) from geotest;

As with :ref:`latlon2geocode <latlon2x>`, the ``geocode`` value is platform-dependent
in accuracy and format, so it should not be copied across platforms, and
the returned coordinates from ``geocode2lat`` and ``geocode2lon`` may
differ up to about half a minute from the original coordinates (due to
the finite resolution of a ``long``). An invalid geocode value (e.g. -1)
will return ``NaN`` (Not a Number).

See Also: :ref:`latlon2geocode <latlon2x>`

parselatitude, parselongitude
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

      parselatitude(latitudeText)
      parselongitude(longitudeText)

The ``parselatitude`` and ``parselongitude`` functions parse a text
(``varchar``) latitude or longitude coordinate, respectively, and return
its value in decimal degrees as a ``double``. The coordinate should be
in one of the following forms (optional parts in square brackets):

| [:math:`H`] :math:`nnn` [:math:`U`] [``:``] [:math:`H`] [:math:`nnn`
  [:math:`U`] [``:``] [:math:`nnn` [:math:`U`]]] [:math:`H`]
| :math:`DDMM`\ [:math:`.MMM`...]
| :math:`DDMMSS`\ [:math:`.SSS`...]

where the terms are:

-  | :math:`nnn`
   | A number (integer or decimal) with optional plus/minus sign. Only
     the first number may be negative, in which case it is a south
     latitude or west longitude. Note that this is true even for
     :math:`DDDMMSS` (DMS) longitudes – i.e. the ISO 6709 east-positive
     standard is followed, not the deprecated Texis/Vortex west-positive
     standard.

-  | :math:`U`
   | A unit (case-insensitive):

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

-  | :math:`H`
   | A hemisphere (case-insensitive):

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

-  | :math:`DD`
   | A two- or three-digit degree value, with optional sign. Note that
     longitudes are east-positive ala ISO 6709, not west-positive like
     the deprecated Texis standard.

-  | :math:`MM`
   | A two-digit minutes value, with leading zero if needed to make two
     digits.

-  | :math:`.MMM`...
   | A zero or more digit fractional minute value.

-  | :math:`SS`
   | A two-digit seconds value, with leading zero if needed to make two
     digits.

-  | :math:`.SSS`...
   | A zero or more digit fractional seconds value.

Whitespace is generally not required between terms in the first format.
A hemisphere token may only occur once. Degrees/minutes/seconds numbers
need not be in that order, if units are given after each number. If a
5-integer-digit :math:`DDDMM`\ [:math:`.MMM`...] format is given and the
degree value is out of range (e.g. more than 90 degrees latitude), it is
interpreted as a :math:`DMMSS`\ [:math:`.SSS`...] value instead. To
force :math:`DDDMMSS`\ [:math:`.SSS`...] for small numbers, pad with
leading zeros to 6 or 7 digits.

::

    insert into geotest(lat, lon)
      values(parselatitude('54d 40m 10"'),
             parselongitude('W90 10.2'));

An invalid or unparseable latitude or longitude value will return
``NaN`` (Not a Number). Extra unparsed/unparsable text may be allowed
(and ignored) after the coordinate in most instances. Out-of-range
values (e.g. latitudes greater than 90 degrees) are accepted; it is up
to the caller to bounds-check the result.
