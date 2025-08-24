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
// FROM: https://github.com/mpaland/printf - with gratitude!
// MODIFIED BY Aaron Flin for use in duktape
// and with 'B', 'J', 's', 'S', 'U' 'C' and 'P' new/altered % format codes
//
// Modifications Copyright (C) 2025  Aaron Flin

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include "printf.h"
#include "colorspace.h"
#include "entities.h"
#include "entities.c"
#include "jcolor.h"

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
#define FLAGS_SQUOTE (1U << 13U)
#define FLAGS_FFORMAT (1U << 14U)
#define FLAGS_COLOR (1U << 15U)
#define FLAGS_COLOR_FORCE (1U << 16U)
#define FLAGS_COLOR_FORCE_TRUECOLOR (1U << 17U)
#define FLAGS_COLOR_FORCE_16 (1U << 18U)

#include <float.h>
// wrapper (used as buffer) for output function type
typedef struct
{
    void (*fct)(char character, void *arg);
    void *arg;
} out_fct_wrap_type;

typedef struct
{
    char *buf;
    size_t mSize;
    size_t size;
} grow_buffer;

// internal buffer output growable
static inline void _out_buffer_grow(char character, void *buffer, size_t idx, size_t maxlen)
{
    grow_buffer *b=(grow_buffer *) buffer;

    if( !(b->buf) )
    {
        b->mSize=64;
        REMALLOC(b->buf, 64);
    }


    if (idx < maxlen)
    {
        if(idx >= b->mSize)
        {
            b->mSize *= 2;
            REMALLOC(b->buf, b->mSize);
        }

        ((char *)b->buf)[idx] = character;
        b->size = idx+1;
    }
}

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
// output the specified null term string, without the trailing '\0' 
static size_t outs(out_fct_type out, char *buffer, size_t idx, size_t maxlen, const char *s)
{
    while(*s)
        out(*(s++), buffer, idx++, maxlen);
    return idx;
}
// output the specified string in reverse, taking care of any zero-padding
static size_t _out_rev(out_fct_type out, char *buffer, size_t idx, size_t maxlen, const char *buf, size_t len, unsigned int width, uint32_t flags)
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
static size_t _ntoa_format(out_fct_type out, char *buffer, size_t idx, size_t maxlen, char *buf, size_t len, bool negative, unsigned int base, unsigned int prec, unsigned int width, uint32_t flags)
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
static size_t _ntoa_long(out_fct_type out, char *buffer, size_t idx, size_t maxlen, unsigned long value, bool negative, unsigned long base, unsigned int prec, unsigned int width, uint32_t flags)
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
static size_t _ntoa_long_long(out_fct_type out, char *buffer, size_t idx, size_t maxlen, unsigned long long value, bool negative, unsigned long long base, unsigned int prec, unsigned int width, uint32_t flags)
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
/*  Scrapped - new version below
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
*/

/* we are gonna cheat and just used sprintf for this */
#define FLOAT_MAX_BUF 512
static size_t _ftoa(out_fct_type out, char *buffer, size_t idx, size_t maxlen, double value, unsigned int prec, unsigned int width, uint32_t flags)
{
    char buf[FLOAT_MAX_BUF];
    char fmt[16];
    int i=0;
    int len=0;
    char formatflag;

    if(width >= FLOAT_MAX_BUF)
        width = FLOAT_MAX_BUF - 1;

    if(flags & FLAGS_FFORMAT)
        formatflag='f';
    else if (flags & FLAGS_ADAPT_EXP)
        formatflag='g';
    else
        formatflag='e';

    if(flags & FLAGS_UPPERCASE)
        formatflag = toupper(formatflag);

    if(flags & FLAGS_PRECISION)
    {
        sprintf(fmt, "%%%s%s%s%s%s%s*.*%c",
            (flags & FLAGS_LEFT)?"-":"",
            (flags & FLAGS_SPACE)?" ":"",
            (flags & FLAGS_PLUS)?"+":"",
            (flags & FLAGS_HASH)?"#":"",
            (flags & FLAGS_ZEROPAD)?"0":"",
            (flags & FLAGS_SQUOTE)?"'":"",
            formatflag
            );
        len=snprintf(buf, FLOAT_MAX_BUF, fmt, width, prec, value);
    }
    else
    {
        sprintf(fmt, "%%%s%s%s%s%s%s*%c",
            (flags & FLAGS_LEFT)?"-":"",
            (flags & FLAGS_SPACE)?" ":"",
            (flags & FLAGS_PLUS)?"+":"",
            (flags & FLAGS_HASH)?"#":"",
            (flags & FLAGS_ZEROPAD)?"0":"",
            (flags & FLAGS_SQUOTE)?"'":"",
            formatflag
            );
        len=snprintf(buf, FLOAT_MAX_BUF, fmt, width, value);
    }

    if(flags & FLAGS_BANG)
    {
        int nsig=0;

        if(buf[i]=='-')i++;

        while (i<len && buf[i]=='0') i++;

        //skip past first 17 digits
        while(i<len && nsig<17)
        {
            if(isdigit(buf[i++]))
                nsig++;
        }
        while(i<len)
        {
            if(isdigit(buf[i]))
                buf[i]='0';
            i++;
        }
    }
    i=0;
    while (i<len)
        out(buf[i++], buffer, idx++, maxlen);

    return idx;
}
#define pushjsonsafe do{\
    char *j=str_rp_to_json_safe(ctx, fidx, NULL,0);\
    duk_push_string(ctx, j);\
    if(width){\
        duk_json_decode(ctx, -1);\
        /* JSON.stringify(obj,null,width) */\
        duk_get_global_string(ctx, "JSON");\
        duk_push_string(ctx, "stringify");\
        duk_pull(ctx, -3);\
        duk_push_null(ctx);\
        duk_push_int(ctx, width);\
        duk_call_prop(ctx, -5, 3);\
        duk_replace(ctx, fidx);\
        duk_pop(ctx); /* JSON */\
    }else duk_replace(ctx, fidx);\
    free(j);\
}while(0)

static void anytostring(duk_context *ctx, duk_idx_t fidx, unsigned int width, uint32_t flags)
{
    // if its already a string, do nothing;
    if (!duk_is_string(ctx, fidx))
    {
        fidx = duk_normalize_index(ctx, fidx);

        if(duk_is_error(ctx, fidx))
        {
            duk_safe_to_stacktrace(ctx, fidx);
        }
        else if (duk_is_undefined(ctx, fidx))
        {
            duk_push_string(ctx,"undefined");
            duk_replace(ctx,fidx);
        }
        else /*if ( !duk_is_function(ctx, fidx) )*/
        {
            if(flags & FLAGS_BANG)
                pushjsonsafe;
            else if(duk_is_function(ctx, fidx))
            {
                duk_push_string(ctx,"{_func:true}");
                duk_replace(ctx,fidx);
            }
            else
            {
                /* JSON.stringify(obj,null,width) */
                duk_get_global_string(ctx, "JSON");
                duk_push_string(ctx, "stringify");
                duk_dup(ctx, fidx);
                duk_push_null(ctx);
                duk_push_int(ctx, width);
                if(duk_pcall_prop(ctx, -5, 3))
                {
                    //cyclic error
                    duk_pop_2(ctx); //JSON, err
                    pushjsonsafe;
                }
                else
                {
                    duk_replace(ctx, fidx);
                    duk_pop(ctx); /* JSON */
                }
            }
        }
    }
}
#define njpal 4
static Jcolors jpal[njpal] = {
    { C_RESET_DEF, C_KEY_DEF,          C_STRING_DEF,     C_NUMBER_DEF,    C_BOOL_DEF,       C_NULL_DEF      },
    { C_RESET_DEF, C_CYAN,             C_BLUE,           C_YELLOW,        C_MAGENTA,        C_RED           },
    { C_RESET_DEF, C_BRIGHT_BLUE,      C_BRIGHT_GREEN,   C_BRIGHT_CYAN,   C_BRIGHT_YELLOW,  C_BRIGHT_MAGENTA},
    { C_RESET_DEF, C_BOLD_BRIGHT_CYAN, C_BRIGHT_BLUE,    C_BRIGHT_YELLOW, C_BRIGHT_MAGENTA, C_BRIGHT_RED    }
};



int rp_printf(out_fct_type out, char *buffer, const size_t maxlen, duk_context *ctx, duk_idx_t fidx, pthread_mutex_t *lock_p)
{
    uint32_t flags;
    unsigned int width, precision, n;
    size_t idx = 0U;
    duk_size_t len=0;
    int preserveUfmt = 0;
    char **free_ptr=NULL;
    int nfree=0;
    int isterm=0;

    // we don't use _out_char in rampart.utils
    // this is only for %M
    if(out == _fout_char && ((FILE*)buffer==stdout || (FILE*)buffer==stderr))
    {
        const char *term = getenv("TERM");
        isterm = (term && isatty(STDOUT_FILENO) && strcmp(term, "dumb"));
        errno=0; //isatty sets this, and that can mess stuff up later.
    }
    
    duk_idx_t topidx = duk_get_top_index(ctx);
    const char	*format = PF_REQUIRE_BUF_OR_STRING(ctx, fidx++, &len),
                *format_end=format+len;
    if (!buffer)
    {
        // use null output function
        out = _out_null;
    }
    while (format<format_end)
    {
        CCODES *ccodes = NULL;
        const char *colstr=NULL;
        char colorflag='a';
        int jpal_idx=0;

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
            // throw error if no var for % format
            if(*format!='%' && fidx>topidx)
                RP_THROW(ctx, "printf(): expecting argument for conversion specifier (variable required at position %d)", (int)fidx);
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
            case '\'':
                flags |= FLAGS_SQUOTE;
                format++;
                n = 1U;
                break;
            case 'a':
                flags |= FLAGS_COLOR;
                colorflag='a';
                format++;
                n = 1U;
                break;
            case 'A':
                flags |= FLAGS_COLOR_FORCE | FLAGS_COLOR; //force 256 by default
                colorflag='A';
                format++;
                n = 1U;
                break;
            case '@':
                flags |= FLAGS_COLOR_FORCE_TRUECOLOR | FLAGS_COLOR | FLAGS_COLOR_FORCE;
                colorflag='@';
                format++;
                n = 1U;
                break;
            case '^':
                flags |= FLAGS_COLOR_FORCE_16 | FLAGS_COLOR | FLAGS_COLOR_FORCE;
                colorflag='^';
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
            const int w = PF_REQUIRE_INT(ctx, fidx++);
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
                const int prec = PF_REQUIRE_INT(ctx, fidx++);
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

        // colors on term
        if(*format != 'J')
        {
            if( (flags & FLAGS_COLOR && isterm) || flags & FLAGS_COLOR_FORCE )
            {
                colstr=PF_REQUIRE_STRING(ctx, fidx,
                    "the '%c' modifer requires a String at format string argument %d", colorflag, fidx);
                fidx++;

                ccodes=new_color_codes();
                ccodes->flags = CCODE_FLAG_HAVE_NAME | CCODE_FLAG_WANT_TERM | CCODE_FLAG_WANT_BKGND;

                if(flags & FLAGS_COLOR_FORCE_TRUECOLOR)
                    ccodes->flags = ccodes->flags | CCODE_FLAG_FORCE_TERM | CCODE_FLAG_FORCE_TERM_TRUECOLOR;

                else if(flags & FLAGS_COLOR_FORCE_16)
                    ccodes->flags = ccodes->flags | CCODE_FLAG_FORCE_TERM | CCODE_FLAG_FORCE_TERM_16;

                else if(flags & FLAGS_COLOR_FORCE)
                    ccodes->flags = ccodes->flags | CCODE_FLAG_FORCE_TERM | CCODE_FLAG_FORCE_TERM_256;


                ccodes->lookup_names=colstr;

                if(rp_color_convert(ccodes))
                    PF_THROW(ctx, "printf: error parsing color string");

                if( *(ccodes->term_start) )
                    idx=outs(out, buffer, idx, maxlen, ccodes->term_start);
            }
            else if( flags & FLAGS_COLOR)
            {
                // later for html, but if not %H we still need to do fidx++
                colstr=PF_REQUIRE_STRING(ctx, fidx,
                    "the '%c' modifer requires a String at format string argument %d", colorflag, fidx);
                fidx++;
            }
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
                    const long long value = (long long)PF_REQUIRE_NUMBER(ctx, fidx++);
                    idx = _ntoa_long_long(out, buffer, idx, maxlen, (unsigned long long)(value > 0 ? value : 0 - value), value < 0, base, precision, width, flags);
                }
                else if (flags & FLAGS_LONG)
                {
                    const long value = (long)PF_REQUIRE_NUMBER(ctx, fidx++);
                    idx = _ntoa_long(out, buffer, idx, maxlen, (unsigned long)(value > 0 ? value : 0 - value), value < 0, base, precision, width, flags);
                }
                else
                {
                    const int value = (flags & FLAGS_CHAR) ? (char)PF_REQUIRE_INT(ctx, fidx++) : (flags & FLAGS_SHORT) ? (short int)PF_REQUIRE_INT(ctx, fidx++) : PF_REQUIRE_INT(ctx, fidx++);
                    idx = _ntoa_long(out, buffer, idx, maxlen, (unsigned int)(value > 0 ? value : 0 - value), value < 0, base, precision, width, flags);
                }
            }
            else
            {
                // unsigned
                if (flags & FLAGS_LONG_LONG)
                {
                    idx = _ntoa_long_long(out, buffer, idx, maxlen, (unsigned long long)PF_REQUIRE_NUMBER(ctx, fidx++), false, base, precision, width, flags);
                }
                else if (flags & FLAGS_LONG)
                {
                    idx = _ntoa_long(out, buffer, idx, maxlen, (unsigned long)PF_REQUIRE_NUMBER(ctx, fidx++), false, base, precision, width, flags);
                }
                else
                {
                    const unsigned int value = (flags & FLAGS_CHAR) ? (unsigned char)PF_REQUIRE_NUMBER(ctx, fidx++) : (flags & FLAGS_SHORT) ? (unsigned short int)PF_REQUIRE_NUMBER(ctx, fidx++) : (unsigned int)PF_REQUIRE_NUMBER(ctx, fidx++);
                    idx = _ntoa_long(out, buffer, idx, maxlen, value, false, base, precision, width, flags);
                }
            }
            format++;
            break;
        }
        case 'f':
        case 'F':
            flags |= FLAGS_FFORMAT;
            if (*format == 'F')
                flags |= FLAGS_UPPERCASE;
            idx = _ftoa(out, buffer, idx, maxlen, PF_REQUIRE_NUMBER(ctx, fidx++), precision, width, flags);
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
            //idx = _etoa(out, buffer, idx, maxlen, PF_REQUIRE_NUMBER(ctx, fidx++), precision, width, flags);
            idx = _ftoa(out, buffer, idx, maxlen, PF_REQUIRE_NUMBER(ctx, fidx++), precision, width, flags);
            format++;
            break;
        case 'c':
        {
            unsigned int l = 1U;
            unsigned char c='\0';

            if (duk_is_string(ctx, fidx))
            {
                c=*(duk_get_string(ctx, fidx++));
            }
            else if (duk_is_number(ctx, fidx))
            {
                int n=(int)duk_get_number(ctx, fidx);
                if(n<0 || n>255)
                     RP_THROW(ctx, "string (single char) or number (0-255) required in format string argument %d", fidx);
                c=(unsigned char)n;
                fidx++;
            }
            else
                RP_THROW(ctx, "string (single char) or number (0-255) required in format string argument %d", fidx);

            // pre padding
            if (!(flags & FLAGS_LEFT))
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            // char output
            out(c, buffer, idx++, maxlen);
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
        /* -AJF: added M, C, B, U, P and J for multi-char, base64, urlencode, prettyprint and JSON.stringify */
        case 'M':
        {
            REQUIRE_ARRAY(ctx, fidx, "Array required in format string argument %d", fidx);
            duk_uarridx_t i=0,len=duk_get_length(ctx, fidx);
            const char *line=NULL;
            // ! forces 
            if (!isterm && !(flags & FLAGS_BANG))
            {
                for (i = 0; i < len; i++) {
                    duk_get_prop_index(ctx, fidx, i);
                    anytostring(ctx, -1, 0, 0); //no width, no flags
                    line=duk_get_string(ctx, -1);
                    if(line && strlen(line))
                        idx=outs(out, buffer, idx, maxlen, line);
                    out('\n', buffer, idx++, maxlen);
                    duk_pop(ctx);
                }
            }
            else
            {
                char b[8];
                idx=outs(out, buffer, idx, maxlen, "\033[s");  // Save cursor
                sprintf(b, "\033[%dA", (int)len);              // Move up
                idx=outs(out, buffer, idx, maxlen, b);
                for (i = 0; i < len; ++i) {
                    duk_get_prop_index(ctx, fidx, i);
                    anytostring(ctx, -1, 0, 0); //no width, no flags
                    line=duk_get_string(ctx, -1);
                    if (line && strlen(line)) {
                        out('\r', buffer, idx++, maxlen);
                        idx=outs(out, buffer, idx, maxlen, line);
                        idx=outs(out, buffer, idx, maxlen, "\033[K");   // Clear to end
                    }
                    out('\n', buffer, idx++, maxlen);
                }
                idx=outs(out, buffer, idx, maxlen, "\033[u");
                fflush((FILE*)buffer);
            }
            format++;
            fidx++;
            break;
        }
        case 'C':
        {
            unsigned int l = 1U;
            uint32_t c={0};

            if (duk_is_number(ctx, fidx))
            {
                uint64_t n=(uint64_t)duk_get_number(ctx, fidx);
                if(n>4294967295)
                     RP_THROW(ctx, "number (0-4294967295) required in format string argument %d", fidx);
                c=(uint32_t)n;
                fidx++;
            }
            else
                RP_THROW(ctx, "number (0-4294967295) required in format string argument %d", fidx);

            // pre padding
            if (!(flags & FLAGS_LEFT))
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            // multi-char output
            if(c>>24)
                out(c>>24, buffer, idx++, maxlen);
            if(c>>16&0xff)
                out(c>>16&0xff, buffer, idx++, maxlen);
            if(c>>8&0xff)
                out(c>>8&0xff, buffer, idx++, maxlen);
            out(c&0xff, buffer, idx++, maxlen);
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
        /* %B is now base64
        case 'B':
        {
            duk_size_t sz;
            char *b=(char *)PF_REQUIRE_BUFFER_DATA(ctx,fidx++,&sz);
            int i=0,isz=(int)sz;
            for (;i<isz;i++)
            {
                out(*(b++), buffer, idx++, maxlen);
            }
            format++;
            break;
        }
        */
        case 'B':
        {
            duk_size_t sz;
            const char *p;

            if (flags & FLAGS_BANG)
            {
                size_t i=0;
                char *s;

                s=(char*)PF_REQUIRE_BUF_OR_STRING(ctx, fidx, &sz);
                duk_dup(ctx, fidx);

                /* make it a buffer so we can translate '_' and '-' */
                if(duk_is_string(ctx, -1))
                    duk_to_fixed_buffer(ctx, -1, &sz);
                s=duk_get_buffer_data(ctx, -1, &sz);

                while(sz--)
                {
                    switch(*s){
                        case '_': *s = '/';break;
                        case '-': *s = '+';break;
                    }
                    s++;
                }

                duk_base64_decode(ctx, -1);
                p = duk_get_buffer_data(ctx, -1, &sz);
                for (;i<sz;i++)
                {
                    out(*(p++), buffer, idx++, maxlen);
                }
            }
            else
            {
                const char *s;
                if(duk_is_object(ctx, fidx))
                    duk_json_encode(ctx, fidx);

                if (duk_is_string(ctx, fidx))
                {
                    s=PF_REQUIRE_LSTRING(ctx, fidx, &sz);
                    /* push it again - PF_REQUIRE_STRING may have performed to_utf8() */
                    duk_push_string(ctx, s);
                }
                else /* buffer */
                    duk_dup(ctx, fidx);

                p = duk_base64_encode(ctx, -1);
                if(width)
                {
                    int i=0;
                    while ((*p != 0) && (!(flags & FLAGS_PRECISION) || precision--))
                    {
                        if( *p=='=' && (flags & FLAGS_ZEROPAD) )
                        {
                            p++;
                            continue;
                        }
                        if(flags & FLAGS_LEFT)
                        {
                            if (*p=='+')
                            {
                                out('-', buffer, idx++, maxlen);
                                p++;
                            }
                            else if (*p=='/')
                            {
                                out('_', buffer, idx++, maxlen);
                                p++;
                            }
                            else
                                out(*(p++), buffer, idx++, maxlen);
                        }
                        else
                            out(*(p++), buffer, idx++, maxlen);
                        i++;
                        if(!(i%width))
                            out('\n', buffer, idx++, maxlen);
                    }
                }
                else
                {
                    while ((*p != 0) && (!(flags & FLAGS_PRECISION) || precision--))
                    {
                        if( *p=='=' && (flags & FLAGS_ZEROPAD) )
                        {
                            p++;
                            continue;
                        }
                        if(flags & FLAGS_LEFT)
                        {
                            if (*p=='+')
                            {
                                out('-', buffer, idx++, maxlen);
                                p++;
                            }
                            else if (*p=='/')
                            {
                                out('_', buffer, idx++, maxlen);
                                p++;
                            }
                            else
                                out(*(p++), buffer, idx++, maxlen);
                        }
                        else
                            out(*(p++), buffer, idx++, maxlen);
                    }
                }
            }
            duk_pop(ctx);
            format++;
            fidx++;
            break;
        }
        case 'U':
        {
            duk_size_t sz;
            const char *s;
            char *u;
            int len=0;
            if (flags & FLAGS_BANG)
            {
                s = PF_REQUIRE_LSTRING(ctx, fidx, &sz);
                len = (int)sz;
                u = duk_rp_url_decode((char *)s, &len);
            }
            else
            {
                if(duk_is_object(ctx, -1))
                    duk_json_encode(ctx, -1);
                s = PF_REQUIRE_LSTRING(ctx, fidx, &sz);
                u = duk_rp_url_encode((char *)s, (int)sz);
                len = (int) strlen(u);
            }
            /* prevent double url encoding on second pass in sprintf*/
            if (!buffer)
            {
                preserveUfmt = 1;
                duk_dup(ctx, fidx);
            }
            duk_push_lstring(ctx, u, (duk_size_t) len);
            free(u);
            duk_replace(ctx, fidx);
            goto string;
        }

        /* pretty print text */
#define printindent if(width) do{\
    unsigned int i=0;\
    while (i++ < width) \
        out(' ', buffer, idx++, maxlen);\
    lpos+=width;\
} while(0);
        case 'w':
        {
            width=0;
            precision=UINT_MAX;
            flags |= FLAGS_BANG|FLAGS_LEFT|FLAGS_PRECISION;
            /* fall through to 'P' */
        }
        case 'P':
        {
            unsigned int l = 80, lpos = 0;
            const char *p;
            int lastwasn=0, respectnl=0, useleadingindent=0;
            if (flags & FLAGS_BANG)
                respectnl=1;

            if(duk_is_buffer(ctx, fidx))
                p = duk_buffer_to_string(ctx, fidx);
            else
                p = PF_REQUIRE_STRING(ctx, fidx, "String required at format string argument %d", fidx);

            fidx++;

            if (flags & FLAGS_PRECISION)
                l = precision;

            /* get width from current indent if not specified */
            if(!width)
            {
                useleadingindent=1;
                while(isspace(*p) && *p!='\n')
                    width++,p++;
            }
            if(flags & FLAGS_LEFT  && flags & FLAGS_BANG)
            {
                while(isspace(*p) && *p!='\n')
                    p++;
                useleadingindent=0;
                width=0;
            }
            while (*p)
            {
                const char *rp=p;
                unsigned int wlen=0;

                if(!lpos)
                    printindent;

                if(isspace(*p))
                {
                    /* always respect double newline */
                    if(*p=='\n' && *(p+1)=='\n')
                    {
                        out('\n', buffer, idx++, maxlen);
                        out('\n', buffer, idx++, maxlen);
                        lpos=0;
                        p++;
                        p++;
                        lastwasn=1;
                        if(useleadingindent)
                        {
                            /* calculate a new indent level */
                            width=0;
                            while(isspace(*p))
                                width++,p++;
                        }
                    }
                    else if(lpos < l && !(respectnl && *p=='\n') )
                    {
                        out(' ', buffer, idx++, maxlen);
                        lpos++;
                        p++;
                        lastwasn=0;
                    }
                    else if(respectnl)
                    {
                        while(isspace(*p)) p++;
                        out('\n', buffer, idx++, maxlen);
                        lpos=0;
                        lastwasn=1;
                    }
                    else
                    /* space coincides with end of line */
                    {
                        out('\n', buffer, idx++, maxlen);
                        p++;
                        lpos=0;
                        lastwasn=1;
                    }
                }
                else
                {
                    /* get word length */
                    while(  *rp && !isspace( *(rp++) )  ) wlen++;
                    /*if it doesn't fits */
                    if(l < lpos+wlen)
                    {
                        /* newline might be already printed above */
                        if(!lastwasn)
                        {
                            out('\n', buffer, idx++, maxlen);
                            lpos=0;
                            printindent;
                        }
                    }
                    lpos+=wlen;
                    /* output word */
                    while (wlen--)
                        out(*(p++), buffer, idx++, maxlen);
                    lastwasn=0;
                }
            }
            format++;
        }
        break;

        /* handle JSON */

        case 'J':
            json:
            {
                if( !ccodes && ( (flags & FLAGS_COLOR && isterm) || flags & FLAGS_COLOR_FORCE) )
                {
                    jpal_idx = duk_get_int_default(ctx, fidx, 0);
                    if(jpal_idx < 0 || jpal_idx >= njpal)
                        jpal_idx=0;

                    fidx++;

                    switch( rp_gettype(ctx, fidx) ){
                        case RP_TYPE_ARRAY:
                        case RP_TYPE_OBJECT:
                            break;
                        case RP_TYPE_NUMBER:
                            idx=outs(out, buffer, idx, maxlen, jpal[jpal_idx].NUMBER); 
                            jpal_idx=-1;
                            break;
                        case RP_TYPE_BOOLEAN:
                            idx=outs(out, buffer, idx, maxlen, jpal[jpal_idx].BOOL); 
                            jpal_idx=-1;
                            break;
                        case RP_TYPE_NULL:
                            idx=outs(out, buffer, idx, maxlen, jpal[jpal_idx].NULLC); 
                            jpal_idx=-1;
                            break;
                        default:
                            idx=outs(out, buffer, idx, maxlen, jpal[jpal_idx].STRING);
                            jpal_idx=-1;
                            break;
                    }

                }

                anytostring(ctx, fidx, width, flags);

                if( jpal_idx != -1 && !ccodes && ( (flags & FLAGS_COLOR && isterm) || flags & FLAGS_COLOR_FORCE) )
                {
                    const char *j = duk_get_string(ctx, fidx);
                    char *jout = print_json_colored (j, width, COLORS, &jpal[jpal_idx]);

                    duk_push_string(ctx, jout);
                    free(jout);
                    duk_replace(ctx, fidx);
                }
            }
            //no ++
            /* fall through */

        /* NO COERSION for upper case 'S' */
        case 'S':
        string:
        {
            duk_size_t len;
            const char *p = PF_REQUIRE_LSTRING(ctx, fidx++, &len);
            //unsigned int l = _strnlen_s(p, precision ? precision : (size_t)-1);
            unsigned int l = (unsigned int) len;
            // pre padding
            if (flags & FLAGS_PRECISION)
                l = (l < precision ? l : precision);
            else
                precision = l;

            if (!(flags & FLAGS_LEFT))
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            // string output
            while (precision--)
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
        /* html esc/unesc */
        case 'H':
        {
            unsigned int l = 0;

            // string output
            if (flags & FLAGS_BANG)
            {
                const char *p = PF_REQUIRE_STRING(ctx, fidx++, "String required at format string argument %d", fidx);
                l = _strnlen_s(p, precision ? precision : (size_t)-1);

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


                char *u_p, *free_p;
                if(!p) p="null";
                u_p = free_p = strdup(p);

                decode_html_entities_utf8(u_p, NULL);
                while ((*u_p != 0) && (!(flags & FLAGS_PRECISION) || precision--))
                {
                    out(*(u_p++), buffer, idx++, maxlen);
                }
                free(free_p);
            }
            else
            {
                const char *p = NULL;
                if( flags & FLAGS_COLOR )
                {
                    if(!ccodes)
                    {
                        if(!colstr)
                        {
                            colstr=PF_REQUIRE_STRING(ctx, fidx,
                                "the '%c' modifer requires a String at format string argument %d", colorflag, fidx);
                            fidx++;
                        }
                        ccodes=new_color_codes();

                        ccodes->flags = CCODE_FLAG_HAVE_NAME | CCODE_FLAG_WANT_BKGND | CCODE_FLAG_WANT_HTMLTEXT;
                        if(flags & FLAGS_PLUS)
                            ccodes->flags |= CCODE_FLAG_WANT_CSSCOLOR;
                        ccodes->lookup_names=colstr;

                        if(rp_color_convert(ccodes))
                            PF_THROW(ctx, "printf: error parsing color string");
                    }
                    else
                    { // we've already calculated the colors above for term
                      // so now just fill in the html
                        ccodes->flags = CCODE_FLAG_WANT_HTMLTEXT;
                        if(flags & FLAGS_PLUS)
                            ccodes->flags |= CCODE_FLAG_WANT_CSSCOLOR;
                        rp_color_convert(ccodes);
                    }
                    idx=outs(out, buffer, idx, maxlen, ccodes->html_start);
                }

                p = PF_REQUIRE_STRING(ctx, fidx++, "String required at format string argument %d", fidx);
                l = _strnlen_s(p, precision ? precision : (size_t)-1);

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

                while ((*p != 0) && (!(flags & FLAGS_PRECISION) || precision--))
                {
                    switch(*p)
                    {
                        case '&':
                            out('&', buffer, idx++, maxlen);
                            out('a', buffer, idx++, maxlen);
                            out('m', buffer, idx++, maxlen);
                            out('p', buffer, idx++, maxlen);
                            out(';', buffer, idx++, maxlen);
                            break;
                        case '<':
                            out('&', buffer, idx++, maxlen);
                            out('l', buffer, idx++, maxlen);
                            out('t', buffer, idx++, maxlen);
                            out(';', buffer, idx++, maxlen);
                            break;
                        case '>':
                            out('&', buffer, idx++, maxlen);
                            out('g', buffer, idx++, maxlen);
                            out('t', buffer, idx++, maxlen);
                            out(';', buffer, idx++, maxlen);
                            break;
                        case '"':
                            out('&', buffer, idx++, maxlen);
                            out('q', buffer, idx++, maxlen);
                            out('u', buffer, idx++, maxlen);
                            out('o', buffer, idx++, maxlen);
                            out('t', buffer, idx++, maxlen);
                            out(';', buffer, idx++, maxlen);
                            break;
                        case '\'':
                            out('&', buffer, idx++, maxlen);
                            out('#', buffer, idx++, maxlen);
                            out('3', buffer, idx++, maxlen);
                            out('9', buffer, idx++, maxlen);
                            out(';', buffer, idx++, maxlen);
                            break;
                        case '/':
                            out('&', buffer, idx++, maxlen);
                            out('#', buffer, idx++, maxlen);
                            out('4', buffer, idx++, maxlen);
                            out('7', buffer, idx++, maxlen);
                            out(';', buffer, idx++, maxlen);
                            break;
                        default:
                            out(*(p), buffer, idx++, maxlen);
                            break;
                    }
                    p++;
                }
            }
            // post padding
            if (flags & FLAGS_LEFT)
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }

            if(ccodes)
                idx=outs(out, buffer, idx, maxlen, ccodes->html_end);

            format++;
            break;
        }
        /* 's' == with coersion/conversion */
        case 's':
        {
            const char *p;
            char *freeme=NULL;
            unsigned int l, max=-1;
            int isbuf = 0;
            /* convert buffers and print as is */
            if (duk_is_buffer_data(ctx, fidx))
            {
                duk_size_t ln;
                p = duk_get_buffer_data(ctx, fidx++, &ln);
                l = max = (unsigned int) ln;
                isbuf=1;
            }
            /* error objects
            else if (duk_is_error(ctx, fidx))
            {
                p=rp_push_error(ctx, fidx, NULL, rp_print_error_lines);
                duk_replace(ctx, fidx);        
            }*/
            /* convert json as above in '%J' */
            else if (duk_is_object(ctx, fidx) && !duk_is_function(ctx, -1))
            {
                goto json;
            }
            else
            /* everything else is coerced */
            {
                p = duk_safe_to_string(ctx, fidx++);

                l = _strnlen_s(p, precision ? precision : (size_t)-1);
            }
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
            if(!isbuf && strchr(p,0xED))
                p=freeme=to_utf8(p);
            // string output
            if (max==-1)
            {
                while (*p != 0  && (!(flags & FLAGS_PRECISION) || precision--))
                {
                    out(*(p++), buffer, idx++, maxlen);
                }
            }
            else
            {
                unsigned int oc=0;

                while (oc<max && (!(flags & FLAGS_PRECISION) || precision--))
                {
                    out(*(p++), buffer, idx++, maxlen);
                    oc++;
                }
            }
            // post padding
            if (flags & FLAGS_LEFT)
            {
                while (l++ < width)
                {
                    out(' ', buffer, idx++, maxlen);
                }
            }
            if(freeme)
                free(freeme);
            format++;
            break;
        }
        case '%':
            out('%', buffer, idx++, maxlen);
            format++;
            break;
        default:
            RP_THROW(ctx, "invalid format specifier character '%c' in format", *format);
        }
        // end color
        if( ccodes && ((flags & FLAGS_COLOR && isterm) || flags & FLAGS_COLOR_FORCE))
        {
            if( *(ccodes->term_end) )
                idx=outs(out, buffer, idx, maxlen, ccodes->term_end);
        }
        else if( jpal_idx == -1)
        {
            idx=outs(out, buffer, idx, maxlen, jpal[0].RESET);
        }
        if(ccodes)
            ccodes=free_color_codes(ccodes);
    }
    FREE_PTRS;
    // termination
    //out((char)0, buffer, idx < maxlen ? idx : maxlen - 1U, maxlen);
    // return written chars without terminating \0

    return (int)idx;
}
///////////////////////////////////////////////////////////////////////////////
// END tiny printf
// FROM: https://github.com/mpaland/printf
// MODIFIED BY Aaron Flin for use in duktape
///////////////////////////////////////////////////////////////////////////////
