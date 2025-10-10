/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
   Copyright (C) 2022 Benjamin Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include "rampart.h"
#include "duktape/register.h"
#include "include/version.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <dirent.h>

#include "event.h"
#include "event2/thread.h"
#include "event2/dns.h"
#include "linenoise.h"
#include "sys/queue.h"
#include "whereami.h"
#include "rp_transpile.h"

int globalize=0;
int duk_rp_globalbabel=0;

int duk_rp_globaltranspile=0;

//clock_gettime for macos < sierra
#ifdef NEEDS_CLOCK_GETTIME
int clock_gettime(clockid_t type, struct timespec *rettime)
{
    mach_timespec_t clk_ts;
    clock_serv_t clksrv;
    int ret=0;

    host_get_clock_service(mach_host_self(), type, &clksrv);
    ret = (int)clock_get_time(clksrv, &clk_ts);
    mach_port_deallocate(mach_task_self(), clksrv);
    rettime->tv_sec = clk_ts.tv_sec;
    rettime->tv_nsec = clk_ts.tv_nsec;

    return ret;
}
#endif

//freebsd
#ifndef ENODATA
#define ENODATA 8088
#endif

int RP_TX_isforked=0;  //set to one in fork so we know not to lock sql db;
int totnthreads=0;
char *RP_script_path=NULL;
char *RP_script=NULL;
//duk_context **thread_ctx = NULL;
//__thread int local_thread_number=0;
duk_context *main_ctx;
RPTHR *mainthr=NULL;

struct event_base **thread_base=NULL;

struct evdns_base **thread_dnsbase=NULL;
int nthread_dnsbase=0;

/* mutex for locking main_ctx when in a thread with other duk stacks open */
pthread_mutex_t ctxlock;
RPTHR_LOCK *rp_ctxlock;

/* mutex for locking around slist operations on the timeout structures*/
pthread_mutex_t slistlock;
RPTHR_LOCK *rp_slistlock;

char *tickify_err(int err)
{
    char *msg="";
    switch (err) {
        case ST_BT:
            msg="unterminated or illegal template literal"; break;
        case ST_SR:
            msg="unterminated or illegal use of String.raw"; break;
        case ST_SQ:
        case ST_TB:
        case ST_DQ:
            msg="unterminated string"; break;
        case ST_BS:
            msg="invalid escape"; break;
        case ST_PM:
            msg="Rest parameter must be last formal parameter";break;
        case ST_PN:
            msg="Illegal parameter name";break;
    }
    return msg;
}

// Standard colors
#define RPCOL_BLK   "\x1B[30m"
#define RPCOL_RED   "\x1B[31m"
#define RPCOL_GRN   "\x1B[32m"
#define RPCOL_YEL   "\x1B[33m"
#define RPCOL_BLU   "\x1B[34m"
#define RPCOL_MAG   "\x1B[35m"
#define RPCOL_CYN   "\x1B[36m"
#define RPCOL_WHT   "\x1B[37m"

// Bright (high-intensity) colors
#define RPCOL_BBLK  "\x1B[90m"
#define RPCOL_BRED  "\x1B[91m"
#define RPCOL_BGRN  "\x1B[92m"
#define RPCOL_BYEL  "\x1B[93m"
#define RPCOL_BBLU  "\x1B[94m"
#define RPCOL_BMAG  "\x1B[95m"
#define RPCOL_BCYN  "\x1B[96m"
#define RPCOL_BWHT  "\x1B[97m"

// Reset
#define RPCOL_RESET "\x1B[0m"

char *serverscript = "//server script now resides in the rampart-webserver.js module\nvar webserv = require('rampart-webserver');webserv.cmdLine(1);\n";

/* remove for now.  revisit when out of beta.
char *upgradescript = "rampart.globalize(rampart.utils);\n"
"function dofetch(url, name) {\n"
"    var curl=require('rampart-curl');\n"
"    var res = curl.fetch(url);\n"
"    if(res.status != 200)\n"
"    {\n"
"        var err;\n"
"        if(res.errMsg && res.errMsg.length)\n"
"            err=res.errMsg;\n"
"        else if (res.status)\n"
"            err='server returned ' + res.statusText;\n"
"        else {\n"
"            if(res.body)\n"
"                delete res.body;\n"
"            err=sprintf('Unknown error: curl res = %3J', res);\n"
"        }\n"
"        printf('Failed to download %s: %s\\n',name, err);\n"
"        process.exit(1);\n"
"    }\n"
"    return res;\n"
"}\n"
"function do_up(){\n"
"    var crypto = require('rampart-crypto');\n"
"    var res = dofetch('https://rampart.dev/downloads/upgrade/upgrade.js', 'upgrade script');\n"
"    var pres = dofetch('https://raw.githubusercontent.com/rampart-dev/rampart_upgrade_key/refs/heads/main/rsa_public.pem', 'public key');\n"
"    var sres = dofetch('https://rampart.dev/downloads/upgrade/upgrade.sig', 'script signature');\n"
"    if( crypto.rsa_verify(res.text, pres.text, sres.text))\n"
"        eval(res.text);\n"
"    else\n"
"        printf('Failed to verify update script\\'s authenticity\\n')\n"
"}\n"
"try {\n"
"    do_up();\n"
"} catch(e) {\n"
"    printf('upgrade failed: %s\\n', e.message)\n"
"}\n";
*/

#define RP_REPL_GREETING   "%s"         \
    "         |>>            |>>\n"     \
    "       __|__          __|__\n"     \
    "      \\  |  /         \\   /\n"   \
    "       | ^ |          | ^ |  \n"   \
    "     __| o |__________| o |__\n"   \
    "    [__|_|__|(rp)|  | |______]\n"  \
    "____[|||||||||||||__|||||||||]____\n" \
    "RAMPART__powered_by_Duktape_" DUK_GIT_DESCRIBE "%s\n"

#define RP_REPL_PREFIX "rampart> "
#define RP_REPL_PREFIX_CONT "... "

char *words[]={
    "arguments",
    "break",
    "case",
    "catch",
    "catch (e) {",
    "console",
    "console.log",
    "const",
    "continue",
    "delete",
    "do",
    "else",
    "false",
    "for",
    "function",
    "if",
    "in",
    "instanceof",
    "isPrototypeOf",
    "new",
    "null",
    "return",
    "switch",
    "then",
    "this",
    "throw",
    "true",
    "try",
    "try {",
    "typeof",
    "undefined",
    "valueOf",
    "var",
    "while"
};

int nwords = sizeof(words)/sizeof(char*);
static char *strrpbrk(const char *str, const char *accept) {
    if (!str || !accept)
        return NULL;

    const char *p = str + strlen(str);
    while (p != str) {
        --p;
        if (strchr(accept, *p))
            return (char *)p;
    }
    return NULL;
}

static void completion(const char *inbuf, linenoiseCompletions *lc) {
    int i=0;
    char **suggwords=words;
    int nsuggwords=nwords;
    RPTHR *thr = get_current_thread();
    duk_context *ctx = thr->ctx;
    char *dotpos=NULL;
    int width=-1, curpos=0;
    struct winsize wsz;
    char *endchar = " (;{=<>/*-+|&!^?:[";
    const char *startpos = inbuf;
    const char *s = strrpbrk(inbuf, endchar);
    int startlen = 0;
    int insq=0;
    int indq=0;

    if(s)
    {
        s++;
        while( isspace(*s) )
            s++;
        startlen = s - startpos;
        inbuf=s;
    }
    int baselen = strlen(inbuf);

    s=startpos;
    while(*s)
    {
        switch(*s){
            case '\'':
                if(!indq) insq=!insq;
                break;
            case '"':
                if(!insq) indq=!indq;
                break;
        }
        s++;
    }

    // Query terminal size
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wsz) != -1)
        width = wsz.ws_col -3;

    // filename/path completion
    if(insq || indq)
    {
        char qc = insq ? '\'' : '"';
        inbuf = strrchr(startpos, qc);
        inbuf++;
        if(*inbuf == '/' || 
          ( *inbuf == '.' && *(inbuf + 1) == '/' ) ||
          ( *inbuf == '.' && *(inbuf + 1) == '.' && *(inbuf + 2) == '/' ) 
        )
        {
            char *s2=strrchr(inbuf, '/');
            size_t pathlen = (size_t)(s2-inbuf)+1;
            char *path = strndup(inbuf, pathlen);
            DIR *dir = opendir(path);
            if(dir)
            {
                struct dirent *entry=NULL, *firstentry=NULL;
                int longest = 0;
                int postlen = strlen(s2)-1;
                int nsugg=0;

                while ((entry = readdir(dir)) != NULL)
                {
                    if( !strcmp(".", entry->d_name) || !strcmp("..", entry->d_name))
                        continue;
                    if( postlen && strncmp(inbuf+pathlen, entry->d_name, postlen)!=0 )
                        continue;

                    rp_string *sugg = rp_string_new(32);
                    rp_string_putsn(sugg, startpos, startlen);
                    rp_string_puts(sugg, entry->d_name);
                    linenoiseAddCompletion(lc, sugg->str);
                    if(sugg->len > longest)
                        longest = sugg->len;
                    rp_string_free(sugg);
                    if(!nsugg)
                        firstentry = entry;
                    nsugg++;
                }

                if(nsugg > 1)
                {
                    int gotone=0;

                    longest +=4;

                    linenoiseAddCompletionUnshift(lc, startpos);
                    rewinddir(dir);
                    while ((entry = readdir(dir)) != NULL)
                    {
                        int slen = strlen(entry->d_name);
                        if( !strcmp(".", entry->d_name) || !strcmp("..", entry->d_name))
                            continue;
                        if( postlen && strncmp(inbuf+pathlen, entry->d_name, postlen)!=0 )
                            continue;

                        if(gotone==0)
                        {
                            printf("\n   ");
                            gotone=1;
                        }
                        if(curpos + longest + 3 > width)
                        {
                            printf("\n   ");
                            curpos=0;
                        }
                        printf("%s%s%*s%s", RPCOL_BBLK, entry->d_name, (int)(3+longest - slen), " ", RPCOL_RESET);
                        curpos += longest + 3;
                    }
                    if(gotone)
                        putchar('\n');
                }

                // check if it is a directory
                else if(nsugg == 1)
                {
                    struct stat st;
                    char *p=NULL;
                    REMALLOC(p, pathlen + sizeof(firstentry->d_name)+2);
                    strcpy(p,path);
                    strcat(p, firstentry->d_name);
                    stat(p, &st);
                    if(S_ISDIR(st.st_mode))
                    {
                        rp_string *sugg = rp_string_new(32);
                        rp_string_putsn(sugg, startpos, startlen);
                        rp_string_puts(sugg, firstentry->d_name);
                        rp_string_putc(sugg, '/');
                        linenoiseReplaceCompletion(lc, sugg->str,0);
                        rp_string_free(sugg);
                    }
                    free(p);
                }
                else
                    linenoiseAddCompletion(lc, startpos);

                closedir(dir);
            }
            free(path);
            return;
        }
    }

    // object/method completion
    if(baselen && !(insq || indq))
    {
        dotpos=strrchr(inbuf, '.');
        int postlen=0;
        if(dotpos)
        {
            baselen = (int)(dotpos-inbuf);
            dotpos++;
            postlen = strlen(dotpos);
        }
        duk_push_sprintf(ctx, "%.*s;", baselen, inbuf);
        if(duk_peval(ctx) == 0)
        {
            if(duk_is_string(ctx, -1))
            {
                duk_push_string(ctx, "String.prototype");
                duk_eval(ctx);
                duk_remove(ctx, -2);
            }
            else if(duk_is_number(ctx, -1))
            {
                duk_push_string(ctx, "Number.prototype");
                duk_eval(ctx);
                duk_remove(ctx, -2);
            }
            if(duk_is_object(ctx, -1) || duk_is_buffer_data(ctx, -1))
            {
                int gotone=0, longest=0, nsugg=0;
                duk_enum(ctx, -1, DUK_ENUM_INCLUDE_NONENUMERABLE);
                rp_string *sugg = NULL, *firstsugg=NULL;
                while(duk_next(ctx, -1, 1))
                {
                    duk_size_t slen;
                    const char *sym = duk_get_lstring(ctx, -2, &slen);
                    int isfunc = duk_is_function(ctx, -1);
                    duk_pop_2(ctx);
                    if(isdigit(*sym))
                        continue;
                    if(dotpos && strncmp(dotpos, sym, postlen)!=0)
                        continue;

                    sugg = rp_string_new(32);
                    if(startlen)
                        rp_string_putsn(sugg, startpos, startlen);

                    rp_string_putsn(sugg, inbuf, baselen);
                    rp_string_putc(sugg, '.');
                    rp_string_puts(sugg,sym);
                    if(isfunc)
                        rp_string_putc(sugg, '(');

                    linenoiseAddCompletion(lc, sugg->str);
                    if(!nsugg)
                        firstsugg=sugg;
                    else
                        sugg=rp_string_free(sugg);

                    if((int)slen > longest)
                        longest = (int)slen;
                    nsugg++;
                }
                duk_pop(ctx); //enum
                if(nsugg == 1)
                {
                    rp_string_putc(firstsugg, '.');
                    linenoiseReplaceCompletion(lc, sugg->str, 0);
                    firstsugg = rp_string_free(firstsugg);
                }
                else if(nsugg > 1)
                {
                    linenoiseAddCompletionUnshift(lc, startpos);
                    duk_enum(ctx, -1, DUK_ENUM_INCLUDE_NONENUMERABLE);
                    while(duk_next(ctx, -1, 1))
                    {
                        duk_size_t slen;
                        const char *sym = duk_get_lstring(ctx, -2, &slen);
                        int isfunc = duk_is_function(ctx, -1);
                        duk_pop_2(ctx);
                        if(isdigit(*sym))
                            continue;
                        if(dotpos && strncmp(dotpos, sym, postlen)!=0)
                            continue;
                        if(gotone==0)
                        {
                            printf("\n   ");
                            gotone=1;
                        }
                        if(curpos + longest + 3 + isfunc *2> width)
                        {
                            printf("\n   ");
                            curpos=0;
                        }
                        if(isfunc)
                            printf("%s%s()%*s%s", RPCOL_BBLK, sym, (int)(3+longest - slen -2), " ", RPCOL_RESET);
                        else
                            printf("%s%s%*s%s", RPCOL_BBLK, sym, (int)(3+longest - slen), " ", RPCOL_RESET);
                        curpos += longest + 3 + isfunc *2;
                    }
                    if(gotone)
                        putchar('\n');
                    duk_pop(ctx); //enum
                }
                if(firstsugg)
                    rp_string_free(firstsugg);
            }
            duk_pop(ctx); //eval ret
            return;
        }
    }
    // if no dot, look for global symbols
    if(!dotpos && !(insq || indq) )
    {
        int inlen = strlen(inbuf), gotone=0, longest=0;

        duk_push_global_object(ctx);

        int nsugg=0;
        duk_enum(ctx, -1, DUK_ENUM_INCLUDE_NONENUMERABLE);
        while(duk_next(ctx, -1, 1))
        {
            duk_size_t slen;
            const char *sym = duk_get_lstring(ctx, -2, &slen);
            int isfunc = duk_is_function(ctx, -1);
            duk_pop_2(ctx);
            if(inlen && strncmp(inbuf, sym, inlen) != 0)
                continue;
            rp_string *sugg = rp_string_new(32);
            rp_string_putsn(sugg, startpos, startlen);
            rp_string_puts(sugg, sym);
            if(isfunc)
                rp_string_putc(sugg, '(');

            linenoiseAddCompletion(lc, sugg->str);
            rp_string_free(sugg);

            if((int)slen > longest)
                longest = (int)slen;
            nsugg++;
        }
        duk_pop(ctx);  // enum

        if(nsugg > 1)
        {
            longest +=4;
            duk_enum(ctx, -1, DUK_ENUM_INCLUDE_NONENUMERABLE);
            while(duk_next(ctx, -1, 1))
            {
                duk_size_t slen;
                const char *sym = duk_get_lstring(ctx, -2, &slen);
                int isfunc = duk_is_function(ctx, -1);
                duk_pop_2(ctx);
                if(inlen && strncmp(inbuf, sym, inlen) != 0)
                    continue;
                if(gotone==0)
                {
                    printf("\n   ");
                    gotone=1;
                }
                if(curpos + longest + 3 + isfunc *2> width)
                {
                    printf("\n   ");
                    curpos=0;
                }
                if(isfunc)
                    printf("%s%s()%*s%s", RPCOL_BBLK, sym, (int)(3+longest - slen -2), " ", RPCOL_RESET);
                else
                    printf("%s%s%*s%s", RPCOL_BBLK, sym, (int)(3+longest - slen), " ", RPCOL_RESET);
                curpos += longest + 3 + isfunc *2;
            }
            if(gotone)
                putchar('\n');
            duk_pop(ctx); //enum
        }
        duk_pop(ctx);  //global object

        for (i=0;i<nsuggwords;i++)
        {
            char *sym = suggwords[i];
            if(inlen && strncmp(inbuf, sym, inlen) != 0)
                continue;
            linenoiseAddCompletion(lc, sym);
        }
    }

}

#define EXIT_FUNC struct rp_exit_funcs_s
EXIT_FUNC {
    rp_vfunc func;
    void     *arg;
//char *nl;
};

EXIT_FUNC **exit_funcs = NULL;
EXIT_FUNC **b4loop_funcs = NULL;

pthread_mutex_t exlock;


//void add_exit_func_2(rp_vfunc func, void *arg, char *nl)
void add_exit_func(rp_vfunc func, void *arg)
{
    int n=0;
    EXIT_FUNC *ef;
    RP_PTLOCK(&exlock);
    if(exit_funcs)
    {
        /* count number of funcs */
        while( (ef=exit_funcs[n++]) );

        n++;
    }
    else
        n=2;

    ef=NULL;

    REMALLOC(ef, sizeof(EXIT_FUNC));
    REMALLOC(exit_funcs, n * sizeof (EXIT_FUNC *));
    ef->func = func;
    ef->arg=arg;
    //ef->nl=nl;
    exit_funcs[n-2]=ef;
    exit_funcs[n-1]=NULL;
    RP_PTUNLOCK(&exlock);
}

void add_b4loop_func(rp_vfunc func, void *arg)
{
    int n=0;
    EXIT_FUNC *ef;
    RP_PTLOCK(&exlock);
    if(b4loop_funcs)
    {
        /* count number of funcs */
        while( (ef=b4loop_funcs[n++]) );

        n++;
    }
    else
        n=2;

    ef=NULL;

    REMALLOC(ef, sizeof(EXIT_FUNC));
    REMALLOC(b4loop_funcs, n * sizeof (EXIT_FUNC *));
    ef->func = func;
    ef->arg=arg;
    //ef->nl=nl;
    b4loop_funcs[n-2]=ef;
    b4loop_funcs[n-1]=NULL;
    RP_PTUNLOCK(&exlock);
}

//lock is probably not necessary now
static void free_dns(void)
{
    int i=0;

    if(!thread_dnsbase)
        return;
    CTXLOCK;
    while(i<nthread_dnsbase)
    {
        evdns_base_free(thread_dnsbase[i], 0);
        i++;
    }
    nthread_dnsbase=0;
    free(thread_dnsbase);
    thread_dnsbase=NULL;
    CTXUNLOCK;
}


//these are for unloading modules
extern void **rp_opened_mods;
extern size_t rp_n_opened_mods;


void duk_rp_exit(duk_context *ctx, int ec)
{
    int i=0,len=0;
    static int ran_already=0;

    if(ran_already)
        exit(ec);
    ran_already=1;

    if(main_babel_opt)
        free(main_babel_opt);
    free_dns();

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "exitfuncs");
    len=duk_get_length(ctx, -1);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
        duk_call(ctx,0);
        duk_pop(ctx);
    }

    duk_destroy_heap(ctx);
    free(RP_script_path);
    free(RP_script);

    // run added exit functions
    if(exit_funcs)
    {
        EXIT_FUNC *ef;
        int n=0;

        while( (ef=exit_funcs[n++]) )
        {
            (ef->func)(ef->arg);
            free(ef);
        }
        free(exit_funcs);
    }

    // close opened modules AFTER exit_funcs
    for (i=0; i<rp_n_opened_mods;i++)
    {
        void *h = rp_opened_mods[i];
        if(h)
            (void)dlclose(h);
    }

    exit(ec);
}

void run_b4loop_funcs()
{
    if(b4loop_funcs)
    {
        EXIT_FUNC *ef;
        int n=0;

        while( (ef=b4loop_funcs[n++]) )
        {
            (ef->func)(ef->arg);
            free(ef);
        }
        free(b4loop_funcs);
    }

}

static void evhandler_repl(int sig, short events, void *flag)
{
    if(flag == NULL)
    {
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
    }
    else
    {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
    }
}


char * tickify(char *src, size_t sz, int *err, int *ln);
int rp_color=1;
int rp_quiet=0;
pthread_mutex_t repl_lock;
#define REPL_LOCK    do {RP_PTLOCK(&repl_lock); /*printf("Locked\n"); */ } while(0)
#define REPL_UNLOCK  do {RP_PTUNLOCK(&repl_lock); /* printf("Unlocked\n"); */} while (0)

static void *repl_thr(void *arg)
{
    char *red="", *reset="", *blue="";
    char *line=NULL, *lastline=NULL;
    char *prefix=RP_REPL_PREFIX;
    char histfn[PATH_MAX];
    char *hfn=NULL, *babelscript=NULL;
    char *home = getenv("HOME");
    //int err;
    duk_context *ctx = (duk_context *) arg;
    RP_ParseRes res;

    if(rp_color)
    {
        red=RPCOL_RED;
        blue=RPCOL_BLU;
        reset=RPCOL_RESET;
    }

    if (!rp_quiet)
    {
        printf(RP_REPL_GREETING, blue, reset);
    }

    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(completion);
    linenoiseHistorySetMaxLen(1024);
    if(home){
        strcpy(histfn, home);
        strcat(histfn, "/.rampart_history");
        hfn = histfn;
        linenoiseHistoryLoad(hfn);
    }

    rp_print_simplified_errors=0;
    rp_print_error_lines=0;

    while (1)
    {
        int cont=0;
        char *oldline=NULL;

        if(lastline)
        {
            prefix=RP_REPL_PREFIX_CONT;
            cont=1;
        }
        else
            prefix=RP_REPL_PREFIX;

        errno=0;
        line = linenoise(prefix);

        if(!line)
        {
            if(errno == EAGAIN || /* ctrl-c */
                (errno == ENODATA && cont) /* ctrl-c on a ... continue line */
              )
            {
                printf("%spress ctrl-c again to exit%s\n", blue, reset);
                if(lastline)
                {
                    free(lastline);
                    lastline=NULL;
                }
                continue;
            }
            if(babelscript)
                free(babelscript);
            if(lastline)
                free(lastline);
            duk_rp_exit(ctx, 0);
        }

        if(*line=='\0')
        {
            free(line);
            continue;
        }

        oldline = line;
        linenoiseHistoryAdd(oldline);
        if(duk_rp_globalbabel)
        {
            REPL_LOCK;
            char *error=NULL, *tryscript=NULL;
            const char *res=NULL, *bline=NULL;

            if(lastline){

                if(*line=='\0')
                {
                    lastline=strcatdup(lastline, "\n");
                    free(line);
                    continue;
                }
                lastline = strcatdup(lastline, line);
                free(line);
                line=lastline;
            }
            lastline=NULL;
            //re-transpile entire script, but extract only last line
            if(babelscript)
                tryscript = strdup(babelscript);
            tryscript=strcatdup(tryscript, line);
            tryscript=strcatdup(tryscript, "\n");
            error=(char *)duk_rp_babelize(ctx, "eval_code", tryscript, 0, babel_setting_eval, main_babel_opt);
            if(error)
            {
                char *end=NULL;

                if(strstr(error, "SyntaxError: Unexpected token") ) //command likely spans multiple lines
                {
                    lastline=line;
                    free(error);
                    free(tryscript);
                    REPL_UNLOCK;
                    continue;
                }
                end = strstr(error,"(");
                if(end)
                    printf("%s%.*s%s\n", red, (int)(end-error), error, reset);
                else
                    printf("%s%s%s\n", red, error, reset);
                free(error);
                free(line);
                free(tryscript);
                REPL_UNLOCK;
                continue;
            }
            else
            {
                res=duk_get_string(ctx, -1);
                bline=res + strlen(res);
                while(bline > res && *bline != '\n') bline--;
                if(*bline =='\n') bline++;
                free(line);
                line=strdup(bline);
                duk_pop(ctx);
                if(babelscript)
                    free(babelscript);
                babelscript=tryscript;
            }
            REPL_UNLOCK;
        }
        else
            //line = tickify(line, strlen(line), &err, &ln);
        {
            res = rp_get_transpiled(line, NULL);
            if(res.transpiled)
            {
                line=res.transpiled;
                res.transpiled=NULL;
            }
            else
                line=strdup(oldline);//mimic what tickify used to do
            freeParseRes(&res);
        }
        if(!duk_rp_globalbabel)
        {
            if (!line)
                line=oldline;
            else if(line != oldline)
                free(oldline);

            if(lastline){

                if(*line=='\0')
                {
                    lastline=strcatdup(lastline, "\n");
                    free(line);
                    continue;
                }
                lastline = strjoin(lastline, line, '\n');
                free(line);
                line=lastline;

                oldline=line;
                //line = tickify(line, strlen(line), &err, &ln);
                res = rp_get_transpiled(line, NULL);
                if(res.transpiled)
                {
                    line=res.transpiled;
                    res.transpiled=NULL;
                }
                else
                    line=strdup(oldline);
                freeParseRes(&res);

                if (!line)
                    line=oldline;
                else
                    free(oldline);
            }
        }

        // if pos of error is at beginning of line or at a '`' , its likely a multiliner
        if(
            res.err &&
                (
                    res.pos==0          ||
                    line[res.pos]=='\'' ||
                    line[res.pos]=='"'  ||
                    line[res.pos]=='`'
                )
        )
        {
            lastline=line;
            continue;
        }

        REPL_LOCK;
        evhandler_repl(0, 0, (void *)1);
        //printf("Doing %s\n", line);
        duk_push_string(ctx, line);
        lastline=NULL;

        // evaluate input
       if (duk_peval(ctx) != 0)
        {
            const char *errmsg=duk_safe_to_string(ctx, -1);
            if(strstr(errmsg, "end of input") /* || (err && err < 5) */ ) //command likely spans multiple lines
            {
                lastline=line;
            }
            //else if(err)
            //    printf("%sERROR: %s%s\n", red, tickify_err(err), reset);
            else
                printf("%s%s%s\n", red, errmsg, reset);
        }
        else
        {
            if(duk_is_object(ctx, -1) && !duk_is_function(ctx, -1)) {
                duk_push_string(ctx, "rampart.utils.sprintf");
                duk_eval(ctx);
                if(rp_color) {
                    duk_push_string(ctx, "%!A3J");
                    duk_push_null(ctx);
                    duk_pull(ctx, -4);
                    duk_call(ctx, 3);

                }
                else
                {
                    duk_push_string(ctx, "%!3J");
                    duk_pull(ctx, -3);
                    duk_call(ctx, 2);
                }
            }
            printf("%s%s%s\n", blue, duk_safe_to_stacktrace(ctx, -1), reset);
        }
        duk_pop(ctx); //the results

        //resume loop by locking main thread
        evhandler_repl(0, 0, NULL);
        REPL_UNLOCK;

        if(hfn)
            linenoiseHistorySave(hfn);

        if(!lastline)
            free(line);

    }
    return NULL;
}


static int repl(duk_context *ctx)
{
    pthread_t thr;
    pthread_attr_t attr;
    struct event ev_sig;
    struct timeval to={0};

    RP_PTINIT(&repl_lock);

    pthread_attr_init(&attr);

    if( pthread_create( &thr, &attr, repl_thr, (void*)ctx) )
        RP_THROW(ctx, "Could not create thread\n");

    event_assign(&ev_sig, mainthr->base, -1,  0, evhandler_repl, NULL);
    event_add(&ev_sig, &to);

    /* start event loop, but don't block repl thread */
    while(1)
    {
        REPL_LOCK;
        event_base_loop(mainthr->base, EVLOOP_NONBLOCK);
        REPL_UNLOCK;
        usleep(50000);
    }

    // won't get here
    pthread_join(thr, NULL);

    return 0;
}
static char * load_polyfill(duk_context *ctx, int setting)
{
    char *pfill="babel-polyfill.js";
    char *pfill_bc=".babel-polyfill.bytecode";
    duk_size_t bsz=0;
    void *buf;
    RPPATH rppath;
    char *babelcode=NULL;
    FILE *f;
    size_t read;

    /* check if polyfill is already loaded */
    duk_eval_string(ctx,"global._babelPolyfill");
    if(duk_get_boolean_default(ctx,-1,0))
    {
        duk_pop(ctx);
        return NULL;
    }
    duk_pop(ctx);

    rppath=rp_find_path(pfill_bc, "modules/", "lib/rampart_modules/");

    if(strlen(rppath.path))
    {
        pfill_bc=rppath.path;
        /* load polyfill bytecode cache */
        f=fopen(pfill_bc,"r");
        if(!f)
        {
            fprintf(stderr,"cannot open '%s': %s\n",pfill_bc,strerror(errno));
        }
        else
        {
            buf=duk_push_fixed_buffer(ctx,(duk_size_t)rppath.stat.st_size);

            read=fread(buf,1,rppath.stat.st_size,f);
            if(read != rppath.stat.st_size)
            {
                fprintf(stderr,"error fread(): error reading file '%s'\n",pfill_bc);
            }
            else
            {
                duk_load_function(ctx);
                goto callpoly;
            }
        }
    }

    /* not found, so load and save it */
    rppath=rp_find_path(pfill, "modules/", "lib/rampart_modules/");

    if (!strlen(rppath.path))
    {
        fprintf(stderr,"cannot locate babel-polyfill.min.js\n");
        exit(1);
    }
    pfill=rppath.path;

    f=fopen(pfill,"r");
    if(!f)
    {
        fprintf(stderr,"cannot open '%s': %s\n",pfill,strerror(errno));
        exit(1);
    }
    REMALLOC(babelcode, rppath.stat.st_size);
    read=fread(babelcode, 1, rppath.stat.st_size,f);
    if(read != rppath.stat.st_size)
    {
        fprintf(stderr,"error fread(): error reading file '%s'\n",pfill);
        exit(1);
    }
    duk_push_lstring(ctx,babelcode,(duk_size_t)rppath.stat.st_size);
    free(babelcode);
    babelcode=NULL;
    fclose(f);

    duk_push_string(ctx, pfill);
    if (duk_pcompile(ctx, 0) == DUK_EXEC_ERROR)
    {
        const char *errmsg = rp_push_error(ctx, -1, NULL, rp_print_error_lines);
        if(setting==babel_setting_eval)
            return strdup(errmsg);
        fprintf(stderr, "%s\n", errmsg);
        duk_rp_exit(ctx, 1);
    }

    /* write bytecode out */
    rppath=rp_get_home_path(pfill_bc,"modules/");
    pfill_bc=rppath.path;
    duk_dup(ctx,-1);
    duk_dump_function(ctx);
    buf=duk_get_buffer_data(ctx,-1,&bsz);
    if(!strlen(pfill_bc) || !(f=fopen(pfill_bc,"w")) )
    {
        fprintf(stderr,"cannot open '%s' for write: %s\n",pfill_bc,strerror(errno));
    }
    else
    {
        size_t wrote;
        wrote=fwrite(buf,1,(size_t)bsz,f);
        if(wrote!=(size_t)bsz)
        {
            fprintf(stderr,"error fwrite(): error writing file '%s'\n",pfill_bc);
            if(wrote>0 && unlink(pfill_bc))
            {
                fprintf(stderr,"error unlink(): error removing '%s'\n",pfill_bc);
            }
        }
        fclose(f);
    }
    duk_pop(ctx);

    /* call the polyfill */
    callpoly:

    if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_rp_exit(ctx, 1);
    }
    duk_pop(ctx);
    duk_eval_string(ctx,"global._babelPolyfill=true;");
    duk_pop(ctx);
    return NULL;
}

static char *checkuse(char *src)
{
    char *s=src, *uline, *ret=NULL;

    /* skip comments at top of file */
    while(isspace(*s)) s++;
    while (*s=='/')
    {
        s++;
        if(*s=='/')
        {
            while(*s && *s!='\n') s++;
            if(!*s) break;
        }
        else if (*s=='*')
        {
            char *e=(char*)memmem(s,strlen(s),"*/",2);
            if(e)
                s=e+2;
            else
                break;
        }
        else
            break;
        while(isspace(*s)) s++;
    }
    uline=s;

    if(!strncmp("\"use ",s,5) )
    {
        char *p, c;

        s+=5;
        while(isspace(*s))
            s++;
        p=s;

        while(p && !isspace(*p)  && *p!='"')
            p++;
        c=*p;
        if(!c)
            return NULL;
        *p='\0';
        ret=strdup(s);
        *p=c;
        /* replace "use xxx" line with spaces, to preserve line nums */
        while (*uline && *uline!='\n') *uline++ = ' ';
        return(ret);
    }
    return NULL;
}

RP_ParseRes rp_get_transpiled(char *src, int *is_tickified)
{
    RP_ParseRes ret = {0};

    size_t src_sz = strlen(src);

    if(!duk_rp_globaltranspile)
    {
        /* check for "use transpiler" */
        char *use = checkuse(src);

        if(!use)
            goto do_tickify;
        if( strcasecmp("transpilerGlobally", use) == 0)
            duk_rp_globaltranspile=1;
        else if( strcmp("transpiler", use) != 0)
        {
            free(use);
            goto do_tickify;
        }
        if(use)
            free(use);
    }

    ret = transpile((const char *)src, src_sz, 0);
    if(is_tickified)
        *is_tickified=0;
    return ret;

    do_tickify:

    if(is_tickified)
        *is_tickified=1;

    ret.transpiled = tickify(src, src_sz, &(ret.err), &(ret.line_num));
    if (ret.err)
    {
        size_t errsz = 128 + strlen(tickify_err(ret.err));

        REMALLOC(ret.errmsg, errsz);

        snprintf(ret.errmsg, errsz, "SyntaxError: %s (line %d)\n", tickify_err(ret.err), ret.line_num);
    }

    return ret;
}

static char *checkbabel(char *src)
{
    char *s=src, *bline, *ret=NULL;

    /* skip comments at top of file */
    while(isspace(*s)) s++;
    while (*s=='/')
    {
        s++;
        if(*s=='/')
        {
            while(*s && *s!='\n') s++;
            if(!*s) break;
        }
        else if (*s=='*')
        {
            char *e=(char*)memmem(s,strlen(s),"*/",2);
            if(e)
                s=e+2;
            else
                break;
        }
        else
            break;
        while(isspace(*s)) s++;
    }
    //printf("'%s'\n",s);
    bline=s;
    /* check for use "babel:{options}" or use "babel" */
#define invalidformat do{\
    fprintf(stderr,"invalid format: \"use babel:{ options }\"\n");\
    exit(1);\
}while(0)

    if(!strncmp("\"use ",s,5) )
    {
        if(*s=='\n') s++;
        s+=5;
        if (!strncmp("babel",s,5))
        {
            char *e;
            s+=5;
            if(!strncmp("Globally",s,8))
            {
                s+=8;
                duk_rp_globalbabel=1;
            }
            while(isspace(*s)) s++;
            if(*s==':')
            {
                s++;
                while(isspace(*s)) s++;
                if(*s!='{')
                    invalidformat;
                e=s;
                /* must end with } before a " or \n */
                while(*e != '"' && *e != '\n') e++;
                if(*e!='"')
                    invalidformat;

                e--;
                while(isspace(*e)) e--;

                if(*e!='}')
                    invalidformat;

                e++;

                {
                    char opt[1+e-s];

                    strncpy(opt,s,e-s);
                    opt[e-s]='\0';
                    //file_src=(char *)duk_rp_babelize(ctx, argv[0], file_src, opt, entry_file_stat.st_mtime);
                    ret=strdup(opt);
                }
            }
            else if (*s=='"')
            {
                //file_src=(char *)duk_rp_babelize(ctx, argv[0], file_src, NULL, entry_file_stat.st_mtime);
                ret=strdup ("{ presets: ['env'],retainLines:true }");
            }
            /* replace "use babel" line with spaces, to preserve line nums */
            while (*bline && *bline!='\n') *bline++ = ' ';
            return(ret);
        }
        return NULL;
    }
    return NULL;
}

char *main_babel_opt=NULL;


/* babelized source is left on top of stack*/
const char *duk_rp_babelize(duk_context *ctx, char *fn, char *src, time_t src_mtime, int setting, char *opt)
{
    char *s, *babelcode=NULL;
    struct stat babstat;
    char babelsrc[strlen(fn)+10];
    FILE *f;
    size_t read;
    duk_size_t bsz=0;

    babelsrc[0]='\0';

    if(duk_rp_globalbabel)
    {
        char *isbabel = strstr(fn, "/babel.js");
        /* don't tickify actual babel.js source */

        if ( isbabel && isbabel == fn + strlen(fn) - 9 )
            return NULL;
        opt=main_babel_opt;

        char *use = checkuse(src);

        if(use && strcmp("transpiler", use) != 0)
        {
            /* IF we used --use-babel
               or if we are in a module, where the main script has
               "use babelGlobally" and the module has "use transpiler"
               THEN don't babelize the source.                           */
            free(use);
            return NULL;
        }

        if(use)
            free(use);
    }
    else if(!opt)
    {
        // if not global, we are overwriting main_babel_opt below
        if(main_babel_opt)
        {
            free(main_babel_opt);
            main_babel_opt=NULL;
        }
        opt=checkbabel(src);
        if(!opt) return NULL;
    }

    main_babel_opt=opt;

    char *err = load_polyfill(ctx, setting);
    if(err) /* for babel_setting_eval */
        return err;

    if(strcmp("stdin",fn) != 0  && strcmp("eval_code",fn) != 0)
    {
        /* file.js => file.babel.js */
        /* skip the first char in case of "./file" */
        s=strrchr(fn+1,'.');
        if(s)
        {
            size_t l=s-fn;
            strncpy(babelsrc,fn,l);
            babelsrc[l]='\0';
            strcat(babelsrc,".babel");
            strcat(babelsrc,s);
        }
        else
        {
            strcpy (babelsrc, fn);
            strcat (babelsrc, ".babel.js");
        }

        /* does the file.babel.js exist? */
        if (stat(babelsrc, &babstat) != -1)
        {
            /* is it newer than the file.js */
            if(babstat.st_mtime >= src_mtime)
            {
                /* load the cached file.babel.js */
                REMALLOC(babelcode,babstat.st_size);

                f=fopen(babelsrc,"r");
                if(f==NULL)
                {
                    fprintf(stderr,"error fopen(): error opening file '%s': %s\n",babelsrc,strerror(errno));
                }
                else
                {
                    read=fread(babelcode, 1, babstat.st_size, f);

                    if(read != babstat.st_size)
                    {
                        fprintf(stderr,"error fread(): error reading file '%s'\n", babelsrc);
                    }
                    if(setting)
                    {
                        int k=0;
                        while(k<13)
                            babelcode[k++]=' ';
                    }
                    duk_push_lstring(ctx, babelcode, (duk_size_t)babstat.st_size);
                    free(babelcode);
                    babelcode=(char *)duk_get_string(ctx,-1);
                    fclose(f);
                    goto end;
                }
            }
        }
    }
    /* file.babel.js does not exist */
    /* load babel.min.js as a module and convert file.js */
    duk_push_sprintf(ctx, "function(input){var b=require('@babel');return b.transform(input, %s ).code;}", opt);
    duk_push_string(ctx,fn);
    if (duk_pcompile(ctx, DUK_COMPILE_FUNCTION) == DUK_EXEC_ERROR)
    {
        const char *errmsg = rp_push_error(ctx, -1, NULL, rp_print_error_lines);
        if(setting==babel_setting_eval)
            return strdup(errmsg);
        fprintf(stderr, "%s\n", errmsg);
        duk_rp_exit(ctx, 1);
    }
    duk_push_string(ctx,src);

    if (duk_pcall(ctx, 1) == DUK_EXEC_ERROR)
    {
        const char *errmsg = rp_push_error(ctx, -1, NULL, rp_print_error_lines);
        if(setting==babel_setting_eval)
            return strdup(errmsg);
        fprintf(stderr, "%s\n", errmsg);
        duk_rp_exit(ctx, 1);
    }
    babelcode=(char *)duk_get_lstring(ctx,-1,&bsz);
    if(strcmp("stdin",fn) != 0  && strcmp("eval_code",fn) != 0)
    {
        f=fopen(babelsrc,"w");
        if(f==NULL)
        {
            fprintf(stderr,"error fopen(): error opening file '%s'\n",babelsrc);
        }
        else
        {
            size_t wrote;
            wrote=fwrite(babelcode,1,(size_t)bsz,f);
            if(wrote!=(size_t)bsz)
            {
                fprintf(stderr,"error fwrite(): error writing file '%s'\n",babelsrc);
                if(wrote>0 && unlink(babelsrc))
                {
                    fprintf(stderr,"error unlink(): error removing '%s'\nNot continuing\n",babelsrc);
                    exit(1);
                }
            }
            fclose(f);
        }
    }

    if(setting)
    {
        duk_push_string(ctx, babelcode+13);
        duk_replace(ctx, -2);
    }

    if(setting==babel_setting_eval)
        return NULL;

    end:
    //free(opt);

    return (const char*) (strlen(babelsrc)) ? strdup(babelsrc): strdup(fn);
}

struct slisthead tohead={0};

/* Pretty sure there should be unfreed timeout structs only if
   there is an explicit process.exit() before the timeout expires */

static void free_tos (void *arg)
{
    EVARGS *e;
    SLISTLOCK;  //probably not needed
    while (!SLIST_EMPTY(&tohead))
    {
        e = SLIST_FIRST(&tohead);
        SLIST_REMOVE_HEAD(&tohead, entries);
        if(RPTHR_TEST(get_current_thread(), RPTHR_FLAG_BASE))
        {
            event_del(e->e);
            event_free(e->e);
        }
        free(e);
    }
    SLISTUNLOCK;
}

void timespec_add_ms(struct timespec *ts, duk_double_t add)
{
    time_t secs = (time_t) add / 1000;

    add -= (double) (secs*1000.0);
    add *= 1000000;

    ts->tv_sec += secs;

    ts->tv_nsec += (long)add;

    if(ts->tv_nsec > 1000000000)
        ts->tv_sec++;
    else if (ts->tv_nsec < 0)
        ts->tv_sec--;
    else
        return;

    ts->tv_nsec = ts->tv_nsec % 1000000000;
}

duk_double_t timespec_diff_ms(struct timespec *ts1, struct timespec *ts2)
{
    double ret;

    ret = 1000.0 * ( (double)ts1->tv_sec - (double)ts2->tv_sec );

    ret += ( (double)ts1->tv_nsec - (double)ts2->tv_nsec ) / 1000000.0;

    return ret;
}


static void rp_el_doevent(evutil_socket_t fd, short events, void* arg)
{
    EVARGS *evargs = (EVARGS *) arg;
    duk_context *ctx= evargs->ctx;
    double key= evargs->key;
    duk_idx_t nargs = 0, func_idx;
    int cbret=1, do_js_cb=0;
    const char *fname = "setTimeout/setInterval";

    duk_set_top(ctx, 0);

    duk_push_global_stash(ctx);
    // ev_callback_object is at idx=1
    if( !duk_get_prop_string(ctx,-1, "ev_callback_object") )
    {
        RP_THROW(ctx, "internal error in rp_el_doevent()");
    }


    // do C timeout callback immediately before the JS callback
    // this is for a generic callback, not used for setTimeout/setInterval
    if(evargs->cb)
        cbret=(evargs->cb)(evargs->cbarg, 0);

    if(!cbret) //if return 0, skip js callback and second c callback
        goto to_doevent_end;


    // the JS callback function
    duk_push_number(ctx, evargs->key);
    duk_get_prop(ctx, -2);

    if(duk_is_function(ctx, -1))
    {
        func_idx = duk_get_top_index(ctx);
        do_js_cb=1;
        // get array and extract parameters.
        // ugly hack from duk_rp_set_to below
        duk_push_number(ctx, evargs->key+0.2);
        if(duk_get_prop(ctx, -3))
        {
            //this should always be an array.
            duk_idx_t i = 0, arr_idx = duk_get_top_index(ctx);
            nargs = (duk_idx_t) duk_get_length(ctx,-1);
            for(; i<nargs; i++)
                duk_get_prop_index(ctx, arr_idx, i);

            duk_remove(ctx, arr_idx); //array
        }
        else
            duk_pop(ctx); //undefined
    }
    // if we don't have a function, skip call
    if(!do_js_cb)
        goto to_post_callback;

    if(cbret == 1) //normal, no 'this' not bound to callback
    {
        if(duk_pcall(ctx, nargs) != 0)
        {
            const char *errmsg;

            // the function name
            duk_push_number(ctx, evargs->key +0.3);
            duk_get_prop(ctx, 1);
            fname=duk_get_string_default(ctx, -1, fname);
            duk_pop(ctx);

            fprintf(stderr, "Error in %s callback:\n  ", fname);

            errmsg = rp_push_error(ctx, -1, NULL, rp_print_error_lines);
            fprintf(stderr, "%s\n", errmsg);

            duk_pop(ctx);
        }
    }
    else
    {
        // if cbret == 2, this binding must be left on top of stack from c callback above
        duk_insert(ctx, func_idx+1); //insert 'this' after function

        if(duk_pcall_method(ctx, nargs) != 0)
        {
            const char *errmsg;
            // the function name
            duk_push_number(ctx, evargs->key +0.3);
            duk_get_prop(ctx, 1);
            fname=duk_get_string_default(ctx, -1, fname);
            duk_pop(ctx);

            fprintf(stderr, "Error in %s callback:\n  ", fname);

            errmsg = rp_push_error(ctx, -1, NULL, rp_print_error_lines);
            fprintf(stderr, "%s\n", errmsg);

            duk_pop(ctx);
        }
    }
    //discard return
    duk_pop(ctx);

    /* evargs may have been freed if clearInterval was called from within the function */
    /* if so, function stored in ev_callback_object[key] will have been deleted */
    duk_push_number(ctx, key);
    if(!duk_has_prop(ctx, 1) )
    {
        duk_set_top(ctx, 0);
        return;
    }

    to_post_callback:

    // do post callback
    if(evargs->cb)
        evargs->repeat=(evargs->cb)(evargs->cbarg, 1); // if returns 1, we repeat


    to_doevent_end:
    //setTimeout
    if(evargs->repeat==0)
    {
        SLISTLOCK;
        SLIST_REMOVE(&tohead, evargs, ev_args, entries);
        SLISTUNLOCK;
        event_del(evargs->e);
        event_free(evargs->e);
        duk_push_number(ctx, key);
        duk_del_prop(ctx, 1);
        duk_push_number(ctx, key+0.2);
        duk_del_prop(ctx, 1);
        duk_push_number(ctx, key+0.3);
        duk_del_prop(ctx, 1);
        free(evargs);
    }
    //setInterval, but event has expired.
    else if (evargs->repeat==1 && ( !event_pending(evargs->e, 0, NULL) )) // the event expired
    {
        //setInterval callback may have taken longer than the given interval.
        event_del(evargs->e);
        event_add(evargs->e, &evargs->timeout);
    }
    //setMetronome
    else if(evargs->repeat==2)
    {
        duk_double_t delay=0.0;
        struct timespec now;
        duk_double_t timediff_ms = 0.0;
        struct timeval newto;

        delay = ( (duk_double_t)evargs->timeout.tv_sec * 1000.0) +
                ( (duk_double_t)evargs->timeout.tv_usec/ 1000);

        clock_gettime(CLOCK_MONOTONIC, &now);

        //add next time to our clock.  That is the time we were aiming for.
        timespec_add_ms(&evargs->start_time, delay);

        //get the actual amount of time
        timediff_ms = delay + timespec_diff_ms(&now, &evargs->start_time);

        /* we may need to skip "frames", but will attempt to keep the timing */
        while( timediff_ms > delay)
        {
            timespec_add_ms(&evargs->start_time, delay);
            timediff_ms -= delay;
        }

        if(timediff_ms<0.0) timediff_ms=0.0;
        delay = (delay - timediff_ms)/1000.0;
        newto.tv_sec=(time_t) delay;
        newto.tv_usec=(suseconds_t)1000000.0 * (delay - (double)newto.tv_sec);
        event_del(evargs->e);
        event_add(evargs->e, &newto);
    }

    duk_set_top(ctx, 0);
}


/* It is not terribly important that this is thread safe (I hope).
   If we are threading, it just needs to be unique on each thread (until it loops).
   The id will be used on separate duk stacks for each thread */
volatile uint32_t ev_id=0;

/* this will insert a javascript callback into the event loop for settimeout et al
    ctx              -   thread's duk_context
    repeat           -   0:setTimeout, 1:setInterval, 2:setMetronome
    func_name        -   a javascript function name for error messages (i.e. "setTimeout")
    timeout_callback -   A c callback, int cb(void *arg, int after)
                           called twice, once with after=0, right before the javascript callback function is called,
                           and once with after=1, right after javascript callback function is called.
                         Return value from func with after=0 should be
                                0 for skip js callback and second c callback,
                                1 for ok
                                    OR
                                2 - cbfunc pushed 'this' onto stack for JS callback.
                         Return value from func with after=1 is used to set a new 'repeat' value (0, 1 or 2).
    arg              -   void pointer for above callback.
    func_idx         -   where to find the js callback.  If DUK_INVALID_INDEX, js callback will be skipped
    arg_start_idx    -   where to start looking for arguments in duktape stack to be eventually passed to js callback
                     -   DUK_INVALID_INDEX means don't look for arguments
    to               -   timeout value in seconds

*/
duk_ret_t duk_rp_insert_timeout(duk_context *ctx, int repeat, const char *fname, timeout_callback *cb, void *arg,
        duk_idx_t func_idx, duk_idx_t arg_start_idx, double to)
{
    EVARGS *evargs=NULL;
    duk_idx_t top=duk_get_top(ctx);
    RPTHR *thr=NULL;

    if(func_idx != DUK_INVALID_INDEX)
    {
        func_idx = duk_normalize_index(ctx, func_idx);
        REQUIRE_FUNCTION(ctx, func_idx, "%s(): Callback must be a function", fname ? fname : "setTimeout/setInterval");
    }

    if(arg_start_idx != DUK_INVALID_INDEX)
    {
        arg_start_idx = duk_normalize_index(ctx, arg_start_idx);
    }

    thr=get_current_thread();

    if(!thr->base)
        RP_THROW(ctx, "event base not found.");

    /* set up struct to be passed to callback */
    REMALLOC(evargs,sizeof(EVARGS));
    evargs->key = (double)ev_id++;
    evargs->ctx=ctx;
    evargs->repeat=repeat;
    evargs->cb=cb;
    evargs->cbarg=arg;
    clock_gettime(CLOCK_MONOTONIC, &evargs->start_time);

    SLISTLOCK;
    SLIST_INSERT_HEAD(&tohead, evargs, entries);
    SLISTUNLOCK;

    /* get the timeout */
    evargs->timeout.tv_sec=(time_t)to;
    evargs->timeout.tv_usec=(suseconds_t)1000000.0 * (to - (double)evargs->timeout.tv_sec);

    /* get object of callback functions from global stash */
    duk_push_global_stash(ctx);
    if( !duk_get_prop_string(ctx,-1, "ev_callback_object") )
    {
        /* if in threads, we need to set this up on new duk_context stack */
        duk_pop(ctx);// remove undefined
        duk_push_object(ctx);//new object
        duk_dup(ctx, -1); //make a reference copy
        duk_put_prop_string(ctx, -3, "ev_callback_object"); // put one reference in stash, leave other reference on top
    }
    duk_push_number(ctx, evargs->key); //array-like access with number as key

    if(func_idx != DUK_INVALID_INDEX)
        duk_dup(ctx,func_idx); //the JS callback function
    else
        duk_push_null(ctx);

    duk_put_prop(ctx, -3);

    // parameters to function
    if( func_idx != DUK_INVALID_INDEX && arg_start_idx != DUK_INVALID_INDEX && top > arg_start_idx )
    {
        duk_uarridx_t aidx=0, i;
        duk_push_number(ctx, evargs->key + 0.2);
        duk_push_array(ctx);
        for (i=arg_start_idx; i<top; i++)
        {
            duk_dup(ctx,i);
            duk_put_prop_index(ctx, -2, aidx++);
        }
        duk_put_prop(ctx, -3);
    }

    //the function name
    duk_push_number(ctx, evargs->key + 0.3);
    duk_push_string(ctx, fname);
    duk_put_prop(ctx, -3);

    duk_pop_2(ctx); //ev_callback_object and global stash

    /* create a new event for js callback and specify the c callback to handle it*/
    evargs->e = event_new(thr->base, -1, EV_PERSIST, rp_el_doevent, evargs);

    /* add event; return object { hidden(eventargs): evargs_pointer, eventId: evargs->key} */
    event_add(evargs->e, &evargs->timeout);
    duk_push_object(ctx);
    duk_push_pointer(ctx,(void*)evargs);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("eventargs") );
    duk_push_number(ctx,evargs->key);
    duk_put_prop_string(ctx, -2, "eventId");

    return 1;
}

inline duk_ret_t duk_rp_set_to(duk_context *ctx, int repeat, const char *fname, timeout_callback *cb, void *arg)
{
    return duk_rp_insert_timeout(ctx, repeat, fname, cb, arg, 0, 2,
        duk_get_number_default(ctx,1, 0) / 1000.0
    );
}

duk_ret_t duk_rp_set_timeout(duk_context *ctx)
{
    return duk_rp_set_to(ctx, 0, "setTimeout", NULL, NULL);
}

duk_ret_t duk_rp_set_interval(duk_context *ctx)
{
    return duk_rp_set_to(ctx, 1, "setInterval", NULL, NULL);
}

duk_ret_t duk_rp_set_metronome(duk_context *ctx)
{
    return duk_rp_set_to(ctx, 2, "setMetronome", NULL, NULL);
}

duk_ret_t duk_rp_clear_either(duk_context *ctx)
{
    EVARGS *evargs=NULL, *p;
    int found=0;

    if(!duk_is_object(ctx,0))
        RP_THROW(ctx, "clearTimeout()/clearInteral() requires variable returned from setTimeout()/setInterval()");

    if( !duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("eventargs") ) )
        RP_THROW(ctx, "clearTimeout()/clearInteral() requires variable returned from setTimeout()/setInterval()");

    evargs=(EVARGS *)duk_get_pointer(ctx, 1);

    if(!evargs)
        return 0;


    SLISTLOCK;
    SLIST_FOREACH(p, &tohead, entries)
    {
        if(p == evargs)
        {
            found=1;
            break;
        }
    }
    if(found)
        SLIST_REMOVE(&tohead, evargs, ev_args, entries);
    SLISTUNLOCK;

    if(!found)
        return 0;

    event_del(evargs->e);
    event_free(evargs->e);
    free(evargs);

    duk_push_global_stash(ctx);
    if( !duk_get_prop_string(ctx, -1, "ev_callback_object") )
        RP_THROW(ctx, "internal error in rp_el_doevent()");

    if( !duk_get_prop_string(ctx, 0, "eventId" ) )
        RP_THROW(ctx, "clearTimeout()/clearInteral() requires variable returned from setTimeout()/setInterval()");

    duk_del_prop(ctx, -2);

    return 0;
}

/* tickify (template literal parsing) section */
/* this has grown out of control and needs to be replaced by a proper parser */

#define adv ({in++;})

#define pushstate(state) do{\
    sstack_no++;\
    sstack[sstack_no] = state;\
}while(0)

#define popstate() sstack_no--

#define getstate() sstack[sstack_no]

#define copy(input) do{\
    if ( ! (getstate() == ST_BT || ( sstack_no>1 && sstack[sstack_no-1]==ST_BT) )){\
        if(out==outbeg+osz-1){\
            int pos = out - outbeg;\
            osz+=1024;\
            REMALLOC(outbeg, osz);\
            out = outbeg + pos;\
        }\
        *out=(input);\
        out++;\
    }\
}while(0)

#define stringcopy2(st) do{\
    char *s=(st);\
    while(*s) {\
        copy(*s);\
        s++;\
    }\
}while(0)


#define scopy(input) do{\
    if(!mute) {\
        if(out==outbeg+*osize-1){\
            int pos = out - outbeg;\
            *osize+=4096;\
            REMALLOC(outbeg, *osize);\
            out = outbeg + pos;\
        }\
        *out=(input);\
        out++;\
    }\
    if(input=='\n' && type < 2)(*lineno)++;\
}while(0)

#define stringcopy(st) do{\
    char *s=(st);\
    while(*s) {\
        scopy(*s);\
        s++;\
    }\
}while(0)


static int proc_triple(char **inp, char **ob, char **o, size_t *osize, int *lineno)
{
    char *in = *inp;
    char *out=*o;
    char *outbeg = *ob;
    int ret=1, mute=0, type=0, nlines=0;

    scopy('"');

    while(1) // everything is fair game until next ```
    {
        switch(*in)
        {
            case '`':
                if(*(in+1) == '`' && *(in+2) == '`')
                {
                    in+=3;
                    scopy('"');
                    while(nlines)
                    {
                        scopy('\n');
                        nlines--;
                    }
                    goto end_db;
                }
                scopy(*in);
                if(*(in+1) == '`')
                {
                    scopy('`');
                    adv;
                }
                break;

            case '"':
            case '\\':
                scopy('\\');
                scopy(*in);
                break;

            case '\n':
                (*lineno)++;
                nlines++;
                scopy ('\\');
                scopy ('n');
                break;

            case '\0':
                ret=0;
                goto end_db;

            default:
                scopy(*in);
                break;
        }
        adv;
    }
    end_db:

    *inp = in;
    *ob=outbeg;
    *o=out;

    return ret;
}

/*
    type==0 - template literal
    type==1 - tag function first pass
    type==2 - tag function second pass
    type==3 - String.raw
*/

static int proc_backtick(char *bt_start, char *end, char **ob, char **o, size_t *osize, int *lineno, int type)
{
    char *out=*o;
    char *in=bt_start;
    char *outbeg = *ob;
    int lastwasbs=0, mute = 0;

    if(type==2)
        mute=1;
    scopy('(');
    /* tag function */
    if(type ==1)
        scopy('[');
    stringcopy("\"\"+\"");
    adv;
    while(in < end)
    {
        switch(*in)
        {
            case '\\':
                scopy('\\');
                if(type==3)
                {
                    scopy('\\');
                }
                lastwasbs = !lastwasbs;
                break;
            case '$':
                if(
                    in-1>bt_start &&   //we can look 2 back
                    in+1<end      &&   //we can look 1 forward - not really necessary, I think
                    *(in+1)=='{'  &&   // we have '${'
                    !lastwasbs
                  )
                {
                    char *s, c, *startquote=NULL, *endquote=NULL;
                    in++;
                    if(*in == '\n') (*lineno)++;
                    in++;
                    if(*in == '\n') (*lineno)++;
                    if (type == 1)
                    {
                        stringcopy("\",\"");
                        mute=1;
                    }
                    else
                        mute=0;
                    s=in;
                    while (isspace(*s)) { if (*s=='\n')(*lineno)++; s++;}
                    /* a quoted string followed by ':' */
                    if(s<end && (*s =='\'' || *s == '"') )
                    {
                        startquote=s+1;
                        c=*s;
                        s++;
                        while (  s<end && (*s != c || ( *(s-1)=='\\' && *(s-2)!='\\')  ) )
                        {
                            s++;
                        }
                        if(*s == c)
                        {
                            endquote=s;
                            s++;
                            while (isspace(*s)) { if (*s=='\n')(*lineno)++; s++;}
                            if(*s == ':')
                            {
                                char *p;
                                int inbs=0;
                                if(type == 2)
                                    scopy(',');
                                else
                                    stringcopy("\"+");
                                stringcopy("rampart.utils.sprintf('");
                                p=startquote;
                                while(p<endquote)
                                {
                                    inbs=0;
                                    switch(*p)
                                    {
                                        case '\\':
                                            inbs= !inbs;
                                            p++;
                                            if(*p=='\\')
                                            {
                                                inbs=0;
                                                scopy('\\');
                                                scopy('\\');
                                                break;
                                            }
                                            else if(*p!='\'')
                                            {
                                                scopy('\\');
                                                scopy(*p);
                                                break;
                                            }
                                            /*skip the ' case for fallthrough */
                                        case '\'':
                                            /* possible fall through from above
                                               If in a double quote from input, we need to escape the
                                               single quote, because the output will be single
                                               quoted */
                                            if(*p=='\'' && (inbs || c == '"') )
                                            {
                                                inbs=0;
                                                scopy('\\');
                                            }
                                            scopy(*p);
                                            break;
                                        case '"':
                                            scopy('"');
                                            break;
                                        case '\n':
                                            (*lineno)++;
                                            stringcopy("\\n");
                                            break;
                                        case '\r':
                                            scopy('\\');
                                            scopy('r');
                                            break;
                                        case '\t':
                                            scopy('\\');
                                            scopy('t');
                                            break;
                                        default:
                                            scopy(*p);
                                    }
                                    p++;
                                }
                                stringcopy("',");
                                in = s+1;
                            }
                            else
                                stringcopy("\"+(");
                        }
                        else
                            stringcopy("\"+(");
                    }
                    /* starts with % and unquoted */
                    else if (*s == '%')
                    {
                        if(type == 2)
                            scopy(',');
                        else
                            stringcopy("\"+");
                        stringcopy("rampart.utils.sprintf('");
                        while (in < end && !(*in == ':' && *(in-1)!='\\') )
                        {
                            switch(*in)
                            {
                                case '\n':
                                    stringcopy("\\n");
                                    (*lineno)++;
                                    break;
                                case '\'':
                                    scopy('\\');
                                    scopy('\'');
                                    break;
                                case '\r':
                                    scopy('\\');
                                    scopy('r');
                                    break;
                                case '\t':
                                    scopy('\\');
                                    scopy('t');
                                    break;
                                default:
                                    scopy(*in);
                            }
                            adv;
                        }
                        if(*in != ':')
                        {
                            *ob=outbeg;
                            *o=out;
                            return 0;
                        }
                        adv;
                        stringcopy("',");
                    }
                    /* end special sprintf formatting */
                    else if(type == 2)
                        stringcopy(",(");
                    else if(type == 1)
                        stringcopy("\",(");
                    else
                    {
                        if(*(out-1) == '"' && *(out-2) != '\\')
                            *(out-1) = '('; //skip empty quotes: "",( => (
                        else
                            stringcopy("\"+(");
                    }
                    /* FIXME: properly this should go back to tickify somehow */
                    while (in < end && *in != '}')
                    {
                        if(*in == '`' && *(in-1)!='\\' )
                        {
                            if(*(in+1)=='`' && *(in+2)=='`') //triple backick = no escapes
                            {
                                in+=3;
                                if(!proc_triple(&in, &outbeg, &out, osize, lineno))
                                {
                                    *ob=outbeg;
                                    *o=out;
                                    return 0;
                                }
                            }
                            else
                            {
                                int r=proc_backtick(in, end, &outbeg, &out, osize, lineno, 0);
                                if(!r)
                                {
                                    *ob=outbeg;
                                    *o=out;
                                    return 0;
                                }
                                in+=r;
                            }
                        }
                        else
                        {
                            if(*in=='\n') (*lineno)++;
                            scopy(*in);
                            adv;
                        }
                    }
                    if(*in != '}')
                    {
                        *ob=outbeg;
                        *o=out;
                        return 0;
                    }
                    if(type==2)
                    {
                        scopy(')');
                        mute=1;
                    }
                    else
                    {
                        stringcopy(")+\"");
                        mute=0;
                    }
                }
                else
                    scopy(*in);
                lastwasbs=0;
                break;
            case '\n':
                stringcopy("\\n\"+\n\"");
                lastwasbs=0;
                break;
            case '\r':
                scopy('\\');
                scopy('r');
                lastwasbs=0;
                break;
            case '\t':
                scopy('\\');
                scopy('t');
                lastwasbs=0;
                break;
            case '`':
                if(lastwasbs)
                {
                    if(type!=3)
                        out--;
                    scopy('`');
                    lastwasbs=0;
                }
                else if(*(in+1)=='`' && *(in+2)=='`') //triple backick = no escapes
                {
                    in+=3;
                    if(!proc_triple(&in, &outbeg, &out, osize, lineno))
                    {
                        *ob=outbeg;
                        *o=out;
                        return 0;
                    }
                }
                else
                {
                    /* remove the '+ ""', BUT not if it is escaped */
                    if( *(out-1) == '"' && *(out-2) != '\\')
                    {
                        /*
                        if (outbeg < out - 3 && *(out-2) == '\\' && *(out-3) == '"')
                            scopy('"');
                        else
                        { */
                            int nls=0;
                            //back up
                            out-=2;
                            while(isspace(*out))
                            {
                                if(*out == '\n') nls++;
                                out--;
                            }
                            //cur char is '+' or ',' and will be overwritten
                            while(nls--)
                                scopy('\n');
                        //}
                    }
                    else
                        scopy('"');
                    if(type == 1)
                    {
                        int r;
                        mute=0;
                        scopy(']');
                        /* second pass, get ${} and comma separate */
                        r=proc_backtick(bt_start, end, &outbeg, &out, osize, lineno, 2);
                        if(!r)
                        {
                            *ob=outbeg;
                            *o=out;
                            return 0;
                        }
                    }
                    scopy(')');
                    adv;
                    *ob=outbeg;
                    *o=out;
                    return (int) (in - bt_start);
                }
                break;
            case '"':
                if(!lastwasbs)
                    scopy('\\');
                scopy('"');
                lastwasbs=0;
                break;
            default:
                lastwasbs=0;
                scopy(*in);
        }
        adv;
    }

    *ob=outbeg;
    *o=out;
    return 0;
}

char * tickify(char *src, size_t sz, int *err, int *ln)
{
    size_t osz=sz+1024;
    char *out = NULL, *outbeg;
    char *in=src, *end=src+sz;
    int line=1, qline=0;
    int sstack_no=0;
    int sstack[8];// only need 3?
    int startexp=0;
    int infuncp=0;

    *err=0;
    *ln=0;
    sstack[0]=ST_NONE;

    CALLOC(out, osz);
    outbeg=out;
    while(*in)
    {
/*
        if(getstate() == ST_TB)
        {
            qline=line;
            copy('"');
            while(*in) // everything is fair game until next ``
            {
                switch(*in)
                {
                    case '`':
                        if(*(in+1) == '`' && *(in+2) == '`')
                        {
                            in+=3;
                            popstate();
                            copy('"');
                            goto end_db;
                        }
                        copy (*in);
                        break;

                    case '"':
                    case '\\':
                        copy ('\\');
                        copy (*in);
                        break;

                    case '\n':
                        line++;
                        copy ('\\');
                        copy ('n');
                        break;

                    default:
                        copy(*in);
                        break;
                }
                adv;
            }
            end_db:
            if(!*in)
                break;
        }
*/
        switch (*in)
        {
            case '/' :
                if(getstate()==ST_NONE)
                {
                    /* skip over comments */
                    if( in+1<end && *(in+1) == '/')
                    {
                        while(*in && *in != '\n')
                        {
                            copy(*in);
                            adv;
                        }
                    }
                    else if ( in+1<end && *(in+1) == '*')
                    {
                        copy(*in);
                        adv;
                        copy(*in);
                        adv;
                        while(in<end)
                        {
                            if(*in=='\n')
                                line++;
                            copy(*in);
                            adv;
                            if(!*in)
                                break;
                            if(*in == '*' && in+1<end && *(in+1)=='/')
                            {
                                copy(*in);
                                adv;
                                copy(*in);
                                adv;
                                break;
                            }
                            if( in >= end )
                                break;
                        }
                    }
                    else if (startexp)
                    {
                        regex:
                        /* regular expression */
                        copy(*in);
                        adv;
                        while (in<end)
                        {
                            copy(*in);
                            if(*in == '/' && ( *(in-1) !='\\' || *(in-2) =='\\' ) )
                                break;
                            if(*in == '\n') /* did we mess up? */
                                break;
                            //if(*in == '\n' || *in == ';')
                            //    break;
                            //if( (*in == ')' && *(in-1) !='\\') )
                            //    break;
                            adv;
                        }
                        adv;
                    }
                    else
                    {
                        char *s=in;

                        s--;
                        while(s>=src && isspace(*s))s--;
                        while(s>=src && isalpha(*s))s--;
                        s++;
                        if(
                            /* reserved words which don't require a (), {} or [] next
                               this prevents >>return /abc`def/;<< from being run through
                               proc_backtick above                                        */
                            !strncmp(s,"return",6) || !strncmp(s,"yield",5)     ||
                            !strncmp(s,"break",5)  || !strncmp(s,"continue",8)  ||
                            !strncmp(s,"case",4)   || !strncmp(s,"else",4)      ||
                            !strncmp(s,"typeof",6) || !strncmp(s,"delete",6)    ||
                            !strncmp(s,"new",3)    || !strncmp(s,"var",3)
                        )
                            goto regex;
                        /* division */
                        copy(*in);
                        adv;
                    }
                    break;
                }
                else
                {
                    if (getstate() == ST_BS)
                        popstate();
                    copy(*in);
                    adv;
                }
                break;
            case '\n':
                if (getstate() == ST_BS)
                    popstate();
                copy(*in);
                line++;
                adv;
                /* This succeeds for some (like return and `` on different lines), but not if
                   function name and tag template are in different lines
                   BTW: ASI..., STBY
                if (!startexp)
                    startexp=1;
                */
                break;
            case '\\':
                copy(*in);
                adv;
                if (getstate() == ST_BS)
                    popstate();
                else
                    pushstate(ST_BS);
                qline=line;
                break;
            case '"':
                if(getstate() == ST_NONE)
                {
                    qline=line;
                    pushstate(ST_DQ);
                }
                else if (getstate() == ST_DQ)
                    popstate();
                else if (getstate() == ST_BS)
                    popstate();

                copy(*in);
                adv;
                break;
            case '\'':
                if(getstate() == ST_NONE)
                {
                    qline=line;
                    pushstate(ST_SQ);
                }
                else if (getstate() == ST_SQ)
                    popstate();
                else if (getstate() == ST_BS)
                    popstate();

                copy(*in);
                adv;
                break;
            case '`':
                if (getstate() == ST_NONE)
                {
                    int r;
                    char *s=in;

                    qline=line;
                    if(*(s+1)=='`' && *(s+2)=='`') //triple backick = no escapes
                    {
                        in+=3;
                        if(!proc_triple(&in, &outbeg, &out, &osz, &line))
                        {
                            *err=ST_TB;
                            *ln=qline;
                            free(outbeg);
                            return NULL;
                        }
                        //pushstate(ST_TB);
                        //in+=3;
                        break;
                    }

                    s--;
                    while(s>=src && isspace(*s))s--;
                    while( s>=src && ( isalpha(*s) || *s == '.' ) )s--;
                    s++;
                    if(
                        // reserved words which don't require a (), {} or [] next
                        // not all make sense in terms of being followed by a `literal`
                        // but covering the cases anyway.
                        !strncmp(s,"return",6) || !strncmp(s,"yield",5)     ||
                        !strncmp(s,"break",5)  || !strncmp(s,"continue",8)  ||
                        !strncmp(s,"case",4)   || !strncmp(s,"else",4)      ||
                        !strncmp(s,"typeof",6) || !strncmp(s,"delete",6)    ||
                        !strncmp(s,"new",3)    || !strncmp(s,"var",3)
                    )
                    {
                        r=proc_backtick(in, end, &outbeg, &out, &osz, &line, startexp);
                    }
                    else if(!strncmp(s,"String.raw",10))
                    {
                        out--;
                        while( out>=outbeg && isspace(*out))out--;
                        while( out>=outbeg && ( isalpha(*out) || *out == '.' ) )out--;
                        out++;
                        //r=proc_sr(in, end, &outbeg, &out, &osz, &line);
                        r=proc_backtick(in, end, &outbeg, &out, &osz, &line, 3);
                        if(!r)
                        {
                            *err=ST_SR;
                            *ln=qline;
printf("outbeg=%s\n",outbeg);
                            free(outbeg);
                            return NULL;
                        }
                    }
                    else
                        r=proc_backtick(in, end, &outbeg, &out, &osz, &line, !startexp);
                    if(!r)
                    {
                        *err=ST_BT;
                        *ln=qline;
                        free(outbeg);
                        return NULL;
                    }
                    in+=r;
                }
                else if (getstate() == ST_BS)
                {
                    popstate();
                    copy(*in);
                    adv;
                }
                else
                {
                    copy(*in);
                    adv;
                }
                break;
            default:
// this gets utf-8 2-4 lenght chars as well.  If not valid in a var or function name, that's fine.
// duktape will catch it anyway.  It is mostly about excluding invalid single byte utf-8
#define islegitchar(x) ( ((unsigned char)(x)) > 0x79 || (x) == '$' || (x) == '_' || isalnum((x)) )
                if (getstate() == ST_NONE)
                {
                    /*  looking for ...arg in "function(x, ...arg) {"
                        to rewrite as "function(x){var arg=Object.values(arguments).slice(x);"
                        where x is the number of preceding arguments.
                    */
                    if( infuncp )
                    {
                        if(*in == ',' || infuncp==1)
                        {
                            char *s = in;

                            if(*s==',')
                                s++;
                            infuncp++;
                            while (isspace(*s))
                                s++;
                            if( *s=='.' && *(s+1)=='.' && *(s+2)=='.' )
                            {
                                char *varname;
                                s+=3;
                                varname=s;
                                while (islegitchar(*s)) s++;

                                if( !isspace(*s) && *s!=')' )
                                {
                                    if(*s==',')
                                        *err=ST_PM;
                                    else
                                        *err=ST_PN;
                                    *ln=line;
                                    free(outbeg);
                                    return NULL;
                                }
                                while (isspace(*s))s++;
                                if(*s != ')')
                                {
                                    *err=ST_PM;
                                    *ln=line;
                                    free(outbeg);
                                    return NULL;
                                }
                                else
                                {
                                    /* check for '{', if not bail and let duktape report error */
                                    s++;
                                    while(isspace(*s))
                                    {
                                        s++;
                                    }
                                    if(*s=='{')
                                    {
                                        //good to go, write altered function
                                        //char *varname,
                                        char nbuf[16];
                                        if(*in==',')
                                            in++; // skip ','
                                        while(isspace(*in))
                                        {
                                            copy(*in);
                                            if(*in=='\n') line++;
                                            adv;
                                        }
                                        //varname=in+3; //advance past the "..."
                                        while( *in != ')')
                                        {
                                            /* only copy white space here */
                                            if(isspace(*in))
                                            {
                                                copy(*in);
                                                if(*in=='\n') line++;
                                            }
                                            adv;
                                        }
                                        copy(*in);// ')'
                                        adv;
                                        while( *in != '{')
                                        {
                                            copy(*in);
                                            if(*in=='\n') line++;
                                            adv;
                                        }
                                        copy(*in);// '{'
                                        adv;
                                        stringcopy2("var ");
                                        while(*varname!=')' && !isspace(*varname) )
                                        {
                                            copy(*varname);
                                            varname++;
                                        }
                                        stringcopy2("=Object.values(arguments).slice(");
                                        snprintf(nbuf,16,"%d",infuncp-2);
                                        stringcopy2(nbuf);
                                        stringcopy2(");");
                                    }
                                }
                                infuncp=0;
                            }
                        }
                        else if (*in ==')')
                            infuncp=0;
                    }
                    if (!startexp && *in == '(')
                    {
                        char *s = in -1;
                        while (s>src && isspace(*s))s--;
                        while (s>src && islegitchar(*s) )s--;
                        /* if not at the very beginning of file
                           or if at beginning and char is not legit.
                           example, if this is your entire file (excluding >>, <<):
                           >>(function ʱ(strings, ...keys) {console.log(strings);console.log(keys);})`arg1 = ${process.argv[0]} and arg2 = ${process.argv[1]}`<<
                        */
                        if(s!=src || !islegitchar(*s))
                            s++;
                        /* anonymous function */
                        if (!strncmp(s,"function",8))
                        {
                            infuncp=1;
                        }
                        else if(s>src)
                        {
                            s--;
                            if( isspace(*s) )
                            {
                                while (s>src && isspace(*s))s--;
                                while (s>src && islegitchar(*s) )s--;
                                if(s!=src || !islegitchar(*s)) //if not at the very beginning of file
                                    s++;
                                if (!strncmp(s,"function",8))
                                {
                                    infuncp=1;
                                }
                            }
                        }
                    }
                    /* end function(...var) processing */

                    /* for the "/regexp/" vs "var x = 2/3" cases, tag function and (...rest) processing,
                       we need to know where we are. This is a horrible hack, but it seems to work
                       Failings might be Automatic Semicolon Insertion at '\n'. See case '\n' above.
                    */
                    if (strchr("{([=;+-/*:,%^&|?", *in))
                        startexp=1;
                    else if (isalnum(*in) || *in =='}' || *in == ')' || *in == ']')
                        startexp=0;
                    /*
                    else if(
                        (*in == '&' && in+1<end && *(in+1)=='&')
                            ||
                        (*in == '|' && in+1<end && *(in+1)=='|')
                    )
                        startexp=1;
                    */
                }
                copy(*in);
                if (getstate() == ST_BS)
                    popstate();
                adv;
        }
    }
    char *db = getenv("RPDEBUG");

    if( db && !strcmp (db, "preparser") )
        fprintf(stderr, "%s",outbeg);
    *err=getstate();
    if(*err)
    {
        *ln=qline;
        free(outbeg);
        return NULL;
    }

    copy('\0');
    return outbeg;
}
/* end tickify */

void duk_rp_fatal(void *udata, const char *msg){
    fprintf(stderr, "*** FATAL ERROR: %s\n", (msg ? msg : "no message"));
    fflush(stderr);
    abort();
}

static void sigint_handler(int sig) {
    duk_rp_exit(main_ctx, 0);
}

static void evhandler(int sig, short events, void *base)
{
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
}

char **rampart_argv;
int   rampart_argc;
char argv0[PATH_MAX];
char rampart_exec[PATH_MAX];
char rampart_dir[PATH_MAX];
char rampart_bin[PATH_MAX];
int base_loop_exited=0;

/* mutex for locking in rampart.thread */
pthread_mutex_t thr_lock;
RPTHR_LOCK *rp_thr_lock;

static void print_version()
{
    printf("v%d.%d.%d\n", RAMPART_VERSION_MAJOR, RAMPART_VERSION_MINOR, RAMPART_VERSION_PATCH);
    exit(0);
}


static void print_help(char *argv0)
{
/*
    printf("Usage:\n\
    %s [options] file_name [--] [args]\n\n\
    Options:\n\
        -g, --globalize                    - globalize rampart.utils\n\
        -b, --use_babel                    - run all scripts through babel to support ECMAScript 2015+\n\
        -c, --command_string \"script\"      - load script from argument string\n\
        -v, --version                      - print version\n\
        -C, --color                        - don't use colors in interactive mode (repl)\n\
        -q, --quiet                        - omit rampart logo in interactive mode (repl)\n\
        --server                           - run rampart-server with default configuration\n\
        --quickserver                      - run rampart-server with alternate configuration\n\
        --[quick]server --help             - show help for built-in server\n\
        --spew-server-script               - print the internal server script to stdout and exit\n\
        --upgrade                          - attempt to upgrade to latest version of rampart\n\
        --                                 - do not process any arguments following (but pass to script)\n\
        -h, --help                         - this help message\n\n\
    \"file_name\" or \"script\" may be '-' for stdin\n\n\
    note: any options specified that do not match the above, or options after '--' are passed to the script\n\n\
    Documentation can be found at https://rampart.dev/docs/\n",
                argv0);
*/
    printf("Usage:\n\
    %s [options] file_name [--] [args]\n\n\
    Options:\n\
        -g, --globalize                    - globalize rampart.utils\n\
        -b, --use_babel                    - run all scripts through babel to support ECMAScript 2015+\n\
        -t, --use_transpiler               - transpile all scripts to support limited ECMAScript 2015+\n\
        -c, --command_string \"script\"      - load script from argument string\n\
        -v, --version                      - print version\n\
        -C, --color                        - do not use colors in interactive mode (repl)\n\
        -q, --quiet                        - omit rampart logo in interactive mode (repl)\n\
        --server                           - run rampart-server with default configuration\n\
        --quickserver                      - run rampart-server with alternate configuration\n\
        --[quick]server --help             - show help for built-in server\n\
        --spew-server-script               - print the internal server script to stdout and exit\n\
        --                                 - do not process any arguments following (but pass to script)\n\
        -h, --help                         - this help message\n\n\
    \"file_name\" or \"script\" may be '-' for stdin\n\n\
    note: any options specified that do not match the above, or options after '--' are passed to the script\n\n\
    Documentation can be found at https://rampart.dev/docs/\n",
                argv0);
    exit(0);
}

int main(int argc, char *argv[])
{
    struct rlimit rlp;
    int isstdin=0, len, dirlen, argi,
        scriptarg=-1, command_string=0, server=0;//, upgrade_check=0;
    char *ptr, *cmdline_src=NULL;
    struct stat entry_file_stat;
    duk_context *ctx;

    /* do this first */
    rp_thread_preinit();
    evthread_use_pthreads();

    SLIST_INIT(&tohead); //timeout list

    // must be init before new thread created below
    rp_thr_lock = RP_MINIT(&thr_lock);

    RP_PTINIT(&exlock);

    /* for later use */
    rampart_argv=argv;
    rampart_argc=argc;
    access_fh=stdout;
    error_fh=stderr;

    strcpy(argv0, argv[0]);

    // find our executable and fill in some global vars from it
    len = wai_getExecutablePath(NULL, 0, NULL);
    wai_getExecutablePath(rampart_exec, len, &dirlen);
    rampart_exec[len]='\0';

    strcpy(rampart_dir, rampart_exec);
    ptr=strrchr(rampart_dir, '/');
    if(!ptr)
    {
        fprintf(stderr,"could not get subpath of '%s'\n", rampart_dir);
        exit(1);
    }
    if( ptr-rampart_dir > 4 &&
        *(ptr-1)=='n' &&
        *(ptr-2)=='i' &&
        *(ptr-3)=='b' &&
        *(ptr-4)=='/'
      )
        ptr-=4;
    *ptr='\0';

    strcpy(rampart_bin, rampart_exec);
    ptr=strrchr(rampart_bin, '/');
    if(!ptr)
    {
        fprintf(stderr,"could not get subpath of '%s'\n", rampart_dir);
        exit(1);
    }
    *ptr='\0';

    /* timeout cleanups */
    add_exit_func(free_tos, NULL);

    /* initialze some locks */
    rp_ctxlock=RP_MINIT(&ctxlock);
    rp_slistlock = RP_MINIT(&slistlock);

    /* set max files open limit to hard limit */
    getrlimit(RLIMIT_NOFILE, &rlp);
    rlp.rlim_cur = rlp.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rlp);


    /* some control over our exit */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    /* init setproctitle() as required */
#ifdef RP_SPT_NEEDS_INIT
    spt_init(argc, argv);
#endif

    /* skip past process name ("rampart") */
    argc--;
    argv++;


    /* get options */
    for (argi=0; argi<argc; argi++)
    {
        char *opt = argv[argi];
        /* stop processing options if we get a "--" */
        if(!strcmp("--",opt))
            break;
        else if( *opt != '-')
        {
            /* the source is the first non option*/
            if(scriptarg<0)
                scriptarg=argi;
        }
        else if (*(opt+1) == '-')
        {
            /* long options */
            if(!strcmp(opt,"--command-string"))
            {
                command_string=1;
                argi++;
                if(argi==argc)
                {
                    fprintf(stderr, "Option '--command-string' requires an argument\n");
                    exit(1);
                }
                scriptarg=argi;
            }
            else if(!strcmp(opt,"--help"))
            {
                if (!server)
                    print_help(argv0);
            }
            else if(!strcmp(opt,"--version"))
                print_version();
            else if(!strcmp(opt,"--spew-server-script"))
            {
                printf("%s", serverscript);
                exit(0);
            }
/*
            else if(!strcmp(opt,"--upgrade"))
                upgrade_check=1;
*/
            else if(!strcmp(opt,"--use-transpiler"))
                duk_rp_globaltranspile=1;
            else if(!strcmp(opt,"--server"))
                server=1;
            else if(!strcmp(opt,"--quickserver"))
                server=2;
            else if(!strcmp(opt,"--globalize"))
                globalize=1;
            else if(!strcmp(opt,"--use-babel"))
            {
                if(!duk_rp_globalbabel)
                {
                    duk_rp_globalbabel=1;
                    main_babel_opt=strdup("{ presets: ['env'],retainLines:true }");
                }
            }
            else if(!strcmp(opt,"--color"))
                rp_color=1;
            else if(!strcmp(opt,"--quiet"))
                rp_quiet=1;
            else if(!strcmp(opt,"--script"))
            {
                argi++;
                if(argi==argc)
                {
                    fprintf(stderr, "Option '--script' requires an argument\n");
                    exit(1);
                }
                scriptarg=argi;
            }
        }
        else if (*(opt+1) == '\0')
            /* option is just '-' */
            isstdin=1;
        else
        {
            /* short options */
            char *o=opt+1;
            while(*o)
            {
                switch(*o)
                {
                    case 'c':
                        command_string=1;
                        argi++;
                        if(argi==argc)
                        {
                            fprintf(stderr, "Option '-c' requires an argument\n");
                            exit(1);
                        }
                        scriptarg=argi;
                        break;
                    case 'g':
                        globalize=1;
                        break;
                    case 'b':
                        if(!duk_rp_globalbabel)
                        {
                            duk_rp_globalbabel=1;
                            main_babel_opt=strdup("{ presets: ['env'],retainLines:true }");
                        }
                        break;
                    case 'v':
                        print_version();
                        break;
                    case 'h':
                        if(!server)
                            print_help(argv0);
                        break;
                    case 'C':
                        rp_color=0;
                        break;
                    case 't':
                        duk_rp_globaltranspile=1;
                    case 'q':
                        rp_quiet=1;
                        break;
                }
                o++;
            }
        }

    }
    /* first check if we have -c "script_src" */
    if(command_string && scriptarg)
    {
        if(strcmp("-",argv[scriptarg]))
            cmdline_src=argv[scriptarg];
        else
            isstdin=1;
        RP_script=strdup("stdin");
    }
    /* second check if we are using the server shortcut */
    else if(server)
    {
        char *p = argv[argc-1];
        cmdline_src=serverscript;
        if(*p !='-')
        {
            RP_script_path=realpath(p, NULL);
        }
        RP_script=strdup("built_in_server");
    }
/*
    else if (upgrade_check)
    {
        char *p = argv[argc-1];
        cmdline_src=upgradescript;
        if(*p !='-')
        {
            RP_script_path=realpath(p, NULL);
        }
        RP_script=strdup("built_in_upgrade_script");
    }
*/
    else if (scriptarg>-1)
    {
        char p[PATH_MAX], *s;

        //script is either first arg or last
        if( stat(argv[scriptarg], &entry_file_stat) != 0 )
        {
            fprintf(stderr, "error opening '%s': %s\n", argv[scriptarg], strerror(errno));
        }

        strcpy(p, argv[scriptarg]);

        //a copy of the complete path/script.js
        RP_script=realpath(p, NULL);

        s=strrchr(p,'/');
        if (s)
        {
            char *dupp;

            *s='\0';
            dupp=strdup(p);
            s=realpath(dupp,p);
            free (dupp);
        }
        else
        {
            if( !(s=getcwd(p,PATH_MAX)) )
            {
                fprintf(stderr,"path to script is longer than allowed\n");
                exit(1);
            }
        }
        if (!s)
            s="";

        RP_script_path=strdup(s);
    }

    if(!RP_script_path)
        RP_script_path=realpath("./", NULL);

    {
        char *file_src=NULL, *free_file_src=NULL, *fn=NULL, *s;
        const char *babel_source_filename;
        FILE *entry_file;
        size_t src_sz=0;
        struct evdns_base *dnsbase=NULL;

        // initialize duktape stack for the main thread
        mainthr=rp_new_thread(RPTHR_FLAG_THR_SAFE, NULL);

        if (!mainthr)
        {
            fprintf(stderr,"could not create duktape context\n");
            return 1;
        }
        ctx = mainthr->ctx;
        main_ctx = ctx; //global var used elsewhere
        mainthr->self=pthread_self(); //not currently used

        /* for cleanup, an array of functions */
        duk_push_global_stash(ctx);
        duk_push_array(ctx);
        duk_put_prop_string(ctx, -2, "exitfuncs");
        duk_pop(ctx);

        /* setTimeout and related functions */
        duk_push_c_function(ctx,duk_rp_set_timeout, DUK_VARARGS);
        duk_put_global_string(ctx,"setTimeout");
        duk_push_c_function(ctx, duk_rp_clear_either, 1);
        duk_put_global_string(ctx,"clearTimeout");
        duk_push_c_function(ctx,duk_rp_set_interval, DUK_VARARGS);
        duk_put_global_string(ctx,"setInterval");
        duk_push_c_function(ctx, duk_rp_clear_either, 1);
        duk_put_global_string(ctx,"clearInterval");
        duk_push_c_function(ctx,duk_rp_set_metronome, DUK_VARARGS);
        duk_put_global_string(ctx,"setMetronome");
        duk_push_c_function(ctx, duk_rp_clear_either, 1);
        duk_put_global_string(ctx,"clearMetronome");

        /* set up object to hold timeout callback function */
        duk_push_global_stash(ctx);
        duk_push_object(ctx);//new object
        duk_put_prop_string(ctx, -2, "ev_callback_object");
        duk_pop(ctx);//global stash

        if(globalize )
        {
            duk_get_global_string(ctx, "rampart");
            duk_get_prop_string(ctx, -1, "globalize");
            duk_get_prop_string(ctx, -2, "utils");
            duk_call(ctx, 1);
            duk_pop_2(ctx); //return and rampart
        }

        if(cmdline_src)
        {
            file_src=strdup(cmdline_src);
            if(server)
                fn="built_in_server";
            else
                fn="command_line_script";
            goto have_src;
        }

        /* scriptarg equiv was '-' */
        if(isstdin)
            goto dofile;

        if (scriptarg==-1)
        {
            /* check if we have incoming from stdin */
            if(!isatty(fileno(stdin)))
            {
                isstdin=1;
                goto dofile;
            }


            /* ********** set up event loop for repl *************** */

            /* set up main event base */
            mainthr->base = event_base_new();
            RPTHR_SET(mainthr, RPTHR_FLAG_BASE);

            dnsbase = evdns_base_new(mainthr->base,
                EVDNS_BASE_DISABLE_WHEN_INACTIVE);
            if(!dnsbase)
                RP_THROW(ctx, "rampart - error creating dnsbase");

            /* for unknown reasons, setting EVDNS_BASE_INITIALIZE_NAMESERVERS
               above results in dnsbase not exiting when event loop is otherwise empty */
            evdns_base_resolv_conf_parse(dnsbase, DNS_OPTIONS_ALL, "/etc/resolv.conf");

            duk_push_global_stash(ctx);
            duk_push_pointer(ctx, dnsbase);
            duk_put_prop_string(ctx, -2, "dns_elbase");
            duk_pop(ctx);
            REMALLOC(thread_dnsbase, (nthread_dnsbase + 1) * sizeof(struct evdns_base *) );
            thread_dnsbase[nthread_dnsbase++]=dnsbase;
            mainthr->dnsbase=dnsbase;
            /* end dns */

            /* run things that need to be run before the loop starts */
            run_b4loop_funcs();

            int ret = repl(ctx);
            return ret;
        }
        else
        {
            dofile:

            if(isstdin)
            {
                size_t read=0;

                fn="stdin";
                /*
                REMALLOC(file_src,1024);
                while( (read=fread(file_src+src_sz, 1, 1024, stdin)) > 0 )
                {
                    src_sz+=read;
                    if(read<1024)
                        break;
                    REMALLOC(file_src,src_sz+1024);
                }
                */
                read=getdelim(&file_src, &read, '\0', stdin);
            }
            else if (!server)
            {
                if (stat(argv[scriptarg], &entry_file_stat))
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not find entry file '%s': %s", argv[scriptarg], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_rp_exit(ctx, 1);
                }
                entry_file = fopen(argv[scriptarg], "r");
                if (entry_file == NULL)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not open entry file '%s': %s", argv[scriptarg], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_rp_exit(ctx, 1);
                }
                src_sz=entry_file_stat.st_size;
                file_src = NULL;
                CALLOC(file_src, src_sz +1 );

                if (fread(file_src, 1, entry_file_stat.st_size, entry_file) != entry_file_stat.st_size)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not read entry file '%s': %s", argv[scriptarg], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    free(free_file_src);
                    duk_rp_exit(ctx, 1);
                }

                fn=argv[scriptarg];
                fclose(entry_file);
            }

            have_src:
            if(cmdline_src)
                src_sz=strlen(file_src);

            free_file_src=file_src;

            /* skip over #!/path/to/rampart */
            if(*file_src=='#' && *(file_src+1)=='!')
            {
                s=strchr(file_src,'\n');

                /* leave '\n' to preserve line numbering */
                if(!s)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not read beyond first line in entry file '%s'\n", fn);
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    free(free_file_src);
                    duk_rp_exit(ctx, 1);
                }
                file_src=s;
            }
            /* ********** set up event loop *************** */
            /* set up main event base */
            mainthr->base = event_base_new();
            RPTHR_SET(mainthr, RPTHR_FLAG_BASE);

            /* push babelized source to stack if available */
            if (! (babel_source_filename=duk_rp_babelize(ctx, fn, file_src, entry_file_stat.st_mtime, babel_setting_none, NULL)) )
            {
                /* No babel, normal compile */
                //int err, lineno;
                /* process basic template literals (backticks) */
                //char *tickified = tickify(file_src, src_sz, &err, &lineno);

                int is_tickified=0;
                RP_ParseRes res = rp_get_transpiled(file_src, &is_tickified);

                if(res.errmsg)
                {
                    // the transpiler.c transpile error messages currently suck.
                    // the tickify ones are fine.
                    // for transpiler.c let duktape report the error unless there is nothing for duktape to process
                    if(is_tickified || !res.transpiled ) {
                        fprintf(stderr, "%s\n", res.errmsg);
                        freeParseRes(&res);
                        exit(1);
                    }
                    /* else don't exit yet, let duktape do a better error message */
                }

                char *dbug = getenv("RPDEBUG");
                if(res.transpiled)
                {
                    free(free_file_src);
                    //file_src = free_file_src = tickified;
                    file_src = free_file_src = stealParseRes(&res);

                    if( dbug && !strcmp (dbug, "transpiler") )
                        fprintf(stderr, "%s",file_src);
                }
                else if( dbug && !strcmp (dbug, "transpiler") )
                    fprintf(stderr, "No Transpile\n");

                duk_push_string(ctx, file_src);
                /* push filename */
                duk_push_string(ctx, fn);
                freeParseRes(&res);
            }
            else
            {
                /* babel src on stack, push babel filename */
                duk_push_string(ctx, babel_source_filename);
                free((char*)babel_source_filename);
            }

            free(free_file_src);

            /*  add dns base for rampart-net
             *  if added before event loop starts, it won't block exit
             */
            dnsbase = evdns_base_new(mainthr->base,
                EVDNS_BASE_DISABLE_WHEN_INACTIVE);
            if(!dnsbase)
                RP_THROW(ctx, "rampart - error creating dnsbase");

            /* for unknown reasons, setting EVDNS_BASE_INITIALIZE_NAMESERVERS
               above results in dnsbase not exiting when event loop is otherwise empty */
            evdns_base_resolv_conf_parse(dnsbase, DNS_OPTIONS_ALL, "/etc/resolv.conf");

            duk_push_global_stash(ctx);
            duk_push_pointer(ctx, dnsbase);
            duk_put_prop_string(ctx, -2, "dns_elbase");
            duk_pop(ctx);
            REMALLOC(thread_dnsbase, (nthread_dnsbase + 1) * sizeof(struct evdns_base *) );
            thread_dnsbase[nthread_dnsbase++]=dnsbase;
            mainthr->dnsbase=dnsbase;
            /* end dns */

            /* run the script */
            if (duk_pcompile(ctx, 0) == DUK_EXEC_ERROR)
            {
                const char *errmsg;
                duk_get_prop_string(ctx, -1, "stack");
                errmsg = duk_get_string(ctx, -1);
                fprintf(stderr, "%s\n", errmsg);
                duk_rp_exit(ctx, 1);
            }

            if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
            {
                const char *errmsg;
                duk_get_prop_string(ctx, -1, "stack");
                errmsg = duk_get_string(ctx, -1);
                fprintf(stderr, "%s\n", errmsg);
                duk_rp_exit(ctx, 1);
            }

            if (!mainthr->base)
            {
                fprintf(stderr,"Eventloop error: could not initialize event base\n");
                duk_rp_exit(ctx, 1);
            }

            /* sigint for libevent2: insert a one time event to register a sigint handler *
             * libeven2 otherwise erases our SIGINT and SIGTERM event handler set above   */
            struct event ev_sig;
            event_assign(&ev_sig, mainthr->base, -1,  0, evhandler, NULL);
            struct timeval to={0};
            event_add(&ev_sig, &to);

            /* run things that need to be run before the loop starts */
            run_b4loop_funcs();

            /* start event loop */
            int sent_finalizers=0;
            int nchildren=0;
            do {
                event_base_loop(mainthr->base, 0);
                THRLOCK;
                sent_finalizers=0;
                //printf("ENTER main loop\n");
                // at this point, if children have no active events, they aint gonna get any
                // as main thread is done with all it's events
                sent_finalizers = rp_thread_close_children();
                //printf("END OF LOOP %d children\n", mainthr->nchildren);
                //printf("EXIT main loop with %d children and %s finalizers set\n", mainthr->nchildren, (sent_finalizers?"some":"no"));
                // we restart the main loop in case children insert an event before they are done
                nchildren=mainthr->nchildren;
                THRUNLOCK;
                usleep(50000);//nchildren can change during this sleep - just fyi
            } while (nchildren || sent_finalizers);
            //printf("FINAL EXIT main loop with %d children and %s finalizers set\n", mainthr->nchildren, (sent_finalizers?"some":"no"));
        }
    }

    duk_rp_exit(ctx, 0);

    return 0;
}
