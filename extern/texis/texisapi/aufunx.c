#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include "dbquery.h"

#include "texint.h"   /* must define FLD, FLDFUNC, FLDOP, getfld(), putfld(), foaddfuncs(), etc. */
#include "fld.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define FTN_IS_VEC(v) ({\
    int __vtype=(v);\
    __vtype=(__vtype>=FTN_VEC_START && __vtype<=FTN_VEC_END);\
    __vtype;\
})

#define FTN_IS_VEC_OR_BYTE(v) ({\
    int __vtype=(v);\
    __vtype= (__vtype== FTN_BYTE || (__vtype>=FTN_VEC_START && __vtype<=FTN_VEC_END));\
    __vtype;\
})


// calculate distance using simsimd - in vector-distance.c
double rp_vector_distance(void *a, void *b, size_t bytesize, const char *metric, const char *datatype, const char **err);

/* vecdist(VEC_xx veca, VEC_xx vecb, CHAR metric, CHAR datatype) -> DOUBLE in f[0]
      or
   vecdist(BYTE veca, BYTE vecb, CHAR metric, CHAR datatype) -> DOUBLE in f[0]
      or a combo of BYTE and VEC_XX
*/
static int vecdist(FLD *f1, FLD *f2, FLD *f3, FLD *f4) // todo: add scale and zp for i8 and u8 if we ever do conversions
{
    /* read args as varbyte */
    size_t len0 = 0, len1 = 0, v_elsz=1;
    const char *metric="dot", *dtype="f16";
    int t1, t2, havetype=0;

    if(!f1)
    {
      putmsg(MERR + UGE, "vecdist", "Null field in arg 1");
      return(FOP_EINVAL);
    }
    if(!f2)
    {
      putmsg(MERR + UGE, "vecdist", "Null field in arg 2");
      return(FOP_EINVAL);
    }

    // compare byte size. must be equal.
    if(f1->size != f2->size)
    {
          putmsg(MERR + UGE, "vecdist", "vector fields must be the same size");
          return(FOP_EINVAL);
    }

    if(f3)
    {
        if((f3->type&DDTYPEBITS) != FTN_CHAR)
        {
            putmsg(MERR + UGE, "vecdist", "wrong type in field 3");
            return(FOP_EINVAL);
        }
        metric = getfld(f3, NULL);
    }

    t1 = f1->type&DDTYPEBITS;
    t2 = f1->type&DDTYPEBITS;

    if( ! FTN_IS_VEC_OR_BYTE(t1) && ! FTN_IS_VEC_OR_BYTE(t2) )
    {
        putmsg(MERR + UGE, "vecdist", "one or both of field 1 and field 2 are not byte or vector types");
        return(FOP_EINVAL);
    }

    //both are untyped
    if (t1==FTN_BYTE && t2==FTN_BYTE)
    {
        // we only care about f4 (type) if both are untyped, otherwise ignore it.
        if(f4)
        {
            if((f4->type&DDTYPEBITS) != FTN_CHAR)
            {
                putmsg(MERR + UGE, "vecdist", "wrong type in field 4");
                return(FOP_EINVAL);
            }
            dtype = getfld(f4, NULL);
        }
        //else use default dtype set above
    }
    // if both are typed
    else if( FTN_IS_VEC(t1) && FTN_IS_VEC(t2))
    {
        if(t1 != t2 ) // unlikely event of being the same byte-length but different types.
        {
            putmsg(MERR + UGE, "vecdist", "vector types from field 1 and 2 do not match");
            return(FOP_EINVAL);
        }
        // they are the same
        havetype=t1;
    }
    // one is a byte, assume its the same type as the other typed vec
    else if(t1 == FTN_BYTE)
        havetype=t2;
    else if(t2 == FTN_BYTE)
        havetype=t1;

    switch(havetype)
    {
        case 0:                   // both are BYTE, set from f4 or default
        case FTN_VEC_F16:  break; //default
        case FTN_VEC_F64:  dtype="f64";  break;
        case FTN_VEC_F32:  dtype="f32";  break;
        case FTN_VEC_BF16: dtype="bf16"; break;
        case FTN_VEC_I8:   dtype="i8";   break;
        case FTN_VEC_U8:   dtype="u8";   break;
    }

    // get actual buffers and do distance function
    void *a = getfld(f1, &len0);
    void *b = getfld(f2, &len1);

    ft_double *dist = malloc(sizeof(ft_double));
    const char *err=NULL;

    *dist = rp_vector_distance(a, b, f1->size, metric, dtype, &err);

    if(err)
    {
      free(dist);
      putmsg(MERR + UGE, "vecdist", err);
      return(FOP_EINVAL);
    }

    setfld(f1, dist, sizeof(ft_double));

    f1->elsz=sizeof(ft_double);
    f1->size=sizeof(ft_double);
    f1->type = FTN_DOUBLE;
    f1->n=1;
    return 0;
}


/* ------------------------ Registration --------------------------- */

#define F(f)    ((int (*)(void))(f))
#define NUSERFUNC 1
static FLDFUNC g_vec_funcs[NUSERFUNC] = {
    /* name,        func,         minargs, maxargs, rettype   input type     */
    { "vecdist",    F(vecdist),    2,       4,     FTN_DOUBLE, 0}
};
#undef F

void adduserfuncs(FLDOP *fo)
{
    /* Register our function(s) */
    (void)foaddfuncs(fo, g_vec_funcs, NUSERFUNC);
}
