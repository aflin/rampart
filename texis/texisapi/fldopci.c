/*
 * template: counteri <-> other ops
 * Only use where other type also has foxxco function
 * Macros defined by #includer:
 *   ft_xx      Other ft_... type
 *   xx         Two-letter function abbreviation for other type
 */

/* This file included multiple times, so put a one-time-ifdef around consts:*/
#ifndef TX_FLDOPCI_ONE_TIME
#  define TX_FLDOPCI_ONE_TIME
static CONST char       TXnoCounterDbNotOpen[] =
  "Cannot generate counter: Database not open";
#endif /* !TX_FLDOPCI_ONE_TIME */

/* We want to concatenate the expanded value of macro `xx' with some
 * identifiers to make a new identifier.  The ## preprocessor operator
 * can concatenate identifiers.  But it only works inside a macro
 * body, so we define a macro (catenate{2|3}) that uses ##.  But ##
 * also does not macro-expand either identifier, so we make *another*
 * macro to do that, which takes an arg (to expand) and then passes
 * its (expanded) arg to the concatenation macro.  Complicated, but
 * saves defining and undefining 3 or 4 more macros every time we
 * #include this file:
 */
#undef stringize
#undef catenate2
#undef catenate3
#undef focixx
#undef foxxci
#undef foxxciStr
#undef foxxco

#define stringize(a)            #a
#define catenate2(a, b)         a##b
#define catenate3(a, b, c)      a##b##c
#define focixx(x)               catenate2(foci, x)
#define foxxci(x)               catenate3(fo, x, ci)
#define foxxciStr(x)            "fo" stringize(x) "ci"
#define foxxco(x)               catenate3(fo, x, co)

/* ------------------------------------------------------------------------ */

int
focixx(xx)(f1, f2, f3, op)
FLD     *f1;    /* (in) left-side ft_counteri FLD */
FLD     *f2;    /* (in) right-side ft_xx type */
FLD     *f3;    /* (out) result FLD */
int     op;     /* (in) operation to perform */
{
  switch(op)
    {
    case FOP_CNV:
      return(foxxci(xx)(f2, f1, f3, FOP_ASN));
    default:
      return(FOP_EINVAL);
    }
}

/* ------------------------------------------------------------------------ */

int
foxxci(xx)(f1, f2, f3, op)
FLD     *f1;    /* (in) left-side ft_xx FLD */
FLD     *f2;    /* (in) right-side ft_counteri type */
FLD     *f3;    /* (out) result FLD */
int     op;     /* (in) operation to perform */
{
  static CONST char     fn[] = foxxciStr(xx);
  ft_counter            *newCtr = NULL;
  int                   ret = FOP_EUNKNOWN;
  FLD                   ctrFld;

  switch(op)
    {
      case FOP_CNV:
        ret = focixx(xx)(f2, f1, f3, FOP_ASN);
        break;
      case FOP_ASN:
        /* `f2' counteri has no real value, so first make a counter as
         * its effective value:
         */
        TXgetstddic();
        if (!ddic)
          {
            putmsg(MERR, fn, TXnoCounterDbNotOpen);
            goto err;
          }
        newCtr = getcounter(ddic);
        if (!newCtr) goto err;
        /* Now put the counter in a temp counter field `ctrFld': */
        memset(&ctrFld, 0, sizeof(FLD));        /* wtf drill for speed */
        ctrFld.type = FTN_COUNTER;
        ctrFld.size = ctrFld.elsz = ctrFld.alloced = sizeof(ft_counter);
        ctrFld.n = 1;
        ctrFld.v = newCtr;
        /* Then let the xx <-> counter function do the actual assignment: */
        ret = foxxco(xx)(f1, &ctrFld, f3, op);
        break;
      default:
        ret = FOP_EINVAL;
        goto done;
    }
  goto done;

err:
  ret = FOP_EUNKNOWN;
done:
  if (newCtr) newCtr = TXfree(newCtr);
  return(ret);
}

/* undefine macros for safety next time; this file included multiple times: */
#undef stringize
#undef catenate2
#undef catenate3
#undef focixx
#undef foxxci
#undef foxxciStr
#undef foxxco

#undef ft_xx
#undef xx
