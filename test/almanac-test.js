rampart.globalize(rampart.utils);

var almanac;
try {
    almanac = require("rampart-almanac");
} catch(e) {
    fprintf(stderr, "Could not load rampart-almanac: %s\nSKIPPING ALMANAC TESTS\n", e.message);
    process.exit(0);
}

function testFeature(name,test)
{
    var error=false;
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    printf("testing almanac - %-52s - ", name);
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}

/* ===================================================================
   SEASONS
   =================================================================== */

testFeature("seasons returns object", function() {
    var s = almanac.seasons(2025);
    return typeof s === 'object' && s.spring && s.summer && s.autumn && s.winter;
});

testFeature("seasons - spring 2025 date", function() {
    var s = almanac.seasons(2025);
    var d = new Date(s.spring);
    return d.getUTCFullYear() === 2025 && d.getUTCMonth() === 2 && d.getUTCDate() === 20;
});

testFeature("seasons - summer 2025 date", function() {
    var s = almanac.seasons(2025);
    var d = new Date(s.summer);
    return d.getUTCFullYear() === 2025 && d.getUTCMonth() === 5 && d.getUTCDate() === 21;
});

testFeature("seasons - winter 2025 date", function() {
    var s = almanac.seasons(2025);
    var d = new Date(s.winter);
    return d.getUTCFullYear() === 2025 && d.getUTCMonth() === 11 && d.getUTCDate() === 21;
});

/* ===================================================================
   SUNTIMES
   =================================================================== */

testFeature("suntimes returns object", function() {
    var t = almanac.suntimes("2024-01-01 -0700", 37.77, -122.42);
    return typeof t === 'object' && t.sunrise && t.sunset && t.solarNoon;
});

testFeature("suntimes - has daylight hours", function() {
    var t = almanac.suntimes("2024-01-01 -0700", 37.77, -122.42);
    return typeof t.daylightHours === 'number' && t.daylightHours > 9 && t.daylightHours < 10;
});

testFeature("suntimes - has twilight fields", function() {
    var t = almanac.suntimes("2024-01-01 -0700", 37.77, -122.42);
    return t.civilTwilightStart && t.civilTwilightEnd &&
           t.nauticalTwilightStart && t.nauticalTwilightEnd &&
           t.astronomicalTwilightStart && t.astronomicalTwilightEnd;
});

testFeature("suntimes - has azimuth fields", function() {
    var t = almanac.suntimes("2024-01-01 -0700", 37.77, -122.42);
    return typeof t.sunriseAzimuth === 'number' && typeof t.sunsetAzimuth === 'number';
});

testFeature("suntimes - sunrise before sunset", function() {
    var t = almanac.suntimes("2024-06-21 -0700", 37.77, -122.42);
    return new Date(t.sunrise) < new Date(t.sunset);
});

testFeature("suntimes - summer longer than winter", function() {
    var winter = almanac.suntimes("2024-12-21 -0700", 37.77, -122.42);
    var summer = almanac.suntimes("2024-06-21 -0700", 37.77, -122.42);
    return summer.daylightHours > winter.daylightHours;
});

/* ===================================================================
   MOONTIMES
   =================================================================== */

testFeature("moontimes returns object", function() {
    var t = almanac.moontimes("2024-01-01 -0700", 37.77, -122.42);
    return typeof t === 'object' && t.moonrise && t.moonset;
});

testFeature("moontimes - has phase fields", function() {
    var t = almanac.moontimes("2024-01-01 -0700", 37.77, -122.42);
    return t.newMoon && t.firstQuarter && t.fullMoon && t.lastQuarter;
});

testFeature("moontimes - phase is 0 to 1", function() {
    var t = almanac.moontimes("2024-01-01 -0700", 37.77, -122.42);
    return typeof t.moonPhase === 'number' && t.moonPhase >= 0 && t.moonPhase <= 1;
});

testFeature("moontimes - illumination is 0 to 1", function() {
    var t = almanac.moontimes("2024-01-01 -0700", 37.77, -122.42);
    return typeof t.moonIllumination === 'number' && t.moonIllumination >= 0 && t.moonIllumination <= 1;
});

testFeature("moontimes - has azimuth fields", function() {
    var t = almanac.moontimes("2024-01-01 -0700", 37.77, -122.42);
    return typeof t.moonriseAzimuth === 'number' && typeof t.moonsetAzimuth === 'number';
});

/* ===================================================================
   CELESTIALS
   =================================================================== */

testFeature("celestials returns object", function() {
    var t = almanac.celestials("2024-01-01", 37.77, -122.42);
    return typeof t === 'object' && t.sun && t.moon;
});

testFeature("celestials - has all planets", function() {
    var t = almanac.celestials("2024-01-01", 37.77, -122.42);
    return t.mercury && t.venus && t.mars && t.jupiter &&
           t.saturn && t.uranus && t.neptune && t.pluto;
});

testFeature("celestials - sun has position fields", function() {
    var t = almanac.celestials("2024-01-01", 37.77, -122.42);
    var s = t.sun;
    return typeof s.currentRightAscension === 'number' &&
           typeof s.currentDeclination === 'number' &&
           typeof s.currentAzimuth === 'number' &&
           typeof s.currentAltitude === 'number' &&
           s.nextRise && s.nextSet;
});

testFeature("celestials - planet has position fields", function() {
    var t = almanac.celestials("2024-01-01", 37.77, -122.42);
    var j = t.jupiter;
    return typeof j.currentRightAscension === 'number' &&
           typeof j.currentDeclination === 'number' &&
           typeof j.currentAzimuth === 'number' &&
           typeof j.currentAltitude === 'number' &&
           j.nextRise && j.nextSet;
});

/* ===================================================================
   HOLIDAYS
   =================================================================== */

var Holidays = almanac.holidays;
var hasHolidays = (typeof Holidays === 'function');

if(!hasHolidays) {
    fprintf(stderr, "almanac.holidays not available, skipping holiday tests\n");
} else {

testFeature("holidays - constructor works", function() {
    var hd = new Holidays();
    return typeof hd === 'object';
});

testFeature("holidays - constructor with country", function() {
    var hd = new Holidays('US');
    return typeof hd === 'object';
});

testFeature("holidays - getCountries returns object", function() {
    var hd = new Holidays();
    var res = hd.getCountries();
    return typeof res === 'object' && res.US && res.DE && res.FR;
});

testFeature("holidays - getStates for US", function() {
    var hd = new Holidays();
    var res = hd.getStates('US');
    return typeof res === 'object' && res.CA && res.NY && res.TX;
});

testFeature("holidays - getRegions for US/LA", function() {
    var hd = new Holidays();
    var res = hd.getRegions('US', 'LA');
    return typeof res === 'object' && res.NO;
});

testFeature("holidays - init and getHolidays", function() {
    var hd = new Holidays();
    hd.init('US');
    var res = hd.getHolidays(2024);
    return Array.isArray(res) && res.length > 5;
});

testFeature("holidays - New Year's Day present", function() {
    var hd = new Holidays('US');
    var res = hd.getHolidays(2024);
    var nyd = res.filter(function(h) { return h.name === "New Year's Day"; });
    return nyd.length === 1 && nyd[0].date.indexOf('2024-01-01') === 0;
});

testFeature("holidays - Independence Day present", function() {
    var hd = new Holidays('US');
    var res = hd.getHolidays(2024);
    var july4 = res.filter(function(h) { return h.name === "Independence Day"; });
    return july4.length === 1 && july4[0].date.indexOf('2024-07-04') === 0;
});

testFeature("holidays - Christmas present", function() {
    var hd = new Holidays('US');
    var res = hd.getHolidays(2024);
    var xmas = res.filter(function(h) { return h.name === "Christmas Day"; });
    return xmas.length === 1 && xmas[0].date.indexOf('2024-12-25') === 0;
});

testFeature("holidays - holiday has required fields", function() {
    var hd = new Holidays('US');
    var res = hd.getHolidays(2024);
    var h = res[0];
    return h.date && h.start && h.end && h.name && h.type;
});

testFeature("holidays - isHoliday positive", function() {
    var hd = new Holidays('US');
    var res = hd.isHoliday(autoScanDate('2024-07-04 00:00:00 -0500').date);
    return Array.isArray(res) && res.length > 0 && res[0].name === "Independence Day";
});

testFeature("holidays - isHoliday negative", function() {
    var hd = new Holidays('US');
    var res = hd.isHoliday(autoScanDate('2024-03-15 00:00:00 -0500').date);
    return res === false;
});

testFeature("holidays - different country (DE)", function() {
    var hd = new Holidays('DE');
    var res = hd.getHolidays(2024);
    var names = res.map(function(h) { return h.name; });
    return names.indexOf("Tag der Deutschen Einheit") !== -1;
});

testFeature("holidays - state level (US/LA/NO)", function() {
    var hd = new Holidays('US', 'LA', 'NO');
    var res = hd.getHolidays(2016);
    var mardi = res.filter(function(h) { return h.name === "Mardi Gras"; });
    return mardi.length === 1;
});

testFeature("holidays - getLanguages", function() {
    var hd = new Holidays('US');
    var langs = hd.getLanguages();
    return Array.isArray(langs) && langs.length > 0 && langs.indexOf('en') !== -1;
});

testFeature("holidays - setLanguages changes names", function() {
    var hd = new Holidays('DE');
    hd.setLanguages('en');
    var res = hd.getHolidays(2024);
    var nyd = res.filter(function(h) { return h.name === "New Year's Day"; });
    hd.setLanguages('de');
    var res2 = hd.getHolidays(2024);
    var neuj = res2.filter(function(h) { return h.name === "Neujahr"; });
    return nyd.length === 1 && neuj.length === 1;
});

testFeature("holidays - getTimezones", function() {
    var hd = new Holidays('US');
    var tz = hd.getTimezones();
    return Array.isArray(tz) && tz.length > 0;
});

testFeature("holidays - setTimezone", function() {
    var hd = new Holidays('US');
    hd.setTimezone('America/New_York');
    var res = hd.getHolidays(2024);
    var start = (typeof res[0].start === 'string') ? res[0].start : res[0].start.toISOString();
    return res.length > 0 && start.indexOf('T05:00:00') !== -1;
});

testFeature("holidays - getDayOff", function() {
    var hd = new Holidays('US');
    var day = hd.getDayOff();
    return day === 'sunday';
});

testFeature("holidays - setHoliday custom", function() {
    var hd = new Holidays('US');
    hd.setHoliday('03-15', {name: 'Company Day', type: 'observance'});
    var res = hd.getHolidays(2024);
    var custom = res.filter(function(h) { return h.name === 'Company Day'; });
    return custom.length === 1 && custom[0].date.indexOf('2024-03-15') === 0;
});

testFeature("holidays - init reinitializes", function() {
    var hd = new Holidays('US');
    var us = hd.getHolidays(2024);
    hd.init('DE');
    var de = hd.getHolidays(2024);
    var usNames = us.map(function(h) { return h.name; });
    var deNames = de.map(function(h) { return h.name; });
    return usNames.indexOf("Independence Day") !== -1 && deNames.indexOf("Independence Day") === -1;
});

} // end hasHolidays

/* ===================================================================
   WEATHER (Open-Meteo)
   =================================================================== */

var weather = almanac.weather;
var hasWeather = (typeof weather === 'object' && typeof weather.search === 'function');

if(!hasWeather) {
    fprintf(stderr, "almanac.weather not available, skipping weather tests\n");
} else {

// Check connectivity to open-meteo before running tests
var weatherOnline = false;
try {
    var curl = require('rampart-curl');
    var res = curl.fetch('https://geocoding-api.open-meteo.com/v1/search?name=Test&count=1');
    if(Array.isArray(res)) res = res[0];
    weatherOnline = (res && res.status === 200);
} catch(e) {}

if(!weatherOnline) {
    fprintf(stderr, "WARNING: open-meteo.com not reachable, skipping weather tests\n");
} else {

// Use a known location for consistent testing
var testLat = 37.7749;
var testLon = -122.4194;

// Clean cache for reproducible tests
weather.clearCache();

/* --- Geocoding --- */

testFeature("weather - search returns results", function() {
    var res = weather.search('San Francisco');
    return Array.isArray(res) && res.length > 0;
});

testFeature("weather - search result has fields", function() {
    var res = weather.search('San Francisco');
    var p = res[0];
    return p.name && typeof p.latitude === 'number' && typeof p.longitude === 'number' &&
           p.country && p.timezone;
});

testFeature("weather - search with countryCode filter", function() {
    var res = weather.search('London', {countryCode: 'GB'});
    return Array.isArray(res) && res.length > 0 && res[0].country_code === 'GB';
});

testFeature("weather - search caches results", function() {
    weather.search('Tokyo');
    var t1 = new Date().getTime();
    weather.search('Tokyo');
    var t2 = new Date().getTime();
    return (t2 - t1) < 50;
});

/* --- Current Weather --- */

testFeature("weather - current returns data", function() {
    var res = weather.current(testLat, testLon);
    return typeof res === 'object' && !res.error && res.current;
});

testFeature("weather - current has temperature", function() {
    var res = weather.current(testLat, testLon);
    return typeof res.current.temperature_2m === 'number';
});

testFeature("weather - current has weather description", function() {
    var res = weather.current(testLat, testLon);
    return typeof res.current.weather_code_description === 'string';
});

testFeature("weather - current has units", function() {
    var res = weather.current(testLat, testLon);
    return res.current_units && res.current_units.temperature_2m;
});

testFeature("weather - current respects unit config", function() {
    weather.configure({units: 'imperial'});
    var res = weather.current(testLat, testLon);
    weather.configure({units: 'metric'});
    return res.current_units && res.current_units.temperature_2m === '°F';
});

/* --- Forecast --- */

testFeature("weather - forecast returns data", function() {
    var res = weather.forecast(testLat, testLon);
    return typeof res === 'object' && !res.error;
});

testFeature("weather - forecast has hourly data", function() {
    var res = weather.forecast(testLat, testLon);
    return res.hourly && Array.isArray(res.hourly.time) && res.hourly.time.length > 0;
});

testFeature("weather - forecast has daily data", function() {
    var res = weather.forecast(testLat, testLon);
    return res.daily && Array.isArray(res.daily.time) && res.daily.time.length > 0;
});

testFeature("weather - forecast has current data", function() {
    var res = weather.forecast(testLat, testLon);
    return res.current && typeof res.current.temperature_2m === 'number';
});

testFeature("weather - forecast days option", function() {
    var res = weather.forecast(testLat, testLon, {days: 3});
    return res.daily && res.daily.time.length === 3;
});

testFeature("weather - forecast weather descriptions", function() {
    var res = weather.forecast(testLat, testLon, {days: 1});
    return res.daily && Array.isArray(res.daily.weather_code_description) &&
           typeof res.daily.weather_code_description[0] === 'string';
});

testFeature("weather - forecast custom hourly vars", function() {
    var res = weather.forecast(testLat, testLon, {
        days: 1,
        hourly: ['temperature_2m', 'dew_point_2m'],
        daily: false,
        current: false
    });
    return res.hourly && res.hourly.dew_point_2m && !res.daily;
});

/* --- Historical Weather --- */

testFeature("weather - history returns data", function() {
    var res = weather.history(testLat, testLon, '2024-01-01', '2024-01-03');
    return typeof res === 'object' && !res.error;
});

testFeature("weather - history has hourly data", function() {
    var res = weather.history(testLat, testLon, '2024-01-01', '2024-01-03');
    return res.hourly && Array.isArray(res.hourly.time) && res.hourly.time.length > 0;
});

testFeature("weather - history has daily data", function() {
    var res = weather.history(testLat, testLon, '2024-01-01', '2024-01-03');
    return res.daily && res.daily.time.length === 3;
});

/* --- Air Quality --- */

testFeature("weather - airQuality returns data", function() {
    var res = weather.airQuality(testLat, testLon);
    return typeof res === 'object' && !res.error;
});

testFeature("weather - airQuality has hourly data", function() {
    var res = weather.airQuality(testLat, testLon);
    return res.hourly && Array.isArray(res.hourly.time) && res.hourly.time.length > 0;
});

testFeature("weather - airQuality has PM2.5", function() {
    var res = weather.airQuality(testLat, testLon);
    return res.hourly && Array.isArray(res.hourly.pm2_5);
});

/* --- Marine Weather --- */

// Use a coastal location
var marineLat = 36.95;
var marineLon = -122.02;

testFeature("weather - marine returns data", function() {
    var res = weather.marine(marineLat, marineLon);
    return typeof res === 'object' && !res.error;
});

testFeature("weather - marine has hourly data", function() {
    var res = weather.marine(marineLat, marineLon);
    return res.hourly && Array.isArray(res.hourly.time);
});

testFeature("weather - marine has wave height", function() {
    var res = weather.marine(marineLat, marineLon);
    return res.hourly && Array.isArray(res.hourly.wave_height);
});

/* --- weatherAt convenience --- */

testFeature("weather - weatherAt by name", function() {
    var res = weather.weatherAt('Paris');
    return typeof res === 'object' && !res.error && res.location && res.location.name === 'Paris';
});

testFeature("weather - weatherAt with country code", function() {
    var res = weather.weatherAt('London, UK');
    return typeof res === 'object' && !res.error &&
           res.location && res.location.name === 'London';
});

testFeature("weather - weatherAt has forecast", function() {
    var res = weather.weatherAt('Tokyo');
    return res.daily && Array.isArray(res.daily.time) && res.daily.time.length > 0;
});

/* --- Configuration --- */

testFeature("weather - configure units imperial", function() {
    weather.configure({units: 'imperial'});
    var res = weather.current(testLat, testLon);
    weather.configure({units: 'metric'});
    return res.current_units.wind_speed_10m === 'mp/h';
});

testFeature("weather - configure units metric", function() {
    weather.configure({units: 'metric'});
    var res = weather.current(testLat, testLon);
    return res.current_units.temperature_2m === '°C';
});

testFeature("weather - configure disable cache", function() {
    weather.configure({lmdbCache: null});
    var t1 = new Date().getTime();
    weather.search('Berlin');
    var t2 = new Date().getTime();
    weather.search('Berlin');
    var t3 = new Date().getTime();
    weather.configure({});  // re-enable auto cache
    // both calls should take similar time (no caching)
    return (t3 - t2) > 50;
});

/* --- weatherCodes --- */

testFeature("weather - weatherCodes lookup", function() {
    return weather.weatherCodes[0] === 'Clear sky' &&
           weather.weatherCodes[61] === 'Rain: slight' &&
           weather.weatherCodes[95] === 'Thunderstorm: slight or moderate';
});

/* --- clearCache --- */

testFeature("weather - clearCache runs without error", function() {
    weather.clearCache();
    return true;
});

} // end weatherOnline
} // end hasWeather
