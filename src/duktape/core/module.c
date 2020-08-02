#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <pthread.h>
#include <libgen.h>
#include "duktape.h"
#include "module.h"
#include "../../rp.h"

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
    const char *id = duk_require_string(ctx, -3);
    duk_idx_t module_idx = duk_normalize_index(ctx, -1);
    struct stat sb;
    if (stat(id, &sb))
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not open %s: %s\n", id, strerror(errno));
        return duk_throw(ctx);
    }

    duk_push_number(ctx, sb.st_mtime);
    duk_put_prop_string(ctx, module_idx, "mtime");
    duk_push_number(ctx, sb.st_atime);
    duk_put_prop_string(ctx, module_idx, "atime");

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
    buffer[sb.st_size]='\0';

    duk_push_string(ctx, "function (module, exports) { ");
    /* check for babel and push src to stack */
    if (! duk_rp_babelize(ctx, (char *)id, buffer, sb.st_mtime) )
    {
        duk_push_lstring(ctx, buffer, sb.st_size);
    }
    duk_push_string(ctx, "\n}");
    duk_concat(ctx, 3);

    duk_push_string(ctx, id);

    duk_compile(ctx, DUK_COMPILE_FUNCTION);

    duk_dup(ctx, module_idx);
    duk_get_prop_string(ctx, -1, "exports");
    duk_call(ctx, 2);

    fclose(f);
    free(buffer);
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
        const char *dl_err = dlerror();
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Error loading: %s", dl_err);
        pthread_mutex_unlock(&modlock);
        return duk_throw(ctx);
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

static duk_ret_t resolve_id(duk_context *ctx)
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
        duk_push_null(ctx);
        return 1;
    }

    duk_push_object(ctx);
    duk_push_string(ctx, id);
    duk_put_prop_string(ctx, -2, "id");
    duk_push_int(ctx, module_loader_idx);
    duk_put_prop_string(ctx, -2, "module_loader_idx");
    free(id);
    return 1;
}

duk_ret_t duk_require(duk_context *ctx)
{
    duk_push_c_function(ctx, duk_resolve, 2);
    duk_dup(ctx, -2);
    duk_push_boolean(ctx, 0);
    duk_call(ctx, 2);
    duk_get_prop_string(ctx, -1, "exports");
    return 1;
}

duk_ret_t duk_resolve(duk_context *ctx)
{
    // TODO: Move directory logic to resolve_id
    int call_with_default_req_id = 1;
    int force_reload = duk_get_boolean_default(ctx, 1, 0);
    int module_loader_idx;
    const char *id;
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "module_stack");

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "module_stack");
    duk_get_prop_index(ctx, -1, duk_get_length(ctx, -1) - 1);

    if (!duk_is_undefined(ctx, -1))
    {
        const char *req_id = duk_get_string(ctx, 0);
        duk_get_prop_string(ctx, -1, "id");
        char *dir = dirname((char *)duk_get_string(ctx, -1)); // will be null on failure
        char req_id_w_dir[strlen(req_id) + 1 + strlen(dir) + 1];

        if (dir != NULL)
        {
            strcpy(req_id_w_dir, dir);
            strcat(req_id_w_dir, "/");
            strcat(req_id_w_dir, req_id);

            duk_push_c_function(ctx, resolve_id, 1);
            duk_push_string(ctx, req_id_w_dir);
            duk_call(ctx, 1);
            call_with_default_req_id = 0;
        }
    }

    if (call_with_default_req_id)
    {
        duk_push_c_function(ctx, resolve_id, 1);
        duk_dup(ctx, 0);
        duk_call(ctx, 1);
    }

    if (duk_is_null(ctx, -1))
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not resolve module id %s: %s\n", duk_get_string(ctx, 0), strerror(errno));
        return duk_throw(ctx);
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
        return duk_throw(ctx);
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
