/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
   Copyright (C) 2020 Benjamin Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pthread.h>
#include <libgen.h>
#include "duktape.h"
#include "module.h"
#include "rampart.h"

/* todo put tickify in its own .c and .h file */
#define ST_NONE 0
#define ST_DQ   1
#define ST_SQ   2
#define ST_BT   3
#define ST_BS   4

char * tickify(char *src, size_t sz, int *err, int *ln);
static pthread_mutex_t modlock = PTHREAD_MUTEX_INITIALIZER;

struct module_loader
{
    char *ext;
    duk_c_function loader;
};
static duk_ret_t load_so_module(duk_context *ctx);
static duk_ret_t load_js_module(duk_context *ctx);

struct module_loader module_loaders[] = {
    {".js", load_js_module},
    {".so", load_so_module},
    // if not known file extension assume javascript
    {"", load_js_module}};

static duk_ret_t load_js_module(duk_context *ctx)
{
    const char *id = duk_require_string(ctx, -3), *bfn=NULL;
    duk_idx_t module_idx = duk_normalize_index(ctx, -1);
    struct stat sb;
    if (stat(id, &sb))
        RP_THROW(ctx, "Could not open %s: %s\n", id, strerror(errno));

    duk_push_number(ctx, sb.st_mtime);
    duk_put_prop_string(ctx, module_idx, "mtime");
    duk_push_number(ctx, sb.st_atime);
    duk_put_prop_string(ctx, module_idx, "atime");

    FILE *f = fopen(id, "r");
    if (!f)
        RP_THROW(ctx, "Could not open %s: %s\n", id, strerror(errno));

    char *buffer = malloc(sb.st_size + 1);
    size_t len = fread(buffer, 1, sb.st_size, f);
    if (sb.st_size != len)
        RP_THROW(ctx, "Error loading file %s: %s\n", id, strerror(errno));

    buffer[sb.st_size]='\0';
    duk_push_string(ctx, "(function (module, exports) { ");
    /* check for babel and push src to stack */
    if (! (bfn=duk_rp_babelize(ctx, (char *)id, buffer, sb.st_mtime, 0)) )
    {
        /* No babel, normal compile */
        int err, lineno;
        char *isbabel = strstr(id, "/babel.js");
        /* don't tickify actual babel.js source */
        if ( !(isbabel && isbabel == id + strlen(id) - 9) )
        {
            char *tickified = tickify(buffer, sb.st_size, &err, &lineno);
            free(buffer);
            buffer = tickified;
            if (err)
            {
                char *msg="";
                switch (err) { 
                    case ST_BT:
                        msg="unterminated or illegal template literal"; break;
                    case ST_SQ:
                        msg="unterminated string"; break;
                    case ST_DQ:
                        msg="unterminated string"; break;
                    case ST_BS:
                        msg="invalid escape"; break;
                }
                RP_THROW(ctx, "SyntaxError: %s (line %d)\n    at %s:%d", msg, lineno, id, lineno);
            }
        }

        duk_push_string(ctx, buffer);
    }

    fclose(f);
    free(buffer);

    duk_push_string(ctx, "\n})");
    duk_concat(ctx, 3);
    if(bfn)
    {
        duk_push_string(ctx, bfn);
        free((char*)bfn);
    } 
    else
        duk_push_string(ctx, id);

    /* 
       DO NOT CALL duk_compile(ctx, DUK_COMPILE_FUNCTION)
       It will miss errors like unbalanced {} where
       there is a missing {
    */
    duk_compile(ctx, DUK_COMPILE_EVAL);
    duk_call(ctx,0);
    duk_dup(ctx, module_idx);
    duk_get_prop_string(ctx, -1, "exports");
    duk_call(ctx, 2);
    return 0;
}

static duk_ret_t load_so_module(duk_context *ctx)
{
    const char *file = duk_require_string(ctx, -3);
    duk_idx_t module_idx = duk_normalize_index(ctx, -1);
    pthread_mutex_lock(&modlock);
    void *lib = dlopen(file, RTLD_NOW);
    if (lib == NULL)
    {
        /* rampart-crypto is required by other moduels
           if not found in install path, try manually
           loading it from RAMPART_PATH                 */
        const char *dl_err = dlerror();
        char *s=strstr(dl_err, "rampart-crypto.so");
        if(s)
        {
            RPPATH rp;

            rp=rp_find_path("rampart-crypto.so", "modules/");

            if(!strlen(rp.path))
            {
                pthread_mutex_unlock(&modlock);
                RP_THROW(ctx, "Error loading: %s\n%s\n%s\n",
                    dl_err,
                    "Try setting the environment variable RAMPART_PATH to the location of the rampart directory",
                    "(the directory containing the 'bin' and 'modules' directories)");
            }
            else
            {
                void *lib2 = dlopen(rp.path, RTLD_NOW);
                if (lib2)
                {
                    lib = dlopen(file, RTLD_NOW);
                    if (lib)
                        goto libload_success;
                    else
                        dl_err = dlerror();
                }
            }
        }
        pthread_mutex_unlock(&modlock);
        RP_THROW(ctx, "Error loading: %s", dl_err);

        libload_success:
        pthread_mutex_unlock(&modlock);
    }
    duk_c_function init = (duk_c_function)dlsym(lib, "duk_open_module");
    pthread_mutex_unlock(&modlock);
    if (init != NULL)
    {
        duk_push_c_function(ctx, init, 0);
        if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
        {
            return duk_throw(ctx);
        }
        duk_put_prop_string(ctx, module_idx, "exports");
    }
    return 0;
}

static int resolve_id(duk_context *ctx, const char *request_id)
{
    char *id = NULL;
    int module_loader_idx;
    RPPATH rppath;
    size_t extlen=0;

    for (module_loader_idx = 0; module_loader_idx < sizeof(module_loaders) / sizeof(struct module_loader); module_loader_idx++)
    {
        const char *fext;

        extlen=strlen(module_loaders[module_loader_idx].ext);
        fext=request_id + ( strlen(request_id) - extlen  );

        if( extlen && !strcmp(fext,module_loaders[module_loader_idx].ext) )
        {
            rppath=rp_find_path((char *)duk_get_string(ctx,-1), "modules/");
            id = (strlen(rppath.path))?rppath.path:NULL;
        }
        else
        {
            duk_push_string(ctx, request_id);
            duk_push_string(ctx, module_loaders[module_loader_idx].ext);
            duk_concat(ctx, 2);
            rppath=rp_find_path((char *)duk_get_string(ctx,-1), "modules/");
            id = (strlen(rppath.path))?rppath.path:NULL;
            duk_pop(ctx);
        }
        if (id != NULL)
        {
            break;
        }
    }

    if (id == NULL)
    {
        return 0;
    }

    duk_push_object(ctx);
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, -2, "id");
    duk_push_int(ctx, module_loader_idx);
    duk_put_prop_string(ctx, -2, "module_loader_idx");
    return 1;
}

duk_ret_t duk_require(duk_context *ctx)
{
    duk_resolve(ctx);
    duk_get_prop_string(ctx, -1, "exports");
    return 1;
}


static duk_ret_t _duk_resolve(duk_context *ctx, const char *name)
{
    int force_reload=1;
    int module_loader_idx;
    const char *id;
    const char *fn;

    if(!name)
    {
        force_reload = duk_get_boolean_default(ctx, 1, 0);
        fn = duk_get_string(ctx,0);
    }
    else
        fn = name;

    if(!resolve_id(ctx, fn))
    {
        if(!name)
            RP_THROW(ctx, "Could not resolve module id %s: %s\n", duk_get_string(ctx, 0), strerror(errno));
        else
            return 0;
    }

    duk_get_prop_string(ctx, -1, "module_loader_idx");
    module_loader_idx = duk_get_int(ctx, -1);

    duk_get_prop_string(ctx, -2, "id");
    id = duk_get_string(ctx, -1);

    // get require() for later use
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "require");
    duk_idx_t require_idx = duk_get_top_index(ctx);

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "module_id_map");
    duk_idx_t module_id_map_idx = duk_get_top_index(ctx);

    if(force_reload)
        duk_del_prop_string(ctx, -1, id);

    // if found the module in the module_id_map
    if (duk_get_prop_string(ctx, -1, id))
        return 1;

    // module
    duk_idx_t module_idx = duk_push_object(ctx);

    // set prototype to be the global module
    duk_get_global_string(ctx, "module");
    duk_set_prototype(ctx, -2);

    // module.id
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, -2, "id");

    // module.exports
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "exports");

    // store 'module' in 'module_id_map'
    duk_dup(ctx, module_idx);
    duk_put_prop_string(ctx, module_id_map_idx, id);

    // push current module
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "module_stack");
    duk_push_string(ctx, "push");
    duk_dup(ctx, module_idx);
    duk_call_prop(ctx, -3, 1);

    // get module source using loader
    // the loader modifies module.exports
    duk_push_c_function(ctx, module_loaders[module_loader_idx].loader, 3);
    // id
    duk_push_string(ctx, id);
    // require
    duk_dup(ctx, require_idx);
    // module
    duk_dup(ctx, module_idx);

    if (duk_pcall(ctx, 3) != DUK_EXEC_SUCCESS)
    {
        if(!name)
            return duk_throw(ctx);
        else
            return -1;
    }

    // pop current module
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "module_stack");
    duk_push_string(ctx, "pop");
    duk_call_prop(ctx, -2, 0);

    // return module
    duk_dup(ctx, module_idx);
    return 1;
}

duk_ret_t duk_resolve(duk_context *ctx)
{
    return _duk_resolve(ctx, NULL);
}

int duk_rp_resolve(duk_context *ctx, const char *name)
{
    duk_idx_t idx=duk_get_top_index(ctx) + 1;
    int ret = (int)_duk_resolve(ctx, name);

    /* always return start stack size + 1
       for consistency in stack size
    */
    if (ret == 0)
    {
        duk_push_undefined(ctx);
    }

    duk_insert(ctx, idx);
 
    while (duk_get_top_index(ctx) > idx)
    {
        duk_pop(ctx);
    }
    return ret;   
}

void duk_module_init(duk_context *ctx)
{
    // put require as global so code with require can eval
    duk_push_global_object(ctx);
    duk_push_string(ctx, "require");
    duk_push_c_function(ctx, duk_require, 1);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE);

    // module.resolve
    duk_push_string(ctx, "module");
    duk_push_object(ctx);
    duk_push_c_function(ctx, duk_resolve, 2);
    duk_put_prop_string(ctx, -2, "resolve");
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE);

    // put module_id_map in global stash
    duk_push_global_stash(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "module_id_map");

    // put module_stack in global stash
    duk_push_array(ctx);
    duk_put_prop_string(ctx, -2, "module_stack");

    duk_pop_2(ctx);
}
