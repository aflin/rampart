/*! https://mths.be/punycode v2.3.1 by @mathias */
/*
 * punycode.js -- Punycode (RFC 3492) implementation for JavaScript.
 *
 * Originally by Mathias Bynens (https://mathiasbynens.be/), distributed
 * under the MIT license:
 *
 *   Copyright Mathias Bynens <https://mathiasbynens.be/>
 *
 *   Permission is hereby granted, free of charge, to any person
 *   obtaining a copy of this software and associated documentation
 *   files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use,
 *   copy, modify, merge, publish, distribute, sublicense, and/or sell
 *   copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 *
 * Adapted into rampart's js_modules/ in CommonJS form.  Algorithm
 * unchanged.
 */
'use strict';

/* RFC 3492 constants */
var maxInt        = 2147483647; // 0x7FFFFFFF
var base          = 36;
var tMin          = 1;
var tMax          = 26;
var skew          = 38;
var damp          = 700;
var initialBias   = 72;
var initialN      = 128; // 0x80
var delimiter     = '-'; // '\x2D'

/* Regex patterns */
var regexPunycode    = /^xn--/;
var regexNonASCII    = /[^\0-\x7F]/;            // matches any non-ASCII char
var regexSeparators  = /[\x2E。．｡]/g; // RFC 3490 separators

/* Error messages */
var errors = {
    'overflow':      'Overflow: input needs wider integers to process',
    'not-basic':     'Illegal input >= 0x80 (not a basic code point)',
    'invalid-input': 'Invalid input'
};

/* Convenience */
var baseMinusTMin = base - tMin;
var floor   = Math.floor;
var stringFromCharCode = String.fromCharCode;

function error(type) {
    throw new RangeError(errors[type]);
}

/* A generic map() that calls fn(item, ...rest) on each item of `array`
   and returns the result array. */
function map(array, fn) {
    var result = [];
    var length = array.length;
    while (length--) result[length] = fn(array[length]);
    return result;
}

/* domainToASCII / domainToUnicode helper: split on RFC 3490 separators,
   transform each label, rejoin with '.'.  Preserves an optional leading
   "name@" portion (email addresses). */
function mapDomain(domain, fn) {
    var parts = domain.split('@');
    var result = '';
    if (parts.length > 1) {
        result = parts[0] + '@';
        domain = parts[1];
    }
    domain = domain.replace(regexSeparators, '\x2E');
    var labels = domain.split('.');
    var encoded = map(labels, fn).join('.');
    return result + encoded;
}

/* ucs2 encode/decode -- convert between JS strings and arrays of full
   Unicode code points (handles surrogate pairs). */
function ucs2decode(string) {
    var output = [];
    var counter = 0;
    var length = string.length;
    while (counter < length) {
        var value = string.charCodeAt(counter++);
        if (value >= 0xD800 && value <= 0xDBFF && counter < length) {
            var extra = string.charCodeAt(counter++);
            if ((extra & 0xFC00) === 0xDC00) {
                output.push(((value & 0x3FF) << 10) + (extra & 0x3FF) + 0x10000);
            } else {
                output.push(value);
                counter--;
            }
        } else {
            output.push(value);
        }
    }
    return output;
}

function ucs2encode(codePoints) {
    return String.fromCodePoint.apply(null, codePoints);
}

/* Polyfill String.fromCodePoint for older engines (duktape lacks it
   for some ranges). */
if (!String.fromCodePoint) {
    String.fromCodePoint = function() {
        var chars = [];
        for (var i = 0; i < arguments.length; i++) {
            var cp = arguments[i];
            if (cp < 0x10000) {
                chars.push(stringFromCharCode(cp));
            } else {
                cp -= 0x10000;
                chars.push(stringFromCharCode(0xD800 | (cp >> 10)));
                chars.push(stringFromCharCode(0xDC00 | (cp & 0x3FF)));
            }
        }
        return chars.join('');
    };
}

/* basicToDigit / digitToBasic -- convert between a "basic code point"
   ASCII character and the integer value it represents in punycode. */
function basicToDigit(codePoint) {
    if (codePoint >= 0x30 && codePoint < 0x3A) {       // '0' .. '9'
        return 26 + (codePoint - 0x30);
    }
    if (codePoint >= 0x41 && codePoint < 0x5B) {       // 'A' .. 'Z'
        return codePoint - 0x41;
    }
    if (codePoint >= 0x61 && codePoint < 0x7B) {       // 'a' .. 'z'
        return codePoint - 0x61;
    }
    return base;
}

function digitToBasic(digit, flag) {
    /* 0..25 -> a..z (lowercase) or A..Z (uppercase), 26..35 -> 0..9 */
    return digit + 22 + 75 * (digit < 26 ? 1 : 0) - ((flag !== 0) << 5);
}

/* RFC 3492 bias adaptation */
function adapt(delta, numPoints, firstTime) {
    var k = 0;
    delta = firstTime ? floor(delta / damp) : delta >> 1;
    delta += floor(delta / numPoints);
    for (; delta > baseMinusTMin * tMax >> 1; k += base) {
        delta = floor(delta / baseMinusTMin);
    }
    return floor(k + (baseMinusTMin + 1) * delta / (delta + skew));
}

/* Decode a punycode string to Unicode. */
function decode(input) {
    var output = [];
    var inputLength = input.length;
    var i = 0;
    var n = initialN;
    var bias = initialBias;

    /* Handle the basic code points: copy everything before the last
       delimiter (if any) to output verbatim. */
    var basic = input.lastIndexOf(delimiter);
    if (basic < 0) basic = 0;
    for (var j = 0; j < basic; ++j) {
        if (input.charCodeAt(j) >= 0x80) error('not-basic');
        output.push(input.charCodeAt(j));
    }

    /* Main loop: decode the rest. */
    var index = basic > 0 ? basic + 1 : 0;
    while (index < inputLength) {
        var oldi = i;
        for (var w = 1, k = base; ; k += base) {
            if (index >= inputLength) error('invalid-input');
            var digit = basicToDigit(input.charCodeAt(index++));
            if (digit >= base) error('invalid-input');
            if (digit > floor((maxInt - i) / w)) error('overflow');
            i += digit * w;
            var t = k <= bias ? tMin : (k >= bias + tMax ? tMax : k - bias);
            if (digit < t) break;
            var baseMinusT = base - t;
            if (w > floor(maxInt / baseMinusT)) error('overflow');
            w *= baseMinusT;
        }
        var out = output.length + 1;
        bias = adapt(i - oldi, out, oldi === 0);
        if (floor(i / out) > maxInt - n) error('overflow');
        n += floor(i / out);
        i %= out;
        output.splice(i++, 0, n);
    }
    return String.fromCodePoint.apply(null, output);
}

/* Encode a Unicode string to punycode. */
function encode(input) {
    var output = [];
    /* Convert the input to an array of code points. */
    input = ucs2decode(input);
    var inputLength = input.length;
    var n = initialN;
    var delta = 0;
    var bias = initialBias;

    /* Emit basic code points verbatim. */
    for (var idx = 0; idx < inputLength; ++idx) {
        var currentValue = input[idx];
        if (currentValue < 0x80) output.push(stringFromCharCode(currentValue));
    }

    var basicLength = output.length;
    var handledCPCount = basicLength;
    if (basicLength) output.push(delimiter);

    /* Main encoding loop. */
    while (handledCPCount < inputLength) {
        var m = maxInt;
        for (var idx2 = 0; idx2 < inputLength; ++idx2) {
            var currentValue2 = input[idx2];
            if (currentValue2 >= n && currentValue2 < m) m = currentValue2;
        }
        var handledCPCountPlusOne = handledCPCount + 1;
        if (m - n > floor((maxInt - delta) / handledCPCountPlusOne)) error('overflow');
        delta += (m - n) * handledCPCountPlusOne;
        n = m;
        for (var idx3 = 0; idx3 < inputLength; ++idx3) {
            var currentValue3 = input[idx3];
            if (currentValue3 < n && ++delta > maxInt) error('overflow');
            if (currentValue3 === n) {
                var q = delta;
                for (var k = base; ; k += base) {
                    var t = k <= bias ? tMin : (k >= bias + tMax ? tMax : k - bias);
                    if (q < t) break;
                    var qMinusT = q - t;
                    var baseMinusT = base - t;
                    output.push(stringFromCharCode(digitToBasic(t + qMinusT % baseMinusT, 0)));
                    q = floor(qMinusT / baseMinusT);
                }
                output.push(stringFromCharCode(digitToBasic(q, 0)));
                bias = adapt(delta, handledCPCountPlusOne, handledCPCount === basicLength);
                delta = 0;
                ++handledCPCount;
            }
        }
        ++delta;
        ++n;
    }
    return output.join('');
}

/* toUnicode -- convert an ACE form ("xn--...") domain or email to its
   Unicode form. */
function toUnicode(input) {
    return mapDomain(input, function(string) {
        return regexPunycode.test(string)
            ? decode(string.slice(4).toLowerCase())
            : string;
    });
}

/* toASCII -- convert a Unicode domain (or email) to its ACE form. */
function toASCII(input) {
    return mapDomain(input, function(string) {
        return regexNonASCII.test(string)
            ? 'xn--' + encode(string)
            : string;
    });
}

module.exports = {
    version:    '2.3.1',
    ucs2:       { decode: ucs2decode, encode: ucs2encode },
    decode:     decode,
    encode:     encode,
    toASCII:    toASCII,
    toUnicode:  toUnicode
};
