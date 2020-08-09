#include "duktape/core/duktape.h"
#include "duktape/register.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <errno.h>
#include "rp.h"

int RP_TX_isforked=0;  //set to one in fork so we know not to lock sql db;

#define RP_REPL_GREETING             \
    "         |>>            |>>\n"     \
    "       __|__          __|__\n"     \
    "      \\  |  /         \\   /\n"   \
    "       | ^ |          | ^ |  \n"   \
    "     __| o |__________| o |__\n"   \
    "    [__|_|__|(rp)|  | |______]\n"  \
    "____[|||||||||||||__|||||||||]____\n" \
    "RAMPART__powered_by_Duktape_" DUK_GIT_DESCRIBE

#define RP_REPL_PREFIX "rampart> "
#define RP_REPL_MAX_LINE_SIZE 32768
#define RP_REPL_HISTORY_LENGTH 256
struct repl_history
{
    char *buffer[RP_REPL_HISTORY_LENGTH];
    int size;
};

static void handle_input(struct repl_history *history)
{
    int cursor_pos = 0;
    int buffer_end = 0;
    int cur_char;
    int cur_history_idx = history->size - 1;

    while ((cur_char = getchar()) != '\n')
    {
        if (cur_char == '\033')
        {
            getchar();
            int esc_char = getchar();
            switch (esc_char)
            {
            case 'A':
            {
                // arrow down
                if (cur_history_idx > 0)
                {
                    cur_history_idx--;
                    buffer_end = strlen(history->buffer[cur_history_idx]);
                    // clear line, move to col 1, print repl prefix
                    printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                    // print out buffer from history
                    printf("%s", history->buffer[cur_history_idx]);
                    // move to correct position and update cursor pos
                    cursor_pos = cursor_pos > buffer_end ? buffer_end : cursor_pos;
                    printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
                }
                break;
            }
            case 'B':
                // arrow up
                if (cur_history_idx + 1 < history->size)
                {
                    cur_history_idx++;
                    buffer_end = strlen(history->buffer[cur_history_idx]);
                    // clear line, move to col 1, print repl prefix
                    printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                    // print out buffer from history
                    printf("%s", history->buffer[cur_history_idx]);
                    // move to correct position and update cursor pos
                    cursor_pos = cursor_pos > buffer_end ? buffer_end : cursor_pos;
                    printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
                }
                break;
            case 'C':
                if (cursor_pos < buffer_end)
                {
                    printf("%s", "\033[1C");
                    cursor_pos++;
                }
                // code for arrow right
                break;
            case 'D':
                if (cursor_pos > 0)
                {
                    printf("%s", "\033[1D");
                    cursor_pos--;
                }
                // code for arrow left
                break;
            }
        }
        else
        {
            char *line_buffer = history->buffer[cur_history_idx];
            line_buffer[buffer_end] = cur_char;
            if (cur_char == '\177')
            {
                // delete character
                if (cursor_pos <= 0)
                    continue;
                printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                memmove(line_buffer + cursor_pos - 1, line_buffer + cursor_pos,
                        buffer_end - cursor_pos + 1);
                line_buffer[buffer_end] = '\0';
                buffer_end--;
                printf("%.*s", buffer_end, line_buffer);
                cursor_pos--;
                printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
            }
            else
            {
                if (cursor_pos >= RP_REPL_MAX_LINE_SIZE)
                    continue;

                printf("%s%s%s", "\033[2K", "\033[1G", RP_REPL_PREFIX);
                memmove(line_buffer + cursor_pos + 1, line_buffer + cursor_pos,
                        buffer_end - cursor_pos);
                line_buffer[cursor_pos] = cur_char;
                buffer_end++;
                printf("%.*s", buffer_end, line_buffer);
                cursor_pos++;
                printf("\033[%luG", strlen(RP_REPL_PREFIX) + cursor_pos + 1);
            }
        }
    }
    history->buffer[cur_history_idx][buffer_end] = '\0';
    printf("\n");
    // copy any changed history into current line
//printf("Line IS '%s'\n",history->buffer[cur_history_idx]);
    memcpy(history->buffer[history->size - 1], history->buffer[cur_history_idx],
           strlen(history->buffer[cur_history_idx]));
}

static int repl(duk_context *ctx)
{
    struct repl_history history;
    history.size = 0;

    printf("%s", RP_REPL_GREETING);
    putchar('\n');

    while (1)
    {
        printf("%s", RP_REPL_PREFIX);

        char *line = malloc(RP_REPL_MAX_LINE_SIZE);
        if (history.size < RP_REPL_HISTORY_LENGTH)
        {
            history.buffer[history.size] = line;
            history.size++;
        }
        else
        {
            // shift last from buffer
            free(history.buffer[0]);
            int i = 0;
            for (i = 0; i < RP_REPL_HISTORY_LENGTH-1; i++)
            {
                history.buffer[i] = history.buffer[i + 1];
            }
            history.buffer[RP_REPL_HISTORY_LENGTH - 1] = line;
        }

        handle_input(&history);

        // ignore empty input
        while (strlen(line) == 0)
        {
            printf("%s", RP_REPL_PREFIX);
            handle_input(&history);
        }

        // line too long
        if (line[RP_REPL_MAX_LINE_SIZE - 1] == '\n')
        {
            printf("Line too long. The max line size is %d", RP_REPL_MAX_LINE_SIZE);
            continue;
        }

        // evaluate input
        duk_push_string(ctx, line);
        if (duk_peval(ctx) != 0)
        {
            printf("%s\n", duk_safe_to_string(ctx, -1));
        }
        else
        {
            printf("%s\n", duk_safe_to_stacktrace(ctx, -1));
        }
        duk_pop(ctx);
    }
    return 0;
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


/* babelized source and fn is left on top of stack*/
const char *duk_rp_babelize(duk_context *ctx, char *fn, char *src, time_t src_mtime)
{
    char *s, *babelcode=NULL;
    char *opt;
    struct stat babstat;
    char babelsrc[strlen(fn)+10];
    FILE *f;
    size_t read;
    char *pfill="babel-polyfill.js";
    char *pfill_bc=".babel-polyfill.bytecode";
    duk_size_t bsz;
    void *buf;
    RPPATH rppath;

    babelsrc[0]='\0';
    opt=checkbabel(src);
    if(!opt) return NULL;

    /* check if polyfill already loaded */
    duk_eval_string(ctx,"global._babelPolyfill");
    if(duk_get_boolean_default(ctx,-1,0))
    {
        duk_pop(ctx);
        goto transpile;
    }
    duk_pop(ctx);

    rppath=rp_find_path(pfill_bc,"modules/");
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
    rppath=rp_find_path(pfill,"modules/");
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
    DUKREMALLOC(ctx, babelcode, rppath.stat.st_size);
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
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_destroy_heap(ctx);
        exit (1);
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
        duk_destroy_heap(ctx);
        exit(1);
    }
    duk_pop(ctx);
    duk_eval_string(ctx,"global._babelPolyfill=true;");
    duk_pop(ctx);

    transpile:
    if(strcmp("stdin",fn) != 0 )
    {
        /* file.js => file.babel.js */
        s=strrchr(fn,'.');
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
                DUKREMALLOC(ctx,babelcode,babstat.st_size);

                f=fopen(babelsrc,"r");
                if(f==NULL)
                {
                    fprintf(stderr,"error fopen(): error opening file '%s': %s\n",babelsrc,strerror(errno));
                }
                else
                {
                    read=fread(babelcode,1,babstat.st_size,f);

                    if(read != babstat.st_size)
                    {
                        fprintf(stderr,"error fread(): error reading file '%s'\n",babelsrc);
                    }
                    duk_push_lstring(ctx,babelcode,(duk_size_t)babstat.st_size);
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
    duk_push_sprintf(ctx, "function(input){var b=require('babel');return b.transform(input, %s ).code;}", opt);
    duk_push_string(ctx,fn);
    if (duk_pcompile(ctx, DUK_COMPILE_FUNCTION) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_destroy_heap(ctx);
        exit (1);
    }
    duk_push_string(ctx,src);
    if (duk_pcall(ctx, 1) == DUK_EXEC_ERROR)
    {
        fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
        duk_destroy_heap(ctx);
        exit(1);
    }
    babelcode=(char *)duk_get_lstring(ctx,-1,&bsz);
    if(strcmp("stdin",fn) != 0 )
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
    end:
    free(opt);
    return (const char*) (strlen(babelsrc)) ? babelsrc: fn;
}

char **rampart_argv;
int   rampart_argc;
int main(int argc, char *argv[])
{
    struct rlimit rlp;
    int filelimit = 16384, lflimit = filelimit, isstdin=0;

    rampart_argv=argv;
    rampart_argc=argc;
    /* set rlimit to filelimit, or highest allowed value below that */
    getrlimit(RLIMIT_NOFILE, &rlp);
    rlp.rlim_cur = filelimit;
    while (setrlimit(RLIMIT_NOFILE, &rlp) != 0)
    {
        lflimit = filelimit;
        filelimit /= 2;
        rlp.rlim_cur = filelimit;
    }
    if (lflimit != filelimit)
    {
        do
        {
            rlp.rlim_cur = (filelimit + lflimit) / 2;
            if (setrlimit(RLIMIT_NOFILE, &rlp) == 0)
                filelimit = rlp.rlim_cur;
            else
                lflimit = rlp.rlim_cur;
        } while (lflimit > filelimit + 1);
    }

    duk_context *ctx = duk_create_heap_default();
    if (!ctx)
    {
        printf("could not create duktape context\n");
        return 1;
    }

    duk_init_context(ctx);

    {   /* add process.argv */
        int i=0;

        duk_push_global_object(ctx);
        /* get global symbol "process" */
        if(!duk_get_prop_string(ctx,-1,"process"))
        {
            duk_pop(ctx);
            duk_push_object(ctx);
        }
        
        duk_push_array(ctx); /* process. */

        for (i=0;i<argc;i++)
        {
            duk_push_string(ctx,argv[i]);
            duk_put_prop_index(ctx,-2,(duk_uarridx_t)i);
        }
        duk_put_prop_string(ctx,-2,"argv");
        duk_push_string(ctx,argv[0]);
        duk_put_prop_string(ctx,-2,"argv0");
        duk_put_prop_string(ctx,-2,"process");
        duk_pop(ctx);

        /* skip past process name */
        argc--;
        argv++;

        /* skip to filename, if any */
        while( argc > 1)
        {
            argc--;
            argv++;
        }
    }

    {
        char *file_src=NULL, *free_file_src=NULL, *fn=NULL, *s;
        const char *bfn;
        FILE *entry_file;
        struct stat entry_file_stat;
        size_t src_sz=0;
        
        if (argc == 0)
        {
            if(!isatty(fileno(stdin)))
            {
                isstdin=1;
                goto dofile;
            }
            // store old terminal settings
            struct termios old_tio, new_tio;
            tcgetattr(STDIN_FILENO, &old_tio);
            new_tio = old_tio;

            // disable buffered output and echo
            new_tio.c_lflag &= (~ICANON & ~ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
            int ret = repl(ctx);
            // restore terminal settings
            tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
            return ret;
        }
        else
        {
            dofile:

            if (!strcmp("-",argv[0]))
                isstdin=1;
            
            if(isstdin)
            {
                size_t read=0;
                
                fn="stdin";
                REMALLOC(file_src,1024);
                while( (read=fread(file_src+src_sz, 1, 1024, stdin)) > 0 )
                {
                    src_sz+=read;
                    if(read<1024)
                        break;
                    REMALLOC(file_src,src_sz+1024);
                }
            }
            else
            {
                if (stat(argv[0], &entry_file_stat))
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not find entry file '%s': %s\n", argv[0], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_destroy_heap(ctx);
                    return 1;
                }
                entry_file = fopen(argv[0], "r");
                if (entry_file == NULL)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not open entry file '%s': %s\n", argv[0], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_destroy_heap(ctx);
                    return 1;
                }
                src_sz=entry_file_stat.st_size + 1;
                file_src = malloc(src_sz);
                if(!file_src)
                {
                    fprintf(stderr,"Error allocating memory for source file\n");
                    duk_destroy_heap(ctx);
                    return 1;
                }

                if (fread(file_src, 1, entry_file_stat.st_size, entry_file) != entry_file_stat.st_size)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not read entry file '%s': %s\n", argv[0], strerror(errno));
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_destroy_heap(ctx);
                    free(free_file_src);
                    return 1;
                }
                fn=argv[0];
                fclose(entry_file);
            }
            free_file_src=file_src;
            file_src[src_sz-1]='\0';

            /* skip over #!/path/to/rampart */
            if(*file_src=='#')
            {
                s=strchr(file_src,'\n');

                /* leave '\n' to preserve line numbering */
                if(!s || !*s)
                {
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "Could not read beyond first line in entry file '%s'\n", fn);
                    fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                    duk_destroy_heap(ctx);
                    free(free_file_src);
                    return 1;
                }
                file_src=s;
            }

            /* push babelized source to stack if available */
            if (! (bfn=duk_rp_babelize(ctx, fn, file_src, entry_file_stat.st_mtime)) )
            {
                /* No babel, normal compile */
                duk_push_string(ctx, file_src);
                /* push filename */
                duk_push_string(ctx, fn);
            }
            else
            {
                /* babel src on stack, push babel filename */
                duk_push_string(ctx, bfn);
            }

            free(free_file_src);
            if (duk_pcompile(ctx, 0) == DUK_EXEC_ERROR)
            {
                fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                duk_destroy_heap(ctx);
                return 1;
            }

            if (duk_pcall(ctx, 0) == DUK_EXEC_ERROR)
            {
                fprintf(stderr,"%s\n", duk_safe_to_stacktrace(ctx, -1));
                duk_destroy_heap(ctx);
                return 1;
            }
        }
    }
    duk_destroy_heap(ctx);
    return 0;
}
