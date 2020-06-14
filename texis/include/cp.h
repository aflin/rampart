#ifndef CP_H
#define CP_H
/************************************************************************/
/**                                                                    **/
/** EPI Control Program Header                                         **/
/**                                                                    **/
/** 12/01/88 MAG - In the beginning...                                 **/
/** 12/22/88 MAG - Beta release, comments added                        **/
/** 03/07/89 MAW - combined into one file                              **/
/** 03/06/91 MAW - added uint/ulong defines                            **/
/**                                                                    **/
/************************************************************************/
#ifndef SETJMP
#define SETJMP
#  include <setjmp.h>
#endif

#include "txtypes.h"

/*#define DEBUG_HEAP*/
/*#define NEW_DIMS_ONLY*/
/*#define LISTING*/
/*#define PR_ERRORS*/                                   /* MAW 3/7/89 */
#define CP_2                        /* MAW - to tell from original CP */
/**********************************************************************
   The current CP is believed reentrant.  We are, of course, assuming that
   library functions such as I/O, heap allocation, etc, are also reentrant.

   Three compile-time flags are available when compiling CP:

       DEBUG_HEAP - if defined, attempt to verify that all heap allocated
                    data is released (see open_mem and close_mem in CPUTIL.C
                    for more details).  CP deallocates only data which was
                    allocated by CP (of course).
       NEW_DIMS_ONLY - if defined, only the new method of specifying array
                       dimensions can be used.  If not defined, only alength
                       and vlength should be used.

                       NOTE:  This flag affects the user interface of CP ONLY!
                              Regardless of the status of this flag, only the new
                              method of dims is utilized internally (i.e., a
                              zero terminated array of integers).
       LISTING - if defined, a list file is created.  The list file is
                 a copy of the source file, with error messages (if any)
                 interspersed.

                 NOTE:  This option is not completely implemented. See CP.C
                        for more details/incomplete code.
***********************************************************************/
#ifndef uchar                                         /* MAW 03-06-91 */
#  define uchar unsigned char
#endif
#ifndef uint                                          /* MAW 03-06-91 */
#  define uint unsigned int
#endif
#ifndef ulong                                         /* MAW 03-06-91 */
#  define ulong unsigned long
#endif
/**********************************************************************/
#define max_token_length  132       /* Maximum size of a lexical unit */
#define max_dimensions    10  /* Maximum number of array dimensions-1 */
#define max_line_length   132                        /* Buffer a line */
                                       /* 2/15/89 - maw - huge buffer */
#define max_string_length 5120      /* More code could eliminate this */
#define max_wrap_length   75      /* Maximum line length during write */

/* Define the valid types. - also indecies into type_size[]! */
#define ENDLIST    0x00
#define Btype      0x01                                    /* boolean */
#define Ctype      0x02                                       /* char */
#define Itype      0x03                                        /* int */
#define UItype     0x04             /* unsigned int *//* MAW 03-06-91 */
#define Ltype      0x05                                       /* long */
#define ULtype     0x06            /* unsigned long *//* MAW 03-06-91 */
#define Ftype      0x07                                      /* float */
#define Stype      0x08                                     /* string */

#define TYPEMASK   0x0f

#define DIM0       0x00
#define DIM1       0x10
#define DIM2       0x20

#define ARRAYMASK  0x30

#define CPOPTIONAL 0x80            /* don't give up if item not found */
/**********************************************************************
   Application programs use the REQUESTPARM type to pass data objects to
   and from CP.  A data object has a type, name, value, and list of dimensions.

   Discrete objects are said to have rank zero (a la APL).

   For backward compatibility, arrays of rank two or less may be
   specified using the 'alength' & 'vlength' designators.
*/

#define REQUESTPARM struct requestparm
REQUESTPARM {
  unsigned char type;       /* Type of object, pseudo C */
  char *value;              /* Ptr to value */
  char *name;               /* Ptr to name */
  unsigned int *dims;       /* Zero terminated array of dimensions */
#ifndef NEW_DIMS_ONLY
  unsigned int vlength;     /* !! retained for compatibility!! */
  unsigned int alength;     /* !! retained for compatibility!! */
  unsigned int dim0;        /* Static allocation of 'dims[0]' */
  unsigned int dim1;        /* Static allocation of 'dims[1]' */
  unsigned int terminator;  /* Necessary for compatibility - always zero */
#endif
  TXbool found;             /* set if item was read properly */
};
/**********************************************************************/

/**********************************************************************
   CONS cells maintain the list of data objects associated with a given
   CP file.  Data objects may become associated with a CP file through
   two possible methods.

   1.  If a CP file is opened for (r)ead or (u)pdate, then space for the
       objects are allocated by CP and their values copied from the
       file into memory.
   2.  If a CP file is opened for (w)rite or (u)pdate, then previously
       allocated objects may be presented to CP for inclusion/update.
*/

#define CONS struct cons
CONS {
  REQUESTPARM *avalue;          /* Ptr to value if allocated by CP */
  REQUESTPARM *value;           /* Ptr to value, regardless of allocation */
  CONS *cdr;                    /* Link to next object */
};
/**********************************************************************/

/**********************************************************************
   The CP type is analagous to the FILE type in C.

   Special notes:
     1. The listing descriptor and listing pathname are not valid
        unless the c sources are compiled with LISTING; even then,
        the method of specifying the list file name has yet to be
        encoded.
     2. A new file mode has been added - update.  Informally, update
        states that we wish to keep the entire contents of the current file,
        except for a few specified changes.
     3. error_env is a generic exception handler employed by all parse
        routines.
     4. if force_mode = TRUE, then data is read solely from the line_buff
        string as opposed to the CP file.
*/

#define CP struct cp
CP {
  FILE *cp_desc;
  char *cp_pathname;
#ifdef LISTING
  FILE *listing_desc;
  char *listing_pathname;
#endif
  char *cp_mode;                                  /* 'R', 'W', or 'U' */
  CONS *list_head;                        /* Data read and/or written */
  char token_buff[max_token_length], line_buff[max_line_length];
  char buff[max_string_length];/* 10/23/89 - maw - was static in get_string() */
  int token_size, line_ptr, line_number;
  TXbool line_printed, force_mode;
  char *error_msg;                                         /* Offense */
  jmp_buf error_env;
  TXbool all_found;                /* did we find everything we needed? */
};
/**********************************************************************/
                                              /* Valid error messages */
#define EOF_too_soon                    1
#define too_many_ungets                 2
#define token_too_big                   3
#define needed_type                     4
#define needed_identifier               5
#define malloc_error                    6
#define too_many_dimensions             7
#define needed_integer                  8
#define needed_left_bracket             9
#define token_not_found                10
#define open_error                     11
#define needed_float                   12
#define line_too_long                  13
#define peek_ahead_failure             14
#define too_many_elements_in_dimension 15
#define needed_nonzero_integer         16
#define needed_bool                    17
#define string_too_long                18
#define internal_program_failure       19
#define required_entry_not_found       20
#define file_not_opened_for_write      21
#define file_not_opened_for_read       22
#define hex_character_expected         23
#define octal_character_expected       24

/**********************************************************************/
extern CP   *opencp     ARGS((char *,char *));
extern CP   *closecp    ARGS((CP *));
extern char *savecp     ARGS((CP *));/* MAW 02-03-93 - save, no close */
extern char *renamecp   ARGS((CP *,char *));           /* MAW 6/13/89 */
extern char *readcp     ARGS((CP *,REQUESTPARM *));
extern char *readcpargs ARGS((CP *, int, char **));
extern char *writecp    ARGS((CP *,REQUESTPARM *));
extern uint  numentries ARGS((CP *, char *));
extern int   cpfindtype ARGS((CP *,char *));          /* MAW 02-03-93 */
/**********************************************************************/
#endif                                                        /* CP_H */
