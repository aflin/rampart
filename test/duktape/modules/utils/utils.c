#include "../../../runner.h"
#include <sys/stat.h>
#include <unistd.h>
#include "duktape/core/duktape.h"
#include "duktape/core/module.h"
#define UTILS_MODULE_PATH "../../../../src/rputils"

void utils_read_file()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "file = utils.readFile({ file: 'helloworld.txt' });";

    duk_eval_string(ctx, js);

    // basic case
    assert(duk_get_length(ctx, -1) == 11);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "hello world"));

    // length
    duk_eval_string(ctx, "file = utils.readFile({ file: 'helloworld.txt', length: 10 });");
    assert(duk_get_length(ctx, -1) == 10);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "hello worl"));

    // negative length (bytes from end)
    duk_eval_string(ctx, "file = utils.readFile({ file: 'helloworld.txt', length: -1 });");
    assert(duk_get_length(ctx, -1) == 10);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "hello worl"));

    // offset
    duk_eval_string(ctx, "file = utils.readFile({ file: 'helloworld.txt', offset: 1 });");
    assert(duk_get_length(ctx, -1) == 10);
    duk_eval_string(ctx, "new TextDecoder().decode(file);");
    assert(!strcmp(duk_get_string(ctx, -1), "ello world"));

    duk_destroy_heap(ctx);
}

void utils_readln()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "var iter = utils.readln('loremipsum.txt')[Symbol.iterator]();"
        "var next = iter.next();"
        "var i = 1;"
        "var value;"
        "while(!next.done) {"
        "   if(i == 12) {"
        "       value = next.value;"
        "       break;"
        "   }"
        "   i++;"
        "   next = iter.next();"
        "}"
        "value";

    // test an arbitrary line
    duk_eval_string(ctx, js);
    assert(!strcmp(duk_get_string(ctx, -1), "Quisque consequat elit non dapibus mattis.\n"));
    duk_destroy_heap(ctx);
}

void utils_stat()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "stat = utils.stat('helloworld.txt');"
        "stat";

    duk_eval_string(ctx, js);

    // size
    duk_get_prop_string(ctx, -1, "size");
    assert(duk_get_uint(ctx, -1) == 11);
    duk_pop(ctx);

    // is_file
    duk_eval_string(ctx, "stat.is_file();");
    assert(duk_get_boolean(ctx, -1));
    duk_pop(ctx);

    // mode
    duk_get_prop_string(ctx, -1, "mode");
    assert(duk_get_uint(ctx, -1) == 0100644);
    duk_pop(ctx);

    // directory
    duk_eval_string(ctx, "stat = utils.stat('../utils');");
    duk_eval_string(ctx, "stat.is_directory();");
    assert(duk_get_boolean(ctx, -1));

    duk_destroy_heap(ctx);
}

// TODO: More tests
void utils_exec()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "execRes = utils.exec({"
        "   path: '/bin/sleep',"
        "   args: ['sleep', '0.2'],"
        "   timeout: 100000,"
        "   killSignal: utils.signals.SIGKILL,"
        "});";
    duk_eval_string(ctx, js);

    // tests path, args, timeout, killSignal
    duk_get_prop_string(ctx, -1, "timedOut");
    assert(duk_get_boolean(ctx, -1));

    const char *js2 =
        "pid = utils.exec({"
        "   path: '/bin/sleep',"
        "   args: ['sleep', '0'],"
        "   background: true,"
        "}).pid;";

    // basic background mode
    duk_eval_string(ctx, js2);
    assert(duk_get_uint(ctx, -1) != 0);

    duk_destroy_heap(ctx);
}

void utils_kill()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "var pid = utils.exec({"
        "   path: '/bin/sleep',"
        "   args: ['sleep', '0.2'],"
        "   background: true,"
        "}).pid;"
        "utils.kill(pid, 0); // will error out if not real process"
        "utils.kill(pid, 9);";

    duk_eval_string(ctx, js);
    duk_destroy_heap(ctx);
}

void utils_mkdir()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    rmdir("this/is/a/test");
    rmdir("this/is/a");
    rmdir("this/is");
    rmdir("this");

    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "utils.mkdir('this/is/a/test');";
    duk_eval_string(ctx, js);

    // should make the directory
    struct stat dir_stat;
    stat("this/is/a/test", &dir_stat);
    assert(S_ISDIR(dir_stat.st_mode));

    rmdir("this/is/a/test");
    rmdir("this/is/a");
    rmdir("this/is");
    rmdir("this");
    duk_destroy_heap(ctx);
}

void utils_copy_file()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    const char *js =
        "utils = require('" UTILS_MODULE_PATH "');"
        "utils.copyFile({ src: 'helloworld.txt', dest: 'foo.txt' });";

    duk_eval_string(ctx, js);
    // check to make sure files have the same content
    struct stat loremipsum_stat;
    struct stat helloworld_stat;
    struct stat foo_stat;
    stat("loremipsum.txt", &loremipsum_stat);
    stat("helloworld.txt", &helloworld_stat);
    stat("foo.txt", &foo_stat);

    // basic usage
    assert(foo_stat.st_size == helloworld_stat.st_size);
    duk_eval_string(ctx, "utils.copyFile({ src: 'loremipsum.txt', dest: 'foo.txt', overwrite: true });");

    // override
    stat("foo.txt", &foo_stat);
    assert(foo_stat.st_size == loremipsum_stat.st_size);

    remove("foo.txt");
    duk_destroy_heap(ctx);
}

void utils_readdir()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "utils.readdir('.').filter(function(file) { return file == 'helloworld.txt'});";

    duk_eval_string(ctx, js);

    // should find 'helloworld.txt'
    assert(duk_get_length(ctx, -1) == 1);

    duk_destroy_heap(ctx);
}

void utils_rmdir()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);

    mkdir("this", 0777);
    mkdir("this/is", 0777);
    mkdir("this/is/a", 0777);
    mkdir("this/is/a/test", 0777);

    const char *js =
        "utils = require('" UTILS_MODULE_PATH "');"
        "utils.rmdir('this/is/a/test');";

    duk_eval_string(ctx, js);

    struct stat dir_stat;
    // should delete non-recursively
    assert(stat("this/is/a/test", &dir_stat) == -1);

    stat("this/is/a", &dir_stat);
    assert(S_ISDIR(dir_stat.st_mode));

    duk_eval_string(ctx, "utils.rmdir('this/is/a', true);");

    assert(stat("this/is/a/test", &dir_stat) == -1);
    assert(stat("this/is/a", &dir_stat) == -1);
    assert(stat("this/is", &dir_stat) == -1);
    assert(stat("this", &dir_stat) == -1);

    duk_destroy_heap(ctx);
}

void utils_chmod()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    const char *js =
        "var utils = require('" UTILS_MODULE_PATH "');"
        "utils.chmod('helloworld.txt', 0o777);";

    duk_eval_string(ctx, js);

    struct stat helloworld_stat;
    stat("helloworld.txt", &helloworld_stat);

    assert(helloworld_stat.st_mode == 0100777);
    chmod("helloworld.txt", 0644);
    duk_destroy_heap(ctx);
}

void utils_touch()
{
    duk_context *ctx = duk_create_heap_default();
    duk_module_init(ctx);
    const char *js =
        "utils = require('" UTILS_MODULE_PATH "');"
        "utils.touch({ path: 'sample.txt' });";

    duk_eval_string(ctx, js);
    // basic usage
    struct stat sample_stat;
    stat("sample.txt", &sample_stat);
    assert(S_ISREG(sample_stat.st_mode));

    // updates atime and mtime
    // sleep first
    duk_eval_string(ctx, "utils.touch({ path: 'sample.txt', setaccess: true, setmodify: true });");
    time_t atime = sample_stat.st_atime;
    time_t mtime = sample_stat.st_mtime;

    stat("sample.txt", &sample_stat);
    assert(sample_stat.st_atime >= atime);
    assert(sample_stat.st_mtime >= mtime);

    // no create
    duk_eval_string(ctx, "utils.touch({ path: 'sample2.txt', nocreate: true });");
    struct stat tmp_stat;
    assert(stat("sample2.txt", &tmp_stat) != 0);

    // reference
    duk_eval_string(ctx, "utils.touch({ path: 'sample.txt', reference: 'helloworld.txt' });");
    struct stat helloworld_stat;
    stat("helloworld.txt", &helloworld_stat);
    stat("sample.txt", &sample_stat);
    assert(helloworld_stat.st_mtime == sample_stat.st_mtime);
    assert(helloworld_stat.st_atime == sample_stat.st_atime);

    // don't set access
    duk_eval_string(ctx, "utils.touch({ path: 'sample.txt', setaccess: false });");
    stat("sample.txt", &sample_stat);
    assert(helloworld_stat.st_atime == sample_stat.st_atime);

    // reset to helloworld reference
    duk_eval_string(ctx, "utils.touch({ path: 'sample.txt', reference: 'helloworld.txt' });");
    stat("sample.txt", &sample_stat);

    // don't set modify
    duk_eval_string(ctx, "utils.touch({ path: 'sample.txt', setmodify: false });");
    stat("sample.txt", &sample_stat);
    assert(helloworld_stat.st_mtime == sample_stat.st_mtime);

    remove("sample.txt");
    duk_destroy_heap(ctx);
}

void test()
{
    utils_read_file();
    utils_readln();
    utils_stat();
    utils_exec();
    utils_kill();
    utils_mkdir();
    utils_readdir();
    utils_rmdir();
    utils_chmod();
    utils_touch();
}