#ifndef MDX_H
#define MDX_H
/**********************************************************************/
#ifdef unix
#define UNIX 1
#endif
#ifdef _WIN32
#  define MDXPATH         "c:\\morph3\\index\\"
#  define MDXTRMODE       "rb"
#  define MDXIRMODE       "rb"
#  define MDXIWMODE       "wb"
#  define MDXPATHC        '\\'
#else                                                       /* !MSDOS */
#  ifdef UNIX
#     define MDXPATH      "/usr/local/metamorph/index/"
#     define MDXTRMODE    "rb"
#     define MDXIRMODE    "rb"
#     define MDXIWMODE    "wb"
#     define MDXPATHC     '/'
#  else                                                      /* !UNIX */
                  stop.                             /* unknown system */
#  endif                                                      /* UNIX */
#endif                                                       /* MSDOS */

#define MDXPATHSZ    80                           /* max size of a path */
#define MDXEXPRS     10            /* maximum number of expressions used*/
#define MDXBLKSZ    300                /* size of a mindex record block */
#define MDXFSSZ      40                             /* format string sz */
#define SPECLNSZ     80                 /* size of a specification line */
#define MDXBUFSZ  30000                      /* size of the read buffer */
#define MDXMAXSZ    255                  /* max size of an index string */

#ifdef _INTELC32_
  #pragma align(mindex_expr_rec=2)
#endif
/* legacy Windows-32 packing: */
#if defined(_WIN32) && !defined(_WIN64)
#  pragma pack(push,mdx_h,2)/* MAW 05-12-92 - must be consistent across dos */
#endif
#define MDXER  struct mindex_expr_rec
MDXER
{
 FFS *ex;                                                 /* expression */
 byte fs[MDXFSSZ];                          /* format string for printf */
#ifdef _INTELC32_
 short first,last;                    /* first and last subexp to print */
#else
 int first,last;                      /* first and last subexp to print */
#endif
 byte pos;             /* position of this expression relative the text */
};

#ifdef _INTELC32_
  #pragma align(mindex_hit_rec=2)
#endif
#define MDXHR  struct mindex_hit_rec
MDXHR
{
 long offset;                               /* offset of the expression */
 byte expsz;                          /* size of the located expression */
 byte expn;             /* expression number associated with this entry */
};
/* legacy Windows-32 packing: */
#if defined(_WIN32) && !defined(_WIN64)
#  pragma pack(pop,mdx_h)             /* MAW 05-12-92 - restore default */
#endif

#define MDXWS struct mindex_write_struct
MDXWS
{
 FILE   *fh;                   /* file handle to associated mindex file */
 FFS    *bufex;                             /* buffer bounds expression */
 MDXER  explst[MDXEXPRS];                        /* list of expressions */
 MDXHR  *blk;                                   /* block of mdx records */
 int blksz;                               /* how many are in this block */
 int expn;                                   /* current being worked on */
 int nexps;                           /* number of expressions in total */
};

#define MDXRS struct mindex_read_struct
MDXRS
{
 FILE   *fh;                               /* file handle of index file */
 FILE   *xfh;                                  /* indexed file's handle */
 MDXER  explst[MDXEXPRS];                    /* list of expression info */
 MDXHR  exploc[MDXEXPRS];                        /* expressions located */
 MDXHR  blk[MDXBLKSZ];                            /* a block of records */
 int expcnt;                             /* number of index expressions */
 int nfwd;                           /* how many forward refs are there */
#ifdef NOSEEKTEXT    /* MAW 03-22-90 - info for NSTfseek(),NSTfread() */
 long xfpos;                              /* current indexed file pos */
 long xftellpos;        /* ftell() version of xfpos so i can get back */
 char seekbuf[BUFSIZ];                        /* use system rec. size */
#endif                                                  /* NOSEEKTEXT */
};

/**********************************************************************/
#ifdef LINT_ARGS
MDXRS *closermdx(MDXRS *);
MDXRS *openrmdx(char *);
char *getmdx(char *,int,long,MDXRS *);
#else
MDXRS *closermdx();
MDXRS *openrmdx();
char *getmdx();
#endif
/**********************************************************************/
#endif                                                       /* MDX_H */
