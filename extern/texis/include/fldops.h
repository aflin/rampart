#ifndef FLDOPS_H
#define FLDOPS_H


/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
#define FLDSWP(a,b) {FLD t; t = *(a);*(a)= *(b);*(b)=t;}
#ifdef JMT
#define FLDSWPV(a,b)	fldswpv((a), (b))
#else
#define FLDSWPV(a,b) { \
   void *v=(a)->v;\
   size_t n=(a)->n;\
   size_t s=(a)->size;\
   (a)->v=(b)->v;\
   (a)->n=(b)->n;\
   (a)->size=(b)->size;\
   (b)->v=v;\
   (b)->n=n;\
   (b)->size=s;\
}
#endif
extern void fldswp ARGS((FLD *f1,FLD *f2));
extern void fldswpv ARGS((FLD *f1,FLD *f2));
                     /* handy function for storing compare op results */
extern int fld2finv ARGS((FLD *f,ft_int  val));/* set fld to int  type set to val */
extern int fld2flov ARGS((FLD *f,ft_long val));/* set fld to long type set to val */
/**********************************************************************/
/* naming convention for default field ops
** 6 chars: "fo" prefix and 2 for each of the types it handles in argument order
** 1st and 2nd char: first 2 bytes of type name
**  exception: INTEGER is abbreviated IR instead of IN
** example: first arg is int, second is byte
**    foinby(...);
**
** arguments: 2 fields and an op
**    place results of op directly in first field
**    both field arguments may be modified (except when returning FOP_EINVAL)
**    do not close either field argument
**    arg2 will be discarded immediately without being used again
**    see FOP_??? in fldmath.h for op types
**    on FOP_EINVAL don't modify either field
**
** return values:
**     <0 is error (see FOP_E* in fldmath.h)
**    ==0 is ok
*/

/* I(type1, type2, func)
 * Each fo....() function should appear once in this list, with its
 * native types.  Functions used for multiple types (e.g. FTN_INTEGER
 * and FTN_INT are the same underlying C type) are manually added
 * for those additional types in foopen().  This list is used to
 * generate prototypes, populate a function-to-string-name array,
 * and add type/type/function tuples to a FLDOP in foopen().
 * KNG 20140702 separate FTN_SMALLINT functions added, even though
 * same C type as FTN_SHORT: prevents certain unexpected type changes
 * during fldmath, e.g. `ifnull(convert(NULL, 'smallint'), 2)'.
 * May be true of other types as well.
 * NOTE: if more FTN_COUNTERI functions added, see retoptype().
 * Note: list is sorted ascending by function name.
 */
#define TX_FLDOP_SYMBOLS_LIST                   \
  I(FTN_BLOBI,          FTN_BLOBI,      fobibi) \
  I(FTN_BLOBI,          FTN_BYTE,       fobiby) \
  I(FTN_BLOBI,          FTN_CHAR,       fobich) \
  I(FTN_BYTE,           FTN_BLOBI,      fobybi) \
  I(FTN_BYTE,           FTN_BYTE,       fobyby) \
/* -ajf 2025-11-19 */\
  I(FTN_VEC_F64,        FTN_VEC_F64,    fobyby) \
  I(FTN_VEC_F32,        FTN_VEC_F32,    fobyby) \
  I(FTN_VEC_F16,        FTN_VEC_F16,    fobyby) \
  I(FTN_VEC_BF16,       FTN_VEC_BF16,   fobyby) \
  I(FTN_VEC_I8,         FTN_VEC_I8,     fobyby) \
  I(FTN_VEC_U8,         FTN_VEC_U8,     fobyby) \
  I(FTN_BYTE,           FTN_VEC_F64,    fobyby) \
  I(FTN_BYTE,           FTN_VEC_F32,    fobyby) \
  I(FTN_BYTE,           FTN_VEC_F16,    fobyby) \
  I(FTN_BYTE,           FTN_VEC_BF16,   fobyby) \
  I(FTN_BYTE,           FTN_VEC_I8,     fobyby) \
  I(FTN_BYTE,           FTN_VEC_U8,     fobyby) \
  I(FTN_VEC_F64,        FTN_BYTE,       fobyby) \
  I(FTN_VEC_F32,        FTN_BYTE,       fobyby) \
  I(FTN_VEC_F16,        FTN_BYTE,       fobyby) \
  I(FTN_VEC_BF16,       FTN_BYTE,       fobyby) \
  I(FTN_VEC_I8,         FTN_BYTE,       fobyby) \
  I(FTN_VEC_U8,         FTN_BYTE,       fobyby) \
  I(FTN_BYTE,           FTN_CHAR,       fobych) \
  I(FTN_BYTE,           FTN_COUNTERI,   fobyci) \
  I(FTN_BYTE,           FTN_COUNTER,    fobyco) \
  I(FTN_BYTE,           FTN_HANDLE,     fobyha) \
  I(FTN_BYTE,           FTN_INT64,      fobyi6) \
  I(FTN_BYTE,           FTN_INT,        fobyin) \
  I(FTN_BYTE,           FTN_LONG,       fobylo) \
  I(FTN_BYTE,           FTN_UINT64,     fobyu6) \
  I(FTN_CHAR,           FTN_BLOBI,      fochbi) \
  I(FTN_CHAR,           FTN_BYTE,       fochby) \
  I(FTN_CHAR,           FTN_CHAR,       fochch) \
  I(FTN_CHAR,           FTN_COUNTERI,   fochci) \
  I(FTN_CHAR,           FTN_COUNTER,    fochco) \
  I(FTN_CHAR,           FTN_DATE,       fochda) \
  I(FTN_CHAR,           FTN_DOUBLE,     fochdo) \
  I(FTN_CHAR,           FTN_DATETIME,   fochdt) \
  I(FTN_CHAR,           FTN_DWORD,      fochdw) \
  I(FTN_CHAR,           FTN_FLOAT,      fochfl) \
  I(FTN_CHAR,           FTN_HANDLE,     fochha) \
  I(FTN_CHAR,           FTN_INT64,      fochi6) \
  I(FTN_CHAR,           FTN_INDIRECT,   fochid) \
  I(FTN_CHAR,           FTN_INTERNAL,   fochil) \
  I(FTN_CHAR,           FTN_INT,        fochin) \
  I(FTN_CHAR,           FTN_LONG,       fochlo) \
  I(FTN_CHAR,           FTN_RECID,      fochre) \
  I(FTN_CHAR,           FTN_SHORT,      fochsh) \
  I(FTN_CHAR,           FTN_STRLST,     fochsl) \
  I(FTN_CHAR,           FTN_SMALLINT,   fochsm) \
  I(FTN_CHAR,           FTN_UINT64,     fochu6) \
  I(FTN_CHAR,           FTN_WORD,       fochwo) \
  I(FTN_COUNTERI,       FTN_BYTE,       fociby) \
  I(FTN_COUNTERI,       FTN_CHAR,       focich) \
  I(FTN_COUNTERI,       FTN_COUNTER,    focico) \
  I(FTN_COUNTERI,       FTN_DATE,       focida) \
  I(FTN_COUNTERI,       FTN_STRLST,     focisl) \
  I(FTN_COUNTER,        FTN_BYTE,       focoby) \
  I(FTN_COUNTER,        FTN_CHAR,       fococh) \
  I(FTN_COUNTER,        FTN_COUNTERI,   fococi) \
  I(FTN_COUNTER,        FTN_COUNTER,    fococo) \
  I(FTN_COUNTER,        FTN_DATE,       focoda) \
  I(FTN_COUNTER,        FTN_STRLST,     focosl) \
  I(FTN_DATE,           FTN_CHAR,       fodach) \
  I(FTN_DATE,           FTN_COUNTERI,   fodaci) \
  I(FTN_DATE,           FTN_COUNTER,    fodaco) \
  I(FTN_DATE,           FTN_DATE,       fodada) \
  I(FTN_DATE,           FTN_DOUBLE,     fodado) \
  I(FTN_DATE,           FTN_DATETIME,   fodadt) \
  I(FTN_DATE,           FTN_FLOAT,      fodafl) \
  I(FTN_DATE,           FTN_HANDLE,     fodaha) \
  I(FTN_DATE,           FTN_INT64,      fodai6) \
  I(FTN_DATE,           FTN_INT,        fodain) \
  I(FTN_DATE,           FTN_LONG,       fodalo) \
  I(FTN_DATE,           FTN_UINT64,     fodau6) \
  I(FTN_DOUBLE,         FTN_CHAR,       fodoch) \
  I(FTN_DOUBLE,         FTN_DATE,       fododa) \
  I(FTN_DOUBLE,         FTN_DOUBLE,     fododo) \
  I(FTN_DOUBLE,         FTN_DWORD,      fododw) \
  I(FTN_DOUBLE,         FTN_FLOAT,      fodofl) \
  I(FTN_DOUBLE,         FTN_HANDLE,     fodoha) \
  I(FTN_DOUBLE,         FTN_INT64,      fodoi6) \
  I(FTN_DOUBLE,         FTN_INT,        fodoin) \
  I(FTN_DOUBLE,         FTN_LONG,       fodolo) \
  I(FTN_DOUBLE,         FTN_SHORT,      fodosh) \
  I(FTN_DOUBLE,         FTN_STRLST,     fodosl) \
  I(FTN_DOUBLE,         FTN_SMALLINT,   fodosm) \
  I(FTN_DOUBLE,         FTN_UINT64,     fodou6) \
  I(FTN_DOUBLE,         FTN_WORD,       fodowo) \
  I(FTN_DATETIME,       FTN_CHAR,       fodtch) \
  I(FTN_DATETIME,       FTN_DATE,       fodtda) \
  I(FTN_DATETIME,       FTN_DATETIME,   fodtdt) \
  I(FTN_DWORD,          FTN_BYTE,       fodwby) \
  I(FTN_DWORD,          FTN_CHAR,       fodwch) \
  I(FTN_DWORD,          FTN_DOUBLE,     fodwdo) \
  I(FTN_DWORD,          FTN_DWORD,      fodwdw) \
  I(FTN_DWORD,          FTN_FLOAT,      fodwfl) \
  I(FTN_DWORD,          FTN_HANDLE,     fodwha) \
  I(FTN_DWORD,          FTN_INT64,      fodwi6) \
  I(FTN_DWORD,          FTN_INT,        fodwin) \
  I(FTN_DWORD,          FTN_LONG,       fodwlo) \
  I(FTN_DWORD,          FTN_SHORT,      fodwsh) \
  I(FTN_DWORD,          FTN_SMALLINT,   fodwsm) \
  I(FTN_DWORD,          FTN_UINT64,     fodwu6) \
  I(FTN_DWORD,          FTN_WORD,       fodwwo) \
  I(FTN_FLOAT,          FTN_CHAR,       foflch) \
  I(FTN_FLOAT,          FTN_DATE,       foflda) \
  I(FTN_FLOAT,          FTN_DOUBLE,     fofldo) \
  I(FTN_FLOAT,          FTN_DWORD,      fofldw) \
  I(FTN_FLOAT,          FTN_FLOAT,      foflfl) \
  I(FTN_FLOAT,          FTN_HANDLE,     foflha) \
  I(FTN_FLOAT,          FTN_INT64,      fofli6) \
  I(FTN_FLOAT,          FTN_INT,        foflin) \
  I(FTN_FLOAT,          FTN_LONG,       fofllo) \
  I(FTN_FLOAT,          FTN_SHORT,      foflsh) \
  I(FTN_FLOAT,          FTN_SMALLINT,   foflsm) \
  I(FTN_FLOAT,          FTN_UINT64,     foflu6) \
  I(FTN_FLOAT,          FTN_WORD,       foflwo) \
  I(FTN_FLOAT,          FTN_STRLST,     fofosl) \
  I(FTN_HANDLE,         FTN_BYTE,       fohaby) \
  I(FTN_HANDLE,         FTN_CHAR,       fohach) \
  I(FTN_HANDLE,         FTN_DATE,       fohada) \
  I(FTN_HANDLE,         FTN_DOUBLE,     fohado) \
  I(FTN_HANDLE,         FTN_DWORD,      fohadw) \
  I(FTN_HANDLE,         FTN_FLOAT,      fohafl) \
  I(FTN_HANDLE,         FTN_HANDLE,     fohaha) \
  I(FTN_HANDLE,         FTN_INT64,      fohai6) \
  I(FTN_HANDLE,         FTN_INT,        fohain) \
  I(FTN_HANDLE,         FTN_LONG,       fohalo) \
  I(FTN_HANDLE,         FTN_SHORT,      fohash) \
  I(FTN_HANDLE,         FTN_STRLST,     fohasl) \
  I(FTN_HANDLE,         FTN_SMALLINT,   fohasm) \
  I(FTN_HANDLE,         FTN_UINT64,     fohau6) \
  I(FTN_HANDLE,         FTN_WORD,       fohawo) \
  I(FTN_INT64,          FTN_BYTE,       foi6by) \
  I(FTN_INT64,          FTN_CHAR,       foi6ch) \
  I(FTN_INT64,          FTN_DATE,       foi6da) \
  I(FTN_INT64,          FTN_DOUBLE,     foi6do) \
  I(FTN_INT64,          FTN_DWORD,      foi6dw) \
  I(FTN_INT64,          FTN_FLOAT,      foi6fl) \
  I(FTN_INT64,          FTN_INT64,      foi6i6) \
  I(FTN_INT64,          FTN_INT,        foi6in) \
  I(FTN_INT64,          FTN_LONG,       foi6lo) \
  I(FTN_INT64,          FTN_SHORT,      foi6sh) \
  I(FTN_INT64,          FTN_STRLST,     foi6sl) \
  I(FTN_INT64,          FTN_SMALLINT,   foi6sm) \
  I(FTN_INT64,          FTN_UINT64,     foi6u6) \
  I(FTN_INT64,          FTN_WORD,       foi6wo) \
  I(FTN_INDIRECT,       FTN_CHAR,       foidch) \
  I(FTN_INTERNAL,       FTN_CHAR,       foilch) \
  I(FTN_INTERNAL,       FTN_INTERNAL,   foilil) \
  I(FTN_INT,            FTN_BYTE,       foinby) \
  I(FTN_INT,            FTN_CHAR,       foinch) \
  I(FTN_INT,            FTN_DATE,       foinda) \
  I(FTN_INT,            FTN_DOUBLE,     foindo) \
  I(FTN_INT,            FTN_DWORD,      foindw) \
  I(FTN_INT,            FTN_FLOAT,      foinfl) \
  I(FTN_INT,            FTN_HANDLE,     foinha) \
  I(FTN_INT,            FTN_INT64,      foini6) \
  I(FTN_INT,            FTN_INT,        foinin) \
  I(FTN_INT,            FTN_LONG,       foinlo) \
  I(FTN_INT,            FTN_SHORT,      foinsh) \
  I(FTN_INT,            FTN_STRLST,     foinsl) \
  I(FTN_INT,            FTN_SMALLINT,   foinsm) \
  I(FTN_INT,            FTN_UINT64,     foinu6) \
  I(FTN_INT,            FTN_WORD,       foinwo) \
  I(FTN_INTEGER,        FTN_INTEGER,    foirir) \
  I(FTN_INTEGER,        FTN_LONG,       foirlo) \
  I(FTN_LONG,           FTN_BYTE,       foloby) \
  I(FTN_LONG,           FTN_CHAR,       foloch) \
  I(FTN_LONG,           FTN_DATE,       foloda) \
  I(FTN_LONG,           FTN_DOUBLE,     folodo) \
  I(FTN_LONG,           FTN_DWORD,      folodw) \
  I(FTN_LONG,           FTN_FLOAT,      folofl) \
  I(FTN_LONG,           FTN_HANDLE,     foloha) \
  I(FTN_LONG,           FTN_INT64,      foloi6) \
  I(FTN_LONG,           FTN_INT,        foloin) \
  I(FTN_LONG,           FTN_INTEGER,    foloir) \
  I(FTN_LONG,           FTN_LONG,       fololo) \
  I(FTN_LONG,           FTN_SHORT,      folosh) \
  I(FTN_LONG,           FTN_STRLST,     folosl) \
  I(FTN_LONG,           FTN_SMALLINT,   folosm) \
  I(FTN_LONG,           FTN_UINT64,     folou6) \
  I(FTN_LONG,           FTN_WORD,       folowo) \
  I(FTN_RECID,          FTN_CHAR,       forech) \
  I(FTN_SHORT,          FTN_CHAR,       foshch) \
  I(FTN_SHORT,          FTN_DOUBLE,     foshdo) \
  I(FTN_SHORT,          FTN_DWORD,      foshdw) \
  I(FTN_SHORT,          FTN_FLOAT,      foshfl) \
  I(FTN_SHORT,          FTN_HANDLE,     foshha) \
  I(FTN_SHORT,          FTN_INT64,      foshi6) \
  I(FTN_SHORT,          FTN_INT,        foshin) \
  I(FTN_SHORT,          FTN_LONG,       foshlo) \
  I(FTN_SHORT,          FTN_SHORT,      foshsh) \
  I(FTN_SHORT,          FTN_UINT64,     foshu6) \
  I(FTN_STRLST,         FTN_CHAR,       foslch) \
  I(FTN_STRLST,         FTN_COUNTERI,   foslci) \
  I(FTN_STRLST,         FTN_COUNTER,    foslco) \
  I(FTN_STRLST,         FTN_DOUBLE,     fosldo) \
  I(FTN_STRLST,         FTN_FLOAT,      foslfo) \
  I(FTN_STRLST,         FTN_HANDLE,     foslha) \
  I(FTN_STRLST,         FTN_INT64,      fosli6) \
  I(FTN_STRLST,         FTN_INTERNAL,   foslil) \
  I(FTN_STRLST,         FTN_INT,        foslin) \
  I(FTN_STRLST,         FTN_LONG,       fosllo) \
  I(FTN_STRLST,         FTN_STRLST,     foslsl) \
  I(FTN_STRLST,         FTN_UINT64,     foslu6) \
  I(FTN_SMALLINT,       FTN_CHAR,       fosmch) \
  I(FTN_SMALLINT,       FTN_DOUBLE,     fosmdo) \
  I(FTN_SMALLINT,       FTN_DWORD,      fosmdw) \
  I(FTN_SMALLINT,       FTN_FLOAT,      fosmfl) \
  I(FTN_SMALLINT,       FTN_HANDLE,     fosmha) \
  I(FTN_SMALLINT,       FTN_INT64,      fosmi6) \
  I(FTN_SMALLINT,       FTN_INT,        fosmin) \
  I(FTN_SMALLINT,       FTN_LONG,       fosmlo) \
  I(FTN_SMALLINT,       FTN_SMALLINT,   fosmsm) \
  I(FTN_SMALLINT,       FTN_UINT64,     fosmu6) \
  I(FTN_UINT64,         FTN_BYTE,       fou6by) \
  I(FTN_UINT64,         FTN_CHAR,       fou6ch) \
  I(FTN_UINT64,         FTN_DATE,       fou6da) \
  I(FTN_UINT64,         FTN_DOUBLE,     fou6do) \
  I(FTN_UINT64,         FTN_DWORD,      fou6dw) \
  I(FTN_UINT64,         FTN_FLOAT,      fou6fl) \
  I(FTN_UINT64,         FTN_HANDLE,     fou6ha) \
  I(FTN_UINT64,         FTN_INT64,      fou6i6) \
  I(FTN_UINT64,         FTN_INT,        fou6in) \
  I(FTN_UINT64,         FTN_LONG,       fou6lo) \
  I(FTN_UINT64,         FTN_SHORT,      fou6sh) \
  I(FTN_UINT64,         FTN_STRLST,     fou6sl) \
  I(FTN_UINT64,         FTN_SMALLINT,   fou6sm) \
  I(FTN_UINT64,         FTN_UINT64,     fou6u6) \
  I(FTN_UINT64,         FTN_WORD,       fou6wo) \
  I(FTN_WORD,           FTN_CHAR,       fowoch) \
  I(FTN_WORD,           FTN_DOUBLE,     fowodo) \
  I(FTN_WORD,           FTN_DWORD,      fowodw) \
  I(FTN_WORD,           FTN_FLOAT,      fowofl) \
  I(FTN_WORD,           FTN_HANDLE,     fowoha) \
  I(FTN_WORD,           FTN_INT64,      fowoi6) \
  I(FTN_WORD,           FTN_INT,        fowoin) \
  I(FTN_WORD,           FTN_LONG,       fowolo) \
  I(FTN_WORD,           FTN_UINT64,     fowou6) \
  I(FTN_WORD,           FTN_WORD,       fowowo)

/* Extra field op functions added/replaced by dbgetfo(): */
#define TX_TEXIS_EXTRA_FLDOP_SYMBOLS_LIST               \
  I(FTN_BLOBI,          FTN_CHAR,       n_fblch)        \
  I(FTN_BYTE,           FTN_CHAR,       n_fbych)        \
  I(FTN_CHAR,           FTN_CHAR,       n_fchch)        \
  I(FTN_INDIRECT,       FTN_CHAR,       n_fidch)

#undef I
#define I(type1, type2, func)   \
  extern int func(FLD *f1, FLD *f2, FLD *f3, int op);
TX_FLDOP_SYMBOLS_LIST
TX_TEXIS_EXTRA_FLDOP_SYMBOLS_LIST
#undef I

extern int TXdayname ARGS((FLD *f1));
extern int TXmonth ARGS((FLD *f1));
extern int TXmonthname ARGS((FLD *f1));
extern int TXdayofmonth ARGS((FLD *f1));
extern int TXdayofweek ARGS((FLD *f1));
extern int TXdayofyear ARGS((FLD *f1));
extern int TXquarter ARGS((FLD *f1));
extern int TXweek ARGS((FLD *f1));
extern int TXyear ARGS((FLD *f1));
extern int TXhour ARGS((FLD *f1));
extern int TXminute ARGS((FLD *f1));
extern int TXsecond ARGS((FLD *f1));
extern int TXmonthseq ARGS((FLD *f1));
extern int TXdayseq ARGS((FLD *f1));
extern int TXweekseq ARGS((FLD *f1));
int TXnow(FLD *f);

/* ISEQ_DT(): true if datetime `a' == `b': */
#define ISEQ_DT(a, b)                   \
  ((a)->year == (b)->year &&            \
  (a)->month == (b)->month &&           \
  (a)->day == (b)->day &&               \
  (a)->hour == (b)->hour &&             \
  (a)->minute == (b)->minute &&         \
  (a)->second == (b)->second &&         \
  (a)->fraction == (b)->fraction)
#define ISLTGT_DT(a, b, op)                     \
  ((a)->year op (b)->year ||                    \
   ((a)->year == (b)->year &&                   \
    ((a)->month op (b)->month ||                \
     ((a)->month == (b)->month &&               \
      ((a)->day op (b)->day ||                  \
       ((a)->day == (b)->day &&                 \
        ((a)->hour op (b)->hour ||              \
         ((a)->hour == (b)->hour &&             \
          ((a)->minute op (b)->minute ||        \
           ((a)->minute == (b)->minute &&       \
            ((a)->second op (b)->second ||      \
             ((a)->second == (b)->second &&     \
              (a)->fraction op (b)->fraction))))))))))))
/* ISLT_DT(): true if datetime `a' < `b': */
#define ISLT_DT(a, b)   ISLTGT_DT(a, b, <)
/* ISGT_DT(): true if datetime `a' > `b': */
#define ISGT_DT(a, b)   ISLTGT_DT(a, b, >)

/**********************************************************************/
#endif                                                    /* FLDOPS_H */
