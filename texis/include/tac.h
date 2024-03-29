#ifndef TAC_H
#define TAC_H


#ifndef OS_H
#  include "os.h"
#endif


typedef struct TACBUF_tag       TACBUF; /* read buffer for TACF_REV on pipe */
#define TACBUFPN        ((TACBUF *)NULL)

typedef enum TACF_tag
{
  TACF_REV      = (1 << 0),             /* return lines in reverse order */
  TACF_TAIL     = (1 << 1),             /* start at EOF */
  TACF_FOLLOW   = (1 << 2),             /* keep trying to read at EOF */
  TACF_OKNOFILE = (1 << 3),             /* don't report error if no file */
  TACF_OKLINE   = (1 << 4),             /* (internal) ok to return lines */
  TACF_READFWD  = (1 << 5),             /* (internal) no lseek; read fwd */
  TACF_USEBUF   = (1 << 6),             /* (internal) use `buflist' */
  TACF_ISATTY   = (1 << 7),             /* (internal) `fh' is a tty */
  TACF_GOTEOF   = (1 << 8)              /* (internal) reached EOF */
}
TACF;
#define TACF_OK_FLAGS   (TACF_REV | TACF_TAIL | TACF_FOLLOW | TACF_OKNOFILE)

typedef struct TAC_tag                  /* A = alloced */
{
  TACF          flags;
  char          *fn;                    /* (A iff not `StdinPipe') path */
  int           fh;                     /* open file handle */
  FFS           *startrex, *endrex;     /* A start/end exprs (if !NULL) */
  int           linecnt, maxlines;
  char          *buf;                   /* A read buffer */
  size_t        bufsz;                  /* its total size */
  char          *preveol;               /* previous EOL in buffer */
  char          *bufstart, *bufend;     /* start/end of line-aligned data */
  char          *bufrdend;              /* end of raw read data */
  char          *saveptr, savech;       /* saved char at '\0'-terminator */
  char          endch;                  /* ending char, if split EOL */
  EPI_OFF_T     curloc;                 /* current offset in file */
  EPI_OFF_T     originalLoc;            /* original offset (for REV) */
  EPI_OFF_T     bufLoc;                 /* offset of `buf' */
  TACBUF        *buflist;               /* A buffers for TACF_REV on pipe */
}
TAC;
#define TACPN   ((TAC *)NULL)


TAC    *opentac(const char *fname, const EPI_OFF_T *startOffset,
                const char *startexp, const char *endexp,
                int maxLines, TACF flags, TXEXIT *errnum);
TAC     *closetac ARGS((TAC *tac));
size_t  tac_readln(TAC *tac, char **sp, EPI_OFF_T *fileOffset, size_t *eolSz);

#endif  /* !TAC_H */
