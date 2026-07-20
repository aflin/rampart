/* vec_value.c -- self-describing chunked-vector VALUES.
 *
 * A vector value produced by chunkembed() may carry an in-band header
 * ahead of its cells:
 *
 *   [ magic(8) hdrBytes(4) k(4) dim(4) dtype(1) version(1) flags(1)
 *     pad(1) | spans: k x {start,end} uint32 pairs (flags&1) |
 *     pad to hdrBytes ] [ k*dim cells ... ]
 *
 * The header makes the value SELF-DESCRIBING: chunk count, per-vector
 * dimension, element type, and (optionally) each chunk's byte span in
 * the source document -- so the 5-arg vec abstract() can seed its
 * snippet from the winning chunk without re-running the chunker, and
 * custom (caller-supplied) chunkings keep best-chunk snippets too.
 *
 * DESIGN RULES:
 *  - Values WITHOUT the header behave exactly as before (every reader
 *    treats the header as optional, per value).  Old tables need no
 *    migration.
 *  - hdrBytes is always a multiple of 8, so cell counts stay integral
 *    for every element width (1/2/4/8) when a whole value's byte size
 *    is divided by elsz.
 *  - Validation is self-contained: magic alone never decides -- the
 *    size arithmetic (hdrBytes + k*dim*elsz(dtype) == total bytes)
 *    must hold, making accidental matches on real cell data
 *    effectively impossible.
 *  - The header survives byte-identical moves (varvec <-> varbyte).
 *    Cell-TRANSFORMING conversions (dtype changes) strip it (see
 *    fobyby): the safe rule is "header follows the exact bytes".
 *
 * This file is in BUILD_SRCS (linked into the lean `texisbuild` too):
 * it is pure byte inspection with no vec/faiss/BLAS dependencies.
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "txtypes.h"
#include "os.h"
#include "vecvalue.h"

static const byte TXVEC_HDR_MAGIC[8] =
    { 0xFF, 'T', 'X', 'v', 'e', 'c', 'H', '1' };

size_t
TXvecHdrElsz(int dtypeCode)
{
    switch (dtypeCode)
    {
    case TXVEC_HDR_DT_F16:
    case TXVEC_HDR_DT_BF16: return 2;
    case TXVEC_HDR_DT_F32:  return 4;
    case TXVEC_HDR_DT_F64:  return 8;
    case TXVEC_HDR_DT_I8:
    case TXVEC_HDR_DT_U8:   return 1;
    }
    return 0;
}

/* Header size for k chunks (fixed part + optional span pairs), padded
 * to a multiple of 8. */
size_t
TXvecHdrSize(size_t k, int withSpans)
{
    size_t sz = TXVEC_HDR_FIXED_BYTES;
    if (withSpans) sz += k * 2 * sizeof(EPI_UINT32);
    return (sz + 7) & ~(size_t)7;
}

/* Decode `data`/`nbytes` (one vector value).  Always fills *out:
 *  - header present + valid: returns 1; out->k/dim/dtype/spans set,
 *    out->cells / out->ncells cover the cells only.
 *  - plain value: returns 0; out->cells = data, out->ncells =
 *    nbytes / elszHint (0 if elszHint is 0), k/dim/dtype/spans zeroed.
 * elszHint is only used for plain values (the caller's column
 * context); header values are validated against their OWN dtype. */
int
TXvecValDecode(const void *data, size_t nbytes, size_t elszHint,
               TXvecValInfo *out)
{
    const byte *d = (const byte *)data;
    EPI_UINT32 hdrBytes, k, dim;
    byte        dtype, version, flags;
    size_t      elsz;

    memset(out, 0, sizeof(*out));
    out->cells  = data;
    out->ncells = (elszHint > 0 && data) ? nbytes / elszHint : 0;

    if (!data || nbytes < TXVEC_HDR_FIXED_BYTES) return 0;
    if (memcmp(d, TXVEC_HDR_MAGIC, sizeof(TXVEC_HDR_MAGIC)) != 0) return 0;

    memcpy(&hdrBytes, d + 8, sizeof(hdrBytes));
    memcpy(&k,        d + 12, sizeof(k));
    memcpy(&dim,      d + 16, sizeof(dim));
    dtype   = d[20];
    version = d[21];
    flags   = d[22];

    if (version != 1) return 0;
    if (hdrBytes < TXVEC_HDR_FIXED_BYTES || (hdrBytes & 7) != 0 ||
        (size_t)hdrBytes > nbytes)
        return 0;
    elsz = TXvecHdrElsz((int)dtype);
    if (elsz == 0 || k == 0 || dim == 0) return 0;
    /* overflow-safe size check */
    if ((size_t)k > (nbytes - hdrBytes) / elsz / dim) return 0;
    if (nbytes - (size_t)hdrBytes != (size_t)k * dim * elsz) return 0;
    /* spans bound: k pairs of 2 EPI_UINT32 must fit inside hdrBytes.
     * Divide, don't multiply: TXvecHdrSize(k,1)'s k*8 product can wrap
     * on 32-bit size_t for crafted k (~2^29), passing a bogus header
     * whose spans pointer then reads far out of bounds downstream. */
    if ((flags & TXVEC_HDR_F_SPANS) &&
        (size_t)k > ((size_t)hdrBytes - TXVEC_HDR_FIXED_BYTES) /
                    (2 * sizeof(EPI_UINT32)))
        return 0;

    out->cells    = d + hdrBytes;
    out->ncells   = (size_t)k * dim;
    out->k        = k;
    out->dim      = dim;
    out->dtype    = (int)dtype;
    out->hdrBytes = hdrBytes;
    if (flags & TXVEC_HDR_F_SPANS)
        out->spans = (const EPI_UINT32 *)(d + TXVEC_HDR_FIXED_BYTES);
    return 1;
}

/* Write a header into `buf` (which must have room for
 * TXvecHdrSize(k, spans != NULL) bytes).  spans, when given, is k
 * {start,end} pairs.  Returns the header size written. */
size_t
TXvecHdrWrite(void *buf, size_t k, size_t dim, int dtypeCode,
              const EPI_UINT32 *spans)
{
    byte       *d = (byte *)buf;
    size_t      hdrBytes = TXvecHdrSize(k, spans != NULL);
    EPI_UINT32 u32;

    memset(d, 0, hdrBytes);
    memcpy(d, TXVEC_HDR_MAGIC, sizeof(TXVEC_HDR_MAGIC));
    u32 = (EPI_UINT32)hdrBytes; memcpy(d + 8,  &u32, sizeof(u32));
    u32 = (EPI_UINT32)k;        memcpy(d + 12, &u32, sizeof(u32));
    u32 = (EPI_UINT32)dim;      memcpy(d + 16, &u32, sizeof(u32));
    d[20] = (byte)dtypeCode;
    d[21] = 1;                                   /* version */
    d[22] = spans ? TXVEC_HDR_F_SPANS : 0;       /* flags */
    if (spans)
        memcpy(d + TXVEC_HDR_FIXED_BYTES, spans,
               k * 2 * sizeof(EPI_UINT32));
    return hdrBytes;
}

/* Convenience for readers whose counts are CELLS of element size
 * `elsz` (typed varvec fields), not bytes: decode and, when a header
 * is present, advance *cellsPtr / reduce *ncellsPtr past it.
 * Returns 1 when a header was skipped. */
int
TXvecValSkipHdrCells(void **cellsPtr, size_t *ncellsPtr, size_t elsz)
{
    TXvecValInfo vvi;

    if (!cellsPtr || !*cellsPtr || !ncellsPtr || elsz == 0) return 0;
    if (!TXvecValDecode(*cellsPtr, *ncellsPtr * elsz, elsz, &vvi)) return 0;
    *cellsPtr = (void *)vvi.cells;
    *ncellsPtr -= vvi.hdrBytes / elsz;
    return 1;
}
