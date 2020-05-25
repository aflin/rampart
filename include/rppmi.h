#ifndef RPPMI_H
#define RPPMI_H


#ifndef RPPM_H
#  include "rppm.h"
#endif
#ifndef FDBI_H
#  include "fdbi.h"
#endif


typedef struct {
  union {
    FFS		*ex;
    XPMS	*xs;
    /* PPM rolled into SPM as 1 string */
    SPMS	*ss;
    NPMS	*np;
    FDBIS       *fs;
  } scanner;
  PMTYPE	type;			/* PMISREX, etc. */
  char		*exp;			/* original query expression */
  int           weight, absweight;      /* weight and abs(weight) */
  long          tblfreq;                /* table frequency */
  int           gain;                   /* +1023/-1023 gain */
  int           hits;                   /* number of hits this document */
  int           order;                  /* order # (excluding LOGINOT sets) */
  int           cookedtblfreq;          /* pre-computed RVAR_TBLFREQ value */
} RPPM_SET;
#define RPPM_SETPN      ((RPPM_SET *)NULL)
#define RPPM_SETPPN     ((RPPM_SET **)NULL)

typedef struct {
  RPPM_SET	*set;
  int		hit;		/* recent hit (offset into buf); -1 if none */
  int		len;
} RPPM_HIT;
#define RPPM_HITPN      ((RPPM_HIT *)NULL)

typedef enum RVAR_tag {
  RVAR_PROXIMITY,
  RVAR_LEADBIAS,
  RVAR_ORDER,
  RVAR_DOCFREQ,
  RVAR_TBLFREQ,                         /* must be last knob */
#define RVAR_KNOBNUM    RVAR_ALLMATCH   /* # gain/importance knobs */
  RVAR_ALLMATCH,
  RVAR_INFTHRESH,
  RVAR_INDEXTHRESH,
  RVAR_NUM                              /* must be last */
} RVAR;

typedef struct RVAL_tag {
  int           gain;                   /* user-settable values */
} RVAL;
#define RVALPN  ((RVAL *)NULL)

struct RPPMS_struct {
  RPPM_SET	*sets;		/* list of queries */
  int		numsets;
  int           numsetand;      /* # of non-LOGINOT sets */
  int           sumabswt, sumwt;
  int           sumknobgain, sumpossetgain;
  byte		*buf, *end;	/* search buffer */
  RPPM_HIT	*hits;		/* recent hits */
  RPPM_HIT	*best;		/* the best set of hits */
  int           bestnum, bestm, bestrank;
  RVAL          vals[RVAR_NUM];
};

RPPM_HIT *rppm_median_hit ARGS((RPPM_HIT *hits, int num));
int      rppm_rank_it ARGS((RPPMS *rp, RPPM_HIT *hits, int num, RPPM_HIT *m));
int      rppm_rank_it_signed ARGS((RPPMS *rp, RPPM_HIT *hits, int num,
                                   RPPM_HIT *m));
void     rppm_precomp ARGS((RPPMS *rp));

#endif	/* !RPPMI_H */
