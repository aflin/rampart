#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pthread.h>

#include "duktape.h"
#include "module.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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
    const char *id = duk_require_string(ctx, -3);
    duk_idx_t module_idx = duk_normalize_index(ctx, -1);
    struct stat sb;
    if (stat(id, &sb))
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", id, strerror(errno));
        return duk_throw(ctx);
    }
    FILE *f = fopen(id, "r");
    if (!f)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", id, strerror(errno));
        return duk_throw(ctx);
    }
    char *buffer = malloc(sb.st_size + 1);
    size_t len = fread(buffer, 1, sb.st_size, f);
    if (sb.st_size != len)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Error loading file %s: %s\n", id, strerror(errno));
        return duk_throw(ctx);
    }
    duk_push_global_object(ctx);

    // make 'module' global
    duk_dup(ctx, module_idx);
    duk_put_prop_string(ctx, -2, "module");

    // make 'exports' global
    duk_dup(ctx, module_idx);
    duk_get_prop_string(ctx, -1, "exports");
    duk_put_prop_string(ctx, -3, "exports");

    duk_push_lstring(ctx, buffer, sb.st_size);
    if (duk_peval(ctx) == DUK_EXEC_ERROR)
    {
        return duk_throw(ctx);
    }

    fclose(f);
    free(buffer);
    return 0;
}

static duk_ret_t load_so_module(duk_context *ctx)
{
    const char *file = duk_require_string(ctx, -3);
    duk_idx_t module_idx = duk_normalize_index(ctx, -1);
    pthread_mutex_lock(&lock);
    void *lib = dlopen(file, RTLD_NOW);
    if (lib == NULL)
    {
        const char *dl_err = dlerror();
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Error loading: %s", dl_err);
        pthread_mutex_unlock(&lock);
        return duk_throw(ctx);
    }
    duk_c_function init = (duk_c_function)dlsym(lib, "duk_open_module");
    pthread_mutex_unlock(&lock);
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

static duk_ret_t require(duk_context *ctx)
{
    const char *request_id = duk_require_string(ctx, -1);

    char *id = NULL;
    int module_loader_idx;
    for (module_loader_idx = 0; module_loader_idx < sizeof(module_loaders) / sizeof(struct module_loader); module_loader_idx++)
    {
        duk_push_string(ctx, request_id);
        duk_push_string(ctx, module_loaders[module_loader_idx].ext);
        duk_concat(ctx, 2);

        id = realpath(duk_get_string(ctx, -1), 0);
        if (id != NULL)
        {
            break;
        }
    }
    if (id == NULL)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not resolve module id %s: %s\n", request_id, strerror(errno));
        return duk_throw(ctx);
    }

    // get require() for later use
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "require");
    duk_idx_t require_idx = duk_get_top_index(ctx);

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "module_id_map");
    duk_idx_t module_id_map_idx = duk_get_top_index(ctx);

    // if found the module in the module_id_map
    if (duk_get_prop_string(ctx, -1, id))
    {
        duk_get_prop_string(ctx, -1, "exports");
        free(id);
        return 1;
    }
    else
    {
        // module
        duk_idx_t module_idx = duk_push_object(ctx);

        // module.id
        duk_push_string(ctx, id);
        duk_put_prop_string(ctx, -2, "id");

        // module.exports
        duk_push_object(ctx);
        duk_put_prop_string(ctx, -2, "exports");

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
            return duk_throw(ctx);
        }

        // store module in modules id map
        duk_dup(ctx, module_idx);
        duk_put_prop_string(ctx, module_id_map_idx, id);

        // return exports
        duk_dup(ctx, module_idx);
        duk_get_prop_string(ctx, -1, "exports");

        free(id);
        return 1;
    }
}

void duk_module_init(duk_context *ctx)
{
    duk_push_global_stash(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -1, "modules");

    // put require as global so code with require can eval
    duk_push_global_object(ctx);
    duk_push_string(ctx, "require");
    duk_push_c_function(ctx, require, 1);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_HAVE_WRITABLE | DUK_DEFPROP_HAVE_ENUMERABLE | DUK_DEFPROP_ENUMERABLE);

    // put module_id_map in global stash
    duk_push_global_stash(ctx);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "module_id_map");
}