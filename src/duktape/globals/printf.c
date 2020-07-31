///////////////////////////////////////////////////////////////////////////////
// \author (c) Marco Paland (info@paland.com)
//             2014-2019, PALANDesign Hannover, Germany
//
// \license The MIT License (MIT)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// \brief Tiny printf, sprintf and (v)snprintf implementation, optimized for speed on
//        embedded systems with a very limited resources. These routines are thread
//        safe and reentrant!
//        Use this instead of the bloated standard/newlib printf cause these use
//        malloc for printf (and may not be thread safe).
//
///////////////////////////////////////////////////////////////////////////////
// FROM: https://github.com/mpaland/printf
// MODIFIED BY Aaron Flin for use in duktape

#include <stdbool.h>
#include <stdint.h>
#include "../../rp.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include "printf.h"
pthread_mutex_t pflock;
// 'ntoa' conversion buffer size, this must be big enough to hold one converted
// numeric number including padded zeros (dynamically created on stack)
// default: 32 byte
#ifndef PRINTF_NTOA_BUFFER_SIZE
#define PRINTF_NTOA_BUFFER_SIZE 32U
#endif
// 'ftoa' conversion buffer size, this must be big enough to hold one converted
// float number including padded zeros (dynamically created on stack)
// default: 32 byte
#ifndef PRINTF_FTOA_BUFFER_SIZE
#define PRINTF_FTOA_BUFFER_SIZE 32U
#endif
// support for the floating point type (%f)
// default: activated
#ifndef PRINTF_DISABLE_SUPPORT_FLOAT
#define PRINTF_SUPPORT_FLOAT
#endif
// support for exponential floating point notation (%e/%g)
// default: activated
#ifndef PRINTF_DISABLE_SUPPORT_EXPONENTIAL
#define PRINTF_SUPPORT_EXPONENTIAL
#endif
// define the default floating point precision
// default: 6 digits
#ifndef PRINTF_DEFAULT_FLOAT_PRECISION
#define PRINTF_DEFAULT_FLOAT_PRECISION 6U
#endif
// define the largest float suitable to print with %f
// default: 1e9
#ifndef PRINTF_MAX_FLOAT
#define PRINTF_MAX_FLOAT 1e9
#endif
// support for the long long types (%llu or %p)
// default: activated
#ifndef PRINTF_DISABLE_SUPPORT_LONG_LONG
#define PRINTF_SUPPORT_LONG_LONG
#endif
// support for the ptrdiff_t type (%t)
// ptrdiff_t is normally defined in <stddef.h> as long or long long type
// default: activated
#ifndef PRINTF_DISABLE_SUPPORT_PTRDIFF_T
#define PRINTF_SUPPORT_PTRDIFF_T
#endif
///////////////////////////////////////////////////////////////////////////////
// internal flag definitions
#define FLAGS_ZEROPAD (1U << 0U)
#define FLAGS_LEFT (1U << 1U)
#define FLAGS_PLUS (1U << 2U)
#define FLAGS_SPACE (1U << 3U)
#define FLAGS_HASH (1U << 4U)
#define FLAGS_UPPERCASE (1U << 5U)
#define FLAGS_CHAR (1U << 6U)
#define FLAGS_SHORT (1U << 7U)
#define FLAGS_LONG (1U << 8U)
#define FLAGS_LONG_LONG (1U << 9U)
#define FLAGS_PRECISION (1U << 10U)
#define FLAGS_ADAPT_EXP (1U << 11U)
#define FLAGS_BANG (1U << 12U)
#include <float.h>
// output function type
typedef void (*out_fct_type)(char character, void *buffer, size_t idx, size_t maxlen);
// wrapper (used as buffer) for output function type
typedef struct
{
    void (*fct)(char character, void *arg);
    void *arg;
} out_fct_wrap_type;
// internal buffer output
static inline void _out_buffer(char character, void *buffer, size_t idx, size_t maxlen)
{
    if (idx < maxlen)
    {
        ((char *)buffer)[idx] = character;
    }
}
// internal null output
static inline void _out_null(char character, void *buffer, size_t idx, size_t maxlen)
{
    (void)character;
    (void)buffer;
    (void)idx;
    (void)maxlen;
}
static inline void _out_char(char character, void *buffer, size_t idx, size_t maxlen)
{
    (void)buffer;
    (void)idx;
    (void)maxlen;
        putchar(character);
}

static inline void _fout_char(char character, void *stream, size_t idx, size_t maxlen)
{
    FILE *out=(FILE*)stream;
    (void)idx;
    (void)maxlen;
    fputc(character,out);
}
// internal output function wrapper
static inline void _out_fct(char character, void *buffer, size_t idx, size_t maxlen)
{
    (void)idx;
    (void)maxlen;
    if (character)
    {
        // buffer is the output fct pointer
        ((out_fct_wrap_type *)buffer)->fct(character, ((out_fct_wrap_type *)buffer)->arg);
    }
}
// internal secure strlen
// \return The length of the string (excluding the terminating 0) limited by 'maxsize'
static inline unsigned int _strnlen_s(const char *str, size_t maxsize)
{
    const char *s;
    for (s = str; *s && maxsize--; ++s)
        ;
    return (unsigned int)(s - str);
}
// internal test if char is a digit (0-9)
// \return true if char is a digit
static inline bool _is_digit(char ch)
{
    return (ch >= '0') && (ch <= '9');
}
// internal ASCII string to unsigned int conversion
static unsigned int _atoi(const char **str)
{
    unsigned int i = 0U;
    while (_is_digit(**str))
    {
        i = i * 10U + (unsigned int)(*((*str)++) - '0');
    }
    return i;
}
// output the specified string in reverse, taking care of any zero-padding
static size_t _out_rev(out_fct_type out, char *buffer, size_t idx, size_t maxlen, const char *buf, size_t len, unsigned int width, unsigned int flags)
{
    const size_t start_idx = idx;
    // pad spaces up to given width
    if (!(flags & FLAGS_LEFT) && !(flags & FLAGS_ZEROPAD))
    {
        size_t i;
        for (i = len; i < width; i++)
        {
            out(' ', buffer, idx++, maxlen);
        }
    }
    // reverse string
    while (len)
    {
        out(buf[--len], buffer, idx++, maxlen);
    }
    // append pad spaces up to given width
    if (flags & FLAGS_LEFT)
    {
        while (idx - start_idx < width)
        {
            out(' ', buffer, idx++, maxlen);
        }
    }
    return idx;
}
// internal itoa format
static size_t _ntoa_format(out_fct_type out, char *buffer, size_t idx, size_t maxlen, char *buf, size_t len, bool negative, unsigned int base, unsigned int prec, unsigned int width, unsigned int flags)
{
    // pad leading zeros
    if (!(flags & FLAGS_LEFT))
    {
        if (width && (flags & FLAGS_ZEROPAD) && (negative || (flags & (FLAGS_PLUS | FLAGS_SPACE))))
        {
            width--;
        }
        while ((len < prec) && (len < PRINTF_NTOA_BUFFER_SIZE))
        {
            buf[len++] = '0';
        }
        while ((flags & FLAGS_ZEROPAD) && (len < width) && (len < PRINTF_NTOA_BUFFER_SIZE))
        {
            buf[len++] = '0';
        }
    }
    // handle hash
    if (flags & FLAGS_HASH)
    {
        if (!(flags & FLAGS_PRECISION) && len && ((len == prec) || (len == width)))
        {
            len--;
            if (len && (base == 16U))
            {
                len--;
            }
        }
        if ((base == 16U) && !(flags & FLAGS_UPPERCASE) && (len < PRINTF_NTOA_BUFFER_SIZE))
        {
            buf[len++] = 'x';
        }
        else if ((base == 16U) && (flags & FLAGS_UPPERCASE) && (len < PRINTF_NTOA_BUFFER_SIZE))
        {
            buf[len++] = 'X';
        }
        else if ((base == 2U) && (len < PRINTF_NTOA_BUFFER_SIZE))
        {
            buf[len++] = 'b';
        }
        if (len < PRINTF_NTOA_BUFFER_SIZE)
        {
            buf[len++] = '0';
        }
    }
    if (len < PRINTF_NTOA_BUFFER_SIZE)
    {
        if (negative)
        {
            buf[len++] = '-';
        }
        else if (flags & FLAGS_PLUS)
        {
            buf[len++] = '+'; // ignore the space if the '+' exists
        }
        else if (flags & FLAGS_SPACE)
        {
            buf[len++] = ' ';
        }
    }
    return _out_rev(out, buffer, idx, maxlen, buf, len, width, flags);
}
// internal itoa for 'long' type
static size_t _ntoa_long(out_fct_type out, char *buffer, size_t idx, size_t maxlen, unsigned long value, bool negative, unsigned long base, unsigned int prec, unsigned int width, unsigned int flags)
{
    char buf[PRINTF_NTOA_BUFFER_SIZE];
    size_t len = 0U;
    // no hash for 0 values
    if (!value)
    {
        flags &= ~FLAGS_HASH;
    }
    // write if precision != 0 and value is != 0
    if (!(flags & FLAGS_PRECISION) || value)
    {
        do
        {
            const char digit = (char)(value % base);
            buf[len++] = digit < 10 ? '0' + digit : (flags & FLAGS_UPPERCASE ? 'A' : 'a') + digit - 10;
            value /= base;
        } while (value && (len < PRINTF_NTOA_BUFFER_SIZE));
    }
    return _ntoa_format(out, buffer, idx, maxlen, buf, len, negative, (unsigned int)base, prec, width, flags);
}
// internal itoa for 'long long' type
static size_t _ntoa_long_long(out_fct_type out, char *buffer, size_t idx, size_t maxlen, unsigned long long value, bool negative, unsigned long long base, unsigned int prec, unsigned int width, unsigned int flags)
{
    char buf[PRINTF_NTOA_BUFFER_SIZE];
    size_t len = 0U;
    // no hash for 0 values
    if (!value)
    {
        flags &= ~FLAGS_HASH;
    }
    // write if precision != 0 and value is != 0
    if (!(flags & FLAGS_PRECISION) || value)
    {
        do
        {
            const char digit = (char)(value % base);
            buf[len++] = digit < 10 ? '0' + digit : (flags & FLAGS_UPPERCASE ? 'A' : 'a') + digit - 10;
            value /= base;
        } while (value && (len < PRINTF_NTOA_BUFFER_SIZE));
    }
    return _ntoa_format(out, buffer, idx, maxlen, buf, len, negative, (unsigned int)base, prec, width, flags);
}
// forward declaration so that _ftoa can switch to exp notation for values > PRINTF_MAX_FLOAT
static size_t _etoa(out_fct_type out, char *buffer, size_t idx, size_t maxlen, double value, unsigned int prec, unsigned int width, unsigned int flags);
// internal ftoa for fixed decimal floating point
static size_t _ftoa(out_fct_type out, char *buffer, size_t idx, size_t maxlen, double value, unsigned int prec, unsigned int width, unsigned int flags)
{
    char buf[PRINTF_FTOA_BUFFER_SIZE];
    size_t len = 0U;
    double diff = 0.0;
    // powers of 10
    static const double pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
    // test for special values
    if (value != value)
        return _out_rev(out, buffer, idx, maxlen, "nan", 3, width, flags);
    if (value < -DBL_MAX)
        return _out_rev(out, buffer, idx, maxlen, "fni-", 4, width, flags);
    if (value > DBL_MAX)
        return _out_rev(out, buffer, idx, maxlen, (flags & FLAGS_PLUS) ? "fni+" : "fni", (flags & FLAGS_PLUS) ? 4U : 3U, width, flags);
    // test for very large values
    // standard printf behavior is to print EVERY whole number digit -- which could be 100s of characters overflowing your buffers == bad
    if ((value > PRINTF_MAX_FLOAT) || (value < -PRINTF_MAX_FLOAT))
    {
        return _etoa(out, buffer, idx, maxlen, value, prec, width, flags);
    }
    // test for negative
    bool negative = false;
    if (value < 0)
    {
        negative = true;
        value = 0 - value;
    }
    // set default precision, if not set explicitly
    if (!(flags & FLAGS_PRECISION))
    {
        prec = PRINTF_DEFAULT_FLOAT_PRECISION;
    }
    // limit precision to 9, cause a prec >= 10 can lead to overflow errors
    while ((len < PRINTF_FTOA_BUFFER_SIZE) && (prec > 9U))
    {
        buf[len++] = '0';
        prec--;
    }
    int whole = (int)value;
    double tmp = (value - whole) * pow10[prec];
    unsigned long frac = (unsigned long)tmp;
    diff = tmp - frac;
    if (diff > 0.5)
    {
        ++frac;
        // handle rollover, e.g. case 0.99 with prec 1 is 1.0
        if (frac >= pow10[prec])
        {
            frac = 0;
            ++whole;
        }
    }
    else if (diff < 0.5)
    {
    }
    else if ((frac == 0U) || (frac & 1U))
    {
        // if halfway, round up if odd OR if last digit is 0
        ++frac;
    }
    if (prec == 0U)
    {
        diff = value - (double)whole;
        if ((!(diff < 0.5) || (diff > 0.5)) && (whole & 1))
        {
            // exactly 0.5 and ODD, then round up
            // 1.5 -> 2, but 2.5 -> 2
            ++whole;
        }
    }
    else
    {
        unsigned int count = prec;
        // now do fractional part, as an unsigned number
        while (len < PRINTF_FTOA_BUFFER_SIZE)
        {
            --count;
            buf[len++] = (char)(48U + (frac % 10U));
            if (!(frac /= 10U))
            {
                break;
            }
        }
        // add extra 0s
        while ((len < PRINTF_FTOA_BUFFER_SIZE) && (count-- > 0U))
        {
            buf[len++] = '0';
        }
        if (len < PRINTF_FTOA_BUFFER_SIZE)
        {
            // add decimal
            buf[len++] = '.';
        }
    }
    // do whole part, number is reversed
    while (len < PRINTF_FTOA_BUFFER_SIZE)
    {
        buf[len++] = (char)(48 + (whole % 10));
        if (!(whole /= 10))
        {
            break;
        }
    }
    // pad leading zeros
    if (!(flags & FLAGS_LEFT) && (flags & FLAGS_ZEROPAD))
    {
        if (width && (negative || (flags & (FLAGS_PLUS | FLAGS_SPACE))))
        {
            width--;
        }
        while ((len < width) && (len < PRINTF_FTOA_BUFFER_SIZE))
        {
            buf[len++] = '0';
        }
    }
    if (len < PRINTF_FTOA_BUFFER_SIZE)
    {
        if (negative)
        {
            buf[len++] = '-';
        }
        else if (flags & FLAGS_PLUS)
        {
            buf[len++] = '+'; // ignore the space if the '+' exists
        }
        else if (flags & FLAGS_SPACE)
        {
            buf[len++] = ' ';
        }
    }
    return _out_rev(out, buffer, idx, maxlen, buf, len, width, flags);
}
// internal ftoa variant for exponential floating-point type, contributed by Martijn Jasperse <m.jasperse@gmail.com>
static size_t _etoa(out_fct_type out, char *buffer, size_t idx, size_t maxlen, double value, unsigned int prec, unsigned int width, unsigned int flags)
{
    // check for NaN and special values
    if ((value != value) || (value > DBL_MAX) || (value < -DBL_MAX))
    {
        return _ftoa(out, buffer, idx, maxlen, value, prec, width, flags);
    }
    // determine the sign
    const bool negative = value < 0;
    if (negative)
    {
        value = -value;
    }
    // default precision
    if (!(flags & FLAGS_PRECISION))
    {
        prec = PRINTF_DEFAULT_FLOAT_PRECISION;
    }
    // determine the decimal exponent
    // based on the algorithm by David Gay (https://www.ampl.com/netlib/fp/dtoa.c)
    union
    {
        uint64_t U;
        double F;
    } conv;
    conv.F = value;
    int exp2 = (int)((conv.U >> 52U) & 0x07FFU) - 1023;          // effectively log2
    conv.U = (conv.U & ((1ULL << 52U) - 1U)) | (1023ULL << 52U); // drop the exponent so conv.F is now in [1,2)
    // now approximate log10 from the log2 integer part and an expansion of ln around 1.5
    int expval = (int)(0.1760912590558 + exp2 * 0.301029995663981 + (conv.F - 1.5) * 0.289529654602168);
    // now we want to compute 10^expval but we want to be sure it won't overflow
    exp2 = (int)(expval * 3.321928094887362 + 0.5);
    const double z = expval * 2.302585092994046 - exp2 * 0.6931471805599453;
    const double z2 = z * z;
    conv.U = (uint64_t)(exp2 + 1023) << 52U;
    // compute exp(z) using continued fractions, see https://en.wikipedia.org/wiki/Exponential_function#Continued_fractions_for_ex
    conv.F *= 1 + 2 * z / (2 - z + (z2 / (6 + (z2 / (10 + z2 / 14)))));
    // correct for rounding errors
    if (value < conv.F)
    {
        expval--;
        conv.F /= 10;
    }
    // the exponent format is "%+03d" and largest value is "307", so set aside 4-5 characters
    unsigned int minwidth = ((expval < 100) && (expval > -100)) ? 4U : 5U;
    // in "%g" mode, "prec" is the number of *significant figures* not decimals
    if (flags & FLAGS_ADAPT_EXP)
    {
        // do we want to fall-back to "%f" mode?
        if ((value >= 1e-4) && (value < 1e6))
        {
            if ((int)prec > expval)
            {
                prec = (unsigned)((int)prec - expval - 1);
            }
            else
            {
                prec = 0;
            }
            flags |= FLAGS_PRECISION; // make sure _ftoa respects precision
            // no characters in exponent
            minwidth = 0U;
            expval = 0;
        }
        else
        {
            // we use one sigfig for the whole part
            if ((prec > 0) && (flags & FLAGS_PRECISION))
            {
                --prec;
            }
        }
    }
    // will everything fit?
    unsigned int fwidth = width;
    if (width > minwidth)
    {
        // we didn't fall-back so subtract the characters required for the exponent
        fwidth -= minwidth;
    }
    else
    {
        // not enough characters, so go back to default sizing
        fwidth = 0U;
    }
    if ((flags & FLAGS_LEFT) && minwidth)
    {
        // if we're padding on the right, DON'T pad the floating part
        fwidth = 0U;
    }
    // rescale the float value
    if (expval)
    {
        value /= conv.F;
    }
    // output the floating part
    const size_t start_idx = idx;
    idx = _ftoa(out, buffer, idx, maxlen, negative ? -value : value, prec, fwidth, flags & ~FLAGS_ADAPT_EXP);
    // output the exponent part
    if (minwidth)
    {
        // output the exponential symbol
        out((flags & FLAGS_UPPERCASE) ? 'E' : 'e', buffer, idx++, maxlen);
        // output the exponent value
        idx = _ntoa_long(out, buffer, idx, maxlen, (expval < 0) ? -expval : expval, expval < 0, 10, 0, minwidth - 1, FLAGS_ZEROPAD | FLAGS_PLUS);
        // might need to right-pad spaces
        if (flags & FLAGS_LEFT)
        {
            while (idx - start_idx < width)
                out(' ', buffer, idx++, maxlen);
        }
    }
    return idx;
}
static int _printf(out_fct_type out, char *buffer, const size_t maxlen, duk_context *ctx, duk_idx_t fidx)
{
    unsigned int flags, width, precision, n;
    size_t idx = 0U;
    int preserveUfmt = 0;
    const char *format = duk_require_string(ctx, fidx++);
    if (!buffer)
    {
        // use null output function
        out = _out_null;
    }
    while (*format)
    {
        // format specifier?  %[flags][width][.precision][length]
        if (*format != '%')
        {
            // no
            out(*format, buffer, idx++, maxlen);
            format++;
            continue;
        }
        else
        {
            // yes, evaluate it
            format++;
        }
        // evaluate flags
        flags = 0U;
        do
        {
            switch (*format)
            {
            case '0':
                flags |= FLAGS_ZEROPAD;
                format++;
                n = 1U;
                break;
            case '-':
                flags |= FLAGS_LEFT;
                format++;
                n = 1U;
                break;
            case '+':
                flags |= FLAGS_PLUS;
                format++;
                n = 1U;
                break;
            case ' ':
                flags |= FLAGS_SPACE;
                format++;
                n = 1U;
                break;
            case '#':
                flags |= FLAGS_HASH;
                format++;
                n = 1U;
                break;
            case '!':
                flags |= FLAGS_BANG;
                format++;
                n = 1U;
                break;
            default:
                n = 0U;
                break;
            }
        } while (n);
        // evaluate width field
        width = 0U;
        if (_is_digit(*format))
        {
            width = _atoi(&format);
        }
        else if (*format == '*')
        {
            const int w = duk_require_int(ctx, fidx++);
            if (w < 0)
            {
                flags |= FLAGS_LEFT; // reverse padding
                width = (unsigned int)-w;
            }
            else
            {
                width = (unsigned int)w;
            }
            format++;
        }
        // evaluate precision field
        precision = 0U;
        if (*format == '.')
        {
            flags |= FLAGS_PRECISION;
            format++;
            if (_is_digit(*format))
            {
                precision = _atoi(&format);
            }
            else if (*format == '*')
            {
                const int prec = duk_require_int(ctx, fidx++);
                precision = prec > 0 ? (unsigned int)prec : 0U;
                format++;
            }
        }
        // evaluate length field
        switch (*format)
        {
        case 'l':
            flags |= FLAGS_LONG;
            format++;
            if (*format == 'l')
            {
                flags |= FLAGS_LONG_LONG;
                format++;
            }
            break;
        case 'h':
            flags |= FLAGS_SHORT;
            format++;
            if (*format == 'h')
            {
                flags |= FLAGS_CHAR;
                format++;
            }
            break;
        case 'j':
            flags |= (sizeof(intmax_t) == sizeof(long) ? FLAGS_LONG : FLAGS_LONG_LONG);
            format++;
            break;
        case 'z':
            flags |= (sizeof(size_t) == sizeof(long) ? FLAGS_LONG : FLAGS_LONG_LONG);
            format++;
            break;
        default:
            break;
        }
        // evaluate specifier
        switch (*format)
        {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'b':
        {
            // set the base
            unsigned int base;
            if (*format == 'x' || *format == 'X')
            {
                base = 16U;
            }
            else if (*format == 'o')
            {
                base = 8U;
            }
            else if (*format == 'b')
            {
                base = 2U;
            }
            else
            {
                base = 10U;
                flags &= ~FLAGS_HASH; // no hash for dec format
            }
            // uppercase
            if (*format == 'X')
            {
                flags |= FLAGS_UPPERCASE;
            }
            // no plus or space flag for u, x, X, o, b
            if ((*format != 'i') && (*format != 'd'))
            {
                flags &= ~(FLAGS_PLUS | FLAGS_SPACE);
            }
            // ignore '0' flag when precision is given
            if (flags & FLAGS_PRECISION)
            {
                flags &= ~FLAGS_ZEROPAD;
            }
            // convert the integer
            if ((*format == 'i') || (*format == 'd'))
            {
                // signed
                if (flags & FLAGS_LONG_LONG)
                {
                    const long long value = (long long)duk_require_number(ctx, fidx++);
                    idx = _ntoa_long_long(out, buffer, idx, maxlen, (unsigned long long)(value > 0 ? value : 0 - value), value < 0, base, precision, width, flags);
                }
                else if (flags & FLAGS_LONG)
                {
                    const long value = (long)duk_require_number(ctx, fidx++);
                    idx = _ntoa_long(out, buffer, idx, maxlen, (unsigned long)(value > 0 ? value : 0 - value), value < 0, base, precision, width, flags);
                }
                else
                {
                    const int value = (flags & FLAGS_CHAR) ? (char)duk_require_int(ctx, fidx++) : (flags & FLAGS_SHORT) ? (short int)duk_require_int(ctx, fidx++) : duk_require_int(ctx, fidx++);
                    idx = _ntoa_long(out, buffer, idx, maxlen, (unsigned int)(value > 0 ? value : 0 - value), value < 0, base, precision, width, flags);
                }
            }
            else
            {
                // unsigned
                if (flags & FLAGS_LONG_LONG)
                {
                    idx = _ntoa_long_long(out, buffer, idx, maxlen, (unsigned long long)duk_require_number(ctx, fidx++), false, base, precision, width, flags);
                }
                else if (flags & FLAGS_LONG)
                {
                    idx = _ntoa_long(out, buffer, idx, maxlen, (unsigned long)duk_require_number(ctx, fidx++), false, base, precision, width, flags);
                }
                else
                {
                    const unsigned int value = (flags & FLAGS_CHAR) ? (unsigned char)duk_require_number(ctx, fidx++) : (flags & FLAGS_SHORT) ? (unsigned short int)duk_require_number(ctx, fidx++) : (unsigned int)duk_require_number(ctx, fidx++);
                    idx = _ntoa_long(out, buffer, idx, maxlen, value, false, base, precision, width, flags);
                }
            }
            format++;
            break;
        }
        case 'f':
        case 'F':
            if (*format == 'F')
                flags |= FLAGS_UPPERCASE;
            idx = _ftoa(out, buffer, idx, maxlen, duk_require_number(ctx, fidx++), precision, width, flags);
            format++;
            break;
        case 'e':
        case 'E':
        case 'g':
        case 'G':
            if ((*format == 'g') || (*format == 'G'))
                flags |= FLAGS_ADAPT_EXP;
            if ((*format == 'E') || (*format == 'G'))
                flags |= FLAGS_UPPERCASE;
            idx = _etoa(out, buffer, idx, maxlen, duk_require_number(ctx, fidx++), precision, width, flags);
            format++;
            break;
        case 'c':
        {
            unsigned int l = 1U;
            const char *c = duk_require_string(ctx, fidx++);
            // pre padding
            if (!(flags & FLAGS_LEFT))
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            // char output
            out((char)*c, buffer, idx++, maxlen);
            // post padding
            if (flags & FLAGS_LEFT)
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            format++;
            break;
        }
        /* -AJF: added B, U and J for buffer, urlencode and JSON.stringify */
        case 'B':
        {
            duk_size_t sz;
            char *b=(char *)duk_require_buffer_data(ctx,fidx++,&sz);
            int i=0,isz=(int)sz;
            for (;i<isz;i++)
            {
                out(*(b++), buffer, idx++, maxlen);
            }
            format++;
            break;
        }
        case 'U':
        {
            duk_size_t sz;
            const char *s = duk_require_lstring(ctx, fidx, &sz);
            char *u;
            if (flags & FLAGS_BANG)
                u = duk_rp_url_decode((char *)s, (int)sz);
            else
                u = duk_rp_url_encode((char *)s, (int)sz);
            /* prevent double url encoding on second pass in sprintf*/
            if (!buffer)
            {
                preserveUfmt = 1;
                duk_dup(ctx, fidx);
            }
            duk_push_string(ctx, u);
            free(u);
            duk_replace(ctx, fidx);
            goto string;
        }
        case 'J':
            if (!duk_is_string(ctx, fidx))
                (void)duk_json_encode(ctx, fidx); //no ++
                                                  //no break
        case 's':
        string:
        {
            const char *p = duk_require_string(ctx, fidx++);
            unsigned int l = _strnlen_s(p, precision ? precision : (size_t)-1);
            // pre padding
            if (flags & FLAGS_PRECISION)
            {
                l = (l < precision ? l : precision);
            }
            if (!(flags & FLAGS_LEFT))
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            // string output
            while ((*p != 0) && (!(flags & FLAGS_PRECISION) || precision--))
            {
                out(*(p++), buffer, idx++, maxlen);
            }
            // post padding
            if (flags & FLAGS_LEFT)
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            /* prevent double url encoding on second pass in sprintf*/
            if (preserveUfmt)
            {
                duk_replace(ctx, fidx - 1);
                preserveUfmt = 0;
            }
            format++;
            break;
        }
        case '%':
            out('%', buffer, idx++, maxlen);
            format++;
            break;
        default:
            out(*format, buffer, idx++, maxlen);
            format++;
            break;
        }
    }
    // termination
    //out((char)0, buffer, idx < maxlen ? idx : maxlen - 1U, maxlen);
    // return written chars without terminating \0
    return (int)idx;
}
///////////////////////////////////////////////////////////////////////////////
duk_ret_t duk_printf(duk_context *ctx)
{
    char buffer[1];
    int ret;
    if (pthread_mutex_lock(&pflock) == EINVAL)
    {
        fprintf(stderr, "could not obtain lock in http_callback\n");
        exit(1);
    }
    ret = _printf(_out_char, buffer, (size_t)-1, ctx,0);
    pthread_mutex_unlock(&pflock);
    duk_push_int(ctx, ret);
    return 1;
}
#define getfh(ctx,idx,func) ({\
    FILE *f;\
    if( !duk_get_prop_string(ctx,idx,DUK_HIDDEN_SYMBOL("filehandle")) )\
    {\
        duk_push_sprintf(ctx,"%s: argument is not a file handle",func);\
        duk_throw(ctx);\
    }\
    f=duk_get_pointer(ctx,-1);\
    duk_pop(ctx);\
    f;\
})
#define getfh_nonull(ctx,idx,func) ({\
    FILE *f;\
    if( !duk_get_prop_string(ctx,idx,DUK_HIDDEN_SYMBOL("filehandle")) )\
    {\
        duk_push_sprintf(ctx,"error %s: argument is not a file handle",func);\
        duk_throw(ctx);\
    }\
    f=duk_get_pointer(ctx,-1);\
    duk_pop(ctx);\
    if(f==NULL){\
        duk_push_sprintf(ctx,"error %s: file handle was previously closed",func);\
        duk_throw(ctx);\
    }\
    f;\
})
duk_ret_t duk_fseek(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fseek()");
    long offset=(long)duk_require_number(ctx,1);
    int whence;
    const char *wstr=duk_require_string(ctx,2);
    
    if(!strcmp(wstr,"SEEK_SET"))
        whence=SEEK_SET;
    else if(!strcmp(wstr,"SEEK_END"))
        whence=SEEK_END;
    else if(!strcmp(wstr,"SEEK_CUR"))
        whence=SEEK_CUR;
    else
    {
        duk_push_sprintf(ctx,"error fseek(): invalid argument '%s'",wstr);
        duk_throw(ctx);
    }
    if(fseek(f, offset, whence))
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error fseek():'%s'", strerror(errno));
        duk_throw(ctx);
    }
    return 0;   
}
duk_ret_t duk_rewind(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"rewind()");
    rewind(f);
    return 0;
}
duk_ret_t duk_ftell(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"ftell()");
    long pos;
    
    pos=ftell(f);
    if(pos==-1)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error ftell():'%s'", strerror(errno));
        duk_throw(ctx);
    }
    duk_push_number(ctx,(double)pos);
    return 1;
}
duk_ret_t duk_fread(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fread()");
    void *buf;
    size_t read, sz=(size_t)duk_require_number(ctx,1);
    buf=duk_push_dynamic_buffer(ctx, (duk_size_t)sz);
    
    read=fread(buf,1,sz,f);
    if(read != sz)
    {
        if(ferror(f))
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "error fread(): error reading file"); 
            duk_throw(ctx);
        }
        duk_resize_buffer(ctx, -1, (duk_size_t)read);
    }
    return(1);
}
duk_ret_t duk_fwrite(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fwrite()");
    void *buf;
    size_t wrote, sz=(size_t)duk_get_number_default(ctx,2,-1);
    duk_size_t bsz;
    duk_to_buffer(ctx,1,&bsz);
    buf=duk_get_buffer_data(ctx, 1, &bsz);
    if(sz !=-1)
    {
        if((size_t)bsz < sz)
            sz=(size_t)bsz;
    }
    else sz=(size_t)bsz;
    
    wrote=fwrite(buf,1,sz,f);
    if(wrote != sz)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error fwrite(): error writing file"); 
        duk_throw(ctx);
    }
    duk_push_number(ctx,(double)wrote);
    return(1);
}
duk_ret_t duk_fopen(duk_context *ctx)
{
    FILE *f;
    const char *fn=duk_require_string(ctx,0);
    const char *mode=duk_require_string(ctx,1);
    
    f=fopen(fn,mode);
    if(f==NULL) goto err;
    
    duk_push_object(ctx);
    duk_push_pointer(ctx,(void *)f);
    duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("filehandle") );
    return 1;
    
    err:
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening file '%s': %s", fn, strerror(errno));
    return duk_throw(ctx);
}
duk_ret_t duk_fclose(duk_context *ctx)
{
    FILE *f = getfh(ctx,0,"fclose()");
    
    fclose(f);
    duk_push_pointer(ctx,NULL);
    duk_put_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filehandle") );
    
    return 1;
}

duk_ret_t duk_fprintf(duk_context *ctx)
{
    int ret;
    const char *fn;
    FILE *out;
    int append=0;
    int closefh=1;
   
    if(duk_is_object(ctx,0))
    {
        if(duk_get_prop_string(ctx,0,"stream"))
        {
            const char *s=duk_require_string(ctx,-1);
            if (!strcmp(s,"stdout"))
            {
                out=stdout;
            }
            else if (!strcmp(s,"stderr"))
            {
                out=stderr;
            }
            else
            {
                duk_push_string(ctx,"error: fprintf({stream:""},...): stream must be stdout or stderr");
                duk_throw(ctx);
            }
            closefh=0;
            duk_pop(ctx);
            goto startprint;
        }
        duk_pop(ctx);
        
        if ( duk_get_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filehandle")) )
        {
                    
            out=duk_get_pointer(ctx,-1);
            duk_pop(ctx);
            closefh=0;
            if(out==NULL)
            {
                duk_push_string(ctx,"error: fprintf(handle,...): handle was previously closed");
                duk_throw(ctx);
            }
            goto startprint;
        }
        duk_pop(ctx);
        
        duk_push_string(ctx,"error: fprintf({},...): invalid option");
        duk_throw(ctx);
    }
    else
    {
        fn=duk_require_string(ctx,0);
        if( duk_is_boolean(ctx,1) )
        {
            append=duk_get_boolean(ctx,1);
            duk_remove(ctx,1);
        }    
        
        if (pthread_mutex_lock(&pflock) == EINVAL)
        {
            fprintf(stderr, "error: could not obtain lock in fprintf\n");
            exit(1);
        }
        if(append)
        {
            if( (out=fopen(fn,"a")) == NULL )
            {
                if( (out=fopen(fn,"w")) == NULL )
                    goto err;
            }
            
        }
        else
        {
            if( (out=fopen(fn,"w")) == NULL )
                goto err;
        }
    }
    startprint:
    ret = _printf(_fout_char, (void*)out, (size_t)-1, ctx,1);
    if(closefh)
        fclose(out);
    pthread_mutex_unlock(&pflock);
    duk_push_int(ctx, ret);
    return 1;
    
    err:
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening file '%s': %s", fn, strerror(errno));
    return duk_throw(ctx);
}
duk_ret_t duk_sprintf(duk_context *ctx)
{
    char *buffer;
    int size = _printf(_out_null, NULL, (size_t)-1, ctx,0);
    buffer = malloc((size_t)size + 1);
    if (!buffer)
    {
        duk_push_string(ctx, "malloc error in sprintf");
        duk_throw(ctx);
    }
    (void)_printf(_out_buffer, buffer, (size_t)-1, ctx,0);
    duk_push_lstring(ctx, buffer,(duk_size_t)size);
    free(buffer);
    return 1;
}
duk_ret_t duk_bprintf(duk_context *ctx)
{
    char *buffer;
    int size = _printf(_out_null, NULL, (size_t)-1, ctx,0);
    buffer = (char *) duk_push_fixed_buffer(ctx, (duk_size_t)size);
    (void)_printf(_out_buffer, buffer, (size_t)-1, ctx,0);
    return 1;
}
void duk_printf_init(duk_context *ctx)
{
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    if(!duk_get_prop_string(ctx,-1,"cfunc"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }
    duk_push_c_function(ctx, duk_printf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "printf");
    duk_push_c_function(ctx, duk_sprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "sprintf");
    duk_push_c_function(ctx, duk_fprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "fprintf");
    duk_push_c_function(ctx, duk_bprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "bprintf");
    duk_push_c_function(ctx, duk_fopen, 2);
    duk_put_prop_string(ctx, -2, "fopen");
    duk_push_c_function(ctx, duk_fclose, 1);
    duk_put_prop_string(ctx, -2, "fclose");
    duk_push_c_function(ctx, duk_fseek, 3);
    duk_put_prop_string(ctx, -2, "fseek");
    duk_push_c_function(ctx, duk_ftell, 1);
    duk_put_prop_string(ctx, -2, "ftell");
    duk_push_c_function(ctx, duk_rewind, 1);
    duk_put_prop_string(ctx, -2, "rewind");
    duk_push_c_function(ctx, duk_fread, 2);
    duk_put_prop_string(ctx, -2, "fread");
    duk_push_c_function(ctx, duk_fwrite, 3);
    duk_put_prop_string(ctx, -2, "fwrite");
    duk_push_object(ctx);
    duk_push_string(ctx,"stdout");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"stdout");
    duk_push_object(ctx);
    duk_push_string(ctx,"stderr");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"stderr");
    duk_put_prop_string(ctx, -2,"cfunc");
    duk_put_global_string(ctx,"rampart");
    if (pthread_mutex_init(&pflock, NULL) == EINVAL)
    {
        fprintf(stderr, "could not initialize context lock\n");
        exit(1);
    }
}
/*
int printf_(const char* format, ...)
{
  va_list va;
  va_start(va, format);
  char buffer[1];
  const int ret = _vsnprintf(_out_char, buffer, (size_t)-1, format, va);
  va_end(va);
  return ret;
}

int sprintf_(char* buffer, const char* format, ...)
{
  va_list va;
  va_start(va, format);
  const int ret = _vsnprintf(_out_buffer, buffer, (size_t)-1, format, va);
  va_end(va);
  return ret;
}

int snprintf_(char* buffer, size_t count, const char* format, ...)
{
  va_list va;
  va_start(va, format);
  const int ret = _vsnprintf(_out_buffer, buffer, count, format, va);
  va_end(va);
  return ret;
}

int vprintf_(const char* format, va_list va)
{
  char buffer[1];
  return _vsnprintf(_out_char, buffer, (size_t)-1, format, va);
}

int vsnprintf_(char* buffer, size_t count, const char* format, va_list va)
{
  return _vsnprintf(_out_buffer, buffer, count, format, va);
}

int fctprintf(void (*out)(char character, void* arg), void* arg, const char* format, ...)
{
  va_list va;
  va_start(va, format);
  const out_fct_wrap_type out_fct_wrap = { out, arg };
  const int ret = _vsnprintf(_out_fct, (char*)(uintptr_t)&out_fct_wrap, (size_t)-1, format, va);
  va_end(va);
  return ret;
}
*/