#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef EPI_UNISTD_H
#  include <unistd.h>
#endif /* EPI_UNISTD_H */
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <errno.h>
#include "texint.h"

#undef word

/* Unix compress(1) code.  Derived from public domain ncompress (CVS'd
 * in devtools).  Not to be confused with zlib compress(), which is a
 * different format (wrapper around deflate method).
 */

#define  LARGS(a)  ()  /* Relay on include files for libary func defs. */


#undef  min
#define  min(a,b)  ((a>b) ? b : a)

/* KNG we are not reading/writing direct to files, so we can enforce
 * consistent/reasonable buffer sizes:
 */
#define IBUFSIZ 8192                            /* input buffer size */
#define OBUFSIZ 8192                            /* output buffer size */

#define MAXPATHLEN 1024    /* MAXPATHLEN - maximum length of a pathname we allow   */
#define  SIZE_INNER_LOOP    256  /* Size of the inter (fast) compress loop      */

              /* Defines for third byte of header           */
#define  MAGIC_1    (char_type)'\037'/* First byte of compressed file        */
#define  MAGIC_2    (char_type)'\235'/* Second byte of compressed file        */
#define BIT_MASK  0x1f      /* Mask for 'number of compresssion bits'    */
                  /* Masks 0x20 and 0x40 are free.          */
                  /* I think 0x20 should mean that there is    */
                  /* a fourth header byte (for expansion).      */
#define BLOCK_MODE  0x80      /* Block compresssion if table is full and    */
                  /* compression rate is dropping flush tables  */

      /* the next two codes should not be changed lightly, as they must not  */
      /* lie within the contiguous general code space.            */
#define FIRST  257          /* first free entry               */
#define  CLEAR  256          /* table clear output code             */

#define INIT_BITS 9      /* initial number of bits/code */

#ifndef SACREDMEM
  /*
    * SACREDMEM is the amount of physical memory saved for others; compress
    * will hog the rest.
    */
#  define SACREDMEM  0
#endif

#ifndef USERMEM
  /*
    * Set USERMEM to the maximum amount of physical user memory available
    * in bytes.  USERMEM is used to determine the maximum BITS that can be used
    * for compression.
   */
#  define USERMEM   5000000     /* default user memory */
#endif

#ifndef  NOALLIGN
#  define  NOALLIGN  0
#endif

/*
 * machine variants which require cc -Dmachine:  pdp11, z8000, DOS
 */

#ifdef interdata  /* Perkin-Elmer                          */
#  define SIGNED_COMPARE_SLOW  /* signed compare is slower than unsigned       */
#endif

#ifdef pdp11     /* PDP11: don't forget to compile with -i             */
#  define  BITS     12  /* max bits/code for 16-bit machine           */
#  define  NO_UCHAR    /* also if "unsigned char" functions as signed char   */
#endif /* pdp11 */

#ifdef z8000    /* Z8000:                             */
#  define  BITS   12  /* 16-bits processor max 12 bits              */
#  undef  vax      /* weird preprocessor                     */
#endif /* z8000 */

#if 0   /* KNG this assumes 16-bit land: */
#ifdef  DOS      /* PC/XT/AT (8088) processor                  */
#  define  BITS   16  /* 16-bits processor max 12 bits              */
#  undef  NOALLIGN
#  define  NOALLIGN  1
#endif /* DOS */
#endif /* 0 */

#ifdef M_XENIX
#  if BITS > 13      /* Code only handles BITS = 12, 13, or 16 */
#    define BITS  13
#  endif
#endif

#ifndef BITS    /* General processor calculate BITS                */
#  if USERMEM >= (800000+SACREDMEM)
#    define FAST
#  else
#  if USERMEM >= (433484+SACREDMEM)
#    define BITS  16
#  else
#  if USERMEM >= (229600+SACREDMEM)
#    define BITS  15
#  else
#  if USERMEM >= (127536+SACREDMEM)
#    define BITS  14
#   else
#  if USERMEM >= (73464+SACREDMEM)
#    define BITS  13
#  else
#    define BITS  12
#  endif
#  endif
#   endif
#  endif
#  endif
#endif /* BITS */

#ifdef FAST
#  define  HBITS    17      /* 50% occupancy */
#  define  HSIZE     (1<<HBITS)
#  define  HMASK     (HSIZE-1)
#  define  HPRIME     9941
#  define  BITS       16
#else
#  if BITS == 16
#    define HSIZE  69001    /* 95% occupancy */
#  endif
#  if BITS == 15
#    define HSIZE  35023    /* 94% occupancy */
#  endif
#  if BITS == 14
#    define HSIZE  18013    /* 91% occupancy */
#  endif
#  if BITS == 13
#    define HSIZE  9001    /* 91% occupancy */
#  endif
#  if BITS <= 12
#    define HSIZE  5003    /* 80% occupancy */
#  endif
#endif

#define CHECK_GAP 10000

typedef long int      code_int;

#ifdef SIGNED_COMPARE_SLOW
  typedef unsigned long int  count_int;
  typedef unsigned short int  count_short;
  typedef unsigned long int  cmp_code_int;  /* Cast to make compare faster  */
#else
  typedef long int       count_int;
  typedef long int      cmp_code_int;
#endif

typedef  unsigned char  char_type;

#define ARGVAL() (*++(*argv) || (--argc && *++argv))

#define MAXCODE(n)  (1L << (n))

#ifndef  REGISTERS
#  define  REGISTERS  2
#endif
#define  REG1  
#define  REG2  
#define  REG3  
#define  REG4  
#define  REG5  
#define  REG6  
#define  REG7  
#define  REG8  
#define  REG9  
#define  REG10
#define  REG11  
#define  REG12  
#define  REG13
#define  REG14
#define  REG15
#define  REG16
#if REGISTERS >= 1
#  undef  REG1
#  define  REG1  register
#endif
#if REGISTERS >= 2
#  undef  REG2
#  define  REG2  register
#endif
#if REGISTERS >= 3
#  undef  REG3
#  define  REG3  register
#endif
#if REGISTERS >= 4
#  undef  REG4
#  define  REG4  register
#endif
#if REGISTERS >= 5
#  undef  REG5
#  define  REG5  register
#endif
#if REGISTERS >= 6
#  undef  REG6
#  define  REG6  register
#endif
#if REGISTERS >= 7
#  undef  REG7
#  define  REG7  register
#endif
#if REGISTERS >= 8
#  undef  REG8
#  define  REG8  register
#endif
#if REGISTERS >= 9
#  undef  REG9
#  define  REG9  register
#endif
#if REGISTERS >= 10
#  undef  REG10
#  define  REG10  register
#endif
#if REGISTERS >= 11
#  undef  REG11
#  define  REG11  register
#endif
#if REGISTERS >= 12
#  undef  REG12
#  define  REG12  register
#endif
#if REGISTERS >= 13
#  undef  REG13
#  define  REG13  register
#endif
#if REGISTERS >= 14
#  undef  REG14
#  define  REG14  register
#endif
#if REGISTERS >= 15
#  undef  REG15
#  define  REG15  register
#endif
#if REGISTERS >= 16
#  undef  REG16
#  define  REG16  register
#endif


union  bytes
{
  long  word;
  struct
  {
#if EPI_LITTLE_ENDIAN
    char_type  b1;
    char_type  b2;
    char_type  b3;
    char_type  b4;
#else
#if EPI_BIG_ENDIAN
    char_type  b4;
    char_type  b3;
    char_type  b2;
    char_type  b1;
#else
    int        dummy;
#endif
#endif
  } bytes;
} ;
#if EPI_LITTLE_ENDIAN && NOALLIGN == 1
#define  output(b,o,c,n)  {                          \
              *(long *)&((b)[(o)>>3]) |= ((long)(c))<<((o)&0x7);\
              (o) += (n);                    \
            }
#else
#ifdef EPI_BIG_ENDIAN   /* KNG 20090720 wtf guess; was #ifdef BYTEORDER */
#define  output(b,o,c,n)  {  REG1 char_type  *p = &(b)[(o)>>3];        \
              union bytes i;                  \
              i.word = ((long)(c))<<((o)&0x7);        \
              p[0] |= i.bytes.b1;                \
              p[1] |= i.bytes.b2;                \
              p[2] |= i.bytes.b3;                \
              (o) += (n);                    \
            }
#else
#define  output(b,o,c,n)  {  REG1 char_type  *p = &(b)[(o)>>3];        \
              REG2 long     i = ((long)(c))<<((o)&0x7);  \
              p[0] |= (char_type)(i);              \
              p[1] |= (char_type)(i>>8);            \
              p[2] |= (char_type)(i>>16);            \
              (o) += (n);                    \
            }
#endif
#endif
#if EPI_LITTLE_ENDIAN && NOALLIGN == 1
#define  input(b,o,c,n,m){                          \
              (c) = (*(long *)(&(b)[(o)>>3])>>((o)&0x7))&(m);  \
              (o) += (n);                    \
            }
#else
#define  input(b,o,c,n,m){  REG1 char_type     *p = &(b)[(o)>>3];      \
              (c) = ((((long)(p[0]))|((long)(p[1])<<8)|    \
                   ((long)(p[2])<<16))>>((o)&0x7))&(m);  \
              (o) += (n);                    \
            }
#endif

/* ------------------------------------------------------------------------ */

/* KNG 20090720  Stuff for objectifying this code */

typedef enum TXUCS_tag                          /* state */
{
  TXUCS_GET_MAGIC,                              /* reading 3-byte header */
  TXUCS_RESET_BUF,                              /* at resetbuf */
  TXUCS_READ_DATA,                              /* read more data */
  TXUCS_RESET_INBITS,
  TXUCS_PROCESS_DATA,
  TXUCS_WRITE_DATA,
  TXUCS_FINAL_FLUSH,
  TXUCS_DONE                                    /* all done */
}
TXUCS;
#define TXUCSPN ((TXUCS *)NULL)

/* An object to compress/decompress data: */
struct TXunixCompress_tag
{
  TXPMBUF       *pmbuf;                         /* putmsg buffer (owned) */
  TXUCS         state;                          /* current state */

  /* ncompress stuff: */
  int           maxBits;                        /* max bits per code */
  int           blockMode;
  code_int      maxMaxCode;
  code_int      maxCode;
  int           nBits;
  int           bitMask;
  code_int      oldCode;
  int           finChar;
  int           posBits;
  code_int      freeEnt;
  code_int      code;
  size_t        i;

  /* For now these MUST be the same size as global `inbuf'/`outbuf'/etc.,
   * until we get rid of the latter and hard-coded ...BUFSIZ/HSIZE refs
   * in the code.  Would like to get rid of these buffers too (or at
   * least reduce their size), and thus consume ...DoDecompress()'s
   * parameter buffers faster:
   */
  char_type     inBuf[IBUFSIZ + 64];
  size_t        inBufLen;                       /*`inBuf' data; was `insize'*/
  char_type     outBuf[OBUFSIZ + 2048];
  size_t        outBufLen;                      /*`outBuf' data;was `outpos'*/
  size_t        outBufFlushedLen;               /* for TXUCS_WRITE_DATA */

  count_int     htab[HSIZE];
  unsigned short codetab[HSIZE];
  char_type     *stackp;
  code_int      inCode;
  int           inBits;
  size_t        lastRdSize;                     /* was `rsize' */
};

/* ------------------------------------------------------------------------ */

#if 0 /* wtf remove once compress() is translated to TXunixCompress method: */
static int        block_mode = BLOCK_MODE;/* Block compress mode -C compatible with 2.0*/
static int        maxbits = BITS;    /* user settable max # bits/code         */

static char_type    inbuf[IBUFSIZ+64];  /* Input buffer                  */
static char_type    outbuf[OBUFSIZ+2048];/* Output buffer                */

static long       bytes_in;      /* Total number of byte from input        */
static long       bytes_out;      /* Total number of byte to output        */
#endif /* 0 */

/*
 * 8086 & 80286 Has a problem with array bigger than 64K so fake the array
 * For processors with a limited address space and segments.
 */
/*
 * To save much memory, we overlay the table used by compress() with those
 * used by decompress().  The tab_prefix table is the same size and type
 * as the codetab.  The tab_suffix table needs 2**BITS characters.  We
 * get this from the beginning of htab.  The output stack uses the rest
 * of htab, and contains characters.  There is plenty of room for any
 * possible stack (stack used to be 8000 characters).
 */
/* count_int    htab[HSIZE]; */
/* unsigned short  codetab[HSIZE]; */

#define  htabof(uc, i)          ((uc)->htab[i])
#define  codetabof(uc, i)       ((uc)->codetab[i])
#define  tab_prefixof(uc, i)    codetabof(uc, i)
#define  tab_suffixof(uc, i)    (((char_type *)((uc)->htab))[i])
#define  de_stack(uc)           (((char_type *)&((uc)->htab[HSIZE-1])))
#define  clear_htab(uc)         memset((uc)->htab, -1, sizeof((uc)->htab))
#define  clear_tab_prefixof(uc) memset((uc)->codetab, 0, 256);

#ifdef FAST
static CONST int primetab[256] =    /* Special secudary hash table.    */
  {
       1013, -1061, 1109, -1181, 1231, -1291, 1361, -1429,
       1481, -1531, 1583, -1627, 1699, -1759, 1831, -1889,
       1973, -2017, 2083, -2137, 2213, -2273, 2339, -2383,
       2441, -2531, 2593, -2663, 2707, -2753, 2819, -2887,
       2957, -3023, 3089, -3181, 3251, -3313, 3361, -3449,
       3511, -3557, 3617, -3677, 3739, -3821, 3881, -3931,
       4013, -4079, 4139, -4219, 4271, -4349, 4423, -4493,
       4561, -4639, 4691, -4783, 4831, -4931, 4973, -5023,
       5101, -5179, 5261, -5333, 5413, -5471, 5521, -5591,
       5659, -5737, 5807, -5857, 5923, -6029, 6089, -6151,
       6221, -6287, 6343, -6397, 6491, -6571, 6659, -6709,
       6791, -6857, 6917, -6983, 7043, -7129, 7213, -7297,
       7369, -7477, 7529, -7577, 7643, -7703, 7789, -7873,
       7933, -8017, 8093, -8171, 8237, -8297, 8387, -8461,
       8543, -8627, 8689, -8741, 8819, -8867, 8963, -9029,
       9109, -9181, 9241, -9323, 9397, -9439, 9511, -9613,
       9677, -9743, 9811, -9871, 9941,-10061,10111,-10177,
       10259,-10321,10399,-10477,10567,-10639,10711,-10789,
       10867,-10949,11047,-11113,11173,-11261,11329,-11423,
       11491,-11587,11681,-11777,11827,-11903,11959,-12041,
       12109,-12197,12263,-12343,12413,-12487,12541,-12611,
       12671,-12757,12829,-12917,12979,-13043,13127,-13187,
       13291,-13367,13451,-13523,13619,-13691,13751,-13829,
       13901,-13967,14057,-14153,14249,-14341,14419,-14489,
       14557,-14633,14717,-14767,14831,-14897,14983,-15083,
       15149,-15233,15289,-15359,15427,-15497,15583,-15649,
       15733,-15791,15881,-15937,16057,-16097,16189,-16267,
       16363,-16447,16529,-16619,16691,-16763,16879,-16937,
       17021,-17093,17183,-17257,17341,-17401,17477,-17551,
       17623,-17713,17791,-17891,17957,-18041,18097,-18169,
       18233,-18307,18379,-18451,18523,-18637,18731,-18803,
       18919,-19031,19121,-19211,19273,-19381,19429,-19477
  } ;
#endif

void    compress    ARGS((int,int));

/*****************************************************************
 * TAG( main )
 *
 * Algorithm from "A Technique for High Performance Data Compression",
 * Terry A. Welch, IEEE Computer Vol 17, No 6 (June 1984), pp 8-19.
 *
 * Algorithm:
 *   Modified Lempel-Ziv method (LZW).  Basically finds common
 *   substrings and replaces them with a variable size code.  This is
 *   deterministic, and can be done on the fly.  Thus, the decompression
 *   procedure needs no input table, but tracks the way the table was built.
 */ 

/*
 * compress fdin to fdout
 *
 * Algorithm:  use open addressing double hashing (no chaining) on the 
 * prefix code / next character combination.  We do a variant of Knuth's
 * algorithm D (vol. 3, sec. 6.4) along with G. Knott's relatively-prime
 * secondary probe.  Here, the modular division first probe is gives way
 * to a faster exclusive-or manipulation.  Also do block compression with
 * an adaptive reset, whereby the code table is cleared when the compression
 * ratio decreases, but after the table fills.  The variable-length output
 * codes are re-sized at this point, and a special CLEAR code is generated
 * for the decompressor.  Late addition:  construct the table according to
 * file size for noticeable speed improvement on small files.  Please direct
 * questions about this implementation to ames!jaw.
 */
#if 0 /* wtf change to a TXunixCompress method, ala TXunixCompressDoDecompress() */
void
compress(fdin, fdout)
  int    fdin;
  int    fdout;
  {
    REG2  long    hp;
    REG3  int      rpos;
#if REGISTERS >= 5
    REG5  long    fc;
#endif
    REG6  int      outbits;
    REG7    int      rlop;
    REG8  int      rsize;
    REG9  int      stcode;
    REG10  code_int  free_ent;
    REG11  int      boff;
    REG12  int      n_bits;
    REG13  int      ratio;
    REG14  long    checkpoint;
    REG15  code_int  extcode;
    union
    {
      long      code;
      struct
      {
        char_type    c;
        unsigned short  ent;
      } e;
    } fcode;

    ratio = 0;
    checkpoint = CHECK_GAP;
    extcode = MAXCODE(n_bits = INIT_BITS)+1;
    stcode = 1;
    free_ent = FIRST;

    memset(outbuf, 0, sizeof(outbuf));
    bytes_out = 0; bytes_in = 0;
    outbuf[0] = MAGIC_1;
    outbuf[1] = MAGIC_2;
    outbuf[2] = (char)(maxbits | block_mode);
    boff = outbits = (3<<3);
    fcode.code = 0;

    clear_htab();

    while ((rsize = read(fdin, inbuf, IBUFSIZ)) > 0)
    {
      if (bytes_in == 0)
      {
        fcode.e.ent = inbuf[0];
        rpos = 1;
      }
      else
        rpos = 0;

      rlop = 0;

      do
      {
        if (free_ent >= extcode && fcode.e.ent < FIRST)
        {
          if (n_bits < maxbits)
          {
            boff = outbits = (outbits-1)+((n_bits<<3)-
                    ((outbits-boff-1+(n_bits<<3))%(n_bits<<3)));
            if (++n_bits < maxbits)
              extcode = MAXCODE(n_bits)+1;
            else
              extcode = MAXCODE(n_bits);
          }
          else
          {
            extcode = MAXCODE(16)+OBUFSIZ;
            stcode = 0;
          }
        }

        if (!stcode && bytes_in >= checkpoint && fcode.e.ent < FIRST)
        {
          REG1 long int rat;

          checkpoint = bytes_in + CHECK_GAP;

          if (bytes_in > 0x007fffff)
          {              /* shift will overflow */
            rat = (bytes_out+(outbits>>3)) >> 8;

            if (rat == 0)        /* Don't divide by zero */
              rat = 0x7fffffff;
            else
              rat = bytes_in / rat;
          }
          else
            rat = (bytes_in << 8) / (bytes_out+(outbits>>3));  /* 8 fractional bits */
          if (rat >= ratio)
            ratio = (int)rat;
          else
          {
            ratio = 0;
            clear_htab();
            output(outbuf,outbits,CLEAR,n_bits);
            boff = outbits = (outbits-1)+((n_bits<<3)-
                    ((outbits-boff-1+(n_bits<<3))%(n_bits<<3)));
            extcode = MAXCODE(n_bits = INIT_BITS)+1;
            free_ent = FIRST;
            stcode = 1;
          }
        }

        if (outbits >= (OBUFSIZ<<3))
        {
          if (write(fdout, outbuf, OBUFSIZ) != OBUFSIZ)
            write_error();

          outbits -= (OBUFSIZ<<3);
          boff = -(((OBUFSIZ<<3)-boff)%(n_bits<<3));
          bytes_out += OBUFSIZ;

          memcpy(outbuf, outbuf+OBUFSIZ, (outbits>>3)+1);
          memset(outbuf+(outbits>>3)+1, '\0', OBUFSIZ);
        }

        {
          REG1  int    i;

          i = rsize-rlop;

          if ((code_int)i > extcode-free_ent)  i = (int)(extcode-free_ent);
          if (i > ((sizeof(outbuf) - 32)*8 - outbits)/n_bits)
            i = ((sizeof(outbuf) - 32)*8 - outbits)/n_bits;
          
          if (!stcode && (long)i > checkpoint-bytes_in)
            i = (int)(checkpoint-bytes_in);

          rlop += i;
          bytes_in += i;
        }

        goto next;
hfound:      fcode.e.ent = codetabof(hp);
next:        if (rpos >= rlop)
             goto endlop;
next2:       fcode.e.c = inbuf[rpos++];
#ifndef FAST
        {
          REG1   code_int  i;
#if REGISTERS >= 5
          fc = fcode.code;
#else
#  define      fc fcode.code
#endif
          hp = (((long)(fcode.e.c)) << (BITS-8)) ^ (long)(fcode.e.ent);

          if ((i = htabof(hp)) == fc)
            goto hfound;

          if (i != -1)
          {
            REG4 long    disp;

            disp = (HSIZE - hp)-1;  /* secondary hash (after G. Knott) */

            do
            {
              if ((hp -= disp) < 0)  hp += HSIZE;

              if ((i = htabof(hp)) == fc)
                goto hfound;
            }
            while (i != -1);
          }
        }
#else /* FAST */
        {
          REG1 long  i;
          REG4 long  p;
#if REGISTERS >= 5
          fc = fcode.code;
#else
#  define      fc fcode.code
#endif
          hp = ((((long)(fcode.e.c)) << (HBITS-8)) ^ (long)(fcode.e.ent));

          if ((i = htabof(hp)) == fc)  goto hfound;
          if (i == -1)        goto out;

          p = primetab[fcode.e.c];
lookup:        hp = (hp+p)&HMASK;
          if ((i = htabof(hp)) == fc)  goto hfound;
          if (i == -1)        goto out;
          hp = (hp+p)&HMASK;
          if ((i = htabof(hp)) == fc)  goto hfound;
          if (i == -1)        goto out;
          hp = (hp+p)&HMASK;
          if ((i = htabof(hp)) == fc)  goto hfound;
          if (i == -1)        goto out;
          goto lookup;
        }
out:      ;
#endif /* FAST */
        output(outbuf,outbits,fcode.e.ent,n_bits);

        {
#if REGISTERS < 5
#  undef  fc
          REG1 long  fc;
          fc = fcode.code;
#endif
          fcode.e.ent = fcode.e.c;


          if (stcode)
          {
            codetabof(hp) = (unsigned short)free_ent++;
            htabof(hp) = fc;
          }
        } 

        goto next;

endlop:      if (fcode.e.ent >= FIRST && rpos < rsize)
          goto next2;

        if (rpos > rlop)
        {
          bytes_in += rpos-rlop;
          rlop = rpos;
        }
      }
      while (rlop < rsize);
    }

    if (rsize < 0)
      read_error();

    if (bytes_in > 0)
      output(outbuf,outbits,fcode.e.ent,n_bits);

    if (write(fdout, outbuf, (outbits+7)>>3) != (outbits+7)>>3)
      write_error();

    bytes_out += (outbits+7)>>3;

    return;
  }
#endif /* 0 */

/*
 * Decompress stdin to stdout.  This routine adapts to the codes in the
 * file building the "string" table on-the-fly; requiring no table to
 * be stored in the compressed file.  The tables used herein are shared
 * with those of the compress() routine.  See the definitions above.
 */

static int TXunixCompressDoDecompress ARGS((TXunixCompress *uc,
    byte **inBuf, size_t inBufSz, byte **outBuf, size_t outBufSz, TXCTEHF
    flags));
static int
TXunixCompressDoDecompress(uc, inBuf, inBufSz, outBuf, outBufSz, flags)
TXunixCompress  *uc;            /* (in/out) object */
byte            **inBuf;        /* (in/out) input data to consume */
size_t          inBufSz;        /* (in) its size */
byte            **outBuf;       /* (in/out) output buffer to write to */
size_t          outBufSz;       /* (in) its size */
TXCTEHF         flags;          /* (in) flags */
/* Advances `*inBuf' past data consumed, and `*outBuf' past data output.
 * Returns 0 on error, 1 if ok, 2 if ok and output EOF reached.
 */
{
  static CONST char     fn[] = "TXunixCompressDoDecompress";
  size_t                n;
  int                   e, o, ret, keepRunning;
  byte                  *inData, *inDataEnd;
  byte                  *outData, *outDataEnd;

  inData = *inBuf;
  inDataEnd = inData + inBufSz;
  outData = *outBuf;
  outDataEnd = outData + outBufSz;

  keepRunning = 0;

  while (((uc->state == TXUCS_WRITE_DATA || uc->state == TXUCS_FINAL_FLUSH) ?
          outData < outDataEnd :
          (inData < inDataEnd || (flags & TXCTEHF_INPUT_EOF))) &&
         uc->state != TXUCS_DONE)
    switch (uc->state)
      {
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_GET_MAGIC:                     /* read 3-byte header */
        /* Copy data from `inData': */
        n = TX_MIN((size_t)(inDataEnd - inData),
                   sizeof(uc->inBuf) - uc->inBufLen);
        if (n > 0) memcpy(uc->inBuf + uc->inBufLen, inData, n);
        uc->inBufLen += n;
        inData += n;
        uc->lastRdSize = uc->inBufLen;

        if (uc->inBufLen < 3)                   /* not enough read yet */
          {
            if (flags & TXCTEHF_INPUT_EOF)
              {
                txpmbuf_putmsg(uc->pmbuf, MERR + FRE, fn,
                               "Data not in Unix compress format: truncated");
                goto err;
              }
            break;
          }
        if (uc->inBuf[0] != MAGIC_1 || uc->inBuf[1] != MAGIC_2)
          {
            txpmbuf_putmsg(uc->pmbuf, MERR + FRE, fn,
                           "Data not in Unix compress format");
            goto err;
          }

        uc->maxBits = (uc->inBuf[2] & BIT_MASK);
        uc->blockMode = (uc->inBuf[2] & BLOCK_MODE);
        uc->maxMaxCode = MAXCODE(uc->maxBits);
        if (uc->maxBits > BITS)
          {
            txpmbuf_putmsg(uc->pmbuf, MERR + FRE, fn,
                      "Data compressed with %d bits, can only handle %d bits",
                           uc->maxBits, BITS);
            goto err;
          }
        uc->maxCode = MAXCODE(uc->nBits = INIT_BITS) - 1;
        uc->bitMask = (1 << uc->nBits) - 1;
        uc->oldCode = -1;
        uc->finChar = 0;
        uc->outBufLen = 0;
        uc->posBits = 3<<3;
        uc->freeEnt = ((uc->blockMode) ? FIRST : 256);
        clear_tab_prefixof(uc);  /* As above, initialize the first
                   256 entries in the table. */
        for (uc->code = 255 ; uc->code >= 0 ; --uc->code)
          tab_suffixof(uc, uc->code) = (char_type)uc->code;
        uc->state = TXUCS_RESET_BUF;
        keepRunning = 1;
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_RESET_BUF:                     /* `resetbuf:' in ncompress */
      resetbuf:
        uc->state = TXUCS_RESET_BUF;
        e = uc->inBufLen - (o = (uc->posBits >> 3));
        if (o && e) memmove(uc->inBuf, uc->inBuf + o, e);
        uc->inBufLen = e;
        uc->posBits = 0;
        if (uc->inBufLen < sizeof(uc->inBuf) - IBUFSIZ)
          uc->state = TXUCS_READ_DATA;
        else
          uc->state = TXUCS_RESET_INBITS;
        keepRunning = 1;
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_READ_DATA:
        /* Copy more input, since `uc->inBuf' is low.  WTF can we just copy
         * data at any time into `uc->inBuf', even if it is not real low?
         */
        n = TX_MIN(inDataEnd - inData, IBUFSIZ);
        if (n > 0)
          {
            memcpy(uc->inBuf + uc->inBufLen, inData, n);
            uc->inBufLen += n;
            inData += n;
            keepRunning = 1;
          }
        uc->lastRdSize = n;
        uc->state = TXUCS_RESET_INBITS;
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_RESET_INBITS:
        uc->inBits = ((uc->lastRdSize > 0) ?
                      ((uc->inBufLen - uc->inBufLen %uc->nBits) << 3) :
                      (uc->inBufLen << 3) - (uc->nBits - 1));
        uc->state = TXUCS_PROCESS_DATA;
        keepRunning = 1;
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_PROCESS_DATA:
        while (uc->inBits > uc->posBits)
          {
            if (uc->freeEnt > uc->maxCode)
              {
                uc->posBits = ((uc->posBits - 1) + ((uc->nBits << 3) -
                    (uc->posBits - 1 + (uc->nBits << 3)) % (uc->nBits << 3)));
                ++uc->nBits;
                if (uc->nBits == uc->maxBits)
                  uc->maxCode = uc->maxMaxCode;
                else
                  uc->maxCode = MAXCODE(uc->nBits) - 1;
                uc->bitMask = (1 << uc->nBits) - 1;
                goto resetbuf;
              }

            input(uc->inBuf, uc->posBits, uc->code, uc->nBits, uc->bitMask);

            if (uc->oldCode == -1)
              {
                if (uc->code >= 256) goto corruptInput;
                uc->outBuf[uc->outBufLen++] =
                  (char_type)(uc->finChar = (int)(uc->oldCode = uc->code));
                continue;
              }

            if (uc->code == CLEAR && uc->blockMode)
              {
                clear_tab_prefixof(uc);
                uc->freeEnt = FIRST - 1;
                uc->posBits = ((uc->posBits - 1) + ((uc->nBits << 3) -
                    (uc->posBits - 1 + (uc->nBits << 3)) % (uc->nBits << 3)));
                uc->maxCode = MAXCODE(uc->nBits = INIT_BITS) -1;
                uc->bitMask = (1 << uc->nBits) - 1;
                goto resetbuf;
              }

            uc->inCode = uc->code;
            uc->stackp = de_stack(uc);

            if (uc->code >= uc->freeEnt)  /* Special case for KwKwK string. */
              {
                if (uc->code > uc->freeEnt)
                  {
                    REG1 char_type     *p;

                    uc->posBits -= uc->nBits;
                    p = &uc->inBuf[uc->posBits>>3];
                  corruptInput:
                    txpmbuf_putmsg(uc->pmbuf, MERR + FRE, fn, "Corrupt input");
                    goto err;
                  }
                *--uc->stackp = (char_type)uc->finChar;
                uc->code = uc->oldCode;
              }

            while ((cmp_code_int)uc->code >= (cmp_code_int)256)
              {         /* Generate output characters in reverse order */
                *--uc->stackp = tab_suffixof(uc, uc->code);
                uc->code = tab_prefixof(uc, uc->code);
              }

            *--uc->stackp =  (char_type)(uc->finChar =
                                         tab_suffixof(uc, uc->code));

            /* And put them out in forward order */

            uc->i = de_stack(uc) - uc->stackp;
            if (uc->outBufLen + uc->i >= OBUFSIZ)
              {
                do
                  {
                    if (uc->i > OBUFSIZ - uc->outBufLen)
                      uc->i = OBUFSIZ - uc->outBufLen;
                    if (uc->i > 0)
                      {
                        memcpy(uc->outBuf + uc->outBufLen, uc->stackp, uc->i);
                        uc->outBufLen += uc->i;
                      }

                    if (uc->outBufLen >= OBUFSIZ)
                      {                         /* time to write output */
                        /* Original ncompress write() assumed it could
                         * write its entire `outBuf' data in one call
                         * here, and would error if not.  We cannot
                         * wait for more `outData' space if needed, so
                         * switch to a new state.  It will jump back
                         * here (to `afterWriteData') when done.  Ugly
                         * jump, but saves us trying to untangle this
                         * loop for now wtf:
                         */
                        uc->outBufFlushedLen = 0;
                        uc->state = TXUCS_WRITE_DATA;
                        goto doWriteData;
                      }
                  afterWriteData:
                    uc->stackp += uc->i;
                  }
                while ((uc->i = (de_stack(uc) - uc->stackp)) > 0);
              }
            else
              {
                memcpy(uc->outBuf + uc->outBufLen, uc->stackp, uc->i);
                uc->outBufLen += uc->i;
              }

            if ((uc->code = uc->freeEnt) < uc->maxMaxCode)
              {                                 /* Generate new entry */
                tab_prefixof(uc, uc->code) = (unsigned short)uc->oldCode;
                tab_suffixof(uc, uc->code) = (char_type)uc->finChar;
                uc->freeEnt = uc->code + 1;
              } 

            uc->oldCode = uc->inCode;           /* Remember previous code */
          }

        /* We reach the TXUCS_FINAL_FLUSH stage when we have read
         * all currently available data (`uc->lastRdSize == 0') and we
         * know the caller will not provide any more data (TXCTEHF_INPUT_EOF):
         */
        if (uc->lastRdSize == 0 && (flags & TXCTEHF_INPUT_EOF))
          uc->state = TXUCS_FINAL_FLUSH;
        else
          uc->state = TXUCS_RESET_BUF;
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_WRITE_DATA:
      doWriteData:
        n = TX_MIN(uc->outBufLen - uc->outBufFlushedLen,
                   (size_t)(outDataEnd - outData));
        if (n > 0)
          {
            memcpy(outData, uc->outBuf + uc->outBufFlushedLen, n);
            outData += n;
            uc->outBufFlushedLen += n;
            keepRunning = 1;
          }
        if (uc->outBufFlushedLen >= uc->outBufLen)
          {                                     /* all data written */
            uc->outBufLen = 0;
            uc->state = TXUCS_PROCESS_DATA;
            /* Original ncompress flush is in the middle of a loop,
             * so we must jump back there when done with flush; wtf:
             */
            goto afterWriteData;
          }
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_FINAL_FLUSH:
        n = TX_MIN(uc->outBufLen, (size_t)(outDataEnd - outData));
        if (n > 0)
          {
            memcpy(outData, uc->outBuf, n);
            outData += n;
            /* wtf avoid this memmove by adding a start offset to
             * `uc->outBuf'?:
             */
            if (n < uc->outBufLen)
              memmove(uc->outBuf, uc->outBuf + n, uc->outBufLen - n);
            uc->outBufLen -= n;
            keepRunning = 1;
          }
        if (uc->outBufLen == 0)                 /* all data flushed out */
          uc->state = TXUCS_DONE;
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      case TXUCS_DONE:                          /* should not get here */
        break;
        /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
      default:
        txpmbuf_putmsg(uc->pmbuf, MERR, fn,
                       "Internal error: Unknown state %d", (int)uc->state);
        goto err;
      }

  ret = (uc->state == TXUCS_DONE ? 2 : 1);
  goto done;

err:
  ret = 0;
done:
  *inBuf = inData;
  *outBuf = outData;
  return(ret);
}

/* ------------------------------------------------------------------------ */

TXunixCompress *
TXunixCompressOpen(flags, pmbuf)
TXFILTERFLAG    flags;  /* (in) encode vs. decode */
TXPMBUF         *pmbuf; /* (in) putmsg buffer (will clone) */
/* Opens a TXunixCompress object.
 * Returns pointer to object, or NULL on error.
 */
{
  static CONST char     fn[] = "TXunixCompressOpen";
  TXunixCompress        *uc = TXunixCompressPN;

  if (!(flags & TXFILTERFLAG_DECODE))
    {
      /* wtf */
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "Encoding not supported");
      goto err;
    }
  uc = (TXunixCompress *)calloc(1, sizeof(TXunixCompress));
  if (uc == TXunixCompressPN)
    {
      TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, 1, sizeof(TXunixCompress));
      goto err;
    }
  uc->pmbuf = txpmbuf_open(pmbuf);
  uc->state = TXUCS_GET_MAGIC;

  /* ncompress stuff: */
  uc->maxBits = BITS;
  uc->blockMode = BLOCK_MODE;

  /* rest cleared by calloc() */
  goto done;

err:
  uc = TXunixCompressClose(uc);
done:
  return(uc);
}

TXunixCompress *
TXunixCompressClose(uc)
TXunixCompress  *uc;    /* (in, opt.) object to close */
{
  if (uc != TXunixCompressPN)
    {
      uc->pmbuf = txpmbuf_close(uc->pmbuf);
      free(uc);
    }
  return(TXunixCompressPN);
}

int
TXunixCompressTranslate(uc, inBuf, inBufSz, outBuf, outBufSz, flags)
TXunixCompress  *uc;            /* (in/out) object */
byte            **inBuf;        /* (in/out) input data to consume */
size_t          inBufSz;        /* (in) its size */
byte            **outBuf;       /* (in/out) output buffer to write to */
size_t          outBufSz;       /* (in) its size */
TXCTEHF         flags;          /* (in) flags */
/* Does compress or decompress from `inBuf' to `outBuf'.
 * Advances `*inBuf' past data consumed, and `*outBuf' past data output.
 * Returns 0 on error, 1 if ok, 2 if ok and output EOF reached.
 */
{
  /* wtf support compression */
  return(TXunixCompressDoDecompress(uc, inBuf, inBufSz, outBuf, outBufSz,
                                    flags));
}
