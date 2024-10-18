#ifndef BOX_H
#define BOX_H
/***********************************************************************
** MAW 01-29-91 - change isok2run() internally to mmstrbox()
**                so as not show thru obviously to linker in libs
**                macro isok2run() should still be used externally
** MAW 01-30-91 - renamed "protect" stuff to "box" to hide meaning
**              - change resetrun() internally to mmstrres()
** MAW 05-02-91 - added api3cpr[] and API3PROTECT
** MAW 06-22-92 - allow/return any dongle number
** MAW 10-14-94 - add dongle counting w/iscntok()
***********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
#ifndef uchar
#  define uchar unsigned char
#endif
#ifndef uint
#  define uint unsigned int
#endif
#ifndef ushort
#  define ushort unsigned short
#endif
#ifndef ulong
#  define ulong  unsigned long
#endif
#ifndef CDECL
#  define CDECL
#endif
/**********************************************************************/
#if !defined(ALLOW_PASSWD) && !defined(macintosh)
#  define ALLOW_PASSWD
#endif
#ifdef API3PROTECT
extern char FAR api3cpr[];                       /* api copyright msg */
#endif
extern char pass3fn[], pass3file[];

#if defined(PROTECT_IT)
             /* isok2run() will print msg and exit if fail first time */
   extern uint  mmstrbox  ARGS((ulong));
   extern uint  mmstrres  ARGS((char *));
   extern uint  mmstrcnt  ARGS((int));   /* return cnt valid, dec cnt */
   extern void  box_mmstr ARGS((char *,int,uint));
#  ifdef unix
#     define PLCK_ERR  (-1)                            /* fatal error */
#     define PLCK_OK   0                          /* created lockfile */
#     define PLCK_XME  1                    /* exists & belongs to me */
#     define PLCK_XNME 2                /* exists & not belongs to me */
      int proglock   ARGS((char *));
      int progunlock ARGS((char *));
#  else
#     define proglock(a)   PLCK_OK
#     define progunlock(a) PLCK_OK

#     define BOXSEED ('m'+'a'+'w')                 /* encryption seed */
#     define PROGID 0x1d61
#  endif
#  define isok2run(a) mmstrbox(a)
#  define resetrun(a) mmstrres(a)
#  define PROT_NEW ((ulong)0x00000001)          /* args to isok2run() */
#  define PROT_MH  PROT_NEW     /* metahelp - work with any new stuff */
#  define PROT_MM3 ((ulong)0x00000002)
#  define PROT_API ((ulong)0x00000004)
#  define PROT_3DB ((ulong)0x00000008)
#  define PROT_MB  ((ulong)0x00000010)                    /* metabook */
#  define PROT_MM4 ((ulong)0x00000020)
#  define PROT_EZW ((ulong)0x00000060)           /* allow ez with mm4 */
#  define PROT_ALL (PROT_MM3|PROT_API|PROT_3DB|PROT_MB|PROT_MM4|PROT_EZW|PROT_MH)
#  define iscntok(a) mmstrcnt(a)
#  define CNT_TYPE 6       /* cnt type header byte and hi byte of cnt */
#  define CNT_CNT  7                           /* cnt header low word */
#  define DCNTNON  0x00                          /* count type: none  */
#  define DCNTCNT  0x01                          /* count type: count */
#  define DCNTDAT  0x02                          /* count type: date  */
#  define DCNTINC  0x80                            /* inc count w/du  */

#  define NEWBOXIO
#  ifdef NEWBOXIO
#     ifdef __TSC__
         extern ushort cdecl BOXIO ARGS((ushort,ushort,ushort,ushort *));
#     else
         extern ushort CDECL BOXIO ARGS((ushort,ushort,ushort,ushort *));
#     endif
#     define boxio(a,b,c,d) BOXIO(a,b,c,d)
#  else
#     ifdef __TSC__
         extern ushort cdecl BOXIO ARGS((ushort,ushort,ushort *));
#     else
         extern ushort CDECL BOXIO ARGS((ushort,ushort,ushort *));
#     endif
#     define boxio(a,b,c,d) BOXIO(b,c,d)
#  endif
#  define DEFHDRADDR static ushort hdraddr[8]={ 63,62,61,60,9,8,7,6 };/* romlock addresses */

#else
#  define isok2run(a) 1
#  define resetrun(a) 0
#endif
/**********************************************************************/
#endif                                                       /* BOX_H */
