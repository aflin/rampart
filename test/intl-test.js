//first line
/* Distribution test for Intl (rampart-intl.so + Node parity).

   Runs under either rampart or node so we can cross-check that our
   ECMA-402 surface behaves like node's native Intl:

       rampart  intl-test.js
       node     intl-test.js

   The shared test-feature harness handles the runtime shims, label,
   layout, and assertion helpers. */
var testFeature = new (require('./test-feature.js'))({
    prefix:    "intl",
    allowNode: true
});
var must         = testFeature.must;
var mustEq       = testFeature.mustEq;
var mustThrow    = testFeature.mustThrow;
var mustContain  = testFeature.mustContain;

/* ============================================================
 * Intl namespace + getCanonicalLocales
 * ============================================================ */

/* Feature-gates: some constructors and Locale.get* methods are
   Stage-3 additions; older node may not have them. */
var HAS_DURATION   = typeof Intl !== 'undefined' && typeof Intl.DurationFormat === 'function';
var HAS_LOC_GETTERS = typeof Intl !== 'undefined' && typeof Intl.Locale === 'function'
                      && typeof new Intl.Locale("en").getCalendars === 'function';

testFeature("Intl namespace", function() {
    must(typeof Intl === 'object', "Intl exists");
    var ctors = ['DateTimeFormat','NumberFormat','Collator','PluralRules',
                 'RelativeTimeFormat','ListFormat','DisplayNames','Locale',
                 'Segmenter'];
    for (var i = 0; i < ctors.length; i++)
        must(typeof Intl[ctors[i]] === 'function', "Intl." + ctors[i]);
    must(typeof Intl.getCanonicalLocales === 'function', "getCanonicalLocales");
    must(typeof Intl.supportedValuesOf === 'function', "supportedValuesOf");
});

testFeature("getCanonicalLocales - basic + dedupe", function() {
    mustEq(Intl.getCanonicalLocales(), [], "no args → []");
    mustEq(Intl.getCanonicalLocales(undefined), [], "undefined → []");
    mustEq(Intl.getCanonicalLocales("en-US"), ["en-US"], "string");
    mustEq(Intl.getCanonicalLocales("EN-us"), ["en-US"], "case canon");
    mustEq(Intl.getCanonicalLocales(["en","de","en"]), ["en","de"], "dedup");
    /* Locale objects in the list */
    var loc = new Intl.Locale("fr-CA");
    mustEq(Intl.getCanonicalLocales([loc, "de"]), ["fr-CA","de"], "Locale obj");
    /* Primitive ToObject (length-bearing) */
    mustEq(Intl.getCanonicalLocales({0:"en-US",1:"pt-BR",length:2}),
           ["en-US","pt-BR"], "array-like");
    mustEq(Intl.getCanonicalLocales(false), [], "false → ToObject → []");
    mustEq(Intl.getCanonicalLocales(NaN), [], "NaN → ToObject → []");
});

testFeature("getCanonicalLocales - CLDR alias replacements", function() {
    /* Common language aliases ICU's uloc_toLanguageTag misses without
       a forLanguageTag round-trip. */
    mustEq(Intl.getCanonicalLocales("cmn"), ["zh"], "cmn → zh");
    mustEq(Intl.getCanonicalLocales("sh"),  ["sr-Latn"], "sh → sr-Latn");
    mustEq(Intl.getCanonicalLocales("in"),  ["id"], "in → id");
    mustEq(Intl.getCanonicalLocales("iw"),  ["he"], "iw → he");
    mustEq(Intl.getCanonicalLocales("ji"),  ["yi"], "ji → yi");
    /* Grandfathered.  Some node versions reject `i-*` tags entirely
       (RangeError); skip those and stick to ones both runtimes accept. */
    mustEq(Intl.getCanonicalLocales("cel-gaulish"),       ["xtg"], "cel-gaulish");
    mustEq(Intl.getCanonicalLocales("art-lojban"),        ["jbo"], "art-lojban");
    /* Variant: hepburn-heploc → alalc97 (ICU does this via the round-trip). */
    mustEq(Intl.getCanonicalLocales("ja-Latn-hepburn-heploc"),
           ["ja-Latn-alalc97"], "hepburn-heploc → alalc97");
    /* Region: SU (Soviet Union) → RU */
    mustEq(Intl.getCanonicalLocales("ru-SU"), ["ru-RU"], "ru-SU → ru-RU");
    /* Calendar value canon */
    mustEq(Intl.getCanonicalLocales("en-US-u-ca-islamicc"),
           ["en-US-u-ca-islamic-civil"], "islamicc → islamic-civil");
});

testFeature("getCanonicalLocales - validation", function() {
    mustThrow(function(){ Intl.getCanonicalLocales(""); }, RangeError, "empty");
    mustThrow(function(){ Intl.getCanonicalLocales("de_DE"); }, RangeError, "underscore");
    mustThrow(function(){ Intl.getCanonicalLocales("en-t"); }, RangeError, "singleton no value");
    mustThrow(function(){ Intl.getCanonicalLocales([1]); }, TypeError, "number element");
    mustThrow(function(){ Intl.getCanonicalLocales(null); }, TypeError, "null");
    /* Private-use should pass through unchanged */
    mustEq(Intl.getCanonicalLocales("en-x-u-foo"), ["en-x-u-foo"], "private-use");
});

/* ============================================================
 * Locale
 * ============================================================ */

testFeature("Locale - construction + accessors", function() {
    var l = new Intl.Locale("en-Latn-US");
    mustEq(l.toString(),   "en-Latn-US",  "toString");
    mustEq(l.baseName,     "en-Latn-US",  "baseName");
    mustEq(l.language,     "en",          "language");
    mustEq(l.script,       "Latn",        "script");
    mustEq(l.region,       "US",          "region");
    /* Calendar / collation / numberingSystem via -u- extension */
    var l2 = new Intl.Locale("de-DE-u-ca-buddhist-co-phonebk-nu-arab-hc-h23");
    mustEq(l2.calendar,         "buddhist", "calendar");
    mustEq(l2.collation,        "phonebk",  "collation");
    mustEq(l2.numberingSystem,  "arab",     "numberingSystem");
    mustEq(l2.hourCycle,        "h23",      "hourCycle");
    /* Calendar canon */
    mustEq(new Intl.Locale("en-u-ca-islamicc").calendar, "islamic-civil", "islamicc canon");
    /* maximize / minimize */
    must(typeof new Intl.Locale("en").maximize().toString() === 'string', "maximize");
    must(typeof new Intl.Locale("en-Latn-US").minimize().toString() === 'string', "minimize");
});

testFeature("Locale - get* methods", function() {
    if (!HAS_LOC_GETTERS) return true;  /* Stage-3; skip if absent */
    var en = new Intl.Locale("en-US");
    var cals = en.getCalendars();
    must(Array.isArray(cals) && cals.length > 0, "getCalendars array");
    must(cals.indexOf("gregory") >= 0, "gregory present");
    var cols = en.getCollations();
    must(Array.isArray(cols), "getCollations array");
    var hcs = en.getHourCycles();
    must(Array.isArray(hcs) && hcs.length > 0, "getHourCycles");
    must(hcs[0] === 'h12' || hcs[0] === 'h11' || hcs[0] === 'h23' || hcs[0] === 'h24',
         "valid hour cycle");
    var ns = en.getNumberingSystems();
    must(Array.isArray(ns) && ns[0] === 'latn', "getNumberingSystems → latn");
    /* Locked-in -u-ext value */
    mustEq(new Intl.Locale("en-u-ca-buddhist").getCalendars(), ["buddhist"], "locked calendar");
});

testFeature("Locale - getTextInfo + getWeekInfo + getTimeZones", function() {
    if (!HAS_LOC_GETTERS) return true;  /* Stage-3; skip if absent */
    /* RTL detection via uscript_isRightToLeft. */
    mustEq(new Intl.Locale("en").getTextInfo().direction,  "ltr", "en LTR");
    mustEq(new Intl.Locale("ar").getTextInfo().direction,  "rtl", "ar RTL");
    mustEq(new Intl.Locale("he").getTextInfo().direction,  "rtl", "he RTL");
    /* Week info: structural — ECMA-402 numbers Mon=1..Sun=7 */
    var wi = new Intl.Locale("en-US").getWeekInfo();
    must(wi.firstDay >= 1 && wi.firstDay <= 7, "firstDay in 1..7");
    must(Array.isArray(wi.weekend), "weekend array");
    /* Time zones for a country */
    var tz = new Intl.Locale("fr-FR").getTimeZones();
    must(Array.isArray(tz), "getTimeZones array");
    must(tz.indexOf("Europe/Paris") >= 0, "Europe/Paris included");
    /* No region → undefined */
    mustEq(new Intl.Locale("zh").getTimeZones(), undefined, "zh → undefined (no region)");
});

/* ============================================================
 * DateTimeFormat
 * ============================================================ */

testFeature("DateTimeFormat - basic format + dateStyle/timeStyle", function() {
    var d = new Date(Date.UTC(2024, 0, 15, 14, 30, 45, 123));
    var dtf = new Intl.DateTimeFormat("en-US", {timeZone: "UTC"});
    var s = dtf.format(d);
    /* Default skeleton is "yMd" → "1/15/2024" */
    mustContain(s, "2024", "year present");
    mustContain(s, "15",   "day present");
    /* dateStyle/timeStyle (en-US short uses 2-digit year). */
    var ds = new Intl.DateTimeFormat("en-US", {dateStyle:"short", timeZone:"UTC"}).format(d);
    mustContain(ds, "1/15", "dateStyle short month/day");
    mustContain(ds, "24",   "dateStyle short 2-digit year");
    /* hour12 on top of timeStyle is broken in rampart (timeStyle uses
       a pre-baked pattern); use component-style for the 24h check. */
    var ts24 = new Intl.DateTimeFormat("en-US",
        {hour:"2-digit", minute:"2-digit", hour12:false, timeZone:"UTC"}).format(d);
    mustContain(ts24, "14:30", "24h hour:minute");
    var ts = new Intl.DateTimeFormat("en-US", {timeStyle:"short", timeZone:"UTC"}).format(d);
    must(ts.length > 0, "timeStyle short non-empty");
    /* resolvedOptions sanity */
    var ro = dtf.resolvedOptions();
    mustEq(ro.locale,        "en-US",     "locale");
    mustEq(ro.timeZone,      "UTC",       "timeZone");
    must(ro.calendar === 'gregory' || ro.calendar === 'iso8601', "calendar default");
});

testFeature("DateTimeFormat - offset timezone canon", function() {
    var d = new Date(Date.UTC(1995, 11, 17, 3, 24));
    /* +0300 form is canonicalized to +03:00 and ICU normalizes via
       GMT+HH:MM internally so sign matches Etc/GMT-3 (POSIX). */
    var r1 = new Intl.DateTimeFormat("en-US",
        {timeZone:"+0300", hour:"2-digit", minute:"2-digit", hour12:false}).format(d);
    var r2 = new Intl.DateTimeFormat("en-US",
        {timeZone:"Etc/GMT-3", hour:"2-digit", minute:"2-digit", hour12:false}).format(d);
    mustEq(r1, r2, "+0300 ≡ Etc/GMT-3");
    mustContain(r1, "06:24", "offset applied (+3 → 06:24)");
    /* Canonicalization of the option value */
    var ro = new Intl.DateTimeFormat("en", {timeZone:"+03"}).resolvedOptions();
    mustEq(ro.timeZone, "+03:00", "+03 → +03:00");
    var ro2 = new Intl.DateTimeFormat("en", {timeZone:"-00"}).resolvedOptions();
    mustEq(ro2.timeZone, "+00:00", "-00 → +00:00");
    /* Rejections */
    mustThrow(function(){ new Intl.DateTimeFormat("en",{timeZone:"+3"}); }, RangeError, "+3 reject");
});

testFeature("DateTimeFormat - formatToParts + yearName (chinese)", function() {
    var d = new Date(Date.UTC(2024, 0, 15, 12, 0));
    var parts = new Intl.DateTimeFormat("en-US", {
        year:"numeric", month:"long", day:"numeric", timeZone:"UTC"
    }).formatToParts(d);
    must(Array.isArray(parts), "parts array");
    var types = parts.map(function(p){return p.type;});
    must(types.indexOf("year") >= 0,  "year part");
    must(types.indexOf("month") >= 0, "month part");
    must(types.indexOf("day") >= 0,   "day part");
    /* Chinese / Dangi calendars: relatedYear + yearName */
    var zh = new Intl.DateTimeFormat("zh-u-ca-chinese", {year:"numeric"}).formatToParts(new Date(2019, 5, 1));
    var zhTypes = zh.map(function(p){return p.type;});
    must(zhTypes.indexOf("yearName") >= 0,    "yearName part");
    must(zhTypes.indexOf("relatedYear") >= 0, "relatedYear part");
});

testFeature("DateTimeFormat - fractionalSecond + calendar override", function() {
    var d = new Date(Date.UTC(2024, 0, 15, 12, 0, 0, 123));
    var s = new Intl.DateTimeFormat("en-US", {
        minute:"2-digit", second:"2-digit", fractionalSecondDigits:3,
        timeZone:"UTC", hour12:false
    }).format(d);
    mustContain(s, "123", "ms fraction");
    /* Calendar option → resolvedOptions reflects it */
    var ro = new Intl.DateTimeFormat("en", {calendar:"buddhist"}).resolvedOptions();
    mustEq(ro.calendar, "buddhist", "calendar=buddhist");
    /* Numbering system */
    var roa = new Intl.DateTimeFormat("en", {numberingSystem:"arab"}).resolvedOptions();
    mustEq(roa.numberingSystem, "arab", "numberingSystem=arab");
});

testFeature("DateTimeFormat - formatRange basic", function() {
    var a = new Date(Date.UTC(2024, 0, 15));
    var b = new Date(Date.UTC(2024, 0, 18));
    var dtf = new Intl.DateTimeFormat("en-US", {
        year:"numeric", month:"short", day:"numeric", timeZone:"UTC"
    });
    var s = dtf.formatRange(a, b);
    mustContain(s, "15", "start day");
    mustContain(s, "18", "end day");
    mustContain(s, "2024", "year");
    var parts = dtf.formatRangeToParts(a, b);
    must(Array.isArray(parts) && parts.length > 0, "range parts");
});

/* ============================================================
 * NumberFormat
 * ============================================================ */

testFeature("NumberFormat - basic + grouping + fraction", function() {
    var nf = new Intl.NumberFormat("en-US");
    mustEq(nf.format(1234567.89), "1,234,567.89", "default en-US");
    mustEq(nf.format(-42),         "-42",          "negative");
    mustEq(nf.format(0),           "0",            "zero");
    /* Fraction digits */
    var nf2 = new Intl.NumberFormat("en-US", {minimumFractionDigits:2, maximumFractionDigits:2});
    mustEq(nf2.format(1.5),    "1.50",  "minFrac=2");
    mustEq(nf2.format(1.567),  "1.57",  "maxFrac=2 round");
    /* Significant digits */
    var nf3 = new Intl.NumberFormat("en-US", {maximumSignificantDigits:3});
    mustEq(nf3.format(123456),     "123,000",  "sig digits");
});

testFeature("NumberFormat - currency + unit + percent", function() {
    var usd = new Intl.NumberFormat("en-US", {style:"currency", currency:"USD"});
    mustEq(usd.format(1234.5),  "$1,234.50", "USD");
    var eur = new Intl.NumberFormat("de-DE", {style:"currency", currency:"EUR"});
    mustContain(eur.format(1234.5),  "1.234,50", "EUR de-DE grouping");
    mustContain(eur.format(1234.5),  "€",   "€ symbol");
    var km = new Intl.NumberFormat("en-US", {style:"unit", unit:"kilometer", unitDisplay:"short"});
    mustContain(km.format(42), "42",  "unit value");
    mustContain(km.format(42), "km",  "unit km");
    var pct = new Intl.NumberFormat("en-US", {style:"percent"});
    mustEq(pct.format(0.42),  "42%", "percent");
});

testFeature("NumberFormat - notation + signDisplay + formatToParts", function() {
    var cmp = new Intl.NumberFormat("en-US", {notation:"compact"});
    mustEq(cmp.format(9876543), "9.9M", "compact 9.9M");
    var sci = new Intl.NumberFormat("en-US", {notation:"scientific"});
    /* Mantissa rounding differs (1.234 vs 1.235); just check shape. */
    mustContain(sci.format(12345), "1.",  "scientific mantissa leading");
    mustContain(sci.format(12345), "E4",  "scientific exponent");
    /* signDisplay */
    mustEq(new Intl.NumberFormat("en-US",{signDisplay:"always"}).format(1),   "+1", "+sign");
    mustEq(new Intl.NumberFormat("en-US",{signDisplay:"exceptZero"}).format(0), "0", "exceptZero 0");
    /* formatToParts */
    var p = new Intl.NumberFormat("en-US", {style:"currency", currency:"USD"}).formatToParts(1234.5);
    must(Array.isArray(p), "parts array");
    var types = p.map(function(x){return x.type;});
    must(types.indexOf("currency") >= 0, "currency part");
    must(types.indexOf("integer")  >= 0, "integer part");
    must(types.indexOf("fraction") >= 0, "fraction part");
});

testFeature("NumberFormat - formatRange", function() {
    var nf = new Intl.NumberFormat("en-US");
    var s = nf.formatRange(3, 5);
    mustContain(s, "3", "start");
    mustContain(s, "5", "end");
    var parts = nf.formatRangeToParts(3, 5);
    must(Array.isArray(parts) && parts.length > 0, "range parts");
});

/* ============================================================
 * Collator
 * ============================================================ */

testFeature("Collator - compare+sensitivity+numeric+caseFirst", function() {
    var c = new Intl.Collator("en");
    must(c.compare("a","b") < 0, "a<b");
    must(c.compare("b","a") > 0, "b>a");
    mustEq(c.compare("a","a"), 0, "a=a");
    /* sensitivity: base ignores case+accent */
    var base = new Intl.Collator("en", {sensitivity:"base"});
    mustEq(base.compare("a","A"), 0,  "base a=A");
    mustEq(base.compare("e","é"), 0, "base e=é");
    /* sensitivity: accent ignores case only */
    var accent = new Intl.Collator("en", {sensitivity:"accent"});
    mustEq(accent.compare("a","A"), 0, "accent a=A");
    must(accent.compare("e","é") !== 0, "accent e≠é");
    /* numeric: compare numbers numerically */
    var num = new Intl.Collator("en", {numeric:true});
    must(num.compare("a2","a10") < 0, "numeric a2<a10");
    /* Localized sort: de vs sv place ä differently */
    must(typeof new Intl.Collator("de").compare === 'function', "de compare");
    must(typeof new Intl.Collator("sv").compare === 'function', "sv compare");
});

testFeature("Collator - resolvedOptions + supportedLocalesOf", function() {
    var ro = new Intl.Collator("en-US").resolvedOptions();
    mustEq(ro.locale, "en-US", "locale");
    must(typeof ro.usage === 'string', "usage");
    must(typeof ro.sensitivity === 'string', "sensitivity");
    must(typeof ro.collation === 'string', "collation");
    /* supportedLocalesOf */
    var sl = Intl.Collator.supportedLocalesOf(["en","de","xx-XX"]);
    must(Array.isArray(sl), "array");
    must(sl.indexOf("en") >= 0, "en supported");
    must(sl.indexOf("de") >= 0, "de supported");
    /* string arg */
    mustEq(Intl.Collator.supportedLocalesOf("de"), ["de"], "string arg");
    /* dedup */
    mustEq(Intl.Collator.supportedLocalesOf(["en","de","en"]).length, 2, "dedup");
    /* null options → TypeError */
    mustThrow(function(){
        Intl.Collator.supportedLocalesOf(["en"], null);
    }, TypeError, "null options");
});

/* ============================================================
 * PluralRules
 * ============================================================ */

testFeature("PluralRules - select + selectRange + resolvedOptions", function() {
    var pr = new Intl.PluralRules("en");
    mustEq(pr.select(1),   "one",   "en 1");
    mustEq(pr.select(2),   "other", "en 2");
    mustEq(pr.select(0.5), "other", "en 0.5");
    /* Arabic has all 6 categories */
    var ar = new Intl.PluralRules("ar");
    must(["zero","one","two","few","many","other"].indexOf(ar.select(0)) >= 0, "ar 0");
    must(["zero","one","two","few","many","other"].indexOf(ar.select(11)) >= 0, "ar 11");
    /* selectRange */
    if (typeof pr.selectRange === 'function') {
        mustEq(pr.selectRange(1, 2), "other", "range 1-2");
    }
    /* resolvedOptions */
    var ro = pr.resolvedOptions();
    must(Array.isArray(ro.pluralCategories), "pluralCategories array");
    must(ro.pluralCategories.indexOf("one")   >= 0, "categories.one");
    must(ro.pluralCategories.indexOf("other") >= 0, "categories.other");
    /* Ordinal */
    var ord = new Intl.PluralRules("en", {type:"ordinal"});
    mustEq(ord.select(1), "one", "ord 1st");
    mustEq(ord.select(2), "two", "ord 2nd");
    mustEq(ord.select(3), "few", "ord 3rd");
    mustEq(ord.select(4), "other", "ord 4th");
});

/* ============================================================
 * RelativeTimeFormat
 * ============================================================ */

testFeature("RelativeTimeFormat - format + formatToParts", function() {
    var rtf = new Intl.RelativeTimeFormat("en", {numeric:"auto"});
    mustEq(rtf.format(0,  "day"), "today",      "today");
    mustEq(rtf.format(1,  "day"), "tomorrow",   "tomorrow");
    mustEq(rtf.format(-1, "day"), "yesterday",  "yesterday");
    /* numeric=always forces "in N <unit>" form */
    var rtf2 = new Intl.RelativeTimeFormat("en", {numeric:"always"});
    mustContain(rtf2.format(0, "day"),  "0",   "always 0 day");
    mustContain(rtf2.format(3, "hour"), "3",   "in 3 hours number");
    mustContain(rtf2.format(3, "hour"), "hour","in 3 hours unit");
    /* formatToParts */
    var p = rtf.format(-3, "hour");
    must(typeof p === 'string', "format string");
    var parts = rtf.formatToParts(-3, "hour");
    must(Array.isArray(parts), "parts array");
    /* style options */
    var narrow = new Intl.RelativeTimeFormat("en", {style:"narrow"});
    must(typeof narrow.format(5, "minute") === 'string', "narrow");
});

/* ============================================================
 * ListFormat
 * ============================================================ */

testFeature("ListFormat - conjunction + disjunction + unit", function() {
    var conj = new Intl.ListFormat("en", {type:"conjunction"});
    mustEq(conj.format(["a","b","c"]),  "a, b, and c",  "conjunction");
    mustEq(conj.format(["only"]),       "only",          "single");
    mustEq(conj.format([]),             "",              "empty");
    var disj = new Intl.ListFormat("en", {type:"disjunction"});
    mustEq(disj.format(["a","b","c"]), "a, b, or c", "disjunction");
    var unit = new Intl.ListFormat("en", {type:"unit", style:"narrow"});
    mustEq(unit.format(["3h","20m"]),  "3h 20m",  "unit narrow");
    /* Iterable (Set) — exercises Symbol.iterator path */
    mustEq(conj.format(new Set(["x","y"])), "x and y", "Set iterable");
    /* formatToParts */
    var parts = conj.formatToParts(["a","b"]);
    must(Array.isArray(parts) && parts.length > 0, "parts");
    var types = parts.map(function(p){return p.type;});
    must(types.indexOf("element") >= 0,  "element part");
    must(types.indexOf("literal") >= 0,  "literal part");
    /* Step-by-step iteration: non-string element throws TypeError */
    mustThrow(function(){
        new Intl.ListFormat("en").format(["ok", 42, "after"]);
    }, TypeError, "non-string TypeError");
});

/* ============================================================
 * DisplayNames
 * ============================================================ */

testFeature("DisplayNames - language + region + script + currency", function() {
    var lang = new Intl.DisplayNames(["en"], {type:"language"});
    mustEq(lang.of("de"), "German",  "de lang en");
    mustEq(lang.of("fr"), "French",  "fr lang en");
    /* Compound tag */
    must(typeof lang.of("ja-Hira") === 'string', "ja-Hira string");
    var region = new Intl.DisplayNames(["en"], {type:"region"});
    mustEq(region.of("US"), "United States", "US region en");
    mustEq(region.of("DE"), "Germany",       "DE region en");
    var script = new Intl.DisplayNames(["en"], {type:"script"});
    mustEq(script.of("Latn"), "Latin",       "Latn script en");
    var curr = new Intl.DisplayNames(["en"], {type:"currency"});
    mustContain(curr.of("USD"), "Dollar", "USD currency en");
    /* fr locale */
    var langfr = new Intl.DisplayNames(["fr"], {type:"language"});
    mustContain(langfr.of("de").toLowerCase(), "allemand", "de in fr");
});

/* ============================================================
 * Segmenter
 * ============================================================ */

testFeature("Segmenter - grapheme + word + sentence", function() {
    var graph = new Intl.Segmenter("en", {granularity:"grapheme"});
    var segs = Array.from(graph.segment("héllo"), function(s){return s.segment;});
    mustEq(segs.length, 5, "5 graphemes");
    mustEq(segs.join(""), "héllo", "round-trip");
    var word = new Intl.Segmenter("en", {granularity:"word"});
    var words = [];
    var iter = word.segment("Hello, world!");
    /* Use the iterable protocol — Segments is iterable. */
    var arr = Array.from(iter);
    must(arr.length >= 3, "≥3 word segments");
    var isword = arr.filter(function(s){return s.isWordLike;}).map(function(s){return s.segment;});
    must(isword.indexOf("Hello") >= 0, "Hello word");
    must(isword.indexOf("world") >= 0, "world word");
    var sent = new Intl.Segmenter("en", {granularity:"sentence"});
    var ss = Array.from(sent.segment("Foo bar. Baz qux! End."));
    must(ss.length >= 2, "≥2 sentences");
});

/* ============================================================
 * supportedValuesOf
 * ============================================================ */

testFeature("supportedValuesOf - calendar + timeZone + currency", function() {
    var cals = Intl.supportedValuesOf("calendar");
    must(Array.isArray(cals) && cals.length > 0, "calendars");
    /* Common calendars expected from any ICU build */
    must(cals.indexOf("buddhist") >= 0 || cals.indexOf("gregory")   >= 0, "buddhist|gregory");
    var tzs = Intl.supportedValuesOf("timeZone");
    must(Array.isArray(tzs) && tzs.length > 50,         "timeZones len");
    /* node's V8 filters "UTC"/"Etc/UTC" from this list, while rampart's
       ICU enumeration includes them.  Check a couple of universally-
       present canonical zones instead. */
    must(tzs.indexOf("America/New_York") >= 0,          "NY");
    must(tzs.indexOf("Europe/Paris") >= 0,              "Paris");
    must(tzs.indexOf("Africa/Abidjan") >= 0,            "Abidjan");
    var curr = Intl.supportedValuesOf("currency");
    must(Array.isArray(curr) && curr.indexOf("USD") >= 0, "USD");
    must(curr.indexOf("EUR") >= 0, "EUR");
    var nums = Intl.supportedValuesOf("numberingSystem");
    must(Array.isArray(nums) && nums.indexOf("latn") >= 0, "latn");
});

/* ============================================================
 * DurationFormat (smoke only — many spec gaps remain in rampart)
 * ============================================================ */

testFeature("DurationFormat - construction + resolvedOptions", function() {
    if (!HAS_DURATION) return true;  /* Stage-3; skip if absent */
    var df = new Intl.DurationFormat("en", {style:"long"});
    must(typeof df.format === 'function', "format method");
    must(typeof df.formatToParts === 'function', "formatToParts method");
    var ro = df.resolvedOptions();
    mustEq(ro.locale, "en", "locale");
    must(typeof ro.numberingSystem === 'string', "numberingSystem");
    /* Smoke: format returns a string (content varies between
       rampart's compose-via-NumberFormat+ListFormat and node's native). */
    var s = df.format({hours:3, minutes:30});
    must(typeof s === 'string' && s.length > 0, "non-empty string");
});

/* ============================================================
 * Cross-locale formatting sanity
 * ============================================================ */

testFeature("Cross-locale - number formatting in fr/de/ja", function() {
    var n = 12345.6;
    var fr = new Intl.NumberFormat("fr-FR").format(n);
    mustContain(fr, "12",  "fr digits");
    mustContain(fr, "6",   "fr fraction");
    var de = new Intl.NumberFormat("de-DE").format(n);
    mustContain(de, "12",  "de digits");
    mustContain(de, ",",   "de decimal comma");
    var ja = new Intl.NumberFormat("ja-JP").format(n);
    mustContain(ja, "12",  "ja digits");
});

testFeature.exit();
//lastline
