#define SOCKETS_IMPLEMENTED

#define STD_API
#define XML_API
#define WIN_IMP
#define EXPRT

#ifdef NEVER_STUPID
void *Malloc(int bytes);
void *Realloc(void *mem, int bytes);
void Free(void *mem);
#else
#define Malloc(a) malloc(a)
#define Realloc(a,b)  ((a)?realloc((a),(b)):malloc((b)))
#define Free(a) ((a) ? free(a) : (void)0)
#endif
