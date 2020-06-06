/* FROM: https://github.com/davidchisnall/jsrun with gratitude, and a few changes --ajf*/

/*
 * Copyright (c) 2015 David Chisnall
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * $FreeBSD$
 */

#include <sys/stat.h>
#include <dlfcn.h>
#include <pthread.h>
#include "duktape.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef duk_ret_t(*initfn)(duk_context*);

static duk_ret_t read_file(duk_context *ctx) {
	duk_idx_t top = duk_get_top(ctx);
	if (top != 1)
	{
		return DUK_RET_TYPE_ERROR;
	}
	const char *fileName = duk_get_string(ctx, -1);
	struct stat sb;
	if (stat(fileName, &sb))
	{
		return 0;
	}
	FILE *f = fopen(fileName, "r");
	if (!f)
	{
		return 0;
	}
	void *buffer = malloc(sb.st_size);
	size_t len = fread(buffer, 1, sb.st_size, f);
	duk_push_lstring(ctx, buffer, len);
	free(buffer);
	return 1;
}

static duk_ret_t load_native_module(duk_context *ctx) {

	duk_idx_t top = duk_get_top(ctx);
	if (top != 1)
	{
		return DUK_RET_TYPE_ERROR;
	}
	const char *file = duk_get_string(ctx, -1);
	pthread_mutex_lock(&lock);
	void *lib = dlopen(file, RTLD_NOW);
	if (!lib)
	{
		const char *e=dlerror();
//printf("%s\n",e);
		if(!strstr(e,"No such file") && !strstr(e,"not found")){
//printf("%s\n",file);
			duk_push_sprintf(ctx,"error loading: %s",e);
			duk_throw(ctx);
		}
		pthread_mutex_unlock(&lock);
		return 0;
	}
	initfn init = (initfn)dlsym(lib, "dukopen_module");
	pthread_mutex_unlock(&lock);
	if (init)
	{
		return init(ctx);
	}
	return 0;
}

static const char modSearch[] =
"Duktape.modSearch = function (id, require, exports, module) {\n"
"    var name;\n"
"    var src;\n"
"    var found = false;\n"
"\n"
"    // FIXME: Should look at various default search paths.\n"
"\n"
"    // Try to load a native library\n"
"    name = id;\n"
"    var lib = Duktape.loadNativeModule(id);\n"
"    if (!lib)\n"
"    {\n"
"       name = './' + id + '.so';\n"
"       lib = Duktape.loadNativeModule(name);\n"
"    }\n"
"    if (!lib)\n"
"    {\n"
"       name = './rp' + id + '.so';\n"
"       lib = Duktape.loadNativeModule(name);\n"
"    }\n"
"    if (lib)\n"
"    {\n"
"        for(var prop in lib) {\n"
"            exports[prop] = lib[prop];\n"
"        }\n"
"        found = true;\n"
"    }\n"
"\n"
"    // Try to load a JavaScript library\n"
"    if(!found) {\n"
"        name = id + '.js';\n"
"        src = Duktape.readFile(name);\n"
"        if (typeof src === 'string')\n"
"        {\n"
"            found = true;\n"
"        }\n"
"    }\n"
"    if (!found)\n"
"    {\n"
"        throw new Error('module not found: ' + id);\n"
"    }\n"
"    return src;\n"
"}";

void init_modules(duk_context *ctx)
{
	duk_push_global_object(ctx);
	duk_get_prop_string(ctx, -1, "Duktape");
	duk_push_c_function(ctx, load_native_module, 1);
	duk_put_prop_string(ctx, -2, "loadNativeModule");
	duk_push_c_function(ctx, read_file, 1);
	duk_put_prop_string(ctx, -2, "readFile");
	duk_pop(ctx);
	duk_pop(ctx);
	duk_eval_string(ctx, modSearch);
}
