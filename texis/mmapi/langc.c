#include "txcoreconfig.h"
#include "stdio.h"
#include "sys/types.h"
#include "errno.h"
#include "string.h"
#include "ctype.h"
#include "sizes.h"
#include "os.h"
#include "cp.h"
#include "mctrl.h"
#include "pm.h"
#include "mmsg.h"
#include "mdx.h"
#include "presuf.h"
#include "fnlist.h"
#include "mm3e.h"


/* WTF this is here in eng lib (instead of texis lib) to avoid link errors: */
int     TxLocaleSerial = 0;


#if PM_NEWLANG                                        /* MAW 03-04-97 */
/*
** langc[] is what is used to decide if a search item is language 
** wordc[] is what is used to expand found morphemes to words
** These lists should generally be the same except for phrase chars like
** dash and space.
*/
static const char langcrexdef[] = "[\\alpha' \\-]";     /* default */
/* large enough for `[\x00...\xFF]': */
static char langcrex[1+4*DYNABYTE+1+1] = "";            /* actual if set */
static byte langc[DYNABYTE];                            /* compiled */

static const char wordcrexdef[] = "[\\alpha']";         /* default */
/* large enough for `[\x00...\xFF]': */
static char wordcrex[1+4*DYNABYTE+1+1] = "";            /* actual if set */
static byte wordc[DYNABYTE];                            /* compiled */

static int didwlc=0;
static int locale_serial = -1;          /* locale serial # at init */


int
TXgetlocaleserial()
{
  return(TxLocaleSerial);
}

static void iinitwlc ARGS((void));
static void
iinitwlc()
{
byte    *p;

   if (didwlc && locale_serial == TXgetlocaleserial()) return;
   /* old way; now we save actual REX for recompile after locale change:
   for(i=0;i<DYNABYTE;i++)
      wordc[i]=langc[i]=(isalpha(i)?1:0);
   wordc['\'']=1;
   langc['\'']=1;
   langc[' ']=1;
   langc['-']=1;
   */
   p = (byte *)(*langcrex != '\0' ? langcrex : langcrexdef);
   memset(langc, 0, sizeof(langc));
   dorange(&p, langc);
   p = (byte *)(*wordcrex != '\0' ? wordcrex : wordcrexdef);
   memset(wordc, 0, sizeof(wordc));
   dorange(&p, wordc);

   didwlc=1;
   locale_serial = TXgetlocaleserial();
}
byte *pm_getlangc(){ iinitwlc(); return(langc); }
byte *pm_getwordc(){ iinitwlc(); return(wordc); }
void  pm_initwlc (){ didwlc=0; iinitwlc(); }

int
pm_setlangc(s)
CONST char      *s;
/* Set langc to REX range `s', or default if NULL/empty.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "pm_setlangc";

  if (s == CHARPN) s = "";                      /* use default */
  if (strlen(s) >= sizeof(langcrex))
    {
      putmsg(MERR + MAE, fn, "REX expression `%s' for langc is too large",
             s);
      return(0);
    }
  TXstrncpy(langcrex, s, sizeof(langcrex));
  didwlc = 0;                                   /* delay init for speed */
  /* WTF cached SPMs etc. may not know to call pm_getlangc() etc. to
   * see if it's changed the next time they getspm(), so re-compile
   * here instead of waiting for pm_getlangc(): they do use our
   * `wordc'/`langc' array directly (without copying to private
   * buffer), so our changes will take effect (at slight expense of
   * re-compiling several times if langc and wordc both set):
   */
  pm_initwlc();
  return(1);                                    /* success */
}

int
pm_setwordc(s)
CONST char      *s;
/* Set wordc to REX range `s', or default if NULL/empty.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "pm_setwordc";

  if (s == CHARPN || *s == '\0') s = wordcrexdef;
  if (strlen(s) >= sizeof(wordcrex))
    {
      putmsg(MERR + MAE, fn, "REX expression `%s' for wordc is too large",
             s);
      return(0);
    }
  TXstrncpy(wordcrex, s, sizeof(wordcrex));
  didwlc = 0;                                   /* delay init for speed */
  pm_initwlc();                                 /* WTF see pm_setlangc() */
  return(1);                                    /* success */
}

#endif                                                  /* PM_NEWLANG */
