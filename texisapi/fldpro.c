/**********************************************************************/
int
promote(f,f2)
/* Casts `f' to type ((f2->type & ~DDTYPEBITS) | probit), storing in `f2'.
 */
FLD *f;
FLD *f2;
{
#ifdef __FUNCTION__
  static const char     fn[] = __FUNCTION__;
#else
  static const char     fn[] = TX_STRINGIZE_VALUE_OF(promote);
#endif
protype *mem;
void *fmem;
int i, n, rc;
size_t alloced;
extern int TXfldmathverb;                       /* WTF */

   if (TXfldmathverb >= 3)
     putmsg(MINFO, fn,
            "promote/demote type %s(%d) to type %s%s%+.*s%s",
            TXfldtypestr(f), (int)f->n,
            ddfttypename((f2->type & ~DDTYPEBITS) | probit),
            (TXfldmathverb >= 2 ? " [" : ""),
            (int)TXfldmathVerboseMaxValueSize,
            (TXfldmathverb >= 2 ? fldtostr(f) : ""),
            (TXfldmathverb >= 2 ? "]" : ""));

   if ((f->type & DDTYPEBITS) == probit)
     {
       rc = FOP_EINVAL;
       goto done;
     }

   if (TXfldIsNull(f))                          /* Bug 3895 */
     {
       releasefld(f2);
       f2->type &= ~DDTYPEBITS;
       f2->type |= probit;
       f2->elsz = sizeof(protype);
       TXfldSetNull(f2);
       goto ok;
     }

   alloced=f->n*sizeof(protype);
   if((mem=(protype *)TXmalloc(TXPMBUFPN, fn, alloced+1))==(protype *)NULL)
     {
       rc = FOP_ENOMEM;
       goto done;
     }
   *(((char *)mem)+alloced)='\0';
   i=0;
   n=f->n;
   fmem = getfld(f, NULL);
   /* WTF if `fmem' is NULL, promote to NaN if `probit' is FTN_FLOAT/DOUBLE?*/
   /* no: SQL NULL */
   switch(f->type&DDTYPEBITS){              /* wtf - dep on dbtable.h */
   case FTN_BYTE    : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_byte     *)fmem)[i] : 0); break;
   case FTN_CHAR    : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_char     *)fmem)[i] : 0); break;
   case FTN_DECIMAL : /* wtf ni */ break;
   case FTN_DOUBLE  : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_double   *)fmem)[i] : 0); break;
   case FTN_DWORD   : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_dword    *)fmem)[i] : 0); break;
   case FTN_FLOAT   : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_float    *)fmem)[i] : 0); break;
   case FTN_INT     : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_int      *)fmem)[i] : 0); break;
   case FTN_INTEGER : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_integer  *)fmem)[i] : 0); break;
   case FTN_LONG    : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_long     *)fmem)[i] : 0); break;
   case FTN_SHORT   : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_short    *)fmem)[i] : 0); break;
   case FTN_SMALLINT: for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_smallint *)fmem)[i] : 0); break;
   case FTN_WORD    : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_word     *)fmem)[i] : 0); break;
   case FTN_INT64   : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_int64    *)fmem)[i] : 0); break;
   case FTN_UINT64  : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_uint64   *)fmem)[i] : 0); break;
   case FTN_HANDLE  : for(;i<n;i++) mem[i]=(protype)(fmem ? ((ft_handle   *)fmem)[i] : 0); break;
   case FTN_BLOB    :
   default          : free(mem); rc = FOP_EINVAL; goto done;
   }
   f2->type&=~DDTYPEBITS;
   f2->type|=probit;
   f2->elsz=sizeof(protype);
   setfldandsize(f2, mem, alloced+1, FLD_FORCE_NORMAL);
ok:
   rc = 0;                                      /* success */

done:
   if (TXfldmathverb >= 3)
     TXfldresultmsg("promote/demote", "", f2, rc, 1);
   return(rc);
}                                                    /* end promote() */
/**********************************************************************/

