#ifndef MAIL_H
#define MAIL_H


/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/

#define MAILLNSZ      1024

#ifdef MSDOS
#   define RMODE      "rb"
#   define MAXMAILSZ  20000
#else
#   define RMODE      "r"
#   define MAXMAILSZ  80000
#endif


#ifndef CPNULL
#   define CPNULL (char *)NULL
#endif

#define MAIL struct mb_struct
#define MAILPN (MAIL *)NULL
MAIL
{
 FILE   *fh;
 off_t   bom,eom;
 char   *msg,*end;
 time_t  date;
 char   *datestr;
 char   *from;
 char   *to;
 char   *subject;
 char   *keywords;
 char   *body;
 int     isnews;
};

/************************************************************************/
/* prototypes go here */
MAIL *closemail ARGS((MAIL *));
MAIL *openmail  ARGS((char *fn));
int getmail  ARGS((MAIL *));
/************************************************************************/
#endif /* MAIL_H */
