#ifndef MONO_H
#define MONO_H
#define ALOTOFDATA struct alotofdata

#ifdef LINT_ARGS
void mbeep(void);
void monofile(FILE *);
void savemono(void);
void restoremono(void);
void mcls(void );
void mcpos(int ,int );
void mclln(int );
void mscroll(void );
void monoputc(int );
void monoputs(unsigned char *);
void mputs(int ,int ,unsigned char *);
#ifndef MONO_C
void CDECL monoprintf(char *,...);
void CDECL mprintf(int y,int x,char *s,...);
#endif
#else
void mbeep();
void monofile();
void savemono();
void restoremono();
void mcls();
void mcpos();
void mclln();
void mscroll();
void monoputc();
void monoputs();
void mputs();
void CDECL monoprintf();
void CDECL mprintf();
#endif

extern int mcursor;

#endif
