

/* template 3: handles small arg1 and large arg2
 * Macros to set when #including this file:
 *   foyyxx   Function to create, for small yy and large xx
 *   foyyyy   field op function for yy and yy
 *   foxxxx   field op function for xx and xx
 *   demote   fld2...() demote function to FTN type of yy
 *   promote  fld2...() promote function to FTN type of xx
 */

/**********************************************************************/
int
foyyxx(f1,f2,f3,op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
int rc;

   if(op==FOP_ASN)
   {
      FLD *f4;

      TXmakesimfield(f1,f3);
      if((rc=demote(f2, f3))!=0) return(rc);
      f4=dupfld(f3);
      if(f4)
      {
	      rc = foyyyy(f1,f4,f3,op);
	      closefld(f4);
      }
      else
      	      rc = FOP_ENOMEM;
      return rc;
   }
   if((rc=promote(f1, f3))!=0) return(rc);
   return(foxxxx(f3,f2,f3,op));
}                                                     /* end foyyxx() */
/**********************************************************************/

/* Clear our "parameter" macros, to avoid potential downstream pollution: */
#undef foyyxx
#undef foyyyy
#undef foxxxx
#undef promote
#undef demote
