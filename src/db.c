#include <stdarg.h>  /* va_list etc */
#include <stddef.h>  /* size_t */
#include <limits.h>
#include <stdlib.h>
#include <pthread.h>
#include <ctype.h>
#include <float.h>
#include "rp.h"

#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include "duktape.h"
#include "ramis.h"
#include "resp_protocol.h"
#include "respClient.h"



#ifdef SINGLETHREADED

#define FLOCK
#define FUNLOCK

#else

int lock_is_init=0;

/* lock around the fetch of all rows, rather than each row */
//#define LOCK_AROUND_ALL_FETCH

#ifdef DBUGLOCKS
#define FLOCK   printf("%d: locking from thread %lu\n",  __LINE__ ,(unsigned long int)pthread_self());fflush(stdout);pthread_mutex_lock  (&lock);
#define FUNLOCK printf("%d: unlocking from thread %lu\n",__LINE__ ,(unsigned long int)pthread_self());fflush(stdout);pthread_mutex_unlock(&lock);
#else
#define FLOCK   pthread_mutex_lock  (&lock);
#define FUNLOCK pthread_mutex_unlock(&lock);
#endif

#endif /* SINGLETHREADED */



#define TEXIS_OPEN(tdb) ({\
  FLOCK\
  TEXIS *rtx=texis_open((tdb), "PUBLIC", "");\
  FUNLOCK\
  rtx;\
})

#define TEXIS_CLOSE(rtx) ({\
  FLOCK\
  (rtx)=texis_close((rtx));\
  FUNLOCK\
  rtx;\
})

#define TEXIS_PREP(a,b) ({\
  FLOCK\
  /*printf("texisprep %s\n",(b));*/\
  int r=texis_prepare((a),(b));\
  FUNLOCK\
  r;\
})

#define TEXIS_EXEC(a) ({\
  FLOCK\
  /* printf("texisexec\n"); */\
  int r=texis_execute((a));\
  FUNLOCK\
  r;\
})

#ifdef LOCK_AROUND_ALL_FETCH

#define TEXIS_FETCH(a,b) ({\
  FLDLST *r=texis_fetch((a),(b));\
  r;\
})

#else

#define TEXIS_FETCH(a,b) ({\
  FLOCK\
  /* printf("texisfetch\n");*/\
  FLDLST *r=texis_fetch((a),(b));\
  FUNLOCK\
  r;\
})

#endif

#define TEXIS_SKIP(a,b) ({\
  FLOCK\
  int r=texis_flush_scroll((a),(b));\
  FUNLOCK\
  r;\
})

#define TEXIS_PARAM(a,b,c,d,e,f) ({\
  FLOCK\
  /* printf("texisparam\n"); */\
  int r=texis_param((a),(b),(c),(d),(e),(f));\
  FUNLOCK\
  r;\
})

void free_handle(DB_HANDLE *h) {
    h->tx = TEXIS_CLOSE(h->tx);
    free(h->db);
    free(h->query);
    free(h);
}

void free_all_handles(DB_HANDLE *h) {
  DB_HANDLE *next;
  while( h!=NULL ) {
      next=h->next;
      free_handle(h);
      h=next;
  }
}

DB_HANDLE *new_handle(const char *d,const char *q){
    DB_HANDLE *h=NULL;
    REMALLOC(h,sizeof(DB_HANDLE));
    h->tx=newsql((char*)(d));
    if(h->tx==NULL) {
      free(h);
      return NULL;
    }
    h->db=strdup(d);
    h->query=strdup(q);
    h->next=NULL;
    return(h);
}

/* for debugging */
void print_handles(DB_HANDLE *head) {
    DB_HANDLE *h=head;
    if(h==NULL)
        printf("[]\n");
    else {
        printf("[\n\tcur=%p, next=%p, q='%s'\n",h,h->next,h->query);
        while( h->next!=NULL ) {
            h=h->next;
            printf("\tcur=%p, next=%p, q='%s'\n",h,h->next,h->query);
        }
        printf("]\n");
    }
}

/* the LRU Texis Handle Cache is indexed by
   query.  Currently we have to reprep the 
   query each time it is used, negating the
   usefullness of indexing it as such, but 
   there may be a future version in which we 
   can use the same prepped handle and just 
   rewind the database to the first row without 
   having to texis_prep() again.  So for now,
   this structure stays.
*/

/* LRU:
   add a handle to beginning of list
   count number of items on list.
   if one too many, evict last one.
*/
DB_HANDLE *add_handle(DB_HANDLE *head,DB_HANDLE *h){
    int i=1; 
    DB_HANDLE *last;
    if(head!=NULL)
        h->next=head;
    head=h;
    while( h->next!=NULL ) {
      last=h;
      h=h->next; 
      i++;
    } 
    if( i == NDB_HANDLES+1 ) {
        last->next=NULL;
        free_handle(h);
    }
    return(head);
}

/*  search for handle (startin at h==head) that has same query and database
    if found, move to beginning of list
    if not, make new, and add to beginning of list
*/
DB_HANDLE *get_handle(DB_HANDLE *head, const char *d, const char *q) { 
   DB_HANDLE *last=NULL;
   DB_HANDLE *h=head;
   do{
       if( !strcmp((d),h->db) && !strcmp((q),h->query) ){
            if (last!=NULL) { /* if not at beginning of list */
                last->next=h->next; /*remove/pull item out of list*/
                h->next=head;  /* put this item at the head of the list */
                head=h;
            } /* else it's already at beginning */
            goto end;
        }
        last=h;
        h=h->next;
    } while(h!=NULL);
    /* got to the end of the list, not found */
    h=new_handle(d,q);
    if(h)
      head=add_handle(head,h);
    else
      return(NULL);

    end:
    return (head);
}

/* **************************************************
   Sql.prototype.close 
   ************************************************** */
duk_ret_t duk_rp_sql_close(duk_context *ctx) {
  DB_HANDLE *hcache=NULL;
  const char *db;
  
  duk_push_heap_stash(ctx);
  if ( duk_get_prop_string(ctx,-1,"hcache") )
  {
    hcache=(DB_HANDLE*)duk_get_pointer(ctx,-1);
    free_all_handles(hcache);
    duk_del_prop_string(ctx,-2,"hcache");
  }
  return 0;
}

#define msgbufsz 1024
#define msgtobuf(buf) \
  fseek(mmsgfh,0,SEEK_SET);\
  fread(buf, msgbufsz, 1, mmsgfh);

/* **************************************************
     store an error string in this.lastErr 
   **************************************************   */
void duk_rp_log_error(duk_context *ctx, char *pbuf)
{
    duk_push_this(ctx);
    duk_push_string(ctx,pbuf);
#ifdef PUTMSG_STDERR
    if(pbuf && strlen(pbuf)) {
#ifdef SINGLETHREADED
      fprintf(stderr,"%s\n",pbuf);
#else
      pthread_mutex_lock  (&printlock);
      fprintf(stderr,"%s\n",pbuf);
      pthread_mutex_unlock  (&printlock);
#endif
    }
#endif
    duk_put_prop_string(ctx, -2, "lastErr");
    duk_pop(ctx);
}

void duk_rp_log_tx_error(duk_context *ctx, char *buf)
{
  msgtobuf(buf);
  duk_rp_log_error(ctx,buf);
}

/* **************************************************
  push a single field from a row of the sql results
   ************************************************** */
void duk_rp_pushfield(duk_context *ctx, FLDLST *fl, int i) {
  char type=fl->type[i] & 0x3f;

  switch(type) {
    case FTN_CHAR:
    case FTN_INDIRECT:
    {
      duk_push_string(ctx,  (char*)fl->data[i]);
      break;
    }
    case FTN_STRLST:
    {
      ft_strlst *p=(ft_strlst*)fl->data[i];
      char *s=p->buf;
      size_t l=strlen(s);
      char *end=s+(p->nb);
      int i=0;

      duk_push_array(ctx);
      while (s<end) {
        duk_push_string(ctx, s);
        duk_put_prop_index(ctx,-2,i++);
        s+=l;
        while (s<end && *s=='\0')s++;
        l=strlen(s);
      }
      break;
    }
    case FTN_INT:
    {
      duk_push_int(ctx, (duk_int_t)*((ft_int*)fl->data[i]) );
      break;
    }
    /*        push_number with (duk_t_double) cast, 
              53bits of double is the best you can
              do for exact whole numbers in javascript anyway */
    case FTN_INT64:
    {
      duk_push_number(ctx, (duk_double_t)*((ft_int64*)fl->data[i]) );
      break;
    }
    case FTN_UINT64:
    {
      duk_push_number(ctx, (duk_double_t)*((ft_uint64*)fl->data[i]) );
      break;
    }
    case FTN_INTEGER:
    {
      duk_push_int(ctx, (duk_int_t)*((ft_integer*)fl->data[i]) );
      break;
    }
    case FTN_LONG:
    {
      duk_push_int(ctx, (duk_int_t)*((ft_long*)fl->data[i]) );
      break;
    }
    case FTN_SMALLINT:
    {
      duk_push_int(ctx, (duk_int_t)*((ft_smallint*)fl->data[i]) );
      break;
    }
    case FTN_SHORT:
    {
      duk_push_int(ctx, (duk_int_t)*((ft_short*)fl->data[i]) );
      break;
    }
    case FTN_DWORD:
    {
      duk_push_int(ctx, (duk_int_t)*((ft_dword*)fl->data[i]) );
      break;
    }
    case FTN_WORD:
    {
      duk_push_int(ctx, (duk_int_t)*((ft_word*)fl->data[i]) );
      break;
    }
    case FTN_DOUBLE:
    {
      duk_push_number(ctx, (duk_double_t)*((ft_double*)fl->data[i]) );
      break;
    }
    case FTN_FLOAT:
    {
      duk_push_number(ctx, (duk_double_t)*((ft_float*)fl->data[i]) );
      break;
    }
    case FTN_DATE:
    {
      /* equiv to js "new Date(seconds*1000)" */
      (void) duk_get_global_string(ctx, "Date");
      duk_push_number(ctx,1000.0*(duk_double_t)*((ft_date*)fl->data[i]));
      duk_new(ctx,1);
      break;
    }
    case FTN_COUNTER:
    {
      unsigned char *p;
      /* TODO: revisit how we want this to be presented to JS */
      /* create backing buffer and copy data into it */
      //p=(unsigned char *) duk_push_fixed_buffer(ctx, 8 /*size*/);
      //memcpy(p,fl->data[i],8);
      char s[32];
      ft_counter *acounter = fl->data[i];
      snprintf(s,32,"%lx%lx",acounter->date, acounter->seq);      
      duk_push_string(ctx,s);
      break;
    }
    case FTN_BYTE:
    {
      unsigned char *p;

      /* create backing buffer and copy data into it */
      p=(unsigned char *) duk_push_fixed_buffer(ctx, fl->ndata[i] /*size*/);
      memcpy(p,fl->data[i],fl->ndata[i]);
      break;
    }
    default:
    duk_push_int(ctx,(int)type);
  }

}

/* **************************************************
   like duk_get_int_default but if string, converts 
   string to number with strtol 
   ************************************************** */
int duk_rp_get_int_default(duk_context *ctx,duk_idx_t i,int def) {
  if(duk_is_number(ctx,i))
    return duk_get_int_default(ctx,i,def);
  if(duk_is_string(ctx,i))
  {
    char *end,*s=(char *)duk_get_string(ctx,i);
    int ret=(int)strtol(s, &end, 10);
          
    if (end==s) return (def);
      return (ret);
  }
  return (def);
}
/*
    CURRENTLY UNUSED and UNTESTED

* **************************************************
   like duk_require_int but if string, converts 
   string to number with strtol 
   ************************************************** *
int duk_rp_require_int(duk_context *ctx,duk_idx_t i) {
  if(duk_is_number(ctx,i))
    return duk_get_int(ctx,i);
  if(duk_is_string(ctx,i))
  {
    char *end,*s=(char *)duk_get_string(ctx,i);
    int ret=(int)strtol(s, &end, 10);
          
    if (end!=s)
      return (ret);
  }

  //throw standard error
  return duk_require_int(ctx,i);
}

* **************************************************
   like duk_get_number_default but if string, converts 
   string to number with strtod 
   ************************************************** *
double duk_rp_get_number_default(duk_context *ctx,duk_idx_t i,double def) {
  if(duk_is_number(ctx,i))
    return duk_get_number_default(ctx,i,def);
  if(duk_is_string(ctx,i))
  {
    char *end,*s=(char *)duk_get_string(ctx,i);
    int ret=(double)strtod(s, &end);
          
    if (end==s) return (def);
      return (ret);
  }
  return (def);
}

* **************************************************
   like duk_require_number but if string, converts 
   string to number with strtod 
   ************************************************** *
int duk_rp_require_number(duk_context *ctx,duk_idx_t i) {
  if(duk_is_number(ctx,i))
    return duk_get_number_default(ctx,i,def);
  if(duk_is_string(ctx,i))
  {
    char *end,*s=(char *)duk_get_string(ctx,i);
    int ret=(int)strtod(s, &end);
          
    if (end!=s)
      return (ret);
  }

  //throw standard error
  return duk_require_number(ctx,i);
}
*/

/* **************************************************
    initialize query struct
   ************************************************** */
void duk_rp_init_qstruct(QUERY_STRUCT *q)
{
    q->sql=(char*)NULL;
    q->arryi=-1;
    q->callback=-1;
    q->skip=0;
    q->max=RESMAX_DEFAULT;
    q->retarray=0;
    q->err=QS_SUCCESS;
}

/* **************************************************
   get up to 4 parameters in any order. 
   object=settings, string=sql, 
   array=params to sql, function=callback 
   example: 
   sql.exec(
     "select * from SYSTABLES where NAME=?",
     ["mytable"],
     {max:1,skip:0,returnType:"array:},
     function (row) {console.log(row);}
   );
   ************************************************** */
/* TODO: leave stack as you found it */

QUERY_STRUCT duk_rp_get_query(duk_context *ctx)
{
  int i=0;
  QUERY_STRUCT q_st;
  QUERY_STRUCT *q=&q_st;
  
  duk_rp_init_qstruct(q);

  for (i=0; i<4; i++) 
  {
    int vtype=duk_get_type(ctx,i);
    switch(vtype) {
      case DUK_TYPE_STRING:
      {
        int l;
        if (q->sql!=(char*)NULL) 
        {
          duk_rp_log_error(ctx,"Only one string may be passed as a parameter and must be a sql statement.");
          duk_push_int(ctx,-1);
          q->sql=(char*)NULL;
          q->err=QS_ERROR_PARAM;
          return(q_st); 
        }
        q->sql=duk_get_string(ctx, i);
        l=strlen(q->sql)-1;
        while( *(q->sql+l) == ' ' && l>0 ) l--;
        if( *(q->sql+l) != ';' )
        {
            duk_dup(ctx,i);
            duk_push_string(ctx,";");
            duk_concat(ctx,2);
            duk_replace(ctx,i);
            q->sql=(char *) duk_get_string(ctx, i);
        }
        break;
      }
      case DUK_TYPE_OBJECT:
      {
        /* array of parameters*/

        if (duk_is_array(ctx,i) && q->arryi==-1)
          q->arryi=i;
                
        /* argument is a function, save where it is on the stack */
        else if(duk_is_function(ctx,i) )
        {
          q->callback=i;
        }

        /* object of settings */
        else
        {
          if(duk_get_prop_string(ctx,i,"skip"))
            q->skip=duk_rp_get_int_default(ctx,-1,0);

          duk_pop(ctx);

          if(duk_get_prop_string(ctx,i,"max")) 
          {
            q->max=duk_rp_get_int_default(ctx,-1,q->max);
            if(q->max<0)q->max=INT_MAX;
          }

          if(duk_get_prop_string(ctx,i,"returnType")) 
          {
            const char *rt=duk_to_string(ctx,-1);
            
            if (!strcasecmp("array",rt)){
              q->retarray=1;
            } else if (!strcasecmp("arrayh",rt)){
              q->retarray=2;
            } else if (!strcasecmp("novars",rt)){
              q->retarray=3;
            }
          }

          duk_pop(ctx);
      
        }
        
        break;
      } /* case */
    } /* switch */
  } /* for */
  if (q->sql==(char*)NULL) {
    q->err=QS_ERROR_PARAM;
    duk_rp_log_error(ctx,"No sql statement.");
    duk_push_int(ctx,-1);
  }
  return(q_st);
}

/* **************************************************
   Push parameters to the database for parameter 
   substitution (?) is sql. 
   i.e. "select * from tbname where col1 = ? and col2 = ?"
     an array of two values should be passed and will be
     processed here.
     arrayi is the place on the stack where the array lives.
   ************************************************** */

int duk_rp_add_parameters(duk_context *ctx, TEXIS *tx, int arryi)
{
    int rc,arryn=0;

    /* Array is at arryi. Iterate over members.
       arryn is the index of the array we are examining */
    while(duk_has_prop_index(ctx,arryi,arryn)) 
    {
      void *v;    /* value to be passed to db */
      long plen;  /* lenght of value */
      double d;   /* for numbers */
      int in,out;

      /* push array member to top of stack */
      duk_get_prop_index(ctx,arryi,arryn);
      
      /* check the datatype of the array member */
      switch(duk_get_type(ctx, -1)) 
      {
        case DUK_TYPE_NUMBER:
        {
          d=duk_get_number(ctx,-1);
          v=(double *)&d;
          plen=sizeof(double);
          in=SQL_C_DOUBLE; 
          out=SQL_DOUBLE;
          break;
        }
        /* all objects are converted to json string
           this works (or will work) for several datatypes (varchar,int(x),strlst,json varchar) */
        case DUK_TYPE_OBJECT:
        {
          char *e;
          char *s=v=(char *)duk_json_encode(ctx, -1);
          plen=strlen(v);

          e=s+plen-1;
          /* a date (and presumably other single values returned from an object which returns a string)
             will end up in quotes upon conversion, we need to remove them */
          if ( *s=='"' && *e=='"' && plen>1 ) {
            /* duk functions return const char* */
            v=s+1;
            plen-=2;
          }
          in=SQL_C_CHAR;
          out=SQL_VARCHAR;
          break;
        }
        /* insert binary data from a buffer */
        case DUK_TYPE_BUFFER:
        {
          duk_size_t sz;
          v=duk_get_buffer_data(ctx, -1, &sz);
          plen=(long)sz;
          in=SQL_C_BINARY;
          out=SQL_BINARY;
          break;
        }
        /* default for strings (not converted)and
           booleans, null and undefined (converted to 
           true/false, "null" and "undefined" respectively */ 
        default:
        {
          v=(char *)duk_to_string(ctx,-1);
          plen=strlen(v);
          in=SQL_C_CHAR;
          out=SQL_VARCHAR;
        }
      }
      arryn++;
      rc=TEXIS_PARAM(tx, arryn, v, &plen, in, out);
      if (!rc) {
        return (0);
      }
      duk_pop(ctx);
    }
    return (1);
}

/* **************************************************
   This is called when sql.exec() has no callback.
   fetch rows and push results to array 
   return number of rows 
   ************************************************** */
int duk_rp_fetch(duk_context *ctx,TEXIS *tx, QUERY_STRUCT *q) {
  int rown=0, i=0,
      resmax=q->max,
      retarray=q->retarray;
  FLDLST *fl;
#ifdef LOCK_AROUND_ALL_FETCH
  FLOCK
#endif
  /* create return array (outer array) */
  duk_push_array(ctx);

  /* array of arrays or novars requested */
  if (retarray)
  {
    /* push values into subarrays and add to outer array */
    while ( rown<resmax && (fl=TEXIS_FETCH(tx,-1)) )
    {
      /* novars, we need to get each row (for del and return count value) but not return any vars */
      if (retarray==3)
      {
        rown++;
        continue; 
      }
      /* we want first rowto be column names */
      if (retarray==2)
      {
        /* an array of column names */
        duk_push_array(ctx);
        for (i = 0; i < fl->n; i++)
        {
          duk_push_string(ctx,fl->name[i]);
          duk_put_prop_index(ctx,-2,i);
        }
        duk_put_prop_index(ctx,-2,rown++);
        retarray=1;/* do this only once */
      }

      /* push values into array */
      duk_push_array(ctx);
      for (i = 0; i < fl->n; i++)
      {
        duk_rp_pushfield(ctx,fl, i);
        duk_put_prop_index(ctx,-2,i);
      }
      duk_put_prop_index(ctx,-2,rown++);

    } /* while */
  }
  else
  /* array of objects requested
     push object of {name:value,name:value,...} into return array */
  {  
printf("getting rows\n");
    while (rown<resmax && (fl=TEXIS_FETCH(tx,-1)) )
    {
      duk_push_object(ctx);
      for (i = 0; i < fl->n; i++)
      {
        duk_rp_pushfield(ctx,fl, i);
//duk_dup_top(ctx);
//printf("%s -> %s\n",fl->name[i],duk_to_string(ctx,-1));
//duk_pop(ctx);
        duk_put_prop_string(ctx,-2,(const char*)fl->name[i]);
      }
      duk_put_prop_index(ctx,-2,rown++);
    }
  }
#ifdef LOCK_AROUND_ALL_FETCH
  FUNLOCK
#endif
  return(rown);
}

/* **************************************************
   This is called when sql.exec() has a callback function 
   Fetch rows and execute JS callback function with 
   results. 
   Return number of rows 
   ************************************************** */
int duk_rp_fetchWCallback(duk_context *ctx,TEXIS *tx, QUERY_STRUCT *q) {
    int rown=0,i=0,
        resmax=q->max,
        retarray=q->retarray,
        callback=q->callback;
    FLDLST *fl;    
#ifdef LOCK_AROUND_ALL_FETCH
  FLOCK
#endif
    while ( rown<resmax && (fl=TEXIS_FETCH(tx,-1)) ) {
      duk_dup(ctx,callback);
      duk_push_this(ctx);

      /* array requested */
      switch (retarray)
      {
        /* novars */
        case 3:
        {
          duk_dup(ctx,callback);
          duk_push_this(ctx);
          duk_push_object(ctx);
          duk_push_int(ctx,rown++);
          duk_call_method(ctx,2);
          break;
        }
        
        /* requesting first row to be column names
           so for the first row, we do two callbacks:
           one for headers, one for first row
        */
        case 2:
        {
          /* set up an extra call to callback */
          duk_dup(ctx,callback);
          duk_push_this(ctx);

          duk_push_array(ctx);
          for (i = 0; i < fl->n; i++)
          {
            duk_push_string(ctx,fl->name[i]);
            duk_put_prop_index(ctx,-2,i);
          }

          duk_push_int(ctx,-1);
          duk_call_method(ctx,2); /* function(res,resnum){} */
          retarray=1; /* give names to callback only once */

          /* if function returns false, exit while loop, return number of rows so far */
          if (duk_is_boolean(ctx,-1) && !duk_get_boolean(ctx,-1) ) {
            duk_pop(ctx);
#ifdef LOCK_AROUND_ALL_FETCH
  FUNLOCK
#endif
            return (rown);
          }
          duk_pop(ctx);
          /* no break, fallthrough */
        }
        case 1:
        {
          duk_dup(ctx,callback);
          duk_push_this(ctx);
          duk_push_array(ctx);
          for (i = 0; i < fl->n; i++)
          {
            duk_rp_pushfield(ctx,fl,i);
            duk_put_prop_index(ctx,-2,i);
          }

          duk_push_int(ctx,rown++);
          duk_call_method(ctx,2); /* function(res,resnum){} */
          break;
        }
        /* object requested */
        case 0:
        {
          duk_push_object(ctx);
          for (i = 0; i < fl->n; i++) 
          {
            duk_rp_pushfield(ctx,fl,i);
            duk_put_prop_string(ctx,-2,(const char*)fl->name[i]);
          }
          duk_push_int(ctx,rown++);
          duk_call_method(ctx,2); /* function(res,resnum){} */
          break;
        }
      } /* switch */
      /* if function returns false, exit while loop, return number of rows so far */
      if (duk_is_boolean(ctx,-1) && !duk_get_boolean(ctx,-1) ) {
        duk_pop(ctx);
#ifdef LOCK_AROUND_ALL_FETCH
        FUNLOCK
#endif
        return (rown);
      }
      /* get rid of ret value from callback*/
      duk_pop(ctx);
    } /* while fetch */
#ifdef LOCK_AROUND_ALL_FETCH
    FUNLOCK
#endif

    return(rown);
}


void printstack(duk_context *ctx) {
  duk_push_context_dump(ctx);
  printf("%s\n",duk_to_string(ctx,-1));
  duk_pop(ctx);
}


/* ************************************************** 
   Sql.prototype.exec 
   ************************************************** */
duk_ret_t duk_rp_sql_exe(duk_context *ctx) 
{
  TEXIS *tx;
  int i=0;
  QUERY_STRUCT *q, q_st;
  DB_HANDLE *hcache=NULL;
  const char *db;
  size_t sz;
  char pbuf[1024];
  FILE* oldfh=mmsgfh;
  FILE* newfh=fmemopen(NULL, 1024,"w+");
  
  mmsgfh=newfh;

  duk_push_this(ctx);

  if(!duk_get_prop_string(ctx,-1,"db"))
  {
    duk_push_string(ctx,"no database is open");
    duk_throw(ctx);
  }
  db=duk_get_string(ctx,-1);
  duk_pop_2(ctx);

  q_st=duk_rp_get_query(ctx);
  q=&q_st;

#ifdef USEHANDLECACHE
  duk_push_heap_stash(ctx);
  /* check that we have a handle cache for this thread */
  if ( duk_get_prop_string(ctx,-1,"hcache") )
  {
    hcache=(DB_HANDLE*)duk_get_pointer(ctx,-1);
    /* get handle from cache.  If miss, make new one */
    hcache=get_handle(hcache,db,q->sql);
  }
  else
  {
    hcache=new_handle(db,q->sql);
  }
  if(hcache==NULL)
  {
    duk_rp_log_tx_error(ctx,pbuf);
    duk_push_int(ctx,-1);
    duk_push_string(ctx,pbuf);
    mmsgfh=oldfh;
    fclose(newfh);
    duk_throw(ctx);
  }
  /* hcache may have changed.  update it for all cases */
  duk_push_pointer(ctx,(void*)hcache);
  duk_put_prop_string(ctx,-3,"hcache"); /* [ heap, [undefined|oldpointer], pointer ] */
  duk_pop_3(ctx);

  tx=hcache->tx;
#else
  tx=newsql((char*)db);
#endif
  if (!tx)
  {
    duk_rp_log_tx_error(ctx,pbuf);
    duk_push_int(ctx,-1);
    duk_push_string(ctx,pbuf);
    mmsgfh=oldfh;
    fclose(newfh);
    duk_throw(ctx);
  }
  /* clear the sql.lastErr string */
  duk_rp_log_error(ctx,"");

  /* call parameters error, message is already pushed */
  if(q->err==QS_ERROR_PARAM) {
    goto end;
  }

  if(!TEXIS_PREP(tx, (char*)q->sql )) {
    duk_rp_log_tx_error(ctx,pbuf);
    duk_push_string(ctx,pbuf);
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
    mmsgfh=oldfh;
    fclose(newfh);
    duk_throw(ctx);
  }

  /* sql parameters are the parameters corresponding to "?" in a sql statement
     and are provide by passing array in JS call parameters */

  /* add sql parameters */
  if(q->arryi !=-1)
    if(!duk_rp_add_parameters(ctx, tx, q->arryi))
    {
      duk_rp_log_tx_error(ctx,pbuf);
      duk_push_string(ctx,pbuf);
#ifndef USEHANDLECACHE
      tx=TEXIS_CLOSE(tx);
#endif
      mmsgfh=oldfh;
      fclose(newfh);
      duk_throw(ctx);
    }

  if(!TEXIS_EXEC(tx))
  {
    duk_rp_log_tx_error(ctx,pbuf);
    duk_push_string(ctx,pbuf);
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
    mmsgfh=oldfh;
    fclose(newfh);
    duk_throw(ctx);
  }
  /* skip rows using texisapi */
  if(q->skip)
    TEXIS_SKIP(tx,q->skip);
  /* callback - return one row per callback */

  if (q->callback>-1) {
    int rows=duk_rp_fetchWCallback(ctx,tx,q);    
    duk_push_int(ctx,rows);
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
    goto end; /* done with exec() */
  }

  /*  No callback, return all rows in array of objects */
  (void) duk_rp_fetch(ctx,tx,q);
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif

  end:

  mmsgfh=oldfh;
  fclose(newfh);
  //free(pbuf);

  return 1;  /* returning outer array */

}

/* **************************************************
   Sql.prototype.eval 
   ************************************************** */
duk_ret_t duk_rp_sql_eval(duk_context *ctx) {
  char *stmt=(char*)NULL;
  char out[2048];
  char *s;
  size_t len;
  int arryi=-1,i=0;

  /* find the argument that is a string */
  for (i=0; i<4; i++) 
  {
    int vtype=duk_get_type(ctx,i);
    if(vtype==DUK_TYPE_STRING)
    {
        stmt=(char *) duk_get_string(ctx, i);
        arryi=i;
        len=strlen(stmt);
        break;
    }
  }

  if (arryi==-1) {
    duk_rp_log_error(ctx,"Error: Eval: No string to evaluate");
    duk_push_int(ctx,-1);
    return(1);
  }

  if (len > 2036) {
    duk_rp_log_error(ctx,"Error: Eval: Eval string too long (>2036)");
    duk_push_int(ctx,-1);
    return(1);
  }

  sprintf(out,"select (%s);",stmt);
  duk_push_string(ctx,out);
  duk_replace(ctx,arryi);

  return (duk_rp_sql_exe(ctx));
}


TEXIS *newsql(char* db)
{
  TEXIS *tx;

  tx = TEXIS_OPEN(db);

  return tx;
}

/* ************************************************************** 

   RAMIS - redis compatible client functions 

   ************************************************************** */

const char *duk_rp_require_bufOrStr(duk_context *ctx, duk_idx_t idx)
{
  if(duk_is_buffer_data(ctx,idx)) {
    duk_size_t sz;
    return ( (const char *) duk_require_buffer_data(ctx,idx,&sz) );
  }
  return duk_require_string(ctx,idx);
}

#define SETARG(type,t,f) do {\
  r->type = (t) f(ctx,1);\
} while (0)

RP_VA_RET duk_rp_getarg(duk_context *ctx, const char *type)
{
  RP_VA_RET ret, *r=&ret;
  
  if(duk_is_undefined(ctx,1))
  {
    duk_push_string(ctx,"not enough arguments for exec(fmt,...)");
    duk_throw(ctx);
  }

  switch(*type)
  {
    case 'c':
    {
      SETARG(c,char *,duk_require_string);
      break;
    }
    case 's':
    {
      SETARG(s,size_t,duk_require_number);
      break;
    }
    case 'i':
    {
      SETARG(i,int,duk_require_int);
      break;
    }
    case 'l':
    {
      if (strlen(type)>5)
        SETARG(L,long long,duk_require_number);
      else
        SETARG(l,long,duk_require_number);
      break;
    }
    case 'u':
    {
      int len=strlen(type);
      if (len>13)
        SETARG(I,unsigned long long,duk_require_number);
      else if (len>9)
        SETARG(U,unsigned long,duk_require_number);
      else
        SETARG(u,unsigned,duk_require_number);
      break;
    }
    case 'd':
    {
      SETARG(d,double, duk_require_number);
      break;
    } 
    case 'b':
    {
      SETARG(c,char *,duk_rp_require_bufOrStr);
      break;
    } 
  }
  duk_pull(ctx,1); //move item to top of stack.  next item is now #1
  return ret;
}


RESPROTO * rc_send(duk_context *ctx, RESPCLIENT *rcp)
{
  char *fmt=(char*)duk_require_string(ctx,0);

  duk_push_undefined(ctx); //marker for end

  return sendRespCommand(rcp,fmt,ctx);
}

void ra_push_response(duk_context *ctx, RESPROTO *response)
{
  int i,endofarray=-1,skipnextput=0;
  duk_uarridx_t l;  

    duk_push_array(ctx); // the return array
    if(response)
     {
       RESPITEM *item=response->items;

       for(i=0;i<response->nItems;i++,item++)
       {
         switch(item->respType)
         {
            case RESPISNULL:
            {
              duk_push_null(ctx);
              break;
            }
            case RESPISFLOAT:
            {
              duk_push_number(ctx,(double)item->rfloat);
              break;
            }
            case RESPISINT:
            {
              duk_push_number(ctx,(double)item->rinteger);
              break;
            }
            
            case RESPISARRAY:
            {
              /* create an inner array and keep track
                 of where it's supposed to end */
              duk_push_array(ctx);
              endofarray=i+item->nItems;
              skipnextput=1;
              break;
            }            
            case RESPISBULKSTR:
            {
              size_t l= 1 + strnlen(item->loc,item->length);
              if(l < item->length)
              { /* if it's binary, put it in a buffer */
                void *b=duk_push_fixed_buffer(ctx,item->length);
                memcpy(b,item->loc,item->length);
                break;
              }/* else fall through and copy string */
            }
            case RESPISSTR:
            case RESPISPLAINTXT:
            {
              duk_push_string(ctx,(const char*)item->loc);
              break;
            }
            /* TODO: how to return errors?? */
            case RESPISERRORMSG:
            {
              duk_push_sprintf(ctx,"Error message: %s\n",item->loc);
              break;
            }
            
         }
         
         

         if (i==endofarray)
         { /* the inner array */
           l=duk_get_length(ctx,-2);
           duk_put_prop_index(ctx,-2,l);
           endofarray=-1;
         }

         l=duk_get_length(ctx,-2);


         if(!skipnextput)
           duk_put_prop_index(ctx,-2,l); //the outer array

         skipnextput=0;
       }
     }
    //TODO: else NULL response == Error, push something useful or throw
}



duk_ret_t duk_rp_ra_send(duk_context *ctx) {
  duk_idx_t top=duk_get_top(ctx);
  int i=0;
  RESPCLIENT *rcp;
  RESPROTO *response;

  /* get the client */
  duk_push_this(ctx);
  duk_get_prop_string(ctx,-1,DUK_HIDDEN_SYMBOL("respclient"));
  rcp=(RESPCLIENT *)duk_get_pointer(ctx,-1);
  duk_pop_2(ctx);
  response=rc_send(ctx,rcp);
  ra_push_response(ctx,response);

  return 1;
}

/* **********************************************************

   SQL and RAMIS constructor functions &
   SQL and RAMIS init function

   ********************************************************** */

/* **************************************************
   Ramis(host,port) constructor
   var ra=new Ramis("127.0.0.1",6379);

   ************************************************** */
duk_ret_t duk_rp_ra_constructor(duk_context *ctx) {
  const char *ip=duk_get_string_default(ctx,0,"127.0.0.1");
  int port = (int) duk_get_int_default(ctx,1,6379);
  RESPCLIENT *respClient=NULL;
  char stashvar[32];

  if (!duk_is_constructor_call(ctx)) {
    return DUK_RET_TYPE_ERROR;
  }

  snprintf(stashvar,32,"%s:%d",ip,port);

  duk_push_heap_stash(ctx);
  if(duk_get_prop_string(ctx,-1,stashvar))
  {
    respClient=(RESPCLIENT *)duk_get_pointer(ctx,-1);
  }
  else
  {
    respClient=connectRespServer((char*)ip,port);
    if(respClient)
    {
      duk_push_pointer(ctx,respClient);
      duk_put_prop_string(ctx,-3,stashvar); /* stack -> [ stash , undefined, pointer ] */
    }
  }

  if (!respClient)
  {
    duk_push_sprintf(ctx,"respClient: Failed to connect to %s:%d\n",ip,port);
    duk_throw(ctx);
  }
  // TODO: ask what this should be set to
  respClient->waitForever=1;
//  respClientWaitForever(respClient,1);
  duk_push_this(ctx);
  duk_push_pointer(ctx,(void*)respClient);
  duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("respclient"));
  return 0;
}

/* **************************************************
   Initialize ramis client into global object. 
   ************************************************** */
void duk_ra_init(duk_context *ctx) {
  /* Set up locks:
     this will be run once per new duk_context/thread in server.c
     but needs to be done only once for all threads
  */
  /* Push constructor function */
  duk_push_c_function(ctx, duk_rp_ra_constructor, 2 /*nargs*/);

  /* Push object that will be Sql.prototype. */
  duk_push_object(ctx);  /* -> stack: [ Sql protoObj ] */

  /* Set Sql.prototype.exec. */
  duk_push_c_function(ctx, duk_rp_ra_send, DUK_VARARGS);  /* [ Sql proto fn_exe ] */
  duk_put_prop_string(ctx, -2, "exec");  /* [Sql protoObj-->[exe=fn_exe] ] */

  /* Set Sql.prototype = proto */
  duk_put_prop_string(ctx, -2, "prototype");  /* -> stack: [ Sql-->[prototype-->[exe=fn_exe,...]] ] */

  /* Finally, register Sql to the global object */
  duk_put_global_string(ctx, "Ramis");  /* -> stack: [ ] */

}


/* **************************************************
   Sql("/database/path") constructor:

   var sql=new Sql("/database/path");
   var sql=new Sql("/database/path",true); //create db if not exists

   There are x handle caches, one for each thread
   There is one handle cache for all new Sql() calls 
   in each thread regardless of how many dbs will be opened.

   Calling new Sql() only stores the name of the db path
   And there is one database per new Sql("/db/path");

   Here we only check to see that the database exists and
   construct the js object.  Actual opening and caching
   of handles to the db is done in exec()

   TODO: prevent second server from accessing any db that
         is opened here, perhaps with a pid file checked
         once upon the first call of new Sql()

   ************************************************** */
duk_ret_t duk_rp_sql_constructor(duk_context *ctx) {

  TEXIS *tx;
  const char * db=duk_get_string(ctx,0);
  char pbuf[1024];
  int i=0;
  int makedb=0;
  FILE *oldfh=mmsgfh;
  FILE *newfh;
  
  newfh=mmsgfh=fmemopen(pbuf, 1024, "w+");

  /* allow call to Sql() with "new Sql()" only */
  if (!duk_is_constructor_call(ctx)) {
    mmsgfh=oldfh;
    fclose(newfh);
    return DUK_RET_TYPE_ERROR;
  }

  /* 
     if sql=new Sql("/db/path",true), we will 
     create the db if it does not exist 
  */
  if(duk_is_boolean(ctx,1) && duk_get_boolean(ctx,1)!=0 ) {
    /* check for db first */
    tx=TEXIS_OPEN((char*)db);
    if (tx==NULL)
    {
      FLOCK
      if(!createdb(db)){
        FUNLOCK
        duk_rp_log_tx_error(ctx,pbuf);
        duk_push_sprintf(ctx,"cannot create database at '%s' (root path not found, lacking permission or other error\n)", db,pbuf);
        mmsgfh=oldfh;
        fclose(newfh);
        duk_throw(ctx);
      }
      FUNLOCK
    }
    else
     tx=TEXIS_CLOSE(tx);
  }

  /* save the name of the database in 'this' */
  duk_push_this(ctx);  /* -> stack: [ db this ] */
  duk_push_string(ctx,db);
  duk_put_prop_string(ctx, -2, "db");

  mmsgfh=oldfh;
  fclose(newfh);
  return 0;
}

/* utility function for global object:
      var buf=toBuffer(val); //fixed if string, same type if already buffer
        or
      var buf=toBUffer(val,"[dynamic|fixed]"); //always converted to type
*/

duk_ret_t duk_rp_strToBuf(duk_context *ctx)
{
  duk_size_t sz;
  const char *opt=duk_to_string(ctx,1);

  if( !strcmp(opt,"dynamic") )
    duk_to_dynamic_buffer(ctx,0,&sz);
  else if ( !strcmp(opt,"fixed") )
    duk_to_fixed_buffer(ctx,0,&sz);
  else
    duk_to_buffer(ctx,0,&sz);

  duk_pop(ctx);
  return 1;
}

void duk_strToBuf_init(duk_context *ctx)
{
  duk_push_c_function(ctx, duk_rp_strToBuf, 2);
  duk_put_global_string(ctx, "toBuffer");
}


/* **************************************************
   Initialize Sql into global object. 
   ************************************************** */
void duk_db_init(duk_context *ctx) {
  /* Set up locks:
     this will be run once per new duk_context/thread in server.c
     but needs to be done only once for all threads
  */
#ifndef SINGLETHREADED
  if(!lock_is_init)
  {
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        exit(1);
    }
#ifdef PUTMSG_STDERR
    if (pthread_mutex_init(&printlock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        exit(1);
    }
#endif
    lock_is_init=1;
  }
#endif

  /* Push constructor function */
  duk_push_c_function(ctx, duk_rp_sql_constructor, 3 /*nargs*/);

  /* Push object that will be Sql.prototype. */
  duk_push_object(ctx);  /* -> stack: [ Sql protoObj ] */

  /* Set Sql.prototype.exec. */
  duk_push_c_function(ctx, duk_rp_sql_exe, 4 /*nargs*/);  /* [ Sql proto fn_exe ] */
  duk_put_prop_string(ctx, -2, "exec");  /* [Sql protoObj-->[exe=fn_exe] ] */

  /* set Sql.prototype.eval */
  duk_push_c_function(ctx, duk_rp_sql_eval, 4 /*nargs*/); /*[Sql proto-->[exe=fn_exe] fn_eval ]*/
  duk_put_prop_string(ctx, -2, "eval"); /*[Sql protoObj-->[exe=fn_exe,eval=fn_eval] ]*/

  /* set Sql.prototype.close */
  duk_push_c_function(ctx, duk_rp_sql_close, 0 /*nargs*/); /* [Sql proto-->[exe=fn_exe] fn_close ] */
  duk_put_prop_string(ctx, -2, "close"); /*[Sql protoObj-->[exe=fn_exe,query=fn_exe,close=fn_close] ] */

  /* Set Sql.prototype = protoObj */
  duk_put_prop_string(ctx, -2, "prototype");  /* -> stack: [ Sql-->[prototype-->[exe=fn_exe,...]] ] */

  /* Finally, register Sql to the global object */
  duk_put_global_string(ctx, "Sql");  /* -> stack: [ ] */

  /* add ramis from above*/
  duk_ra_init(ctx);

  /* add utility function toBuffer() */
  duk_strToBuf_init(ctx);
}
