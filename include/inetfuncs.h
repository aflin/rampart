#ifndef INETFUNCS_H
#define INETFUNCS_H

/* IP address Texis SQL functions */

extern int      txfunc_inetabbrev ARGS((FLD *f1));
extern int      txfunc_inetcanon ARGS((FLD *f1));
extern int      txfunc_inetnetwork ARGS((FLD *f1));
extern int      txfunc_inethost ARGS((FLD *f1));
extern int      txfunc_inetbroadcast ARGS((FLD *f1));
extern int      txfunc_inetnetmask ARGS((FLD *f1));
extern int      txfunc_inetnetmasklen ARGS((FLD *f1));
extern int      txfunc_inetcontains ARGS((FLD *f1, FLD *f2));
extern int      txfunc_inetclass ARGS((FLD *f1));
extern int      txfunc_inet2int ARGS((FLD *f1));
extern int      txfunc_int2inet ARGS((FLD *f1));
int     txfunc_inetToIPv4(FLD *f1);
int     txfunc_inetToIPv6(FLD *f1);
int     txfunc_inetAddressFamily(FLD *f1);

#endif /* !INETFUNCS_H */
