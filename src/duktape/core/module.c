/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
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

duk_ret_t duk_rp_push_current_module(duk_context *ctx)
{
    duk_idx_t top = duk_get_top(ctx);
    const char *id=NULL;

    duk_get_global_string(ctx, "Error");
    duk_push_string(ctx, "test");
    duk_new(ctx, 1);
    duk_get_prop_string(ctx, -1, "fileName");
    id=duk_get_string(ctx, -1);
    duk_pop_2(ctx); //new error, filename

    duk_push_global_stash(ctx);
    if(duk_get_prop_string(ctx, -1, "module_id_map"))
    {
        if(duk_get_prop_string(ctx, -1, id))
        {
            duk_remove(ctx, -2);//module_id_map
            duk_remove(ctx, -2);//global stash
            //id's module object is on top
            return 1;
        }
    }
    
    duk_set_top(ctx, top);
    duk_push_undefined(ctx);
    return 0;
}

static pthread_mutex_t modlock = PTHREAD_MUTEX_INITIALIZER;

typedef int (*module_load_function) (duk_context *ctx, const char *id, duk_idx_t module_idx, int is_server);

struct module_loader
{
    char *ext;
    module_load_function loader;
};

// push error, throw if not server
#define MOD_THROW(ctx,type,...) do {\
    duk_get_prop_string(ctx, module_idx, "id");\
    const char *id=duk_get_string(ctx, -1);duk_pop(ctx);\
    duk_push_global_stash(ctx);\
    duk_get_prop_string(ctx, -1, "module_id_map");\
    duk_del_prop_string(ctx, -1, id);duk_pop_2(ctx);\
    duk_push_error_object(ctx, type, __VA_ARGS__);\
    if(is_server) return 0;\
    (void) duk_throw(ctx);\
} while(0)


static int load_js_module(duk_context *ctx, const char *file, duk_idx_t module_idx, int is_server)
{
    struct stat sb;
    const char *bfn=NULL;
    if (stat(file, &sb))
        MOD_THROW(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", file, strerror(errno));

    duk_push_number(ctx, sb.st_mtime);
    duk_put_prop_string(ctx, module_idx, "mtime");
    duk_push_number(ctx, sb.st_atime);
    duk_put_prop_string(ctx, module_idx, "atime");

    FILE *f = fopen(file, "r");
    if (!f)
        MOD_THROW(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", file, strerror(errno));

    char *buffer = malloc(sb.st_size + 1);
    size_t len = fread(buffer, 1, sb.st_size, f);
    if (sb.st_size != len)
        MOD_THROW(ctx, DUK_ERR_ERROR, "Error loading file %s: %s\n", file, strerror(errno));

    buffer[sb.st_size]='\0';
    duk_push_string(ctx, "(function (module, exports) { ");

    /* check for babel and push src to stack */
    if (! (bfn=duk_rp_babelize(ctx, (char *)file, buffer, sb.st_mtime, babel_setting_none, NULL)) )
    {
        /* No babel, normal compile */
        int err, lineno;
        char *isbabel = strstr(file, "/babel.js");
        /* don't tickify actual babel.js source */

        if ( !(isbabel && isbabel == file + strlen(file) - 9) )
        {
            char *tickified = tickify(buffer, sb.st_size, &err, &lineno);
            free(buffer);
            buffer = tickified;
            if (err)
            {
                MOD_THROW(ctx, DUK_ERR_SYNTAX_ERROR, "%s (line %d)\n    at %s:%d", tickify_err(err), lineno, file, lineno);
            }
        }

        duk_push_string(ctx, buffer);
    }
    // else is babel, babelized source is on top of stack.
    fclose(f);
    free(buffer);

    duk_push_string(ctx, "\n})");
    duk_concat(ctx, 3);

    if(bfn)
    {
    //duk_push_string(ctx, bfn);
        free((char*)bfn);
    } 
    //else - we need the orig filename in error objects for duk_rp_push_current_module above
        duk_push_string(ctx, file);

    /* 
        execute (function(module,exports) {...})(module,exports);
        1) compile and call "function(){...}", which leaves function on stack
        2) call compiled function with args (module,exports)
    */
    if(is_server)
    {
        if( duk_pcompile(ctx, DUK_COMPILE_EVAL) != 0)
            return 0;
        if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
            return 0;
    }
    else
    {
        duk_compile(ctx, DUK_COMPILE_EVAL);
        duk_call(ctx,0);
    }

    duk_dup(ctx, module_idx);
    duk_get_prop_string(ctx, -1, "exports");

    if(is_server)
    {
        if (duk_pcall(ctx, 2) == DUK_EXEC_ERROR)
            return 0;
    }
    else
        duk_call(ctx, 2);

    duk_pop(ctx);
    return 1;
}

static int load_so_module(duk_context *ctx, const char *file, duk_idx_t module_idx, int is_server)
{
    pthread_mutex_lock(&modlock);
    void *lib = dlopen(file, RTLD_GLOBAL|RTLD_NOW); // --RTLD_GLOBAL is necessary for python to load .so modules properly

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

            rp=rp_find_path("rampart-crypto.so", "modules/", "lib/rampart_modules/");

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
            RP_THROW(ctx, "Error loading module '%s' - %s", file, duk_to_string(ctx, -1));
            return 0;
        }
        duk_put_prop_string(ctx, module_idx, "exports");
    }
    return 1;
}

struct module_loader module_loaders[] = {
    {".js", &load_js_module},
    {".so", &load_so_module},
    // if not known file extension assume javascript
    {"",    &load_js_module}
};

static RPPATH resolve_id(duk_context *ctx, const char *request_id)
{
    char *id = NULL;
    int module_loader_idx;
    RPPATH rppath={0};
    size_t extlen=0;
    const char *modpath=NULL;

    if(!request_id)	
        return rppath;

    if(duk_rp_push_current_module(ctx))
    {
        duk_get_prop_string(ctx, -1, "path");
        modpath=duk_get_string(ctx, -1);
        duk_pop(ctx);
    }
    duk_pop(ctx);

    for (module_loader_idx = 0; module_loader_idx < sizeof(module_loaders) / sizeof(struct module_loader); module_loader_idx++)
    {
        const char *fext;

        extlen=strlen(module_loaders[module_loader_idx].ext);
        fext=request_id + ( strlen(request_id) - extlen  );

        // we have a ".so" or a '.js'
        if( extlen && !strcmp(fext,module_loaders[module_loader_idx].ext) )
        {
            rppath=rp_find_path((char*)request_id, "modules/", "lib/rampart_modules/", modpath);
            id = (strlen(rppath.path))?rppath.path:NULL;
        }
        else
        {
            // try adding '.so' or '.js'
            duk_push_string(ctx, request_id);
            duk_push_string(ctx, module_loaders[module_loader_idx].ext);
            duk_concat(ctx, 2);
            rppath=rp_find_path((char *)duk_get_string(ctx,-1), "modules/", "lib/rampart_modules/", modpath);
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
        return rppath;
    }

    duk_push_string(ctx, id);
    duk_push_int(ctx, module_loader_idx);

    return rppath;
}

duk_ret_t duk_require(duk_context *ctx)
{
    duk_resolve(ctx);
    duk_get_prop_string(ctx, -1, "exports");
    return 1;
}

// If name is not null, we are calling from rampart-server.
// In that case, always force reload, and use pcall
static duk_ret_t _duk_resolve(duk_context *ctx, const char *name)
{
    int force_reload=1, is_babel=0;
    int module_loader_idx;
    duk_idx_t global_stash_idx, module_idx, module_id_map_idx;
    const char *id, *p;
    const char *fn;
    RPPATH rppath;

    duk_push_global_stash(ctx);
    global_stash_idx = duk_get_top_index(ctx);
    duk_get_prop_string(ctx, global_stash_idx, "module_id_map");
    module_id_map_idx = duk_get_top_index(ctx);

    if(!name)
    {
        force_reload = duk_get_boolean_default(ctx, 1, 0);
        fn = duk_get_string(ctx,0);
    }
    else
        fn = name;
    errno=0;

    //no need to keep checking babel src over and over
    if(fn && strcmp(fn,"@babel")==0)
    {
        if(duk_get_prop_string(ctx, module_id_map_idx, "@babel"))
            return 1;

        fn++;
        is_babel=1;
    }

    rppath = resolve_id(ctx, fn);//pushes id and module_loader_idx onto stack
    if(!strlen(rppath.path))
    {
        if(!name)
            RP_THROW(ctx, "Could not resolve module id %s: %s\n", duk_get_string(ctx, 0), errno? strerror(errno):"");
        else
            return 0;
    }

    module_loader_idx = duk_get_int(ctx, -1);
    id = duk_get_string(ctx, -2);

    if(force_reload)
    {
        duk_del_prop_string(ctx, -1, id);
    }
    // if found the module in the module_id_map
    if (duk_get_prop_string(ctx, module_id_map_idx, id))
    {
        time_t old_mtime;

        if(!duk_get_prop_string(ctx, -1, "mtime")) {
            // this should never happen
            duk_pop(ctx); // mtime
            duk_del_prop_string(ctx, -1, id);
        }
        else
        {
            old_mtime = (time_t) duk_get_number_default(ctx, -1, 0);
            duk_pop(ctx); //mtime
            if(!old_mtime)
            {
                //again, should never happen.
                duk_del_prop_string(ctx, -1, id);
            }
            else
            {
                if (rppath.stat.st_mtime > old_mtime)
                    duk_del_prop_string(ctx, -1, id); //its newer, reload
                else
                    return 1; // current version is up to date
            }
        }
    }
    // module
    module_idx = duk_push_object(ctx);

    // set prototype to be the global module
    duk_get_global_string(ctx, "module");
    duk_set_prototype(ctx, module_idx);

    // module.id
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, module_idx, "id");

    // module.path
    p = strrchr(id,'/');
    if(p)
        duk_push_sprintf(ctx, "%.*s", (int)(p-id), id);
    else
        duk_push_string(ctx, "");        

    duk_put_prop_string(ctx, module_idx, "path");

    // module.exports
    duk_push_object(ctx);
    duk_put_prop_string(ctx, module_idx, "exports");

    // store 'module' in 'module_id_map'
    duk_dup(ctx, module_idx);
    duk_put_prop_string(ctx, module_id_map_idx, id);

    // if @babel store under @babel also
    if(is_babel)
    {
        duk_dup(ctx, module_idx);
        duk_put_prop_string(ctx, module_id_map_idx, "@babel");
    }

    // call appropriate module loader
    if(! (module_loaders[module_loader_idx].loader)(ctx, id, module_idx, (name)?1:0 ) )
    {
        duk_del_prop_string(ctx, module_id_map_idx, id);
        return -1;
    }
    // return module
    duk_pull(ctx, module_idx);
    return 1;
}

duk_ret_t duk_resolve(duk_context *ctx)
{
    return _duk_resolve(ctx, NULL);
}

int duk_rp_resolve(duk_context *ctx, const char *name)
{
    duk_idx_t idx=duk_get_top(ctx);// i.e. duk_get_top_index(ctx) + 1;
    int ret = (int)_duk_resolve(ctx, name);

    /* always return start stack size + 1
       for consistency in stack size
    */

    duk_insert(ctx, idx);
 
    duk_set_top(ctx, idx+1);
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

    duk_pop_2(ctx);
}
