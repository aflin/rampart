/* PBR 01-19-90 - Added the fnameseek function                      */
/* PBR 03-01-90 - file name list support                            */
/* MAW 08-27-90 - converted fread() to read() for MSDOS             */
/* PBR 11-27-90 - added hit function                                */
/* PBR 01-29-91 - fixed intersections<sets-1 bug                    */
/* PBR 01-29-91 - chged setup hit so it looks for edx after 1st SEL */
/* PBR 02-01-91 - added "member" to SEL to mean member-of-this-hit  */
/* PBR 06-02-91 - forced mindex to use ctr of hit as its reference  */
/* PBR 08-23-91 - fixed small bug with the ea->lang init function   */
/**********************************************************************/
#include "txcoreconfig.h"
#include "stdio.h"
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#ifdef MVS
#  if MVS_C_REL > 1
#     include <stddef.h>
#  else
#     include <stdefs.h>
#  endif
#endif
#if defined(MSDOS)
#  if !defined(AUTHOR_COPY) && !defined(PROTECT_IT)
#     define PROTECT_IT
#  endif
#  define LOWIO
#endif
#include "errno.h"
#include "string.h"
#include "ctype.h"
#include "api3.h"
#include "unicode.h"
#include "txtypes.h"
#include "mctrl.h"
#include "fnlist.h"
#ifdef PROTECT_IT
#   include "box.h"
#endif
#ifdef unix
#  define RDPACKED /* MAW 09-10-92 - read packed/compressed files via popen() */
#endif
#ifndef CPNULL
#  define CPNULL (char *)NULL
#endif

#ifdef _WIN32
#  define _huge
#endif

#ifdef fwrite             /* MAW 08-10-92 - will be writing to stream */
#  undef fwrite
#endif

/* wtf move to APICP/MM3S eventually? */
extern int      TXwildoneword;
extern int      TXwildsufmatch;

/* see pm.h for bits: */
TX_TMF  TXtraceMetamorph = (TX_TMF)0;

/* Bug 5420; should probably always be on, but is changeable just in case: */
int     TXfindselLoopCheck = 1;

static const char       TraceMsgFmt[] =
  "%s of SEL #%d `%s' hit at %+wd length %d `%s' %s: %s";
#define EL(ms, n)       ((ms)->el[(n)])
#define TRACE_MSG(verb, ms, n, okStr, reasonStr)                        \
  {                                                                     \
    char        traceContextBuf[256];                                   \
    size_t      selHitOffset = EL(ms, n)->hit - (ms)->start;            \
    size_t      selHitLen = EL(ms, n)->hitsz;                           \
                                                                        \
    TXmmShowHitContext(traceContextBuf, sizeof(traceContextBuf),        \
                       -1, 0, &selHitOffset, &selHitLen, 1,             \
                       (char *)(ms)->start, (ms)->end - (ms)->start);   \
    putmsg(MINFO, CHARPN, TraceMsgFmt,                                  \
           (verb), (int)(n),                                            \
           EL(ms, n)->lst[0], (EPI_HUGEINT)(EL(ms, n)->hit - (ms)->start), \
           (int)EL(ms, n)->hitsz, traceContextBuf, (okStr), (reasonStr));  \
  }

#ifndef FORMMAPI
static byte *spacecat ARGS((byte *));
#endif
       int   setuphit ARGS((MM3S *,int ));
       void  printhit ARGS((MM3S *,byte *,long,byte *));
static int   englcmp  ARGS((byte *,byte *, MM3S *ms, SEL *sel));
       int   mm3emain ARGS((int,char **));
       int   CDECL main     ARGS((int,char **));
/**********************************************************************/
                        /* STATIC GLOBAL DATA */
/************************************************************************/

#ifndef FORMMAPI
extern char *mdxpath;  /* MAW 8-9-89 - added to prevent compiler diag */

static MM3S _mcf;    /* this is the temporary storage area for control data */

static REQUESTPARM rqp[]=
{
#define INIT_REST	NULL, 0, 0, 0, 0, 0, 0
  {Btype     ,(char *)&_mcf.suffixproc   ,MM_SUFFIXPROC,   INIT_REST },
  {Btype     ,(char *)&_mcf.prefixproc   ,MM_PREFIXPROC,   INIT_REST },
  {Btype     ,(char *)&_mcf.rebuild      ,MM_REBUILD,      INIT_REST },
  {Btype     ,(char *)&_mcf.filter_mode  ,MM_FILTER_MODE,  INIT_REST },
  {Btype     ,(char *)&_mcf.incsd        ,MM_INC_SDEXP,    INIT_REST },
  {Btype     ,(char *)&_mcf.inced        ,MM_INC_EDEXP,    INIT_REST },
  {Itype     ,(char *)&_mcf.minwordlen   ,MM_MINWORDLEN,   INIT_REST },
  {Itype     ,(char *)&_mcf.intersects   ,MM_INTERSECTS,   INIT_REST },
  {Stype     ,(char *)&_mcf.sdexp        ,MM_SDEXP,        INIT_REST },
  {Stype     ,(char *)&_mcf.edexp        ,MM_EDEXP,        INIT_REST },
  {Stype     ,(char *)&_mcf.query        ,MM_QUERY,        INIT_REST },
  {Stype|CPOPTIONAL,(char *)&mdxpath       ,MM_MDXPATH,      INIT_REST },
  {Stype|CPOPTIONAL,(char *)&_mcf.cmd      ,MM_HITFUNCTION,  INIT_REST },
  {Stype|DIM1,(char *)&_mcf.set          ,MM_EQUIVS,       INIT_REST },
  {Stype|DIM1,(char *)&_mcf.suffix       ,MM_SUFFIX,       INIT_REST },
  {Stype|DIM1,(char *)&_mcf.prefix       ,MM_PREFIX,       INIT_REST },
  {Stype|DIM1|CPOPTIONAL,(char *)&_mcf.searchfiles,MM_SEARCHFILES, INIT_REST},
  {Btype|DIM0|CPOPTIONAL,(char *)&freadex_strip8, MM_HIGHBITSTRIP, INIT_REST},
  {ENDLIST, NULL, 0, INIT_REST }
#undef INIT_REST
};
/*
  {Stype     ,(char *)&_mcf.msgfile      ,MM_MSGFILE      },
*/
#endif                                                    /* FORMMAPI */

static CONST char Out_of_mem[]="Out of memory";
static CONST char   CannotAlloc[] = "Cannot alloc %lu bytes of memory: %s";
#define maerr() putmsg(MERR+MAE,Fn,Out_of_mem);
/************************************************************************/
#ifdef RDPACKED                                       /* MAW 09-10-92 */
#define PXHITFRAME 1024     /* size of text surrounding a packed hit */
#define PX struct px_s
PX {
        char *ext;
        char *cmd;
};
PX pxlst[]={
        { ".Z", "zcat <" },
        { ".taz", "zcat <" },                         /* MAW 04-23-93 */
        { ".z", "pcat " },
#ifdef __linux__                         /* MAW 04-29-96 - add gunzip */
        { ".tgz", "gunzip <" },
        { ".gz", "gunzip <" },
#endif
        { (char *)NULL, (char *)NULL }
};

static int tailcmp ARGS((char *tail,char *s,int slen));
static int tailcmp(t,s,l)
char *t, *s;
int l;
{
int tl=strlen(t);

        if(l<tl) return(1);
        l-=tl;
        return(strcmp(t,s+l));
}


static int ispx ARGS((char *));
static int
ispx(s)
char *s;
{
 PX *pxp;
 int l=strlen(s);
 for(pxp=pxlst;pxp->ext!=(char *)NULL;pxp++)
    if(!tailcmp(pxp->ext,s,l)) return(1);
 return(0);
}

/**********************************************************************/
#else                                                     /* !RDPACKED */
#  ifdef MVS
#     define opensrch(a,b) fopen((a),"r")
#     define closesrch(a,b) fclose(a)
#  else
#     define opensrch(a,b) fopen((a),"rb")
#     define closesrch(a,b) fclose(a)
#  endif
#endif                                                    /* !RDPACKED */
/************************************************************************/
                             /* FUNCTIONS */
/************************************************************************/

int
TXmmShowHitContext(char *outBuf, size_t outBufSz, size_t hitOffset,
                   size_t hitLen, size_t *subHitOffsets, size_t *subHitLens,
                 size_t numSubHits, const char *searchBuf, size_t searchBufSz)
/* Prints context of hit at `hitOffset' in `searchBuf', to `outBuf'.
 * `hitOffset' may be -1 if no hit; will show start of buffer then.
 * `subHitOffsets'/`subHitLens', non-NULL, are parallel arrays of
 * sub-hits to mark (with brackets); NULL-terminted, and offset -1
 * means no hit.  Hit is marked with curly braces, subhits with
 * brackets; both optional.  Returns 0 on error (not enough room in
 * `outBuf').
 */
{
  static const char     ellipsis[] = "...";
#define ELLIPSIS_LEN    (sizeof(ellipsis) - 1)
#define EXTRA_LEN       16      /* preferred pre/post context length */
  const char            *hit, *hitEnd, *context, *contextEnd, *src, *srcEnd;
#define CONTEXT_LEN     ((size_t)(contextEnd - context))
  const char            *searchBufEnd = searchBuf + searchBufSz;
  char                  *d, *e;
  EPI_SSIZE_T           preHitLen = -1, postHitLen = -1;
  size_t                subHitIdx, minNeeded;

  if (!subHitOffsets || !subHitLens) numSubHits = 0;

  minNeeded = 2*ELLIPSIS_LEN + 3 + 2*numSubHits;/* +3 for nul, `{', `}' */
  if (outBufSz <= minNeeded)                    /* `outBuf' too small */
    {
      if (outBufSz > 0) *outBuf = '\0';
      return(0);
    }
  /* For simplicity below, assume we have room for non-text added stuff: */
  outBufSz -= minNeeded;

  if (hitOffset == (size_t)(-1))                /* no overall hit */
    {
      if (numSubHits > 0)
        {
          TXbool        gotValidSubHit = TXbool_False;

          /* Make overall hit a rubber-band around subhits: */
          hit = searchBufEnd;
          hitEnd = searchBuf;
          for (subHitIdx = 0; subHitIdx < numSubHits; subHitIdx++)
            {
              if (subHitOffsets[subHitIdx] == (size_t)(-1)) continue;
              gotValidSubHit = TXbool_True;
              src = searchBuf + subHitOffsets[subHitIdx];
              if (src < hit) hit = src;
              src += subHitLens[subHitIdx];
              if (src > hitEnd) hitEnd = src;
            }
          if (!gotValidSubHit) hit = hitEnd = searchBuf;
        }
      else
        hit = hitEnd = searchBuf;
    }
  else
    {
      hit = searchBuf + hitOffset;
      hitEnd = hit + hitLen;
    }
  if (hit < searchBuf) hit = searchBuf;                 /* sanity */
  if (hitEnd < searchBuf) hitEnd = searchBuf;           /* "" */
  if (hit > searchBufEnd) hit = searchBufEnd;           /* "" */
  if (hitEnd > searchBufEnd) hitEnd = searchBufEnd;     /* "" */
  if (hit > hitEnd) hit = hitEnd;                       /* "" */
  /* Start with context rubber-banded around hit: */
  context = hit;
  contextEnd = hitEnd;
  /* Expand if possible, up to EXTRA_LEN before and after: */
  if (CONTEXT_LEN <= outBufSz)
    {
      size_t    expandSz = 2*EXTRA_LEN;         /* preferred expansion size */

      if (CONTEXT_LEN + expandSz > outBufSz)    /* too big for `outBuf' */
        expandSz = outBufSz - CONTEXT_LEN;
      preHitLen = TX_MIN(expandSz/2, (size_t)(context - searchBuf));
      context -= preHitLen;
      postHitLen = TX_MIN(expandSz - expandSz/2,
                          (size_t)(searchBufEnd - contextEnd));
      contextEnd += postHitLen;
    }
  /* Otherwise shrink if too big: */
  else if (CONTEXT_LEN > outBufSz)
    {
      size_t            shrinkSz = (CONTEXT_LEN) - outBufSz;
      size_t            subHitCenter;
      EPI_SSIZE_T       start;

      /* But try to at least stay centered on a subhit: */
      if (numSubHits > 0 && subHitOffsets[0] != (size_t)(-1))
        {
          subHitCenter = subHitOffsets[0] + subHitLens[0]/2;
          start = (EPI_SSIZE_T)subHitCenter - (EPI_SSIZE_T)(outBufSz/2);
          if (start < 0)
            start = 0;
          else if ((size_t)start + outBufSz > searchBufSz)
            start = searchBufSz - outBufSz;
          context = searchBuf + start;
          contextEnd = context + outBufSz;
        }
      else                                      /* shrink symmetrically */
        {
          context += shrinkSz/2;
          contextEnd -= shrinkSz - (shrinkSz/2);
        }
    }
  /* Copy to `outBuf': */
  d = outBuf;
  if (context > searchBuf)                      /* we truncated beginning */
    {
      memcpy(d, ellipsis, ELLIPSIS_LEN);
      d += ELLIPSIS_LEN;
    }
  src = context;
  /* CONTEXT_LEN == preHitLen + hit length + postHitLen: */
  if (preHitLen > 0)
    {
      memcpy(d, context, preHitLen);
      d += preHitLen;
      src += preHitLen;                         /* brings `src' up to `hit' */
    }
  if (hitOffset != (size_t)(-1) && src == hit) *(d++) = '{';
  /* Copy hit, marking subhits along the way: */
  for (srcEnd = TX_MIN(hitEnd, contextEnd); ; src++)
    {
      for (subHitIdx = 0; subHitIdx < numSubHits; subHitIdx++)
        {
          if (subHitOffsets[subHitIdx] == (size_t)(-1)) continue;
          if ((size_t)(src - searchBuf) == subHitOffsets[subHitIdx])
            *(d++) = '[';
          if ((size_t)(src - searchBuf) ==
              subHitOffsets[subHitIdx] + subHitLens[subHitIdx])
            *(d++) = ']';
        }
      if (src >= srcEnd) break;
      *(d++) = *src;
    }
  if (hitOffset != (size_t)(-1) && src == hitEnd) *(d++) = '}';
  if (postHitLen > 0)
    {
      memcpy(d, hitEnd, postHitLen);
      d += postHitLen;
    }
  if (contextEnd < searchBufEnd)                /* we truncated end */
    {
      memcpy(d, ellipsis, ELLIPSIS_LEN);
      d += ELLIPSIS_LEN;
    }
  *d = '\0';
  /* Zap nuls, controls, newlines: */
  e = d;
  for (d = outBuf; d < e; d++)
    {
      if (TX_ISSPACE(*d)) *d = ' ';
      else if (*(byte *)d < ' ') *d = '?';
    }
  return(1);
#undef ELLIPSIS_LEN
#undef EXTRA_LEN
#undef CONTEXT_LEN
}

int
TXmmSetupHitContext(MM3S *ms, char *contextBuf, size_t contextBufSz)
/* Returns 0 on error (not enough room in `contextBuf').
 */
{
  byte          *startOk = TX_MIN(ms->start, ms->end);
  size_t        selHitOffsets[MAXSELS], selHitLens[MAXSELS];
  int           selIdx;
  SEL           *sel;

  for (selIdx = 0; selIdx < ms->nels; selIdx++)
    {
      sel = ms->el[selIdx];
      selHitOffsets[selIdx] = (sel->hit ? sel->hit - ms->start : -1);
      selHitLens[selIdx] = sel->hitsz;
    }
  return(TXmmShowHitContext(contextBuf, contextBufSz,
                            (ms->hit ? ms->hit - startOk : -1), ms->hitsz,
                            selHitOffsets, selHitLens, ms->nels,
                            (char *)startOk, ms->end - startOk));
}

static unsigned char
getboolval(const char *value)
{
	long longval;

	longval=strtol(value, NULL, 0);
	return longval ? 1 : 0;
}

static int
parsesetting(MM3S *ms, const char *setting)
/* Parses inline API setting `setting' and applies to `ms'.
 * Returns -1 on error, 0 if ok.
 */
{
	size_t settingoffset, settinglen;
	const char *value, *settingname;

	settingoffset=1;
	value = strchr(setting+1, '=');
	if(!value) return -1; /* WTF: Can some settings be tokens, no value? */
	value++;
	settinglen=(value-setting-2);
	settingname = setting + settingoffset;
	switch(settinglen)
	{
	case 7:
		if(strncmp(settingname, "rebuild", 7) == 0)
		{
			ms->rebuild = getboolval(value);
			return 0;
		}
	case 8:
		if(strncmp(settingname, "defsufrm", 8) == 0)
		{
			ms->defsuffrm = getboolval(value);
			return 0;
		}
	case 9:
		if(strncmp(settingname, "defsuffrm", 9) == 0)
		{
			ms->defsuffrm = getboolval(value);
			return 0;
		}
	case 10:
		if(strncmp(settingname, "suffixproc", 10) == 0)
		{
			ms->suffixproc = getboolval(value);
			return 0;
		}
		if(strncmp(settingname, "minwordlen", 10) == 0)
		{
			ms->minwordlen = strtol(value, NULL, 0);
			return 0;
		}
		if(strncmp(settingname, "prefixproc", 10) == 0)
		{
			ms->prefixproc = getboolval(value);
			return 0;
		}
	}
	putmsg(MWARN, "Parse Setting", "Unknown setting: %s", setting);
	return -1;
}

/************************************************************************/


void
rmpresuf(daword,ms)    /* removes both prefix and suffix as appropriate */
byte *daword;
MM3S *ms;
{
  char          *s = (char *)daword;
  size_t        n;

 if(ms->suffixproc)
     rmsuffix(&s,(char **)ms->suffix,ms->nsuf,ms->minwordlen,
              ms->defsuffrm, ms->phrasewordproc, ms->textsearchmode);
 if(ms->prefixproc)
   rmprefix(&s,(char **)ms->prefix,ms->npre,ms->minwordlen,
            ms->textsearchmode);
 /* KNG 20080822 Bug 2317 slide word back over deleted prefixes,
  * so that pointer to word can remain the same (for alloc'd pointers):
  */
 n = strlen(s);
 memmove(daword, s, n + 1);
}

/************************************************************************/

void
closesels(ms)                                 /* close all set elements */
MM3S *ms;
{
  int i, elIdx;
  SEL   *sel;
 /* MAW 09-30-96 - check for MAXSELS, there may not be a term if full */
 for(i=0;i<MAXSELS && ms->el[i]!=(SEL *)NULL;i++)/* close the pattern matchers */
    {
     sel = ms->el[i];
     if(sel->ex!=(FFS   *)NULL)  closerex(sel->ex);
     if(sel->ps!=(PPMS  *)NULL)  closeppm(sel->ps);
     if(sel->xs!=(XPMS  *)NULL)  closexpm(sel->xs);
     if(sel->ss!=(SPMS  *)NULL)  closespm(sel->ss);
     if(sel->np!=(NPMS  *)NULL)  closenpm(sel->np);
	 if(sel->mm3s)
	 {
		sel->mm3s->refcount--;
		if(sel->mm3s->refcount == 0)
		{
			free(sel->mm3s);
		}
	 }
     /* KNG 20080822 Bug 2317 SEL.lst elements are alloced now: */
     for (elIdx = 0; elIdx < sel->lstsz; elIdx++)
       if (sel->lst[elIdx] != CHARPN) free(sel->lst[elIdx]);
     free(sel);                                 /* free the element */
    }
}

/************************************************************************/

static int
eq2lst(const byte *s, SEL *ea)
/* Parses equiv list `s' into list `ea->lst'.
 * `ea->lst' elements (but not `ea->lst' itself) will be alloced and folded.
 * Returns number of elements, or -1 on error.
 */
{
 static CONST char fn[]="eq2lst";
 int n;                                              /* number of eqs */
 int i = 0;
 int inprops = 0;
 byte *d = BYTEPN;
 char   *lstItem, *tmp = NULL, tmpBuf[256];
 size_t tmpSz, foldSz;
 int     textsearchmode = ea->mm3s->textsearchmode; /* (in) TXCFF textsearchmode */
 byte **lst = (byte **)ea->lst;
 byte   *sDup = NULL;

 /* KNG 20140724 dup `s' temporarily so we do not stomp it, so callers
  * can still see the full original MM3S.set value:
  */
 sDup = (byte *)strdup((char *)s);
 if (!sDup)
   {
     putmsg(MERR + MAE, fn, CannotAlloc, (unsigned long)strlen((char *)s),
            strerror(errno));
     goto err1;
   }
 s = sDup;

 for(n=0;*s!='\0' && n<MAXEQS;n++)
    {
      for(lst[n]=d=(byte*)s;*s!='\0';s++)  /* record ptr then eat the equiv */
         {
          if(*s==EQESC)                         /* `\' */
              {
               *(d++)= *(++s);
              }
          else
           if(*s==EQDELIM)                      /* `,' */
              {
               *d++ ='\0';
               s++;
               break;
              }
          else
          if(*s==POSDELIM)                      /* `;': secondary pos */
             {
              *d++ ='\0';
              for(s++;*s!=EQDELIM && *s!='\0';s++);     /* skip up to `,' */
              if(*s!='\0')                      /* and skip the `,' too */
                    s++;
              break;
             }
          else
		  if(*s=='{')
			 {
				 MM3S *nmm3s, *mm3s;
				 nmm3s = calloc(1, sizeof(MM3S));
				 if (!nmm3s)
                                   {
                                     putmsg(MERR + MAE, __FUNCTION__,
                                            CannotAlloc,
                                            (unsigned long)sizeof(MM3S),
                                            strerror(errno));
                                     goto err1;
                                   }
				 mm3s = ea->mm3s;
				 *nmm3s = *mm3s;
				 /* KNG should not clone `sdx'/`edx'
				  * (see close3eapi())?
				  */
				 mm3s->refcount --;
				 nmm3s->refcount = 1;
				 ea->mm3s = nmm3s;
				 inprops = 1;
			 }
		  else if(inprops && *s == '}')
		  {
            *d++ ='\0';
			s++;
			inprops=0;
			break;
		  }
		  else
             *(d++)= *s;
         }
         /* Fixes eqlst with a ' ' in e.g. magic: */
         /* KNG 011203 also remove empties: confuses list-length funcs: */
         if((lst[n][0]==' ' && lst[n][1]=='\0') ||      /* JMT 98-06-09 */
            lst[n][0] == '\0')                          /* KNG 011203 */
	 	n--;
		 if(inprops && lst[n][0] == '@')
		 {
                   parsesetting(ea->mm3s, (char *)lst[n]);
			 n--;
		 }
    }
 if (d != BYTEPN) *d='\0';                      /* if !null  KNG 020131 */
 if(n>=MAXEQS)
   {
    n=MAXEQS-1;         /* MAW 05-13-96 - don't walk past end of list */
    putmsg(MWARN,fn,"Truncating \"%s\" list at %d elements",lst[0],n);
   }
 lst[n] = (byte *)"";           /* force last ptr to point to empty str */

 /* KNG 20080822 Bug 2317 fold list elements, so that minwordlen counts
  * are consistent (e.g. for ligatures), and rmsuffix() can drill into them;
  * see also comments in remorph().  Also dup them here:
  */
 textsearchmode = ea->mm3s->textsearchmode;
 tmp = tmpBuf;
 tmpSz = sizeof(tmpBuf);
 for (i = 0; i < n; i++)
   {
     for (;;)                                   /* realloc if needed */
       {
         foldSz = TXunicodeStrFold(tmp, tmpSz, (char *)lst[i], -1,
              /* Suppress expanddiacritics and ignorediacritics for now;
               * see explanation in remorph():
               */
            (textsearchmode &
              ~(TXCFF_EXPDIACRITICS | TXCFF_IGNDIACRITICS | TXCFF_IGNWIDTH)));
         if (foldSz != (size_t)(-1)) break;     /* success */
         if (tmp != tmpBuf) free(tmp);
         tmpSz += (tmpSz >> 2) + 16;            /* arbitrary increase */
         if ((tmp = (char *)malloc(tmpSz)) == CHARPN)
           {
             putmsg(MERR + MAE, fn, CannotAlloc,
                    (unsigned long)tmpSz, strerror(errno));
             goto err1;
           }
       }
     if ((lstItem = (char *)malloc(foldSz + 1)) == CHARPN)
       {
         putmsg(MERR + MAE, fn, CannotAlloc,
                (unsigned long)foldSz + 1LU, strerror(errno));
         goto err1;
       }
     memcpy(lstItem, tmp, foldSz);
     lstItem[foldSz] = '\0';
     lst[i] = (byte *)lstItem;
   }
  goto finally;

err1:
  for (i--; i >= 0; i--)
    {
      free(lst[i]);
      lst[i] = BYTEPN;
    }
  n = -1;
finally:
  if (tmp && tmp != tmpBuf)
    {
      free(tmp);
      tmp = NULL;
    }
  if (sDup)
    {
      free(sDup);
      sDup = NULL;
    }
 return(n);
}

/************************************************************************/

int
xpmsetup(s,ea,ms)                      /* sets up an xpm for processing */
byte *s ;
SEL  *ea;
MM3S *ms;
{
 static CONST char fn[]="xpmsetup";
 int percentage=0;

 (void)ms;
 if(isdigit(*s) && isdigit(*(s+1)))
    percentage=(10*ctoi(*s))+ctoi(*(s+1));
 else
    {
     putmsg(MERR,fn,"Invalid percentage in %c%s",XPMSYM,s);
     return(0);
    }
 s+=2;
 /* KNG 20080822 Bug 2317 SEL.lst elements are alloced now: */
 if ((ea->lst[0]=strdup((char *)s)) == CHARPN)
   {
     putmsg(MERR + MAE, fn, CannotAlloc,
            (unsigned long)strlen((char *)s) + 1, strerror(errno));
     return(0);
   }
 ea->lstsz=1;
 ea->pmtype=PMISXPM;
 if((ea->xs=openxpm((char *)s,percentage))==(XPMS *)NULL)
    return(0);
 return(1);

}

/************************************************************************/
#if PM_NEWLANG
static byte *langc, *wordc;
#define PM_ISLANGC(a) (langc[(a)])
#define PM_ISWORDC(a) (wordc[(a)])
#endif                                                  /* PM_NEWLANG */
/************************************************************************/
static int couldbeaword ARGS((byte *s));

                                       /* PBR 08-23-91 added this func. */
static int                          /* sees if a string could be a word */
couldbeaword(s)
byte *s;
{
 for(;*s;s++)
   {
#if PM_NEWLANG
    if(!PM_ISLANGC(*s)) return(0);
#else
    switch(*s)
         {
          case ' '  :
          case '-'  :
          case '\'' :  break;
          default   :  if(!isalpha(*s)) return(0);
         }
#endif
   }
 return(1);
}

/************************************************************************/

int
lstsetup(s,ea)              /* sets up a list of eq's for processing */
byte *s ;
SEL  *ea;
{
 int i,n;
 MM3S *ms = ea->mm3s;
 if(!(n=eq2lst(s,ea)))/*change the equiv line to a list */
    return(0);
 ea->lstsz=n;                                /* assign elements to list */
#if PM_NEWLANG                                        /* MAW 03-05-97 */
 ea->lang=1;
 ms = ea->mm3s; /* eq2lst may have changed it */
 if(ms->suffixproc || ms->prefixproc)
    for(i=0;i<n;i++)
       if(couldbeaword((byte *)ea->lst[i]))
          rmpresuf((byte *)ea->lst[i],ms);
#else
 ea->lang=1;              /* process this as language not an expression */

 for(i=0;i<n && ea->lang;i++)            /* check list out PBR 08-23-91 */
      if(!couldbeaword((byte *)ea->lst[i])) ea->lang=0;

 if(ea->lang && (ms->suffixproc || ms->prefixproc))
     for(i=0;i<n;i++)
         rmpresuf((byte *)ea->lst[i],ms);
#endif                                                  /* PM_NEWLANG */


 if(n==1)                                /* if only one element use spm */
   {
    ea->pmtype=PMISSPM;
    ea->ss=openspmmm3s(ea->lst[0], ms);
    if(ea->ss==(SPMS *)NULL)
       return(0);
   }
 else
   {
    ea->pmtype=PMISPPM;
    ea->ps=openppm((byte **)ea->lst);
    if(ea->ps==(PPMS *)NULL)
         return(0);
   }
 return(1);
}

/************************************************************************/

int
opensels(MM3S **pms)                           /* open the set elements */
{
 static CONST char Fn[]="opensels";
 char   *dupSrc = CHARPN;
 SEL *ea;                                              /* element alias */
 int i;
 MM3S *ms = *pms, *nms;
#if PM_NEWLANG                                        /* MAW 03-04-97 */
 langc=pm_getlangc();
 wordc=pm_getwordc();
#endif
 for(i=0;i<MAXSELS && *ms->set[i]!='\0';i++,ms->nels++)
    {
     byte *sa=ms->set[i];                                  /* set alias */

     if((ms->el[i]=(SEL *)calloc(1,sizeof(SEL)))==(SEL *)NULL)
         {
          maerr();
          return(0);
         }
     ea=ms->el[i];                                      /* assign alias */
     ea->lang=0;                                      /* MAW 03-05-97 */
     ea->orpos = i;          /* JMT 6/10/94 Store the original position */
     switch(*sa)                 /* check for valid logic and assign it */
        {
         case ANDSYM: ea->logic=LOGIAND;++ms->nands;break;
         case NOTSYM: ea->logic=LOGINOT;++ms->nnots;break;
         case SETSYM: ea->logic=LOGISET;++ms->nsets;break;
		 case '@':
			ea->logic=LOGIPROP;
			ea->pmtype=PMISNOP;
			if(ms->refcount > 0)
			{
				int j;

				nms = (MM3S *)calloc(1, sizeof(MM3S));
				*nms = *ms;
				*pms = nms;
				/* WTF would like to still have access
				 * to these, to avoid TXfindselWithSels():
				 */
				for(j=0; j < MAXSELS; j++)
					ms->el[j] = NULL;
				ms=nms;
				ms->refcount = 0;
			}
			parsesetting(ms, (char *)sa);
			continue;
         default : putmsg(MERR,Fn,"invalid set logic"); return(0);
        }
	 ea->mm3s = ms;
	 ms->refcount ++;
     switch(*(sa+1)) /*second char will tell what kinda pattern matcher */
        {
        case REXSYM:                            /* `/' */
              if ((ea->lst[0] = strdup(dupSrc = (char *)sa + 2)) == CHARPN)
                goto dupErr;
              ea->pmtype=PMISREX;
              ea->lstsz=1;
              if ((ea->ex = openrex(sa + 2, TXrexSyntax_Rex)) ==
                  (FFS *)NULL)
                goto OPENERR;
              break;
         case XPMSYM:                           /* `%' */
              if(!xpmsetup(sa+2,ea,ms))
                   goto OPENERR;
              break;
         case NPMSYM:                           /* `#' */
              ea->lstsz=1;
              ea->pmtype=PMISNPM;
              if((ea->np=opennpm((char *)sa+2))==(NPMS *)NULL) goto OPENERR;
              if ((ea->lst[0] = strdup(dupSrc = ea->np->ss)) == CHARPN)
                {
                dupErr:
                  putmsg(MERR + MAE, Fn, CannotAlloc,
                         (unsigned long)strlen(dupSrc) + 1, strerror(errno));
                  goto OPENERR;
                }
              break;
         default     :              /* default is ppm or spm if no list */
              if(!lstsetup(sa+1,ea))
                   goto OPENERR;
        }
    }
 return(1);
 OPENERR:
    {
     char *pm;
     switch(ea->pmtype)
        {
         case PMISREX : pm="Rex";break;
         case PMISPPM : pm="Parallel Pattern Matcher";break;
         case PMISSPM : pm="Single Pattern Matcher";break;
         case PMISXPM : pm="Approximate Pattern Matcher";break;
         case PMISNPM : pm="Numeric Pattern Matcher";break;
         default      : pm="?";break;                   /* KNG 020131 */
        }

     ms->refcount++; // fix double free when searching for "wtf %" - empty xpm --ajf 2025-12-07

     putmsg(MERR,Fn,"%s open error for : %s",
            pm, (ea->lst[0] ? ea->lst[0] : dupSrc));
    }
 return(0);
}

/************************************************************************/

#ifndef FORMMAPI
MM3S *
closemm(ms)
MM3S *ms;
{
 if(ms!=(MM3S *)NULL)
    {
     if(ms->cps!=(CP *)NULL)                      /* close the control file */
         closecp(ms->cps);
     closesels(ms);                           /* close the set elements */
     if(ms->mindex!=(MDXRS *)NULL) closermdx(ms->mindex);
     if(ms->sdx!=(FFS *)NULL) closerex(ms->sdx);
     if(ms->edx!=(FFS *)NULL) closerex(ms->edx);
     free((char *)ms);
    }
 return((MM3S *)NULL);
}
#endif                                                    /* FORMMAPI */

/************************************************************************/

int
CDECL
selcmp(aa,bb)                            /* sort by logic then by lstsz */
CONST void *aa,*bb;
{
SEL **a=(SEL **)aa,**b=(SEL **)bb;
 int aval = 0,bval=0;
 /* First, sort by logic; findintrsect() assumes AND < SET < NOT order: */
 switch((*a)->logic)
    {
     case LOGIAND: aval=1;break;
     case LOGISET: aval=2;break;
     case LOGINOT: aval=3;break;
	 case LOGIPROP: aval=4;break;
    }
 switch((*b)->logic)
    {
     case LOGIAND: bval=1;break;
     case LOGISET: bval=2;break;
     case LOGINOT: bval=3;break;
	 case LOGIPROP: bval=4;break;
    }
 aval-=bval;
 /* Second, sort by pattern matcher type: */
 if(aval==0)                                              /* same logic */
    {                                                /* sort by pm type */
     if((*a)->ss)      aval=1;  /* spm */
     else if((*a)->ex) aval=2;  /* rex */
     else if((*a)->ps) aval=3;  /* ppm */
     else if((*a)->np) aval=4;  /* npm */
     else if((*a)->xs) aval=5;  /* xpm */
     else              aval=6;

     if((*b)->ss)      bval=1;  /* spm */
     else if((*b)->ex) bval=2;  /* rex */
     else if((*b)->ps) bval=3;  /* ppm */
     else if((*b)->np) bval=4;  /* npm */
     else if((*b)->xs) bval=5;  /* xpm */
     else              aval=6;

     aval-=bval;
     /* Third, sort by pattern size: */
     if(aval==0)                                /* same pattern matcher */
         {
          if((*a)->ex)              /* are they rex's ?? ret big on top */
               aval = ((*b)->ex->patsize-(*a)->ex->patsize);
          else
          if((*a)->ss)                  /* is it spm ?? ret biggest one */
               aval = ((*b)->ss->patlen-(*a)->ss->patlen);/* PBR 10-04-91 patlen instead of patsize */
          else
            aval = ((*a)->lstsz-(*b)->lstsz);              /* smaller list */
         }
    }
 /* KNG 20060804 avoid platform differences in qsort() of same-value elements;
  * always compare elements as different so all platforms sort the same:
  */
 if (aval == 0) aval = ((*a)->orpos - (*b)->orpos);
 return(aval);
}

/************************************************************************/


#ifndef FORMMAPI
MM3S *
openmm(cfn)
char *cfn;                                         /* control file name */
{
  static const char     fn[] = "openmm";
 extern REQUESTPARM rqp[];
 extern MM3S _mcf;
 MM3S *ms=(MM3S *)calloc(1,sizeof(MM3S));
 CP   *mcp;
 char *mcferr;

 _mcf.cmd=BPNULL; /* null out the command field for later testing */

 if(ms==(MM3S *)NULL)
    {
     putmsg(MERR + MAE, fn, Out_of_mem);
     return(ms);
    }

 if((mcp=opencp(cfn,"r"))==(CP *)NULL)                  /* open control */
    {
     putmsg(MERR+FOE,fn,"Cannot open control file");
     return(closemm(ms));
    }
 ms->cps=mcp;                                            /* PBR 2-13-90 */
 if((mcferr=readcp(mcp,rqp))!=(char *)NULL)             /* read control */
    {
     putmsg(MERR+FRE,fn,mcferr);
     return(closemm(ms));
    }

 *ms=_mcf;              /* copy the static control over to the live one */
 ms->cps=mcp;                                            /* PBR 2-13-90 */

 putmsg(MQUERY,CPNULL,"%s",ms->query); /* say what were searching for */
 ms->npre=initprefix((char **)ms->prefix,ms->textsearchmode);/* init the prefix and suffix lists */
 ms->nsuf=initsuffix((char **)ms->suffix,ms->textsearchmode);

 if(!opensels(&ms))
     return(closemm(ms));
 if ((ms->sdx = openrex(ms->sdexp, TXrexSyntax_Rex)) == (FFS *)NULL)
     return(closemm(ms));
 if ((ms->edx = openrex(ms->edexp, TXrexSyntax_Rex)) == (FFS *)NULL)
     return(closemm(ms));

 qsort((char *)ms->el,(size_t)ms->nels,sizeof(SEL *),selcmp);/*order optimize the sels */

 mmreport(ms);
#ifndef ALLOWALLNOT
 if(ms->el[0]->logic==LOGINOT)
    {
     putmsg(MERR,fn,"Cannot allow an all NOT logic search");
     return(closemm(ms));
    }
#endif

 return(ms);
}
#endif                                                    /* FORMMAPI */

/************************************************************************/

int
inset(ms,n)               /* says whether or not an element is in a set */
MM3S *ms;
int n;
/* Returns 1 if set `n' is within/overlaps another set that is already
 * a member of the final result hit, 0 if not.
 */
{
  static const char     fn[] = "inset";
 int i;
 if(ms->el[n]->logic==LOGINOT)     return(0);

 for(i=0;i<ms->nels;i++)
    {
     if(i==n ||
        !ms->el[i]->member ||
        ms->el[i]->hit==BPNULL ||
        ms->el[i]->logic==LOGINOT
       ) continue;           /* PBR 01-31-91 */
     else
     if(ms->el[i]->hit==ms->el[n]->hit)
       {
         if (TXtraceMetamorph & TX_TMF_Inset)
           {
             char       reasonBuf[128 + EPI_OS_INT_BITS/2];

             sprintf(reasonBuf, "overlaps SEL #%d", (int)i);
             TRACE_MSG(fn, ms, n, "rejected", reasonBuf);
           }
         return(1);
       }
    }
 return(0);
}

/************************************************************************/

static int *cmptab=NULL;

/* if this englcmp not static then need to check for locale chg on entry */
/* otherwise, we know that cmptab is reset on every new search */
static int
englcmp(text, key, ms, sel)
byte *text;             /* (in) potential match in text */
byte *key;              /* (in) search key, with wildcards (if any) */
MM3S    *ms;            /* object for settings */
SEL     *sel;           /* set element this is for */
{
  TXUPM *txupm;
  int   isMatch;
  byte  *keyEnd, *k, *textEnd;
  size_t        keyLen;
  TXCFF flags;

 if(cmptab==NULL)
    cmptab=pm_getct();

 /* Should only need to compare leading-prefix (before wildcard, if
  * any) and trailing-suffix (after wildcard) of `key', as internal
  * strings (e.g. `b' in `a*b*c') are bounded by wildcards (no
  * TXwildsufmatch anchor) or would have been TXwildsufmatch-anchored
  * in verifyphrase()/findspm()?  KNG 20080416
  */
 keyEnd = key + strlen((char *)key);

 /* KNG 20080409 use Unicode compare if the pattern matcher did: */
 switch (sel->pmtype)
    {
    case  PMISSPM: txupm = sel->ss->upm; break;
    case  PMISREX:
    case  PMISXPM:
    case  PMISPPM:
    case  PMISNPM:
    default:       txupm = TXUPMPN;      break;
    }

 /* Check prefix: */
 if (*key != '*')                               /* e.g. `ab*...' or `abc' */
   {
     /* KNG 20080416 Bug 2136: stop at first wildcard in `key'; do not
      * try to match it in `text' in case `*' is in langc/wordc or the
      * search text (otherwise key `al*h' fails to match text `al*xh'):
      */
     for (k = key; k < keyEnd && *k != '*'; k++);
     if (txupm != TXUPMPN)                      /* Unicode search */
       {
         flags = ms->textsearchmode;
         keyLen = -1;
         if (*k == '*')                         /* e.g. `ab*...' */
           {
             flags |= TXCFF_PREFIX;             /* only need to prefix-match*/
             keyLen = k - key;                  /* do not match `*' in key */
           }
         switch (TXunicodeStrFoldCmp((CONST char **)&key, keyLen,
                                     (CONST char **)&text, -1, flags))
           {
           case 0:                              /* `key' matches `text' */
           case TX_STRFOLDCMP_ISPREFIX:         /* `key' matches a prefix */
             isMatch = 1;
             break;
           default:
             isMatch = 0;
             break;
           }
       }
     else                                       /* SPM ok */
       {
         for (;
              *text && key < k && cmptab[*text] == cmptab[*key];
              text++, key++)
           ;
         isMatch = (*text == '\0' && *key == '\0');
       }
     if (!isMatch)
#ifndef NO_ROOT_WILD    /* JMT 98-05-26 Make default, wildcard rooted */
       if(*key != '*')
#endif
         return(0);
   }

 /* Check suffix (Bug 2126): */
 if (*key != '\0' &&                            /* key has wildcard */
     TXwildsufmatch)                            /* suffix must match too */
   {                                            /* get key wildcard suffix: */
     for (k = keyEnd; k > key && k[-1] != '*'; k--);
     if (*k)                                    /* non-empty wild suffix */
       {
         textEnd = text + strlen((char *)text);
         if (txupm != TXUPMPN)                  /* Unicode search */
           isMatch = (TXunicodeStrFoldIsEqualBackwards((CONST char**)&keyEnd,
                           keyEnd - k, (CONST char **)&textEnd, textEnd - text,
                           (ms->textsearchmode | TXCFF_PREFIX)) != 0);
         else
           {
             for (textEnd--, keyEnd--;
                  textEnd >= text && keyEnd >= k &&
                    cmptab[*textEnd] == cmptab[*keyEnd];
                  textEnd--, keyEnd--);
             isMatch = (keyEnd < k);
           }
         if (!isMatch) return(0);
       }
   }

 return(1);                                     /* successful match */
}

/************************************************************************/
#if !PM_NEWLANG
#ifdef NEVER	              /* 95-06-30 JMT - Remove '-' from iswordc */
#define PM_ISWORDC(x) (isalpha((x)) || (x)=='\'' || (x)=='-')
#else
#define PM_ISWORDC(x) (isalpha((x)) || (x)=='\'')
#endif
#endif                                                 /* !PM_NEWLANG */

int
remorph(MM3S *ims, int n)
{
  static CONST char     fn[] = "remorph";
 SEL *el=ims->el[n];
 MM3S *ms = el->mm3s;
 byte cbuf[REMORPHBUFSZ];
 char   foldBuf[REMORPHBUFSZ];
 byte *cbp;                                      /* copy buffer pointer */
#ifdef _WIN32     /* MAW 02-08-93 - windows far ptrs roll over on dec */
 byte _huge *scp, _huge *ecp;
#else
 byte *scp,*ecp;                     /* start and end of copy pointers  */
#endif
 byte *sob,*eob;                       /* start of buffer end of buffer */

 if(!ms->rebuild)                                /* dont do the remorph */
   {
     if (TXtraceMetamorph & TX_TMF_Remorph)
       TRACE_MSG(fn, ims, n, "ok", "rebuild off");
     return(1);
   }

 scp=el->hit;                                       /* delimit the word */
 ecp=scp+el->hitsz;

 sob=ims->start;                                 /* delimiters of buffer */
 eob=ims->end;
 /* KNG 20070226 sanity check: linear-dictionary search did not init this: */
 if (sob == BPNULL || eob == BPNULL)
   {
     putmsg(MERR + UGE, fn, "Internal error: MM3S start not initialized");
     return(0);
   }

 /* Expand `scp'/`ecp' to entire word, according to wordc: */
 /* Bug 5848: was *shrinking* start of hit by one char if not wordc: */
 for ( ; scp > sob && PM_ISWORDC(scp[-1]); scp--);  /* find beg of word */
 for(;ecp<eob && PM_ISWORDC(*ecp);ecp++);           /* find end of word */

 el->hit=scp;                               /* redo the size of the hit */
 el->hitsz=(int)(ecp-scp);                                   /* dos ism */

#ifdef NEVER             /* MAW 04-26-93 - this would find substrings */
/* MAW 04-26-93 - change && to || below. turning off prefix or suffix was turning off rebuild */
 if(!(ms->prefixproc || ms->suffixproc)) /* we dont need to do anything */
    return(1);                                  /* let's go home Irving */
#endif                                                       /* NEVER */

 if(el->hitsz>=(REMORPHBUFSZ-1))              /* is it too big to copy? */
   {
     if (TXtraceMetamorph & TX_TMF_Remorph)
       TRACE_MSG(fn, ims, n, "rejected", "hit too large");
    return(0);                                    /* yep, let's go home */
   }

 for(cbp=cbuf;scp<ecp;cbp++,scp++)     /* copy the word into the buffer */
    *cbp= *scp;

 *cbp='\0';                               /* TERMINATE HIM ANDROID!!!!! */

 /* KNG 20080822 fold the potential match first, before prefix/suffix
  * removal, so that ligatures are expanded and thus minwordlen counts
  * their individual characters (and thus count is consistent with query),
  * and rmsuffix() can see and partially remove them (e.g. minwordlen 3
  * can thus strip `o f-f-i-ligature c e' down to `o f').  But do not fold
  * completely: leave expanddiacritics till later (actual strfoldcmp),
  * otherwise `f u-umlaut r' would not match `f u r', because knowledge
  * of the optional-`e' would be lost here; optional-`e' would also
  * unnecessarily increase character count for minwordlen.  Also must
  * suppress ignorediacritics here, so that diacritic is not lost that
  * way either.  Also must suppress ignorewidth because ignorediacritics
  * should be applied before ignorewidth (see unicode.h).  See also eq2lst():
  */
 if (TXunicodeStrFold(foldBuf, sizeof(foldBuf), (CONST char *)cbuf,
                      cbp - cbuf, (ms->textsearchmode &
          ~(TXCFF_EXPDIACRITICS | TXCFF_IGNDIACRITICS | TXCFF_IGNWIDTH)))
     == (size_t)(-1))
   {
     if (TXtraceMetamorph & TX_TMF_Remorph)
       TRACE_MSG(fn, ims, n, "rejected", "fold buffer too small");
     return(0);                                 /* buffer too small */
   }
 cbp = (byte *)foldBuf;

 if(el->lang != LANG_WILD)/* JMT 2000-04-13 */
    rmpresuf(cbp,ms);         /* will check for prefix/suffixproc flags */
#if PM_FLEXPHRASE                                     /* MAW 01-20-97 */
 if(el->srchp!=PMPHRPN)
    {
     if(samephrase(el->srchp,cbp))
       {
         if (TXtraceMetamorph & TX_TMF_Remorph)
           TRACE_MSG(fn, ims, n, "ok", "same phrase");
         return(1);
       }
     if (TXtraceMetamorph & TX_TMF_Remorph)
       TRACE_MSG(fn, ims, n, "rejected", "not same phrase");
     return(0);
    }
 else
#endif
   {
   if(englcmp(cbp,el->srchs,ms,el))
     {
       if (TXtraceMetamorph & TX_TMF_Remorph)
         TRACE_MSG(fn, ims, n, "ok", "englcmp() ok");
       return(1);
     }
   if (TXtraceMetamorph & TX_TMF_Remorph)
     TRACE_MSG(fn, ims, n, "rejected", "englcmp() failed");
   return(0);
   }
}

/************************************************************************/

byte *
selsrch(el,start,end,op)              /* locate a potential set element */
SEL *el;
byte *start,*end;
int op;                   /* operation = SEARCHNEWBUF or CONTINUESEARCH */
{
  static const char     fn[] = "selsrch";
 byte *loc=BYTEPN;

 switch(el->pmtype)
    {
     case  PMISSPM: loc=getspm(el->ss,start,end,op); break;
     case  PMISREX: loc=getrex(el->ex,start,end,op); break;
     case  PMISXPM: loc=getxpm(el->xs,start,end,op); break;
     case  PMISPPM: loc=getppm(el->ps,start,end,op); break;
     case  PMISNPM: loc=getnpm(el->np,start,end,op); break;
     case  PMISNOP: loc = NULL;                      break;
     default:
       putmsg(MERR + UGE, fn, "Unknown pattern matcher type #%d",
              (int)el->pmtype);
       loc = NULL;
       break;
    }

 if(loc!=BPNULL)
    {
     el->hit=loc;
     switch(el->pmtype)       /* find out what was located and its size */
         {
          case  PMISSPM: el->hitsz=el->ss->patsize;
                         el->srchs=(byte *)el->lst[0];
#if PM_FLEXPHRASE
                         el->srchp=spmsrchp(el->ss);
#endif
#if PM_NEWLANG
                         el->lang=spmlang(el->ss);
#endif
                         break;
          case  PMISREX: el->hitsz=rexsize(el->ex);
                         el->srchs=(byte *)el->lst[0];
                         break;
          case  PMISXPM: el->hitsz=el->xs->patsize;
                         el->srchs=(byte *)el->lst[0];
                         break;
          case  PMISPPM:
#ifdef NEVER                          /* MAW 01-09-97 - use functions */
                         el->hitsz=strlen((char *)el->ps->slist[el->ps->sn]);
                         el->srchs=el->ps->slist[el->ps->sn];
#else
                         el->hitsz=ppmhitsz(el->ps);
                         el->srchs=ppmsrchs(el->ps);
#endif
#if PM_FLEXPHRASE
                         el->srchp=ppmsrchp(el->ps);
#endif
#if PM_NEWLANG
                         el->lang=ppmlang(el->ps);
#endif
                         break;
          case  PMISNPM: el->hitsz=el->np->hitsz;
                         el->srchs=(byte *)el->lst[0];
                         break;
          default:
            break;
         }
    }
 else
    {
     el->hit=BPNULL;
     el->hitsz=0;
    }
 return(loc);
}

byte *
TXfindselWithSels(MM3S *ms, SEL **sels, int numSels, int selIdx, byte *start,
                  byte *end, TXPMOP op)
/* Wrapper for findsel() when using a SEL's MM3S instead of the "main"
 * MM3S: former may have NULLs in MM3S.el[] array, if there were
 * inline settings in the query (due to clearing in opensels()?).
 * Bug 4474.  See TXfindselWithSels() comment in opensels().
 */
{
  byte  *ret;
  int   msNels, saveNels;
  SEL   *saveSels[MAXSELS];

  /* Avoid array save/restore if not needed: */
  if (!sels ||
      numSels <= 0 ||
      /* Bug 6699: check actual `selIdx' SEL: sometimes `ms->el[0]' is
       * set but `ms->nels' is 0 or `ms->el[selIdx]' is not set:
       */
      (selIdx >= 0 && selIdx < ms->nels && ms->el[selIdx]))
    return(findsel(ms, selIdx, start, end, op));

  /* Sanity-check `ms->nels', `numSels': */
  msNels = ms->nels;
  if (msNels < 0) msNels = 0;
  else if (msNels > MAXSELS) msNels = MAXSELS;
  if (numSels < 0) numSels = 0;
  else if (numSels > MAXSELS) numSels = MAXSELS;

  /* Temporarily put `sels' into `ms->el'.  Bug 6699: added and use
   * `numSels' too, as `ms->nels' may be wrong:
   */
  memcpy(saveSels, ms->el, msNels*sizeof(saveSels[0]));         /* save */
  saveNels = ms->nels;                                          /* "" */
  memcpy(ms->el, sels, numSels*sizeof(saveSels[0]));            /* set */
  ms->nels = numSels;                                           /* "" */

  /* Do the search: */
  ret = findsel(ms, selIdx, start, end, op);

  /* Restore `ms->el', `ms->nels': */
  memcpy(ms->el, saveSels, msNels*sizeof(saveSels[0]));
  ms->nels = saveNels;

  return(ret);
}

/************************************************************************/

byte *
findsel(ms,n,start,end,op)                        /* find a set element */
MM3S *ms;                                                 /* MM3 struct */
int n;                                                /* element number */
byte *start,*end;
int op;                   /* operation = SEARCHNEWBUF or CONTINUESEARCH */
{
  static const char      fn[] = "findsel";
  byte *loc, *prevHit, *prevLoc = NULL;
  TXPMOP        orgOp = op;
  size_t        numHitsSameLoc = 0, numLoopDetected = 0;
  SEL           *el = NULL;

  if (n < 0 || n >= ms->nels)                   /* Bug 6699 */
    {
      putmsg(MERR, __FUNCTION__,
   "Internal error: Attempt to search for set %d in MM3S object with %d sets",
             (int)n, (int)ms->nels);
      loc = NULL;
      goto finally;
    }
  el = ms->el[n];

again:
 /* Prep for Bug 5420 infinite-loop detection: */
 if (op == CONTINUESEARCH)
   prevHit = el->hit;
 else                                           /* SEARCHNEWBUF */
   {
     prevHit = NULL;
     el->numHitsSameLoc = 0;
   }

 for(loc=selsrch(el,start,end,op);
     loc!=BPNULL;
     prevLoc = loc, loc = selsrch(el, start, end, op = CONTINUESEARCH)
    )
    {
      /* Bug 5420: check for potential infinite loop.
       * Check here in addition to below, in case loop happens before
       * remorph() is successful, e.g. pre-Bug 5420 fix with minwordlen 5
       * search `(no-toasting,untoasting,untoasted)' against
       * `... toast ...' where `untoast' items would match erroneously
       * (and ad infinitum) in PPM, but fail to remorph() here:
       */
      if (loc == prevLoc)
        {
          if (TXfindselLoopCheck && op == CONTINUESEARCH &&
              /* Could legitimately get same-hit same-size from PPM, if
               * multiple terms have same root word, e.g.  `toasting',
               * `toasted' with minwordlen 5, so we need to let those
               * through in case later ones remorph() ok when earlier ones
               * do not.  So only yap when we exceed set equiv size,
               * i.e. all terms have had a chance to legitimately match:
               */
              numHitsSameLoc++ > (size_t)el->lstsz)
            goto loopDetected;
        }
      else
        numHitsSameLoc = 0;                     /* Bug 5543: reset count */

     if(el->lang)                 /* if it is a language or wildcard term */
        {
         if(remorph(ms,n))                             /* english is ok */
              if(!inset(ms,n))           /* not a member of another set */
                   break;                                    /* go home */
        }
     else if(!inset(ms,n))
         break;
    }

 /* Bug 5420: check for potential infinite loop: */
 if (el->hit == prevHit)
   {
     if (TXfindselLoopCheck && op == CONTINUESEARCH &&
         el->numHitsSameLoc++ > (size_t)el->lstsz)
       {
       loopDetected:
         /* Bug 5420 comment 4: do not bail, just re-start with new
          * search, so we do not miss later hits.  Might still miss
          * hit at/near current location though; unavoidable:
          */
         if (++numLoopDetected < 3 || (TXtraceMetamorph & TX_TMF_Findsel))
           {
             char       traceContextBuf[256];
             size_t     selHitOffset = el->hit - ms->start;
             size_t     selHitLen = el->hitsz;

             TXmmShowHitContext(traceContextBuf, sizeof(traceContextBuf),
                                -1, 0, &selHitOffset, &selHitLen, 1,
                                (char *)ms->start, ms->end - ms->start);
             putmsg(MWARN, fn,
                    "Internal error: set `%s' returned same hit multiple times (context: `%s'); restarting search at hit + 1",
                    ms->set[el->orpos], traceContextBuf);
           }
         start = el->hit + 1;
         op = SEARCHNEWBUF;
         if (start > end)                       /* end of buf */
           {
             loc = el->hit = NULL;
             el->hitsz = 0;
             goto finally;
           }
         goto again;
       }
   }
 else
   el->numHitsSameLoc = 0;                      /* Bug 5543: reset count */

               /* invert the sense for the not operation */
 if (el->logic == LOGINOT && !ms->noNotInversion)
    {
     el->hitsz=0;                                     /* it has no size */
     if(loc==BPNULL)
         {
          loc=el->hit=start;   /* if its not there: hit is start */
         }
     else
         {
          loc=el->hit=BPNULL;           /* it is there, so it is not !? */
         }
    }
finally:
 if (el && (TXtraceMetamorph & TX_TMF_Findsel))
   {
     char       contextBuf[256];
     size_t     selHitOffset = (el->hit ? el->hit - ms->start : -1);
     size_t     selHitLen = el->hitsz;

     TXmmShowHitContext(contextBuf, sizeof(contextBuf),
                        -1, 0, &selHitOffset, &selHitLen, 1,
                        (char *)ms->start, ms->end - ms->start);
     if (el->hit)
       putmsg(MINFO, CHARPN,
              "findsel of SEL #%d `%s': hit at %+wd length %d: `%s'",
              (int)n, el->lst[0], (EPI_HUGEINT)(el->hit - ms->start),
              (int)el->hitsz, contextBuf);
     else
       putmsg(MINFO, CHARPN,
              "findsel of SEL #%d `%s': no%s hits in `%s'",
              (int)n, el->lst[0], (orgOp == CONTINUESEARCH ? " more" : ""),
              contextBuf);
   }
 return(loc);
}

/* ------------------------------------------------------------------------ */

static byte *TXmmFindNWordsLeft ARGS((MM3S *ms, byte *loc, int flags,
                                      size_t *numToFind));
static byte *
TXmmFindNWordsLeft(ms, loc, flags, numToFind)
MM3S    *ms;            /* (in) MM3S object */
byte    *loc;           /* (in) location to search left of */
int     flags;          /* (in) flags */
size_t  *numToFind;     /* (in/out) number of words to find/found */
/* Returns left edge of `n'th word left of `loc', or `ms->start' if
 * reached before that.  Sets `*numToFind' to number of words actually
 * found.  `flags' are bit flags:
 *   0x1: word at `loc-1' should be included in the count
 *   0x2: after words found, continue scan to right edge of next word
 */
{
  byte          *sdloc, *nloc;
  int           inWord;                         /* are we in a word */
  size_t        numFound;                       /* # words found so far */
  byte          *wordc;

  wordc = pm_getwordc();
  numFound = 0;
  inWord = !(flags & 0x1);
  for (sdloc = loc; sdloc > ms->start; sdloc = nloc)
    {                                           /* scan left from hit */
      nloc = sdloc - 1;
      if (wordc[*nloc])                         /* `*nloc' part of a word */
        {
          if (!inWord)
            {
              inWord = 1;
              numFound++;
            }
        }
      else                                      /*`*nloc' not part of a word*/
        {
          if (inWord)
            {
              inWord = 0;
              if (numFound >= *numToFind)
	   	break;
            }
        }
    }
  *numToFind = numFound;                        /* return # actually found */
  if (flags & 0x2)                              /* scan to next word edge */
    {
      for ( ; sdloc > ms->start && !wordc[*(sdloc - 1)]; sdloc--);
    }
  return(sdloc);
}

/* ------------------------------------------------------------------------ */

static byte *TXmmFindNWordsRight ARGS((MM3S *ms, byte *loc, int flags,
                                       size_t *numToFind));
static byte *
TXmmFindNWordsRight(ms, loc, flags, numToFind)
MM3S    *ms;            /* (in) MM3S object */
byte    *loc;           /* (in) location to search right of */
int     flags;          /* (in) flags */
size_t  *numToFind;     /* (in/out) number of words to find/found */
/* Returns right edge (i.e. just past last byte) of `n'th word right
 * of `loc', or `ms->end' if reached before that.  Sets `*numToFind'
 * to number of words actually found.  `flags' are bit flags:
 *   0x1:  word at `loc' is to be included in the count
 *   0x2:  after words found, continue scan to left edge of next word
 */
{
  byte          *edloc;
  int           inWord;                         /* are we in a word */
  size_t        numFound;                       /* # words found so far */
  byte          *wordc;

  wordc = pm_getwordc();
  numFound = 0;
  inWord = !(flags & 0x1);
  for (edloc = loc; edloc < ms->end; edloc++)
    {                                           /* scan right from hit end */
      if (wordc[*edloc])                        /* `*edloc' part of a word */
        {
          if (!inWord)
            {
              inWord = 1;
              numFound++;
            }
        }
      else                                      /*`*edloc' not part of word */
        {
          if (inWord)
            {
              inWord = 0;
              if (numFound >= *numToFind)
	   	break;
            }
        }
    }
  *numToFind = numFound;
  if (flags & 0x2)                              /* continue to left edge */
    {
      for ( ; edloc < ms->end && !wordc[*edloc]; edloc++);
    }
  return(edloc);
}

static int TXmmAdvanceASetForWithinN ARGS((MM3S *ms, int anchorSetIdx,
                 int leftSetIdx, int rightSetIdx, byte *leftEdgePlusN,
                 byte *rightEdgeMinusN));
static int
TXmmAdvanceASetForWithinN(ms, anchorSetIdx, leftSetIdx, rightSetIdx,
                          leftEdgePlusN, rightEdgeMinusN)
MM3S    *ms;                    /* (in/out) MM3S object */
int     anchorSetIdx;           /* (in) set loose delims are anchored to */
int     leftSetIdx;             /* (in) leftmost AND/SET */
int     rightSetIdx;            /* (in) rightmost AND/SET */
byte    *leftEdgePlusN;         /* (in, opt.) `leftSetIdx' set hit + `w/N' */
byte    *rightEdgeMinusN;       /* (in) `rightSetIdx' set hit minus `w/N' */
/* Advance a set to try to make `w/N' match.  Primarily for ...TYPE_RADIUS,
 * but also used for ...TYPE_SPAN, in which case `leftEdgePlusN' is NULL
 * (not relevant).  Used as a fudge by TXmmCheckWithinN().
 * Returns 1 if a set was advanced (try `w/N' logic again), 0 if not.
 * For Bug 2899/3021.  Assumes `set->nib & 0x2' flags cleared by first
 * TXmmCheckWithinN() pass.  Assumes `ms->hit'/`ms->hitsz' are original
 * loose (setuphitWithin{Char|Word}()) delimiters.
 */
{
  SEL   *leftSet, *bestSet, *set;
  int   i, n, bestSetIdx, saveHitSz;
  byte  *loc, *saveHit;

  (void)rightEdgeMinusN;

  leftSet = ms->el[leftSetIdx];

  /*   Bug 2899: We might have failed to find a match in
   * TXmmCheckWithinN(), e.g. for query `+A +(C,D) +E w/6' API3WITHINCHAR
   * ...TYPE_RADIUS and this text:
   *     A x C D x x E
   * we might have just examined hits `A', `C', `E' (in
   * TXmmCheckWithinN()) and thus found no middle anchor, when in fact
   * `D' would work.  But if A or E is the setuphit() anchor, findmm()
   * will get-next-hit on one of those instead of (C,D), and will miss
   * the hit.  So try advancing a set to its next hit *here* -- in the
   * same delims -- and try to match logic again in
   * TXmmCheckWithinN().  (WTF we are only trying sets in the current
   * member logic for now; this will still fail to match if the query
   * is `A C D E @2 w/6' and we need to look for `D' instead of
   * next-`(C,D)').  We try to use the leftmost set, in the hopes of
   * getting a hit in the middle-anchor area (and because next-hit on
   * left sets should move *towards* not away from the overall locus):
   */
  do
    {
      if (leftSetIdx == anchorSetIdx ||         /* do not use `anchorSetIdx'*/
          (leftSet->nib & 0x2))                 /* no more hits in delims */
        {
          /*   We do not use `anchorSetIdx', because the loose
           * (setuphitWithin{Char|Word}()) delimiters are based off
           * its location: changing its location changes the loose
           * delimiters' location assumptions.  findmm() will
           * CONTINUESEARCH on `anchorSetIdx' for us anyway.  (Also,
           * its findsel() buffer bounds are the entire buffer, not
           * the loose delimiters, so CONTINUESEARCH might go too
           * far?  moot because we SEARCHNEWBUF instead here anyway.)
           *   Look for the next leftmost set; wtf use a heap?:
           */
          bestSet = SELPN;                      /* no candidates yet */
          bestSetIdx = -1;
          n = ms->nands + ms->nsets;
          for (i = 0; i < n; i++)               /* all ANDs/SETs */
            {
              if (i == anchorSetIdx) continue;  /* do not use */
              /* Do not bother with `rightSet', since next hit on that
               * would merely widen the overall set, not help us get a
               * middle anchor (is this assumption correct?):
               */
              if (i == rightSetIdx) continue;
              set = ms->el[i];
              if (!set->member) continue;       /* wtf try it? */
              /* Do not use sets that are already past
               * `leftEdgePlusN': since we have to keep `leftSet' in
               * place (since it is `anchorSetIdx'), `leftEdgePlusN'
               * is still a valid limit, and any set that is past it
               * already will certainly have its next hit past it too:
               */
              if (leftEdgePlusN != BYTEPN &&    /* ...TYPE_RADIUS && */
                  set->hit > leftEdgePlusN)     /*   past `leftEdgePlusN' */
                continue;
              /* Do not use sets that have no more hits in loose delims: */
              if (set->nib & 0x2) continue;
              /* Passed checks; see if leftmost: */
              if (bestSet == SELPN ||
                  set->hit < bestSet->hit)      /* new leftmost set */
                {
                  bestSet = set;
                  bestSetIdx = i;
                }
            }
        }
      else
        {
          bestSet = leftSet;
          bestSetIdx = leftSetIdx;
        }
      if (bestSet == SELPN) break;              /* no candidate found */

      /* Get next `bestSet' hit.  We must use SEARCHNEWBUF instead of
       * CONTINUESEARCH, since findintrsect() may not have done a
       * SEARCHNEWBUF within the *current* loose delimiters (because
       * the hit may have been left over from previous setuphit()
       * delimiters and happened to fit within current ones too):
       */
      saveHit = bestSet->hit;                   /* save in case no more hits*/
      saveHitSz = bestSet->hitsz;
      loc = findsel(ms, bestSetIdx, bestSet->hit + bestSet->hitsz,
                    ms->hit + ms->hitsz, SEARCHNEWBUF);
      /* If we got a hit that is still inside (loose) delims,
       * try TXmmCheckWithinN() logic again:
       */
      if (loc != BYTEPN &&
          bestSet->hit + bestSet->hitsz <= ms->hit + ms->hitsz)
          /* WTF maybe further restrict acceptable hits to ones that are
           * plausible middle anchors; e.g. char radius query
           * `+A +(C,D) +E w/6' and this text:
           *   A x C D x x E A
           * and we have hits A C E: we could get-next on `+A' here,
           * but that will not match in caller (TXmmCheckWithinN()),
           * and we will end up back here trying get-next on `+(C,D)';
           * we may eventually get a match but will miss the one with
           * the earliest `A'.  So do not accept a get-next on `A'
           * here since it would not make a valid middle anchor,
           * to try to force get-next on `+(C,D)'.  WTF this would fail
           * in some situations, e.g. Texis test591, so we do not do it
           * (&& bestSet->hit <= leftEdgePlusN); but without it, we miss
           * a hit in Vortex test431.
           */
        return(1);                              /* try TXmmCheck... again */
      /* No more hits in delims; restore previous hit: */
      bestSet->hit = saveHit;                   /* restore previous hit */
      bestSet->hitsz = saveHitSz;
      bestSet->nib |= 0x2;                      /* flag it for later */
    }
  while (bestSet != SELPN);                     /* until no more candidates */

  return(0);                                    /* no set able to advance */
}

static int TXmmCheckWithinN ARGS((MM3S *ms, int anchorSetIdx));
static int
TXmmCheckWithinN(ms, anchorSetIdx)
MM3S    *ms;            /* (in) MM3S object */
int     anchorSetIdx;   /* (in) index of anchor set */
/* Returns 1 if current set hits are truly within-N, 0 if not.  For
 * w/N delimiters only.  For use by findintrsect(), because
 * setuphitWithin{Char|Word} had to set loose delimiter locations,
 * which must be finalized here, as well as re-checking NOTs because
 * the delimiter locations changed.  May do CONTINUESEARCHes on sets
 * other than `anchorSetIdx' (i.e. assumes NOTs were SEARCHNEWBUFed on
 * `ms->hit'/`ms->hitsz').  Assumes selcmp() set ordering (ANDs, SETs
 * then NOTs).
 */
{
  SEL           *set, *leftSet, *rightSet, *leftNot, *rightNot;
  SEL           leftNotCopy;
  byte          *leftEdgePlusN, *rightEdgeMinusN, *setEnd, *loc, *delimsEnd;
  byte          *wordc = BYTEPN, *delimBeginOrg, *delimEndOrg, clrNib = ~0x2;
  int           i, n, leftSetIdx, rightSetIdx;
  size_t        numWords, span;

  delimBeginOrg = ms->hit;
  delimEndOrg = ms->hit + ms->hitsz;

again:
  /* Find leftmost and rightmost non-NOT set.  WTF track these with a
   * heap to avoid this loop every time?:
   */
  leftSet = rightSet = SELPN;
  leftSetIdx = rightSetIdx = 0;
  /* Ignore NOTs: we will check them below.  (And had we not disabled
   * findsel() NOT inversion via ms->noNotInversion = 1, they might
   * appear as "hits" because of that inversion, but in a fake (and
   * potentially too-far-away) position at `ms->hit'.):
   */
  n = ms->nands + ms->nsets;
  for (i = 0; i < n; i++)                       /* all ANDs and SETs */
    {
      set = ms->el[i];
      set->nib &= clrNib;                       /* clear not-in-delims flag */
      if (!set->member) continue;               /* not part of current hit */
      if (leftSet == SELPN || set->hit < leftSet->hit)
        {
          leftSet = set;
          leftSetIdx = i;
        }
      if (rightSet == SELPN ||
          set->hit + set->hitsz > rightSet->hit + rightSet->hitsz)
        {
          rightSet = set;
          rightSetIdx = i;
        }
    }
  clrNib = ~0x0;                                /* only clear once */

  /* Find innermost left and right NOTs, i.e. the NOTs closest to the
   * AND/SET set span, but not overlapping: any NOTs that overlap
   * mean this is not a match.  Note that we assume `ms->noNotInversion'
   * was set non-zero, so that NOT hits are true hits, not inversions.
   * Note also that `leftNot'/`rightNot' might remain NULL, if none found:
   */
  leftNot = rightNot = SELPN;
  delimsEnd = ms->hit + ms->hitsz;
  for (i = ms->nands + ms->nsets; i < ms->nels; i++)
    {                                           /* just NOTs */
      set = ms->el[i];
      if (!set->member) continue;               /* not part of current hit */
      if (set->hit == BYTEPN) continue;         /* not found */
      /* Update `leftNot'/`rightNot': */
    updateLeftRightNot:
      if ((setEnd = set->hit + set->hitsz) <= leftSet->hit)
        {                                       /* is left of AND/SET span */
          if (leftNot == SELPN || setEnd > leftNot->hit + leftNot->hitsz)
            {
              /* Preserve `leftNot->hit'/`hitsz', since we will findsel()
               * on `set' below.  But clear out all other fields to
               * prevent accidental usage of them:
               */
              memset(&leftNotCopy, 0, sizeof(SEL));
              leftNotCopy.hit = set->hit;
              leftNotCopy.hitsz = set->hitsz;
              leftNot = &leftNotCopy;
            }
          /* Bug 2846 comment #2: need to get and check next hit for NOTs
           * that are left of the AND/NOT span:
           */
          if (findsel(ms, i, ms->hit, delimsEnd, CONTINUESEARCH) != BYTEPN)
            goto updateLeftRightNot;
        }
      else if (set->hit >= rightSet->hit + rightSet->hitsz)
        {                                       /* is right of AND/SET span */
          if (rightNot == SELPN || set->hit < rightNot->hit)
            rightNot = set;
        }
      else                                      /* overlaps AND/SET span */
        goto noMatch;
    }

  /* Check the within logic: */
  switch (API3WITHIN_GET_UNIT(ms->withinmode))
    {
    case API3WITHINCHAR:
    default:                                    /* assume CHAR if unknown */
      leftEdgePlusN = leftSet->hit + ms->withincount;
      rightEdgeMinusN = (rightSet->hit + rightSet->hitsz) - ms->withincount;
      n = ms->nands + ms->nsets;
      if (API3WITHIN_GET_TYPE(ms->withinmode) == API3WITHIN_TYPE_SPAN)
        {
          /* No anchor needed, but must check that span <= N: */
          span = (rightSet->hit + rightSet->hitsz) - leftSet->hit;
          if (span <= (size_t)ms->withincount)  /* span is within N */
            {
              /* Set final delimiters to true/exact N span.
               * Try to center them on the AND/SET set span:
               */
              ms->hit = leftSet->hit - ((size_t)ms->withincount - span)/2;
              ms->hitsz = ms->withincount;
              /* Slide/shrink delims if beyond buffer ends: */
              if (ms->hit + ms->hitsz > ms->end)/* too far right */
                {
                  ms->hit = ms->end - ms->hitsz;/* slide delims left */
                  if (ms->hit < ms->start)      /* shrink delims */
                    {
                      ms->hit = ms->start;
                      ms->hitsz = ms->end - ms->hit;
                    }
                }
              else if (ms->hit < ms->start)     /* too far left */
                {
                  ms->hit = ms->start;          /* slide delims right */
                  if (ms->hit + ms->hitsz > ms->end)
                    ms->hitsz = ms->end - ms->hit;      /* shrink delims */
                }
              /* Slide delims left if there is a close NOT on the right: */
              if (rightNot != SELPN &&
                  ms->hit + ms->hitsz > rightNot->hit)
                {
                  ms->hit = rightNot->hit - ms->hitsz;
                  if (ms->hit < ms->start) goto noMatch; /* not enough room */
                  /* Make sure we do not now overlap a left NOT: */
                  if (leftNot != SELPN &&
                      ms->hit < leftNot->hit + leftNot->hitsz)
                    goto noMatch;
                }
              /* Slide delims right if there is a close NOT on the left: */
              else if (leftNot != SELPN &&
                       ms->hit < (setEnd = leftNot->hit + leftNot->hitsz))
                {
                  ms->hit = setEnd;
                  if (ms->hit + ms->hitsz > ms->end) goto noMatch; /*no room*/
                  /* Make sure we do not now overlap a right NOT: */
                  if (rightNot != SELPN &&
                      ms->hit + ms->hitsz > rightNot->hit)
                    goto noMatch;
                }
              goto gotMatch;
            }
          /* No need for Bug 2899 ...TYPE_RADIUS get-next-hit-and-check
           * here, because we do not need a middle anchor.
           * KNG 20100309 Bug 3021: yes we do need get-next-hit-and-check:
           */
          ms->hit = delimBeginOrg;              /* restore for this call */
          ms->hitsz = delimEndOrg - delimBeginOrg;
          if (TXmmAdvanceASetForWithinN(ms, anchorSetIdx, leftSetIdx,
                                 rightSetIdx, leftEdgePlusN, rightEdgeMinusN))
            goto again;
          goto noMatch;
        }                                       /* if ...TYPE_SPAN */
      /* API3WITHIN_TYPE_RADIUS.  See setuphitWithinChar() comments on
       * why w/N TYPE_RADIUS is not commutative, and setuphit() limits
       * are thus looser.  Thus we must validate even for two-set
       * queries (unlike API3WITHINWORD), and we cannot skip the
       * leftmost (i == 0) set.  We also cannot optimize by skipping
       * this loop (and failing the match) if the left and right edges
       * are more than 2N apart, because the middle "anchor" hit we
       * are looking for might be very wide and allow the match; see
       * Bug 2901 comments in setuphitWithinChar():
       */
      for (i = 0; i < n; i++)                   /* all ANDs and SETs */
        {
          set = ms->el[i];
          if (!set->member) continue;           /* not part of current hit */
          /* Note: TXmmAdvanceASetForWithinN() assumes middle anchors
           * must pass this leftEdgePlusN/rightEdgeMinusN test:
           */
          if (set->hit <= leftEdgePlusN &&
              set->hit + set->hitsz >= rightEdgeMinusN)
            {
              /* `set' can be our radius anchor set.  Now try to set
               * our final/true delimiters anchored to it, and see if
               * that excludes all NOTs:
               */
              ms->hit = set->hit - ms->withincount;
              if (ms->hit < ms->start) ms->hit = ms->start;
              loc = (set->hit + set->hitsz + ms->withincount);
              if (loc > ms->end) loc = ms->end;
              ms->hitsz = loc - ms->hit;
              /* Make sure no NOTs within delims: */
              if (leftNot != SELPN &&
                  leftNot->hit + leftNot->hitsz > ms->hit)
                /* Left innermost NOT would be inside delims: no match.
                 * But do not bail yet: might be able to use a later
                 * set as the anchor:
                 */
                continue;
              if (rightNot != SELPN &&
                  rightNot->hit < ms->hit + ms->hitsz)
                continue;                       /* not a match (yet?) */
              goto gotMatch;
            }
        }
      /* Bug 2899: might be able to get a match for ...TYPE_RADIUS
       * by advancing a set:
       */
      ms->hit = delimBeginOrg;                  /* restore for this call */
      ms->hitsz = delimEndOrg - delimBeginOrg;
      if (TXmmAdvanceASetForWithinN(ms, anchorSetIdx, leftSetIdx,
                                 rightSetIdx, leftEdgePlusN, rightEdgeMinusN))
        goto again;
      break;
    case API3WITHINWORD:
      wordc = pm_getwordc();
      if (API3WITHIN_GET_TYPE(ms->withinmode) == API3WITHIN_TYPE_SPAN)
        {
          /* No anchor needed, but must check that span <= N.  Note
           * that unlike ...TYPE_RADIUS, all sets are counted by `w/N'
           * (...TYPE_RADIUS does not count the anchor itself):
           */
          numWords = ms->withincount;
          leftEdgePlusN = TXmmFindNWordsRight(ms, leftSet->hit, 1, &numWords);
          /* delay computing `rightEdgeMinusN' until we need it, below */
          if (rightSet->hit + rightSet->hitsz <= leftEdgePlusN)
            {                                   /* edges span N words */
              /* Set final delimiters to true/exact N span.
               * wtf would like to center them on the AND/SET set span
               * ala API3WITHINCHAR, but computing WORD delimiters is
               * expensive and more dynamic, so use just-computed
               * `leftEdgePlusN':
               */
              ms->hit = leftSet->hit;
              ms->hitsz = leftEdgePlusN - leftSet->hit;
              /* Expand if we hit the right-side buffer end: */
              if (numWords < (size_t)ms->withincount)
                {                               /* not all N found */
                  /* Find remaining words by expanding left: */
                  numWords = (size_t)ms->withincount - numWords;
                  delimsEnd = ms->hit + ms->hitsz;
                  ms->hit = TXmmFindNWordsLeft(ms, ms->hit, 0, &numWords);
                  ms->hitsz = delimsEnd - ms->hit;
                }
              /* Slide delims left if there is a close NOT on the right: */
              if (rightNot != SELPN &&
                  ms->hit + ms->hitsz > rightNot->hit)
                {                               /* delims overlap `rightNot'*/
                  numWords = ms->withincount;
                  ms->hit = TXmmFindNWordsLeft(ms, rightNot->hit,0,&numWords);
                  ms->hitsz = rightNot->hit - ms->hit;
                  if (numWords < (size_t)ms->withincount)   /* out of room */
                    goto noMatch;
                  /* Make sure we do not now overlap a left NOT: */
                  if (leftNot != SELPN &&
                      ms->hit < leftNot->hit + leftNot->hitsz)
                    goto noMatch;
                  /* Rubber-band right edge in to edge of rightmost word,
                   * just for neatness:
                   */
                  for (loc = rightNot->hit;
                       loc > ms->hit && !wordc[*(loc - 1)];
                       loc--);
                  ms->hitsz = loc - ms->hit;
                }
              /* Slide delims right if there is a close NOT on the left: */
              else if (leftNot != SELPN &&
                       ms->hit < (setEnd = leftNot->hit + leftNot->hitsz))
                {                               /* delims overlap `leftNot' */
                  numWords = ms->withincount;
                  delimsEnd = TXmmFindNWordsRight(ms, leftNot->hit +
                                                leftNot->hitsz, 0, &numWords);
                  ms->hit = setEnd;
                  ms->hitsz = delimsEnd - ms->hit;
                  if (numWords < (size_t)ms->withincount)   /* out of room */
                    goto noMatch;
                  /* Make sure we do not now overlap a right NOT: */
                  if (rightNot != SELPN && delimsEnd > rightNot->hit)
                    goto noMatch;
                  /* Rubber-band left edge in to edge of leftmost word,
                   * just for neatness:
                   */
                  for ( ;
                        ms->hit < leftSet->hit && !wordc[*ms->hit];
                        ms->hit++);
                  ms->hitsz = delimsEnd - ms->hit;
                }
              goto gotMatch;
            }
          /* Bug 3021: might be able to get a match by advancing a set: */
          ms->hit = delimBeginOrg;              /* restore for this call */
          ms->hitsz = delimEndOrg - delimBeginOrg;
          rightEdgeMinusN = TXmmFindNWordsLeft(ms, rightSet->hit +
                                             rightSet->hitsz, 0x1, &numWords);
          if (TXmmAdvanceASetForWithinN(ms, anchorSetIdx, leftSetIdx,
                                 rightSetIdx, leftEdgePlusN, rightEdgeMinusN))
            goto again;
          goto noMatch;
        }
      /* For API3WITHIN_TYPE_RADIUS, look for an anchor set that is
       * within-N of *both* `leftSet' and `rightSet' (Bug 690/fdbi.c:1.193
       * bug C).
       */
      /* If two or fewer sets needed, no need for "anchor" set check;
       * each set is an edge, and setuphit() limited to N-left/N-right.
       * But we do need to check for NOTs, so cannot use this optimization:
       */
      /* if (ms->nands + (ms->intersects + 1) <= 2) goto gotMatch; */
      /* We assume `w/N' is commutative for API3WITHINWORD, unlike
       * API3QWITHINCHAR, so that we can look from the fixed edges
       * inward for the anchor, rather than look outward from the
       * iterative anchor choices for the edges.  This means we can
       * call TXmmFindNWords...() once for the edges, not multiple
       * times for every anchor possibility.  We will need to fix up
       * the TXmmFindNWords...() results by skipping to the edge of
       * the next word though:
       */
      numWords = ms->withincount;
      leftEdgePlusN = TXmmFindNWordsRight(ms, leftSet->hit, 0x3, &numWords);
      numWords = ms->withincount;
      rightEdgeMinusN = TXmmFindNWordsLeft(ms, rightSet->hit +rightSet->hitsz,
                                           0x3, &numWords);
      n = ms->nands + ms->nsets;
      for (i = 0; i < n; i++)                   /* all ANDs and SETs */
        {
          set = ms->el[i];
          if (!set->member) continue;
          /* Note: TXmmAdvanceASetForWithinN() assumes middle anchors
           * must pass this leftEdgePlusN/rightEdgeMinusN test:
           */
          if (set->hit <= leftEdgePlusN &&
              set->hit + set->hitsz >= rightEdgeMinusN)
            {
              /* `set' can be our radius anchor; now find final delims: */
              numWords = ms->withincount;
              ms->hit = TXmmFindNWordsLeft(ms, set->hit, 0, &numWords);
              numWords = ms->withincount;
              delimsEnd = TXmmFindNWordsRight(ms, set->hit + set->hitsz, 0,
                                              &numWords);
              ms->hitsz = delimsEnd - ms->hit;
              /* Make sure no NOTs within delims: */
              if (leftNot != SELPN &&
                  leftNot->hit + leftNot->hitsz > ms->hit)
                /* No match, but do not bail yet: might be able to use
                 * a later set as the anchor:
                 */
                continue;                       /* not a match (yet?) */
              if (rightNot != SELPN &&
                  rightNot->hit < ms->hit + ms->hitsz)
                continue;                       /* not a match (yet?) */
              goto gotMatch;
            }
        }
      /* Bug 2899: might be able to get a match for ...TYPE_RADIUS
       * by advancing a set:
       */
      ms->hit = delimBeginOrg;                  /* restore for this call */
      ms->hitsz = delimEndOrg - delimBeginOrg;
      if (TXmmAdvanceASetForWithinN(ms, anchorSetIdx, leftSetIdx,
                                 rightSetIdx, leftEdgePlusN, rightEdgeMinusN))
        goto again;
      break;
    }
noMatch:                                        /* current hits not a match */
  return(0);

gotMatch:
  return(1);                                    /* we have a match */
}

/************************************************************************/

int
findintrsect(ms, eln)
MM3S *ms;
int eln;
/* Finds the remaining required sets (ANDs, intersections and no NOTs),
 * assuming set `eln' has already been found.
 * Called after setuphit() has found delimiters and set `ms->hit'/`ms->hitsz'.
 * Returns 1 if overall query logic satisified, 0 if not.
 */
{
 static const char      fn[] = "findintrsect";
 int i, ret;
 byte *loc;
 int reqands,reqsets,reqnots;
 byte *sdloc,*edloc;

 /* KNG 200911014 Bug 2846/2901: since setuphit() might set more
  * expansive delimiters for w/N than the final/true ones, we might
  * find NOTs here within setuphit() delimiters, but that actually are
  * *outside* the final delimiters (determined by TXmmCheckWithinN());
  * this would erroneously exclude the hit.  So we will need to know
  * the true location of NOT hits for TXmmCheckWithinN() to resolve this
  * below; thus, turn off findsel()'s NOT inversion so that it does
  * not NULL out NOT hits.  We will need to remember this when
  * checking NOT logic:
  */
 if (ms->withincount > 0) ms->noNotInversion = 1;

 sdloc=ms->hit;
 edloc=sdloc+ms->hitsz;
 reqands=ms->nands;            /* required number of ands sets and nots */
 reqnots=ms->nnots;
 reqsets=ms->intersects+1;              /* could be <= 0 after Bug 7375 */
 if(reqsets>ms->nsets)               /* PBR 05-18-92 - all and search */
    reqsets=ms->nsets;


 switch(ms->el[eln]->logic)          /* count in the first el (el[eln]) */
    {
     case LOGIAND: reqands= reqands>0 ? reqands - 1 : 0;break;
     case LOGISET: reqsets= reqsets>0 ? reqsets - 1 : 0;break;
     case LOGINOT: reqnots= reqnots>0 ? reqnots - 1 : 0;break;
     default:
       putmsg(MERR + UGE, fn, "Unknown logic value #%d",
              (int)ms->el[eln]->logic);
       goto noMatch;
    }

 for(i=0;i<ms->nels;ms->el[i++]->member=0);       /* reset member flags */
 ms->el[eln]->member=1;                           /* cur el is a member */
                                 /* 1st el already found, find the rest */
 for(i=0;i<ms->nels;i++)                                /* PBR 01-29-91 */
     {
      TXLOGITYPE log = ms->el[i]->logic;                /* alias logic */
      if(i==eln || (ms->el[i]->nib & 0x1)) continue;    /* PBR 01-29-91 */

      if(log==LOGISET)
         {
           /* We are now in the SETs, past all ANDs (due to selcmp()
            * ordering of `ms->el' array).  Thus, if we still have
            * unsatisfied ANDs, overall logic fails:
            */
          if(reqands) goto noMatch; /* go home if logic cant be satisfied */
          /* If no more SETs needed, do not bother searching for this SET: */
          if(!reqsets) continue;    /* these are satisfied, continue on */
         }
      else if(log==LOGINOT && (reqands || reqsets))
        /* We have reached the NOTs, yet still have unsatisified ANDs or
         * SETs; due to selcmp() ordering, we will not see any more
         * ANDs/SETs, so logic fails:
         */
         goto noMatch;              /* go home if logic cant be satisfied */

                        /* find occs of this el */
      loc = ms->el[i]->hit;
      if (loc == BPNULL || loc < sdloc)         /* PBR 02-01-91 */
        /* NOTE: TXmmCheckWithinN() assumes CONTINUESEARCH on a non-`eln'
         * set will search at least `sdloc' to `edloc':
         */
        loc = (byte *)findsel(ms, i, sdloc, edloc, SEARCHNEWBUF);

      if(loc!=BPNULL && loc<edloc)                      /* PBR 02-01-91 */
        {
         ms->el[i]->member=1;   /* Now Im a member of the current logic */
         switch(log)
            {
             case LOGIAND: reqands= reqands>0 ? reqands - 1 : 0; break;
             case LOGISET: reqsets= reqsets>0 ? reqsets - 1 : 0; break;
             case LOGINOT:
               /* findsel() inverts return value for NOTs (i.e. if found
                * returns NULL, if not found returns `sdloc'), so we can
                * treat it as a "find requirement" like ANDs and SETs.
                * (If `ms->noNotInversion', this inversion was turned off;
                * we will ignore `reqnots' an check NOT locations below.):
                */
               reqnots = reqnots > 0 ? reqnots - 1 : 0;
               break;
             default:
               break;
            }
        }
     }
 if (reqands || reqsets ||
     /* Only trust `reqnots' if NOTs inverted by findsel() (normally): */
     (reqnots && !ms->noNotInversion))
    goto noMatch;

 /* KNG 20091113 Bug 2846/2901: we had to loosen setuphit() bounds for
  * w/N, so re-validate here with TXmmCheckWithinN(), which will also
  * check NOTs (see `ms->noNotInversion' comment at top):
  */
 if (ms->withincount > 0 && !TXmmCheckWithinN(ms, eln))
   {
   noMatch:
     ret = 0;                                   /* no match */
     goto done;
   }

 ret = 1;                                       /* success: match */
done:
 ms->noNotInversion = 0;                        /* restore flag to default */
 return(ret);
}

/************************************************************************/

static int setuphit2 ARGS((MM3S *, int));

static int
setuphit2(ms,eln)
MM3S *ms;
int eln;             /* PBR 01-29-91 element that the hit is rooted on  */
/* Returns 1 on success, 0 on error (e.g. delims required but not found).
 */
{
 byte *loc=ms->el[eln]->hit;    /* init the location to the located SEL */
 byte *sdloc,*edloc;
 /* putmsg(999, "setuphit2", ""); */
 if((sdloc=getrex(ms->sdx,ms->start,loc,BSEARCHNEWBUF))==BPNULL)
 {
    if(ms->reqsdelim)
       return 0; /* No delim, no hit */
    else
       sdloc=ms->start;
 }
 else if(!ms->incsd) sdloc+=rexsize(ms->sdx);            /* bump delims */
 /* Look for end delimiter.  Begin searching at start delimiter,
  * not at end of hit, so that we can detect a situation like this:
  *   ...start1...end1...hit...start2...end2...
  * where `hit' is not actually within delimiters, even though it's
  * between `start1' and `end2':
  */
 if((edloc=getrex(ms->edx,sdloc,ms->end,SEARCHNEWBUF))==BPNULL)
 {
     if(ms->reqedelim)
         return 0; /* No delim, no hit */
     else
         edloc=ms->end;
 }
 else if(ms->inced) edloc+=rexsize(ms->edx);             /* bump delims */
 loc+=ms->el[eln]->hitsz;        /* PBR 01-29-91 look for edx after SEL */
 if(edloc < loc)
 	return 0; /* Found end-delim before end of hit. */
 if(getrex(ms->sdx,loc,edloc,BSEARCHNEWBUF)!=BPNULL)
    return 0; /* Found start delim after hit */
 ms->hit=sdloc;
 ms->hitsz=(int)(edloc-sdloc);
 return 1;
}

static int setuphitWithinChar ARGS((MM3S *ms, int eln));
static int
setuphitWithinChar(MM3S *ms, int eln)
/* Find delimiters for within N characters - WTF Unicode / UTF-8.
 * Note that delimiters set here may be wider than the final delimiters,
 * for various reasons (see comments); TXmmCheckWithinN()
 * finds the final delimiters.
 * Returns 1 on success, 0 on error (e.g. delims required but not found).
 */
{
  byte          *loc;
  byte          *sdloc, *edloc, *wordc;
  size_t        numCharsToFind;

  /* See Bug 2846 comment in setuphitWithinWord() on why 2N instead of N: */
  if (API3WITHIN_GET_TYPE(ms->withinmode) == API3WITHIN_TYPE_SPAN ||
      ms->nands + ms->intersects + 1 <= 2)
    numCharsToFind = ms->withincount;
  else
    numCharsToFind = 2*ms->withincount;

  /* Bug 2901:  Due to varying word lengths, w/N is not commutative for
   * API3WITHINCHAR TYPE_RADIUS, e.g. given query `/A+ /B+ /C+ w/5',
   * this text matches:
   *     AAA..BBBBBBB..CCC
   * because `CCC' is w/5 of `BBBBBBB' (and `AAA' w/5 of `BBBBBBB'),
   * because `w/N' counts from the *edge* of the anchor set (`BBBBBBB').
   * But `BBBBBBB' is not w/5 of `CCC' (nor `AAA').  So if `eln' is `AAA',
   * going 2N right of it here will not be enough to let `CCC' be found.
   *   Thus, for TYPE_RADIUS, instead of going 2N right, we go N, then
   * skip to end of word (in case that is the real anchor), then N more;
   * similarly for left.  TXmmCheckWithinN() does final validate.
   * WTF is that enough to cover all possibilities?  We do not do this
   * skip-to-end-of-word extension for TYPE_SPAN, since all sets are
   * *inside* (counted in) the `w/N' value for TYPE_SPAN.
   */
  if (API3WITHIN_GET_TYPE(ms->withinmode) == API3WITHIN_TYPE_RADIUS)
    {
      wordc = pm_getwordc();
      loc = ms->el[eln]->hit;
      if ((loc - ms->start) > ms->withincount)
        {
          sdloc = loc - ms->withincount;
          while (sdloc > ms->start && wordc[*sdloc]) sdloc--;
          if (numCharsToFind > (size_t)ms->withincount)
            {                                   /* if still more to find */
              if ((sdloc - ms->start) > ms->withincount)
                sdloc -= ms->withincount;
              else
                sdloc = ms->start;
            }
        }
      else
        sdloc = ms->start;
      loc += ms->el[eln]->hitsz;                /* now look right of set */
      if ((ms->end - loc) > ms->withincount)
        {
          edloc = loc + numCharsToFind;
          while (edloc < ms->end && wordc[*edloc]) edloc++;
          if (numCharsToFind > (size_t)ms->withincount)
            {                                   /* if still more to find */
              if ((ms->end - edloc) > ms->withincount)
                edloc += ms->withincount;
              else
                edloc = ms->end;
            }
        }
      else
        edloc = ms->end;
    }
  else                                          /* API3WITHIN_TYPE_SPAN */
    {
      /* All sets are included in `w/N' span for TYPE_SPAN, unlike
       * TYPE_RADIUS where the anchor set is not included:
       */
      loc = ms->el[eln]->hit + ms->el[eln]->hitsz;
      if ((size_t)(loc - ms->start) > numCharsToFind)
        sdloc = loc - numCharsToFind;
      else
        sdloc = ms->start;                      /* no hit, use beg of buffer*/
      loc = ms->el[eln]->hit;
      if ((size_t)(ms->end - loc) > numCharsToFind)
        edloc = loc + numCharsToFind;
      else
        edloc = ms->end;                        /* no hit, use end of buffer*/
    }
  ms->hit = sdloc;
  ms->hitsz = (int)(edloc - sdloc);
  return(1);
}

static int setuphitWithinWord ARGS((MM3S *ms, int eln));
static int
setuphitWithinWord(MM3S *ms, int eln)
/* Find delimiters for within N words - WTF Unicode / UTF-8.
 * Note that delimiters set here may be wider than the final delimiters,
 * for various reasons (see comments); TXmmCheckWithinN()
 * finds the final delimiters.
 * Returns 1 on success, 0 on error (e.g. delims required but not found).
 */
{
  size_t        tofind, numWords;
  SEL           *set = ms->el[eln];

  /*   Bug 2846: Unlike other `w/' types, `w/N' delimiters' locations are
   * relative to a found set, and thus their locations could change
   * based on what "anchor" set is used.  E.g. for API3WITHINWORD
   * ...TYPE_RADIUS, query `AA BB CC w/3', this search text matches:
   *     AA xx xx BB xx xx CC
   * but the `BB' term must be the anchor: `AA' and `CC' are w/3 of it.
   * `AA' cannot be the anchor, since `CC' is 6 words away from it.
   *   Since setuphit() is called when only the first set (not the
   * whole query) has been found, we do not yet know if this set can be the
   * `w/N' "anchor".  Thus the delimiter locations we set here must allow
   * for all possibilities, e.g. do not cut off the future `CC' hit above
   * if we are looking at hit `AA' now.  Thus, instead of looking
   * N-left and N-right of the set (e.g. for ...TYPE_RADIUS), we look
   * 2N-left and 2N-right of it, but fix up delimiters again later in
   * TXmmisWithinN() as we find more sets.  (We *can* use N instead of 2N
   * if only looking for two sets, since neither will be in the middle.
   * Can also use N if API3WITH_TYPE_SPAN, since whole span is N then.):
   */
  if (API3WITHIN_GET_TYPE(ms->withinmode) == API3WITHIN_TYPE_SPAN)
    {
      numWords = ms->withincount;
      /* *All* sets are counted by `w/N' for ...TYPE_SPAN
       * (...TYPE_RADIUS does not count the anchor set itself),
       * so look for start-delim from *right* edge of anchor:
       */
      ms->hit = TXmmFindNWordsLeft(ms, set->hit + set->hitsz, 1, &numWords);
      numWords = ms->withincount;
      ms->hitsz = (int)(TXmmFindNWordsRight(ms, set->hit, 1, &numWords) -
                        ms->hit);
    }
  else                                          /* API3WITHIN_TYPE_RADIUS */
    {
      if (ms->nands + ms->intersects + 1 <= 2)
        tofind = ms->withincount;
      else
        tofind = 2*ms->withincount;
      numWords = tofind;
      ms->hit = TXmmFindNWordsLeft(ms, set->hit, 0, &numWords);
      numWords = tofind;
      ms->hitsz = (int)(TXmmFindNWordsRight(ms, set->hit + set->hitsz, 0,
                                            &numWords) - ms->hit);
    }
  return(1);
}

int
setuphit(ms,eln)     /* find start & end delimiters and setup hit stuff */
MM3S *ms;
int eln;             /* PBR 01-29-91 element that the hit is rooted on  */
/* Returns 1 on success, 0 on error (e.g. delims required but not found).
 */
{
 byte *loc=ms->el[eln]->hit;    /* init the location to the located SEL */
 byte *sdloc,*edloc;
 if(!ms->delimseq && !ms->olddelim)
    return setuphit2(ms,eln);
 if(ms->withincount > 0)                        /* w/N used */
 {
   /* Both ...TYPE_RADIUS and ...TYPE_SPAN use the same setuphit... functions;
    * the differences are handled later in TXmmCheckWithinN()
    * functions:
    */
   switch(API3WITHIN_GET_UNIT(ms->withinmode))
     {
     case API3WITHINCHAR:
       return(setuphitWithinChar(ms, eln));
     case API3WITHINWORD:
       return(setuphitWithinWord(ms, eln));
     }
 }
 if((sdloc=getrex(ms->sdx,ms->start,loc,BSEARCHNEWBUF))==BPNULL)
    sdloc=ms->start;             /* no hit, use beg of buffer */
 else if(!ms->incsd) sdloc+=rexsize(ms->sdx);            /* bump delims */
 loc+=ms->el[eln]->hitsz;        /* PBR 01-29-91 look for edx after SEL */
 if((edloc=getrex(ms->edx,loc,ms->end,SEARCHNEWBUF))==BPNULL)
     edloc=ms->end;               /* no hit, use end of buffer */
 else if(ms->inced) edloc+=rexsize(ms->edx);             /* bump delims */
 ms->hit=sdloc;
 ms->hitsz=(int)(edloc-sdloc);
 return 1;
}

/************************************************************************/

byte *
findmm(ms)
MM3S *ms;
{

 register byte *loc;

 ms->hit=BPNULL;

#ifdef ALLOWALLNOT
 register byte *sdloc,*edloc;
 stop. this wont work as is - loc needs to be set     /* MAW 02-14-91 */
 if(ms->el[0]->logic==LOGINOT)       /* el[0] not's work really weird ! */
    {
     for(edloc=getrex(ms->sdx,loc,ms->end,SEARCHNEWBUF);
         edloc!=BPNULL;
         edloc=getrex(ms->sdx,loc,ms->end,CONTINUESEARCH)
        )
         {
          if((sdloc=getrex(ms->sdx,ms->start,edloc,BSEARCHNEWBUF))==BPNULL)
              sdloc=ms->start;             /* no hit, use beg of buffer */
          if((loc=(byte *)findsel(ms,0,sdloc,edloc,SEARCHNEWBUF))!=BPNULL)
              {
WTF - New delim behaviour.
               ms->hit=sdloc;
               ms->hitsz=(int)(edloc-sdloc);
               if(findintrsect(ms,0))
                   return(ms->hit);
               else ms->hit=BPNULL;
              }
         }
    }
 else
#endif /* ALLOWNOT */

 /* selcmp() ordered `ms->el' with ANDs first, so if there are any ANDs,
  * the first element will be an AND:
  */
 if(ms->el[0]->logic==LOGIAND)                  /* at least one AND */
    {
                        /* while finding el[0] */
      for(ms->el[0]->hit=BPNULL,loc=(byte *)findsel(ms,0,ms->start,ms->end,SEARCHNEWBUF);
          loc!=BPNULL;
          ms->el[0]->hit=BPNULL,loc=(byte *)findsel(ms,0,ms->start,ms->end,CONTINUESEARCH)
         )
         {
          if(setuphit(ms,0) &&                               /* PBR 01-29-91 */
             findintrsect(ms,0))
             {
              return(ms->hit);
             }
          else ms->hit=BPNULL;
         }
    }

 else if(ms->el[0]->logic==LOGISET)             /* there are no ANDs */
    {
     int i,eln = 0;
     int passes;
     /* Bug 5612: valid hit could occur at end of buf, if zero-length; +1: */
     byte *first = ms->end + 1, *prev;
     SEL **set=ms->el;                                         /* alias */

     /* We will need to find at least `ms->intersects + 1' of the
      * `ms->nsets' SET-logic sets; therefore at least one of the
      * first `ms->nsets - ms->intersects' sets in the `ms->el' array
      * will have to be part of the final hit.  Find the earliest
      * occurring of those first `passes' sets. (KNG interpretation;
      * I did not write this code):
      *
      * Note that after Bug 7375 `ms->intersects' could be < 0,
      * i.e. no SETs required; should only happen if ANDs are present:
      */
     passes = ms->nsets - ms->intersects;
     for(i=0;i<passes;i++)
        {
         set[i]->member=1; /* so inset knows its ok */
#ifndef KAI_SEL_HACK
         loc=(byte *)findsel(ms,i,ms->start,ms->end,SEARCHNEWBUF);
#else
         /* KNG 990309 is this faster, and correct? */
         loc=(byte *)((set[i]->nib & 0x1) || set[i]->hit >= ms->start ?
                      set[i]->hit :
                      findsel(ms,i,ms->start,ms->end,SEARCHNEWBUF));
#endif
         if(loc==BPNULL) set[i]->nib |= 0x1;
         else if(loc<first)
             {
              first=loc;
              eln=i;
             }
        }

     while (first < ms->end + 1)                /* found at least one set */
         {
          prev=set[eln]->hit+set[eln]->hitsz;        /* setup prior hit */
          if(setuphit(ms,eln) && findintrsect(ms,eln))
              return(ms->hit);

          /* Bug 5612: valid hit could occur at end of buffer; + 1: */
          first = ms->end + 1;                  /* reset `first' */

          if(*ms->sdexp=='\0' && *ms->edexp=='\0')
             goto END; /* PBR MAW 10-17-95 Hack to keep it from reviewing
                          a buffer endlessly in the special case of
                          w/buffer. This will most certainly produce
                          some missing hits at some time in the future
                      */

          for(i=0;i<passes;i++)
             {
              set[i]->member=1;
              if(set[i]->nib & 0x1) continue;
              loc=set[i]->hit;
              if(loc<prev)
                  {             /* look for new set just after last one */
                   loc=findsel(ms,i,prev,ms->end,SEARCHNEWBUF);
                   if(loc==BPNULL)
                        {
                         set[i]->nib |= 0x1;
                         continue;
                        }
                  }
              if(loc<first)
                  {
                   first=loc;
                   eln=i;
                  }
             }
         }
    }
 END:
 return(ms->hit=BPNULL);
}

/************************************************************************/

byte *
getmm(ms,buf,end,operation)
MM3S *ms;
byte *buf,*end;
int operation;
{
 char *Fn="search for hit";
 int i;
 byte   *ret;

 if(operation==SEARCHNEWBUF)
    {
     cmptab=NULL;                       /* reset in case locale changed */
     for(i=0;i<ms->nels;i++)
         {
          ms->el[i]->hit=BPNULL;                        /* PBR 01-29-91 */
          ms->el[i]->nib=0     ;                        /* PBR 02-01-91 */
         }
     ms->start=buf;
     ms->end=end;
     ms->hit=BPNULL;
    }
 else if(operation==CONTINUESEARCH)
    {
     ms->start=ms->hit+ms->hitsz;
     if (ms->start >= ms->end) goto err;
     for(i=0;i<ms->nels;i++)                            /* PBR 01-29-91 */
         if(ms->el[i]->hit!=BPNULL && ms->el[i]->hit<ms->start)
              ms->el[i]->hit=BPNULL;
    }
 else
    {
     putmsg(MERR,Fn,"invalid operation");
     goto err;
    }
 ret = findmm(ms);
 goto finally;

err:
 ret = NULL;
finally:
 return(ret);
}

/************************************************************************/

#ifndef FORMMAPI
static byte * /* adds a space to end of string and rets a ptr to the \0 */
spacecat(a)
byte *a;
{
 for(;*a!='\0';a++);
 *a++=' ';
 *a='\0';
 return(a);
}

/************************************************************************/

void
printhit(ms,fnm,foff,buf)       /* print the bloody hit to the msg file */
MM3S *ms;
byte *fnm;
long foff;
byte *buf;
{
 char mindexbuf[MDXMAXSZ+1];             /* buffer to hold index string */
 int i,j;
 long hitoffset=foff+(long)(ms->hit-buf);

 putmsg(MHITLOGIC,CPNULL,CPNULL);
 for(i=0;i<ms->nels;i++)
    {
     byte *subhit=ms->el[i]->hit;
     if(ms->el[i]->member && ms->el[i]->logic!=LOGINOT)
         {
          for(j=0;j<ms->el[i]->hitsz;j++)
              putmsg(-1,CPNULL,"%c",subhit[j]);
          putmsg(-1,CPNULL," ");
         }
    }
 putmsg(-1,CPNULL,CPNULL);                                 /* put EOL */
/* PBR 06-02-91 made mindex point to the middle of the hit */
 while(getmdx(mindexbuf,MDXMAXSZ,hitoffset+(ms->hitsz>>1),ms->mindex))
    putmsg(MMDXHIT,CPNULL,"%03d %s",strlen(mindexbuf),mindexbuf);


/* the format of the hit is: MSGNUM FNAME FILE-OFFSET SIZE [OFFSET SIZE]... */
/* pbr 11/27/90 changed below to support hit command */

 if(ms->cmd!=BPNULL)
    {
     byte cmdln[MAXHITLNSZ];
     byte *p,*q;
     strcpy((char *)cmdln,(char *)ms->cmd);
     p=spacecat(cmdln);
     for(q=fnm;*q && *q!='@';*p++ = *q++); /* rm @offset,len from fname */
     sprintf((char *)p," %ld %d",hitoffset,ms->hitsz);
     for(i=0;i<ms->nels;i++)
        {
         if(ms->el[i]->member && ms->el[i]->logic!=LOGINOT)
            {
             p=spacecat(p);
             sprintf((char *)p, "%d %d", (int)(ms->el[i]->hit - ms->hit),
                     (int)ms->el[i]->hitsz);
            }
        }
#    ifdef MVS                                        /* MAW 02-12-91 */
        {
         char *args;
         for(args=(char *)cmdln;*args!=' ' && *args!='\0';args++) ;
         if(*args==' ')
             {
              *args='\0';
              for(++args;*args==' ';args++) ;
             }
         else args=(char *)NULL;
         systm((char *)cmdln,args,(char *)NULL);
        }
#    else
        system((char *)cmdln);
#    endif
    }

#ifdef RDPACKED        /* PBR 09-11-92 q&d support for compressed files */
 if(ispx((char *)fnm))
    {
     char *znm=tmpnam((char *)NULL);
     FILE *fh=fopen(znm,"wb");
     if(fh!=(FILE *)NULL)
         {
          byte *beg;
          byte *end;
          beg=ms->hit-PXHITFRAME;
          if(beg<buf)beg=buf;
          end=ms->hit+ms->hitsz+PXHITFRAME;
          if(end>ms->end) end=ms->end;
          fwrite(beg,end-beg,sizeof(char),fh);
          fclose(fh);
          putmsg(MHIT-1,CPNULL,CPNULL);
#         ifdef NOWHITEINFILENAME
              putmsg(-1,CPNULL,"%s ",(char *)fnm);
#         else
              {/* MAW 11-22-93 - support space in filename - wtp ascii */
               byte *p;
               for(p=fnm;*p!='\0' && *p!=' ' && *p!='\t';p++) ;
               if(*p=='\0')
                   putmsg(-1,CPNULL,"%s ",(char *)fnm);
               else/* only quote if weird so sane people have it easy */
                   putmsg(-1,CPNULL,"\"%s\" ",(char *)fnm);
              }
#         endif
          putmsg(-1,CPNULL,"%ld %d ",hitoffset,ms->hitsz);
          putmsg(-1,CPNULL,CPNULL);                          /* put EOL */
          putmsg(MHIT,CPNULL,CPNULL);
          putmsg(-1,CPNULL,"%s %ld %d ",(char *)znm,(long)(ms->hit-beg),ms->hitsz);
         }
    }
 else
#endif
    {
     putmsg(MHIT,CPNULL,CPNULL);
#    ifdef NOWHITEINFILENAME
         putmsg(-1,CPNULL,"%s ",(char *)fnm);
#    else
         {/* MAW 11-22-93 - support space in filename - wtp ascii */
          byte *p;
          for(p=fnm;*p!='\0' && *p!=' ' && *p!='\t';p++) ;
          if(*p=='\0')
              putmsg(-1,CPNULL,"%s ",(char *)fnm);
          else/* only quote if weird so sane people have it easy */
              putmsg(-1,CPNULL,"\"%s\" ",(char *)fnm);
         }
#    endif
     putmsg(-1,CPNULL,"%ld %d ",hitoffset,ms->hitsz);
    }
 for(i=0;i<ms->nels;i++)
     if(ms->el[i]->member)
          putmsg(-1,CPNULL,"%d %d ",ms->el[i]->hit-ms->hit,ms->el[i]->hitsz);
 putmsg(-1,CPNULL,CPNULL);                                 /* put EOL */
 okdmsg();
 datamsg(1,(char *)ms->hit,sizeof(char),ms->hitsz);
 putmsg(MENDHIT,CPNULL,"End of Metamorph hit");
}
#endif                                                    /* FORMMAPI */

/************************************************************************/

/*

1/19/90 PBR

This checks for an offset and size as part of the filename in the
form:
/usr/local/text@1000,100
which xlates to:
offset 1000 for 100 characters in the file /usr/local/text

*/

int
fnameseek(fname,offset,len)
char *fname;
long *offset;
long *len;
{
 *offset=0L;
 *len=HUGEFILESZ;
 for(;*fname!='\0';fname++)
    {
     if(*fname==FSEEKCHAR)
         {
          if(isdigit((int)(*(byte *)fname+1)))
              {
               *fname='\0';
               fname++;
               *offset=atol(fname);
               for(;*fname!='\0';fname++)
                  {
                   if(*fname==',' && isdigit(*(fname+1)))
                        {
                         ++fname;
                         *len=atol(fname);
                         return(1);
                        }
                  }
               return(1);
              }
          else return(0);
         }
    }
 return(0);
}

/************************************************************************/

byte *
allocmmrdbuf(ms)
MM3S *ms;
{
 byte *p;

   for(p=BPNULL,ms->mmrdbufsz=MMRDBUFSZ;
       ms->mmrdbufsz>=MMRDBUFMINSZ;
       ms->mmrdbufsz-=MMRDBUFDECSZ
      )
       if((p=(byte *)calloc(ms->mmrdbufsz,sizeof(byte)))!=BPNULL) break;
   if(ms->mmrdbufsz<MMRDBUFSZ)
      {
       putmsg(MWARN+MAE,CPNULL,"Not enough memory - Read buffer adjusted to %u bytes",ms->mmrdbufsz);
      }
   return(p);
}

/************************************************************************/

#ifdef TEST

/**********************************************************************/

#  ifdef RDPACKED
static void closesrch ARGS((FILE *fp,int fpispipe));
static void
closesrch(fp,fpispipe)
FILE *fp;
int fpispipe;
{
   if(fpispipe) pclose(fp);
   else         fclose(fp);
}

/**********************************************************************/

static FILE *opensrch ARGS((char *fn,int *ispipe));
static FILE *
opensrch(fn,ispipe)
char *fn;
int *ispipe;
{
PX *px;
int l=strlen(fn);

   for(px=pxlst;px->ext!=(char *)NULL;px++){
      if(tailcmp(px->ext,fn,l)==0){
      char *cmd=(char *)malloc(strlen(px->cmd)+strlen(fn)+2+2);
         if(cmd==(char *)NULL) goto zopen;
         else{
         FILE *fp;
            strcpy(cmd,px->cmd);
            strcat(cmd,"\"");
            strcat(cmd,fn);
            strcat(cmd,"\"");
            if((fp=popen(cmd,"r"))==(FILE *)NULL) goto zopen;
            free(cmd);
            *ispipe=1;
            return(fp);
         }
      }
   }
zopen:
   *ispipe=0;
   return(fopen(fn,"rb"));
}
#  endif /* !RDPACKED */

/************************************************************************/
int
domme(ms)
MM3S *ms;
{
 FNS *fns=(FNS *)NULL;           /* 3/1/90 PBR file name list support */
 char *fname;
 char *Fn=(char *)NULL;
 byte *buf=allocmmrdbuf(ms);
 int  nread;                       /* file number, number of bytes read */
 long foff,ftot,fmax,ntot;         /* file offset, file total, file max */
 byte *hitloc,*end;                      /* hit location, end of buffer */
 time_t stime;
#ifdef PRINTMMTIME
 time_t etime;
#endif /* PRINTMMTIME */
 int hits;
 int retval=0;
 long shits=0L,sntot=0L;                               /* search totals */

 if(buf==BPNULL)
   {
    putmsg(MERR+MAE,Fn,"out of memory");
    return(0);
   }

 putmsg(MMPROCBEG,Fn,"search engine started");
 time(&stime);

 if(ms->filter_mode)
    {
     ms->fh=stdin;
     for(foff=0L,nread=freadex(ms->fh,buf,ms->mmrdbufsz,ms->edx);
         nread;
         foff+=nread,nread=freadex(ms->fh,buf,ms->mmrdbufsz,ms->edx)
        )
         {
                     /* write the input to output */
          fwrite((char *)buf,sizeof(byte),nread,stdout);
          for(end=buf+nread,hitloc=getmm(ms,buf,end,SEARCHNEWBUF);
              hitloc!=BPNULL;
              hitloc=getmm(ms,buf,end,CONTINUESEARCH)   /* PBR 01-31-91 */
             )
              {
               printhit(ms,(byte *)"stdin",foff,buf);/* it's a hit , so shove it out */
               ++shits;
              }
         }
     sntot=foff+nread;
     retval=1;
     goto END;
    }
 if((fns=openfn((char **)ms->searchfiles))==(FNS *)NULL)
    {
     retval=0;
     putmsg(MERR,Fn,"Cannot process file list");
     goto END;
    }
 while(*(fname=getfn(fns))!='\0')
    {
     fnameseek(fname,&foff,&fmax);
     ms->fh=opensrch(fname,&ms->fhispipe);
     putmsg(MFILEINFO,Fn,"opening the file \"%s\"",fname);
     ms->mindex=openrmdx(fname);/* open the mindex file */
     hits=0;
     if(ms->fh==(FILE *)NULL)
         {
          putmsg(MWARN+FOE,Fn,"unable to open input file: %s",fname);
          continue;           /* keep trying the rest of the file names */
         }
#    if defined(MSDOS) && !defined(LOWIO)
     else setvbuf(ms->fh,(char *)NULL,_IOFBF,ms->mmrdbufsz);
#    endif /* MSDOS */
          /* read the file and keep track of your offset in it */

     if(foff!=0L)
         {
          if(fseek(ms->fh,foff,SEEK_SET)!=0)
             {
              putmsg(MERR+FSE,Fn,"seek to requested offset failed");
              closesrch(ms->fh,ms->fhispipe);          /* close it up */
              continue;
             }
          putmsg(MFILEINFO,Fn,"seeking to %ld reading %ld characters",foff,fmax);
         }
     ftot=fmax;
     ntot=0L;
#define LESSER(a,b) ((a)<(b)?(a):(b))
     for(ftot-=nread=freadex(ms->fh,buf,(int)LESSER(ftot,ms->mmrdbufsz),ms->edx);
         nread && ftot>=0;
         foff+=nread,ftot-=nread=freadex(ms->fh,buf,(int)LESSER(ftot,ms->mmrdbufsz),ms->edx)
        )
         {
          ntot+=nread;
          for(end=buf+nread,hitloc=getmm(ms,buf,end,SEARCHNEWBUF);
              hitloc!=BPNULL;
              hitloc=getmm(ms,buf,end,CONTINUESEARCH)
             )
              {
               ++hits;
               printhit(ms,(byte *)fname,foff,buf);/* it's a hit , so shove it out */
              }
         }
     putmsg(MFILEINFO,Fn,"closing \"%s\", hits: %d, bytes: %ld",
            fname,hits,ntot);
     closesrch(ms->fh,ms->fhispipe);                   /* close it up */
     if(ms->mindex!=(MDXRS *)NULL)            /* close mindex (if open) */
         ms->mindex=closermdx(ms->mindex);
     shits+=hits;
     sntot+=ntot;
    }
    retval=1;                                         /* MAW 02-21-90 */
END:
 if(fns!=(FNS *)NULL)
    closefn(fns);
 free(buf);
#ifdef PRINTMMTIME
 time(&etime);
 putmsg(MTOTALS,Fn,"end of search totals:  hits %ld, bytes %ld, time %ld",
        shits,sntot,etime-stime);
#else
 putmsg(MTOTALS,Fn,"end of search totals:  hits %ld, bytes %ld",
        shits,sntot);
#endif /* PRINTMMTIME */
 putmsg(MMPROCEND,Fn,"search engine finished");
 return(retval);
}

#undef LESSER

/************************************************************************/

#ifdef USECMDLN
int
mmcmdln(argc,argv)
int argc;
char *argv[];
{
 int arg,i,j;
 char *Fn="process command line"
 extern REQUESTPARM rqp[];

 for(arg=1;arg<argc && *argv[arg]=='-';arg+=2)          /* get -options */
    {
     char *opt=argv[arg]+1
     int type;
     for(j=0;rqp[j].type!=ENDLIST;j++)
         {
          REQUESTPARM *q=&rqp[j];
          if(strcmp(opt,q->name)==0)
              {
               switch(q->type)
                   {
                    case Itype : (int  *)*name=atoi(argv[arg+1]);break;
                    case Btype : (TXbool *)*name=atoi(argv[arg+1]);break;
                    case Stype : *name=argv[arg+1];break;
                    default    :
                        putmsg(MERR,Fn,"argument \"%s\" invalid");
                        return(0);
                   }
              }
         }

    }
 return(1);
}
#endif /* USECMDLN */
/************************************************************************/
#ifdef MM3ECALL
mm3emain(argc,argv)
#else
# define return(x) exit(x)  /* WATCH OUT !!!!!! PBR 2-14-90 */
main(argc,argv)
#endif
int argc;
char *argv[];
{
 MM3S *ms;
 char *Fn="main";
 char *flist[MAXFILES+1];         /* override the files in the control file */
 int  nfiles=0;                                   /* how many are there */
 int  retcd=0;
 int  i,j;
#ifdef unix
 int  topipe=0,tofile=0;            /* 2/15/90 PBR SUPPORT FOR PIPE OUT */
#endif

#ifdef PROTECT_IT
 isok2run(PROT_MM3);  /* check for protection key and exit if missing */
#endif
#ifndef MM3ECALL
 trap24();                                            /* MAW 01-09-91 */
#endif
 if(argc<2)
    {
     putmsg(MERR+FOE,Fn,"no control file name, use: mm3e control-file(s) [-option(s)] [-f input-filename(s)]");
     return(1);
    }

#ifdef MVS                                            /* MAW 03-14-90 */
 if(expargs(&argc,&argv)<1){
    putmsg(MERR+MAE,"expargs","Can't expand wildcards");
    return(1);
 }
#endif
 for(i=0;i<argc && *argv[i]!='-';i++);               /* skip all control's */
 for(;i<argc;i++)
    {
     if(*argv[i]=='-')
         switch(*(argv[i]+1))
              {
#             ifdef unix                /* 2/15/90 support for pipe out */
               case 'p' : if(tofile) break;topipe=1;
                          mmsgfh=popen(argv[++i],"w");
                          if(mmsgfh==(FILE *)NULL)
                             {
                              mmsgfh=stderr;
                              putmsg(MERR,Fn,"Can't open pipe");
#                             ifdef MVS               /* MAW 03-14-90 */
                                 freeargs();
#                             endif
                              return(1);
                             }
                          mmsgfname=CPNULL;
                          break;
               case 'o' : if(topipe) break;tofile=1;mmsgfname=argv[++i];break;
#             else
#             ifdef MVS
               case 'O' :
#             endif
               case 'o' : mmsgfname=argv[++i];break;   /* set msg fname */
#             endif
#             ifdef MVS
               case 'F' :
#             endif
               case 'f' : for(j=i+1;nfiles<MAXFILES && j<argc;j++,nfiles++)
                             flist[nfiles]=argv[j];
                          flist[nfiles]="";
                          i=argc;
                          break;
               default  : putmsg(MERR,Fn,"Invalid command line option \"%s\"",argv[i]);
#                         ifdef MVS                   /* MAW 03-14-90 */
                             freeargs();
#                         endif
                          return(1);
              }
    }
 for(i=1;i<argc && *argv[i]!='-' ; i++)
    {
     if((ms=openmm(argv[i]))==(MM3S *)NULL)
         {
          putmsg(MERR,Fn,"Error processing the control file: %s",argv[i]);
          continue;
         }
     if(nfiles>0)
          ms->searchfiles=(byte **)flist;
     /* retcd|=domme(ms); MAW 9/25/89 - should ret 0 to env for OK */
     retcd=domme(ms);
     if(retcd) retcd=0;
     else      retcd=1;
     closemm(ms);
    }
#ifdef unix                                           /* MAW 02-20-90 */
 if(topipe) pclose(mmsgfh),mmsgfh=(FILE *)NULL;
#endif
#ifdef MVS                                            /* MAW 03-14-90 */
 freeargs();
#endif
 return(retcd);
}

/************************************************************************/
#ifdef return
#undef return
#endif
#endif                                                        /* TEST */
