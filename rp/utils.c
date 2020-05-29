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
#include <pthread.h>
#include <signal.h>
#include <utime.h> /* utime */
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
#define DUK_PUT(ctx, type, key, value, idx) \
  {                                         \
    duk_push_##type(ctx, value);            \
    duk_put_prop_string(ctx, idx, key);     \
  }
#define BUFREADSZ 4096
#define MAXBUFFER 536870912 /* 512mb */

/**
 * @typedef {Object} ReadFileOptions
 * @property {string} file - the file to be read
 * @property {string} mode - the file mode to be used. Defaults to "r"
 * @property {size_t=} offset - the start position of the read. Defaults to 0.
 * @property {long=} length - the number of bytes to be read. If undefined, it will read the whole file. Passing in a number, n <= 0 will read n bytes from the end.
 * @param {ReadFileOptions} The read options.
 * @returns {Buffer} A buffer containing the read bytes.
 */

#define SAFE_SEEK(fp, length, whence)                                                                    \
  if (fseek(f, length, whence))                                                                          \
  {                                                                                                      \
    fclose(f);                                                                                           \
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error seeking file '%s': %s", filename, strerror(errno)); \
    return duk_throw(ctx);                                                                               \
  }

duk_ret_t duk_util_read_file(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "file");
  const char *filename = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "mode");
  const char *mode = duk_get_string_default(ctx, -1, "r");
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "offset");
  size_t offset = (size_t)duk_get_number_default(ctx, -1, 0);
  duk_pop(ctx);

  if (offset < 0)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "offset cannot be negative");
    return duk_throw(ctx);
  }

  duk_get_prop_string(ctx, -1, "length");
  long length = (long)duk_get_number_default(ctx, -1, 0);
  duk_pop(ctx);

  FILE *f = fopen(filename, mode);
  if (f == NULL)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening '%s': %s", filename, strerror(errno));
    return duk_throw(ctx);
  }

  if (length > 0)
  {
    SAFE_SEEK(f, length, SEEK_SET);
  }
  else
  {
    SAFE_SEEK(f, length, SEEK_END);
    long cur_offset;
    if ((cur_offset = ftell(f)) == -1)
    {
      duk_push_error_object(ctx, DUK_ERR_ERROR, "error getting offset '%s': %s", filename, strerror(errno));
      return duk_throw(ctx);
    }
    length = cur_offset - offset;
  }

  if (length < 0)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "start position appears after end position");
    return duk_throw(ctx);
  }

  SAFE_SEEK(f, offset, SEEK_SET);

  void *buf = duk_push_fixed_buffer(ctx, length);

  size_t off = 0;
  size_t nbytes;
  while ((nbytes = fread(buf + off, 1, length - off, f)) != 0)
  {
    off += nbytes;
  }

  if (ferror(f))
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error reading file '%s': %s", filename, strerror(errno));
    return duk_throw(ctx);
  }
  fclose(f);

  return 1;
}

int open_file_with_callback(duk_context *ctx, FILE **fp, int *func_idx, const char **filename)
{
  int i = 0;

  if (duk_is_string(ctx, i) || duk_is_string(ctx, ++i))
    *filename = duk_get_string(ctx, i);
  else
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "readln requires a file name passed as a string");
    return 1;
  }

  i = !i; /* get the other option */

  if (duk_is_function(ctx, i))
    *func_idx = i;
  else
  {
    duk_push_string(ctx, "readln requires a callback function");
    return 1;
  }

  struct stat path_stat;
  int err = stat(*filename, &path_stat);
  if (err)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error resolving '%s': %s", *filename, strerror(errno));
    return 1;
  }
  if (!S_ISREG(path_stat.st_mode))
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "'%s' is not a file", *filename);
    return 1;
  }
  *fp = fopen(*filename, "r");
  return 0;
}

duk_ret_t duk_util_readln(duk_context *ctx)
{
  int func_idx = -1;
  FILE *fp;
  const char *filename;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  if (open_file_with_callback(ctx, &fp, &func_idx, &filename))
  {
    // return error that was put on stack
    return duk_throw(ctx);
  }

  if (fp == NULL)
  {
    const char *fn = duk_to_string(ctx, !func_idx);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening '%s': %s", fn, strerror(errno));
    return duk_throw(ctx);
  }
  while ((read = getline(&line, &len, fp)) != -1)
  {
    duk_dup(ctx, func_idx);
    duk_push_this(ctx);
    duk_push_lstring(ctx, line, read - 1);
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
    duk_push_this(ctx);                               \
    duk_get_prop_string(ctx, -1, "mode");             \
    int mode = duk_require_int(ctx, -1);              \
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
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error getting status '%s': %s", path, strerror(errno));
    return duk_throw(ctx);
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
  mtime = path_stat.st_mtimespec.tv_sec * 1000;
  ctime = path_stat.st_ctimespec.tv_sec * 1000;
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
struct exec_thread_waitpid_arg
{
  pid_t pid;
  unsigned int timeout;
  int signal;
  unsigned int killed;
};
void *duk_util_exec_thread_waitpid(void *arg)
{
  struct exec_thread_waitpid_arg *arg_s = ((struct exec_thread_waitpid_arg *)arg);
  usleep(arg_s->timeout);
  kill(arg_s->pid, arg_s->signal);
  arg_s->killed = 1;
  return NULL;
}

#define DUK_UTIL_EXEC_READ_FD(ctx, buf, fildes, nread)                                                \
  {                                                                                                   \
    int size = BUFREADSZ;                                                                             \
    REMALLOC(buf, size);                                                                              \
    int nbytes = 0;                                                                                   \
    nread = 0;                                                                                        \
    while ((nbytes = read(fildes, buf + nread, size - nread)) > 0)                                    \
    {                                                                                                 \
      size *= 2;                                                                                      \
      nread += nbytes;                                                                                \
      REMALLOC(buf, size);                                                                            \
    }                                                                                                 \
    if (nbytes < 0)                                                                                   \
    {                                                                                                 \
      duk_push_error_object(ctx, DUK_ERR_ERROR, "could not read output buffer: %s", strerror(errno)); \
      return duk_throw(ctx);                                                                          \
    }                                                                                                 \
  }

/**
 * Executes a command where the arguments are the arguments to execv.
 * @param {string} path - The path to the program to execute.
 * @param {string[]} args - The arguments to provide to the program (including the program name).
 * @param {int} timeout - The optional timeout in microseconds.
 * @param {int} kill_signal - The signal to use to kill a timed out process. Default is SIGKILL (9)
 * @returns an object with stdout, stderr and return status
 * Ex.
 * const { 
 *    stdout: string, 
 *    stderr: string, 
 *    exit_status: int,
 *    timed_out: bool
 * } = utils.exec({ 
 *    path: "/bin/ls", 
 *    args: ["ls", "-1"], 
 *    timeout: 1000
 *    kill_signal: 9 });
 */
duk_ret_t duk_util_exec(duk_context *ctx)
{

  // get options
  duk_get_prop_string(ctx, -1, "timeout");
  unsigned int timeout = duk_get_uint_default(ctx, -1, 0);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "kill_signal");
  int kill_signal = duk_get_int_default(ctx, -1, SIGKILL);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "path");
  const char *path = duk_require_string(ctx, -1);
  duk_pop(ctx);

  // get arguments into null terminated buffer
  duk_get_prop_string(ctx, -1, "args");
  duk_size_t nargs = duk_get_length(ctx, -1);
  char **args = NULL;
  REMALLOC(args, (nargs + 1) * sizeof(char *));
  for (int i = 0; i < nargs; i++)
  {
    duk_get_prop_index(ctx, -1, i);
    args[i] = (char *)duk_require_string(ctx, -1);
    duk_pop(ctx);
  }
  args[nargs] = NULL;
  duk_pop(ctx);

  int stdout_pipe[2];
  int stderr_pipe[2];
  if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "could not create pipe: %s", strerror(errno));
    return duk_throw(ctx);
  }
  pid_t pid;
  if ((pid = fork()) == -1)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "could not fork: %s", strerror(errno));
    return duk_throw(ctx);
  }
  else if (pid == 0)
  {
    // make pipe equivalent to stdout and stderr
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    // close unused pipes
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    execv(path, args);
    fprintf(stderr, "could not execute %s\n", args[0]);
    exit(EXIT_FAILURE);
  }
  // create thread for timeout
  struct exec_thread_waitpid_arg arg;
  pthread_t thread;
  arg.signal = kill_signal;
  arg.pid = pid;
  arg.timeout = timeout;
  arg.killed = 0;
  if (timeout > 0)
  {
    pthread_create(&thread, NULL, duk_util_exec_thread_waitpid, &arg);
  }
  int exit_status;
  waitpid(pid, &exit_status, 0);
  // cancel timeout thread in case it is still running
  if (timeout > 0)
  {
    pthread_cancel(thread);
    pthread_join(thread, NULL);
  }
  // close unused pipes
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  char *stdout_buf = NULL;
  char *stderr_buf = NULL;

  // read output
  int stdout_nread, stderr_nread;
  DUK_UTIL_EXEC_READ_FD(ctx, stdout_buf, stdout_pipe[0], stdout_nread);
  DUK_UTIL_EXEC_READ_FD(ctx, stderr_buf, stderr_pipe[0], stderr_nread);

  // push return object
  duk_push_object(ctx);

  duk_push_lstring(ctx, stdout_buf, stdout_nread);
  duk_put_prop_string(ctx, -2, "stdout");

  duk_push_lstring(ctx, stderr_buf, stderr_nread);
  duk_put_prop_string(ctx, -2, "stderr");

  DUK_PUT(ctx, boolean, "timed_out", arg.killed, -2);
  DUK_PUT(ctx, int, "exit_status", exit_status, -2);
  free(stdout_buf);
  free(stderr_buf);
  free(args);
  return 1;
}

/**
 * Creates a directory with the name given as a path
 * @param {path} - the directory to be created
 * @param {mode} - the mode of the newly created directory (default: 0777)
 * Ex.
 * utils.mkdir("new/directory")
 */
duk_ret_t duk_util_mkdir(duk_context *ctx)
{

  duk_idx_t nargs = duk_get_top(ctx);

  const char *path;

  mode_t mode;
  if (nargs == 2) // a file mode was specified
  {
    path = duk_get_string(ctx, -2);
    const char *str_mode = duk_get_string(ctx, -1);
    mode = atoi(str_mode);
  }
  else if (nargs == 1) // default to ACCESSPERMS (0777)
  {
    path = duk_get_string(ctx, -1);
    mode = ACCESSPERMS;
  }
  else
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "too many arguments");
    return duk_throw(ctx);
  }

  char _path[PATH_MAX];

  strcpy(_path, path);

  /* Move through the path string to recurisvely create directories */
  for (char *p = _path + 1; *p; p++)
  {

    if (*p == '/')
    {

      *p = '\0';

      if (mkdir(_path, mode) != 0)
      {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error creating directory: %s", strerror(errno));
        return duk_throw(ctx);
      }

      *p = '/';
    }
  }

  if (mkdir(path, mode) != 0)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error creating directory: %s", strerror(errno));
    return duk_throw(ctx);
  }

  return 0;
}

/**
 * Reads the directory given by path.
 * @param {path} the directory
 * @returns an array of file names
 */
duk_ret_t duk_util_readdir(duk_context *ctx)
{
  const char *path = duk_require_string(ctx, -1);
  DIR *dir = opendir(path);
  if (dir == NULL)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "could not open directory %s: %s", path, strerror(errno));
    return duk_throw(ctx);
  }

  struct dirent *entry;
  errno = 0;
  duk_push_array(ctx);
  int i = 0;
  while ((entry = readdir(dir)) != NULL)
  {
    duk_push_string(ctx, entry->d_name);
    duk_put_prop_index(ctx, -2, i++);
  }
  if (errno)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error reading directory %s: %s", path, strerror(errno));
    return duk_throw(ctx);
  }
  closedir(dir);
  return 1;
}

#define DUK_UTIL_REMOVE_FILE(ctx, file)                                                            \
  if (remove(file))                                                                                \
  {                                                                                                \
    duk_push_error_object(ctx, DUK_ERR_ERROR, "could not remove '%s': %s", file, strerror(errno)); \
    return duk_throw(ctx);                                                                         \
  }

/**                                                                                                
 * Copies the file from src to dest. Passing overwrite will overwrite any file already present.    
 * It will try to preserve the file mode.                                                          
 * @param {{ src: string, dest: string, overwrite: boolean }} options - the options to be given                  
 */
duk_ret_t duk_util_copy_file(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "src");
  const char *src_filename = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "dest");
  const char *dest_filename = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "overwrite");
  int overwrite = duk_get_boolean_default(ctx, -1, 0);
  duk_pop(ctx);

  FILE *src = fopen(src_filename, "r");
  if (src == NULL)
  {
    fclose(src);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "could not open file '%s': %s", src_filename, strerror(errno));
    return duk_throw(ctx);
  }
  struct stat src_stat;
  if (stat(src_filename, &src_stat))
  {
    fclose(src);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error getting status '%s': %s", src_filename, strerror(errno));
    return duk_throw(ctx);
  }
  struct stat dest_stat;
  int err = stat(dest_filename, &dest_stat);
  if (!err && !overwrite)
  {
    // file exists and shouldn't be overwritten
    fclose(src);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error copying '%s': %s", dest_filename, "file already exists");
    return duk_throw(ctx);
  }
  if (err && errno != ENOENT)
  {
    fclose(src);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error getting status '%s': %s", dest_filename, strerror(errno));
    return duk_throw(ctx);
  }

  FILE *dest = fopen(dest_filename, "w");
  if (dest == NULL)
  {
    fclose(src);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "could not open file '%s': %s", dest_filename, strerror(errno));
    return duk_throw(ctx);
  }
  char buf[BUFREADSZ];
  int nread;
  while ((nread = read(fileno(src), buf, BUFREADSZ)) > 0)
  {
    if (write(fileno(dest), buf, nread) != nread)
    {
      fclose(src);
      fclose(dest);
      DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
      duk_push_error_object(ctx, DUK_ERR_ERROR, "could not write to file '%s': %s", dest_filename, strerror(errno));
      return duk_throw(ctx);
    }
  }
  if (nread < 0)
  {
    fclose(src);
    fclose(dest);
    DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error reading file '%s': %s", src_filename, strerror(errno));
    return duk_throw(ctx);
  }
  if (chmod(dest_filename, src_stat.st_mode))
  {
    DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
    fclose(src);
    fclose(dest);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error setting file mode %o for '%s': %s", src_stat.st_mode, dest_filename, strerror(errno));
    return duk_throw(ctx);
  }
  fclose(src);
  fclose(dest);
  return 0;
}

/**
 * Creates a hard or symbolic link 
 * @param {{src: string, target: string, symbolic: boolean }} options
 * Ex.
 * utils.link({ src: "some_file", target: "some_link", symbolic: true });
 */
duk_ret_t duk_util_link(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "src");
  const char *src = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "target");
  const char *target = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "symbolic");
  int symbolic = duk_get_boolean_default(ctx, -1, 0);
  duk_pop(ctx);

  if (symbolic)
  {
    if (symlink(src, target))
    {
      duk_push_error_object(ctx, DUK_ERR_ERROR, "error creating symbolic link from '%s' to '%s': %s", src, target, strerror(errno));
      return duk_throw(ctx);
    }
  }
  else
  {
    if (link(src, target))
    {
      duk_push_error_object(ctx, DUK_ERR_ERROR, "error creating hard link from '%s' to '%s': %s", src, target, strerror(errno));
      return duk_throw(ctx);
    }
  }
  return 0;
}
/**
 * @param {string} old - the source file or directory
 * @param {string} new - the target path
 * 
 * Ex.
 * utils.rename("sample.txt", "sample-2.txt");
 */
duk_ret_t duk_util_rename(duk_context *ctx)
{
  const char *old = duk_require_string(ctx, -2);
  const char *new = duk_require_string(ctx, -1);

  if (rename(old, new))
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error renaming '%s' to '%s': %s", old, new, strerror(errno));
    return duk_throw(ctx);
  }

  return 0;
}

/**
 * Removes an empty directory with the name given as a path. Allows recursively removing nested directories 
 * @param {path} - The path to the directory to be deleted
 * @param {recursive: boolean} - recursively delete
 * Ex.
 * utils.rmdir("directory/to/be/deleted")
 */
duk_ret_t duk_util_rmdir(duk_context *ctx)
{
  duk_idx_t nargs = duk_get_top(ctx);

  const char *path;

  mode_t mode;
  int recursive;
  if (nargs == 1) // Non-recursive deletion
  {
    path = duk_require_string(ctx, -1);
    recursive = 0;
  }
  else if (nargs == 2) // An option was specified
  {
    path = duk_require_string(ctx, -2);
    recursive = duk_require_boolean(ctx, -1);
  }
  else
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "too many arguments");
    return duk_throw(ctx);
  }

  char _path[PATH_MAX];

  strcpy(_path, path);

  if (rmdir(path) != 0)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error removing directory: %s", strerror(errno));
    return duk_throw(ctx);
  }

  if (recursive)
  {
    int length = strlen(_path);
    for (char *p = _path + length - 1; p != _path; p--)
    { // Traverse the path backwards to delete nested directories

      if (*p == '/')
      {

        *p = '\0';

        if (rmdir(_path) != 0)
        {
          duk_push_error_object(ctx, DUK_ERR_ERROR, "error removing directory: %s", strerror(errno));
          return duk_throw(ctx);
        }

        *p = '/';
      }
    }
  }

  return 0;
}

/**
 * Changes the file permissions of a specified file
 * @param {path} - The path to the file
 * @param {mode} - The new permissions for the file
 */
duk_ret_t duk_util_chmod(duk_context *ctx)
{
  const char *path = duk_require_string(ctx, -2);
  mode_t new_mode = duk_require_int(ctx, -1);

  if (chmod(path, new_mode) == -1)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing permissions: %s", strerror(errno));
    return duk_throw(ctx);
  }

  return 0;
}

/**
 * Updates last access time to now. Creates the file if it doesn't exist
 * @param {string} path - The path to the file to update/create
 * @param {boolean} nocreate - Don't create the file if exist (defaults to false)
 * @param {string} reference - A file to copy last access time from instead of current time
 * @param {boolean} setaccess - Set the access time (defaults to setting both access and modified if neither specified)
 * @param {boolean} setmodify - Set the modified time (defaults to setting both access and modified if neither specified)
 */
duk_ret_t duk_util_touch(duk_context *ctx)
{

  duk_get_prop_string(ctx, -1, "path");
  const char *path = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "nocreate");
  int nocreate = duk_get_boolean_default(ctx, -1, 0);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "reference");
  const char *reference = duk_get_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "setaccess");
  int setaccess = duk_get_boolean_default(ctx, -1, 1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "setmodify");
  int setmodify = duk_get_boolean_default(ctx, -1, 1);
  duk_pop(ctx);

  struct stat filestat;
  if (stat(path, &filestat) != 0) // file doesn't exist
  {
    if (nocreate)
    {
      return 0;
    }
    else
    {
      FILE *fp = fopen(path, "w"); // create file
      fclose(fp);
      return 0;
    }
  }

  time_t new_mtime, new_atime;

  struct stat refrence_stat;

  if (reference)
  {

    if (stat(reference, &refrence_stat) != 0) //reference file doesn't exist
    {
      duk_push_error_object(ctx, DUK_ERR_ERROR, "reference file does not exist");
      return duk_throw(ctx);
    }

    new_mtime = setmodify ? refrence_stat.st_mtime : filestat.st_mtime; // if setmodify, update m_time
    new_atime = setaccess ? refrence_stat.st_atime : filestat.st_atime; // if setacccess, update a_time
  }
  else
  {
    new_mtime = setmodify ? time(NULL) : filestat.st_mtime; //set to current time if set modify
    new_atime = setaccess ? time(NULL) : filestat.st_atime;
  }

  struct utimbuf new_times;

  new_times.actime = new_atime;
  new_times.modtime = new_mtime;

  utime(path, &new_times);

  return 0;
}

/**
 * Deletes a file at the given path
 * @param {string} path - the file to be deleted
*/
duk_ret_t duk_util_delete(duk_context *ctx)
{
  const char* file = duk_require_string(ctx, -1);

  if (remove(file) != 0)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error deleting file: %s", strerror(errno));
    return duk_throw(ctx);
  }

  return 0; 
}

static const duk_function_list_entry utils_funcs[] = {
    {"readFile", duk_util_read_file, 1},
    {"readln", duk_util_readln, 2 /*nargs*/},
    {"stat", duk_util_stat, 1},
    {"exec", duk_util_exec, 1},
    {"readdir", duk_util_readdir, 1},
    {"copyFile", duk_util_copy_file, 1},
    {"link", duk_util_link, 1},
    {"mkdir", duk_util_mkdir, DUK_VARARGS},
    {"rmdir", duk_util_rmdir, DUK_VARARGS},
    {"rename", duk_util_rename, 2},
    {"chmod", duk_util_chmod, 2},
    {"touch", duk_util_touch, 1},
    {"delete", duk_util_delete, 1},
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

static const duk_number_list_entry signals[] = {
    {"SIGKILL", SIGKILL},
    {"SIGTERM", SIGTERM},
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

  duk_push_object(ctx);
  duk_put_number_list(ctx, -1, signals);
  duk_put_prop_string(ctx, -2, "signals");

  duk_put_function_list(ctx, -1, utils_funcs);
  duk_put_number_list(ctx, -1, utils_consts);

  return 1;
}
