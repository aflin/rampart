/* -=- kai-mode: john -=- */

/******************************************************************/

int
fodada(f1,f2,f3,op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
ft_date *vp1, *vp2, *vp3;
size_t n1, n2, n3;
int var1, var2;
int rc=0;

   TXmakesimfield(f1, f3);
   vp1=(ft_date *)getfld(f1,&n1);
   vp2=(ft_date *)getfld(f2,&n2);
   vp3=(ft_date *)getfld(f3,&n3);
   var1=fldisvar(f1);
   var2=fldisvar(f2);
   if(n1>1 || var1){
      switch(op){
#ifdef NEVER
      case FOP_ADD: rc=varcat(f1,f2); break;/* wtf - trim if fixed?? */
      case FOP_CNV: if(var2) f1->type|=DDVARBIT;
                    else     f1->type&=~DDVARBIT;/* wtf - copy n member?? */
                    break;
      case FOP_ASN: if(var1){
                       FLDSWPV(f1,f2);
                    }else{
                       memcpy(vp3,vp2,MIN(n1,n2)*sizeof(ft_date));
                    }
                    break;
#endif
      default     : rc=FOP_EINVAL;
      }
   }else{
      switch(op){
      case FOP_ADD: *vp3 = *vp1 + *vp2; break;
#ifdef NEVER
      case FOP_SUB: *vp3 = *vp1 - *vp2; f3->type=FTN_INT; break;
#else
      case FOP_SUB: rc=fld2finv(f3,*vp1-*vp2); break;
#endif
      case FOP_MUL: *vp3 = *vp1 * *vp2; break;
      case FOP_DIV:
	      if (*vp2 == (ft_date)0)
	      {
		      TXfldSetNull(f3);		/* Bug 6914 */
		      rc = FOP_EDOMAIN;
	      }
	      else
		      *vp3 = *vp1 / *vp2;
	      break;
#     ifndef fonomodulo
      case FOP_MOD:
	      if (*vp2 == (ft_date)0)
	      {
		      TXfldSetNull(f3);		/* Bug 6914 */
		      rc = FOP_EDOMAIN;
	      }
	      else
		      *vp3 = *vp1 % *vp2;
	      break;
#     endif
      case FOP_CNV: if(var2) f3->type|=DDVARBIT;
                    else     f3->type&=~DDVARBIT;
		    *vp3=*vp1;
                    break;
      case FOP_ASN: *vp3= *vp2; break;
      case FOP_EQ : rc=fld2finv(f3,*vp1==*vp2); break;
      case FOP_NEQ: rc=fld2finv(f3,*vp1!=*vp2); break;
      case FOP_LT : rc=fld2finv(f3,*vp1< *vp2); break;
      case FOP_LTE: rc=fld2finv(f3,*vp1<=*vp2); break;
      case FOP_GT : rc=fld2finv(f3,*vp1> *vp2); break;
      case FOP_GTE: rc=fld2finv(f3,*vp1>=*vp2); break;
      case FOP_COM: if(*vp1 > *vp2)
			rc = 1;
		    else if (*vp1 < *vp2)
			rc = -1;
		    else
		    	rc = 0;
                    rc=fld2finv(f3,rc); break;
      case FOP_IN :
      case FOP_IS_SUBSET:
      case FOP_INTERSECT_IS_EMPTY :
      case FOP_INTERSECT_IS_NOT_EMPTY :
        /* WTF support multi/var `f1' properly; for now `f1' is
         * single-item fixed (checked above), thus intersect and
         * subset behave same:
         */
      {
	size_t i;

	for(i=0; i < n2; i++)
	{
		if(*vp1 == vp2[i])
		{
			rc = fld2finv(f3, (op != FOP_INTERSECT_IS_EMPTY));
			return rc;
		}
	}
	rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
	break;
      }
      case FOP_INTERSECT:
        /* INTERSECT returns a set not a boolean; wtf support: */
        rc = FOP_EILLEGAL;
        break;
      default     : rc=FOP_EINVAL;
      }
   }
   return(rc);
}                                                     /* end fodada() */

/******************************************************************/

int
fodtdt(f1,f2,f3,op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
ft_datetime *vp1, *vp2, *vp3;
size_t n1, n2, n3;
int var1, var2;
int rc=0;
#define ISNULL(f)       ((f)->v == NULL || (f)->n <= (size_t)0)

   TXmakesimfield(f1, f3);
   vp1=(ft_datetime *)getfld(f1,&n1);
   vp2=(ft_datetime *)getfld(f2,&n2);
   vp3=(ft_datetime *)getfld(f3,&n3);
   var1=fldisvar(f1);
   var2=fldisvar(f2);
   if(n1>1 || var1){
      switch(op){
      default     : rc=FOP_EINVAL;
      }
   }else{
      switch(op){
      case FOP_CNV: if(var2) f3->type|=DDVARBIT;
                    else     f3->type&=~DDVARBIT;
		    *vp3=*vp1;
                    break;
      case FOP_ASN: *vp3= *vp2; break;
      case FOP_EQ :
        rc=fld2finv(f3,(!ISNULL(f1) && !ISNULL(f2) && ISEQ_DT(vp1, vp2)));
        break;
      case FOP_NEQ:
        rc=fld2finv(f3,(!ISNULL(f1) && !ISNULL(f2) && !ISEQ_DT(vp1, vp2)));
        break;
      case FOP_LT :
        rc=fld2finv(f3,(!ISNULL(f1) && !ISNULL(f2) && ISLT_DT(vp1, vp2)));
        break;
      case FOP_LTE:
        rc=fld2finv(f3,(!ISNULL(f1) && !ISNULL(f2) &&
                        (ISLT_DT(vp1, vp2) || ISEQ_DT(vp2, vp2))));
        break;
      case FOP_GT :
        rc=fld2finv(f3,(!ISNULL(f1) && !ISNULL(f2) && ISGT_DT(vp1, vp2)));
        break;
      case FOP_GTE:
        rc=fld2finv(f3,(!ISNULL(f1) && !ISNULL(f2) &&
                        (ISGT_DT(vp1, vp2) || ISEQ_DT(vp2, vp2))));
        break;
      case FOP_COM:
        /* see also fldops.c COM(): */
#undef COM_DT
#define COM_DT(af, bf, a, b)                    \
        (ISNULL(af) ? (ISNULL(bf) ? 0 : 1) :    \
         (ISNULL(bf) ? -1 :                     \
          (ISGT_DT(a, b) ? 1 : (ISLT_DT(a, b) ? -1 : 0))))
        rc = COM_DT(f1, f2, vp1, vp2);
        rc=fld2finv(f3,rc); break;
      case FOP_IN :
      case FOP_IS_SUBSET:
      case FOP_INTERSECT_IS_EMPTY:
      case FOP_INTERSECT_IS_NOT_EMPTY:
        /* WTF support multi/var `f1' properly; for now `f1' is
         * single-item fixed (checked above), thus intersect and
         * subset behave same:
         */
      {
	size_t	i;

      if (!ISNULL(f1))
	for(i=0; i < n2; i++)
	{
		if(ISEQ_DT(vp1, &vp2[i]))
		{
			rc = fld2finv(f3, (op != FOP_INTERSECT_IS_EMPTY));
			return rc;
		}
	}
      rc = fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
	break;
      }
      case FOP_INTERSECT:	/* returns a set WTF support */
	      rc = FOP_EILLEGAL;
	      break;
      default     : rc=FOP_EINVAL; break;
      }
   }
   return(rc);
#undef ISNULL
#undef COM_DT
}                                                     /* end fodtdt() */

/* ------------------------------------------------------------------------ */

/* date int
 * int date
 */
#undef ft_xx
#undef fodaxx
#undef foxxda
#define ft_xx	ft_int
#define fodaxx	fodain
#define foxxda	foinda
#include "fldop6.c"
#undef ft_xx
#undef fodaxx
#undef foxxda

/* ------------------------------------------------------------------------ */

/* date long
 * long date
 */
#undef ft_xx
#undef fodaxx
#undef foxxda
#define ft_xx	ft_long
#define fodaxx	fodalo
#define foxxda	foloda
#include "fldop6.c"
#undef ft_xx
#undef fodaxx
#undef foxxda

/* ------------------------------------------------------------------------ */

/* date float
 * float date
 */
#undef ft_xx
#undef fodaxx
#undef foxxda
#define ft_xx	ft_float
#define fodaxx	fodafl
#define foxxda	foflda
#include "fldop6.c"
#undef ft_xx
#undef fodaxx
#undef foxxda

/* ------------------------------------------------------------------------ */

/* date double
 * double date
 */
#undef ft_xx
#undef fodaxx
#undef foxxda
#define ft_xx	ft_double
#define fodaxx	fodado
#define foxxda	fododa
#include "fldop6.c"
#undef ft_xx
#undef fodaxx
#undef foxxda

/* ------------------------------------------------------------------------ */

/* date int64
 * int64 date
 */
#undef ft_xx
#undef fodaxx
#undef foxxda
#define ft_xx		ft_int64
#define fodaxx	fodai6
#define foxxda	foi6da
#include "fldop6.c"
#undef ft_xx
#undef fodaxx
#undef foxxda

/* ------------------------------------------------------------------------ */

/* date uint64
 * uint64 date
 */
#undef ft_xx
#undef fodaxx
#undef foxxda
#define ft_xx		ft_uint64
#define fodaxx	fodau6
#define foxxda	fou6da
#include "fldop6.c"
#undef ft_xx
#undef fodaxx
#undef foxxda

/* ------------------------------------------------------------------------ */

/* date handle
 * handle date
 */
#undef ft_xx
#undef fodaxx
#undef foxxda
#define ft_xx		ft_handle
#define fodaxx	fodaha
#define foxxda	fohada
#include "fldop6.c"
#undef ft_xx
#undef fodaxx
#undef foxxda

/******************************************************************/

int
fodadt(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_date *vp3;
	ft_datetime *vp2;
	size_t n2;
	TXTIMEINFO	t1;
	time_t	tim;

	vp2 = getfld(f2, &n2);
	switch(op)
	{
	case FOP_ASN:				/* date = datetime */
		TXmakesimfield(f1, f3);
		vp3 = getfld(f3, NULL);
		TXTIMEINFO_INIT(&t1);
		t1.year = vp2->year;
		t1.month = vp2->month;
		t1.dayOfMonth = vp2->day;
		t1.hour = vp2->hour;
		t1.minute = vp2->minute;
		t1.second = vp2->second;
		t1.isDst = -1;
		if (!TXlocalTxtimeinfoToTime_t(&t1, &tim))
			return(FOP_EINVAL);
		*vp3 = tim;
		return 0;
	case FOP_CNV:
		return(fodtda(f2, f1, f3, FOP_ASN));
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

int
fodtda(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_date *datePtr;
	ft_datetime *datetime;
	size_t n2;
	TXTIMEINFO	timeinfo;

	datePtr = (ft_date *)getfld(f2, &n2);
	switch(op)
	{
	case FOP_ASN:				/* datetime = date */
		TXmakesimfield(f1, f3);
		datetime = (ft_datetime *)getfld(f3, NULL);
		if (!TXtime_tToLocalTxtimeinfo(*datePtr, &timeinfo))
			return(FOP_EINVAL);
		datetime->year = timeinfo.year;
		datetime->month = timeinfo.month;
		datetime->day = timeinfo.dayOfMonth;
		datetime->hour = timeinfo.hour;
		datetime->minute = timeinfo.minute;
		datetime->second = timeinfo.second;
		datetime->fraction = 0;		/* due to limited time_t res*/
		return 0;
	case FOP_CNV:
		return(fodadt(f2, f1, f3, FOP_ASN));
	default:
		return FOP_EINVAL;
	}
}

/******************************************************************/

#define DATE_DAYNAME	1
#define DATE_MONTH	2
#define DATE_MONTHNAME	3
#define DATE_DAYOFMONTH	4
#define DATE_DAYOFWEEK	5
#define DATE_DAYOFYEAR	6
#define DATE_QUARTER	7
#define DATE_WEEK	8
#define DATE_YEAR	9
#define DATE_HOUR	10
#define DATE_MINUTE	11
#define DATE_SECOND	12
#define DATE_MONTHSEQ	13
#define DATE_WEEKSEQ	14
#define DATE_DAYSEQ	15

#define leap_year(yr)                   \
 (((yr) <= 1752 ? ((yr) % 4 == 0) :     \
 (((yr) % 4 == 0) && ((yr) % 100 != 0)) || ((yr) % 400 == 0)) ? 1 : 0)

/* number of centuries since 1700, not inclusive */
#define centuries_since_1700(yr) \
 ((yr) > 1700 ? (yr) / 100 - 17 : 0)

/* number of centuries since 1700 whose modulo of 400 is 0 */
#define quad_centuries_since_1700(yr) \
 ((yr) > 1600 ? ((yr) - 1600) / 400 : 0)

/* number of leap years between year 1 and this year, not inclusive */
#define leap_years_since_year_1(yr) \
 ((yr) / 4 - centuries_since_1700(yr) + quad_centuries_since_1700(yr))

static int dateconv ARGS((FLD *f1, int op));
static int
dateconv(f1, op)
FLD *f1;
int op;
{
	ft_date *vp1;
	char *datefmt;
	char datestr[80];
	int inc=0, rettype, dv=1, pdiv = 1, retval;
	size_t n1;
	struct tm *tm;

	vp1 = getfld(f1, &n1);
	if(n1 == 1)
	{
		tm = localtime(vp1);
		if(!tm)
			return FOP_EINVAL;
		switch(op)
		{
		case DATE_DAYNAME:
			datefmt="%A";
			rettype=FTN_CHAR;
			break;
		case DATE_MONTHSEQ:
			datestr[0] = '\0';
			strftime(datestr, sizeof(datestr), "%Y", tm);
			inc = (atoi(datestr) - 1) * 12;
			datefmt="%m";
			rettype=FTN_INT;
			break;
		case DATE_MONTH:
			datefmt="%m";
			rettype=FTN_INT;
			break;
		case DATE_MONTHNAME:
			datefmt="%B";
			rettype=FTN_CHAR;
			break;
		case DATE_DAYOFMONTH:
			datefmt="%d";
			rettype=FTN_INT;
			break;
		case DATE_DAYOFWEEK:
			datefmt="%w";
			rettype=FTN_INT;
			inc = 1;
			break;
		case DATE_WEEKSEQ:
			pdiv=7;
			goto dayseq;	/* avoid gcc 7 warning */
		case DATE_DAYSEQ:
		dayseq:
		{
			int Year;

			datestr[0] = '\0';
			strftime(datestr, sizeof(datestr), "%Y", tm);
			Year = atoi(datestr) - 1;
			inc = (Year * 365) + leap_years_since_year_1(Year);
			goto dayofyear;
		}
		case DATE_DAYOFYEAR:
		dayofyear:
			datefmt="%j";
			rettype=FTN_INT;
			break;
		case DATE_QUARTER:
			datefmt="%m";
			rettype=FTN_INT;
			dv = 3; inc = 1;
			break;
		case DATE_WEEK:
			datefmt="%U";
			rettype=FTN_INT;
			break;
		case DATE_YEAR:
			datefmt="%Y";
			rettype=FTN_INT;
			break;
		case DATE_HOUR:
			datefmt="%H";
			rettype=FTN_INT;
			break;
		case DATE_MINUTE:
			datefmt="%M";
			rettype=FTN_INT;
			break;
		case DATE_SECOND:
			datefmt="%S";
			rettype=FTN_INT;
			break;
		default:
			return FOP_EINVAL;
		}
		datestr[0] = '\0';
		strftime(datestr, sizeof(datestr), datefmt, tm);
		switch(rettype)
		{
		case FTN_CHAR:
			f1->type = FTN_CHAR | DDVARBIT;
			f1->elsz = 1;
			putfld(f1, strdup(datestr), strlen(datestr));
			return 0;
		case FTN_INT:
			retval = atoi(datestr);
			if(dv != 1)
				retval = retval / dv;
			retval += inc;
			if(pdiv != 1)
				retval = retval / pdiv;
			return fld2finv(f1, retval);
		}
	}
	return FOP_EINVAL;
}

/******************************************************************/

int
TXdayname(f1)
FLD *f1;
{
	return dateconv(f1, DATE_DAYNAME);
}

/******************************************************************/

int
TXmonth(f1)
FLD *f1;
{
	return dateconv(f1, DATE_MONTH);
}

/******************************************************************/

int
TXmonthname(f1)
FLD *f1;
{
	return dateconv(f1, DATE_MONTHNAME);
}

/******************************************************************/

int
TXdayofmonth(f1)
FLD *f1;
{
	return dateconv(f1, DATE_DAYOFMONTH);
}

/******************************************************************/

int
TXdayofweek(f1)
FLD *f1;
{
	return dateconv(f1, DATE_DAYOFWEEK);
}

/******************************************************************/

int
TXdayofyear(f1)
FLD *f1;
{
	return dateconv(f1, DATE_DAYOFYEAR);
}

/******************************************************************/

int
TXquarter(f1)
FLD *f1;
{
	return dateconv(f1, DATE_QUARTER);
}

/******************************************************************/

int
TXweek(f1)
FLD *f1;
{
	return dateconv(f1, DATE_WEEK);
}

/******************************************************************/

int
TXyear(f1)
FLD *f1;
{
	return dateconv(f1, DATE_YEAR);
}

/******************************************************************/

int
TXhour(f1)
FLD *f1;
{
	return dateconv(f1, DATE_HOUR);
}

/******************************************************************/

int
TXminute(f1)
FLD *f1;
{
	return dateconv(f1, DATE_MINUTE);
}

/******************************************************************/

int
TXsecond(f1)
FLD *f1;
{
	return dateconv(f1, DATE_SECOND);
}

/******************************************************************/

int
TXmonthseq(f1)
FLD *f1;
{
	return dateconv(f1, DATE_MONTHSEQ);
}

/******************************************************************/

int
TXdayseq(f1)
FLD *f1;
{
	return dateconv(f1, DATE_DAYSEQ);
}

/******************************************************************/

int
TXweekseq(f1)
FLD *f1;
{
	return dateconv(f1, DATE_WEEKSEQ);
}

