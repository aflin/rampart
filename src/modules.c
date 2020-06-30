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
#include "rp.h"

static pthread_mutex_t modlock = PTHREAD_MUTEX_INITIALIZER;

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

duk_ret_t duk_load_native_module_string(duk_context *ctx, const char *file)
{
	pthread_mutex_lock(&modlock);
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
		pthread_mutex_unlock(&modlock);
		return 0;
	}

	initfn init = (initfn)dlsym(lib, "dukopen_module");
	pthread_mutex_unlock(&modlock);
	if (init)
	{
		duk_ret_t ret;
		ret=init(ctx);

		duk_push_string(ctx,file);
		duk_put_prop_string(ctx,-2,"rp_modfile");

		return ret;
	}
	return 0;
}

duk_ret_t duk_load_native_module(duk_context *ctx) 
{
	duk_idx_t top = duk_get_top(ctx);
	if (top != 1)
	{
		return DUK_RET_TYPE_ERROR;
	}
	const char *file = duk_get_string(ctx, -1);
	return duk_load_native_module_string(ctx,file);
}


static const char modSearch[] =
"Duktape.modSearch = function (id, require, exports, module) {\n"
"    var name=[];\n"
"    var src;\n"
"    var lib = false;\n"
"\n"
"     if(id.charAt(0)=='/'){ //assume full path, don't modify\n"
"         lib = Duktape.loadNativeModule(id);\n"
"         if(!lib) throw new Error('module not found: ' + id);\n"
"     }\n"
"\n"
"    name[0]=id;\n"
"    name[1]='rp'+id;\n"
"    name[2]='./' + id + '.so';\n"
"    name[3]='./rp' + id + '.so';\n"
"    if(Duktape.modulesPath) {\n"
"        if(Duktape.modulesPath[Duktape.modulesPath.length -1]!='/')\n"
"            Duktape.modulesPath+='/';\n"
"        name[4]=Duktape.modulesPath + id;\n"
"        name[5]=Duktape.modulesPath + 'rp' + id;\n"
"        name[6]=Duktape.modulesPath + id + '.so';\n"
"        name[7]=Duktape.modulesPath + 'rp' + id + '.so';\n"
"    }\n"
"    for (var i=0;i<name.length;i++) {\n"
"        lib = Duktape.loadNativeModule(name[i]);\n"
"        if(lib) break;\n"
"    }\n"
"\n"
"    if (lib)\n"
"    {\n"
"        for(var prop in lib) {\n"
"            exports[prop] = lib[prop];\n"
"        }\n"
"        found = true;\n"
"    }\n"
"    else throw new Error('module not found: ' + id);\n"
"\n"
"}";

/* FIXME:
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
*/

void init_modules(duk_context *ctx)
{
	duk_push_global_object(ctx);
	duk_get_prop_string(ctx, -1, "Duktape");
	duk_push_c_function(ctx, duk_load_native_module, 1);
	duk_put_prop_string(ctx, -2, "loadNativeModule");
	duk_push_c_function(ctx, read_file, 1);
	duk_put_prop_string(ctx, -2, "readFile");
	duk_pop(ctx);
	duk_pop(ctx);
	duk_eval_string(ctx, modSearch);
}
