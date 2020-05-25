#ifndef POOL_H
#define POOL_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* POOL object: holds resources associated with some transaction */

typedef struct POOL_tag         /* no user-serviceable parts inside */
{
  void  **list;                 /* array of allocated mem blocks */
  int   numsz, num;
}
POOL;
#define POOLPN  ((POOL *)NULL)


POOL    *openpool ARGS((void));
POOL    *closepool ARGS((POOL *p));
void    *pool_add ARGS((POOL *p, void *blk));
void    *pool_malloc ARGS((POOL *p, CONST char *fn, size_t sz));
void    *pool_calloc ARGS((POOL *p, CONST char *fn, size_t n, size_t sz));
char    *pool_strdup ARGS((POOL *p, CONST char *fn, CONST char *s));
int     pool_empty_actual ARGS((POOL *p, int level));
#define pool_empty(p, level)    \
  ((unsigned)(level) < (unsigned)(p)->num ? pool_empty_actual(p, level) : 1)
#define pool_level(p)   ((p)->num)
void    *pool_free ARGS((POOL *p, CONST char *fn, void *ptr));

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* !POOL_H */
