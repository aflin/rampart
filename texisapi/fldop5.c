
/*
   template 5: handles convert of diff size args
*/

/**********************************************************************/
int
foxxyy(f1,f2,f3,op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
int     var2;
int rc=0;

   var2=fldisvar(f2);
   /* KNG 20070411 FOP_CNV: `f3' should always have same type (w/var bit)
    * as `f2', not just when `(n1>1 || var1)':
    */
   /* if(n1>1 || var1) */
      switch(op){
      case FOP_CNV: rc=mote(f1, f3);
                    if(var2) f3->type|=DDVARBIT;
                    else     f3->type&=~DDVARBIT;/* wtf - copy n member?? */
                    break;
      default     : rc=FOP_EINVAL;
      }
#if 0
   }else{
      switch(op){
      case FOP_CNV: rc=mote(f1, f3); break;
      default     : rc=FOP_EINVAL;
      }
   }
#endif /* 0 */
   return(rc);
}                                                     /* end foxxyy() */
/**********************************************************************/

/* undef all macros now for safety; this file included multiple times: */
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
