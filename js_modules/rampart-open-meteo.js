/*
    rampart-open-meteo.js - Weather data module using Open-Meteo API

    https://open-meteo.com/

    Open-Meteo is free for non-commercial use.
    Attribution: "Weather data by Open-Meteo.com"

    License: MIT (this module)

    Geocoding data from GeoNames (geonames.org)
    Creative Commons Attribution 4.0
*/

var curl = require('rampart-curl');
var lmdb = require('rampart-lmdb');

/* ===================================================================
   WMO Weather Code descriptions
   =================================================================== */
var weatherCodes = {
    0:  "Clear sky",
    1:  "Mainly clear",
    2:  "Partly cloudy",
    3:  "Overcast",
    45: "Fog",
    48: "Depositing rime fog",
    51: "Drizzle: light",
    53: "Drizzle: moderate",
    55: "Drizzle: dense",
    56: "Freezing drizzle: light",
    57: "Freezing drizzle: dense",
    61: "Rain: slight",
    63: "Rain: moderate",
    65: "Rain: heavy",
    66: "Freezing rain: light",
    67: "Freezing rain: heavy",
    71: "Snow fall: slight",
    73: "Snow fall: moderate",
    75: "Snow fall: heavy",
    77: "Snow grains",
    80: "Rain showers: slight",
    81: "Rain showers: moderate",
    82: "Rain showers: violent",
    85: "Snow showers: slight",
    86: "Snow showers: heavy",
    95: "Thunderstorm: slight or moderate",
    96: "Thunderstorm with slight hail",
    99: "Thunderstorm with heavy hail"
};

/* ===================================================================
   Configuration
   =================================================================== */
var _config = {
    cacheDir: null, // set on first use
    temperature_unit: 'celsius',
    wind_speed_unit: 'kmh',
    precipitation_unit: 'mm',
    timezone: 'auto',
    maxTime: 10,     // max seconds per request
    retries: 2       // number of retries on failure
};

// Cache TTLs in milliseconds
var TTL = {
    geocoding:  30 * 24 * 3600 * 1000,  // 30 days
    current:    15 * 60 * 1000,          // 15 minutes
    forecast:   60 * 60 * 1000,          // 1 hour
    historical: 7 * 24 * 3600 * 1000,   // 7 days
    airQuality: 60 * 60 * 1000,          // 1 hour
    marine:     60 * 60 * 1000           // 1 hour
};

var _db = null;
var _cacheDisabled = false;
var _cacheInitialized = false;

function getDb() {
    if (_cacheDisabled) return null;
    if (_db) return _db;
    if (_cacheInitialized) return null; // init was attempted and failed

    _cacheInitialized = true;

    var dir = _config.cacheDir;
    if (!dir) {
        // auto-detect a writable tmp directory
        var candidates = ['/tmp', '/var/tmp'];
        if (typeof process !== 'undefined' && process.env && process.env.TMPDIR)
            candidates.unshift(process.env.TMPDIR);
        for (var i = 0; i < candidates.length; i++) {
            try {
                var st = rampart.utils.stat(candidates[i]);
                if (st && st.isDirectory) {
                    dir = candidates[i] + '/rampart-open-meteo-cache';
                    break;
                }
            } catch(e) {}
        }
        if (!dir) return null; // no writable tmp found
    }

    try {
        var st = rampart.utils.stat(dir);
        if (!st) rampart.utils.mkdir(dir);
        _db = new lmdb.init(dir, true);
        _config.cacheDir = dir;
    } catch(e) {
        // silently disable caching if lmdb init fails
        _db = null;
    }
    return _db;
}

function cacheKey(prefix, params) {
    var keys = Object.keys(params).sort();
    var parts = [prefix];
    for (var i = 0; i < keys.length; i++)
        parts.push(keys[i] + '=' + params[keys[i]]);
    return parts.join('|');
}

function cacheGet(key, ttl) {
    var db = getDb();
    if (!db) return null;
    try {
        var raw = db.get(null, key);
        if (raw !== undefined && raw !== null) {
            var entry = JSON.parse(raw);
            if (Date.now() - entry._cachedAt < ttl)
                return entry.data;
        }
    } catch(e) {}
    return null;
}

function cachePut(key, data) {
    var db = getDb();
    if (!db) return;
    try {
        db.put(null, key, JSON.stringify({ _cachedAt: Date.now(), data: data }));
    } catch(e) {}
}

/* ===================================================================
   Common country code aliases
   =================================================================== */
var countryAliases = {
    'UK': 'GB', 'EN': 'GB', 'ENGLAND': 'GB',
    'SCOTLAND': 'GB', 'WALES': 'GB',
    'USA': 'US', 'AMERICA': 'US'
};

/* ===================================================================
   HTTP helpers
   =================================================================== */
var BASE_FORECAST  = 'https://api.open-meteo.com/v1/forecast';
var BASE_ARCHIVE   = 'https://archive-api.open-meteo.com/v1/archive';
var BASE_AIR       = 'https://air-quality-api.open-meteo.com/v1/air-quality';
var BASE_MARINE    = 'https://marine-api.open-meteo.com/v1/marine';
var BASE_GEOCODING = 'https://geocoding-api.open-meteo.com/v1/search';

function buildUrl(base, params) {
    var parts = [];
    var keys = Object.keys(params);
    for (var i = 0; i < keys.length; i++) {
        var v = params[keys[i]];
        if (v !== undefined && v !== null && v !== '')
            parts.push(encodeURIComponent(keys[i]) + '=' + encodeURIComponent(v));
    }
    return base + '?' + parts.join('&');
}

function parseResponse(res) {
    if (!res || res.status !== 200) {
        var msg = 'HTTP error';
        if (res && res.status) msg += ' ' + res.status;
        if (res && res.body) {
            try {
                var err = JSON.parse(res.body);
                if (err.reason) msg += ': ' + err.reason;
            } catch(e) {
                msg += ': ' + ('' + res.body).substring(0, 200);
            }
        }
        return { error: msg };
    }
    try {
        return JSON.parse(res.body);
    } catch(e) {
        return { error: 'Invalid JSON response' };
    }
}

function doFetch(url) {
    var opts = { 'max-time': _config.maxTime };
    var lastErr;
    for (var attempt = 0; attempt <= _config.retries; attempt++) {
        var res = curl.fetch(url, opts);
        if (Array.isArray(res)) res = res[0];
        var data = parseResponse(res);
        if (!data.error) return data;
        lastErr = data;
        if (attempt < _config.retries)
            rampart.utils.sleep(0.5); // brief pause before retry
    }
    return lastErr;
}

function doFetchAsync(url, callback) {
    var opts = { 'max-time': _config.maxTime };
    var attempts = 0;
    function tryFetch() {
        curl.fetch(url, opts, function(res) {
            var data = parseResponse(res);
            if (!data.error || attempts >= _config.retries)
            {
                if (data.error)
                    callback(data.error, null);
                else
                    callback(null, data);
                return;
            }
            attempts++;
            setTimeout(tryFetch, 500);
        });
    }
    tryFetch();
}

/* ===================================================================
   Default variable sets
   =================================================================== */
var DEFAULTS = {
    current: [
        'temperature_2m', 'relative_humidity_2m', 'apparent_temperature',
        'weather_code', 'wind_speed_10m', 'wind_direction_10m',
        'precipitation', 'cloud_cover', 'surface_pressure'
    ],
    forecast_hourly: [
        'temperature_2m', 'precipitation', 'relative_humidity_2m',
        'wind_speed_10m', 'cloud_cover', 'weather_code'
    ],
    forecast_daily: [
        'temperature_2m_max', 'temperature_2m_min', 'precipitation_sum',
        'sunrise', 'sunset', 'wind_speed_10m_max', 'weather_code'
    ],
    air_hourly: [
        'pm10', 'pm2_5', 'us_aqi', 'european_aqi', 'uv_index'
    ],
    marine_hourly: [
        'wave_height', 'wave_direction', 'wave_period',
        'sea_surface_temperature'
    ],
    marine_daily: [
        'wave_height_max', 'wave_direction_dominant', 'wave_period_max'
    ]
};

/* ===================================================================
   Unit parameters
   =================================================================== */
function unitParams(opts) {
    var p = {};
    p.temperature_unit = (opts && opts.temperature_unit) || _config.temperature_unit;
    p.wind_speed_unit = (opts && opts.wind_speed_unit) || _config.wind_speed_unit;
    p.precipitation_unit = (opts && opts.precipitation_unit) || _config.precipitation_unit;
    return p;
}

/* ===================================================================
   Add weather code descriptions to response
   =================================================================== */
function addWeatherDescriptions(data) {
    if (!data || data.error) return data;
    function annotate(obj, key) {
        if (obj && obj[key]) {
            obj[key + '_description'] = [];
            for (var i = 0; i < obj[key].length; i++) {
                var code = obj[key][i];
                obj[key + '_description'].push(weatherCodes[code] || 'Unknown');
            }
        }
    }
    if (data.current && data.current.weather_code !== undefined)
        data.current.weather_code_description = weatherCodes[data.current.weather_code] || 'Unknown';
    annotate(data.hourly, 'weather_code');
    annotate(data.daily, 'weather_code');
    return data;
}

/* ===================================================================
   PUBLIC API
   =================================================================== */

function configure(opts) {
    if ('lmdbCache' in opts) {
        if (opts.lmdbCache === null) {
            _cacheDisabled = true;
            _db = null;
        } else {
            _cacheDisabled = false;
            _config.cacheDir = opts.lmdbCache;
            _db = null;
            _cacheInitialized = false;
        }
    }
    if (opts.temperature_unit) _config.temperature_unit = opts.temperature_unit;
    if (opts.wind_speed_unit) _config.wind_speed_unit = opts.wind_speed_unit;
    if (opts.precipitation_unit) _config.precipitation_unit = opts.precipitation_unit;
    if (opts.timezone) _config.timezone = opts.timezone;
    if (typeof opts.maxTime === 'number') _config.maxTime = opts.maxTime;
    if (typeof opts.retries === 'number') _config.retries = opts.retries;
    if (opts.units === 'imperial') {
        _config.temperature_unit = 'fahrenheit';
        _config.wind_speed_unit = 'mph';
        _config.precipitation_unit = 'inch';
    }
    if (opts.units === 'metric') {
        _config.temperature_unit = 'celsius';
        _config.wind_speed_unit = 'kmh';
        _config.precipitation_unit = 'mm';
    }
}

/* --- Geocoding --- */

function search(name, opts) {
    var params = { name: name, count: (opts && opts.count) || 10, format: 'json' };
    if (opts && opts.language) params.language = opts.language;
    if (opts && opts.countryCode) params.countryCode = opts.countryCode;

    var key = cacheKey('geo', params);
    var cached = cacheGet(key, TTL.geocoding);
    if (cached) return cached;

    var url = buildUrl(BASE_GEOCODING, params);
    var data = doFetch(url);
    if (data.error) return data;

    var results = data.results || [];
    cachePut(key, results);
    return results;
}

function searchAsync(name, opts, callback) {
    if (typeof opts === 'function') { callback = opts; opts = {}; }
    var params = { name: name, count: (opts && opts.count) || 10, format: 'json' };
    if (opts && opts.language) params.language = opts.language;
    if (opts && opts.countryCode) params.countryCode = opts.countryCode;

    var key = cacheKey('geo', params);
    var cached = cacheGet(key, TTL.geocoding);
    if (cached) return callback(null, cached);

    var url = buildUrl(BASE_GEOCODING, params);
    doFetchAsync(url, function(err, data) {
        if (err) return callback(err, null);
        var results = data.results || [];
        cachePut(key, results);
        callback(null, results);
    });
}

/* --- Current Weather --- */

function current(lat, lon, opts) {
    opts = opts || {};
    var vars = opts.variables || DEFAULTS.current;
    var params = {
        latitude: lat, longitude: lon,
        current: vars.join(','),
        timezone: opts.timezone || _config.timezone
    };
    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;
    params.wind_speed_unit = up.wind_speed_unit;
    params.precipitation_unit = up.precipitation_unit;

    var key = cacheKey('cur', params);
    var cached = cacheGet(key, TTL.current);
    if (cached) return cached;

    var url = buildUrl(BASE_FORECAST, params);
    var data = doFetch(url);
    if (!data.error) {
        addWeatherDescriptions(data);
        cachePut(key, data);
    }
    return data;
}

function currentAsync(lat, lon, opts, callback) {
    if (typeof opts === 'function') { callback = opts; opts = {}; }
    opts = opts || {};
    var vars = opts.variables || DEFAULTS.current;
    var params = {
        latitude: lat, longitude: lon,
        current: vars.join(','),
        timezone: opts.timezone || _config.timezone
    };
    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;
    params.wind_speed_unit = up.wind_speed_unit;
    params.precipitation_unit = up.precipitation_unit;

    var key = cacheKey('cur', params);
    var cached = cacheGet(key, TTL.current);
    if (cached) return callback(null, cached);

    var url = buildUrl(BASE_FORECAST, params);
    doFetchAsync(url, function(err, data) {
        if (err) return callback(err, null);
        addWeatherDescriptions(data);
        cachePut(key, data);
        callback(null, data);
    });
}

/* --- Forecast --- */

function forecast(lat, lon, opts) {
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        forecast_days: opts.days || 7,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly !== false) {
        if (opts.hourly)
            params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
        else
            params.hourly = DEFAULTS.forecast_hourly.join(',');
    }

    if (opts.daily !== false) {
        if (opts.daily)
            params.daily = Array.isArray(opts.daily) ? opts.daily.join(',') : opts.daily;
        else
            params.daily = DEFAULTS.forecast_daily.join(',');
    }

    if (opts.current !== false) {
        if (opts.current)
            params.current = Array.isArray(opts.current) ? opts.current.join(',') : opts.current;
        else
            params.current = DEFAULTS.current.join(',');
    }

    if (opts.past_days) params.past_days = opts.past_days;
    if (opts.models) params.models = opts.models;

    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;
    params.wind_speed_unit = up.wind_speed_unit;
    params.precipitation_unit = up.precipitation_unit;

    var key = cacheKey('fc', params);
    var cached = cacheGet(key, TTL.forecast);
    if (cached) return cached;

    var url = buildUrl(BASE_FORECAST, params);
    var data = doFetch(url);
    if (!data.error) {
        addWeatherDescriptions(data);
        cachePut(key, data);
    }
    return data;
}

function forecastAsync(lat, lon, opts, callback) {
    if (typeof opts === 'function') { callback = opts; opts = {}; }
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        forecast_days: opts.days || 7,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly !== false) {
        if (opts.hourly)
            params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
        else
            params.hourly = DEFAULTS.forecast_hourly.join(',');
    }
    if (opts.daily !== false) {
        if (opts.daily)
            params.daily = Array.isArray(opts.daily) ? opts.daily.join(',') : opts.daily;
        else
            params.daily = DEFAULTS.forecast_daily.join(',');
    }
    if (opts.current !== false) {
        if (opts.current)
            params.current = Array.isArray(opts.current) ? opts.current.join(',') : opts.current;
        else
            params.current = DEFAULTS.current.join(',');
    }
    if (opts.past_days) params.past_days = opts.past_days;
    if (opts.models) params.models = opts.models;
    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;
    params.wind_speed_unit = up.wind_speed_unit;
    params.precipitation_unit = up.precipitation_unit;

    var key = cacheKey('fc', params);
    var cached = cacheGet(key, TTL.forecast);
    if (cached) return callback(null, cached);

    var url = buildUrl(BASE_FORECAST, params);
    doFetchAsync(url, function(err, data) {
        if (err) return callback(err, null);
        addWeatherDescriptions(data);
        cachePut(key, data);
        callback(null, data);
    });
}

/* --- Historical Weather --- */

function history(lat, lon, startDate, endDate, opts) {
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        start_date: startDate,
        end_date: endDate,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly)
        params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
    else
        params.hourly = DEFAULTS.forecast_hourly.join(',');
    if (opts.daily)
        params.daily = Array.isArray(opts.daily) ? opts.daily.join(',') : opts.daily;
    else
        params.daily = DEFAULTS.forecast_daily.join(',');

    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;
    params.wind_speed_unit = up.wind_speed_unit;
    params.precipitation_unit = up.precipitation_unit;

    var key = cacheKey('hist', params);
    var cached = cacheGet(key, TTL.historical);
    if (cached) return cached;

    var url = buildUrl(BASE_ARCHIVE, params);
    var data = doFetch(url);
    if (!data.error) {
        addWeatherDescriptions(data);
        cachePut(key, data);
    }
    return data;
}

function historyAsync(lat, lon, startDate, endDate, opts, callback) {
    if (typeof opts === 'function') { callback = opts; opts = {}; }
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        start_date: startDate,
        end_date: endDate,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly)
        params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
    else
        params.hourly = DEFAULTS.forecast_hourly.join(',');
    if (opts.daily)
        params.daily = Array.isArray(opts.daily) ? opts.daily.join(',') : opts.daily;
    else
        params.daily = DEFAULTS.forecast_daily.join(',');
    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;
    params.wind_speed_unit = up.wind_speed_unit;
    params.precipitation_unit = up.precipitation_unit;

    var key = cacheKey('hist', params);
    var cached = cacheGet(key, TTL.historical);
    if (cached) return callback(null, cached);

    var url = buildUrl(BASE_ARCHIVE, params);
    doFetchAsync(url, function(err, data) {
        if (err) return callback(err, null);
        addWeatherDescriptions(data);
        cachePut(key, data);
        callback(null, data);
    });
}

/* --- Air Quality --- */

function airQuality(lat, lon, opts) {
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly)
        params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
    else
        params.hourly = DEFAULTS.air_hourly.join(',');
    if (opts.current)
        params.current = Array.isArray(opts.current) ? opts.current.join(',') : opts.current;
    if (opts.forecast_days) params.forecast_days = opts.forecast_days;
    if (opts.past_days) params.past_days = opts.past_days;

    var key = cacheKey('air', params);
    var cached = cacheGet(key, TTL.airQuality);
    if (cached) return cached;

    var url = buildUrl(BASE_AIR, params);
    var data = doFetch(url);
    if (!data.error) cachePut(key, data);
    return data;
}

function airQualityAsync(lat, lon, opts, callback) {
    if (typeof opts === 'function') { callback = opts; opts = {}; }
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly)
        params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
    else
        params.hourly = DEFAULTS.air_hourly.join(',');
    if (opts.current)
        params.current = Array.isArray(opts.current) ? opts.current.join(',') : opts.current;
    if (opts.forecast_days) params.forecast_days = opts.forecast_days;
    if (opts.past_days) params.past_days = opts.past_days;

    var key = cacheKey('air', params);
    var cached = cacheGet(key, TTL.airQuality);
    if (cached) return callback(null, cached);

    var url = buildUrl(BASE_AIR, params);
    doFetchAsync(url, function(err, data) {
        if (err) return callback(err, null);
        cachePut(key, data);
        callback(null, data);
    });
}

/* --- Marine Weather --- */

function marine(lat, lon, opts) {
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly)
        params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
    else
        params.hourly = DEFAULTS.marine_hourly.join(',');
    if (opts.daily)
        params.daily = Array.isArray(opts.daily) ? opts.daily.join(',') : opts.daily;
    else
        params.daily = DEFAULTS.marine_daily.join(',');
    if (opts.forecast_days) params.forecast_days = opts.forecast_days;
    if (opts.past_days) params.past_days = opts.past_days;

    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;

    var key = cacheKey('mar', params);
    var cached = cacheGet(key, TTL.marine);
    if (cached) return cached;

    var url = buildUrl(BASE_MARINE, params);
    var data = doFetch(url);
    if (!data.error) cachePut(key, data);
    return data;
}

function marineAsync(lat, lon, opts, callback) {
    if (typeof opts === 'function') { callback = opts; opts = {}; }
    opts = opts || {};
    var params = {
        latitude: lat, longitude: lon,
        timezone: opts.timezone || _config.timezone
    };
    if (opts.hourly)
        params.hourly = Array.isArray(opts.hourly) ? opts.hourly.join(',') : opts.hourly;
    else
        params.hourly = DEFAULTS.marine_hourly.join(',');
    if (opts.daily)
        params.daily = Array.isArray(opts.daily) ? opts.daily.join(',') : opts.daily;
    else
        params.daily = DEFAULTS.marine_daily.join(',');
    if (opts.forecast_days) params.forecast_days = opts.forecast_days;
    if (opts.past_days) params.past_days = opts.past_days;
    var up = unitParams(opts);
    params.temperature_unit = up.temperature_unit;

    var key = cacheKey('mar', params);
    var cached = cacheGet(key, TTL.marine);
    if (cached) return callback(null, cached);

    var url = buildUrl(BASE_MARINE, params);
    doFetchAsync(url, function(err, data) {
        if (err) return callback(err, null);
        cachePut(key, data);
        callback(null, data);
    });
}

/* --- Convenience: weather by place name --- */

function weatherAt(placeName, opts) {
    opts = opts || {};
    var searchOpts = { count: 5, countryCode: opts.countryCode };
    // Parse "City, Country" or "City, State, Country" patterns
    var parts = placeName.split(/\s*,\s*/);
    var searchName = parts[0];
    if (parts.length >= 2) {
        var cc = parts[parts.length-1].toUpperCase();
        cc = countryAliases[cc] || cc;
        if (cc.length === 2)
            searchOpts.countryCode = searchOpts.countryCode || cc;
    }
    var places = search(searchName, searchOpts);
    if (!places || places.error || places.length === 0)
        return { error: 'Place not found: ' + placeName };
    var place = places[0];
    var data = forecast(place.latitude, place.longitude, opts);
    data.location = {
        name: place.name,
        country: place.country,
        admin1: place.admin1,
        latitude: place.latitude,
        longitude: place.longitude,
        timezone: place.timezone,
        population: place.population
    };
    return data;
}

function weatherAtAsync(placeName, opts, callback) {
    if (typeof opts === 'function') { callback = opts; opts = {}; }
    opts = opts || {};
    var searchOpts = { count: 5, countryCode: opts.countryCode };
    var parts = placeName.split(/\s*,\s*/);
    var searchName = parts[0];
    if (parts.length >= 2) {
        var cc = parts[parts.length-1].toUpperCase();
        cc = countryAliases[cc] || cc;
        if (cc.length === 2)
            searchOpts.countryCode = searchOpts.countryCode || cc;
    }
    searchAsync(searchName, searchOpts, function(err, places) {
        if (err || !places || places.length === 0)
            return callback(err || 'Place not found: ' + placeName, null);
        var place = places[0];
        forecastAsync(place.latitude, place.longitude, opts, function(err, data) {
            if (err) return callback(err, null);
            data.location = {
                name: place.name,
                country: place.country,
                admin1: place.admin1,
                latitude: place.latitude,
                longitude: place.longitude,
                timezone: place.timezone,
                population: place.population
            };
            callback(null, data);
        });
    });
}

/* --- Cache management --- */

function clearCache() {
    if (_cacheDisabled) return;
    _db = null;
    _cacheInitialized = false;
    var dir = _config.cacheDir;
    if (!dir) {
        var tmpdir = (typeof process !== 'undefined' && process.env && process.env.TMPDIR) || '/tmp';
        dir = tmpdir + '/rampart-open-meteo-cache';
    }
    try {
        rampart.utils.rmFile(dir + '/data.mdb');
        rampart.utils.rmFile(dir + '/lock.mdb');
        rampart.utils.rmdir(dir);
    } catch(e) {}
}

/* ===================================================================
   Exports
   =================================================================== */
module.exports = {
    configure:      configure,
    weatherCodes:   weatherCodes,

    search:         search,
    searchAsync:    searchAsync,

    current:        current,
    currentAsync:   currentAsync,

    forecast:       forecast,
    forecastAsync:  forecastAsync,

    history:        history,
    historyAsync:   historyAsync,

    airQuality:     airQuality,
    airQualityAsync: airQualityAsync,

    marine:         marine,
    marineAsync:    marineAsync,

    weatherAt:      weatherAt,
    weatherAtAsync: weatherAtAsync,

    clearCache:     clearCache
};
