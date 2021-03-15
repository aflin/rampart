/* Copyright (C) 2021 Aaron Flin - All Rights Reserved
 * You may use, distribute this code under the
 * terms of the Rampart Open Source License.
 * see rsal.txt for details
 */
#include <limits.h>
#include <stdlib.h>
#include <pthread.h>
#include <ctype.h>
#include <float.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "rampart.h"
#include "../../extern/lmdb/lmdb.h"

#define lock_main_ctx do{\
    if (ctx != main_ctx && pthread_mutex_lock(&ctxlock) == EINVAL){\
        fprintf( stderr, "could not obtain context lock\n");\
        exit(1);\
    }\
}while(0)

#define unlock_main_ctx do{\
    if(ctx != main_ctx)\
        pthread_mutex_unlock(&ctxlock);\
}while(0)

pthread_mutex_t lmdblock;

//int ltimes = 0;

#define mdblock do{\
    /*printf("lock set %d, line %d\n", ltimes, __LINE__);*/\
    /*ltimes++;*/\
    if (pthread_mutex_lock(&lmdblock) == EINVAL){\
        fprintf( stderr, "could not obtain lmdb lock\n");\
        exit(1);\
    }\
}while(0)

#define mdbunlock do{\
    pthread_mutex_unlock(&lmdblock);\
    /*ltimes--;*/\
    /*printf("lock free %d, line %d\n",ltimes, __LINE__);*/\
}while(0)




// conversion types
#define RP_LMDB_BUFFER 0
#define RP_LMDB_STRING 1
#define RP_LMDB_JSON   2
#define RP_LMDB_CBOR   3

/* check if a lmdb.transaction is open for writing */
static void check_txn(duk_context *ctx, const char *fname)
{
    int n;

        if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("writer_txn")))
        {
            if(duk_is_object(ctx, -1))
            {
                duk_get_prop_string(ctx, -1, "db");
                RP_THROW(ctx, "%s - error - A read/write transaction is already open for the %s database",
                                fname, duk_get_string(ctx, -1));
            }
        }
        duk_pop(ctx);
    return;

    //dead code, remove all txn_opens
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("txn_open"));
    n = (int)duk_get_int_default(ctx, -1, 0);
    duk_pop(ctx);
    if (n>0)
        RP_THROW(ctx, "%s: - cannot execute function while an lmdb.transaction is open", fname);
}

static int get_dbi_idx(duk_context *ctx, MDB_txn *txn, MDB_dbi *dbi, int flags, duk_idx_t dbi_idx, const char *fname)
{
    const char *db;

    if(duk_is_object(ctx, dbi_idx) && duk_has_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("dbi")) )
    {
        duk_get_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("dbi"));
        *dbi = (MDB_dbi) duk_get_int(ctx,-1);
        duk_pop(ctx);
        return 0;
    }

    db = REQUIRE_STRING(ctx, dbi_idx, "%s: parameter %d must be a string or dbi object", fname, (int)dbi_idx +1);

    return mdb_dbi_open(txn, db, flags, dbi);
}

/* 'this' must be on top of the stack */
static MDB_env *redo_env(duk_context *ctx, const char *dbpath, MDB_env *env, int openflags, size_t mapsize) {
    int rc;
    pid_t opening_pid=0;
    unsigned int oflags=0;

    if(openflags<0)
    {
        if (!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("openflags")))
            RP_THROW(ctx, "lmdb - unknown error getting environment flags");
            oflags = (unsigned int)duk_get_int(ctx, -1);
        duk_pop(ctx);
    }
    else
        oflags = (unsigned int)openflags;

    if(!mapsize)
    {
        if (!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("mapsize")))
            RP_THROW(ctx, "lmdb - unknown error getting mapsize");
        mapsize = (size_t)duk_get_int(ctx, -1);
        duk_pop(ctx);
    }

    if (env == NULL)
    {
        if (!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("env")))
            RP_THROW(ctx, "lmdb - unknown error getting environment");
        env = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }

    mdb_env_close(env);
    env=NULL;

    if(dbpath == NULL)
    {
        if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dbpath")) )
            RP_THROW(ctx, "lmdb - internal error in redo_env");
        dbpath=duk_get_string(ctx, -1);
        duk_pop(ctx);    
    }

    if(mdb_env_create(&env))
        RP_THROW(ctx, "lmdb.init - failed to create environment");

    opening_pid=getpid();

    mdb_env_set_mapsize(env, mapsize * 1048576);
    mdb_env_set_maxdbs(env, 256);

    if((rc=mdb_env_open(env, dbpath, oflags|MDB_NOTLS, 0644)))
    {
        mdb_env_close(env);
        RP_THROW(ctx, "lmdb.init - failed to open %s %s", dbpath, mdb_strerror(rc));
    }
    /* get the object with previously opened lmdb environments */
    if(!duk_get_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        /* doesn't exist, pop the undefined on the stack */
        duk_pop(main_ctx);
        /* make a new object to be our lmdbenvs */
        duk_push_object(main_ctx);
    }

    if(!duk_get_prop_string(main_ctx, -1, dbpath))
    {
        /* object for this path doesn't exist, pop undefined */
        duk_pop(main_ctx);
        /* make a new object for this path */
        duk_push_object(main_ctx);
    }

    duk_push_pointer(main_ctx, (void *) env);
    duk_put_prop_string(main_ctx, -2, "env");
    duk_push_int(main_ctx, (int) opening_pid);
    duk_put_prop_string(main_ctx, -2, "pid");

    duk_put_prop_string(main_ctx, -2, dbpath);

    /* put object in global as hidden symbol "lmdbenvs" */
    duk_put_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")); 
    unlock_main_ctx;

    duk_push_pointer(ctx, (void *) env);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("env")); 

    duk_push_int(ctx, (int)opening_pid );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pid")); 
    return env;
}

/* 'this' must be on top of the stack */
static MDB_env *check_env(duk_context *ctx)
{
    pid_t opening_pid;
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pid")))
        RP_THROW(ctx, "lmdb - internal error getting pid\n");
    opening_pid = (pid_t) duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(opening_pid != getpid() )
        return redo_env(ctx, NULL, NULL, -1, 0);

    return NULL;
}	

duk_ret_t duk_rp_lmdb_sync(duk_context *ctx)
{
    int rc;
    MDB_env *env;

    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    if(!(env=check_env(ctx))) // check if redo necessary, if so, return new env
    {

        if (!duk_get_prop_string(ctx, -1,  DUK_HIDDEN_SYMBOL("env")))
            RP_THROW(ctx, "lmdb - unknown error getting environment");
        env = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }
    rc=mdb_env_sync(env, 1);
    if(rc)
    {
        RP_THROW(ctx, "lmdb.sync - error: %s", mdb_strerror(rc));
    }

    return 0;
}

#define GETDBNAME \
const char *db;\
if(duk_is_string(ctx, 0))\
    db = duk_get_string(ctx, 0);\
else {\
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("db"));\
    db = duk_get_string(ctx, -1);\
}


duk_ret_t duk_rp_lmdb_drop(duk_context *ctx)
{
    int rc;
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;

    check_txn(ctx, "lmdb.drop");

    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    if(!(env=check_env(ctx))) // check if redo necessary, if so, return new env
    {

        if (! duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("env")) )
            RP_THROW(ctx, "lmdb.drop - unknown error getting environment");
        env = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }

    duk_pop(ctx);//this

    mdblock;
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if(rc)
    {
        mdbunlock;
        RP_THROW(ctx, "lmdb.drop - error beginning transaction - %s", mdb_strerror(rc));
    }

    rc = get_dbi_idx(ctx, txn, &dbi, MDB_CREATE, 0, "lmdb.drop");
    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        mdbunlock;
        RP_THROW(ctx, "lmdb.drop - error opening %s - %s", db, mdb_strerror(rc));
    }
    rc = mdb_drop(txn, dbi, 1);
    if(rc)
    {
        GETDBNAME
        mdb_dbi_close(env,dbi);
        mdb_txn_abort(txn);
        mdbunlock;
        RP_THROW(ctx, "lmdb.drop - error dropping %s - %s", db, mdb_strerror(rc));
    }
    rc = mdb_txn_commit(txn);
    mdb_dbi_close(env,dbi);
    if (rc)
    {
        GETDBNAME
        mdbunlock;
        RP_THROW(ctx, "lmdb.drop - error dropping db %s: (%d) %s\n", db, rc, mdb_strerror(rc));
    }
    mdbunlock;
    return 0;
}

duk_ret_t duk_rp_lmdb_put(duk_context *ctx)
{
    int rc, convtype=RP_LMDB_BUFFER;
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, val;
    const char *s=NULL;
    duk_size_t sz;

    check_txn(ctx, "lmdb.put");

    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    if(!(env=check_env(ctx))) // check if redo necessary, if so, return new env
    {
        if (! duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("env")) )
            RP_THROW(ctx, "lmdb.put - unknown error getting environment");
        env = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }

    if( duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("convtype")) )
        convtype=duk_get_int(ctx, -1);

    duk_pop(ctx);//this

    if(!duk_is_object(ctx,1))
    {
        key.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, 1, &sz, "lmdb.put - second parameter must be an object (key:value pairs) or a string or buffer (key)");
        key.mv_size=(size_t)sz;

        if(duk_is_undefined(ctx, 2))
        {
            duk_push_null(ctx);
            duk_replace(ctx, 2);
        }

        if(convtype == RP_LMDB_JSON)
        {
            s=duk_json_encode(ctx, 2);
            val.mv_data = (void*)s;
            val.mv_size = strlen(s);
        }
        else if(convtype == RP_LMDB_CBOR)
        {
            duk_cbor_encode(ctx, 2, 0);
            s=(const char *) duk_get_buffer_data(ctx, 2, &sz);
            val.mv_data = (void*)s;
            val.mv_size = (size_t)sz;
        }
        else
        {
            s = REQUIRE_STR_OR_BUF(ctx, 2, &sz, "lmdb.put - third parameter must be a Buffer or String (unless conversion is set to JSON or CBOR in Lmdb.init)");
            val.mv_data = (void*)s;
            val.mv_size = (size_t)sz;
        }

        mdblock;
        rc = mdb_txn_begin(env, NULL, 0, &txn);
        if(rc)
        {
            mdbunlock;
            RP_THROW(ctx, "lmdb.put - error beginning transaction - %s", mdb_strerror(rc));
        }

        rc = get_dbi_idx(ctx, txn, &dbi, MDB_CREATE, 0, "lmdb.put");

        if(rc)
        {
            GETDBNAME
            mdb_txn_abort(txn);
            mdbunlock;
            RP_THROW(ctx, "lmdb.put - error opening %s - %s", db, mdb_strerror(rc));
        }
        rc = mdb_put(txn, dbi, &key, &val, 0);

        if(rc)
        {
            mdb_txn_abort(txn);           
            mdbunlock;
            RP_THROW(ctx, "lmdb.put failed - %s", mdb_strerror(rc));
        }

        rc = mdb_txn_commit(txn);
        if(rc)
        {
            mdb_txn_abort(txn);           
            mdbunlock;
            RP_THROW(ctx, "lmdb.put failed to commit - %s", mdb_strerror(rc));
        }

        mdbunlock;
        return 0;
    }


    mdblock;
    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if(rc)
    {
        mdbunlock;
        RP_THROW(ctx, "lmdb.put - error beginning transaction - %s", mdb_strerror(rc));
    }

    rc = get_dbi_idx(ctx, txn, &dbi, MDB_CREATE, 0, "lmdb.put");

    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        mdbunlock;
        RP_THROW(ctx, "lmdb.put - error opening %s - %s", db, mdb_strerror(rc));
    }


    duk_enum(ctx, 1, 0);
    while (duk_next(ctx, -1, 1))
    {
        
        if(duk_is_undefined(ctx, -1))
        {
            duk_push_null(ctx);
            duk_replace(ctx, -2);
        }

        if(convtype == RP_LMDB_JSON)
        {
            s=duk_json_encode(ctx, -1);
            val.mv_data = (void*)s;
            val.mv_size = strlen(s);
        }
        else if(convtype == RP_LMDB_CBOR)
        {
            duk_cbor_encode(ctx, -1, 0);
            s=(const char *) duk_get_buffer_data(ctx, -1, &sz);
            val.mv_data = (void*)s;
            val.mv_size = (size_t)sz;
        }
        else
        {
            if(duk_is_buffer_data(ctx, -1))
                s=(const char *) duk_get_buffer_data(ctx, -1, &sz);
            else if (duk_is_string(ctx, -1))
                s=duk_get_lstring(ctx, -1, &sz);
            else
            {
                mdb_txn_abort(txn);           
                mdbunlock;
                RP_THROW(ctx, "lmdb.put - Value to store must be a Buffer or String (unless conversion is set to JSON or CBOR in Lmdb.init)"); 
            }
            val.mv_data = (void*)s;
            val.mv_size = (size_t)sz;
        }
       
        s = duk_get_string(ctx, -2);
        key.mv_data=(void*)s;
        key.mv_size=strlen(s);

	rc = mdb_put(txn, dbi, &key, &val, 0);
	if(rc)
	{
            mdb_txn_abort(txn);
            mdbunlock;
	    RP_THROW(ctx, "lmdb - put failed - %s", mdb_strerror(rc));
	}
	duk_pop_2(ctx);
    }

    rc = mdb_txn_commit(txn);

    if (rc)
    {
        mdb_txn_abort(txn);
        mdbunlock;
        RP_THROW(ctx, "lmdb.put - put failed to commit %s\n",  mdb_strerror(rc));
    }
    mdbunlock;
    return 0;
}

#define pushkey do{\
    duk_push_lstring(ctx, key.mv_data, (duk_size_t)key.mv_size);\
} while(0)

#define pushval do{\
    switch(convtype)\
    {\
            case RP_LMDB_BUFFER:\
            {\
                void *b = duk_push_fixed_buffer(ctx, (duk_size_t)data.mv_size);\
                memcpy(b, data.mv_data, data.mv_size);\
                break;\
            }\
            case RP_LMDB_JSON:\
                duk_get_global_string(ctx,"JSON");\
                duk_get_prop_string(ctx, -1, "parse");\
                duk_remove(ctx, -2);\
                duk_push_lstring(ctx, data.mv_data, (duk_size_t)data.mv_size);\
                if(duk_pcall(ctx, 1) == DUK_EXEC_ERROR)\
                {\
                    mdb_cursor_close(cursor);\
                    mdb_txn_abort(txn);\
                    RP_THROW(ctx, "%s - error parsing JSON value", fname);\
                }\
                break;\
            case RP_LMDB_CBOR:\
            {\
                void *b = duk_push_fixed_buffer(ctx, (duk_size_t)data.mv_size);\
                memcpy(b, data.mv_data, data.mv_size);\
                duk_cbor_decode(ctx, -1, 0);\
                break;\
            }\
            case RP_LMDB_STRING:\
                duk_push_lstring(ctx, data.mv_data, (duk_size_t)data.mv_size);\
                break;\
    }\
} while(0)

/* get count from MDB_stat */
duk_ret_t duk_rp_lmdb_get_count(duk_context *ctx)
{
    int rc;
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat stat;
    const char *fname = "lmdb.getCount";

    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    if(!(env=check_env(ctx))) // check if redo necessary, if so, return new env
    {
        if (!duk_get_prop_string(ctx, -1,  DUK_HIDDEN_SYMBOL("env")))
            RP_THROW(ctx, "%s - unknown error getting environment", fname);
        env = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }

    rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        RP_THROW(ctx, "%s - error beginning transaction - %s", fname, mdb_strerror(rc));
    }

    rc = get_dbi_idx(ctx, txn, &dbi, 0, 0, fname);
    if(rc == MDB_NOTFOUND)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error opening database - database does not exist", fname);
    }
    else if(rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error opening database - %s", fname, mdb_strerror(rc));
    }    

    rc = mdb_stat(txn, dbi, &stat);

    if(rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error getting database item count - %s", fname, mdb_strerror(rc));
    }    
    
    mdb_txn_commit(txn);
    duk_push_int(ctx, (int) stat.ms_entries);
    return 1;
}

static duk_ret_t get_del(duk_context *ctx, int del, int retvals)
{
    int rc, max=-1, convtype=RP_LMDB_BUFFER;
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_cursor *cursor;
    MDB_val key, data;
    int items=0, glob=0;
    const char *s=NULL;
    const char *endstr=NULL;
    const char *fname = "lmdb.get";

#define errexit do {\
    mdb_cursor_close(cursor);\
    mdb_txn_abort(txn);\
    if(del) mdbunlock;\
    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));\
} while(0)

    if(del)
    {
        fname="lmdb.del";
        check_txn(ctx, fname);
    }

    if (!duk_is_string(ctx, 1))
        RP_THROW(ctx, "%s - second parameter must be a string (keys to retrieve)", fname);


    if (duk_is_number(ctx,2))
    {
        items=duk_get_int(ctx, 2);
    }
    else if (duk_is_string(ctx, 2))
    {
        endstr=duk_get_string(ctx, 2);
        if(!strcmp(endstr,"*"))
            glob=1;
    }

    if (endstr && duk_is_number(ctx,3))
    {
        max=duk_get_int(ctx, 3);
        if(max <1) RP_THROW(ctx, "%s - fourth parameter must be a number greater than 0 (maximum number of items to retrieve)", fname);
    }
    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    if(!(env=check_env(ctx))) // check if redo necessary, if so, return new env
    {
        if (!duk_get_prop_string(ctx, -1,  DUK_HIDDEN_SYMBOL("env")))
            RP_THROW(ctx, "%s - unknown error getting environment", fname);
        env = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }

    if( duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("convtype")) )
        convtype=duk_get_int(ctx, -1);
    duk_pop(ctx);

    duk_pop(ctx); //this
    
    s = duk_get_string(ctx, 1);
    key.mv_data=(void*)s;
    key.mv_size=strlen(s);    


    if(del)
    {
        mdblock;
        rc = mdb_txn_begin(env, NULL, 0, &txn);
    }
    else
        rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        mdbunlock;
        RP_THROW(ctx, "%s - error beginning transaction - %s", fname, mdb_strerror(rc));
    }

    if(del)
        rc = get_dbi_idx(ctx, txn, &dbi, MDB_CREATE, 0, fname);
    else
    {
        rc = get_dbi_idx(ctx, txn, &dbi, 0, 0, fname);
        if(rc == MDB_NOTFOUND)
        {
            mdb_txn_abort(txn);
            RP_THROW(ctx, "%s - error opening database - database does not exist", fname);
        }
    }
    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        mdbunlock;
        RP_THROW(ctx, "%s - error opening %s - %s", db, fname, mdb_strerror(rc));
    }    

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        mdbunlock;
        RP_THROW(ctx, "%s - error opening cursor%s - %s", db, fname, mdb_strerror(rc));
    }    

    /* handle glob case */
    if(glob)
    {
        int len=strlen(s);

        if(key.mv_size)
            rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
        else
            rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);

        if(rc == MDB_NOTFOUND)
        {
            mdb_cursor_close(cursor);
            mdb_txn_abort(txn);
            mdbunlock;
            return 1;
        }
        else if (rc)
            errexit;

        if(del)
        {
            rc = mdb_cursor_del(cursor, 0);
            if(rc)
                errexit;
        }

        if(!retvals)
        {
            while(1)
            {
                if(max>0)
                {
                    max--;
                    if(!max)
                        break;
                }
                rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
                if(rc == MDB_NOTFOUND) 
                    break;
                else if(rc)
                    errexit;
                if(strncmp(s, key.mv_data, len))
                    break;
                //if(del) currently always true if retvals is 0
                //{
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        errexit;
                //}
            }
            mdb_cursor_close(cursor);
            mdb_txn_commit(txn);
            mdbunlock;
            return 0;
        }

        // implicit if (retvals)
        duk_push_object(ctx);
        pushkey;
        pushval;
        duk_put_prop(ctx, -3);

        while(1)
        {
            if(max>0)
            {
                max--;
                if(!max)
                    break;
            }
            rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
            if(rc == MDB_NOTFOUND) 
                break;
            else if(rc) 
                errexit;
            if(strncmp(s, key.mv_data, len))
                break;
            if(del)
            {
                rc = mdb_cursor_del(cursor, 0);
                if(rc)
                    errexit;
            }
            pushkey;
            pushval;
            duk_put_prop(ctx, -3);
        }

        mdb_cursor_close(cursor);
        mdb_txn_commit(txn);
        mdbunlock;
        return 1;
    }

    /* non glob cases - start by retrieving the key as given */

    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET);
    if(rc == MDB_NOTFOUND)
    {
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        mdbunlock;
        /* return empty object if we asked for more than a single */
        if(items || endstr)
        {
            duk_push_object(ctx);
            return 1;
        }
        /* otherwise returned undefined */
        return 0;
    }
    else if (rc)
        errexit;

    if(del)
    {
        /* found and delete requested, so delete */
        rc = mdb_cursor_del(cursor, 0);
        if(rc)
            errexit;
    }

    /* now check for an end string or a range number */ 
    if(retvals)
    {
        pushval;

        if(items)
        {
            int i=1, flag=MDB_NEXT;
            if(items<0)
            {
                flag = MDB_PREV;
                items *= -1;
            }
            duk_push_object(ctx);
            pushkey;
            duk_pull(ctx, -3);
            duk_put_prop(ctx, -3);
            while(i<items)
            {
                rc = mdb_cursor_get(cursor, &key, &data, flag);
                if(rc == MDB_NOTFOUND) 
                    break;
                else if(rc)
                    errexit;
                if(del)
                {
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        errexit;
                }
                pushkey;
                pushval;
                duk_put_prop(ctx, -3);
                i++;                
            }
        }
        else if (endstr)
        {
            int flag=MDB_NEXT, direction=0;

            duk_push_object(ctx);
            pushkey;
            duk_pull(ctx, -3);
            duk_put_prop(ctx, -3);
            direction = strcmp(endstr,s);
            if (direction<0)
                flag = MDB_PREV;            

            while(1)
            {
                if(max>0)
                {
                    max--;
                    if(!max)
                        break;
                }
                rc = mdb_cursor_get(cursor, &key, &data, flag);
                if(rc == MDB_NOTFOUND) 
                    break;
                else if(rc)
                    errexit;

                if(direction * strncmp(endstr, key.mv_data, key.mv_size) < 0)
                    break;
                if(del)
                {
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        errexit;
                }
                pushkey;
                pushval;
                duk_put_prop(ctx, -3);
            }
        }
    }
    else
    {
        /* deleting without returning values */
        if(items)
        {
            int i=1, flag=MDB_NEXT;
            if(items<0)
            {
                flag = MDB_PREV;
                items *= -1;
            }
            while(i<items)
            {
                rc = mdb_cursor_get(cursor, &key, &data, flag);
                if(rc == MDB_NOTFOUND) 
                    break;
                else if(rc)
                    errexit;
                //if(del) currently can only ask for no retval from del
                //{
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        errexit;
                //}
                i++;                
            }
        }
        else if (endstr)
        {
            int flag=MDB_NEXT, direction=0;

            direction = strcmp(endstr,s);
            if (direction<0)
                flag = MDB_PREV;            

            while(1)
            {
                if(max>0)
                {
                    max--;
                    if(!max)
                        break;
                }
                rc = mdb_cursor_get(cursor, &key, &data, flag);
                if(rc == MDB_NOTFOUND) 
                    break;
                else if(rc)
                    errexit;
                if(direction * strncmp(endstr, key.mv_data, key.mv_size) < 0)
                    break;
                if(del)
                {
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        errexit;
                }
            }
        }
    }

    mdb_cursor_close(cursor);
    mdb_txn_commit(txn);
    mdbunlock;
    return (duk_ret_t)retvals;
}

duk_ret_t duk_rp_lmdb_get(duk_context *ctx)
{
    return get_del(ctx, 0, 1);
}

duk_ret_t duk_rp_lmdb_del(duk_context *ctx)
{
    duk_idx_t i=2;
    int retvals=0;
    for(;i<5;i++)
    {
        if(duk_is_boolean(ctx, i))
        {
            retvals=duk_get_boolean(ctx, i);
            duk_remove(ctx, i);
            break;
        }
    }
    return get_del(ctx, 1, retvals);
}

int rp_mkdir_parent(const char *path, mode_t mode);

int duk_rp_lmdb_exitset = 0;

MDB_env **all_env;

extern rp_vfunc duk_rp_lmdb_free_all_env;

static void free_all_env(void)
{
    int i=0;
    MDB_env *env;

    while( (env = all_env[i]) )
    {
        mdb_env_close(env);
        i++;
    }
    free(all_env);
}

duk_ret_t duk_rp_lmdb_cleanup(duk_context *ctx)
{
    ctx = main_ctx;
    int n=0;

    if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        duk_enum(ctx, -1, 0);
        while (duk_next(ctx, -1, 1))
        {
            duk_get_prop_string(ctx, -1, "env");
            MDB_env *env = (MDB_env *) duk_get_pointer(ctx, -1);
            duk_pop(ctx);
            mdb_env_sync(env, 1);

            /*
                cannot free env when finalizers for transactions haven't run
                and they won't be run until ctx is destroyed. So we need to
                copy them out and free later.                                  */
            REMALLOC(all_env, (n+1) * sizeof(MDB_env *));
            all_env[n]=env;
            n++;
            //mdb_env_close(env);

            duk_pop_2(ctx);
        }
        REMALLOC(all_env, (n+1) * sizeof(MDB_env *));
        all_env[n]=NULL;
        duk_rp_lmdb_free_all_env = free_all_env;        
    }
    return 0;
}

/* Cursor Get Operations, stored in new Lmdb.init() below */

#define pushcop(name,type) do{\
    duk_push_string(ctx, (name));\
    duk_push_uint(ctx, (type));\
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE |DUK_DEFPROP_CLEAR_WC);\
} while(0);

/* this must be on top of stack */
static void push_cursor_ops(duk_context *ctx)
{
    pushcop("op_first",    MDB_FIRST);
    pushcop("op_current",  MDB_GET_CURRENT);
    pushcop("op_last",     MDB_LAST);
    pushcop("op_next",     MDB_NEXT);
    pushcop("op_prev",     MDB_PREV);
    pushcop("op_set",      MDB_SET);
    pushcop("op_setRange", MDB_SET_RANGE);
}


/* **************************************************
   Lmdb.init("/database/path") constructor:

   var lmdb=new Lmdb.init("/database/path");
   var lmdb=new Lmdb.init("/database/path",true); //create db if not exists

   ************************************************** */
duk_ret_t duk_rp_lmdb_constructor(duk_context *ctx)
{
    MDB_env *env;
    int rc, envopened=0;
    const char *dbpath=NULL;
    pid_t opening_pid = 0;
    size_t mapsize=16;
    unsigned int openflags=0;        
    int convtype=RP_LMDB_BUFFER;
    int maxdbs=256;

    duk_idx_t obj_idx=-1, bool_idx=-1, str_idx = -1, i=0;

    if(!main_ctx)
        main_ctx=ctx;

    /* allow call to init() with "new Lmdb.init()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }
    /* a function to be run when duktape exits */
    if(!duk_rp_lmdb_exitset)
    {
        /* in pseudo js:
            globalstash.exitfuncs.push(duk_rp_lmdb_cleanup);
        */
        duk_uarridx_t len=0;
        duk_push_global_stash(main_ctx);
        duk_get_prop_string(main_ctx, -1, "exitfuncs");
        len = duk_get_length(main_ctx, -1);
        duk_push_c_function(main_ctx, duk_rp_lmdb_cleanup,0);
        duk_put_prop_index(main_ctx, -2, len);
        duk_pop_2(main_ctx);
        duk_rp_lmdb_exitset=1;
    }
    /* 
     if init("/db/path",true), we will 
     create the db if it does not exist 
  */

    for (i=0;i<3;i++)
    {
        if(duk_is_string(ctx, i))
        {
            if(str_idx > -1)
                RP_THROW(ctx,"Lmdb.init - only one parameter can be a String");
            str_idx=i;
        }
        else if(duk_is_boolean(ctx, i))
        {
            if(bool_idx > -1)
                RP_THROW(ctx,"Lmdb.init - only one parameter can be a Boolean");
            bool_idx=i;
        }
        else if (duk_is_object(ctx, i) && !duk_is_array(ctx, i) && !duk_is_function(ctx, i))
        {
            if(obj_idx > -1)
                RP_THROW(ctx,"Lmdb.init - only one parameter can be an Object");
            obj_idx=i;
        }
    }

    if(str_idx > -1)
        dbpath = duk_get_string(ctx, str_idx);
    else
        RP_THROW(ctx, "new Lmdb.init() requires a string (database environment location) for one of its parameters" );

    if (bool_idx>-1 && duk_get_boolean(ctx, 1) != 0)
    {
        mode_t mode=0755;
        int ret = rp_mkdir_parent(dbpath, mode);
        if(ret==-1)
            RP_THROW(ctx, ": lmdb.init - error creating directory: %s", strerror(errno));
    }

#define check_set_flag(propname, flagname) do{\
    if(duk_get_prop_string(ctx, obj_idx, propname)){\
        if(duk_is_boolean(ctx, -1)) {\
            if(duk_get_boolean(ctx, -1))\
                openflags |= flagname;\
        }\
        else RP_THROW(ctx,"lmdb.init - option %s must be a boolean", propname);\
    }\
    duk_pop(ctx);\
} while(0)

    if(obj_idx > -1)
    {

        check_set_flag("noMetaSync", MDB_NOMETASYNC);
        check_set_flag("noSync", MDB_NOSYNC);
        check_set_flag("mapAsync", MDB_MAPASYNC); 
        check_set_flag("noReadAhead", MDB_NORDAHEAD);
        check_set_flag("writeMap", MDB_WRITEMAP);
        
        if(duk_get_prop_string(ctx, obj_idx, "mapSize"))
        {
            mapsize = (size_t) REQUIRE_INT(ctx, -1, "lmdb.init - option mapSize must be an integer (map/db size in Mb)\n");
        }
        duk_pop(ctx);
        if(duk_get_prop_string(ctx, obj_idx, "conversion"))
        {
            const char *conv = REQUIRE_STRING(ctx, -1, "lmdb.init - option conversion must be a string('JSON'|'CBOR'|'String'|'Buffer')");
            if(!strcasecmp(conv,"json"))
                convtype=RP_LMDB_JSON;
            else if(!strcasecmp(conv,"cbor"))
                convtype=RP_LMDB_CBOR;
            else if(!strcasecmp(conv,"string"))
                convtype=RP_LMDB_STRING;
            else if(!strcasecmp(conv,"buffer"))
                convtype=RP_LMDB_BUFFER;
            else
                RP_THROW(ctx, "lmdb.init - option conversion must be one of 'JSON', 'CBOR', 'String' or 'Buffer'");
        }
        if (duk_get_prop_string(ctx, obj_idx, "maxDbs"))
        {
            maxdbs=REQUIRE_UINT(ctx, -1, "lmdb.init - option maxDbs must be a positive number (Max number of named databases)");
            maxdbs++; //apparently this included the default unnamed db, despite the docs.
            duk_pop(ctx);
        }
    }

    /* search for existing env for this db, from any thread */
    lock_main_ctx;
    if(duk_get_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        if(duk_get_prop_string(main_ctx, -1, dbpath))
        {
            if(duk_get_prop_string(main_ctx, -1, "env"))
            {
                env = (MDB_env *) duk_get_pointer(main_ctx, -1);
                envopened=1;
            }
            duk_pop(main_ctx);

            if(duk_get_prop_string(main_ctx, -1, "openflags"))
            {
                unsigned int newopenflags = openflags;

                openflags = (unsigned int) duk_get_int(main_ctx, -1);
                /* if writemap setting not the same between old and new */
                if( (openflags & MDB_WRITEMAP) ^ (newopenflags & MDB_WRITEMAP) )
                    RP_THROW(ctx, "lmdb.init - Not Allowed - the database was already opened with a different writeMap setting");
            }
            duk_pop(main_ctx);

            if(duk_get_prop_string(main_ctx, -1, "pid"))
            {
                opening_pid = (pid_t) duk_get_int(main_ctx, -1);
                if(opening_pid != getpid())
                {
                    duk_push_this(ctx);
                    env = redo_env(ctx, dbpath, env, (int)openflags, mapsize);
                    opening_pid=getpid();
                    duk_pop_2(ctx);
                }
            }
            duk_pop(main_ctx);
        }
        duk_pop(main_ctx);
    }
    duk_pop(main_ctx);
    unlock_main_ctx;

    if(!envopened)
    {
        if(mdb_env_create(&env))
            RP_THROW(ctx, "lmdb.init - failed to create envirnoment");

        opening_pid=getpid();

        mdb_env_set_maxdbs(env, maxdbs);

        mdb_env_set_mapsize(env, mapsize*1048576);

        if((rc=mdb_env_open(env, dbpath, openflags|MDB_NOTLS, 0644)))
        {
            mdb_env_close(env);
            RP_THROW(ctx, "lmdb.init - failed to open %s %s", dbpath, mdb_strerror(rc));
        }
        /* get the object with previously opened lmdb environments and options, 
           all in an object indexed by path */
        lock_main_ctx;
        if(!duk_get_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
        {
            /* doesn't exist, pop the undefined on the stack */
            duk_pop(main_ctx);
            /* make a new object to be our lmdbenvs */
            duk_push_object(main_ctx);
        }
        if(!duk_get_prop_string(main_ctx, -1, dbpath))
        {
            /* object for this path doesn't exist, pop undefined */
            duk_pop(main_ctx);
            /* make a new object for this path */
            duk_push_object(main_ctx);
        }

        duk_push_pointer(main_ctx, (void *) env);
        duk_put_prop_string(main_ctx, -2, "env");
        duk_push_int(main_ctx, (int) opening_pid);
        duk_put_prop_string(main_ctx, -2, "pid");
        duk_push_int(main_ctx, (int) openflags);
        duk_put_prop_string(main_ctx, -2, "openflags");
        duk_push_int(main_ctx, (int) mapsize);
        duk_put_prop_string(main_ctx, -2, "mapsize");

        duk_put_prop_string(main_ctx, -2, dbpath);

        /* put object in global as hidden symbol "lmdbenvs" */
        duk_put_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")); 
        unlock_main_ctx;
    }

    /* save the name of the database in 'this' */
    duk_push_this(ctx); /* -> stack: [ db this ] */

    duk_push_string(ctx, dbpath);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("dbpath"));

    duk_push_int(ctx, (int)getpid() );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pid")); 

    duk_push_int(ctx, (int)openflags );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("openflags")); 

    duk_push_int(ctx, (int)mapsize );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("mapsize")); 

    duk_push_int(ctx, (int)convtype );
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("convtype")); 

    duk_push_pointer(ctx, (void *) env);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("env")); 

    duk_get_prop_string(ctx, -1, "transaction");
    duk_pull(ctx, -2);//this
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("self"));

    return 0;
}

/* low level txn functions */

static MDB_txn * get_txn(duk_context *ctx, duk_idx_t this_idx)
{
    MDB_txn * txn;

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("txn"));
    txn= duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(txn == NULL)
        RP_THROW(ctx, "transaction - transaction has already been closed (or other error)");

    return txn;
}

/* 
    get_dbi must be called before getting
    any parameters, because it checks idx 0
    for the dbi object, and removes it if there.
*/

static MDB_dbi get_dbi(duk_context *ctx, duk_idx_t this_idx)
{
    MDB_dbi dbi;

    if(duk_is_object(ctx,0) && duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi")) )
    {
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi"));
        dbi = (MDB_dbi) duk_get_int(ctx,-1);
        duk_pop(ctx);
        duk_remove(ctx, 0);
        return dbi;
    }

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("dbi"));
    dbi = (MDB_dbi) REQUIRE_NUMBER(ctx, -1, "transaction - Error retrieving opened database");
    duk_pop(ctx);
    return dbi;
}

/* this must be on top of stack */
static void clean_txn(duk_context *ctx, MDB_txn *txn, int commit)
{
    MDB_cursor *cursor;
    int n_txn, rc=0;

    if(commit)
    {
        rc = mdb_txn_commit(txn);
    }
    else
        mdb_txn_abort(txn);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("iswriter")))
    {
        if (rc)
        {
            mdb_txn_abort(txn);
            mdbunlock;
            RP_THROW(ctx, "transaction.commit - error committing data: (%d) %s\n", rc, mdb_strerror(rc));
        }
        mdbunlock;
        duk_del_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("iswriter"));
        duk_push_undefined(ctx);
        duk_put_global_string (ctx, DUK_HIDDEN_SYMBOL("writer_txn"));
    }
    duk_pop(ctx);//true or undefined

    if (rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "transaction.commit - error committing data: (%d) %s\n", rc, mdb_strerror(rc));
    }


    /* txn is complete and invalid. Mark it as such. */
    duk_push_pointer(ctx, (void*)NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("txn"));

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("cursors"));
    duk_enum(ctx, -1, 0);
    while(duk_next(ctx, -1, 1))
    {
        cursor = (MDB_cursor *)duk_get_pointer(ctx, -1);
        mdb_cursor_close(cursor);
        duk_pop_2(ctx);
    }
    duk_pop_2(ctx);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("txn_open"));
    n_txn = (int)duk_get_int_default(ctx, -1, 0) - 1;
    duk_pop(ctx);
    duk_push_int(ctx, (n_txn<0) ? 0 : n_txn);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("txn_open"));
}


/* if finalizer, 'this' is automatically on top of the stack */
duk_ret_t duk_rp_lmdb_ll_abort_(duk_context *ctx)
{
    MDB_txn *txn = get_txn(ctx, -1);

    clean_txn(ctx, txn, 0);

    return 0;
}

duk_ret_t duk_rp_lmdb_ll_abort(duk_context *ctx)
{
    duk_push_this(ctx);

    return duk_rp_lmdb_ll_abort_(ctx);
}

/* if finalizer, 'this' is automatically on top of the stack */
duk_ret_t duk_rp_lmdb_ll_commit_(duk_context *ctx)
{
    MDB_txn *txn = get_txn(ctx, -1);

    clean_txn(ctx, txn, 1);

    return 0;
}

duk_ret_t duk_rp_lmdb_ll_commit(duk_context *ctx)
{
    duk_push_this(ctx);

    return duk_rp_lmdb_ll_commit_(ctx);
}

duk_ret_t duk_rp_lmdb_ll_put(duk_context *ctx)
{
    int rc;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, val;
    duk_size_t sz;

    duk_push_this(ctx);
    txn = get_txn(ctx, -1);
    dbi = get_dbi(ctx, -1);
    if(!duk_is_object(ctx,0))
    {
        if(duk_is_object(ctx, 1))
            duk_cbor_encode(ctx, 1, 0);

        key.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, 0, &sz, "transaction.put - first parameter must be a String or Buffer (key)");
        key.mv_size=(size_t)sz;

        val.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, 1, &sz, "transaction.put - second parameter must be a String, Buffer or Object (value)");
        val.mv_size=(size_t)sz;

        rc = mdb_put(txn, dbi, &key, &val, 0);
        if(rc)
        {
            RP_THROW(ctx, "transaction.put failed - %s", mdb_strerror(rc));
        }
        return 0;
    }
    else
    {
        duk_enum(ctx, 0,0);
        while (duk_next(ctx, -1, 1))
        {
            const char *s=NULL;
            
            if(duk_is_object(ctx, -1))
                duk_cbor_encode(ctx, -1, 0);

            s = (const char *)REQUIRE_STR_OR_BUF(ctx, -1, &sz, "transaction.put - Value to store must be a Buffer, String or Object");
            val.mv_data = (void*)s;
            val.mv_size = (size_t)sz;
           
            s = duk_get_string(ctx, -2);
            key.mv_data = (void*)s;
            key.mv_size = strlen(s);

            rc = mdb_put(txn, dbi, &key, &val, 0);
            if(rc)
            {
                RP_THROW(ctx, "transaction.put failed - %s", mdb_strerror(rc));
            }
            duk_pop_2(ctx);
        }
    }

    return 0;
}

/* get a single value from the db */
duk_ret_t duk_rp_lmdb_ll_get(duk_context *ctx)
{
    int rc;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, val;
    duk_size_t ksz;

    duk_push_this(ctx);
    txn = get_txn(ctx, -1);
    dbi = get_dbi(ctx, -1);

    key.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, 0, &ksz, "transaction.get - first parameter must be a string or buffer (key)");
    key.mv_size=(duk_size_t)ksz;


    rc = mdb_get(txn, dbi, &key, &val);

    if(rc == MDB_NOTFOUND)
        return 0;

    if(rc)
    {
        RP_THROW(ctx, "transaction.get failed - %s", mdb_strerror(rc));
    }

    if(duk_get_boolean_default(ctx, 1, 0))
    {
        duk_push_lstring(ctx, (const char*) val.mv_data, (duk_size_t)val.mv_size);
    }
    else
    {
        void *data=duk_push_fixed_buffer(ctx, (duk_size_t)val.mv_size);
        memcpy(data, val.mv_data, val.mv_size);
    }
    return 1;
}

/* delete a single value from the db */
duk_ret_t duk_rp_lmdb_ll_del(duk_context *ctx)
{
    int rc;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key;
    duk_size_t ksz;

    duk_push_this(ctx);
    txn = get_txn(ctx, -1);
    dbi = get_dbi(ctx, -1);

    key.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, 0, &ksz, "transaction.del - first parameter must be a string or buffer (key)");
    key.mv_size=(duk_size_t)ksz;


    rc = mdb_del(txn, dbi, &key, NULL);

    if(rc == MDB_NOTFOUND)
    {
        duk_push_false(ctx);
        return 1;
    }

    if(rc)
    {
        RP_THROW(ctx, "transaction.del failed - %s", mdb_strerror(rc));
    }

    duk_push_true(ctx);
    return 1;
}

inline static MDB_cursor *get_cursor(duk_context *ctx, MDB_dbi dbi, duk_idx_t this_idx)
{
    MDB_cursor *cursor;

    this_idx = duk_normalize_index(ctx, this_idx);

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("cursors"));
    duk_push_int(ctx, (int)dbi);
    duk_get_prop(ctx, -2);

    cursor = (MDB_cursor *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!cursor)
    {
        int rc;
        MDB_txn *txn = get_txn(ctx, this_idx);

        rc = mdb_cursor_open(txn, dbi, &cursor);
        if(rc)
            RP_THROW(ctx, "transaction - error opening new cursor");

        duk_push_int(ctx, (int)dbi);
        duk_push_pointer(ctx, (void*)cursor);
        duk_put_prop(ctx, -3);
    }
    duk_pop(ctx);
    return cursor;
}

duk_ret_t duk_rp_lmdb_cursor_get(duk_context *ctx)
{
    MDB_val val={0}, key = {0};
    duk_size_t ksz;
    int op, rc, key_is_string=0, val_is_string=0;
    duk_idx_t val_idx, obj_idx, nxt_idx;
    MDB_dbi dbi;
    MDB_cursor *cursor;

    duk_push_this(ctx);
    dbi = get_dbi(ctx, -1);
    cursor = get_cursor(ctx, dbi, -1);

    op = REQUIRE_INT(ctx, 0, "transaction.cursorGet - first parameter must be a lmdb.op_* value");
    nxt_idx=1;
    if(!duk_is_undefined(ctx, nxt_idx) && !duk_is_boolean(ctx, nxt_idx))
    {
        key.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, nxt_idx, &ksz, "transaction.cursorGet - second parameter must be a String or Buffer (key)");
        key.mv_size=(duk_size_t)ksz;
        nxt_idx++;
    }

    if(!duk_is_undefined(ctx, nxt_idx))
    {
        key_is_string = REQUIRE_BOOL(ctx, nxt_idx, "transaction.cursorGet - parameter %d must be a Boolean(key_is_string)", (int)nxt_idx+1);
    }

    nxt_idx++;
    if(!duk_is_undefined(ctx, nxt_idx))
    {
        val_is_string = REQUIRE_BOOL(ctx, nxt_idx, "transaction.cursorGet - parameter %d must be a Boolean(val_is_string)", (int)nxt_idx+1);
    }

    rc = mdb_cursor_get(cursor, &key, &val, op);
    duk_push_object(ctx);

    if(rc == MDB_NOTFOUND || !key.mv_size)
        return 1; //return empty object
    else if (rc)
        RP_THROW(ctx, "transaction.cursorGet - %s", mdb_strerror(rc));

    obj_idx = duk_normalize_index(ctx, -1);

    if(val_is_string)
        duk_push_lstring(ctx, (const char*)val.mv_data, (duk_size_t)val.mv_size);
    else
    {
        void *p  = duk_push_fixed_buffer(ctx, (duk_size_t)val.mv_size);
        memcpy(p, val.mv_data, val.mv_size);
    }
    val_idx = duk_normalize_index(ctx, -1);

    if(key_is_string)
    {
        duk_push_lstring(ctx, (const char*)key.mv_data, (duk_size_t)key.mv_size);
    }
    else
    {
        void *p  = duk_push_fixed_buffer(ctx, (duk_size_t)key.mv_size);
        memcpy(p, key.mv_data, key.mv_size);
    }

    duk_put_prop_string(ctx, obj_idx, "key");
    duk_pull(ctx, val_idx);
    duk_put_prop_string(ctx, obj_idx, "value");

    return 1;
}

duk_ret_t duk_rp_lmdb_cursor_next_prev(duk_context *ctx, int isprev)
{
    MDB_val val={0}, key = {0};
    int op, rc, key_is_string=0, val_is_string=0;
    duk_idx_t val_idx, obj_idx;
    MDB_dbi dbi;
    MDB_cursor *cursor;

    duk_push_this(ctx);
    dbi = get_dbi(ctx, -1);
    cursor = get_cursor(ctx, dbi, -1);

    if(isprev)
        op = MDB_PREV;
    else
        op = MDB_NEXT;

    if(!duk_is_undefined(ctx, 0))
    {
        key_is_string = REQUIRE_BOOL(ctx, 0, "transaction.cursorNext - first parameter must be a Boolean(key_is_string)");
    }

    if(!duk_is_undefined(ctx, 1))
    {
        val_is_string = REQUIRE_BOOL(ctx, 1, "transaction.cursorNext - second parameter must be a Boolean(val_is_string)");
    }

    rc = mdb_cursor_get(cursor, &key, &val, op);
    duk_push_object(ctx);

    if(rc == MDB_NOTFOUND || !key.mv_size)
        return 0; //return undefined
    else if (rc)
        RP_THROW(ctx, "transaction.cursorNext - %s", mdb_strerror(rc));

    obj_idx = duk_normalize_index(ctx, -1);

    if(val_is_string)
        duk_push_lstring(ctx, (const char*)val.mv_data, (duk_size_t)val.mv_size);
    else
    {
        void *p  = duk_push_fixed_buffer(ctx, (duk_size_t)val.mv_size);
        memcpy(p, val.mv_data, val.mv_size);
    }
    val_idx = duk_normalize_index(ctx, -1);

    if(key_is_string)
    {
        duk_push_lstring(ctx, (const char*)key.mv_data, (duk_size_t)key.mv_size);
    }
    else
    {
        void *p  = duk_push_fixed_buffer(ctx, (duk_size_t)key.mv_size);
        memcpy(p, key.mv_data, key.mv_size);
    }

    duk_put_prop_string(ctx, obj_idx, "key");
    duk_pull(ctx, val_idx);
    duk_put_prop_string(ctx, obj_idx, "value");

    return 1;
}

duk_ret_t duk_rp_lmdb_cursor_next(duk_context *ctx)
{
    return duk_rp_lmdb_cursor_next_prev(ctx, 0);
}

duk_ret_t duk_rp_lmdb_cursor_prev(duk_context *ctx)
{
    return duk_rp_lmdb_cursor_next_prev(ctx, 1);
}


duk_ret_t duk_rp_lmdb_cursor_put(duk_context *ctx)
{
    MDB_val val={0}, key = {0};
    duk_size_t sz;
    int rc;
    MDB_dbi dbi;
    MDB_cursor *cursor;

    duk_push_this(ctx);
    dbi = get_dbi(ctx, -1);
    cursor = get_cursor(ctx, dbi, -1);

    key.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, 0, &sz, "transaction.cursorPut - first parameter must be a String or Buffer (key)");
    key.mv_size=(duk_size_t)sz;

    if(duk_is_object(ctx, 1))
        duk_cbor_encode(ctx, 1, 0);

    val.mv_data = (void *) REQUIRE_STR_OR_BUF(ctx, 1, &sz, "transaction.cursorPut - second parameter must be a String or Buffer (value)");
    val.mv_size=(duk_size_t)sz;

    rc = mdb_cursor_put(cursor, &key, &val, 0);

    if (rc)
        RP_THROW(ctx, "transaction.cursorPut - %s", mdb_strerror(rc));

    return 0;
}

duk_ret_t duk_rp_lmdb_cursor_del(duk_context *ctx)
{
    int rc;
    MDB_dbi dbi;
    MDB_cursor *cursor;

    duk_push_this(ctx);
    dbi = get_dbi(ctx, -1);
    cursor = get_cursor(ctx, dbi, -1);

    rc = mdb_cursor_del(cursor, 0);

    if (rc)
        RP_THROW(ctx, "transaction.cursorDel - %s", mdb_strerror(rc));

    return 0;
}

/* open each database in its own transaction. */
/* if the value at rc_p is not NULL, set rc, return 0 instead of throwing error */
static MDB_dbi open_dbi(duk_context *ctx, MDB_env *env, const char *name, unsigned int flag, int *rc_p)
{
    MDB_txn *txn=NULL;
    int rc;
    MDB_dbi dbi;

    if(flag == MDB_CREATE)
    {
        check_txn(ctx, "lmdb.openDb");
        mdblock;
        rc = mdb_txn_begin(env, NULL, 0, &txn);
    }
    else
        rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        if(flag == MDB_CREATE)
            mdbunlock;
        RP_THROW(ctx, "lmdb.openDb - error beginning transaction - %s", mdb_strerror(rc));
    }
    rc = mdb_dbi_open(txn, name, flag, &dbi);
    if(rc)
    {
        if(flag == MDB_CREATE)
            mdbunlock;
        mdb_txn_abort(txn);
        if(rc_p)
        {
            *rc_p = rc;
            return 0;
        }
        RP_THROW(ctx, "lmdb.openDb - error opening \"%s\" - %s", name, mdb_strerror(rc));
    }    

    mdb_txn_commit(txn);
    if(flag == MDB_CREATE)
        mdbunlock;

    return dbi;            
}

duk_ret_t duk_rp_lmdb_open_db(duk_context *ctx)
{
    MDB_env *env=NULL;
    MDB_dbi dbi;
    const char *db;
    unsigned int flag = 0;

    if(duk_is_undefined(ctx,0) || duk_is_null(ctx,0))
        db = "";
    else
        db = REQUIRE_STRING(ctx, 0, "lmdb.open_db - parameter must be a string (database name)");

    if(duk_get_boolean_default(ctx, 1, 0))
        flag = MDB_CREATE;    

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("env"));
    env = (MDB_env *)duk_get_pointer(ctx, -1);

    dbi = open_dbi(ctx, env, db, flag, NULL);

    duk_push_object(ctx);
    duk_push_uint(ctx, dbi);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("dbi"));
    if(*db=='\0')
        db = "lmdb default";
    duk_push_string(ctx, db);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("db"));
    return 1;
}


/* new txn */
duk_ret_t duk_rp_lmdb_new_txn(duk_context *ctx)
{
    MDB_env *env=NULL;
    MDB_txn *txn=NULL;
    MDB_dbi dbi;
    const char *db=NULL;
    int rw=0, rc, n_txn;

    /* allow call to transaction() with "new lmdb.transaction()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    //allow a boolean as the sole parameter for default db
    if(duk_is_boolean(ctx, 0) && ( duk_is_undefined(ctx, 1) || duk_is_boolean(ctx, 1) ) ) 
    {
                          // [ bool1, [bool2|undefined], undefined ]
        duk_pull(ctx, 0); // [ [bool2|undefined], undefined, bool1 ]
        duk_pull(ctx, 0); // [ undefined, bool1, [bool2|undefined] ]
    }

    duk_push_current_function(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("self"));

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("env"));
    env = (MDB_env *)duk_get_pointer(ctx, -1);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("env"));

    if(duk_is_object(ctx,0) && duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi")) )
    {
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi"));
        dbi = (MDB_dbi) duk_get_int(ctx,-1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("db"));
        db = duk_get_string(ctx, -1);
        duk_pop(ctx);
    }
    else if (duk_is_undefined(ctx, 0) || duk_is_null(ctx, 0) )
    {
        db="lmdb default";
        dbi = open_dbi(ctx, env, "", 0, NULL);
    }
    else
    {
        db = REQUIRE_STRING(ctx, 0, "lmdb.transaction - first parameter must be a string (the database from the current database environment)");

        if(*db =='\0')
        {
            db="lmdb default";
            dbi = open_dbi(ctx, env, "", 0, NULL);
        }
        else
        {
            dbi = open_dbi(ctx, env, db, 0, &rc);
            if(rc == MDB_NOTFOUND)
                dbi = open_dbi(ctx, env, db, MDB_CREATE, NULL);
        }        
    }

    rw = REQUIRE_BOOL(ctx, 1, "lmdb.transaction - second parameter must be a boolean (false for readonly; true for readwrite)");

    /* a place for {dbi:cursor} pairs */
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("cursors"));

    /* see if we already have a rw transaction open */
    if(rw)
    {
        if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("writer_txn")))
        {
            // can we return the same MDB_txn if db is the same without
            // inconsistencies with transaction.commit from two different transactions
            // with the same MDB_txns ?? - probably not, but maybe worth exploring later.
            //MDB_txn *wtxn;
            //duk_get_prop_string(ctx, -1, "ptr");
            //wtxn = (MDB_txn *)duk_get_pointer();
            if(duk_is_object(ctx, -1))
            {
                duk_get_prop_string(ctx, -1, "db");
                RP_THROW(ctx, "lmdb.transaction - error beginning transaction - A read/write transaction is already open for the %s database",
                                duk_get_string(ctx, -1));
            }
        }
        duk_pop(ctx);
    }

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("txn_open"));
    n_txn = (int)duk_get_int_default(ctx, -1, 0) + 1;
    duk_pop(ctx);
    duk_push_int(ctx, n_txn);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("txn_open"));

    if(rw)
    {
        mdblock;
        rc = mdb_txn_begin(env, NULL, 0, &txn);
    }
    else
        rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        //mdb_txn_abort(txn);
        RP_THROW(ctx, "lmdb.transaction - error beginning transaction - %s", mdb_strerror(rc));
    }

    duk_push_pointer(ctx, (void*)txn);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("txn"));

    duk_push_int(ctx, (int)dbi);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("dbi"));

    if(rw)
    {
        duk_push_object(ctx);
        duk_push_pointer(ctx, (void*)txn);
        duk_put_prop_string(ctx, -2, "ptr");
        duk_push_string(ctx, db);
        duk_put_prop_string(ctx, -2, "db");
        duk_put_global_string (ctx, DUK_HIDDEN_SYMBOL("writer_txn"));
        duk_push_true(ctx);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("iswriter"));
    }

    if(duk_get_boolean_default(ctx, 2, 0))
        duk_push_c_function(ctx, duk_rp_lmdb_ll_commit_, 1);
    else
        duk_push_c_function(ctx, duk_rp_lmdb_ll_abort_, 1);
    duk_set_finalizer(ctx, -2);

    return 0;
}

/* **************************************************
   Initialize Sql module 
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    static int isinit=0;

    if(!isinit)
    {
        if (pthread_mutex_init(&lmdblock, NULL) == EINVAL)
        {
            fprintf(stderr, "rampart.event: could not initialize cbor lock\n");
            exit(1);
        }
        isinit=1;
    }

    duk_push_object(ctx); // the return object

    duk_push_c_function(ctx, duk_rp_lmdb_constructor, 3 /*nargs*/);

    /* Push object that will be Lmdb.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Lmdb protoObj ] */

    /* Set Lmdb.prototype.exec. */
    duk_push_c_function(ctx, duk_rp_lmdb_get, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "get");

    duk_push_c_function(ctx, duk_rp_lmdb_get_count, 1 /*nargs*/);
    duk_put_prop_string(ctx, -2, "getCount");

    duk_push_c_function(ctx, duk_rp_lmdb_put, 3 /*nargs*/);
    duk_put_prop_string(ctx, -2, "put");

    duk_push_c_function(ctx, duk_rp_lmdb_del, 5 /*nargs*/);
    duk_put_prop_string(ctx, -2, "del");

    duk_push_c_function(ctx, duk_rp_lmdb_sync, 0 /*nargs*/);
    duk_put_prop_string(ctx, -2, "sync");

    duk_push_c_function(ctx, duk_rp_lmdb_drop, 1 /*nargs*/);
    duk_put_prop_string(ctx, -2, "drop");

    duk_push_c_function(ctx, duk_rp_lmdb_open_db, 2);
    duk_put_prop_string(ctx, -2, "openDb");

    push_cursor_ops(ctx);

    /* new txn */
    duk_push_c_function(ctx, duk_rp_lmdb_new_txn, 3);
    duk_push_object(ctx); //prototype obj

    duk_push_c_function(ctx, duk_rp_lmdb_ll_put, 3);
    duk_put_prop_string(ctx, -2, "put");

    duk_push_c_function(ctx, duk_rp_lmdb_ll_get, 3);
    duk_put_prop_string(ctx, -2, "get");

    duk_push_c_function(ctx, duk_rp_lmdb_ll_del, 2);
    duk_put_prop_string(ctx, -2, "del");

    duk_push_c_function(ctx, duk_rp_lmdb_cursor_get, 5);
    duk_put_prop_string(ctx, -2, "cursorGet");

    duk_push_c_function(ctx, duk_rp_lmdb_cursor_next, 3);
    duk_put_prop_string(ctx, -2, "cursorNext");

    duk_push_c_function(ctx, duk_rp_lmdb_cursor_prev, 3);
    duk_put_prop_string(ctx, -2, "cursorPrev");

    duk_push_c_function(ctx, duk_rp_lmdb_cursor_put, 3);
    duk_put_prop_string(ctx, -2, "cursorPut");

    duk_push_c_function(ctx, duk_rp_lmdb_cursor_del, 1);
    duk_put_prop_string(ctx, -2, "cursorDel");

    duk_push_c_function(ctx, duk_rp_lmdb_ll_commit, 0);
    duk_put_prop_string(ctx, -2, "commit");

    duk_push_c_function(ctx, duk_rp_lmdb_ll_abort, 0);
    duk_put_prop_string(ctx, -2, "abort");

    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "transaction");
    /* end new txn */

    /* Set Sql.prototype = protoObj */
    duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ {}, Lmdb-->[prototype-->{exe=fn_exe,...}] ] */
    duk_put_prop_string(ctx, -2, "init");      /* [ {init()} ] */

    return 1;
}
