#include <stdio.h>
#include <stdarg.h>  /* va_list etc */
#include <stddef.h>  /* size_t */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include "duktape.h"

#define REMALLOC(s,t)  do {(s) = realloc( (s) , (t) ); if ((char*)(s)==(char*)NULL){ return(2000001); } } while(0)
#define BUFREADSZ 4096
#define MAXBUFFER 536870912  /* 512mb */
#define GETDATAEND 1999999

/* fill buffer with more data from file.
   If "wrappos" > -1, data from wrappos to curend will be moved to
   the beginning of buf and writing to buf will begin at the end of valid
   data (curend).
   cursize and curend will be updated appropriately for multiple calls
   --  *fd       - file read handle
   --  *buf      - malloced buffer to fill
   --  *cursize  - current size of buffer
   --  *curend   - last byte of valid data in buf

   returns errno or other custom error code
   buf will be null terminated at curend
*/

int getdata(FILE *fd, char **buf, char **curend, size_t *cursize, int wrappos)
{
  int i=0,curdatasize=0;
  char *b;

  errno=0;
  /* get a block of memory, 
     curend shouldn't be set in this case*/
  if (*cursize < BUFREADSZ)
  {
    *cursize=BUFREADSZ;
    REMALLOC(*buf,*cursize+1);
  }
  /* set end of buffer data if not set */
  if (*curend==(char*)NULL) *curend=*buf+BUFREADSZ; /* -1 == end of buffer */

  /* check if we are wrapping.
     If so, move data from wrap point to curend to beginning of buffer.
     set up to write at new curend (buf + (curend-wrappos) )
     Make sure there is enough space for extra data */
  if (wrappos > -1)
  {
    /* get size of data to copy to beginning of string */
    curdatasize = *curend - (*buf + wrappos) ;
//printf("curdatasize(%d)=curend(%d) - (buf(%d) + wrappos(%d)\n", curdatasize,(int)*curend,(int)*buf,wrappos);
//printf("curend-1='%c', buf+wrappos='%c'\n",*(*curend-1),*(*buf+wrappos));
    int oldsize=*cursize;
    /* is our buffer currently large enough for new block of data ? */
    if( curdatasize + BUFREADSZ > *cursize)
    {
      *cursize+=BUFREADSZ;
      if (*cursize > MAXBUFFER)
        return (2000000); /* so as not to be confused with errno range errors*/
      REMALLOC(*buf,*cursize+1);
    }
    if(wrappos) /* "Wherever you go, there you are" --Buckaroo Banzai 
                   "If you don't know where you are going, you'll end up someplace else." -- Yogi Berra. */
      /* move data that starts at wrappoint to beginning of buf */
      memmove(*buf, *buf+wrappos, curdatasize);
    /* else if wrappos is 0, don't memmove from 0 to 0 */

    b=*buf+curdatasize;
  }
  /* no wrapping, overwrite buffer */
  else 
  {
    b=*buf;
  }
  i=(int)fread(b, 1, BUFREADSZ , fd );
  *curend=b+i;

  *(*curend)='\0'; /* at 4097 */

//printf("i=%d\n",i);
  if (i<BUFREADSZ)
    return(GETDATAEND);

  return(errno);  
}

FILE *openFileWithCallback(duk_context *ctx, int *func_idx, const char **fn)
{
  FILE * fp;
  int i=0;
  
  if(duk_is_string(ctx,i) || duk_is_string(ctx,++i) )
    *fn=duk_get_string(ctx,i);
  else
  {
    duk_push_string(ctx,"readln requires a file name passed as a string");
    (void) duk_throw(ctx);
  }
  
  i=!i; /* get the other option */
  
  if(duk_is_function(ctx,i))
    *func_idx=i;
  else
  {
    duk_push_string(ctx,"readln requires a callback function");
    (void) duk_throw(ctx);
  }
  /* TODO: check for directory */
  return(fopen(*fn, "r"));
}

#define NOWRAP -1

#define GETDATA(x) do {\
  error=getdata(fp,&buf,&end,&cursize,(x));\
  if(error && error!=GETDATAEND)\
  {\
      if(error==2000000)\
        duk_push_string(ctx,"value too large to parse (>512mb)");\
      else if(error==2000001)\
        duk_push_string(ctx,"error realloc()");\
      else\
        duk_push_sprintf(ctx,"error opening or parsing'%s': %s",fn,strerror(errno));\
      return duk_throw(ctx);\
  }\
} while (0)

#define GETVAL {}

duk_ret_t duk_util_readcsv(duk_context *ctx) {
  int wp=0, func_idx=-1, i=0;
  FILE * fp;
  const char *fn;
  char *buf=(char*)NULL, *end=(char*)NULL;
  size_t cursize=0;
  int error=0,inval=1,inq=0,wrap=NOWRAP;
  char *vs, *ve; /* value start and value end */   
  fp=openFileWithCallback(ctx, &func_idx,&fn);
/*
  while (!error)
  {
    GETDATA(wrap);
    GETVAL;
  }



  GETDATA(NOWRAP);
  printf("buf=%p, end=%p\n",buf,end);
  printf("cursize=%d, buf='%s'\n",(int)cursize, buf);
  while (!error)
  {
    int cutoff=(int)(end-buf)-10;
    GETDATA(cutoff);
    printf("\nbuf=%p, end=%p\n",buf,end);
    printf("cursize=%d, buf='%s'\n",(int)cursize, buf);
  }

  free(buf);
*/  return 0;
}


duk_ret_t duk_util_readln(duk_context *ctx) {
  int func_idx=-1;
  FILE * fp;
  const char *fn;
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  
  fp=openFileWithCallback(ctx, &func_idx,&fn);
  
  if (fp == NULL)
  {
      const char *fn=duk_to_string(ctx,!func_idx);
      duk_push_sprintf(ctx,"error opening '%s': %s",fn,strerror(errno));
      return duk_throw(ctx);
  }
  while ((read = getline(&line, &len, fp)) != -1) {
      duk_dup(ctx,func_idx);
      duk_push_this(ctx);
      *(line+strlen(line)-1)='\0';
      duk_push_string(ctx,line);
      duk_call_method(ctx,1);
      if(duk_is_boolean(ctx,-1) && duk_get_boolean(ctx,-1)==0)
        break;
      
      duk_pop(ctx);
  }

  fclose(fp);
  if (line)
      free(line);

  return(0);
}





static const duk_function_list_entry utils_funcs[] = {
    { "readln",         duk_util_readln, 2 /*nargs*/ },
    { "readcsv",        duk_util_readcsv, 2 /*nargs*/ },
    { NULL, NULL, 0 }
};

static const duk_number_list_entry utils_consts[] = {
    { NULL, 0.0 }
};

duk_ret_t dukopen_module(duk_context *ctx) {
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, utils_funcs);
    duk_put_number_list(ctx, -1, utils_consts);

    return 1;
}

