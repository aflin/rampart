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

// conversion types
#define RP_LMDB_BUFFER 0
#define RP_LMDB_STRING 1
#define RP_LMDB_JSON   2
#define RP_LMDB_CBOR   3


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
        RP_THROW(ctx, "lmdb.init - failed to create envirnoment");

    opening_pid=getpid();

    mdb_env_set_mapsize(env, mapsize * 1048576);
    mdb_env_set_maxdbs(env, 256);

    if((rc=mdb_env_open(env, dbpath, oflags, 0644)))
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
duk_ret_t duk_rp_lmdb_drop(duk_context *ctx)
{
    int rc;
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    const char *db = REQUIRE_STRING(ctx, 0, "lmdb.put - first argument must be a string (the database from the current database environment)");
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

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if(rc)
    {
        RP_THROW(ctx, "lmdb.drop - error beginning transaction - %s", mdb_strerror(rc));
    }

    rc = mdb_dbi_open(txn, db, MDB_CREATE, &dbi);
    if(rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "lmdb.drop - error opening %s - %s", db, mdb_strerror(rc));
    }
    rc = mdb_drop(txn, dbi, 1);
    if(rc)
    {
        mdb_dbi_close(env,dbi);
        mdb_txn_abort(txn);
        RP_THROW(ctx, "lmdb.drop - error dropping %s - %s", db, mdb_strerror(rc));
    }
    rc = mdb_txn_commit(txn);
    mdb_dbi_close(env,dbi);
    if (rc)
    {
            RP_THROW(ctx, "lmdb.drop - error dropping db %s: (%d) %s\n", db, rc, mdb_strerror(rc));
    }
    return 0;
}

duk_ret_t duk_rp_lmdb_put(duk_context *ctx)
{
    int rc, convtype=RP_LMDB_BUFFER;
    MDB_env *env;
    MDB_txn *txn;
    MDB_dbi dbi;
    const char *db = REQUIRE_STRING(ctx, 0, "lmdb.put - first argument must be a string (the database from the current database environment)");

    if (!duk_is_object(ctx, 1))
        RP_THROW(ctx, "lmdb.put - second argument must be an object with keys to store");

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

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if(rc)
    {
        RP_THROW(ctx, "lmdb.put - error beginning transaction - %s", mdb_strerror(rc));
    }

    rc = mdb_dbi_open(txn, db, MDB_CREATE, &dbi);
    if(rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "lmdb.put - error opening %s - %s", db, mdb_strerror(rc));
    }
    duk_enum(ctx, 1,0);
    while (duk_next(ctx, -1, 1))
    {
        MDB_val key, val;
        const char *s=NULL;
        
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
            duk_size_t sz;
            duk_cbor_encode(ctx, -1, 0);
            s=(const char *) duk_get_buffer_data(ctx, -1, &sz);
            val.mv_data = (void*)s;
            val.mv_size = (size_t)sz;
        }
        else
        {
            duk_size_t sz;

            if(duk_is_buffer_data(ctx, -1))
                s=(const char *) duk_get_buffer_data(ctx, -1, &sz);
            else if (duk_is_string(ctx, -1))
                s=duk_get_lstring(ctx, -1, &sz);
            else 
            {
                mdb_txn_abort(txn);           
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
	    RP_THROW(ctx, "lmdb - put failed - %s", mdb_strerror(rc));
	}
	duk_pop_2(ctx);
    }

    rc = mdb_txn_commit(txn);
    if (rc)
    {
            mdb_txn_abort(txn);
            RP_THROW(ctx, "lmdb.put - error committing data: (%d) %s\n", rc, mdb_strerror(rc));
    }
    
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
    const char *db;
    
    if(del)
        fname="lmdb.del";
    
    db = REQUIRE_STRING(ctx, 0, "%s - first argument must be a string (the database from the current database environment)", fname);

    if (!duk_is_string(ctx, 1))
        RP_THROW(ctx, "%s - second argument must be a string (keys to retrieve)", fname);

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
        if(max <1) RP_THROW(ctx, "%s - fourth argument must be a number greater than 0 (maximum number of items to retrieve)", fname);
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
        rc = mdb_txn_begin(env, NULL, 0, &txn);
    else
        rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        //mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error beginning transaction - %s", fname, mdb_strerror(rc));
    }

    rc = mdb_dbi_open(txn, db, 0, &dbi);
    if(rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error opening %s - %s", db, fname, mdb_strerror(rc));
    }    

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if(rc)
    {
        mdb_txn_abort(txn);
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
            return 1;
        }
        else if (rc)
            RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));

        if(del)
        {
            rc = mdb_cursor_del(cursor, 0);
            if(rc)
                RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
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
                    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
                if(strncmp(s, key.mv_data, len))
                    break;
                //if(del) currently always true if retvals is 0
                //{
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
                //}
            }
            mdb_cursor_close(cursor);
            mdb_txn_commit(txn);
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
                RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
            if(strncmp(s, key.mv_data, len))
                break;
            if(del)
            {
                rc = mdb_cursor_del(cursor, 0);
                if(rc)
                    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
            }
            pushkey;
            pushval;
            duk_put_prop(ctx, -3);
        }
        mdb_cursor_close(cursor);
        mdb_txn_commit(txn);
        return 1;
    }

    /* non glob cases - start by retrieving the key as given */

    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET);
    if(rc == MDB_NOTFOUND)
    {
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
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
        RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));

    if(del)
    {
        /* found and delete requested, so delete */
        rc = mdb_cursor_del(cursor, 0);
        if(rc)
            RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
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
                    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
                if(del)
                {
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
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
                    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
                if(direction * strncmp(endstr, key.mv_data, key.mv_size) < 0)
                    break;
                if(del)
                {
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
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
                    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
                //if(del) currently can only ask for no retval from del
                //{
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
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
                    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
                if(direction * strncmp(endstr, key.mv_data, key.mv_size) < 0)
                    break;
                if(del)
                {
                    rc = mdb_cursor_del(cursor, 0);
                    if(rc)
                        RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));
                }
            }
        }
    }

    mdb_cursor_close(cursor);
    mdb_txn_commit(txn);
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

duk_ret_t duk_rp_lmdb_cleanup(duk_context *ctx)
{
    ctx = main_ctx;
    if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        duk_enum(ctx, -1, 0);
        while (duk_next(ctx, -1, 1))
        {
            duk_get_prop_string(ctx, -1, "env");
            MDB_env *env = (MDB_env *) duk_get_pointer(ctx, -1);
            duk_pop(ctx);
            mdb_env_sync(env, 1);
            mdb_env_close(env);
            duk_pop_2(ctx);
        }
    }
    return 0;
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
    size_t mapsize=10;
    unsigned int openflags=0;        
    int convtype=RP_LMDB_BUFFER;

    duk_idx_t obj_idx=-1, bool_idx=-1, str_idx = -1, i=0;

    if(!main_ctx)
        main_ctx=ctx;

    /* allow call to Sql() with "new Sql()" only */
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
        RP_THROW(ctx, "new Lmdb.init() requires a string (database environment location) for one of its arguments" );

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
    }


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

        mdb_env_set_maxdbs(env, 256);
        mdb_env_set_mapsize(env, mapsize*1048576);

        if((rc=mdb_env_open(env, dbpath, openflags, 0644)))
        {
            mdb_env_close(env);
            RP_THROW(ctx, "lmdb.init - failed to open %s%s", dbpath, mdb_strerror(rc));
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
    return 0;
}

/* **************************************************
   Initialize Sql module 
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{

    duk_push_object(ctx); // the return object

    duk_push_c_function(ctx, duk_rp_lmdb_constructor, 3 /*nargs*/);

    /* Push object that will be Lmdb.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Lmdb protoObj ] */

    /* Set Lmdb.prototype.exec. */
    duk_push_c_function(ctx, duk_rp_lmdb_get, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "get");

    duk_push_c_function(ctx, duk_rp_lmdb_put, 2 /*nargs*/);
    duk_put_prop_string(ctx, -2, "put");

    duk_push_c_function(ctx, duk_rp_lmdb_del, 5 /*nargs*/);
    duk_put_prop_string(ctx, -2, "del");

    duk_push_c_function(ctx, duk_rp_lmdb_sync, 0 /*nargs*/);
    duk_put_prop_string(ctx, -2, "sync");

    duk_push_c_function(ctx, duk_rp_lmdb_drop, 1 /*nargs*/);
    duk_put_prop_string(ctx, -2, "drop");

    /* Set Sql.prototype = protoObj */
    duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ {}, Lmdb-->[prototype-->{exe=fn_exe,...}] ] */
    duk_put_prop_string(ctx, -2, "init");      /* [ {init()} ] */

    return 1;
}
