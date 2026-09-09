/* charconv() for the vendored libetpan MIME subset, backed by rampart's own
 * charset layer instead of libetpan's.
 *
 * Upstream ships src/data-types/charconv.c: ~1100 lines wrapping iconv, ICU
 * and CoreFoundation behind #ifdefs, and it fails SILENTLY when built with
 * none of them -- an RFC 2047 Subject then comes back as the raw
 * "=?utf-8?b?...?=" with no error anywhere.  It is not vendored.  rampart
 * already has rp_charset_to_utf8() (rampart-utils.c), already links iconv,
 * and already has a no-iconv fallback, so routing through it means ONE
 * charset stack for the whole module rather than two that can disagree.
 *
 * Only two files in the subset call this: mailmime_decode.c (RFC 2047
 * encoded-words) and mailmime_rfc2231.c (RFC 2231 parameter values).
 *
 * Contract, from those callers: return MAIL_CHARCONV_NO_ERROR and set
 * *result to a NUL-terminated buffer they will free() themselves.  Returning
 * MAIL_CHARCONV_ERROR_UNKNOWN_CHARSET is useful rather than fatal -- the
 * caller retries as iso-8859-1, which is the right guess for mislabelled
 * mail.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rampart.h"
#include <libetpan/charconv.h>

int charconv(const char *tocode, const char *fromcode,
             const char *str, size_t length,
             char **result)
{
    rp_charset_result cs;
    const char *err = NULL;
    char *out;

    if(!result)
        return MAIL_CHARCONV_ERROR_CONV;
    *result = NULL;

    /* The subset only ever converts INTO utf-8; anything else is a caller we
       did not vendor, and guessing would be worse than saying so. */
    if(tocode && strcasecmp(tocode, "utf-8") && strcasecmp(tocode, "utf8"))
        return MAIL_CHARCONV_ERROR_UNKNOWN_CHARSET;

    if(rp_charset_to_utf8((const unsigned char *)str, length, fromcode, 0,
                          &cs, &err) != 0)
        return MAIL_CHARCONV_ERROR_UNKNOWN_CHARSET;

    /* libetpan's callers free() what they get, so hand them their own copy
       rather than something rp_charset_result_free() also owns. */
    out = malloc(cs.len + 1);
    if(!out)
    {
        rp_charset_result_free(&cs);
        return MAIL_CHARCONV_ERROR_MEMORY;
    }
    memcpy(out, cs.text, cs.len);
    out[cs.len] = 0;
    rp_charset_result_free(&cs);

    *result = out;
    return MAIL_CHARCONV_NO_ERROR;
}

/* Upstream declares these alongside charconv(); the subset does not call
 * them, but the header is vendored verbatim so the symbols must exist if
 * anything ever links against it expecting them. */
int charconv_buffer(const char *tocode, const char *fromcode,
                    const char *str, size_t length,
                    char **result, size_t *result_len)
{
    char *s;
    int r = charconv(tocode, fromcode, str, length, &s);

    if(r != MAIL_CHARCONV_NO_ERROR)
        return r;
    *result = s;
    if(result_len)
        *result_len = strlen(s);
    return MAIL_CHARCONV_NO_ERROR;
}

void charconv_buffer_free(char *str)
{
    free(str);
}
