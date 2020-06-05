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
#include <pwd.h> /* getpwnam */
#include <grp.h> /* getgrnam */

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

#define SAFE_SEEK(fp, length, whence)                                                                    \
  if (fseek(fp, length, whence))                                                                         \
  {                                                                                                      \
    fclose(fp);                                                                                          \
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error seeking file '%s': %s", filename, strerror(errno)); \
    return duk_throw(ctx);                                                                               \
  }
/**
 * Reads a specified number of bytes from a file into a buffer.
 * @typedef {Object} ReadFileOptions
 * @property {string} file - the file to be read
 * @property {size_t=} offset - the start position of the read. Defaults to 0.
 * @property {long=} length - the number of bytes to be read. If undefined, it will read the whole file. Passing in a number, n <= 0 will read n bytes from the end.
 * @param {ReadFileOptions} The read options.
 * @returns {Buffer} A buffer containing the read bytes.
 */
duk_ret_t duk_util_read_file(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "file");
  const char *filename = duk_require_string(ctx, -1);
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

  FILE *fp = fopen(filename, "r");
  if (fp == NULL)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening '%s': %s", filename, strerror(errno));
    return duk_throw(ctx);
  }

  if (length > 0)
  {
    SAFE_SEEK(fp, length, SEEK_SET);
  }
  else
  {
    SAFE_SEEK(fp, length, SEEK_END);
    long cur_offset;
    if ((cur_offset = ftell(fp)) == -1)
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

  SAFE_SEEK(fp, offset, SEEK_SET);

  void *buf = duk_push_fixed_buffer(ctx, length);

  size_t off = 0;
  size_t nbytes;
  while ((nbytes = fread(buf + off, 1, length - off, fp)) != 0)
  {
    off += nbytes;
  }

  if (ferror(fp))
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error reading file '%s': %s", filename, strerror(errno));
    return duk_throw(ctx);
  }
  fclose(fp);

  return 1;
}

duk_ret_t duk_util_readln_finalizer(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filepointer"));
  FILE *fp = duk_get_pointer(ctx, -1);
  duk_pop(ctx);
  if (fp)
  {
    fclose(fp);
  }
  return 0;
}

duk_ret_t duk_util_iter_readln(duk_context *ctx)
{
  duk_push_this(ctx);

  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filename"));
  const char *filename = duk_get_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filepointer"));
  FILE *fp = duk_get_pointer(ctx, -1);
  duk_pop(ctx);

  // already at the end of the iterator
  if (fp == NULL)
  {
    // return object
    duk_push_object(ctx);

    duk_push_null(ctx);
    duk_put_prop_string(ctx, -2, "value");

    duk_push_boolean(ctx, 0);
    duk_put_prop_string(ctx, -2, "done");
    return 1;
  }

  char *line = NULL;
  size_t len = 0;
  errno = 0;
  int nread = getline(&line, &len, fp);
  if (errno)
  {
    if (line)
      free(line);
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error reading file '%s': %s", filename, strerror(errno));
    return duk_throw(ctx);
  }
  // return object
  duk_push_object(ctx);

  duk_push_string(ctx, line);
  duk_put_prop_string(ctx, -2, "value");

  duk_push_boolean(ctx, nread == -1);
  duk_put_prop_string(ctx, -2, "done");

  if (nread == -1)
  {
    fclose(fp);
  }

  if (line)
    free(line);
  return 1;
}
/**
 * Reads a file line by line using getline and javascript iterators.
 * @param {string} filename - the path to the file to be read.
 * @returns {Iterator} an object with a Symbol.iterator.
 */
duk_ret_t duk_util_readln(duk_context *ctx)
{
  const char *filename = duk_require_string(ctx, -1);
  FILE *fp = fopen(filename, "r");
  if (fp == NULL)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening '%s': %s", filename, strerror(errno));
    return duk_throw(ctx);
  }

  // return object
  duk_push_object(ctx);

  // [Symbol.iterator] function
  duk_push_string(ctx, "(function() { return function getiter(iter) { return (function () { return iter; }); }})()");
  duk_eval(ctx);

  // iterator object
  duk_push_object(ctx);

  // add fp, filename and finalizer
  duk_push_pointer(ctx, fp);
  duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));

  duk_push_string(ctx, filename);
  duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filename"));

  duk_push_c_function(ctx, duk_util_readln_finalizer, 1);
  duk_set_finalizer(ctx, -2);

  // next
  duk_push_c_function(ctx, duk_util_iter_readln, 0);
  duk_put_prop_string(ctx, -2, "next");
  duk_call(ctx, 1);

  // iterator function is at top of stack
  duk_put_prop_string(ctx, -2, DUK_WELLKNOWN_SYMBOL("Symbol.iterator"));

  return 1;
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
 *  @typedef {Object} StatObject
 *  @property {int} dev - id of device containing file
 *  @property {int} ino - inode number
 *  @property {int} mode - the file mode
 *  @property {int} nlink - the number of hard links
 *  @property {int} uid - the user id of the owner
 *  @property {int} gid - the group id of the owner
 *  @property {int} rdev - device id if special file
 *  @property {int} size - total size in bytes
 *  @property {int} blksize - the blocksize for the system I/O
 *  @property {int} blocks - the number of blocks
 *  @property {Date} atime - time of last access
 *  @property {Date} mtime - time of last modification
 *  @property {Date} ctime - time of last status
 * 
 *  @param {string} The path name
 *  @returns {StatObject} a javascript object of the following form:
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

  long long atime, mtime, ctime;
  atime = path_stat.st_atime * 1000;
  mtime = path_stat.st_mtime * 1000;
  ctime = path_stat.st_ctime * 1000;

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
 * @typedef {Object} ExecOptions 
 * @property {string} path - The path to the program to execute.
 * @property {string[]} args - The arguments to provide to the program (including the program name).
 * @property {int} timeout - The optional timeout in microseconds.
 * @property {int=} killSignal - The signal to use to kill a timed out process. Default is SIGKILL (9)
 * @property {int=} background - Whether to put the process in the background. stdout, stderr will be null in this case.
 * 
 * @typedef {Object} ExecReturnObject 
 * @property {string?} stdout - The stdout of the program as a string. Will be null if background is set in ExecOptions.
 * @property {string?} stderr - The stderr of the program as a string. Will be null if background is set in ExecOptions.
 * @property {int?} exitStatus - The exit status of the program. Will be null if background is set in ExecOptions.
 * @property {boolean} timedOut - whether the program timed out using after the specified timeout in ExecOptions.
 * @property {int} pid - the pid of the program.
 * 
 * @param {ExecOptions} options
 * @returns {ExecReturnObject}
 * Ex.
 * const { 
 *    stdout: string, 
 *    stderr: string, 
 *    exit_status: int,
 *    timed_out: bool,
 *    pid: int
 * } = utils.exec({ 
 *    path: "/bin/ls", 
 *    args: ["ls", "-1"], 
 *    timeout: 1000
 *    kill_signal: 9, background: false });
 */
duk_ret_t duk_util_exec(duk_context *ctx)
{

  // get options
  duk_get_prop_string(ctx, -1, "timeout");
  unsigned int timeout = duk_get_uint_default(ctx, -1, 0);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "killSignal");
  int kill_signal = duk_get_int_default(ctx, -1, SIGKILL);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "path");
  const char *path = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "background");
  int background = duk_get_boolean_default(ctx, -1, 0);
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
  if (!background)
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
    if (!background)
    {
      // make pipe equivalent to stdout and stderr
      dup2(stdout_pipe[1], STDOUT_FILENO);
      dup2(stderr_pipe[1], STDERR_FILENO);

      // close unused pipes
      close(stdout_pipe[0]);
      close(stderr_pipe[0]);
    }
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

  if (background)
  {
    // return object
    duk_push_object(ctx);

    DUK_PUT(ctx, int, "pid", pid, -2);

    // set stderr and stdout to null
    duk_push_null(ctx);
    duk_put_prop_string(ctx, -2, "stderr");
    duk_push_null(ctx);
    duk_put_prop_string(ctx, -2, "stdout");
    duk_push_null(ctx);
    duk_put_prop_string(ctx, -2, "exitStatus");

    // set timed_out to false
    DUK_PUT(ctx, boolean, "timedOut", 0, -2);
  }
  else
  {
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

    DUK_PUT(ctx, boolean, "timedOut", arg.killed, -2);
    DUK_PUT(ctx, int, "exitStatus", exit_status, -2);
    DUK_PUT(ctx, int, "pid", pid, -2);
    free(stdout_buf);
    free(stderr_buf);
  }
  free(args);
  return 1;
}
/**
 * Kills a process with the process id given by the argument
 * @param {int} process id
 * @param {int} signal
 */
duk_ret_t duk_util_kill(duk_context *ctx)
{
  pid_t pid = duk_require_int(ctx, -2);
  int signal = duk_require_int(ctx, -1);

  if (kill(pid, signal))
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error killing '%d' with signal '%d': %s", pid, signal, strerror(errno));
    return duk_throw(ctx);
  }

  return 0;
}

/**
 * Creates a directory with the name given as a path
 * @param {path} - the directory to be created
 * @param {mode=} - the mode of the newly created directory (default: 0777)
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
 * @typedef {Object} CopyFileOptions
 * @property {string} src - the path to the file source.
 * @property {string} dest - the path to where the file will be moved.
 * @property {string=} overwrite - whether to overwrite any existing file at dest. Set to false by default.                                                        
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
 * @typedef {Object} LinkOptions
 * @property {string} path - the path to the source file to link
 * @property {string} target - the path target file that will be created
 * @property {boolean=} symbolic - whether the link is symbolic. Set to false by default. 
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
 * Renames or moves a source file to a target path.
 * @param {string} old - the source file or directory
 * @param {string} new - the target path
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
 * @param {string} path - The path to the directory to be deleted
 * @param {boolean=} recursive - whether to recursively delete. Set to false by default.
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
 * Updates last access time to now. Creates the file if it doesn't exist.
 * @typedef {Object} TouchOptions
 * @property {string} path - The path to the file to update/create
 * @property {boolean=} nocreate - Don't create the file if exist (defaults to false)
 * @property {string?} reference - A file to copy last access time from instead of current time
 * @property {boolean=} setaccess - Set the access time (defaults to setting both access and modified if neither specified)
 * @property {boolean=} setmodify - Set the modified time (defaults to setting both access and modified if neither specified)
 * @param {TouchOptions} options
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
  const char *reference = duk_get_string_default(ctx, -1, NULL);
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
 * @param {string} file - the file to be deleted
 */
duk_ret_t duk_util_delete(duk_context *ctx)
{
  const char *file = duk_require_string(ctx, -1);

  if (remove(file) != 0)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error deleting file: %s", strerror(errno));
    return duk_throw(ctx);
  }

  return 0;
}

/** 
 * Changes ownership of a file to a given user or group.
 * @typedef {Object} ChownOptions
 * @property {string} path - the path to the file to change
 * @property {string} group_name - the name of the group to change ownership to
 * @property {string} user_name - the name of the user to change ownership to
 * @property {int} group_id - the group identifier to change ownership to 
 * @property {int} user_id - the user identifier to change ownership to
 * @param {ChownOptions} options
 */
duk_ret_t duk_util_chown(duk_context *ctx)
{
  duk_get_prop_string(ctx, -1, "path");
  const char *path = duk_require_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "group_name");
  const char *group_name = duk_get_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "user_name");
  const char *user_name = duk_get_string(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "group_id");
  gid_t group_id = duk_get_int(ctx, -1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "user_id");
  uid_t user_id = duk_get_int(ctx, -1);
  duk_pop(ctx);

  struct stat file_stat;

  if ((user_id != 0) && (user_name != 0)) // both a user name and user_id was specified
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "Error changing ownership: too many users were specified");
    return duk_throw(ctx);
  }

  if ((group_id != 0) && (group_name != 0)) // both a group name and group_id was specified
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing ownership: too many groups were specified");
    return duk_throw(ctx);
  }

  if (group_name != 0) // a group name was specfied; lookup the group id
  {
    struct group *grp = getgrnam(group_name);

    if (grp == NULL)
    {
      duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing  ownership: %s", strerror(errno));
      return duk_throw(ctx);
    }
    group_id = grp->gr_gid;
  }

  if (user_name != 0) // a user name was specified; lookup the user id
  {
    struct passwd *user = getpwnam(user_name);

    if (user == NULL)
    {
      duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing  ownership: %s", strerror(errno));
      return duk_throw(ctx);
    }
    user_id = user->pw_gid;
  }

  stat(path, &file_stat);
  if (user_id == 0) // no specified user
  {
    user_id = file_stat.st_uid;
  }

  if (group_id == 0) // no specified group
  {
    group_id = file_stat.st_gid;
  }

  if (chown(path, user_id, group_id) != 0)
  {
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing  ownership: %s", strerror(errno));
    return duk_throw(ctx);
  }

  return 0;
}

static const duk_function_list_entry utils_funcs[] = {
    {"readFile", duk_util_read_file, 1},
    {"readln", duk_util_readln, 1},
    {"stat", duk_util_stat, 1},
    {"exec", duk_util_exec, 1},
    {"kill", duk_util_kill, 2},
    {"readdir", duk_util_readdir, 1},
    {"copyFile", duk_util_copy_file, 1},
    {"link", duk_util_link, 1},
    {"mkdir", duk_util_mkdir, DUK_VARARGS},
    {"rmdir", duk_util_rmdir, DUK_VARARGS},
    {"rename", duk_util_rename, 2},
    {"chmod", duk_util_chmod, 2},
    {"touch", duk_util_touch, 1},
    {"delete", duk_util_delete, 1},
    {"chown", duk_util_chown, 1},
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
