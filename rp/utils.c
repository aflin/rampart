#include <stdio.h>
#include <stdarg.h> /* va_list etc */
#include <stddef.h> /* size_t */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include "duktape.h"
#define REMALLOC(s, t)               \
  do                                 \
  {                                  \
    (s) = realloc((s), (t));         \
    if ((char *)(s) == (char *)NULL) \
    {                                \
      return (2000001);              \
    }                                \
  } while (0)

#define BUFREADSZ 4096
#define MAXBUFFER 536870912 /* 512mb */
#define GETDATAEND 1999999

/* fill buffer with more data from file.
   If "wrappos" > -1, data from wrappos to curend will be moved to
   the beginning of buf and writing to buf will begin at the end of valid
   data (curend).
   cursize and curend will be updated appropriately for multiple calls
   --  *fd       - file read handle
   --  *buf      - malloced buffer to fill
   --  *cursize  - current size of buffer
   --  *curend   - last byte of valid data in buf

   returns errno or other custom error code
   buf will be null terminated at curend
*/

int getdata(FILE *fd, char **buf, char **curend, size_t *cursize, int wrappos)
{
  int i = 0, curdatasize = 0;
  char *b;
  errno = 0;
  /* get a block of memory, 
     curend shouldn't be set in this case*/
  if (*cursize < BUFREADSZ)
  {
    *cursize = BUFREADSZ;
    REMALLOC(*buf, *cursize + 1);
  }
  /* set end of buffer data if not set */
  if (*curend == (char *)NULL)
    *curend = *buf + BUFREADSZ; /* -1 == end of buffer */

  /* check if we are wrapping.
     If so, move data from wrap point to curend to beginning of buffer.
     set up to write at new curend (buf + (curend-wrappos) )
     Make sure there is enough space for extra data */
  if (wrappos > -1)
  {
    /* get size of data to copy to beginning of string */
    curdatasize = *curend - (*buf + wrappos);
    //printf("curdatasize(%d)=curend(%d) - (buf(%d) + wrappos(%d)\n", curdatasize,(int)*curend,(int)*buf,wrappos);
    //printf("curend-1='%c', buf+wrappos='%c'\n",*(*curend-1),*(*buf+wrappos));
    int oldsize = *cursize;
    /* is our buffer currently large enough for new block of data ? */
    if (curdatasize + BUFREADSZ > *cursize)
    {
      *cursize += BUFREADSZ;
      if (*cursize > MAXBUFFER)
        return (2000000); /* so as not to be confused with errno range errors*/
      REMALLOC(*buf, *cursize + 1);
    }
    if (wrappos) /* "Wherever you go, there you are" --Buckaroo Banzai 
                   "If you don't know where you are going, you'll end up someplace else." -- Yogi Berra. */
      /* move data that starts at wrappoint to beginning of buf */
      memmove(*buf, *buf + wrappos, curdatasize);
    /* else if wrappos is 0, don't memmove from 0 to 0 */

    b = *buf + curdatasize;
  }
  /* no wrapping, overwrite buffer */
  else
  {
    b = *buf;
  }
  i = (int)fread(b, 1, BUFREADSZ, fd);
  *curend = b + i;

  *(*curend) = '\0'; /* at 4097 */

  //printf("i=%d\n",i);
  if (i < BUFREADSZ)
    return (GETDATAEND);

  return (errno);
}

FILE *openFileWithCallback(duk_context *ctx, int *func_idx, const char **filename)
{
  int i = 0;

  if (duk_is_string(ctx, i) || duk_is_string(ctx, ++i))
    *filename = duk_get_string(ctx, i);
  else
  {
    duk_push_string(ctx, "readln requires a file name passed as a string");
    (void)duk_throw(ctx);
  }

  i = !i; /* get the other option */

  if (duk_is_function(ctx, i))
    *func_idx = i;
  else
  {
    duk_push_string(ctx, "readln requires a callback function");
    (void)duk_throw(ctx);
  }

  struct stat path_stat;
  int err = stat(*filename, &path_stat);
  if (err)
  {
    duk_push_sprintf(ctx, "error resolving '%s': %s", *filename, strerror(errno));
    (void)duk_throw(ctx);
  }
  if (!S_ISREG(path_stat.st_mode))
  {
    duk_push_sprintf(ctx, "'%s' is not a file", *filename);
    (void)duk_throw(ctx);
  }
  return fopen(*filename, "r");
}

#define NOWRAP -1

#define GETDATA(x)                                                                      \
  do                                                                                    \
  {                                                                                     \
    error = getdata(fp, &buf, &end, &cursize, (x));                                     \
    if (error && error != GETDATAEND)                                                   \
    {                                                                                   \
      if (error == 2000000)                                                             \
        duk_push_string(ctx, "value too large to parse (>512mb)");                      \
      else if (error == 2000001)                                                        \
        duk_push_string(ctx, "error realloc()");                                        \
      else                                                                              \
        duk_push_sprintf(ctx, "error opening or parsing'%s': %s", fn, strerror(errno)); \
      return duk_throw(ctx);                                                            \
    }                                                                                   \
  } while (0)

duk_ret_t duk_util_readln(duk_context *ctx)
{
  int func_idx = -1;
  FILE *fp;
  const char *filename;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  fp = openFileWithCallback(ctx, &func_idx, &filename);

  if (fp == NULL)
  {
    const char *fn = duk_to_string(ctx, !func_idx);
    duk_push_sprintf(ctx, "error opening '%s': %s", fn, strerror(errno));
    return duk_throw(ctx);
  }
  while ((read = getline(&line, &len, fp)) != -1)
  {
    duk_dup(ctx, func_idx);
    duk_push_this(ctx);
    *(line + strlen(line) - 1) = '\0';
    duk_push_string(ctx, line);
    duk_call_method(ctx, 1);
    if (duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1) == 0)
      break;

    duk_pop(ctx);
  }

  fclose(fp);
  if (line)
    free(line);

  return 0;
}

#define DUK_UTIL_STAT_TEST_MODE(mode, test)           \
  duk_ret_t duk_util_stat_is_##mode(duk_context *ctx) \
  {                                                   \
    \                  
    duk_push_this(ctx);                               \
    duk_get_prop_string(ctx, -1, "mode");             \
    int mode = duk_get_int(ctx, -1);                  \
    duk_push_boolean(ctx, test(mode));                \
    return 1;                                         \
  }

DUK_UTIL_STAT_TEST_MODE(block_device, S_ISBLK);
DUK_UTIL_STAT_TEST_MODE(character_device, S_ISCHR);
DUK_UTIL_STAT_TEST_MODE(directory, S_ISDIR);
DUK_UTIL_STAT_TEST_MODE(fifo, S_ISFIFO);
DUK_UTIL_STAT_TEST_MODE(file, S_ISREG);
DUK_UTIL_STAT_TEST_MODE(socket, S_ISSOCK);
DUK_UTIL_STAT_TEST_MODE(symbolic_link, S_ISLNK);

static const duk_function_list_entry stat_methods[] = {
    {"is_block_device", duk_util_stat_is_block_device, 0},
    {"is_character_device", duk_util_stat_is_character_device, 0},
    {"is_directory", duk_util_stat_is_directory, 0},
    {"is_fifo", duk_util_stat_is_fifo, 0},
    {"is_file", duk_util_stat_is_file, 0},
    {"is_socket", duk_util_stat_is_socket, 0},
    {"is_symbolic_link", duk_util_stat_is_symbolic_link, 0},
    {NULL, NULL, 0}};

#define DUK_PUT(ctx, type, key, value, idx) \
  {                                         \
    duk_push_##type(ctx, value);            \
    duk_put_prop_string(ctx, idx, key);     \
  }
/**
 *  Filesystem stat
 *  @param {string} The path name
 *  @returns a javascript object of the following form:
 *  stat: {
 *    dev: int,
 *    ino: int,
 *    mode: int,
 *    nlink: int,
 *    uid: int,
 *    gid: int,
 *    rdev: int,
 *    size: int,
 *    blksize: int,
 *    blocks: int,
 *    atime: Date,
 *    mtime: Date,
 *    ctime: Date,
 *  }
 **/
duk_ret_t duk_util_stat(duk_context *ctx)
{
  const char *path = duk_get_string(ctx, -1);
  struct stat path_stat;
  int err = stat(path, &path_stat);
  if (err)
  {
    duk_push_sprintf(ctx, "error getting status '%s': %s", path, strerror(errno));
    (void)duk_throw(ctx);
  }

  // stat
  duk_push_object(ctx);

  DUK_PUT(ctx, int, "dev", path_stat.st_dev, -2);
  DUK_PUT(ctx, int, "ino", path_stat.st_ino, -2);
  DUK_PUT(ctx, int, "mode", path_stat.st_mode, -2);
  DUK_PUT(ctx, int, "nlink", path_stat.st_nlink, -2);
  DUK_PUT(ctx, int, "uid", path_stat.st_uid, -2);
  DUK_PUT(ctx, int, "gid", path_stat.st_gid, -2);
  DUK_PUT(ctx, int, "rdev", path_stat.st_rdev, -2);
  DUK_PUT(ctx, int, "size", path_stat.st_size, -2);
  DUK_PUT(ctx, int, "blksize", path_stat.st_blksize, -2);
  DUK_PUT(ctx, int, "blocks", path_stat.st_blocks, -2);

  long atime, mtime, ctime;
#if __DARWIN_64_BIT_INO_T || !defined(_POSIX_C_SOURCE) || defined(_DARWIN_C_SOURCE)
  atime = path_stat.st_atimespec.tv_sec * 1000;
  mtime = path_stat.st_atimespec.tv_sec * 1000;
  ctime = path_stat.st_atimespec.tv_sec * 1000;
#else
  atime = path_stat.atime;
  mtime = path_stat.mtime;
  ctime = path_stat.ctime;
#endif

  // atime
  (void)duk_get_global_string(ctx, "Date");
  duk_push_number(ctx, atime);
  duk_new(ctx, 1);
  duk_put_prop_string(ctx, -2, "atime");

  // mtime
  (void)duk_get_global_string(ctx, "Date");
  duk_push_number(ctx, mtime);
  duk_new(ctx, 1);
  duk_put_prop_string(ctx, -2, "mtime");

  // ctime
  (void)duk_get_global_string(ctx, "Date");
  duk_push_number(ctx, mtime);
  duk_new(ctx, 1);
  duk_put_prop_string(ctx, -2, "ctime");

  // add methods
  duk_put_function_list(ctx, -1, stat_methods);

  // see duktape function interface
  return 1;
}

/**
 * Executes a command where the arguments are the arguments to execv.
 * @returns a string with the stdout of the file
 * Ex.
 * var stdout = utils.exec("/bin/ls", "ls", "-1");
 */
duk_ret_t duk_util_exec(duk_context *ctx)
{

  // get variadic arguments and store in null terminated argument list
  duk_idx_t nargs = duk_get_top(ctx);
  char **args = NULL;
  REMALLOC(args, (nargs + 1) * sizeof(char *));
  for (int i = 0; i < nargs; i++)
  {
    args[i] = (char *)duk_get_string(ctx, i);
  }
  args[nargs] = NULL;

  int pipes[2];
  if (pipe(pipes) == -1)
  {
    duk_push_sprintf(ctx, "error creating pipes: %s", strerror(errno));
    (void)duk_throw(ctx);
  }

  pid_t pid;
  if ((pid = fork()) == -1)
  {
    duk_push_sprintf(ctx, "error forking: %s", strerror(errno));
    (void)duk_throw(ctx);
  }
  else if (pid == 0)
  {
    // child process

    // redirect to stdout
    dup2(pipes[1], STDOUT_FILENO);
    close(pipes[0]);
    close(pipes[1]);
    execv(args[0], args + 1);
    fprintf(stderr, "error executing %s\n", args[1]);
    exit(EXIT_FAILURE);
  }
  else
  {
    // read from stdin and buffer output
    close(pipes[1]);
    char *buf = NULL;
    int size = BUFREADSZ;
    REMALLOC(buf, size);
    int nbytes = 0;
    int nread = 0;
    while ((nbytes = read(pipes[0], buf + nread, size - nread)) > 0)
    {
      size *= 2;
      nread += nbytes;
      REMALLOC(buf, size);
    }
    if (nbytes < 0)
    {
      duk_push_sprintf(ctx, "error reading stdout: %s", strerror(errno));
      (void)duk_throw(ctx);
    }
    // push string to stack and exit
    duk_push_string(ctx, buf);
    free(buf);
    free(args);
    return 1;
  }
}

duk_ret_t duk_util_readdir(duk_context *ctx)
{
}

static const duk_function_list_entry utils_funcs[] = {
    {"readln", duk_util_readln, 2 /*nargs*/},
    {"stat", duk_util_stat, 1},
    {"exec", duk_util_exec, DUK_VARARGS},
    {NULL, NULL, 0}};

static const duk_number_list_entry file_types[] = {
    {"SOCKET", S_IFSOCK},
    {"LINK", S_IFLNK},
    {"FILE", S_IFREG},
    {"BLOCK_DEVICE", S_IFBLK},
    {"DIRECTORY", S_IFDIR},
    {"CHARACTER_DEVICE", S_IFCHR},
    {"FIFO", S_IFIFO},
    {NULL, 0.0}};

static const duk_number_list_entry utils_consts[] = {
    {NULL, 0.0}};

duk_ret_t dukopen_module(duk_context *ctx)
{
  duk_push_object(ctx);

  // add stat constants
  duk_push_object(ctx);
  duk_put_number_list(ctx, -1, file_types);
  duk_put_prop_string(ctx, -2, "file_types");

  duk_put_function_list(ctx, -1, utils_funcs);
  duk_put_number_list(ctx, -1, utils_consts);

  return 1;
}
