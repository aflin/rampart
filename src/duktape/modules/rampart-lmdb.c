/* Copyright (C) 2024  Aaron Flin - All Rights Reserved
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

#define GROW_ON_PUT 1

#define LMDB_ENV struct lmdb_env_s
LMDB_ENV {
    char                        *dbpath;
    pid_t                       pid;
    unsigned int                openflags;
    unsigned int                rp_flags;  //currently only for growOnPut
    size_t                      mapsize;
    int                         convtype;
    int                         maxdbs;
    MDB_env                     *env;
//    RPTHR_LOCK			*rp_lock;
//    pthread_mutex_t             lock;
};

#define lock_main_ctx do{\
    if (ctx != main_ctx)\
        CTXLOCK;\
} while(0)

#define unlock_main_ctx do{\
    if(ctx != main_ctx)\
        CTXUNLOCK;\
}while(0)

pthread_mutex_t lmdblock;
RPTHR_LOCK *rp_lmdblock;

#define mdb_lock RP_MLOCK(rp_lmdblock)

#define mdb_unlock RP_MUNLOCK(rp_lmdblock)

//#define write_lock RP_MLOCK(lenv->rp_lock)
#define write_lock /*nada*/
// we are not consistent about write lock/unlock pairing.  Might want to fix.
//#define write_unlock if(RPTHR_TEST(lenv->rp_lock, RPTHR_LOCK_FLAG_LOCKED) ) RP_MUNLOCK(lenv->rp_lock)
#define write_unlock /*nada*/
// conversion types
#define RP_LMDB_BUFFER 0
#define RP_LMDB_STRING 1
#define RP_LMDB_JSON   2
#define RP_LMDB_CBOR   3

/* 'this' must be on top of the stack */
static LMDB_ENV *redo_env(duk_context *ctx, LMDB_ENV *lenv) {
    int rc;

    if(lenv->env)
    {
        mdb_env_close(lenv->env);
        lenv->env=NULL;
    }

    if(mdb_env_create(&lenv->env))
    {
        lenv->env=NULL;
        RP_THROW(ctx, "lmdb.reinit - failed to create environment");
    }

    lenv->pid = getpid();

    mdb_env_set_mapsize(lenv->env, lenv->mapsize);
    mdb_env_set_maxdbs(lenv->env, lenv->maxdbs);

    if((rc=mdb_env_open(lenv->env, lenv->dbpath, lenv->openflags|MDB_NOTLS, 0644)))
    {
        mdb_env_close(lenv->env);
        RP_THROW(ctx, "lmdb.reinit - failed to open %s %s", lenv->dbpath, mdb_strerror(rc));
    }
    lock_main_ctx;
    /* get the object with previously opened lmdb environments */
    if(!duk_get_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        /* doesn't exist, pop the undefined on the stack */
        duk_pop(main_ctx);
        /* make a new object to be our lmdbenvs */
        duk_push_object(main_ctx);
        duk_dup(main_ctx, -1);
        duk_put_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs"));
    }

    duk_push_pointer(main_ctx, (void *) lenv);
    duk_put_prop_string(main_ctx, -2, lenv->dbpath);

    duk_pop(main_ctx);
    unlock_main_ctx;

    duk_push_pointer(ctx, (void *) lenv);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("lenv")); // into 'this'

    return lenv;
}

static int lmdb_destroyed = 0;

/* get an opened environment.  open/reopen as necessary  *
 *    'this' must be on top of the stack                 */
static LMDB_ENV * get_env(duk_context *ctx)
{
    LMDB_ENV *lenv;

    // it would appear that duktape runs finalizers in some sort of
    // try/cancel manner.  Upon doing the get_prop_string below, it stops
    // and jumps to the next JS instruction without doing anything else here
    // if the finalizer has already been run manually (txn.commit()|txn.abort()).
    // So we need to lock as late as possible.  At least after the next duk_* call
    // I'm still not sure exactly what is going on.

    // mdb_lock;

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("lenv")))
    {
        //mdb_unlock;
        RP_THROW(ctx, "lmdb: database was previously closed");
    }

    mdb_lock;  //lock here!

    lenv = (LMDB_ENV *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!lenv)
    {
        mdb_unlock;
        RP_THROW(ctx, "lmdb: database was previously closed");
    }
    if( lenv->pid == getpid() )
    {
        mdb_unlock;
        return lenv;
    }
    else
    {
        int rc;

        lenv = redo_env(ctx, lenv);
        //RP_PTINIT(&lenv->lock);
        /* must sync or some stuff done before a fork
           (like creating a db) may not be available
           to the child                               */
        rc=mdb_env_sync(lenv->env, 1);
        if(rc)
        {
            RP_THROW(ctx, "lmdb.sync - error: %s", mdb_strerror(rc));
        }

        /* start fresh with no writers */
        duk_push_object(ctx);
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdb_writers"));

        mdb_unlock;
        return lenv;
    }
}


/* check if a lmdb.transaction is open for writing */
static inline void check_txn_write_open(duk_context *ctx, LMDB_ENV *lenv, const char *fname)
{
    const char *wdb=NULL;

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdb_writers"));
    if(duk_get_prop_string(ctx, -1, lenv->dbpath))
        wdb = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    if(wdb)
    {
        RP_THROW(ctx, "%s - error - A read/write transaction is already open for the %s database in environment %s",
            fname, wdb, lenv->dbpath);
    }
}
/* get a database handle at the given idx.  If it is a string, open the named db */
static int get_dbi_idx(duk_context *ctx, MDB_txn *txn, MDB_dbi *dbi, int flags, duk_idx_t dbi_idx, const char *fname)
{
    const char *db;

    if(duk_is_object(ctx, dbi_idx) && duk_has_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("dbi")) )
    {
        pid_t pid;
        duk_get_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("pid"));
        pid = (pid_t) duk_get_int(ctx, -1);
        duk_pop(ctx);

        /* we forked, need to get new handle */
        if(pid != getpid())
        {
            int rc;

            duk_get_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("db"));
            db = duk_get_string(ctx, -1);
            duk_pop(ctx);
            if(!strcmp(db,"lmdb default"))
                db=NULL;
            rc = mdb_dbi_open(txn, db, flags, dbi);

            if (rc)
            {
                *dbi=0;
                duk_push_int(ctx, 0);
                duk_put_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("pid"));
                duk_push_int(ctx, 0);
                duk_put_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("dbi"));
                return rc;
            }

            duk_push_int(ctx, (int)getpid());
            duk_put_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("pid"));

            duk_push_int(ctx, (int)*dbi);
            duk_put_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("dbi"));
            return rc;
        }
        duk_get_prop_string(ctx, dbi_idx, DUK_HIDDEN_SYMBOL("dbi"));
        *dbi = (MDB_dbi) duk_get_int(ctx,-1);
        duk_pop(ctx);
        return 0;
    }

    if(duk_is_null(ctx, dbi_idx))
        db=NULL;
    else
        db = REQUIRE_STRING(ctx, dbi_idx, "%s: parameter %d must be a null, string or dbi object", fname, (int)dbi_idx +1);
    if(db && !strlen(db))
        db=NULL;

    return mdb_dbi_open(txn, db, flags, dbi);
}

duk_ret_t duk_rp_lmdb_sync(duk_context *ctx)
{
    int rc;
    LMDB_ENV *lenv;

    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    lenv = get_env(ctx);
    rc=mdb_env_sync(lenv->env, 1);
    if(rc)
    {
        RP_THROW(ctx, "lmdb.sync - error: %s", mdb_strerror(rc));
    }

    return 0;
}

#define GETDBNAME \
const char *db="lmdb main";\
if(duk_is_string(ctx, 0))\
    db = duk_get_string(ctx, 0);\
else if(duk_is_object(ctx,0)){\
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("db"));\
    db = duk_get_string(ctx, -1);\
}


duk_ret_t duk_rp_lmdb_drop(duk_context *ctx)
{
    int rc;
    LMDB_ENV *lenv;
    MDB_txn *txn;
    MDB_dbi dbi;


    duk_push_this(ctx);
    /* 'this' must be on top of stack */
    lenv = get_env(ctx);
    duk_pop(ctx);//this

    check_txn_write_open(ctx, lenv, "lmdb.drop");

    write_lock;
    rc = mdb_txn_begin(lenv->env, NULL, 0, &txn);
    if(rc)
    {
        write_unlock;
        RP_THROW(ctx, "lmdb.drop - error beginning transaction - %s", mdb_strerror(rc));
    }

    rc = get_dbi_idx(ctx, txn, &dbi, MDB_CREATE, 0, "lmdb.drop");
    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        write_unlock;
        RP_THROW(ctx, "lmdb.drop - error opening %s - %s", db, mdb_strerror(rc));
    }
    if(dbi != 1) // the "MAIN_DBI"
    {
        /* occassionally getting MDB_PROBLEM return rc from mdb_txn_commit
           when doing mdb_drop(txn, dbi, 1);
           But if we do mdb_drop(txn, dbi, 0); first, then mdb_drop(txn, dbi, 1);
           it seems to work.  No idea why.
        */

        rc = mdb_drop(txn, dbi, 0);
        if(rc)
        {
            GETDBNAME
            // ?? mdb_dbi_close(lenv->env,dbi);
            mdb_txn_abort(txn);
            write_unlock;
            RP_THROW(ctx, "lmdb.drop - error dropping %s - %s", db, mdb_strerror(rc));
        }
        rc = mdb_txn_commit(txn);
        if (rc)
        {
            GETDBNAME
            write_unlock;
            RP_THROW(ctx, "lmdb.drop - error dropping db %s: (%d) %s\n", db, rc, mdb_strerror(rc));
        }

        rc = mdb_txn_begin(lenv->env, NULL, 0, &txn);
        if(rc)
        {
            write_unlock;
            RP_THROW(ctx, "lmdb.drop - error beginning transaction - %s", mdb_strerror(rc));
        }

        rc = mdb_drop(txn, dbi, 1);
        if(rc)
        {
            GETDBNAME
            // ?? mdb_dbi_close(lenv->env,dbi);
            mdb_txn_abort(txn);
            write_unlock;
            RP_THROW(ctx, "lmdb.drop - error dropping %s - %s", db, mdb_strerror(rc));
        }
        rc = mdb_txn_commit(txn);
        mdb_dbi_close(lenv->env,dbi);
    }
    else
    /* just delete the data, but not the named db metadata */
    {
        MDB_cursor *cursor;
        MDB_val key={0}, val={0};

        rc = mdb_cursor_open(txn, dbi, &cursor);

        if(rc)
        {
            mdb_txn_abort(txn);
            write_unlock;
            RP_THROW(ctx, "lmdb.drop - error opening database cursor - %s", mdb_strerror(rc));
        }

        while(1)
        {
            int rc2;
            rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);

            if(rc)
                break;

            /* names of dbs end in '\0' */
            if( val.mv_size == 48 && ((char *)key.mv_data)[key.mv_size-1] == '\0' )
                continue;

            rc2 = mdb_cursor_del(cursor, 0);
            if(rc2)// && rc2!=MDB_INCOMPATIBLE) /* a dbname will come back with this*/
            {
                mdb_cursor_close(cursor);
                mdb_txn_abort(txn);
                write_unlock;
                RP_THROW(ctx, "lmdb.drop - error deleting data in the default database - %s", mdb_strerror(rc2));
            }
        }
        mdb_cursor_close(cursor);
        rc = mdb_txn_commit(txn);
    }

    if (rc)
    {
        GETDBNAME
        write_unlock;
        RP_THROW(ctx, "lmdb.drop - error dropping db %s: (%d) %s\n", db, rc, mdb_strerror(rc));
    }
    write_unlock;
    return 0;
}

duk_ret_t duk_rp_lmdb_put(duk_context *ctx)
{
    int rc, convtype;
    LMDB_ENV *lenv;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, val;
    const char *s=NULL;
    duk_size_t sz;
    duk_idx_t top;

    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    lenv = get_env(ctx);

    convtype=lenv->convtype;

    duk_pop(ctx);//this

    top=duk_get_top(ctx);

    redo_txn:

    check_txn_write_open(ctx, lenv, "lmdb.put");

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

        write_lock;
        rc = mdb_txn_begin(lenv->env, NULL, 0, &txn);
        if(rc)
        {
            write_unlock;
            RP_THROW(ctx, "lmdb.put - error beginning transaction - %s", mdb_strerror(rc));
        }

        rc = get_dbi_idx(ctx, txn, &dbi, MDB_CREATE, 0, "lmdb.put");

        if(rc)
        {
            GETDBNAME
            mdb_txn_abort(txn);
            write_unlock;
            RP_THROW(ctx, "lmdb.put - error opening %s - %s", db, mdb_strerror(rc));
        }
        rc = mdb_put(txn, dbi, &key, &val, 0);

        if(rc)
        {
            mdb_txn_abort(txn);
            write_unlock;
            RP_THROW(ctx, "lmdb.put failed - %s", mdb_strerror(rc));
        }

        rc = mdb_txn_commit(txn);
        if(rc)
        {
            mdb_txn_abort(txn);
            write_unlock;
            if(rc == MDB_MAP_FULL && lenv->rp_flags & GROW_ON_PUT)
            {
                lenv->mapsize = (lenv->mapsize *15)/10;
                duk_push_this(ctx);
                lenv=redo_env(ctx, lenv);
                duk_set_top(ctx,top);
                goto redo_txn;
            }
            RP_THROW(ctx, "lmdb.put failed to commit - %s", mdb_strerror(rc));
        }

        write_unlock;
        return 0;
    }


    write_lock;
    rc = mdb_txn_begin(lenv->env, NULL, 0, &txn);
    if(rc)
    {
        write_unlock;
        RP_THROW(ctx, "lmdb.put - error beginning transaction - %s", mdb_strerror(rc));
    }

    rc = get_dbi_idx(ctx, txn, &dbi, MDB_CREATE, 0, "lmdb.put");

    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        write_unlock;
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
                write_unlock;
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
            write_unlock;
            // I think this might not happen until commit, but here for good measure
            if(rc == MDB_MAP_FULL && lenv->rp_flags & GROW_ON_PUT)
            {
                lenv->mapsize = (lenv->mapsize *15)/10;
                duk_push_this(ctx);
                lenv=redo_env(ctx, lenv);
                duk_set_top(ctx,top);
                goto redo_txn;
            }
	    RP_THROW(ctx, "lmdb - put failed - %s", mdb_strerror(rc));
	}
	duk_pop_2(ctx);
    }

    rc = mdb_txn_commit(txn);

    if (rc)
    {
        mdb_txn_abort(txn);
        write_unlock;
        if(rc == MDB_MAP_FULL && lenv->rp_flags & GROW_ON_PUT)
        {
            lenv->mapsize = (lenv->mapsize *15)/10;
            duk_push_this(ctx);
            lenv=redo_env(ctx, lenv);
            duk_set_top(ctx,top);
            goto redo_txn;
        }
        RP_THROW(ctx, "lmdb.put - put failed to commit %s\n",  mdb_strerror(rc));
    }
    write_unlock;
    return 0;
}

/* list databases */
duk_ret_t duk_rp_lmdb_list_dbs(duk_context *ctx)
{
    int rc;
    LMDB_ENV *lenv;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key={0}, val={0};
    MDB_cursor *cursor;
    const char *fname = "lmdb.listDbs";
    duk_uarridx_t i=0;
    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    lenv = get_env(ctx);

    rc = mdb_txn_begin(lenv->env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        RP_THROW(ctx, "%s - error beginning transaction - %s", fname, mdb_strerror(rc));
    }

    rc = mdb_dbi_open(txn, NULL, 0, &dbi);

    if(rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error opening database - %s", fname, mdb_strerror(rc));
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);

    if(rc)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error opening database cursor - %s", fname, mdb_strerror(rc));
    }

    duk_push_array(ctx);
    while(1) {
        rc = mdb_cursor_get(cursor, &key, &val, MDB_NEXT);
        /* names of dbs end in '\0' */
        if(rc)
            break;

        if( val.mv_size == 48 && ((char *)key.mv_data)[key.mv_size-1] == '\0' )
        {
            duk_push_string(ctx, (char *)key.mv_data);
            duk_put_prop_index(ctx, -2, i++);
        }
    }

    if(rc != MDB_NOTFOUND)
    {
        mdb_txn_abort(txn);
        RP_THROW(ctx, "%s - error retrieving database names - %s", fname, mdb_strerror(rc));
    }

    mdb_cursor_close(cursor);
    mdb_txn_commit(txn);

    return 1;
}

/* get count from MDB_stat */
duk_ret_t duk_rp_lmdb_get_count(duk_context *ctx)
{
    int rc;
    LMDB_ENV *lenv;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_stat stat;
    const char *fname = "lmdb.getCount";

    duk_push_this(ctx);

    /* 'this' must be on top of stack */
    lenv = get_env(ctx);

    rc = mdb_txn_begin(lenv->env, NULL, MDB_RDONLY, &txn);

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
                    if(del) write_unlock;\
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
    int rc, max=-1, convtype;
    LMDB_ENV *lenv;
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
    if(del) write_unlock;\
    RP_THROW(ctx, "%s - %s\n", fname, mdb_strerror(rc));\
} while(0)

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
    lenv = get_env(ctx);

    if(del)
    {
        fname="lmdb.del";
        check_txn_write_open(ctx, lenv, fname);
    }

    convtype = lenv->convtype;
    duk_pop(ctx);

    duk_pop(ctx); //this

    s = duk_get_string(ctx, 1);
    key.mv_data=(void*)s;
    key.mv_size=strlen(s);


    if(del)
    {
        write_lock;
        rc = mdb_txn_begin(lenv->env, NULL, 0, &txn);
    }
    else
        rc = mdb_txn_begin(lenv->env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        if(del)
            write_unlock;
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
            if(del)
                write_unlock;
            RP_THROW(ctx, "%s - error opening database - database does not exist", fname);
        }
    }
    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        if(del)
            write_unlock;
        RP_THROW(ctx, "%s - error opening %s - %s", db, fname, mdb_strerror(rc));
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if(rc)
    {
        GETDBNAME
        mdb_txn_abort(txn);
        if(del)
            write_unlock;
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
            if(del)
                write_unlock;
            duk_push_object(ctx);
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
            if(del)
                write_unlock;
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
        if(del)
            write_unlock;
        return 1;
    }

    /* non glob cases - start by retrieving the key as given */

    rc = mdb_cursor_get(cursor, &key, &data, MDB_SET);
    if(rc == MDB_NOTFOUND)
    {
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        if(del)
            write_unlock;
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
    if(del)
        write_unlock;
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

//MDB_env **all_env;
LMDB_ENV **all_env;

static void free_all_env(void *arg)
{
    int i=0;
    LMDB_ENV *lenv;
    while( (lenv = all_env[i]) )
    {
        if(lenv->env)
            mdb_env_close(lenv->env);
        if(lenv->dbpath)
            free(lenv->dbpath);
        free(lenv);
        i++;
    }
    free(all_env);
}

/* ctx is main_ctx here */
duk_ret_t duk_rp_lmdb_cleanup(duk_context *ctx)
{
    ctx = main_ctx;
    int n=0;

    if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        duk_enum(ctx, -1, 0);
        while (duk_next(ctx, -1, 1))
        {
            LMDB_ENV *lenv = (LMDB_ENV *) duk_get_pointer(ctx, -1);
            if(lenv->env)
            {
                mdb_env_sync(lenv->env, 1);

                /*
                    cannot free env when finalizers for transactions haven't run
                    and they won't be run until ctx is destroyed. So we need to
                    copy them out and free later.                                  */
                REMALLOC(all_env, (n+1) * sizeof(LMDB_ENV *));
                all_env[n]=lenv;
                n++;
            }
            duk_pop_2(ctx);
        }
        REMALLOC(all_env, (n+1) * sizeof(MDB_env *));
        all_env[n]=NULL;
        add_exit_func(free_all_env, NULL);
    }
    duk_pop(ctx);
    duk_push_object(ctx);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdbenvs"));
    lmdb_destroyed=1;
    return 0;
}

duk_ret_t duk_rp_lmdb_close(duk_context *ctx)
{
    ctx = main_ctx;
    LMDB_ENV *lenv;

    duk_push_this(ctx);
    lenv = get_env(ctx);
    //There's a gap between the unlock in get_env and the lock here.
    //Bad things could happen in that gap.  FIXME
    mdb_lock;
    duk_pop(ctx);

    if(lenv->env)
    {
        mdb_env_close(lenv->env);
        lenv->env=NULL;
    }

    mdb_unlock;
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

/* low level txn functions */

static MDB_txn * get_txn(duk_context *ctx, duk_idx_t this_idx)
{
    MDB_txn * txn;

    duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("txn"));
    txn= duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(txn == NULL)
        RP_THROW(ctx, "lmdb.transaction - transaction has already been closed (or other error)");

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
        pid_t pid;
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pid"));
        pid = (pid_t) duk_get_int(ctx, -1);
        duk_pop(ctx);

        if(pid == getpid())
        {
            duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi"));
            dbi = (MDB_dbi) duk_get_int(ctx,-1);
            duk_pop(ctx);
            duk_remove(ctx, 0);
            return dbi;
        }
        else
        /* we forked, need to get new handle */
        {
            int rc;
            const char *db;
            MDB_txn * txn;

            duk_push_this(ctx);
            txn = get_txn(ctx, -1);
            duk_pop(ctx);

            duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("db"));
            db = duk_get_string(ctx, -1);
            duk_pop(ctx);

            if(!strcmp(db,"lmdb default"))
                db=NULL;
            rc = mdb_dbi_open(txn, db, 0, &dbi); // <= 0, presumably it exists

            if (rc)
            {
                duk_push_int(ctx, 0);
                duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pid"));
                duk_push_int(ctx, 0);
                duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi"));
                RP_THROW(ctx, "lmdb tranaction - error reopening database after fork - %s", mdb_strerror(rc));
            }

            duk_push_int(ctx, (int)getpid());
            duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pid"));

            duk_push_int(ctx, (int)dbi);
            duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi"));

            duk_remove(ctx, 0);
            return dbi;
        }
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
    int rc=0;
    LMDB_ENV *lenv;
    const char *wdb=NULL;

    /* Finalizers run after the cleanup functions.
       don't do anything if we've already run duk_rp_lmdb_cleanup */

    if(lmdb_destroyed)
        return;

    if( duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("refbuffers")) )
    {
        duk_uarridx_t i=0, len = duk_get_length(ctx, -1);
        while(i<len)
        {
            duk_get_prop_index(ctx, -1, i);
            duk_config_buffer(ctx, -1, NULL, 0);
            duk_pop(ctx);
            i++;
        }
    }
    duk_pop(ctx);

    lenv = get_env(ctx);

    if(commit)
        rc = mdb_txn_commit(txn);
    else
        mdb_txn_abort(txn);

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdb_writers"));
    if(duk_get_prop_string(ctx, -1, lenv->dbpath))
        wdb = duk_get_string(ctx, -1);
    duk_pop(ctx);//just the string

    /* global "lmdb_writers" object is still at -1 */
    if(wdb)
    {
        duk_del_prop_string(ctx, -1, lenv->dbpath);
        write_unlock;
    }
    duk_pop(ctx); //global "lmdb_writers"

    /* txn is complete and invalid. Mark it as such. */
    duk_push_pointer(ctx, (void*)NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("txn"));

    /* free the cursors, if any */
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("cursors"));
    duk_enum(ctx, -1, 0);
    while(duk_next(ctx, -1, 1))
    {
        cursor = (MDB_cursor *)duk_get_pointer(ctx, -1);
        mdb_cursor_close(cursor);
        duk_pop_2(ctx);
    }
    duk_pop_2(ctx);

    if(rc)
        RP_THROW(ctx, "transaction.commit - error committing data: (%d) %s\n", rc, mdb_strerror(rc));
}


/* if finalizer, 'this' is automatically on top of the stack */
duk_ret_t duk_rp_lmdb_txn_abort_(duk_context *ctx)
{
    MDB_txn *txn = get_txn(ctx, -1);

    if(txn)
        clean_txn(ctx, txn, 0);

    return 0;
}

duk_ret_t duk_rp_lmdb_txn_abort(duk_context *ctx)
{
    duk_push_this(ctx);

    return duk_rp_lmdb_txn_abort_(ctx);
}

/* if finalizer, 'this' is automatically on top of the stack */
duk_ret_t duk_rp_lmdb_txn_commit_(duk_context *ctx)
{
    MDB_txn *txn = get_txn(ctx, -1);

    if(txn)
    {
        duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("txn"));
        clean_txn(ctx, txn, 1);
    }
    return 0;
}

duk_ret_t duk_rp_lmdb_txn_commit(duk_context *ctx)
{
    duk_push_this(ctx);

    return duk_rp_lmdb_txn_commit_(ctx);
}

duk_ret_t duk_rp_lmdb_txn_put(duk_context *ctx)
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
duk_ret_t duk_rp_lmdb_txn_get(duk_context *ctx)
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

/* get a single value from the db by reference*/
duk_ret_t duk_rp_lmdb_txn_get_ref(duk_context *ctx)
{
    int rc;
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, val;
    duk_size_t ksz;
    duk_uarridx_t i=0;

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

    duk_push_external_buffer(ctx);
    duk_config_buffer(ctx, -1, val.mv_data, (duk_size_t)val.mv_size);
    duk_dup(ctx, -1);

    /* stash it away in 'this' so we can reconfigure to 0 after tnx closes */
    if( !duk_get_prop_string(ctx, -3, DUK_HIDDEN_SYMBOL("refbuffers")) )
    {
        duk_pop(ctx);
        duk_push_array(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -5, DUK_HIDDEN_SYMBOL("refbuffers"));
    }
    else
        i = duk_get_length(ctx, -1);
    // [ this, buffer, buffer, ref_array]
    duk_pull(ctx, -2); // [ this, buffer, ref_array, buffer]
    duk_put_prop_index(ctx, -2, i); //[ this, buffer, ref_array]
    duk_pop(ctx); // [ this, buffer ]

    return 1;
}

/* delete a single value from the db */
duk_ret_t duk_rp_lmdb_txn_del(duk_context *ctx)
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
static MDB_dbi open_dbi(duk_context *ctx, LMDB_ENV *lenv, const char *name, unsigned int flag, int *rc_p)
{
    MDB_txn *txn=NULL;
    int rc;
    MDB_dbi dbi;

    if(flag == MDB_CREATE)
    {
        check_txn_write_open(ctx, lenv, "lmdb.openDb");
        write_lock;
        rc = mdb_txn_begin(lenv->env, NULL, 0, &txn);
    }
    else
        rc = mdb_txn_begin(lenv->env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        if(flag == MDB_CREATE)
            write_unlock;
        RP_THROW(ctx, "lmdb.openDb - error beginning transaction - %s", mdb_strerror(rc));
    }
    rc = mdb_dbi_open(txn, name, flag, &dbi);
    if(rc_p)
        *rc_p = rc;

    if(rc)
    {
        if(flag == MDB_CREATE)
            write_unlock;
        mdb_txn_abort(txn);
        if(rc_p)
        {
            return 0;
        }
        RP_THROW(ctx, "lmdb.openDb - error opening \"%s\" - %s", name, mdb_strerror(rc));
    }

    mdb_txn_commit(txn);
    if(flag == MDB_CREATE)
        write_unlock;

    return dbi;
}

duk_ret_t duk_rp_lmdb_open_db(duk_context *ctx)
{
    LMDB_ENV *lenv;
    MDB_dbi dbi;
    const char *db;
    unsigned int flag = 0;

    duk_push_this(ctx);
    lenv = get_env(ctx);

    if(duk_is_undefined(ctx,0) || duk_is_null(ctx,0))
        db = NULL;
    else
        db = REQUIRE_STRING(ctx, 0, "lmdb.open_db - parameter must be a string (database name)");

    if( db && !strlen(db))
        db=NULL;

    if(duk_get_boolean_default(ctx, 1, 0))
        flag = MDB_CREATE;

    dbi = open_dbi(ctx, lenv, db, flag, NULL);

    duk_push_object(ctx);
    duk_push_uint(ctx, dbi);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("dbi"));
    if(db==NULL)
        db = "lmdb default";
    duk_push_string(ctx, db);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("db"));

    duk_push_int(ctx, (int)getpid());
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pid"));

    return 1;
}


/* new txn */
duk_ret_t duk_rp_lmdb_new_txn(duk_context *ctx)
{
    LMDB_ENV *lenv=NULL;
    MDB_txn *txn=NULL;
    MDB_dbi dbi;
    const char *db=NULL, *wdb=NULL;
    int rw=0, rc;

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

    duk_push_this(ctx);

    lenv = get_env(ctx);

    if(!lenv->env)
        RP_THROW(ctx, "lmdb.transaction - cannot proceed, database was closed");

    if(lenv->pid != getpid())
        RP_THROW(ctx, "lmdb.transaction - transaction was opened in a different process and cannot be used");


    if(duk_is_object(ctx,0) && duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi")) )
    {
        pid_t pid;
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pid"));
        pid = (pid_t) duk_get_int(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("db"));
        db = duk_get_string(ctx, -1);
        duk_pop(ctx);

        if(pid == getpid())
        {   // here we have a dbi object and it was created in this process
            duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi"));
            dbi = (MDB_dbi) duk_get_int(ctx,-1);
            duk_pop(ctx);
        }
        /* we forked, need to get new handle */
        else
        {
            if(!strcmp(db,"lmdb default"))
                dbi = open_dbi(ctx, lenv, NULL, 0, NULL); //we assume it exists.
            else
                dbi = open_dbi(ctx, lenv, db, 0, NULL); //we assume it exists.

            duk_push_int(ctx, (int)getpid());
            duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("pid"));

            duk_push_int(ctx, (int)dbi);
            duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dbi"));
        }
    }
    else if (duk_is_undefined(ctx, 0) || duk_is_null(ctx, 0) )
    {
        db="lmdb default";
        dbi = open_dbi(ctx, lenv, NULL, 0, &rc);
    }
    else
    {
        db = REQUIRE_STRING(ctx, 0, "lmdb.transaction - first parameter must be a string or dbi object (the database from the current database environment to use)");

        if(*db =='\0')
        {
            db="lmdb default";
            dbi = open_dbi(ctx, lenv, NULL, 0, &rc);
        }
        else
        {
            dbi = open_dbi(ctx, lenv, db, 0, &rc);
            if(rc == MDB_NOTFOUND)
                dbi = open_dbi(ctx, lenv, db, MDB_CREATE, NULL);
        }
    }
    rw = REQUIRE_BOOL(ctx, 1, "lmdb.transaction - second parameter must be a boolean (false for readonly; true for readwrite)");

    /* a place for {dbi:cursor} pairs */
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("cursors"));

    /* see if we already have a rw transaction open */
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdb_writers"));
    if(duk_get_prop_string(ctx, -1, lenv->dbpath))
        wdb = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    if(rw && wdb)
        RP_THROW(ctx, "lmdb.transaction - error beginning transaction - A read/write transaction is already open for the %s database in environment %s",
            wdb, lenv->dbpath);
    if(rw)
    {
        //write_lock;
        rc = mdb_txn_begin(lenv->env, NULL, 0, &txn);
        //write_unlock;
    }
    else
        rc = mdb_txn_begin(lenv->env, NULL, MDB_RDONLY, &txn);

    if(rc)
    {
        RP_THROW(ctx, "lmdb.transaction - error beginning transaction - %s", mdb_strerror(rc));
    }

    duk_push_pointer(ctx, (void*)txn);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("txn"));

    duk_push_int(ctx, (int)dbi);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("dbi"));

    /* There can be only one write per open database env */
    if(rw)
    {
        duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdb_writers"));
        duk_push_string(ctx, db);
        duk_put_prop_string(ctx, -2, lenv->dbpath);
        duk_pop(ctx);
    }

    /* by default, abort if not committed */
    if(duk_get_boolean_default(ctx, 2, 0))
        duk_push_c_function(ctx, duk_rp_lmdb_txn_commit_, 1);
    else
        duk_push_c_function(ctx, duk_rp_lmdb_txn_abort_, 1);
    duk_set_finalizer(ctx, -2);

    return 0;
}

/* **************************************************
   Lmdb.init("/database/path") constructor:

   var lmdb=new Lmdb.init("/database/path");
   var lmdb=new Lmdb.init("/database/path",true); //create db if not exists

   ************************************************** */
duk_ret_t duk_rp_lmdb_constructor(duk_context *ctx)
{
    const char *dbpath=NULL;
    size_t mapsize=16 * 1048576;
    unsigned int openflags=0, rp_flags=0;
    int convtype=RP_LMDB_BUFFER;
    int maxdbs=256, create=0;
    char rdbpath[PATH_MAX];
    LMDB_ENV *lenv = NULL;
    duk_idx_t obj_idx=-1, bool_idx=-1, str_idx = -1, i=0;

//    if(!main_ctx)
//        main_ctx=ctx;//WTF -- Uh, no, no, no -- a thousand times no

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
                RP_THROW(ctx,"Lmdb.init - only one argument can be a String");
            str_idx=i;
        }
        else if(duk_is_boolean(ctx, i))
        {
            if(bool_idx > -1)
                RP_THROW(ctx,"Lmdb.init - only one argument can be a Boolean");
            bool_idx=i;
        }
        else if (duk_is_object(ctx, i) && !duk_is_array(ctx, i) && !duk_is_function(ctx, i))
        {
            if(obj_idx > -1)
                RP_THROW(ctx,"Lmdb.init - only one argument can be an Object");
            obj_idx=i;
        }
    }

    if(str_idx > -1)
        dbpath = duk_get_string(ctx, str_idx);
    else
        RP_THROW(ctx, "new Lmdb.init() requires a string (database environment location) for one of its arguments" );

    if (bool_idx>-1 && duk_get_boolean(ctx, 1) != 0)
    {
        create = 1;
        mode_t mode=0755;
        int ret = rp_mkdir_parent(dbpath, mode);
        if(ret==-1)
            RP_THROW(ctx, ": lmdb.init - error creating directory: %s", strerror(errno));
    }

    dbpath = realpath(dbpath, rdbpath);
    if(!dbpath)
        RP_THROW(ctx, "lmdb.init - error opening database at '%s': %s", duk_get_string(ctx, str_idx), strerror(errno));

    // if noCreate, check for db file, if dir exists.
    if(!create) {
        char fn[] = "data.mdb";
        char dbfile[strlen(dbpath) + strlen(fn) + 2];
        struct stat sb;

        strcpy(dbfile, dbpath);
        strcat(dbfile, "/");
        strcat(dbfile, fn);

        if (stat(dbfile, &sb) == -1)
            RP_THROW(ctx, "lmdb.init - error opening database file at '%s': %s", dbfile, strerror(errno));
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

        if(duk_get_prop_string(ctx, obj_idx, "growOnPut"))
        {
            if( REQUIRE_BOOL(ctx, -1, "lmdb.init - option growOnPut must be a Boolean"))
                rp_flags |= GROW_ON_PUT;
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "mapSize"))
        {
            double ms = REQUIRE_NUMBER(ctx, -1, "lmdb.init - option mapSize must be a positive number >= 1.0 (map/db size in Mb)");
            if(ms < 1.0)
                RP_THROW(ctx, "lmdb.init - option mapSize must be a positive number >= 1.0 (map/db size in Mb)");
            mapsize = (size_t)(1048576.0 * ms);
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

    // check if this db has been opened before
    /*    lock_main_ctx;
    if(duk_get_global_string(main_ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        duk_get_prop_string(main_ctx, -1, dbpath);
        lenv = duk_get_pointer(main_ctx, -1);
        duk_pop(main_ctx);
    }
    duk_pop(main_ctx);
    unlock_main_ctx;
    */
    //  Check if this db has been opened before.
    //  The hidden symbol should have been copied from main_ctx
    if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdbenvs")))
    {
        duk_get_prop_string(ctx, -1, dbpath);
        lenv = duk_get_pointer(ctx, -1);//null if not found
    }
    duk_pop(ctx);

    duk_push_this(ctx);
    if(lenv)
    {
        // we've got a live one.
        duk_push_pointer(ctx, (void *) lenv);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("lenv"));
        lenv = get_env(ctx); //reopen if forked or closed
    }
    else
    {
        REMALLOC(lenv, sizeof(LMDB_ENV));
        lenv->dbpath    = strdup(dbpath);
        lenv->pid       = getpid();
        lenv->openflags = openflags;
        lenv->rp_flags =  rp_flags;
        lenv->mapsize   = mapsize;
        lenv->maxdbs    = maxdbs;
        lenv->convtype  = convtype;
        lenv->env       = NULL;

        //lenv->rp_lock=RP_MINIT(&lenv->lock);
        //RPTHR_SET(lenv->rp_lock, RPTHR_LOCK_FLAG_JSFIN); //lock is unlocked in a finalizer

        lenv = redo_env(ctx, lenv);
        duk_push_pointer(ctx, (void *) lenv);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("lenv"));
    }

    /* slots for writers, per environment */
    if( !duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdb_writers")) )
    {
        duk_push_object(ctx);
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("lmdb_writers"));
    }
    duk_pop(ctx);

    /* new txn */
    duk_push_c_function(ctx, duk_rp_lmdb_new_txn, 3);

    duk_push_object(ctx); //prototype obj

    duk_push_pointer(ctx, (void *) lenv);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("lenv"));

    duk_push_c_function(ctx, duk_rp_lmdb_txn_put, 3);
    duk_put_prop_string(ctx, -2, "put");

    duk_push_c_function(ctx, duk_rp_lmdb_txn_get, 3);
    duk_put_prop_string(ctx, -2, "get");

    duk_push_c_function(ctx, duk_rp_lmdb_txn_get_ref, 2);
    duk_put_prop_string(ctx, -2, "getRef");

    duk_push_c_function(ctx, duk_rp_lmdb_txn_del, 2);
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

    duk_push_c_function(ctx, duk_rp_lmdb_txn_commit, 0);
    duk_put_prop_string(ctx, -2, "commit");

    duk_push_c_function(ctx, duk_rp_lmdb_txn_abort, 0);
    duk_put_prop_string(ctx, -2, "abort");

    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "transaction");
    /* end new txn */

    return 0;
}

/* **************************************************
   Initialize Lmdb module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    static int isinit=0;

    if(!isinit)
    {
        rp_lmdblock=RP_MINIT(&lmdblock);
        isinit=1;
    }

    duk_push_object(ctx); // the return object

    duk_push_c_function(ctx, duk_rp_lmdb_constructor, 3 /*nargs*/);

    /* Push object that will be Lmdb.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Lmdb protoObj ] */

    duk_push_c_function(ctx, duk_rp_lmdb_get, 4);
    duk_put_prop_string(ctx, -2, "get");

    duk_push_c_function(ctx, duk_rp_lmdb_get_count, 1);
    duk_put_prop_string(ctx, -2, "getCount");

    duk_push_c_function(ctx, duk_rp_lmdb_list_dbs, 1);
    duk_put_prop_string(ctx, -2, "listDbs");

    duk_push_c_function(ctx, duk_rp_lmdb_put, 3);
    duk_put_prop_string(ctx, -2, "put");

    duk_push_c_function(ctx, duk_rp_lmdb_del, 5);
    duk_put_prop_string(ctx, -2, "del");

    duk_push_c_function(ctx, duk_rp_lmdb_sync, 0);
    duk_put_prop_string(ctx, -2, "sync");

    duk_push_c_function(ctx, duk_rp_lmdb_drop, 1);
    duk_put_prop_string(ctx, -2, "drop");

    duk_push_c_function(ctx, duk_rp_lmdb_close, 0);
    duk_put_prop_string(ctx, -2, "close");

    duk_push_c_function(ctx, duk_rp_lmdb_open_db, 2);
    duk_put_prop_string(ctx, -2, "openDb");

    push_cursor_ops(ctx);


    /* Set Lmdb.prototype = protoObj */
    duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ {}, Lmdb-->[prototype-->{exe=fn_exe,...}] ] */
    duk_put_prop_string(ctx, -2, "init");      /* [ {init()} ] */

    return 1;
}
