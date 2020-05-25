/*************************************************************************
**
** EPI Control Parameter File routines
**
** 12/01/88 MAG - In the beginning...
** 12/22/88 MAG - Beta release, comments added
** 01/24/89 MAW - added casts, func declarations for lint
** 01/24/89 MAW - added msgcp() to handle cp error msgs
** 02/07/89 MAW - combined into 1 file
** 06/13/89 MAW - switch ret for renamecp() from (CP *) to (char *)
** 07/27/89 MAW - removed "Strcmpi()" in favor of strcmpi() in os.c
** 10/23/89 MAW - moved static buff in get_string() to CP struct
** 01/21/91 MAW - convert all refs to TRUE/FALSE macros to 1/0
** 03/06/91 MAW - added UItype and ULtype
**              - changed all funcs with char arg to int arg
**              - made internal functions static
**              - pretty'd up the output format a little
**
*************************************************************************/
#include "txcoreconfig.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#endif
#include "stdlib.h"
#include "os.h"
#include "cp.h"

/**********************************************************************/
static void   copy_value      ARGS((REQUESTPARM *, REQUESTPARM *));
static char  *read_file       ARGS((CP *));
static char  *read_args       ARGS((CP *, int, char **));
static void   msgcp           ARGS((int,char *,char *,int));
static char  *allocate_memory (CP *, uint, uint);
static void   rel_memory      ARGS((char *));
static TXbool end_of_file     ARGS((CP *));
static void   rel_value       ARGS((char *,int,uint *,int));
static char  *ea              ARGS((char *,int, uint *, int));
static CONS  **find_entry     ARGS((CP *, char *));
static char  *write_file      ARGS((CP *));
#ifndef MSDOS
#ifndef MVS
/*static double strtod          ARGS((char *, char **));*/
#endif
#define ltoa myltoa                         /* MAW 07-14-92 - hp port */
#define ultoa myultoa
static char  *ltoa            ARGS((long, char *, int));
static char  *ultoa           ARGS((ulong, char *, int));
#endif                                                      /* !MSDOS */
#ifdef DEBUG_HEAP
static void   open_mem        ARGS((void));
static void   close_mem       ARGS((void));
#endif                                                  /* DEBUG_HEAP */
/**********************************************************************/
/* Static list of type sizes and error messages */
static int type_size[]={     /* indecies defined in cp.h, Btype, etc. */
    0             ,
    sizeof(TXbool),
    sizeof(char)  ,
    sizeof(int)   ,
    sizeof(uint)  ,
    sizeof(long)  ,
    sizeof(ulong) ,
    sizeof(float) ,
    sizeof(char *)
};

static char *cp_error_msgs[] = {
    "Syntax error(s) in CP file",        /* Generic msg returned to caller. */
    "EOF encountered unexpectedly",      /* The rest of these go into the   */
    "Too many sequential UNGETs",        /*   listing file.                 */
    "Token larger than expected",
    "Type specifier expected",
    "Identifier expected",
    "No memory",
    "Too many array dimensions",
    "Integer expected",
    "Left bracket expected",
    "Unexpected token",
    "Error opening control file",
    "Float expected",
    "Line too long",
    "Peek ahead failure",
    "Too many elements in dimension",
    "Non-zero integer expected",
    "TRUE, 1, FALSE, or 0, expected",
    "String too long",
    "Internal program failure",
    "Required entry(s) not found",
    "File not opened for write",
    "File not opened for read",
    "Hex character expected",
    "Octal character expected"
};

#ifndef MSDOS

/* Of course, if we're running on the gould we don't have all those nice
   libraries available, such as stdlib.  There's only four routines we
   actually need, and those we only implement as much as is required for
   our particular use. */

#if !defined(sun) && !defined(macintosh) && !defined(__MACH__) && !defined(DGUX) && !defined(__linux__) && !defined(__osf__) && !defined(__FreeBSD__) && !defined(__STDC__)
/**********************************************************************/
static double
strtod(nptr, endptr)
char *nptr;
char **endptr;
{
  int done;
  double bar, foo = 0;

  done = sscanf(nptr, "%lf", &foo);
  if (done)
    *endptr = "";
  else
    *endptr = "?";

  return(bar = foo);
}
/**********************************************************************/
#endif                                                        /* !sun */

/**********************************************************************/
static char *
ltoa(value, string, radix)
long value;
char *string;
int radix;
{
  if (radix == 16)
    sprintf(string, "%lx", value);
  else
    sprintf(string, "%ld", value);

  return(string);
}
/**********************************************************************/

/**********************************************************************/
static char *
ultoa(value, string, radix)
ulong value;
char *string;
int radix;
{
  if (radix == 16)
    sprintf(string, "%lx", value);
  else
    sprintf(string, "%lu", value);

  return(string);
}
/**********************************************************************/
#endif                                                      /* !MSDOS */

/**********************************************************************/
static void
copy_value(to, from)
REQUESTPARM *to, *from;

/* Do an object copy from one requestparm structure to another */

{
  if (to->type & ARRAYMASK) {

    /* Just move the pointer to the array, along with the dimension
       specification */

    *((char **)to->value) = *((char **)from->value);
#ifndef NEW_DIMS_ONLY
    if (to->type & DIM1)
      to->vlength = from->dims[0];
    else {
      to->vlength = from->dims[1];
      to->alength = from->dims[0]; }
#endif
    to->dims = from->dims; }
  else {

#ifndef NEW_DIMS_ONLY
    to->dims = (unsigned int *)&to->dim0;
    to->dim0 = 0;
#endif

    /* Move the actual object, but treat strings the same as arrays */

    switch (to->type & TYPEMASK) {
      case Btype : *((TXbool*)to->value) = *((TXbool*)from->value); break;
      case Ctype : *((char  *)to->value) = *((char  *)from->value); break;
      case Itype : *((int   *)to->value) = *((int   *)from->value); break;
      case UItype: *((uint  *)to->value) = *((uint  *)from->value); break;
      case Ltype : *((long  *)to->value) = *((long  *)from->value); break;
      case ULtype: *((ulong *)to->value) = *((ulong *)from->value); break;
      case Ftype : *((float *)to->value) = *((float *)from->value); break;
      case Stype : *((char **)to->value) = *((char **)from->value); break; }}
}
/**********************************************************************/

/**********************************************************************/
CP *
opencp(pathname, mode)
char *pathname, *mode;

/* Allocate a CP data object, initialize the fields, and read in the
   CP file if necessary */

{
  CP *cpd;

                                        /* 3/9/89 - free mem on error */
  if((cpd=(CP *)calloc(1,sizeof(CP)))!=NULL){
     if((cpd->cp_pathname=(char *)malloc(strlen(pathname)+1))!=NULL){
        if((cpd->cp_mode=(char *)malloc(strlen(mode)+1))==NULL){
           free(cpd->cp_pathname); free((void *)cpd); cpd=NULL;
        }
     }else{
        free((void *)cpd); cpd=NULL;
     }
  }
  if(cpd==NULL) return((CP *)NULL);

  strcpy(cpd->cp_pathname, pathname);
#ifdef LISTING
  cpd->listing_pathname = "CON";        /* Hardcode for now! */
#endif
  strcpy(cpd->cp_mode, mode);
  cpd->list_head = (CONS *)NULL;
  cpd->token_size = 0;
  cpd->line_buff[cpd->line_ptr = 0] = '\0';
  cpd->line_number = 0;
  cpd->line_printed = TXbool_True;
  cpd->force_mode = TXbool_False;
  cpd->error_msg = "";
  cpd->all_found = TXbool_False;

  /* Read in the CP file if necessary */

  if(((cpd->cp_mode[0]=(char)toupper(cpd->cp_mode[0]))=='R') ||
     (cpd->cp_mode[0]=='U')){
     if((cpd->error_msg = read_file(cpd)) != (char *)NULL){
        free(cpd->cp_mode); free(cpd->cp_pathname); free((char *)cpd);
        return((CP *)NULL);
     }
  }

  return(cpd);
}
/**********************************************************************/

/**********************************************************************/
CP *
closecp(cpd)
CP *cpd;

/* Write out the CP file if necessary, and deallocate all heap allocated
   objects */

{
  CONS *foo, *next;

  if (cpd == (CP *)NULL)
    return((CP *)NULL);

  /* On a write, do a "write_file" to put all the data back */

  cpd->token_size = 0;
  cpd->line_ptr = 0;/* max_wrap_length; MAW 03-07-91 *//* Current column number */
  cpd->line_number = 0;                 /* Current line number */
  cpd->line_printed = TXbool_False;
  cpd->error_msg = "";

  if ((cpd->cp_mode[0]=='W') || (cpd->cp_mode[0]=='U'))
    cpd->error_msg = write_file(cpd);

  /* Run down the CONS list deallocating objects (including the CONS
     list itself).  Be careful only to deallocate objects actually
     allocated by CP. */

  if (cpd != (CP *)NULL) {
    foo = cpd->list_head;

    while (foo != (CONS *)NULL) {
      if (foo->avalue != (REQUESTPARM *)NULL) {
        rel_memory(foo->avalue->name);
        rel_value(foo->avalue->value, (int)foo->avalue->type,
                  foo->avalue->dims, 1);
        rel_memory((char *)foo->avalue->dims);
        rel_memory((char *)foo->avalue); }
      next = foo->cdr;
      rel_memory((char *)foo);
      foo = next; }}

  /* Since the following weren't allocated using 'allocate_memory', don't
     release them using 'rel_memory' */

  free(cpd->cp_pathname);
  free(cpd->cp_mode);
  free((char *)cpd);
  return(NULL);
}
/**********************************************************************/

/**********************************************************************/
char *
savecp(cpd)          /* MAW 02-03-93- save cp to file without closing */
CP *cpd;
{
  if (cpd == (CP *)NULL)
    return((char *)NULL);
  if (cpd->cp_mode[0]=='R')
    return(cp_error_msgs[file_not_opened_for_write]);
  cpd->token_size = 0;
  cpd->line_ptr = 0;/* max_wrap_length; MAW 03-07-91 *//* Current column number */
  cpd->line_number = 0;                 /* Current line number */
  cpd->line_printed = TXbool_False;
  cpd->error_msg = "";
  cpd->error_msg = write_file(cpd);
  return(cpd->error_msg);
}                                                     /* end savecp() */
/**********************************************************************/

/**********************************************************************/
char *                       /* MAW 6/13/89 - from (CP *) to (char *) */
renamecp(cpd, pathname)
CP *cpd;
char *pathname;

/* Change the filename associated with a CP structure */

{
  if (cpd == (CP *)NULL)
    return(NULL);

  free(cpd->cp_pathname);
  if ((cpd->cp_pathname = (char *)malloc(strlen(pathname)+1)) == (char *)NULL)
    return(cp_error_msgs[malloc_error]);

  strcpy(cpd->cp_pathname, pathname);
  return(NULL);                                           /* no error */
}
/**********************************************************************/

/**********************************************************************/
char *
readcp(cpd, block_data)
CP *cpd;
REQUESTPARM block_data[];

/* Assign data from the CP file to the user's local variables */

{
  int i;
  TXbool errors = TXbool_False;
  CONS **bar;

  if (cpd == (CP *)NULL)
    return((char *)NULL);

  if (cpd->cp_mode[0]=='W')
    return(cp_error_msgs[file_not_opened_for_read]);

  /* Run down the list seeing if we obtained each element upon OPENCP.
     If yes, copy out a pointer to the obtained value; if no, see if we
     can safely ignore this fact. */

  for (i = 0; block_data[i].type != ENDLIST; i++) {
    bar = find_entry(cpd, block_data[i].name);

    block_data[i].found = (TXbool)!(*bar == (CONS *)NULL);
    if (block_data[i].found)
      copy_value((REQUESTPARM *)&block_data[i], (*bar)->value);
    else
      if (!(block_data[i].type & CPOPTIONAL)) {
#ifdef LISTING
        fprintf(cpd->listing_desc, "CP READ:  Required entry not found, '%s'\n",
                        block_data[i].name);
#endif
        errors = 1;
        msgcp(0,"CP READ: Required entry not found, ",block_data[i].name,-1);
      }
  }

  cpd->all_found = (TXbool)!errors;
  if (errors)
    return(cp_error_msgs[required_entry_not_found]);
  else
    return(NULL);
}
/**********************************************************************/

/**********************************************************************/
char *
readcpargs(cpd, argc, argv)
CP *cpd;
int argc;
char **argv;

/* Update the contents associated with a CP file, using information
   supplied from the command line (in the form of "id" "value" pairs) */

{
  if (cpd == (CP *)NULL)
    return((char *)NULL);

  if (cpd->cp_mode[0]=='R')
    return(cp_error_msgs[file_not_opened_for_write]);

  return(read_args(cpd, argc, argv));
}
/**********************************************************************/

/**********************************************************************/
char *
writecp(cpd, block_data)
CP *cpd;
REQUESTPARM block_data[];

/* Assign data from the user's local structure to the internal structure
   associated with each CP file */

{
  int i;
  CONS **bar, *foo;

  if (cpd == (CP *)NULL)
    return((char *)NULL);

  if (cpd->cp_mode[0]=='R')
    return(cp_error_msgs[file_not_opened_for_write]);

  /* Run down the list copying pointers to the user's values into
     our data area.  If necessary, convert from vlength/alength format
     into DIMS format (zero terminated array of integers).  */

  for (i = 0; block_data[i].type != ENDLIST; i++) {
#ifndef NEW_DIMS_ONLY

/* Fake a dims array */

    block_data[i].dims = (unsigned int *)&block_data[i].dim0;

    if (block_data[i].type & ARRAYMASK)
      if (block_data[i].type & DIM1) {
        block_data[i].dim0 = block_data[i].vlength;
        block_data[i].dim1 = 0; }
      else {
        block_data[i].dim0 = block_data[i].alength;
        block_data[i].dim1 = block_data[i].vlength;
        block_data[i].terminator = 0; }
    else
      block_data[i].dim0 = 0;

#endif
    bar = find_entry(cpd, block_data[i].name);

    /* If we don't locate the element, create a new cons cell and
       tack it on the end of our list.  Zero out the avalue field
       to avoid our code from deallocating the user's data structure. */

    if (*bar == (CONS *)NULL) {
      foo = (CONS *)allocate_memory(cpd, 1, sizeof(CONS));
      if (foo == (CONS *)NULL)
        return(cp_error_msgs[malloc_error]);
      else {
        *bar = foo;
        foo->cdr = (CONS *)NULL;
        foo->avalue = (REQUESTPARM *)NULL; }}

    /* Store a pointer to the user's value */

    (*bar)->value = (REQUESTPARM *)&block_data[i]; }

  return(NULL);
}
/**********************************************************************/

/**********************************************************************/
unsigned int
numentries(cpd, value)
CP *cpd;
char *value;

/* Return the rank of the first dimension for an array element.
   If 'value' points to a discrete value, the rank returned is undefined
   (although most assuredly will be zero!).

   This routine ONLY works for arrays up to two dimensions.

   If 'value' points to a non-existant array, a value of zero is returned. */

{
  CONS *foo;
  unsigned int i;

  if (cpd != (CP *)NULL) {
    foo = cpd->list_head;
    while (foo != (CONS *)NULL) {
      if (foo->value->type & ARRAYMASK)
        {
          if (*((char **)foo->value->value) == value)
            return(foo->value->dims[0]);
          else if (foo->value->type & DIM2)
            {
              for(i=0; i!=foo->value->dims[1]; i++)
                if ((*((char ***)foo->value->value))[i] == value)
                  return(foo->value->dims[1]);
            }
        }
      foo = foo->cdr;
    }
  }

  return(0);
}
/**********************************************************************/

/**********************************************************************/
static void
msgcp(off,msg,why,line)                               /* MAW 01/24/89 */
int off;
char *msg, *why;
int line;
{
#ifdef PR_ERRORS
   while(off-->0) fputc(' ',stderr);
   fputs(msg,stderr);
   if(why!=NULL) fputs(why,stderr);
   if(line>0) fprintf(stderr,", line %d",line);
   fputc('\n',stderr);
#else
   (void)off;
   (void)line;
   (void)msg;
   (void)why;                             /* use for compiler */
#endif
   return;
}                                                      /* end msgcp() */
/**********************************************************************/

/* from cpr.c */
/************************************************************************/
/**                                                                    **/
/** 02/15/89 MAW - make input string buf larger (5k)                   **/
/**                                                                    **/
/************************************************************************/
static void   parse_error     ARGS((CP *, int));
static char   get_char        ARGS((CP *));
static char   unget_char      ARGS((CP *, int));
static char   peek_char       ARGS((CP *));
static int    is_hex          ARGS((int));
static int    is_alpha        ARGS((int));
static int    is_number       ARGS((int));
static int    is_bool         ARGS((int));
static int    hex_to_dec      ARGS((int));
static int    is_octal        ARGS((int));
static int    octal_to_dec    ARGS((int));
static char   get_echar       ARGS((CP *));
static char   get_token       ARGS((CP *,int (*) ARGS((int)),int));/* Implicit peek_char perfomed */
static char   get_ctoken      ARGS((CP *, int));/* Implicit peek_char again */
static char   skip_whitespace ARGS((CP *));/* Implicit peek_char also */
static long   get_long        ARGS((CP *));
static ulong  get_ulong       ARGS((CP *));
static double get_double      ARGS((CP *));
static TXbool get_bool        ARGS((CP *));
static char  *get_string      ARGS((CP *, int));
static uchar  get_type        ARGS((CP *));
static int    is_id           ARGS((int));
static char  *get_id          ARGS((CP *));
static uint  *get_dims        ARGS((CP *));
static char  *zero_value      ARGS((CP *,int, uint *, char *, int));
static char  *get_value       ARGS((CP *,int, char *, uint *, char *,int));
static char   resync          ARGS((CP *, int));
static void   copy_plus_space ARGS((CP *, char *));

/**********************************************************************/
static void
parse_error(cpd, n)
CP *cpd;
int n;

/* Raise an exception */

{
  longjmp(cpd->error_env, n);
}
/**********************************************************************/

/**********************************************************************/
static char
get_char(cpd)
CP *cpd;

/* Retrieve the next available character.  If (FORCE_MODE) then
   take the character out of the line buffer and declare EOF at end
   of string. if (!FORCE_MODE) then take the character out of the line
   buffer, and at end of string load another line from the file; declare
   EOF at end of file. */

{
  int i, ch;

  if (end_of_file(cpd))
    parse_error(cpd, EOF_too_soon);

  else {
    if (cpd->line_buff[cpd->line_ptr] == '\0') {
#ifdef LISTING
      if (!cpd->line_printed)
        fputs(cpd->line_buff, cpd->listing_desc);
#endif

      /* Read in another line from the file */

      cpd->line_ptr = i = 0;
      ch='a';
      while ((!feof(cpd->cp_desc)) &&
             (cpd->line_buff[i++] =(char)( ch = fgetc(cpd->cp_desc))) != '\n' &&
             ch!=EOF &&         /* MAW - 10-24-95 - use int, not char */
             (i < (max_line_length - 1)))
          ;
      if(ch==EOF)                                   /* MAW - 10-24-95 */
      {
         if(i>0)
            cpd->line_buff[i-1] =(char)EOF;
      }

      cpd->line_buff[i++] = '\0';
      cpd->line_printed = TXbool_False;
      cpd->line_number++;

      if (i == max_line_length)
        parse_error(cpd, line_too_long); }

    return(cpd->line_buff[cpd->line_ptr++]); }
}
/**********************************************************************/

/**********************************************************************/
static char
unget_char(cpd, ch)
CP *cpd;
int ch;

/* Return a character to the buffer.  Of course, one cannot unget past
   the beginning of the buffer. */

{
  if (cpd->line_ptr == 0)
    parse_error(cpd, too_many_ungets);
  else
    return(cpd->line_buff[--(cpd->line_ptr)] = (char)ch);
}
/**********************************************************************/

/**********************************************************************/
static char
peek_char(cpd)
CP *cpd;

/* Return what the next character would be if we do a get.

   Note that peek_char was implemented, on purpose, as a get followed
   by an unget.  The get forces the possible reading of a new line from
   the file. */

{
  return(unget_char(cpd,(int)get_char(cpd)));
}
/**********************************************************************/

/**********************************************************************/
static int
is_hex(c)
int c;
{
  return(((c >= '0' && c <= '9') ||            /* wtp - assumes ascii */
          (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')));
}
/**********************************************************************/

/**********************************************************************/
static int
hex_to_dec(c)                                  /* wtp - assumes ascii */
int c;
{
  c = toupper(c);
  if(isdigit(c))
    return(c - '0');
  else
    return(c - 'A' + 10);
}
/**********************************************************************/

/**********************************************************************/
static int
is_octal(c)
int c;
{
  return((c >= '0' && c <= '7'));              /* wtp - assumes ASCII */
}
/**********************************************************************/

/**********************************************************************/
static int
octal_to_dec(c)
int c;
{
  return(c - '0');                             /* wtp - assumes ASCII */
}
/**********************************************************************/

/**********************************************************************/
static char
get_echar(cpd)
CP *cpd;

/* Retrieve an extended character - one which is possibly specified
   via an escape sequence */

{
  char c;

  if ((c = get_char(cpd)) == '\\')
    if ((peek_char(cpd)) != '\n')
      if (!(isdigit(c = peek_char(cpd))))
        switch (c = get_char(cpd)) {
          case 'n': return('\n');
          case 't': return('\t');
          case 'b': return('\b');
          case 'r': return('\r');
          case 'f': return('\f');
          case '\\': return('\\');
          case '\'': return('\'');
          case 'x':
            if (is_hex((int)(c = get_char(cpd))))
              return((is_hex((int)peek_char(cpd))) ?
                     (char) (hex_to_dec((int)c) * 16 +
                            hex_to_dec((int)get_char(cpd))) :
                     (char) hex_to_dec((int)c));
            else
              parse_error(cpd, hex_character_expected); }
      else
        if (is_octal((int)(c = get_char(cpd)))) {
          c = (char) octal_to_dec((int)c);
          if (is_octal((int)peek_char(cpd))) {
            c = (char) ((int)c * 8 + octal_to_dec((int)get_char(cpd)));
            if (is_octal((int)peek_char(cpd)))
              c = (char) ((int)c * 8 + octal_to_dec((int)get_char(cpd))); }
          return(c); }
        else
          parse_error(cpd, octal_character_expected);

    else

      /* I just love C's comma operator.  Make's me want to write
         a "curry" macro and implement all sequencing via it (a la Scheme).

         It would probably trash most compilers though ... */

      return((get_char(cpd), get_echar(cpd)));  /* Ignore backslash & newline */

  return(c);
}
/**********************************************************************/

/**********************************************************************/
static char
skip_whitespace(cpd)
CP *cpd;

/* Skip past ascii characters <32 or =7f. */

{
  TXbool done = TXbool_False;
  char c;

  while (!done) {
                                        /* MAW - 10-24-95 - check EOF */
    while ((c= get_char(cpd))!=(char)EOF &&
            (iscntrl(c) || isspace(c)) );
    if(c==(char)EOF)
       return(EOF);

    done = (TXbool)(!((c == '/') && (peek_char(cpd) == '*')));
    if (!done) {
      get_char(cpd);
      while (!(((c=get_char(cpd)) == '*') && (peek_char(cpd) == '/')));
      get_char(cpd); c = peek_char(cpd); }
    else
      unget_char(cpd, (int)c); }

  return(c);
}
/**********************************************************************/

/**********************************************************************/
static char
get_token(cpd, is_ok, max_size)
CP *cpd;
int  (*is_ok) ARGS((int));
int max_size;

/* Now here's a cute one.  First skip any whitespace, then scan off
   all characters which satisfy the supplied predicate. Obviously, the
   predicate should be restrictive in some form - if it always returns
   true you're in trouble! */

{
  char c;

  skip_whitespace(cpd);

  cpd->token_size = 0;
  while ((*is_ok)((int)(c = get_char(cpd))))
    if (cpd->token_size == max_size)
      parse_error(cpd, token_too_big);
    else
      cpd->token_buff[cpd->token_size++] = c;

  /* Slap a null on the end */

  cpd->token_buff[cpd->token_size] = '\0';
  unget_char(cpd, (int)c);

  return(cpd->token_buff[0]);
}
/**********************************************************************/

/**********************************************************************/
static char
get_ctoken(cpd, c)
CP *cpd;
int c;
/* Get a single (c)haracter token (i.e., after skipping any whitespace,
   you MUST immediately encounter the specified character */

{
  if (c == (int)skip_whitespace(cpd))
    return(get_char(cpd));
  else
    parse_error(cpd, token_not_found);
}
/**********************************************************************/

/**********************************************************************/
static int
is_number(c)
int c;
/* A silly little predicate - is the character possibly part of a number ? */

{
  return(((isalnum(c)) || (c == '.') || (c == '-') || (c == '+')));
}
/**********************************************************************/

/**********************************************************************/
static long
get_long(cpd)
CP *cpd;

/* Retrieve a long integer, specified in decimal, octal, or hex.
   The gould had a peculiar habit of printing hex in lowercase, but
   only allowing the reading of hex in uppercase. ARGHHHH! Is this
   gould, unix, or an underspecified Unix library routine! */

{
  long foo;
  char *end[1];
#ifndef MSDOS
  int i,j;
#endif

  get_token(cpd, is_number, max_token_length);

#ifndef MSDOS
  /* Ok.  If gould, and hex, convert to upper case. */

  j = strlen(cpd->token_buff);
  if ((j > 2) && (cpd->token_buff[0] == '0') && (cpd->token_buff[1] == 'x'))
    for(i=2; i!=j; i++)
      if ((cpd->token_buff[i] >= 'a') && (cpd->token_buff[i] <= 'f'))
        cpd->token_buff[i] = toupper(cpd->token_buff[i]);
#endif
  foo = strtol(cpd->token_buff, end, 0);
  if (!*end[0])
    return(foo);
  else
    parse_error(cpd, needed_integer);
}
/**********************************************************************/

/**********************************************************************/
static ulong
get_ulong(cpd)
CP *cpd;

/* Retrieve a unsigned long integer, specified in decimal, octal, or hex.
   The gould had a peculiar habit of printing hex in lowercase, but
   only allowing the reading of hex in uppercase. ARGHHHH! Is this
   gould, unix, or an underspecified Unix library routine! */

{
  ulong foo;
  char *end[1];
#ifndef MSDOS
  int i,j;
#endif

  get_token(cpd, is_number, max_token_length);

#ifndef MSDOS
  /* Ok.  If gould, and hex, convert to upper case. */

  j = strlen(cpd->token_buff);
  if ((j > 2) && (cpd->token_buff[0] == '0') && (cpd->token_buff[1] == 'x'))
    for(i=2; i!=j; i++)
        cpd->token_buff[i] = toupper(cpd->token_buff[i]);
#endif
  foo = (ulong)strtol(cpd->token_buff, end, 0);
  if (!*end[0])
    return(foo);
  else
    parse_error(cpd, needed_integer);
}
/**********************************************************************/

/**********************************************************************/
static double
get_double(cpd)
CP *cpd;

/* Retrieve a float. */

{
  double foo;
  char *end[1];

  get_token(cpd, is_number, max_token_length);
  foo = strtod(cpd->token_buff, end);
  if (!*end[0])
    return(foo);
  else
    parse_error(cpd, needed_float);
}
/**********************************************************************/

/**********************************************************************/
static int
is_alpha(ch)
int ch;
/* Alpha predicate */

{
  return(isalpha(ch));
}
/**********************************************************************/

/**********************************************************************/
static int
is_bool(ch)
int ch;
/* Bool predicate */

{
  return((isalpha(ch)||ch=='0'||ch=='1'));  /* MAW 3/7/89 - allow 1/0 */
}
/**********************************************************************/

/**********************************************************************/
static TXbool
get_bool(cpd)
CP *cpd;

/* Retrieve a boolean.  Note size parameter to get_token.  We know
   that there are only two discrete booleans (of a certain size).  If more
   non-whitespace characters are read than the maximum boolean size,
   something tain't right! */

{
  get_token(cpd, is_bool, 6);               /* MAW 3/7/89 - allow 1/0 */
  if ((!strcmpi(cpd->token_buff, "true"))||*cpd->token_buff=='1')
    return(1);
  else if ((!strcmpi(cpd->token_buff, "false"))||*cpd->token_buff=='0')
    return(0);
  else
    parse_error(cpd, needed_bool);
}
/**********************************************************************/

/**********************************************************************/
static char *
get_string(cpd, parse_only)
CP *cpd;
int parse_only;
/* Retrieve a string represented in 'C' syntax */

{
  char *foo;
  int i = 0;

  while (peek_char(cpd) != '"') {
    cpd->buff[i++] = get_echar(cpd);

    if (i == max_string_length)
      parse_error(cpd, string_too_long); }

  cpd->buff[i++] = '\0';

  if (parse_only)
    return(NULL);
  else {
    foo = allocate_memory(cpd, i, sizeof(char));
    return(strcpy(foo, cpd->buff)); }
}
/**********************************************************************/

/**********************************************************************/
static uchar
get_type(cpd)
CP *cpd;

/* Retrieve a valid CP type */

{
static char *types[] = {
     "TXbool", "char", "int", "uint", "long", "ulong", "float"
};
static uchar bit_masks[] = {
     Btype , Ctype , Itype, UItype, Ltype , ULtype , Ftype
};
int i;

  get_token(cpd, is_alpha, 6);
  for (i = 0; i != 7; i++)
    if (!strcmpi(cpd->token_buff, types[i]))
      if ((i == 1) && (skip_whitespace(cpd) == '*'))
        return((uchar)(get_char(cpd), Stype));
      else
        return(bit_masks[i]);

  parse_error(cpd, needed_type);
}
/**********************************************************************/

/**********************************************************************/
static int
is_id(c)
int c;
/* Id's may contain alphas and underscores.  Note that we're slightly
   less restrictive in that the first character can be a number, etc.
   Oh well. It can't hurt. */

{
  return((isalnum(c) || c == '_'));
}
/**********************************************************************/

/**********************************************************************/
static char *
get_id(cpd)
CP *cpd;

/* Scan in an 'id' */

{
  char *foo;

  get_token(cpd, is_id, max_token_length);
  if (cpd->token_size == 0)
    parse_error(cpd, needed_identifier);

  foo = allocate_memory(cpd, 1, strlen(cpd->token_buff)+1);
  strcpy(foo, cpd->token_buff);

  return(foo);
}
/**********************************************************************/

/**********************************************************************/
static uint *
get_dims(cpd)
CP *cpd;

/* scan off the dimension specification, if present. Standard C
   syntax is assumed */

{
  unsigned int vals[max_dimensions], i, j;
  unsigned int *foo;

  i = 0;
  while (skip_whitespace(cpd) == '[') {
    get_ctoken(cpd, '[');
    if (i == max_dimensions)
      parse_error(cpd, too_many_dimensions);

    if (((vals[i++] = (unsigned int)get_long(cpd)) == 0))
      parse_error(cpd, needed_nonzero_integer);

    get_ctoken(cpd, ']');
    if (cpd->token_size == 0)
      parse_error(cpd, needed_left_bracket); }

  vals[i] = 0;                                          /* zero terminated */
  foo = (unsigned int *)allocate_memory(cpd, i+1, sizeof(unsigned int));

  for (j=0; j<=i; j++) foo[j] = vals[j];
  return(foo);
}
/**********************************************************************/

#define get_assignment(cpd) get_ctoken(cpd, '=')

/**********************************************************************/
static char *
zero_value(cpd, type, dims, address, parse_only)
CP *cpd;
int type;
unsigned int *dims;
char *address;
int parse_only;

/* Supply a default, null, value for the specified object.  A null
   integer is the zero integer, etc., and a null string is a string of
   length 0 (i.e., a ptr to a '\0').

   If parse only is TRUE, just do syntax checking.

   If an address is supplied, store the object there; otherwise,
   allocate memory to store the object and RETURN the address */

{
  char *foo = address, *bar;

  if (parse_only)
    return(foo);

  if (dims[0] == 0)
    switch (type & TYPEMASK) {
      case Btype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(TXbool));
        *foo = 0;
        break;
      case Ctype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(char));
        *foo = '\0';
        break;
      case Itype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(int));
        *((int *)foo) = 0;
        break;
      case UItype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(uint));
        *((uint *)foo) = 0;
        break;
      case Ftype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(float));
        *((float *)foo) = (float)0.0;
        break;
      case Ltype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(long));
        *((long *)foo) = 0;
        break;
      case ULtype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(ulong));
        *((ulong *)foo) = 0;
        break;
      case Stype:
        if (foo == (char *)NULL) foo = allocate_memory(cpd, 1, sizeof(char *));
        bar = allocate_memory(cpd, 1, sizeof(char));
        *bar = '\0';
        *((char **)foo) = bar;
        break;
      default:
        parse_error(cpd, internal_program_failure); }

  else {

    /* Arrays really aren't that bad, there merely recursively constructed */

    char *data;
    unsigned int i;

    data = allocate_memory(cpd, dims[0],
              ((dims[1] == 0) ? type_size[type & TYPEMASK] : sizeof(char *)));
    for (i=0; i!=dims[0];
      zero_value(cpd, type, &dims[1], ea(data, type, dims, i++), parse_only));
    if (foo == (char *)NULL)
      foo = allocate_memory(cpd, 1, sizeof(char *));
    *((char **)foo) = data; }

  return(foo);
}
/**********************************************************************/

/**********************************************************************/
static char *
get_value(cpd, type, id, dims, address, parse_only)
CP *cpd;
int type;
char *id, *address;
unsigned int *dims;
int parse_only;
/* Retrieve the specified object from the input stream.  Remember,
   if FORCE_MODE = TRUE, the input stream is a string, else the input
   stream is the CP file.

   If parse only is TRUE, just do syntax checking.

   If an address is supplied, store the object there; otherwise,
   allocate memory to store the object and RETURN the address */

{
  char *foo = address;
  TXbool bfoo;
  char cfoo;
  int ifoo;
  uint uifoo;
  long lfoo;
  ulong ulfoo;
  float ffoo;
  char *sfoo;

  if (dims[0] == 0)
    switch (type & TYPEMASK) {
      case Btype:
        bfoo = get_bool(cpd);
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(TXbool));
          *((TXbool *)foo) = bfoo; }
        break;
      case Ctype:
        get_ctoken(cpd, '\'');
        cfoo = get_echar(cpd);
        get_ctoken(cpd, '\'');              /* Slightly less restrictive */
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(char));
          *foo = cfoo; }
        break;
      case Itype:
        ifoo = (int)get_long(cpd);
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(int));
          *((int *)foo) = ifoo; }
        break;
      case UItype:
        uifoo = (uint)get_ulong(cpd);
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(uint));
          *((uint *)foo) = uifoo; }
        break;
      case Ftype:
        ffoo = (float)get_double(cpd);
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(float));
          *((float *)foo) = ffoo; }
        break;
      case Ltype:
        lfoo = get_long(cpd);
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(long));
          *((long *)foo) = lfoo; }
        break;
      case ULtype:
        ulfoo = get_ulong(cpd);
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(ulong));
          *((ulong *)foo) = ulfoo; }
        break;
      case Stype:
        get_ctoken(cpd, '"');
        sfoo = get_string(cpd, parse_only);
        get_ctoken(cpd, '"');
        if (!parse_only) {
          if (foo == (char *)NULL)
            foo = allocate_memory(cpd, 1, sizeof(char *));
          *((char **)foo) = sfoo; }
        break;
      default:
        parse_error(cpd, internal_program_failure); }

  else {
    char *data;
    unsigned int i = 0;
    char c;

    data = allocate_memory(cpd, dims[0],
              ((dims[1] == 0) ? type_size[type & TYPEMASK] : sizeof(char *)));
    get_ctoken(cpd, '{');
    while (((c = skip_whitespace(cpd)) != '}') && (i != dims[0]))
      if (c == ',') {
        zero_value(cpd, type, &dims[1], ea(data, type, dims, i++), parse_only);
        get_ctoken(cpd, ','); }
      else {
        get_value(cpd, type, id, &dims[1], ea(data, type, dims, i++), parse_only);
        if ((i < dims[0]) && ((c = skip_whitespace(cpd)) != '}'))
          get_ctoken(cpd, ','); }

    if (i == dims[0]) {
      if (c != '}')
        parse_error(cpd, too_many_elements_in_dimension); }
    else
      while (i != dims[0])
        zero_value(cpd, type, &dims[1], ea(data, type, dims, i++), parse_only);

    get_ctoken(cpd, '}');                             /* skip past '}' */

    if (parse_only) {
      rel_memory((char *)data);
      foo = (char *)NULL; }
    else {
      if (foo == (char *)NULL)
        foo = allocate_memory(cpd, 1, sizeof(char *));
      *((char **)foo) = data; }}

  return(foo);
}
/**********************************************************************/

/**********************************************************************/
static char
resync(cpd, tag)
CP *cpd;
int tag;

/* Resync is called by the exception handler (i.e., SETJMP).  Upon
   encountering a parse error, print the error message, attempt to
   resync (i.e., locate a reasonably recognizable portion of text),
   and then look for syntax errors in the rest of the file. */

{
  int i,j;

  if (!cpd->line_printed) {
#ifdef LISTING
    fputs(cpd->line_buff, cpd->listing_desc);
#endif
    msgcp(0,cpd->line_buff,NULL,-1);
    cpd->line_printed = TXbool_True; }

  for(i=j=0;i!=cpd->line_ptr;i++){
     if(cpd->line_buff[i]=='\t') j+=((j%8)==0?8:8-(j%8));  /* tabstop */
     else j++;
  }


#ifdef LISTING
  for(i=j;i>0;i--) fputc(' ',cpd->listing_desc);
  fprintf(cpd->listing_desc, "^  CP OPEN:   %s, line %d\n",
          cp_error_msgs[tag], cpd->line_number);
#endif
  msgcp(j,"^ CP OPEN: ",cp_error_msgs[tag],cpd->line_number);

  /* Kind of a simple minded resync - just look for a semicolon. */

  while ((!end_of_file(cpd)) && (get_char(cpd) != ';'));

  /* Like most of my routines, an implicit peek_char is performed */

  return((char)(end_of_file(cpd) ? ' ' : peek_char(cpd)));
}
/**********************************************************************/

/**********************************************************************/
static void
copy_plus_space(cpd, string)
CP *cpd;
char *string;

/* Simple little utility required by readcpargs - copy a string from
   one place to another and tack on an end-of-line character.

   Fill up CP buff along the way, so that if an overflow occurs,
   the error handler will have something intelligent to print out */

{
  int j, k;

  cpd->line_ptr = 0;

  /* strcpy wouldn't work in this case! */

  j = strlen(string);
  for(k=0; k!=j; k++) {
    cpd->line_buff[k] = string[k];
    if (k > (max_line_length - 2)) {
      cpd->line_ptr = k;
      cpd->line_buff[k] = '\0';
      parse_error(cpd, token_too_big); }}

  cpd->line_buff[k++] = '\n';
  cpd->line_buff[k] = '\0';
}
/**********************************************************************/

/**********************************************************************/
static char *
read_file(cpd)
CP *cpd;

/* Open the CP file, read in the contents and store said contents
   in a list of CONS cells off of CP */

{
  CONS *foo, **tail;
  int tag;
  TXbool syntax_check_only = TXbool_False;
  unsigned char type;
  char *id, *value;
  unsigned int *dims;

  if ((cpd->cp_desc = fopen(cpd->cp_pathname, "r")) == (FILE *)NULL)
    return(cp_error_msgs[open_error]);

#ifdef LISTING
  if ((cpd->listing_desc = fopen(cpd->listing_pathname, "w")) == (FILE *)NULL)
    return(cp_error_msgs[open_error]);
#endif

  /* Point to the location which would point to the next CONS cell, if
     it existed. */

  tail = (CONS **)&cpd->list_head;

  /* Set up an exception handler */

  if ((tag = setjmp(cpd->error_env)) != 0) {
    syntax_check_only = 1;
    resync(cpd, tag);
    if (end_of_file(cpd))           /* Avoid infinite exceptions */
      return(cp_error_msgs[0]); }

  skip_whitespace(cpd);

  /* Go for it!  Don't stop until the end of the file */

  while (!end_of_file(cpd)) {
    type = get_type(cpd);
    id = get_id(cpd);
    dims = get_dims(cpd);

    /* Backwards compatibility time */

    if (dims[0] != 0)
      if (dims[1] != 0)
        type = type | (unsigned char)DIM2;             /* Two or more */
      else
        type = type | (unsigned char)DIM1;             /* One */

    get_assignment(cpd);
    value = get_value(cpd, (int)type, id, dims, (char *)NULL, syntax_check_only);

    /* If everything is ok, allocate another CONS cell and attach it on
       the chain */

    if (!syntax_check_only) {
      foo = (CONS *)allocate_memory(cpd, 1, sizeof(CONS));
      if (foo == (CONS *)NULL)
        return(cp_error_msgs[malloc_error]);

      /* Note that the most recent "value" of a CONS cell can always be
         found in the value slot.  Also, a value which was allocated by
         CP, at some time (if any), for this CONS cell can always be found
         in the avalue slot.  The point is this - objects hanging off the
         avalue slot must be FREEd by CP (through a simple recursive
         descent) - they're are responsibility */

      foo->avalue = foo->value =
        (REQUESTPARM *)allocate_memory(cpd, 1, sizeof(REQUESTPARM));

      *tail = foo;
      tail = (CONS **)&foo->cdr;

      foo->value->value = value;
      foo->value->type = type;
      foo->value->name = id;
      foo->value->dims = dims;
      foo->value->found = TXbool_True; }

    get_ctoken(cpd, ';');
    skip_whitespace(cpd); }

  /* Slap a NULL on the end, close up all files, and return */

  *tail = (CONS *)NULL;

#ifdef LISTING
  if (!cpd->line_printed) {
    fputs(cpd->line_buff, cpd->listing_desc);
    cpd->line_printed = TXbool_True; }
  fclose(cpd->listing_desc);
#endif

  fclose(cpd->cp_desc);

  return((syntax_check_only ? cp_error_msgs[0] : (char *)NULL));
}
/**********************************************************************/

/**********************************************************************/
static char *
read_args(cpd, argc, argv)
CP *cpd;
int argc;
char **argv;

/* Update the contents of a CONS list as specified by an argc/argv type
   list. */

{
  CONS **foo;
  int tag, i;
  TXbool syntax_check_only = TXbool_False;
  char *id;
  REQUESTPARM *rp;

#ifdef LISTING
  if ((cpd->listing_desc = fopen(cpd->listing_pathname, "w")) == (FILE *)NULL)
    return(cp_error_msgs[open_error]);
#endif

  /* Set ourselves into force mode */

  cpd->line_printed = TXbool_False;
  cpd->line_number = 0;
  cpd->force_mode = TXbool_True;
  i = 0;

  /* Set up the exception handler */

  if ((tag = setjmp(cpd->error_env)) != 0) {
    syntax_check_only = 1;
    i += 2;
    resync(cpd, tag); }

  /* We're expecting the data in "id" "value" pairs. */

  while (i < argc) {

    copy_plus_space(cpd, argv[i]);
    id = get_id(cpd);

    foo = find_entry(cpd, id);
    if (*foo == (CONS *)NULL)
      parse_error(cpd, required_entry_not_found);
    rel_memory(id);
    rp = (*foo)->value;

    copy_plus_space(cpd, argv[i+1]);

    /* Store the new value - if we're overwriting a value we previously
       allocated, release that prior value */

    if ((*foo)->avalue != (REQUESTPARM *)NULL) {
      rel_value(rp->value, (int)rp->type, rp->dims, 1);
      rp->value = get_value(cpd, (int)rp->type, id, rp->dims, (char *)NULL, syntax_check_only); }
    else

      /* Just overwrite, the application is responsible for it's own GC */

      get_value(cpd, (int)rp->type, id, rp->dims, rp->value, syntax_check_only);

    i += 2; }

#ifdef LISTING
  fclose(cpd->listing_desc);
#endif

  cpd->force_mode = TXbool_False;

  return((syntax_check_only ? cp_error_msgs[0] : (char *)NULL));
}
/**********************************************************************/

/* from cputil.c */

#ifdef DEBUG_HEAP
/*
   Yes, I know this isn't re-entrant code, but so what?  Debugging
   the release of heap allocated data should be done in solo mode
   anyhow
*/

#define MEM_ENTRY struct mem_entry
MEM_ENTRY {
  char *ptr;
  unsigned int elements, bytes };

MEM_ENTRY mem_entries[1000];
int mem_index, release_count = 0;
#endif

#ifdef DEBUG_HEAP
/**********************************************************************/
static void
open_mem()

/* This routine should be called at the very beginning of an application
   program */

{
  mem_index = 0;
  release_count = 0;
}
/**********************************************************************/

/**********************************************************************/
static void
close_mem()

/* This routine should be called at the very end of an application program */

{
  int i;

  for(i=0; i!=mem_index; i++)
    if (mem_entries[i].ptr != NULL)
      printf("CLOSE_MEM:  %u, %u, %u\n", (unsigned int)mem_entries[i].ptr,
             mem_entries[i].elements, mem_entries[i].bytes);
}
/**********************************************************************/
#endif                                                  /* DEBUG_HEAP */


/**********************************************************************/
static char *
allocate_memory(cpd, elements, bytes)
CP *cpd;
unsigned int elements, bytes;

/* Put a wrapper around malloc for debug purposes */

{
  char *foo;

  foo = (char *)calloc(elements,bytes);
  if (foo == NULL)
    parse_error(cpd, malloc_error);

#ifdef DEBUG_HEAP
  mem_entries[mem_index].ptr = foo;
  mem_entries[mem_index].elements = elements;
  mem_entries[mem_index++].bytes = bytes;
#endif

  return(foo);
}
/**********************************************************************/

/**********************************************************************/
static void
rel_memory(ptr)
char *ptr;

/* Put a wrapper around free for debug purposes */

{
#ifdef DEBUG_HEAP
  int i;
#endif

  free(ptr);

#ifdef DEBUG_HEAP
  i = 0;
  while (i!=mem_index) {                /* Let's be verbose for once! */
    if (mem_entries[i].ptr == ptr)
      break;
    else
      i++; }

  if (i == mem_index) {
    printf("RELEASE_MEMORY: %u\n", (unsigned int)ptr);
    exit(0); }                         /* Bogus free "mem" */

  mem_entries[i].ptr = NULL;
  release_count++;
#endif
}
/**********************************************************************/

/**********************************************************************/
static TXbool
end_of_file(cpd)
CP *cpd;

/* Whether or not we're at the logical end of file depends upon whether
   we're in force mode or not. */

{
  return((TXbool)(((cpd->force_mode) && (cpd->line_buff[cpd->line_ptr] == '\0')) ||
                ((!cpd->force_mode) && (feof(cpd->cp_desc)))));
}
/**********************************************************************/

/**********************************************************************/
static void
rel_value(char *value, int type, uint *dims, int at_top)
/* A recursive walk to free up any memory that was allocated to store
   a specific data value.  For discrete objects, such as ints, this
   is trivial; for three dimensional arrays it's slightly more interesting */

{
  if (value != (char *)NULL)
    if ((type & ARRAYMASK) == 0) {
      if (type == Stype)
        rel_memory(*((char **)value));
      rel_memory(value); }
    else {
      unsigned int i=0;

      if (at_top)
        rel_value(*((char **)value), type, dims, 0);
      else if (dims[1] == 0) {
        if ((type & TYPEMASK) == Stype)
          while(i!=dims[0])
            rel_memory(((char **)value)[i++]); }
      else
        while (i!=dims[0])
          rel_value(((char **)value)[i++], type, &dims[1], 0);

      rel_memory(value); }
}
/**********************************************************************/

/**********************************************************************/
static char *
ea(array, type, dims, index)
char *array;
int type;
unsigned int *dims;
int index;
/* Return the (e)ffective (a)ddress for an array index. */

{
  if (dims[1] == 0)
    switch (type & TYPEMASK) {
      case Btype : return((char *)&((TXbool*)array)[index]);
      case Ctype : return((char *)&((char  *)array)[index]);
      case Itype : return((char *)&((int   *)array)[index]);
      case UItype: return((char *)&((uint  *)array)[index]);
      case Ltype : return((char *)&((long  *)array)[index]);
      case ULtype: return((char *)&((ulong *)array)[index]);
      case Ftype : return((char *)&((float *)array)[index]);
      case Stype : return((char *)&((char **)array)[index]);
      default: return(NULL); }
  else
    return((char *)&((char **)array)[index]);
}
/**********************************************************************/

/**********************************************************************/
static CONS **
find_entry(cpd, id)
CP *cpd;
char *id;

/* Locate the specific CONS cell for the specified id.  Return
   a ptr. to the pointer which points to the CONS cell.  If the object
   is not found, return a pointer to the pointer which would point
   to the CONS cell (i.e., the one at the tail of the list) */

{
  CONS **foo;

  foo = (CONS **)&cpd->list_head;
  while (*foo != (CONS *)NULL)
    if (((*foo)->value != (REQUESTPARM *)NULL) &&
        (!strcmpi((*foo)->value->name, id)))
      return(foo);
    else
      foo = (CONS **)&(*foo)->cdr;
  return(foo);
}
/**********************************************************************/

extern int cpfindtype ARGS((CP *,char *));
/**********************************************************************/
int
cpfindtype(cpd,id)           /* MAW 02-03-93 - find type for variable */
CP *cpd;
char *id;
{
CONS **ent;

   ent=find_entry(cpd,id);
   if(*ent==(CONS *)NULL) return(0);
   return((int)(*ent)->value->type&0xff);
}                                                 /* end cpfindtype() */
/**********************************************************************/

/* from cpw.c */

/* Note that the organization of this file is almost the reverse of
   the parser - we talk about printing tokens (i.e., strings separated
   by whitespace), etc.  One additional parameter, on most every
   routine, is the current (i)ndent (l)evel, specifying how far to
   indent after a newline.

   All in all, it really came out nice. */


static char *build_echar    ARGS((int, char *,int));
static void  print_new_line ARGS((CP *, int));
static void  print_token    ARGS((CP *, char *, int,int));
static void  print_type     ARGS((CP *, int, int));
static void  print_dims     ARGS((CP *, unsigned int *, int));
static void  print_value    ARGS((CP *, char *, int, unsigned int *, int));

/**********************************************************************/
static char *
build_echar(c, buff, instring)
int c;
char *buff;
int instring;
/* Create an extended character, one possibly specified as an escape
   sequence. Return the next available buffer address. */

{
  int i = 0;

  /* We need to know whether the character will be printed inside a
     string, or inside a char spec */

  if ((!isprint(c)) || (c == '\\') || ((instring) && (c == '"')) ||
      ((!instring) && (c == '\''))) {
    buff[i++] = '\\';
    switch (c) {
      case '\n': buff[i++] = 'n'; break;
      case '\t': buff[i++] = 't'; break;
      case '\b': buff[i++] = 'b'; break;
      case '\r': buff[i++] = 'r'; break;
      case '\f': buff[i++] = 'f'; break;
      case '\\': buff[i++] = '\\'; break;
      case '\'': buff[i++] = '\''; break;
      case '"' : buff[i++] = '"'; break;
      default:
        if (c) {
          buff[i++] = 'x';
          ltoa((long)c, &buff[i], 16); i = strlen(buff); }
        else
          buff[i++] = '0'; }}
  else
    buff[i++] = (char)c;

  return(buff + i);
}
/**********************************************************************/

/**********************************************************************/
static void
print_new_line(cpd, il)
CP *cpd;
int il;

/* Print a newline and indent to the specified column */

{
  cpd->line_number++;
  fputc('\n', cpd->cp_desc);
  for (cpd->line_ptr = 0; cpd->line_ptr != il; cpd->line_ptr++)
    fputc(' ', cpd->cp_desc);
}
/**********************************************************************/

/**********************************************************************/
static void
print_token(cpd, token, il, nospace)
CP *cpd;
char *token;
int il;
int nospace;
/* Print a token, possibly without an intervening space between
   the previous token */

{
  int i;

  i = strlen(token);
  if ((cpd->line_ptr + i + (nospace ? 0 : 1)) > max_wrap_length)
    print_new_line(cpd, il);
  else
    if ((!nospace) && (cpd->line_ptr))
      (cpd->line_ptr++, fputc(' ', cpd->cp_desc));

  fputs(token, cpd->cp_desc);
  cpd->line_ptr = cpd->line_ptr + i;
}
/**********************************************************************/

/**********************************************************************/
static void
print_type(cpd, type, il)
CP *cpd;
int type;
int il;
{
  switch(type&TYPEMASK){
    case Btype : print_token(cpd, "TXbool " , il, 0); break;
    case Ctype : print_token(cpd, "char " , il, 0); break;
    case Itype : print_token(cpd, "int  " , il, 0); break;
    case UItype: print_token(cpd, "uint " , il, 0); break;
    case Ltype : print_token(cpd, "long " , il, 0); break;
    case ULtype: print_token(cpd, "ulong" , il, 0); break;
    case Ftype : print_token(cpd, "float" , il, 0); break;
    case Stype : print_token(cpd, "char *" , il, 0); break;
    default    : print_token(cpd, "*ERROR*", il, 0); break;
  }
}
/**********************************************************************/

/**********************************************************************/
static void
print_dims(cpd, dims, il)
CP *cpd;
unsigned int *dims;
int il;
{
  int i = 0, j;

  cpd->token_buff[0] = '[';
  while (dims[i]) {
    ltoa((long)dims[i++], &cpd->token_buff[1], 10);
    j = strlen(cpd->token_buff);
    cpd->token_buff[j++] = ']';
    cpd->token_buff[j] = '\0';
    print_token(cpd, cpd->token_buff, il, (TXbool)((i > 0) ? 1 : 0)); }
}
/**********************************************************************/

/**********************************************************************/
static void
print_value(cpd, value, type, dims, il)
CP *cpd;
char *value;
int type;
unsigned int *dims;
int il;
{
  char *end;

  if (dims[0] == 0)
    switch (type & TYPEMASK) {
      case Btype:
        if (*((TXbool *)value))
          print_token(cpd, "TRUE", il, 0);
        else
          print_token(cpd, "FALSE", il, 0);
        break;
      case Ctype:
        cpd->token_buff[0] = '\'';
        end = build_echar((int)*value, &cpd->token_buff[1], 0);
        *end++ = '\'';
        *end = '\0';
        print_token(cpd, cpd->token_buff, il, 0);
        break;
      case Itype:
        ltoa((long)*((int *)value), cpd->token_buff, 10);
        print_token(cpd, cpd->token_buff, il, 0);
        break;
      case UItype:
        ultoa((ulong)*((uint *)value), cpd->token_buff, 10);
        print_token(cpd, cpd->token_buff, il, 0);
        break;
      case Ltype:
        ltoa(*((long *)value), cpd->token_buff, 10);
        print_token(cpd, cpd->token_buff, il, 0);
        break;
      case ULtype:
        ultoa(*((ulong *)value), cpd->token_buff, 10);
        print_token(cpd, cpd->token_buff, il, 0);
        break;
      case Ftype:
        /*gcvt((double)*((float *)value), 7, cpd->token_buff); not portable*/
        sprintf(cpd->token_buff,"%.7g",(double)*((float *)value));
        print_token(cpd, cpd->token_buff, il, 0);
        break;
      case Stype:
        {
          int i,j;
          int bump;

          /* Employ a simple pretty-printing heuristic - if the string
             won't fit on the current line (approximately that is!), and
             it will fit on the next line, skip a newline right now.
             the effects of escapment are not considered. */

          j = strlen(*((char **)value));
          bump = (int)(((j + il + 2) <= max_wrap_length) &&
                       ((j + cpd->line_ptr + 3) > max_wrap_length));
          if (bump)
            print_new_line(cpd, il);

          print_token(cpd, "\"", il, bump);
          i = 0;
          while(i != j){
            end = build_echar((int)(*((char **)value))[i++], cpd->token_buff, 1);
            *end = '\0';
            if((cpd->line_ptr + strlen(cpd->token_buff)) > (max_wrap_length - 1))
              (cpd->line_ptr = 0, fputs("\\\n", cpd->cp_desc));
            fputs(cpd->token_buff, cpd->cp_desc);
            cpd->line_ptr += strlen(cpd->token_buff);
          }

          fputc('"', cpd->cp_desc);
          cpd->line_ptr++;
          break; }
      default:
        print_token(cpd, "*ERROR*", il, 0);
    }
  else{
    unsigned int i;

    /* Arrays are a simple recursive descent. */

    print_token(cpd, "{", il, 0);
    il+=4;                                            /* MAW 03-08-91 */
    print_new_line(cpd, il);                          /* MAW 03-07-91 */
    i = 0;
    while(i!=dims[0]){
      print_value(cpd,ea(*((char **)value),type,dims,i++),type,&dims[1],il);
      if(i!=dims[0])
        print_token(cpd, ",", il, 1);
    }
    il-=4;                                            /* MAW 03-08-91 */
    print_new_line(cpd, il);                          /* MAW 03-07-91 */
    print_token(cpd, "}", il, 0);
  }
}
/**********************************************************************/

/**********************************************************************/
static char *
write_file(cpd)
CP *cpd;
{
  CONS *next;
  REQUESTPARM *foo;

  if (cpd == (CP *)NULL)
    return((char *)NULL);

  if ((cpd->cp_desc = fopen(cpd->cp_pathname, "w")) == (FILE *)NULL)
    return(cp_error_msgs[open_error]);

  next = cpd->list_head;
  while (next != (CONS *)NULL) {
    foo = next->value;

    /* Don't write zero length arrays */

    if(((foo->type & ARRAYMASK) == DIM0) || foo->dims[0]) {
      print_type(cpd, (int)foo->type, 0);
      print_token(cpd, foo->name, 0, (int)((foo->type & TYPEMASK) == Stype));
      print_dims(cpd, foo->dims, 0);
      print_token(cpd, "=", 0, 0);
      print_value(cpd, foo->value, (int)foo->type, foo->dims, 0);
      print_token(cpd, ";\n", 0, 1);
      cpd->line_ptr = 0;
    }

    next = next->cdr;
    /*fflush(cpd->cp_desc);*/        /* MAW 03-07-91 - wtf why flush? */
  }

  fclose(cpd->cp_desc);
  return((char *)NULL);                /* 2/24/88 - MAW, was fallthru */
}
/**********************************************************************/
