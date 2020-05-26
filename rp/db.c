#include <stdio.h>
#include <stdarg.h>  /* va_list etc */
#include <stddef.h>  /* size_t */
#include <limits.h>
#include <stdlib.h>
#include <pthread.h>
#include "rp.h"



#ifdef SINGLETHREADED

#define GLOCK
#define GUNLOCK
#define FLOCK
#define FUNLOCK

#else

pthread_mutex_t lock;

#ifdef PUTMSG_STDERR
pthread_mutex_t printlock;
#endif

#define FINELOCK

#ifdef FINELOCK

#ifdef DBUGLOCKS
#define FLOCK   printf("%d: locking from thread %lu\n",__LINE__ ,(unsigned long int)pthread_self());fflush(stdout);pthread_mutex_lock  (&lock);
#define FUNLOCK printf("%d: locking from thread %lu\n",__LINE__ ,(unsigned long int)pthread_self());fflush(stdout);pthread_mutex_unlock(&lock);
#else
#define FLOCK   pthread_mutex_lock  (&lock);
#define FUNLOCK pthread_mutex_unlock(&lock);
#endif

#define GLOCK
#define GUNLOCK

#else /* ifndef finelock */

#ifdef DBUGLOCKS
#define GLOCK printf(    "%d: locking from thread %lu\n",__LINE__ ,(unsigned long int)pthread_self());fflush(stdout);pthread_mutex_lock  (&lock);
#define GUNLOCK printf("%d: unlocking from thread %lu\n",__LINE__ ,(unsigned long int)pthread_self());fflush(stdout);pthread_mutex_unlock(&lock);
#else
#define GLOCK   pthread_mutex_lock  (&lock);
#define GUNLOCK pthread_mutex_unlock(&lock);
#endif

#define FLOCK
#define FUNLOCK

#endif /*ifdef finelock */

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

#define TEXIS_FETCH(a,b) ({\
  FLOCK\
  /* printf("texisfetch\n");*/\
  FLDLST *r=texis_fetch((a),(b));\
  FUNLOCK\
  r;\
})

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


int mal=0;
void free_handle(DB_HANDLE *h) {
    h->tx = TEXIS_CLOSE(h->tx);
    free(h->db);
    free(h->query);
    free(h);
    mal--;
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
    mal++;
    h->tx=newsql((char*)(d));
    h->db=strdup(d);
    h->query=strdup(q);
    h->next=NULL;
/*  moved
    if(!TEXIS_PREP(h->tx, h->query )) {
        free_handle(h);
        h=NULL;
    }
*/
    return(h);
}

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


/* LRU:
   add a handle to beginning of list
   count number of items on list.
   if one too many, evict last one
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
                last->next=h->next; /*remove item*/
                h->next=head;
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


/* **************************************************
     store an error string in this.lastErr 
   **************************************************   */
void duk_rp_log_error(duk_context *ctx,char *pbuf)
{
    duk_push_this(ctx);
    duk_push_string(ctx,pbuf);
#ifdef PUTMSG_STDERR
    if(strlen(pbuf)) {
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

/* **************************************************
  push a single field 
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
      (void) duk_get_global_string(ctx, "Date");
      duk_push_number(ctx,1000.0*(duk_double_t)*((ft_date*)fl->data[i]));
      duk_new(ctx,1);
      break;
    }
    case FTN_COUNTER:
    {
      unsigned char *p;

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

      /* DON'T create view  -- leave it for js */
      //duk_push_buffer_object(ctx,-1,0,fl->ndata[i],DUK_BUFOBJ_UINT8ARRAY);
      //printf("fl->ndata[i]=%d\n",fl->ndata[i]);
      /* object is now three back */
      //idx=-3;
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
int duk_rp_get_int_default(duk_context *ctx,int i,int def) {
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
                
        /* argument is a function */
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
            
            if (!strcasecmp("array",rt))
              q->retarray=1;
            if (!strcasecmp("arrayh",rt))
              q->retarray=2;
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
     an array of two values should be passed and processed here              
   ************************************************** */

int duk_rp_add_parameters(duk_context *ctx, TEXIS *tx, int arryi)
{
    int rc,arryn=0;

    /* array is at arryi. iterate over members */
    while(duk_has_prop_index(ctx,arryi,arryn)) /* arryi is where our array is on the duk ctx stack
                                                  arryn is the index of the array we are examining */
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
   fetch rows and push results to array 
   return number of rows 
   ************************************************** */
int duk_rp_fetch(duk_context *ctx,TEXIS *tx, QUERY_STRUCT *q) {
  int rown=0, i=0,
      resmax=q->max,
      retarray=q->retarray;
  FLDLST *fl;
    
  /* create return array (outer array) */
  duk_push_array(ctx);

  /* array of arrays requested */
  if (retarray)
  {
    /* push values into subarrays and add to outer array */
    while ((fl=TEXIS_FETCH(tx,-1)) && rown<resmax)
    {
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

    while ((fl=TEXIS_FETCH(tx,-1)) && rown<resmax)
    {
      duk_push_object(ctx);
      for (i = 0; i < fl->n; i++)
      {
        duk_rp_pushfield(ctx,fl, i);
        duk_put_prop_string(ctx,-2,(const char*)fl->name[i]);
      }
      duk_put_prop_index(ctx,-2,rown++);
    }
  }
  return(rown);
}

/* **************************************************
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

    while ( (fl=TEXIS_FETCH(tx,-1))  && rown<resmax) {
      duk_dup(ctx,callback);
      duk_push_this(ctx);

      /* array requested */
      if (retarray)
      {
        /* requesting first row to be column names*/
        if (retarray==2)
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

          duk_push_int(ctx,rown++);
          GUNLOCK
          duk_call_method(ctx,2); /* function(res,resnum){} */
          GLOCK
          retarray=1; /* give names to callback only once */

          /* if function returns false, exit while loop, return number of rows so far */
          if (duk_is_boolean(ctx,-1) && !duk_get_boolean(ctx,-1) ) {
            duk_pop(ctx);
            return (rown);
          }
          duk_pop(ctx);
        }

        duk_push_array(ctx);
        for (i = 0; i < fl->n; i++)
        {
          duk_rp_pushfield(ctx,fl,i);
          duk_put_prop_index(ctx,-2,i);
        }

        duk_push_int(ctx,rown++);
        GUNLOCK
        duk_call_method(ctx,2); /* function(res,resnum){} */
        GLOCK        
      }
      /* object requested */
      else
      {
        duk_push_object(ctx);
        for (i = 0; i < fl->n; i++) 
        {
          duk_rp_pushfield(ctx,fl,i);
          duk_put_prop_string(ctx,-2,(const char*)fl->name[i]);
        }
        duk_push_int(ctx,rown++);
        GUNLOCK
        duk_call_method(ctx,2); /* function(res,resnum){} */
        GLOCK
      }

      /* if function returns false, exit while loop, return number of rows so far */
      if (duk_is_boolean(ctx,-1) && !duk_get_boolean(ctx,-1) ) {
        duk_pop(ctx);
        return (rown);
      }

      /* get rid of ret value from callback*/
      duk_pop(ctx);  

    }/* while fetch */
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
  char pbuf[1024];

  *pbuf='\0';
  mmsgfh=fmemopen(pbuf, 1024, "w+");

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
  GLOCK
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
    if(hcache==NULL)
    {
      duk_push_string(ctx,"error in texis_prepare");
      duk_throw(ctx);
    }
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
    duk_rp_log_error(ctx,pbuf);
    duk_push_int(ctx,-1);
    duk_push_string(ctx,pbuf);
    duk_throw(ctx);
  }
  /* clear the sql.lastErr string */
  duk_rp_log_error(ctx,"");

  /* call parameters error, message is already pushed */
  if(q->err==QS_ERROR_PARAM) {
    return(1);
  }

  if(!TEXIS_PREP(tx, (char*)q->sql )) {
    duk_rp_log_error(ctx,pbuf);
    duk_push_int(ctx,-1);
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
    return 1;
  }

  /* sql parameters are the parameters corresponding to "?" in a sql statement
     and are provide by passing array in JS call parameters */

  /* add sql parameters */
  if(q->arryi !=-1)
    if(!duk_rp_add_parameters(ctx, tx, q->arryi))
    {
      duk_rp_log_error(ctx,pbuf);
      duk_push_int(ctx,-1);
#ifndef USEHANDLECACHE
      tx=TEXIS_CLOSE(tx);
#endif
      return(1);
    }

  if(!TEXIS_EXEC(tx))
  {
    GUNLOCK
    duk_rp_log_error(ctx,pbuf);
    duk_push_int(ctx,-1);
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
    return 1;
  }
  /* skip rows using texisapi */
  if(q->skip)
    TEXIS_SKIP(tx,q->skip);
  /* callback - return one row per callback */

  if (q->callback>-1) {
    int rows=duk_rp_fetchWCallback(ctx,tx,q);    
    duk_push_int(ctx,rows);
    GUNLOCK
    tx=TEXIS_CLOSE(tx);
    return (1); /* done with exec() */
  }

  /*  No callback, return all rows in array of objects */
  (void) duk_rp_fetch(ctx,tx,q);
  GUNLOCK
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif

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

//  TXsingleuser=1;
  if(!tx)
  {
    return((TEXIS*)NULL);
  }
  return tx;
}

/* **************************************************
   Sql('/database/path) constructor
   var sql=new Sql("/database/path:);

   There are x handle caches, one for each thread
   There is one handle cache for all new calls in this thread
   And there is one database per new Sql();

   Here we only check to see that the database exists and
   construct the js object.  Actual opening and caching
   of handles to the db is done in exec()
   ************************************************** */
duk_ret_t duk_rp_sql_constructor(duk_context *ctx) {

  TEXIS *tx;
  const char * db=duk_get_string(ctx,0);
  char pbuf[1024];
  int i=0;
  int makedb=0;

  *pbuf='\0';
  mmsgfh=fmemopen(pbuf, 1024, "w+");

  if (!duk_is_constructor_call(ctx)) {
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
      GLOCK
      FLOCK
      if(!createdb(db)){
        GUNLOCK
        FUNLOCK
        duk_rp_log_error(ctx,pbuf);
        duk_push_sprintf(ctx,"cannot create database at '%s' (root path not found, lacking permission or other error)", db);
        duk_throw(ctx);
      }
      FUNLOCK
      GUNLOCK
    }
    else
     tx=TEXIS_CLOSE(tx);
  }

  duk_push_this(ctx);  /* -> stack: [ db this ] */
  duk_push_string(ctx,db);
  duk_put_prop_string(ctx, -2, "db");

  /* Return undefined: default instance will be used. */
  return 0;
}

/* **************************************************
   Initialize Sql into global object. 
   ************************************************** */
void duk_db_init(duk_context *ctx) {

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

  /* Set Sql.prototype = proto */
  duk_put_prop_string(ctx, -2, "prototype");  /* -> stack: [ Sql-->[prototype-->[exe=fn_exe,...]] ] */

  /* Finally, register Sql to the global object */
  duk_put_global_string(ctx, "Sql");  /* -> stack: [ ] */

}
