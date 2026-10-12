The rampart-almanac module
==========================

Preface
-------

Acknowledgment
~~~~~~~~~~~~~~

The rampart-almanac module uses 
`astronomy.c library <https://github.com/cosinekitty/astronomy>`_
and the `date-holidays JavaScript module <https://github.com/commenthol/date-holidays>`_
The authors of Rampart extend our thanks to authors of these libraries.

License
~~~~~~~

The astronomy.c library is licensed under the
`MIT license <https://github.com/cosinekitty/astronomy/blob/master/LICENSE>`_\ .
The date-holidays Javascript module is licensed under an
`ISC license <https://github.com/commenthol/date-holidays/blob/master/LICENSE>`_
and includes other licenses therein.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-astronomy module calculates Sun, Moon and Planet rise, set and positions
given a date and time, and latitude and longitude, and seasons given a year.  It also includes
rampart-date-holidays.js which calculates holidays for a particular locale and year.

Loading and Using the Module
----------------------------

Loading
~~~~~~~

Loading the module is a simple matter of using the ``require()`` function:

.. code-block:: javascript

    var almanac = require("rampart-almanac");



Celestial Positions
-------------------

almanac.suntimes()
~~~~~~~~~~~~~~~~~~

Calculate sun position times for a given day, longitude and latitude.

Usage:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var times = almanac.suntimes(date, latitude, longitude);

Where:

* ``date`` is a JavaScript :green:`Date` or a :green:`String` (which is converted using
  :ref:`autoScanDate <rampart-utils:autoScanDate>`).

* ``latitude`` is a :green:`Number` (negative for south).

* ``longitude`` is a :green:`Number` (negative for west).

Return Value: An :green:`Object` with the relevant information.

Example:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var times = almanac.suntimes("2024-01-01 -0700", 37.77, -122.42);
    /* times = 
       {
           "daylightHours": 9.605555555555556,
           "civilTwilightHours": 10.580555555555556,
           "nauticalTwilightHours": 11.674444444444445,
           "astronomicalTwilightHours": 12.7375,
           "solarNoon": "2024-01-01T20:13:08.000Z",
           "sunrise": "2024-01-01T15:25:03.000Z",
           "sunset": "2024-01-02T01:01:23.000Z",
           "civilTwilightStart": "2024-01-01T14:55:47.000Z",
           "civilTwilightEnd": "2024-01-02T01:30:37.000Z",
           "nauticalTwilightStart": "2024-01-01T14:22:59.000Z",
           "nauticalTwilightEnd": "2024-01-02T02:03:27.000Z",
           "astronomicalTwilightStart": "2024-01-01T13:51:05.000Z",
           "astronomicalTwilightEnd": "2024-01-02T02:35:18.000Z",
           "sunriseAzimuth": 118.89538324193013,
           "sunsetAzimuth": 241.1489255497421
       }
    */
    /* get sunrise as a local time by using '%z' */
    var sunrise = rampart.utils.dateFmt("%c %z\n", times.sunrise);
    /* sunrise = "Mon 01 Jan 2024 07:25:04 AM PST -0800"
       (the ``%c`` portion is locale-dependent and will vary by system) */

almanac.moontimes()
~~~~~~~~~~~~~~~~~~~

Calculate the next moon rise and set times from the given date/time, for a
given longitude and latitude.  Also return dates of the next moon phases.

Usage:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var times = almanac.moontimes(date, latitude, longitude);

Where:

* ``date`` is a JavaScript :green:`Date` or a :green:`String` (which is converted using
  :ref:`autoScanDate <rampart-utils:autoScanDate>`).  The next moonrise and moonset
  occurring after this date/time are returned.  To get times for a specific day,
  pass local midnight (e.g. ``"2024-01-01 00:00:00 -0700"``).

* ``latitude`` is a :green:`Number` (negative for south).

* ``longitude`` is a :green:`Number` (negative for west).

Return Value: An :green:`Object` with the relevant information.

Example:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var times = almanac.moontimes("2024-01-01 00:00:00 -0700", 37.77, -122.42);
    /*  times =
        {
           "moonrise": "2024-01-02T06:37:25.000Z",
           "moonset": "2024-01-01T19:02:43.000Z",
           "newMoon": "2024-01-11T11:57:55.000Z",
           "firstQuarter": "2024-01-18T03:53:10.000Z",
           "fullMoon": "2024-01-25T17:54:42.000Z",
           "lastQuarter": "2024-01-04T03:31:09.000Z",
           "moonriseAzimuth": 82.64960233180054,
           "moonsetAzimuth": 280.67746349893633,
           "moonPhase": 0.6641758156156009,
           "moonIllumination": 0.7567459628710032
        }
    */
    /* Note: moonset may be before moonrise if the moon is
       already up at the given time. */

almanac.celestials()
~~~~~~~~~~~~~~~~~~~~

Calculate the next rise and set times for the sun, moon and planets from
the given date/time, for a given longitude and latitude.  Also return
current position data (right ascension, declination, azimuth and altitude).

Usage:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var times = almanac.celestials(date, latitude, longitude);

Where:

* ``date`` is a JavaScript :green:`Date` or a :green:`String` (which is converted using
  :ref:`autoScanDate <rampart-utils:autoScanDate>`).

* ``latitude`` is a :green:`Number` (negative for south).

* ``longitude`` is a :green:`Number` (negative for west).

Return Value: An :green:`Object` with the relevant information.

Example:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var times = almanac.celestials("2024-01-01", 37.77, -122.42);
    /*  times =
        {
           "sun": {
              "currentRightAscension": 18.70381774294387,
              "currentDeclination": -23.082972736894366,
              "currentAzimuth": 231.2447789386162,
              "currentAltitude": 9.200955835755252,
              "nextRise": "2024-01-01T15:25:03.000Z",
              "nextSet": "2024-01-01T01:00:35.000Z"
           },
           "moon": {
              "currentRightAscension": 10.585655630489418,
              "currentDeclination": 12.06431630189065,
              "currentAzimuth": 358.2427660196457,
              "currentAltitude": -39.911128386544675,
              "nextRise": "2024-01-01T05:39:21.000Z",
              "nextSet": "2024-01-01T19:02:43.000Z"
           },
           "mercury": {
              "currentRightAscension": 17.42857713438222,
              "currentDeclination": -20.134230004864982,
              "currentAzimuth": 245.65820797921364,
              "currentAltitude": -1.1285066854169088,
              "nextRise": "2024-01-01T13:56:36.000Z",
              "nextSet": "2024-01-01T23:48:43.000Z"
           },
           "venus": {
              "currentRightAscension": 16.04024593585745,
              "currentDeclination": -18.703696660954833,
              "currentAzimuth": 258.8170836590166,
              "currentAltitude": -16.054068589788287,
              "nextRise": "2024-01-01T12:31:52.000Z",
              "nextSet": "2024-01-01T22:35:04.000Z"
           },
           "mars": {
              "currentRightAscension": 17.77925369534947,
              "currentDeclination": -23.951999465047503,
              "currentAzimuth": 239.58535957726625,
              "currentAltitude": 0.013586371580359469,
              "nextRise": "2024-01-01T14:34:09.000Z",
              "nextSet": "2024-01-01T00:00:02.000Z"
           },
           "jupiter": {
              "currentRightAscension": 2.224206624688085,
              "currentDeclination": 12.150270854695547,
              "currentAzimuth": 101.66782757086347,
              "currentAltitude": 34.25711749414994,
              "nextRise": "2024-01-01T20:58:35.000Z",
              "nextSet": "2024-01-01T10:23:51.000Z"
           },
           "saturn": {
              "currentRightAscension": 22.363992244102082,
              "currentDeclination": -11.961562816580198,
              "currentAzimuth": 182.50892699669285,
              "currentAltitude": 40.37470253011248,
              "nextRise": "2024-01-01T18:23:45.000Z",
              "nextSet": "2024-01-01T05:16:58.000Z"
           },
           "uranus": {
              "currentRightAscension": 3.1122566235436486,
              "currentDeclination": 17.187443780343074,
              "currentAzimuth": 88.20667075806635,
              "currentAltitude": 26.70533930046701,
              "nextRise": "2024-01-01T21:34:38.000Z",
              "nextSet": "2024-01-01T11:34:04.000Z"
           },
           "neptune": {
              "currentRightAscension": 23.711146313700397,
              "currentDeclination": -3.221011807290628,
              "currentAzimuth": 153.3716388125294,
              "currentAltitude": 45.795412016162715,
              "nextRise": "2024-01-01T19:16:31.000Z",
              "nextSet": "2024-01-01T07:05:07.000Z"
           },
           "pluto": {
              "currentRightAscension": 20.118773598789403,
              "currentDeclination": -23.058689775023026,
              "currentAzimuth": 214.92988354299945,
              "currentAltitude": 20.68436892218172,
              "nextRise": "2024-01-01T16:48:25.000Z",
              "nextSet": "2024-01-01T02:23:45.000Z"
           }
        }
    */

almanac.seasons()
~~~~~~~~~~~~~~~~~

Calculate the start of seasons for a given year.

Usage:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var times = almanac.seasons(year);

Where:

* ``date`` is a :green:`Number`, the year to calculate.

Return Value: An :green:`Object` with dates for each season.

Example:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var seasons = almanac.seasons(2025);
    /*  seasons = 
        {
           "spring": "2025-03-20T09:01:27.000Z",
           "summer": "2025-06-21T02:42:18.000Z",
           "autumn": "2025-09-22T18:19:34.000Z",
           "winter": "2025-12-21T15:03:04.000Z"
        }
    */
    console.log( dateFmt("Happy Nowruz! %c %z", seasons.spring) );
        /* Happy Nowruz! Thu 20 Mar 2025 02:01:27 AM PDT -0700 */

Note that ``%c`` renders according to the current locale, so its exact
appearance will differ between systems; use an explicit format string such as
``"%Y-%m-%d %H:%M:%S %z"`` when a stable layout is needed.

Holidays
--------

new almanac.holidays()
~~~~~~~~~~~~~~~~~~~~~~

Create a new ``holidays`` object.  Note that the constructor name is
lowercase.

Usage:

.. code-block:: javascript

    var almanac = require("rampart-almanac");

    var hd = new almanac.holidays();

    /* or */

    var hd = new almanac.holidays(countryCode [, LocaleCode [, LocaleCode]]);

    hd.getHolidays(year);

More information, and this example (slightly modified) can be found `here <https://www.npmjs.com/package/date-holidays#Usage>`_\ :

.. code-block:: javascript

    var almanac = require('rampart-almanac');
    var Holidays = almanac.holidays;
    var printf = rampart.utils.printf;

    var hd = new Holidays();

    // get supported countries
    var res = hd.getCountries();
    printf("%3J\n", res);
    /*  res = 
        {
           "AD": "Andorra",
           "AE": "دولة الإمارات العربية المتحدة",
           "AG": "Antigua & Barbuda",
            ...
           "ZM": "Zambia",
           "ZW": "Zimbabwe"
        }
    */

    // get supported states e.g. for US
    var res = hd.getStates('US');
    printf("%3J\n", res);
    /*  res =
        {
           AL: 'Alabama',
           ...
           WY: 'Wyoming'
        }
    */

    // get supported regions e.g. for US, Louisiana
    res = hd.getRegions('US', 'LA');
    printf("%3J\n", res);
    /*  res = 
        {
           "NO": "New Orleans"
        }
    */

    // initialize holidays for US, Louisiana, New Orleans
    hd.init('US', 'LA', 'NO');

    /* or using a new instance */
    //hd = new Holidays('US', 'la', 'no');

    // get all holidays for the year 2016
    res = hd.getHolidays(2016);
    printf("%3J\n", res);
    /*  res = 
        {
           "date": "2016-01-01 00:00:00",
           "start": "2016-01-01T06:00:00.000Z",
           "end": "2016-01-02T06:00:00.000Z",
           "name": "New Year's Day",
           "type": "public",
           "rule": "01-01 and if sunday then next monday if saturday then previous friday"
        },
        {
           "date": "2016-01-18 00:00:00",
           "start": "2016-01-18T06:00:00.000Z",
           "end": "2016-01-19T06:00:00.000Z",
           "name": "Martin Luther King Jr. Day",
           "type": "public",
           "rule": "3rd monday in January"
        },
        ...
        {
           "date": "2016-12-31 00:00:00",
           "start": "2016-12-31T06:00:00.000Z",
           "end": "2017-01-01T06:00:00.000Z",
           "name": "New Year's Eve",
           "type": "observance",
           "rule": "12-31"
        }
    */

    // check if date is a holiday while respecting timezones
    res = hd.isHoliday(new Date('2016-02-09 00:00:00'));
    printf("%3J\n", res);
    // res = false

    res = hd.isHoliday(rampart.utils.autoScanDate('2016-02-09 00:00:00 -0600').date);
    printf("%3J\n", res);
    /*  res =
        [
           {
              "date": "2016-02-09 00:00:00",
              "start": "2016-02-09T06:00:00.000Z",
              "end": "2016-02-10T06:00:00.000Z",
              "name": "Mardi Gras",
              "type": "public"
           }
        ]
    */

Weather
-------

The weather sub-module provides access to weather data from
`Open-Meteo <https://open-meteo.com/>`_\ , a free weather API for
non-commercial use.  Commercial use requires a paid API subscription.
It is available as ``almanac.weather`` and includes geocoding, current
conditions, forecasts, historical data, air quality and marine weather.
Results are cached in an LMDB database to minimize API calls.

Weather data from Open-Meteo is licensed under
`CC BY 4.0 <https://creativecommons.org/licenses/by/4.0/>`_\ , which
requires attribution.  Any application that displays Open-Meteo data
should include an attribution notice such as:

    Weather data by `Open-Meteo.com <https://open-meteo.com/>`_
    (`CC BY 4.0 <https://creativecommons.org/licenses/by/4.0/>`_)

almanac.weather.configure()
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Configure the weather module.

Usage:

.. code-block:: javascript

    var almanac = require("rampart-almanac");
    var weather = almanac.weather;

    weather.configure(options);

Where ``options`` is an :green:`Object` which may contain any of the following:

* ``lmdbCache`` - :green:`String` or ``null``.  Path to the LMDB cache
  directory.  If not set, defaults to ``~/.rampart/open-meteo-cache`` when
  a home directory is available, otherwise falling back to a writable
  temporary directory (``$TMPDIR``, ``/tmp`` or ``/var/tmp``).  If set to
  ``null``, caching is disabled entirely.

* ``units`` - :green:`String`.  Set to ``"imperial"`` for Fahrenheit, mph
  and inches, or ``"metric"`` for Celsius, km/h and mm.

* ``temperature_unit`` - :green:`String`.  ``"celsius"`` (default) or
  ``"fahrenheit"``.

* ``wind_speed_unit`` - :green:`String`.  ``"kmh"`` (default), ``"mph"``,
  ``"ms"`` or ``"kn"``.

* ``precipitation_unit`` - :green:`String`.  ``"mm"`` (default) or
  ``"inch"``.

* ``timezone`` - :green:`String`.  IANA timezone or ``"auto"`` (default).

* ``maxTime`` - :green:`Number`.  Maximum seconds per HTTP request
  (default ``10``).

* ``retries`` - :green:`Number`.  Number of retries on failure (default
  ``2``).

Example:

.. code-block:: javascript

    weather.configure({ units: 'imperial', lmdbCache: '/var/data/weather-cache' });

almanac.weather.search()
~~~~~~~~~~~~~~~~~~~~~~~~

Search for a place by name using the Open-Meteo geocoding API.

Usage:

.. code-block:: javascript

    var results = weather.search(name [, options]);

    /* async version */
    weather.searchAsync(name [, options], callback);

Where:

* ``name`` is a :green:`String`, the place name to search for.

* ``options`` is an optional :green:`Object` with:

  * ``count`` - :green:`Number` (default ``10``).  Maximum results.
  * ``language`` - :green:`String`.  ISO-639 language code.
  * ``countryCode`` - :green:`String`.  ISO-3166-1 alpha-2 country code filter.

Return Value: An :green:`Array` of :green:`Objects`, each containing
``name``, ``latitude``, ``longitude``, ``country``, ``country_code``,
``admin1``, ``timezone``, ``population`` and ``elevation``.

Results are cached for 30 days.

Example:

.. code-block:: javascript

    var places = weather.search('San Francisco');
    printf("%s: %.4f, %.4f\n", places[0].name, places[0].latitude, places[0].longitude);
    /* San Francisco: 37.7749, -122.4194 */

almanac.weather.current()
~~~~~~~~~~~~~~~~~~~~~~~~~

Get current weather conditions for a location.

Usage:

.. code-block:: javascript

    var res = weather.current(latitude, longitude [, options]);

    /* async version */
    weather.currentAsync(latitude, longitude [, options], callback);

Where:

* ``latitude`` is a :green:`Number`.

* ``longitude`` is a :green:`Number`.

* ``options`` is an optional :green:`Object` with:

  * ``variables`` - :green:`Array` of :green:`Strings`.  Variables to request.
    Default:

    .. code-block:: javascript

        ['temperature_2m', 'relative_humidity_2m', 'apparent_temperature',
         'weather_code', 'wind_speed_10m', 'wind_direction_10m',
         'precipitation', 'cloud_cover', 'cloud_cover_low',
         'cloud_cover_mid', 'cloud_cover_high', 'surface_pressure']

  * Unit overrides (see `almanac.weather.configure()`_\ ).

Return Value: An :green:`Object` containing ``current`` (with the
requested variables), ``current_units`` and a
``weather_code_description`` :green:`String`.

Results are cached for 15 minutes.

Example:

.. code-block:: javascript

    var res = weather.current(37.77, -122.42);
    printf("Temperature: %s %s\n", res.current.temperature_2m, res.current_units.temperature_2m);
    printf("Conditions: %s\n", res.current.weather_code_description);
    /* Temperature: 18.5 °C */
    /* Conditions: Partly cloudy */

almanac.weather.forecast()
~~~~~~~~~~~~~~~~~~~~~~~~~~

Get weather forecast (current conditions, hourly and daily data).

Usage:

.. code-block:: javascript

    var res = weather.forecast(latitude, longitude [, options]);

    /* async version */
    weather.forecastAsync(latitude, longitude [, options], callback);

Where:

* ``latitude`` is a :green:`Number`.

* ``longitude`` is a :green:`Number`.

* ``options`` is an optional :green:`Object` with:

  * ``days`` - :green:`Number` (default ``7``, max ``16``).  Forecast days.

  * ``hourly`` - :green:`Array` of :green:`Strings` or ``false``.
    Hourly variables to request.  Set to ``false`` to omit hourly data.
    Default:

    .. code-block:: javascript

        ['temperature_2m', 'precipitation', 'relative_humidity_2m',
         'wind_speed_10m', 'cloud_cover', 'weather_code']

  * ``daily`` - :green:`Array` of :green:`Strings` or ``false``.
    Daily variables to request.  Set to ``false`` to omit daily data.
    Default:

    .. code-block:: javascript

        ['temperature_2m_max', 'temperature_2m_min', 'precipitation_sum',
         'sunrise', 'sunset', 'wind_speed_10m_max', 'weather_code']

  * ``current`` - :green:`Array` of :green:`Strings` or ``false``.
    Current condition variables.  Set to ``false`` to omit.
    Default:

    .. code-block:: javascript

        ['temperature_2m', 'relative_humidity_2m', 'apparent_temperature',
         'weather_code', 'wind_speed_10m', 'wind_direction_10m',
         'precipitation', 'cloud_cover', 'cloud_cover_low',
         'cloud_cover_mid', 'cloud_cover_high', 'surface_pressure']

  * ``past_days`` - :green:`Number` (``0``-``92``).  Include past days.
  * ``timezone`` - :green:`String`.  IANA timezone or ``"auto"``.
  * Unit overrides (see `almanac.weather.configure()`_\ ).

  The full list of available variables can be found in the
  `Open-Meteo forecast API documentation <https://open-meteo.com/en/docs>`_\ .

Return Value: An :green:`Object` containing ``current``, ``hourly``,
``daily``, their corresponding ``*_units`` :green:`Objects`, and
``weather_code_description`` :green:`Arrays` for weather codes.

Results are cached for 1 hour.

Example:

.. code-block:: javascript

    var res = weather.forecast(37.77, -122.42, { days: 3 });
    for (var i = 0; i < res.daily.time.length; i++) {
        printf("%s: %s/%s %s\n",
            res.daily.time[i],
            res.daily.temperature_2m_max[i],
            res.daily.temperature_2m_min[i],
            res.daily.weather_code_description[i]);
    }

almanac.weather.history()
~~~~~~~~~~~~~~~~~~~~~~~~~

Get historical weather data (from 1940 to present).

Usage:

.. code-block:: javascript

    var res = weather.history(latitude, longitude, startDate, endDate [, options]);

    /* async version */
    weather.historyAsync(latitude, longitude, startDate, endDate [, options], callback);

Where:

* ``latitude`` is a :green:`Number`.

* ``longitude`` is a :green:`Number`.

* ``startDate`` is a :green:`String` (parsed by
  :ref:`autoScanDate <rampart-utils:autoScanDate>`), a JavaScript
  :green:`Date`, or a :green:`Number` (epoch seconds).

* ``endDate`` is a :green:`String` (parsed by
  :ref:`autoScanDate <rampart-utils:autoScanDate>`), a JavaScript
  :green:`Date`, or a :green:`Number` (epoch seconds).

* ``options`` is an optional :green:`Object` with ``hourly``, ``daily``,
  ``timezone`` and unit overrides.

Return Value: Same shape as `almanac.weather.forecast()`_\ .

Results are cached for 7 days.

Example:

.. code-block:: javascript

    var res = weather.history(37.77, -122.42, '2024-01-01', '2024-01-07');
    printf("High on Jan 1: %s\n", res.daily.temperature_2m_max[0]);

almanac.weather.airQuality()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Get air quality data.

Usage:

.. code-block:: javascript

    var res = weather.airQuality(latitude, longitude [, options]);

    /* async version */
    weather.airQualityAsync(latitude, longitude [, options], callback);

Where:

* ``latitude`` is a :green:`Number`.

* ``longitude`` is a :green:`Number`.

* ``options`` is an optional :green:`Object` with:

  * ``hourly`` - :green:`Array` of :green:`Strings`.  Default:

    .. code-block:: javascript

        ['pm10', 'pm2_5', 'us_aqi', 'european_aqi', 'uv_index']

    See the `Open-Meteo air quality API documentation
    <https://open-meteo.com/en/docs/air-quality-api>`_ for all available
    variables.

  * ``forecast_days`` - :green:`Number`.
  * ``past_days`` - :green:`Number`.

Return Value: An :green:`Object` containing ``hourly`` data with time,
pollutant concentrations and air quality indices.

Results are cached for 1 hour.

Example:

.. code-block:: javascript

    var res = weather.airQuality(37.77, -122.42);
    printf("PM2.5: %s\n", res.hourly.pm2_5[0]);
    printf("US AQI: %s\n", res.hourly.us_aqi[0]);

almanac.weather.marine()
~~~~~~~~~~~~~~~~~~~~~~~~

Get marine/ocean weather data.

Usage:

.. code-block:: javascript

    var res = weather.marine(latitude, longitude [, options]);

    /* async version */
    weather.marineAsync(latitude, longitude [, options], callback);

Where:

* ``latitude`` is a :green:`Number`.

* ``longitude`` is a :green:`Number`.

* ``options`` is an optional :green:`Object` with:

  * ``hourly`` - :green:`Array` of :green:`Strings`.  Default:

    .. code-block:: javascript

        ['wave_height', 'wave_direction', 'wave_period',
         'sea_surface_temperature']

  * ``daily`` - :green:`Array` of :green:`Strings`.  Default:

    .. code-block:: javascript

        ['wave_height_max', 'wave_direction_dominant', 'wave_period_max']

  * ``forecast_days`` - :green:`Number`.
  * ``past_days`` - :green:`Number`.

  See the `Open-Meteo marine API documentation
  <https://open-meteo.com/en/docs/marine-weather-api>`_ for all available
  variables.

Return Value: An :green:`Object` containing ``hourly`` and ``daily`` data
with the requested variables.

Results are cached for 1 hour.

Example:

.. code-block:: javascript

    var res = weather.marine(36.95, -122.02);
    printf("Wave height: %s m\n", res.hourly.wave_height[0]);

almanac.weather.weatherAt()
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Convenience function: geocode a place name and fetch the full forecast
in a single call.

Usage:

.. code-block:: javascript

    var res = weather.weatherAt(placeName [, options]);

    /* async version */
    weather.weatherAtAsync(placeName [, options], callback);

Where:

* ``placeName`` is a :green:`String`.  Supports ``"City"``,
  ``"City, CC"`` (two-letter country code) and common aliases like
  ``"UK"`` for ``"GB"`` and ``"USA"`` for ``"US"``.

* ``options`` is an optional :green:`Object` with the same options as
  `almanac.weather.forecast()`_\ .

Return Value: An :green:`Object` containing ``location`` (with name,
country, latitude, longitude, timezone), ``current``, ``hourly``,
``daily`` and their units.

Example:

.. code-block:: javascript

    var res = weather.weatherAt('London, UK');
    printf("%s, %s: %s°C %s\n",
        res.location.name, res.location.country,
        res.current.temperature_2m,
        res.current.weather_code_description);

almanac.weather.clearCache()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Delete the LMDB cache database.  The cache will be recreated
automatically on the next API call.

Usage:

.. code-block:: javascript

    weather.clearCache();

almanac.weather.weatherCodes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An :green:`Object` mapping WMO weather codes to human-readable
descriptions.

.. code-block:: javascript

    printf("%s\n", weather.weatherCodes[0]);   /* "Clear sky" */
    printf("%s\n", weather.weatherCodes[61]);  /* "Rain: slight" */
    printf("%s\n", weather.weatherCodes[95]);  /* "Thunderstorm: slight or moderate" */

Async Callbacks
~~~~~~~~~~~~~~~

All async methods (``searchAsync``, ``currentAsync``, ``forecastAsync``,
``historyAsync``, ``airQualityAsync``, ``marineAsync``,
``weatherAtAsync``) take a callback as the last argument with the
signature ``function(error, data)``.

.. code-block:: javascript

    weather.weatherAtAsync('Tokyo', function(err, data) {
        if (err) {
            printf("Error: %s\n", err);
            return;
        }
        printf("Temperature in %s: %s°C\n",
            data.location.name, data.current.temperature_2m);
    });
