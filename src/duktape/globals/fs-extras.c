/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 *
 * fs-extras.c
 *
 * Additional filesystem primitives for rampart.utils, organized into
 * five sections.  This file is #include'd from rampart-utils.c (same
 * compilation unit) so that it has free access to the internal helpers
 * defined there (rp_fopen, rp_stat, getfh_nonull, pushffunc, etc.).
 *
 *   Section 1: Path-based primitives that POSIX has but rampart didn't
 *              (readLink, truncate, statVfs, writeFile, appendFile,
 *              exists, tmpDir, homeDir, mkdTemp).
 *   Section 2: Extensions to the existing fopen handle (fh.fstat,
 *              fh.fsync, fh.fdatasync, fh.ftruncate, fh.fchmod,
 *              fh.fchown, fh.fUtimes, fh.fileNo).
 *   Section 3: POSIX integer-fd API (open, close, read, write,
 *              pread, pwrite, lseek, plus fd-keyed f-variants and
 *              rampart.utils.O.* open(2) flag constants).
 *   Section 4: Recursive helpers (cp, rm, glob, walkDir).
 *   Section 5: File watching (inotify on Linux, kqueue on BSD/macOS,
 *              polling on other platforms; all share one dispatch).
 *   Section 7: Compression / checksums (gzip, gunzip, deflate, inflate,
 *              deflateRaw, inflateRaw, crc32, adler32) via libdeflate.
 *
 * Section 6 is the single registration entry point
 * `rp_fs_extras_register(ctx)`, called from duk_rampart_init while
 * rampart.utils is on top of the duktape stack.
 *
 * All functions follow rampart-utils.c conventions: positional or
 * options-object arguments, throw via RP_THROW on error, mode args
 * accept either int or octal string, time args accept number-seconds
 * or Date.  Names use camelCase; the registration block also exposes
 * underscored / lowercase aliases where consistent with existing
 * rampart.utils precedent.
 */

#include <sys/statvfs.h>

/* ==========================================================================
 * Section 1 — Path-based primitives
 * ========================================================================== */

/* readLink(path) -> string (target of symlink)
 *
 * Throws if the path is not a symlink or readlink(2) fails.  lstat()
 * already returns the link target in the `link` property of its result
 * but a standalone helper is the conventional way to ask the question.
 */
static duk_ret_t duk_rp_readlink(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "readLink(): first argument must be a String (path)");
    char buf[PATH_MAX];
    ssize_t n = readlink(path, buf, sizeof(buf) - 1);
    if (n < 0)
        RP_THROW(ctx, "readLink(): %s: %s", path, strerror(errno));
    buf[n] = '\0';
    duk_push_lstring(ctx, buf, (duk_size_t)n);
    return 1;
}

/* truncate(path, length) -> void
 *
 * Truncate or extend the named file to `length` bytes.  Extending a
 * regular file beyond its current size creates a hole; reading the
 * hole returns NUL bytes.
 */
static duk_ret_t duk_rp_truncate(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "truncate(): first argument must be a String (path)");
    off_t length = 0;
    if (!duk_is_undefined(ctx, 1))
    {
        double dl = duk_get_number_default(ctx, 1, 0.0);
        if (dl < 0)
            RP_THROW(ctx, "truncate(): length must be non-negative");
        length = (off_t)dl;
    }
    if (truncate(path, length) != 0)
        RP_THROW(ctx, "truncate(): %s: %s", path, strerror(errno));
    return 0;
}

/* statVfs(path) -> object  (filesystem statistics)
 *
 * Returns a plain object with keys mirroring struct statvfs:
 *   { bsize, frsize, blocks, bfree, bavail, files, ffree, favail,
 *     fsid, flag, namemax }
 *
 * Sizes are in bytes (already multiplied by frsize for the convenience
 * keys totalBytes/freeBytes/availBytes).
 */
static duk_ret_t duk_rp_statvfs(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "statVfs(): first argument must be a String (path)");
    struct statvfs sv;
    if (statvfs(path, &sv) != 0)
        RP_THROW(ctx, "statVfs(): %s: %s", path, strerror(errno));

    duk_push_object(ctx);
    DUK_PUT_NUMBER(ctx, "bsize",   sv.f_bsize,   -2);
    DUK_PUT_NUMBER(ctx, "frsize",  sv.f_frsize,  -2);
    DUK_PUT_NUMBER(ctx, "blocks",  sv.f_blocks,  -2);
    DUK_PUT_NUMBER(ctx, "bfree",   sv.f_bfree,   -2);
    DUK_PUT_NUMBER(ctx, "bavail",  sv.f_bavail,  -2);
    DUK_PUT_NUMBER(ctx, "files",   sv.f_files,   -2);
    DUK_PUT_NUMBER(ctx, "ffree",   sv.f_ffree,   -2);
    DUK_PUT_NUMBER(ctx, "favail",  sv.f_favail,  -2);
    DUK_PUT_NUMBER(ctx, "fsid",    sv.f_fsid,    -2);
    DUK_PUT_NUMBER(ctx, "flag",    sv.f_flag,    -2);
    DUK_PUT_NUMBER(ctx, "namemax", sv.f_namemax, -2);

    /* Byte-denominated convenience values, sized for double precision */
    duk_push_number(ctx, (double)sv.f_frsize * (double)sv.f_blocks);
    duk_put_prop_string(ctx, -2, "totalBytes");
    duk_push_number(ctx, (double)sv.f_frsize * (double)sv.f_bfree);
    duk_put_prop_string(ctx, -2, "freeBytes");
    duk_push_number(ctx, (double)sv.f_frsize * (double)sv.f_bavail);
    duk_put_prop_string(ctx, -2, "availBytes");

    return 1;
}

/* Shared implementation for writeFile / appendFile.
 *
 * Arguments:  (path, data [, opts])
 *
 * data may be a String or a Buffer-like (duktape buffer, Uint8Array,
 * etc.).  opts may include { mode: <octal int or string>, encoding:
 * <ignored for now> }.
 *
 * If `append` is non-zero, fopen() is invoked in "a" mode; otherwise
 * in "w" mode.  Mode bits are applied via fchmod() after the fd is
 * obtained when an explicit mode is supplied (otherwise the system
 * umask determines the bits).
 */
static int rp_writefile_impl(duk_context *ctx, int append, const char *fname)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "%s(): first argument must be a String (path)", fname);
    const void *data = NULL;
    duk_size_t len = 0;
    int have_mode = 0;
    mode_t mode = 0644;
    const char *mode_str = append ? "a" : "w";

    if (duk_is_string(ctx, 1))
    {
        data = duk_get_lstring(ctx, 1, &len);
    }
    else if (duk_is_buffer_data(ctx, 1))
    {
        data = duk_get_buffer_data(ctx, 1, &len);
    }
    else
    {
        RP_THROW(ctx, "%s(): data must be a String or Buffer", fname);
    }

    if (duk_is_object(ctx, 2) && !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        if (duk_get_prop_string(ctx, 2, "mode"))
        {
            if (duk_is_string(ctx, -1))
            {
                const char *s = duk_get_string(ctx, -1);
                char *e;
                long v = strtol(s, &e, 8);
                if (s != e) { mode = (mode_t)v; have_mode = 1; }
            }
            else if (duk_is_number(ctx, -1))
            {
                mode = (mode_t)duk_get_int(ctx, -1);
                have_mode = 1;
            }
        }
        duk_pop(ctx);

        /* "encoding" key is accepted but, for now, the bytes go out
           exactly as supplied.  String args are already utf-8 in
           duktape.  This matches what rampart's other writers do. */
        if (duk_get_prop_string(ctx, 2, "flag"))
        {
            const char *flag = duk_get_string_default(ctx, -1, NULL);
            if (flag && *flag) mode_str = flag;
        }
        duk_pop(ctx);
    }

    FILE *fp = rp_fopen(path, mode_str);
    if (!fp)
        RP_THROW(ctx, "%s(): %s: %s", fname, path, strerror(errno));

    if (len > 0)
    {
        size_t written = 0;
        while (written < len)
        {
            size_t n = fwrite((const char *)data + written, 1, len - written, fp);
            if (n == 0)
            {
                int saved = errno;
                rp_fclose(fp);
                RP_THROW(ctx, "%s(): %s: %s",
                    fname, path, strerror(saved ? saved : EIO));
            }
            written += n;
        }
    }

    if (have_mode)
    {
        int fd = fileno(fp);
        if (fd >= 0) (void)fchmod(fd, mode);
    }

    if (rp_fclose(fp) != 0)
        RP_THROW(ctx, "%s(): %s: %s", fname, path, strerror(errno));

    return 0;
}

static duk_ret_t duk_rp_writefile(duk_context *ctx)
{
    return rp_writefile_impl(ctx, 0, "writeFile");
}

static duk_ret_t duk_rp_appendfile(duk_context *ctx)
{
    return rp_writefile_impl(ctx, 1, "appendFile");
}

/* exists(path) -> boolean.
 *
 * Sugar around stat().  Returns true if the path resolves to anything
 * accessible to the caller, false on ENOENT / ENOTDIR / permission
 * errors that prevent the stat from succeeding.  This matches node's
 * fs.existsSync — which famously eats all errors.
 */
static duk_ret_t duk_rp_exists(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "exists(): first argument must be a String (path)");
    struct stat st;
    duk_push_boolean(ctx, rp_stat(path, &st) == 0);
    return 1;
}

/* tmpDir() -> string
 *
 * Returns $TMPDIR, $TMP, or $TEMP if any is set in the environment,
 * otherwise the platform default (/tmp on POSIX, /tmp under Cygwin).
 * Trailing slashes are trimmed.  The returned path is not validated
 * to exist — that's the caller's job.
 */
static duk_ret_t duk_rp_tmpdir(duk_context *ctx)
{
    const char *env_keys[] = { "TMPDIR", "TMP", "TEMP", NULL };
    int i;
    for (i = 0; env_keys[i]; i++)
    {
        const char *v = getenv(env_keys[i]);
        if (v && *v)
        {
            size_t n = strlen(v);
            while (n > 1 && v[n-1] == '/') n--;
            duk_push_lstring(ctx, v, (duk_size_t)n);
            return 1;
        }
    }
    duk_push_string(ctx, "/tmp");
    return 1;
}

/* homeDir() -> string
 *
 * Returns $HOME if set, otherwise getpwuid(geteuid())->pw_dir.  Throws
 * only when both lookups fail (extremely rare; would mean no /etc/passwd
 * entry for the current uid and no HOME set).
 */
static duk_ret_t duk_rp_homedir(duk_context *ctx)
{
    const char *h = getenv("HOME");
    if (h && *h)
    {
        duk_push_string(ctx, h);
        return 1;
    }
    struct passwd *pw = getpwuid(geteuid());
    if (pw && pw->pw_dir && *pw->pw_dir)
    {
        duk_push_string(ctx, pw->pw_dir);
        return 1;
    }
    RP_THROW(ctx, "homeDir(): cannot determine home directory");
    return 0;  /* unreachable; silences -Wreturn-type */
}

/* mkdTemp(prefix) -> string
 *
 * Creates a uniquely-named directory using mkdtemp(3).  The argument is
 * treated as a *prefix*; "XXXXXX" is appended internally as mkdtemp(3)
 * requires.  If prefix already ends in slash or path separator, the
 * random suffix is appended directly to the trailing segment, matching
 * node's fs.mkdtempSync semantics ("/tmp/foo-" -> "/tmp/foo-AbCdEf").
 *
 * Returns the full path of the created directory.  Throws on failure.
 */
static duk_ret_t duk_rp_mkdtemp(duk_context *ctx)
{
    const char *prefix = REQUIRE_STRING(ctx, 0,
        "mkdTemp(): first argument must be a String (prefix)");
    size_t plen = strlen(prefix);
    char tmpl[PATH_MAX];
    if (plen + 7 >= sizeof(tmpl))
        RP_THROW(ctx, "mkdTemp(): prefix too long");
    memcpy(tmpl, prefix, plen);
    memcpy(tmpl + plen, "XXXXXX", 7);
    if (!mkdtemp(tmpl))
        RP_THROW(ctx, "mkdTemp(): %s: %s", prefix, strerror(errno));
    duk_push_string(ctx, tmpl);
    return 1;
}

/* ==========================================================================
 * Shared helpers used by Sections 2 and 3
 * ========================================================================== */

/* Resolve a mode argument (idx) that may be:
 *   - a number  : use directly
 *   - an octal string ("0644", "755", "0o600") : parsed as octal
 *
 * `dflt` is returned when the slot is undefined.  Throws on bad type.
 */
static mode_t rp_parse_mode_arg(duk_context *ctx, duk_idx_t idx,
                                 mode_t dflt, const char *fname)
{
    if (duk_is_undefined(ctx, idx)) return dflt;
    if (duk_is_number(ctx, idx)) return (mode_t)duk_get_int(ctx, idx);
    if (duk_is_string(ctx, idx))
    {
        const char *s = duk_get_string(ctx, idx);
        if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) s += 2;
        char *e;
        long v = strtol(s, &e, 8);
        if (s == e) RP_THROW(ctx, "%s(): invalid octal mode \"%s\"",
                              fname, duk_get_string(ctx, idx));
        return (mode_t)v;
    }
    RP_THROW(ctx, "%s(): mode must be a number or octal string", fname);
    return 0;  /* unreachable */
}

/* Resolve a (user, group) pair following rampart's chown(2) style:
 *   - either argument may be a name (looked up via getpwnam/getgrnam)
 *     or a numeric id (negative = leave unchanged, mapping (uid_t)-1)
 *
 * `*uid_out` / `*gid_out` get the resolved ids on return.  Throws on
 * name-lookup failure or wrong arg type.
 */
static void rp_parse_chown_args(duk_context *ctx, duk_idx_t uidx, duk_idx_t gidx,
                                 uid_t *uid_out, gid_t *gid_out, const char *fname)
{
    *uid_out = (uid_t)-1;
    *gid_out = (gid_t)-1;
    if (!duk_is_undefined(ctx, uidx))
    {
        if (duk_is_string(ctx, uidx))
        {
            const char *n = duk_get_string(ctx, uidx);
            struct passwd *pw = getpwnam(n);
            if (!pw) RP_THROW(ctx, "%s(): unknown user '%s'", fname, n);
            *uid_out = pw->pw_uid;
        }
        else if (duk_is_number(ctx, uidx))
        {
            int v = duk_get_int(ctx, uidx);
            if (v >= 0) *uid_out = (uid_t)v;
        }
        else RP_THROW(ctx, "%s(): user must be a name or number", fname);
    }
    if (!duk_is_undefined(ctx, gidx))
    {
        if (duk_is_string(ctx, gidx))
        {
            const char *n = duk_get_string(ctx, gidx);
            struct group *gr = getgrnam(n);
            if (!gr) RP_THROW(ctx, "%s(): unknown group '%s'", fname, n);
            *gid_out = gr->gr_gid;
        }
        else if (duk_is_number(ctx, gidx))
        {
            int v = duk_get_int(ctx, gidx);
            if (v >= 0) *gid_out = (gid_t)v;
        }
        else RP_THROW(ctx, "%s(): group must be a name or number", fname);
    }
}

/* Resolve a time argument (number-of-seconds, or Date object).
 * Returns the value or `dflt` if the slot is undefined / not recognized.
 *
 * Normalizes the input index up front so subsequent stack pushes don't
 * shift it out from under us. */
static time_t rp_parse_time_arg(duk_context *ctx, duk_idx_t idx, time_t dflt)
{
    idx = duk_normalize_index(ctx, idx);
    if (duk_is_undefined(ctx, idx)) return dflt;
    if (duk_is_number(ctx, idx)) return (time_t)duk_get_number(ctx, idx);
    if (duk_is_object(ctx, idx) &&
        duk_has_prop_string(ctx, idx, "getMilliseconds") &&
        duk_has_prop_string(ctx, idx, "getUTCDay"))
    {
        duk_push_string(ctx, "getTime");
        duk_call_prop(ctx, idx, 0);
        time_t t = (time_t)(duk_get_number(ctx, -1) / 1000.0);
        duk_pop(ctx);
        return t;
    }
    return dflt;
}

/* Push a stat-shaped object built from `struct stat *st`.
 *
 * This mirrors duk_rp_stat_lstat's output but skips the access(R_OK/W_OK)
 * probes (the file is already open / fd-addressable; access bits don't
 * apply the same way).  Used by fh.fstat and fstatFd.
 */
static void rp_push_stat_object(duk_context *ctx, const struct stat *st)
{
    struct passwd *pw = getpwuid(st->st_uid);
    struct group  *gr = getgrgid(st->st_gid);
    char perms[11];

    duk_push_object(ctx);

    if (pw) duk_push_string(ctx, pw->pw_name);
    else    duk_push_sprintf(ctx, "%d", (int)st->st_uid);
    duk_put_prop_string(ctx, -2, "owner");

    if (gr) duk_push_string(ctx, gr->gr_name);
    else    duk_push_sprintf(ctx, "%d", (int)st->st_gid);
    duk_put_prop_string(ctx, -2, "group");

    DUK_PUT_NUMBER(ctx, "dev",     st->st_dev,     -2);
    DUK_PUT_NUMBER(ctx, "ino",     st->st_ino,     -2);
    DUK_PUT_NUMBER(ctx, "mode",    st->st_mode,    -2);
    DUK_PUT_NUMBER(ctx, "nlink",   st->st_nlink,   -2);
    DUK_PUT_NUMBER(ctx, "uid",     st->st_uid,     -2);
    DUK_PUT_NUMBER(ctx, "gid",     st->st_gid,     -2);
    DUK_PUT_NUMBER(ctx, "rdev",    st->st_rdev,    -2);
    DUK_PUT_NUMBER(ctx, "size",    st->st_size,    -2);
    DUK_PUT_NUMBER(ctx, "blksize", st->st_blksize, -2);
    DUK_PUT_NUMBER(ctx, "blocks",  st->st_blocks,  -2);

    /* atime / mtime / ctime as Date */
    int64_t atms = (int64_t)st->st_atime * 1000;
    int64_t mtms = (int64_t)st->st_mtime * 1000;
    int64_t ctms = (int64_t)st->st_ctime * 1000;
    (void)duk_get_global_string(ctx, "Date");
    duk_push_number(ctx, atms); duk_new(ctx, 1);
    duk_put_prop_string(ctx, -2, "atime");
    (void)duk_get_global_string(ctx, "Date");
    duk_push_number(ctx, mtms); duk_new(ctx, 1);
    duk_put_prop_string(ctx, -2, "mtime");
    (void)duk_get_global_string(ctx, "Date");
    duk_push_number(ctx, ctms); duk_new(ctx, 1);
    duk_put_prop_string(ctx, -2, "ctime");

    /* Boolean type predicates (rampart-utils convention: properties not methods) */
    duk_push_boolean(ctx, S_ISBLK(st->st_mode));  duk_put_prop_string(ctx, -2, "isBlockDevice");
    duk_push_boolean(ctx, S_ISCHR(st->st_mode));  duk_put_prop_string(ctx, -2, "isCharacterDevice");
    duk_push_boolean(ctx, S_ISDIR(st->st_mode));  duk_put_prop_string(ctx, -2, "isDirectory");
    duk_push_boolean(ctx, S_ISFIFO(st->st_mode)); duk_put_prop_string(ctx, -2, "isFIFO");
    duk_push_boolean(ctx, S_ISREG(st->st_mode));  duk_put_prop_string(ctx, -2, "isFile");
    duk_push_boolean(ctx, S_ISSOCK(st->st_mode)); duk_put_prop_string(ctx, -2, "isSocket");

    /* rwxrwxrwx permission string */
    perms[0] = S_ISDIR(st->st_mode) ? 'd' : '-';
    perms[1] = (st->st_mode & S_IRUSR) ? 'r' : '-';
    perms[2] = (st->st_mode & S_IWUSR) ? 'w' : '-';
    perms[3] = (st->st_mode & S_ISUID) ? 's' : ((st->st_mode & S_IXUSR) ? 'x' : '-');
    perms[4] = (st->st_mode & S_IRGRP) ? 'r' : '-';
    perms[5] = (st->st_mode & S_IWGRP) ? 'w' : '-';
    perms[6] = (st->st_mode & S_ISGID) ? 's' : ((st->st_mode & S_IXGRP) ? 'x' : '-');
    perms[7] = (st->st_mode & S_IROTH) ? 'r' : '-';
    perms[8] = (st->st_mode & S_IWOTH) ? 'w' : '-';
    perms[9] = (st->st_mode & S_ISVTX) ? 't' : ((st->st_mode & S_IXOTH) ? 'x' : '-');
    perms[10] = '\0';
    duk_push_string(ctx, perms);
    duk_put_prop_string(ctx, -2, "permissions");
}

/* Pull the FILE* out of `this` (an fopen-returned handle). */
#define fh_this_fp(ctx, name) ({                                \
    duk_push_this(ctx);                                         \
    FILE *_fp = getfh_nonull(ctx, -1, (name));                  \
    duk_pop(ctx);                                               \
    _fp;                                                        \
})

/* ---- symlink-specific variants of chmod/chown/utimes -----------------
 * lchown / lutimes work on Linux (lchown is a direct syscall; lutimes
 * uses utimensat with AT_SYMLINK_NOFOLLOW).
 *
 * lchmod is a hard case: Linux has no syscall to change the mode of a
 * symlink (the symlink's mode bits aren't used anyway).  We surface
 * ENOSYS-equivalent for actual symlinks on Linux, matching node, and
 * fall through to chmod() for non-symlinks so portable callers don't
 * have to special-case the not-actually-a-symlink case.  On macOS, the
 * real lchmod(2) is used.
 */
static duk_ret_t duk_rp_lchown(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "lchown(): first argument must be a String (path)");
    uid_t uid; gid_t gid;
    rp_parse_chown_args(ctx, 1, 2, &uid, &gid, "lchown");
    if (lchown(path, uid, gid) != 0)
        RP_THROW(ctx, "lchown(): %s: %s", path, strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_lchmod(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "lchmod(): first argument must be a String (path)");
    mode_t mode = rp_parse_mode_arg(ctx, 1, 0644, "lchmod");
#ifdef __linux__
    struct stat st;
    if (lstat(path, &st) != 0)
        RP_THROW(ctx, "lchmod(): %s: %s", path, strerror(errno));
    if (S_ISLNK(st.st_mode))
        RP_THROW(ctx, "lchmod(): %s: ENOSYS (symlink mode is not modifiable on Linux)", path);
    if (chmod(path, mode) != 0)
        RP_THROW(ctx, "lchmod(): %s: %s", path, strerror(errno));
#else
    if (lchmod(path, mode) != 0)
        RP_THROW(ctx, "lchmod(): %s: %s", path, strerror(errno));
#endif
    return 0;
}

static duk_ret_t duk_rp_lutimes(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "lUtimes(): first argument must be a String (path)");
    struct stat st;
    if (lstat(path, &st) != 0)
        RP_THROW(ctx, "lUtimes(): %s: %s", path, strerror(errno));
    time_t atime = st.st_atime, mtime = st.st_mtime;

    if (duk_is_object(ctx, 1) && !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1)
        && !(duk_has_prop_string(ctx, 1, "getMilliseconds") &&
             duk_has_prop_string(ctx, 1, "getUTCDay")))
    {
        if (duk_get_prop_string(ctx, 1, "setaccess"))
            atime = rp_parse_time_arg(ctx, -1, atime);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 1, "setmodify"))
            mtime = rp_parse_time_arg(ctx, -1, mtime);
        duk_pop(ctx);
    }
    else
    {
        atime = rp_parse_time_arg(ctx, 1, atime);
        mtime = rp_parse_time_arg(ctx, 2, mtime);
    }

#if defined(__linux__)
    struct timespec ts[2];
    ts[0].tv_sec = atime; ts[0].tv_nsec = 0;
    ts[1].tv_sec = mtime; ts[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW) != 0)
        RP_THROW(ctx, "lUtimes(): %s: %s", path, strerror(errno));
#else
    struct timeval tv[2];
    tv[0].tv_sec = atime; tv[0].tv_usec = 0;
    tv[1].tv_sec = mtime; tv[1].tv_usec = 0;
    if (lutimes(path, tv) != 0)
        RP_THROW(ctx, "lUtimes(): %s: %s", path, strerror(errno));
#endif
    return 0;
}

/* ==========================================================================
 * Section 2 — fopen-handle f-methods
 *
 * Methods attached to objects returned by rampart.utils.fopen().
 * They use `this` to retrieve the FILE* via getfh_nonull, then operate
 * on the underlying fd via fileno(fp).
 * ========================================================================== */

static duk_ret_t duk_rp_fh_fstat(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.fstat");
    int fd = fileno(fp);
    if (fd < 0) RP_THROW(ctx, "fh.fstat(): handle has no underlying fd");
    struct stat st;
    if (fstat(fd, &st) != 0)
        RP_THROW(ctx, "fh.fstat(): %s", strerror(errno));
    rp_push_stat_object(ctx, &st);
    return 1;
}

static duk_ret_t duk_rp_fh_fsync(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.fsync");
    fflush(fp);  /* drain stdio buffer first */
    int fd = fileno(fp);
    if (fd < 0) RP_THROW(ctx, "fh.fsync(): handle has no underlying fd");
    if (fsync(fd) != 0)
        RP_THROW(ctx, "fh.fsync(): %s", strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_fh_fdatasync(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.fdatasync");
    fflush(fp);
    int fd = fileno(fp);
    if (fd < 0) RP_THROW(ctx, "fh.fdatasync(): handle has no underlying fd");
#if defined(__APPLE__)
    /* macOS has no fdatasync(); F_FULLFSYNC is the closest equivalent */
    if (fsync(fd) != 0)
        RP_THROW(ctx, "fh.fdatasync(): %s", strerror(errno));
#else
    if (fdatasync(fd) != 0)
        RP_THROW(ctx, "fh.fdatasync(): %s", strerror(errno));
#endif
    return 0;
}

static duk_ret_t duk_rp_fh_ftruncate(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.ftruncate");
    fflush(fp);
    int fd = fileno(fp);
    if (fd < 0) RP_THROW(ctx, "fh.ftruncate(): handle has no underlying fd");
    off_t len = 0;
    if (!duk_is_undefined(ctx, 0))
    {
        double dl = duk_get_number_default(ctx, 0, 0.0);
        if (dl < 0) RP_THROW(ctx, "fh.ftruncate(): length must be non-negative");
        len = (off_t)dl;
    }
    if (ftruncate(fd, len) != 0)
        RP_THROW(ctx, "fh.ftruncate(): %s", strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_fh_fchmod(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.fchmod");
    int fd = fileno(fp);
    if (fd < 0) RP_THROW(ctx, "fh.fchmod(): handle has no underlying fd");
    mode_t m = rp_parse_mode_arg(ctx, 0, 0644, "fh.fchmod");
    if (fchmod(fd, m) != 0)
        RP_THROW(ctx, "fh.fchmod(): %s", strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_fh_fchown(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.fchown");
    int fd = fileno(fp);
    if (fd < 0) RP_THROW(ctx, "fh.fchown(): handle has no underlying fd");
    uid_t uid; gid_t gid;
    rp_parse_chown_args(ctx, 0, 1, &uid, &gid, "fh.fchown");
    if (fchown(fd, uid, gid) != 0)
        RP_THROW(ctx, "fh.fchown(): %s", strerror(errno));
    return 0;
}

/* fh.fUtimes — accepts either positional (atime, mtime) where each may
 * be a number-seconds or Date, OR an options object
 * {setaccess: <num|Date>, setmodify: <num|Date>} matching touch's style. */
static duk_ret_t duk_rp_fh_futimes(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.fUtimes");
    fflush(fp);
    int fd = fileno(fp);
    if (fd < 0) RP_THROW(ctx, "fh.fUtimes(): handle has no underlying fd");

    /* Pull current times in case caller specifies only one */
    struct stat st;
    if (fstat(fd, &st) != 0)
        RP_THROW(ctx, "fh.fUtimes(): %s", strerror(errno));

    time_t atime = st.st_atime, mtime = st.st_mtime;

    if (duk_is_object(ctx, 0) && !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0)
        && !(duk_has_prop_string(ctx, 0, "getMilliseconds") &&
             duk_has_prop_string(ctx, 0, "getUTCDay")))
    {
        if (duk_get_prop_string(ctx, 0, "setaccess"))
            atime = rp_parse_time_arg(ctx, -1, atime);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "setmodify"))
            mtime = rp_parse_time_arg(ctx, -1, mtime);
        duk_pop(ctx);
    }
    else
    {
        atime = rp_parse_time_arg(ctx, 0, atime);
        mtime = rp_parse_time_arg(ctx, 1, mtime);
    }

    struct timeval tv[2];
    tv[0].tv_sec = atime; tv[0].tv_usec = 0;
    tv[1].tv_sec = mtime; tv[1].tv_usec = 0;
    if (futimes(fd, tv) != 0)
        RP_THROW(ctx, "fh.fUtimes(): %s", strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_fh_fileno(duk_context *ctx)
{
    FILE *fp = fh_this_fp(ctx, "fh.fileNo");
    int fd = fileno(fp);
    duk_push_int(ctx, fd);
    return 1;
}

/* Helper called from inside duk_rp_fopen (rampart-utils.c) — Section 6
 * registers a forwarder, here we just expose the attachment logic. */
static void rp_fs_extras_attach_fh_methods(duk_context *ctx)
{
    /* stack top must be the handle object */
    duk_push_c_function(ctx, duk_rp_fh_fstat,     0);
    duk_put_prop_string(ctx, -2, "fstat");
    duk_push_c_function(ctx, duk_rp_fh_fsync,     0);
    duk_put_prop_string(ctx, -2, "fsync");
    duk_push_c_function(ctx, duk_rp_fh_fdatasync, 0);
    duk_put_prop_string(ctx, -2, "fdatasync");
    duk_push_c_function(ctx, duk_rp_fh_ftruncate, 1);
    duk_put_prop_string(ctx, -2, "ftruncate");
    duk_push_c_function(ctx, duk_rp_fh_fchmod,    1);
    duk_put_prop_string(ctx, -2, "fchmod");
    duk_push_c_function(ctx, duk_rp_fh_fchown,    2);
    duk_put_prop_string(ctx, -2, "fchown");
    duk_push_c_function(ctx, duk_rp_fh_futimes,   2);
    duk_put_prop_string(ctx, -2, "fUtimes");
    duk_push_c_function(ctx, duk_rp_fh_fileno,    0);
    duk_put_prop_string(ctx, -2, "fileNo");
}

/* ==========================================================================
 * Section 3 — POSIX integer-fd API
 *
 * Functions live on rampart.utils.* and take an integer fd as their
 * first argument (or path, for open).  Flag constants are exposed as
 * rampart.utils.O.{RDONLY, WRONLY, RDWR, CREAT, EXCL, TRUNC, APPEND,
 * NONBLOCK, CLOEXEC, NOFOLLOW, SYNC, DSYNC[, DIRECT, NOATIME]}.
 *
 * Distinguishes from the fopen-handle API which is stdio-buffered and
 * stateful.  Use this set when you need O_EXCL atomicity, pread/pwrite
 * positional I/O, fsync durability, libevent integration via integer
 * fds, etc.  See the design discussion in transpiler-todo / commit log.
 * ========================================================================== */

static duk_ret_t duk_rp_open(duk_context *ctx)
{
    const char *path = NULL;
    int flags = O_RDONLY;
    mode_t mode = 0644;
    int have_path = 0, have_flags = 0;

    /* Options object form: open({path, flags, mode}) */
    if (duk_is_object(ctx, 0) && !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0))
    {
        if (duk_get_prop_string(ctx, 0, "path"))
        {
            path = REQUIRE_STRING(ctx, -1, "open(): path must be a String");
            have_path = 1;
        }
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "flags"))
        {
            flags = REQUIRE_INT(ctx, -1, "open(): flags must be an integer bitmask");
            have_flags = 1;
        }
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "mode"))
        {
            mode = rp_parse_mode_arg(ctx, -1, 0644, "open");
        }
        duk_pop(ctx);
    }

    if (!have_path)
        path = REQUIRE_STRING(ctx, 0, "open(): first argument must be a String (path)");
    if (!have_flags && !duk_is_undefined(ctx, 1))
        flags = REQUIRE_INT(ctx, 1, "open(): second argument must be an integer flags bitmask");

    if (!duk_is_undefined(ctx, 2))
        mode = rp_parse_mode_arg(ctx, 2, 0644, "open");

    int fd = open(path, flags, mode);
    if (fd < 0)
        RP_THROW(ctx, "open(): %s: %s", path, strerror(errno));

    duk_push_int(ctx, fd);
    return 1;
}

static duk_ret_t duk_rp_close(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "close(): first argument must be an integer fd");
    if (close(fd) != 0)
        RP_THROW(ctx, "close(fd=%d): %s", fd, strerror(errno));
    return 0;
}

/* read(fd, length, [position]) -> Buffer
 *   - length: number of bytes to attempt
 *   - position: if given, uses pread(); otherwise uses read() at current offset
 * Returns a Buffer of the bytes actually read (may be shorter than length).
 * Returns a zero-length Buffer on EOF. */
static duk_ret_t duk_rp_fd_read(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "read(): first argument must be an integer fd");
    int len = REQUIRE_INT(ctx, 1, "read(): second argument must be a length (integer)");
    if (len < 0) RP_THROW(ctx, "read(): length must be non-negative");

    int has_pos = !duk_is_undefined(ctx, 2);
    off_t pos = has_pos ? (off_t)duk_get_number(ctx, 2) : 0;

    void *buf = duk_push_dynamic_buffer(ctx, (duk_size_t)len);
    ssize_t n;
    if (has_pos)
        n = pread(fd, buf, (size_t)len, pos);
    else
        n = read(fd, buf, (size_t)len);

    if (n < 0)
        RP_THROW(ctx, "read(fd=%d): %s", fd, strerror(errno));

    duk_resize_buffer(ctx, -1, (duk_size_t)n);
    return 1;
}

/* write(fd, data, [position]) -> int (bytes written)
 *   - data: Buffer-like or String
 *   - position: if given, uses pwrite(); otherwise write() at current offset
 * Single-syscall write — may return less than the input length. */
static duk_ret_t duk_rp_fd_write(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "write(): first argument must be an integer fd");
    const void *data = NULL;
    duk_size_t len = 0;
    if (duk_is_string(ctx, 1))
        data = duk_get_lstring(ctx, 1, &len);
    else if (duk_is_buffer_data(ctx, 1))
        data = duk_get_buffer_data(ctx, 1, &len);
    else
        RP_THROW(ctx, "write(): data must be a String or Buffer");

    int has_pos = !duk_is_undefined(ctx, 2);
    off_t pos = has_pos ? (off_t)duk_get_number(ctx, 2) : 0;

    ssize_t n;
    if (has_pos)
        n = pwrite(fd, data, (size_t)len, pos);
    else
        n = write(fd, data, (size_t)len);

    if (n < 0)
        RP_THROW(ctx, "write(fd=%d): %s", fd, strerror(errno));

    duk_push_int(ctx, (int)n);
    return 1;
}

/* lseek(fd, offset, [whence]) -> new position
 *   whence: "SEEK_SET" (default) | "SEEK_CUR" | "SEEK_END"
 *           or integer 0/1/2 */
static duk_ret_t duk_rp_lseek(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "lseek(): first argument must be an integer fd");
    off_t off = (off_t)duk_get_number(ctx, 1);
    int whence = SEEK_SET;
    if (!duk_is_undefined(ctx, 2))
    {
        if (duk_is_number(ctx, 2))
        {
            whence = duk_get_int(ctx, 2);
        }
        else if (duk_is_string(ctx, 2))
        {
            const char *w = duk_get_string(ctx, 2);
            if      (!strcasecmp(w, "SEEK_SET")) whence = SEEK_SET;
            else if (!strcasecmp(w, "SEEK_CUR")) whence = SEEK_CUR;
            else if (!strcasecmp(w, "SEEK_END")) whence = SEEK_END;
            else RP_THROW(ctx, "lseek(): invalid whence '%s'", w);
        }
    }
    off_t r = lseek(fd, off, whence);
    if (r == (off_t)-1)
        RP_THROW(ctx, "lseek(fd=%d): %s", fd, strerror(errno));
    duk_push_number(ctx, (double)r);
    return 1;
}

static duk_ret_t duk_rp_fstatFd(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "fstatFd(): first argument must be an integer fd");
    struct stat st;
    if (fstat(fd, &st) != 0)
        RP_THROW(ctx, "fstatFd(fd=%d): %s", fd, strerror(errno));
    rp_push_stat_object(ctx, &st);
    return 1;
}

static duk_ret_t duk_rp_fsyncFd(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "fsyncFd(): first argument must be an integer fd");
    if (fsync(fd) != 0)
        RP_THROW(ctx, "fsyncFd(fd=%d): %s", fd, strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_fdatasyncFd(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "fdatasyncFd(): first argument must be an integer fd");
#if defined(__APPLE__)
    if (fsync(fd) != 0)
        RP_THROW(ctx, "fdatasyncFd(fd=%d): %s", fd, strerror(errno));
#else
    if (fdatasync(fd) != 0)
        RP_THROW(ctx, "fdatasyncFd(fd=%d): %s", fd, strerror(errno));
#endif
    return 0;
}

static duk_ret_t duk_rp_ftruncateFd(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "ftruncateFd(): first argument must be an integer fd");
    off_t len = 0;
    if (!duk_is_undefined(ctx, 1))
    {
        double dl = duk_get_number_default(ctx, 1, 0.0);
        if (dl < 0) RP_THROW(ctx, "ftruncateFd(): length must be non-negative");
        len = (off_t)dl;
    }
    if (ftruncate(fd, len) != 0)
        RP_THROW(ctx, "ftruncateFd(fd=%d): %s", fd, strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_fchmodFd(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "fchmodFd(): first argument must be an integer fd");
    mode_t m = rp_parse_mode_arg(ctx, 1, 0644, "fchmodFd");
    if (fchmod(fd, m) != 0)
        RP_THROW(ctx, "fchmodFd(fd=%d): %s", fd, strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_fchownFd(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "fchownFd(): first argument must be an integer fd");
    uid_t uid; gid_t gid;
    rp_parse_chown_args(ctx, 1, 2, &uid, &gid, "fchownFd");
    if (fchown(fd, uid, gid) != 0)
        RP_THROW(ctx, "fchownFd(fd=%d): %s", fd, strerror(errno));
    return 0;
}

static duk_ret_t duk_rp_futimesFd(duk_context *ctx)
{
    int fd = REQUIRE_INT(ctx, 0, "futimesFd(): first argument must be an integer fd");
    struct stat st;
    if (fstat(fd, &st) != 0)
        RP_THROW(ctx, "futimesFd(): %s", strerror(errno));
    time_t atime = st.st_atime, mtime = st.st_mtime;

    if (duk_is_object(ctx, 1) && !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1)
        && !(duk_has_prop_string(ctx, 1, "getMilliseconds") &&
             duk_has_prop_string(ctx, 1, "getUTCDay")))
    {
        if (duk_get_prop_string(ctx, 1, "setaccess"))
            atime = rp_parse_time_arg(ctx, -1, atime);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 1, "setmodify"))
            mtime = rp_parse_time_arg(ctx, -1, mtime);
        duk_pop(ctx);
    }
    else
    {
        atime = rp_parse_time_arg(ctx, 1, atime);
        mtime = rp_parse_time_arg(ctx, 2, mtime);
    }

    struct timeval tv[2];
    tv[0].tv_sec = atime; tv[0].tv_usec = 0;
    tv[1].tv_sec = mtime; tv[1].tv_usec = 0;
    if (futimes(fd, tv) != 0)
        RP_THROW(ctx, "futimesFd(fd=%d): %s", fd, strerror(errno));
    return 0;
}

/* Helper: push the rampart.utils.O.* constants object. */
static void rp_push_open_flag_constants(duk_context *ctx)
{
    duk_push_object(ctx);

/* `&#name[2]` is the same address as `#name + 2` but written in array-
   indexing form so clang doesn't warn ("adding 'int' to a string does
   not append to the string"). Drops the "O_" prefix from the
   stringified flag name. */
#define O_CONST(name) do {                                      \
        duk_push_int(ctx, name);                                \
        duk_put_prop_string(ctx, -2, &(#name)[2]);              \
    } while(0)

    O_CONST(O_RDONLY);
    O_CONST(O_WRONLY);
    O_CONST(O_RDWR);
    O_CONST(O_CREAT);
    O_CONST(O_EXCL);
    O_CONST(O_TRUNC);
    O_CONST(O_APPEND);
    O_CONST(O_NONBLOCK);
#ifdef O_CLOEXEC
    O_CONST(O_CLOEXEC);
#endif
#ifdef O_NOFOLLOW
    O_CONST(O_NOFOLLOW);
#endif
#ifdef O_SYNC
    O_CONST(O_SYNC);
#endif
#ifdef O_DSYNC
    O_CONST(O_DSYNC);
#endif
#ifdef O_DIRECTORY
    O_CONST(O_DIRECTORY);
#endif
#ifdef O_NOCTTY
    O_CONST(O_NOCTTY);
#endif
#ifdef O_DIRECT
    O_CONST(O_DIRECT);
#endif
#ifdef O_NOATIME
    O_CONST(O_NOATIME);
#endif

#undef O_CONST

    /* SEEK_* convenience for lseek() */
    duk_push_int(ctx, SEEK_SET); duk_put_prop_string(ctx, -2, "SEEK_SET");
    duk_push_int(ctx, SEEK_CUR); duk_put_prop_string(ctx, -2, "SEEK_CUR");
    duk_push_int(ctx, SEEK_END); duk_put_prop_string(ctx, -2, "SEEK_END");
}

/* ==========================================================================
 * Section 4 — Recursive helpers
 *
 *   walkDir(dir, callback, [opts])  — generic recursive directory walker
 *   cp(src, dest, [opts])           — recursive copy
 *   rm(path, [opts])                — rm -rf
 *   glob(pattern, [opts])           — wildcard expansion
 *
 * walkDir is the foundation; cp/rm both consume it (in C, not via the JS
 * callback bridge — that would be slow).  glob does its own descent
 * because it short-circuits on non-matching directory branches.
 * ========================================================================== */

/* Internal walker shared between cp/rm and the JS walkDir wrapper.
 *
 * `cb` is called for each entry encountered as
 *    cb(arg, path, type, depth)
 * where type is one of RP_WALK_FILE / RP_WALK_DIR / RP_WALK_SYMLINK /
 * RP_WALK_OTHER, and depth counts from 0 at the entry passed in.
 *
 * Order: pre-order (callback fires on directory before its contents).
 * Returning non-zero from cb aborts the walk and propagates that value
 * upward.  Returning 0 continues.
 *
 * For rm-style post-order traversal, the caller can request post-order
 * via the `post_order` flag — directories then receive a second
 * callback with type=RP_WALK_DIR_POST after their contents are walked. */
#define RP_WALK_FILE      0
#define RP_WALK_DIR       1
#define RP_WALK_SYMLINK   2
#define RP_WALK_OTHER     3
#define RP_WALK_DIR_POST  4

typedef int (*rp_walk_cb_t)(void *arg, const char *path, int type, int depth);

static int rp_walk_recursive(const char *path, int depth, int post_order,
                              int follow_links, rp_walk_cb_t cb, void *arg)
{
    struct stat st;
    int r;
    if ((follow_links ? stat(path, &st) : lstat(path, &st)) != 0)
        return -1;

    int type = RP_WALK_OTHER;
    if      (S_ISDIR(st.st_mode))  type = RP_WALK_DIR;
    else if (S_ISLNK(st.st_mode))  type = RP_WALK_SYMLINK;
    else if (S_ISREG(st.st_mode))  type = RP_WALK_FILE;

    if (!post_order)
    {
        if ((r = cb(arg, path, type, depth)) != 0) return r;
    }

    if (type == RP_WALK_DIR)
    {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *e;
        size_t plen = strlen(path);
        while ((e = readdir(d)) != NULL)
        {
            if (e->d_name[0] == '.' && (e->d_name[1] == '\0' ||
                (e->d_name[1] == '.' && e->d_name[2] == '\0')))
                continue;
            char child[PATH_MAX];
            int needslash = (plen > 0 && path[plen-1] != '/');
            if (plen + (needslash?1:0) + strlen(e->d_name) + 1 > sizeof(child))
                continue;
            if (needslash) snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
            else snprintf(child, sizeof(child), "%s%s", path, e->d_name);

            r = rp_walk_recursive(child, depth+1, post_order, follow_links, cb, arg);
            if (r != 0) { closedir(d); return r; }
        }
        closedir(d);

        if (post_order)
        {
            if ((r = cb(arg, path, RP_WALK_DIR, depth)) != 0) return r;
        }
        else
        {
            /* In pre-order we may still want a post-marker so users can
               sync "directory done"; expose it but only if requested.
               For now, only post_order=1 mode emits RP_WALK_DIR_POST. */
            (void)0;
        }
    }
    else if (post_order)
    {
        if ((r = cb(arg, path, type, depth)) != 0) return r;
    }
    return 0;
}

/* JS bridge: walkDir(dir, callback, [opts]).
 *
 * Callback is invoked synchronously per entry as
 *    callback(path, type, depth)
 * where type is one of "file", "dir", "symlink", "other".  Returning
 * `false` from the callback aborts the walk (rampart convention).
 *
 * opts: { followLinks: bool, postOrder: bool }
 */
typedef struct rp_walkdir_jsarg_s {
    duk_context *ctx;
    duk_idx_t cb_idx;
    int aborted;
} rp_walkdir_jsarg;

static int rp_walkdir_js_cb(void *arg, const char *path, int type, int depth)
{
    rp_walkdir_jsarg *a = arg;
    duk_context *ctx = a->ctx;

    if (a->aborted) return 1;

    duk_dup(ctx, a->cb_idx);
    duk_push_string(ctx, path);
    const char *tname;
    switch (type) {
        case RP_WALK_FILE:    tname = "file";    break;
        case RP_WALK_DIR:     tname = "dir";     break;
        case RP_WALK_SYMLINK: tname = "symlink"; break;
        case RP_WALK_DIR_POST:tname = "dir";     break;
        default:              tname = "other";   break;
    }
    duk_push_string(ctx, tname);
    duk_push_int(ctx, depth);

    if (duk_pcall(ctx, 3) != 0)
    {
        /* JS threw — surface it after the walk by stashing on stack */
        a->aborted = 1;
        return 1;  /* abort */
    }
    int stop = (duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1));
    duk_pop(ctx);
    return stop ? 1 : 0;
}

static duk_ret_t duk_rp_walkdir(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "walkDir(): first argument must be a String (path)");
    if (!duk_is_function(ctx, 1))
        RP_THROW(ctx, "walkDir(): second argument must be a Function (callback)");
    int follow = 0, post = 0;
    if (duk_is_object(ctx, 2) && !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
        if (duk_get_prop_string(ctx, 2, "followLinks"))
            follow = duk_get_boolean_default(ctx, -1, 0);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 2, "postOrder"))
            post = duk_get_boolean_default(ctx, -1, 0);
        duk_pop(ctx);
    }

    rp_walkdir_jsarg a = { ctx, 1, 0 };
    int r = rp_walk_recursive(path, 0, post, follow, rp_walkdir_js_cb, &a);
    if (r < 0 && !a.aborted)
        RP_THROW(ctx, "walkDir(): %s: %s", path, strerror(errno));
    /* If aborted by JS exception during callback, re-throw isn't possible
       without preserving the error — pcall already swallowed it.  Since the
       walker reports any cb non-zero as "stop", a clean stop is indistinguishable
       from an aborted-by-throw stop.  Accept that ambiguity; users who want
       error propagation should set a flag in their callback's closure. */
    return 0;
}

/* ---- cp -------------------------------------------------------------- */

typedef struct rp_cp_arg_s {
    const char *src_root;
    const char *dst_root;
    size_t      src_root_len;
    int         dereference;
    int         preserve_ts;
    int         force;
    int         error_on_exist;
    int         err;
    char        errbuf[PATH_MAX + 128];
} rp_cp_arg;

static int rp_cp_copy_regular(rp_cp_arg *a, const char *src, const char *dst,
                               const struct stat *src_st)
{
    /* Open src, ensure parent dir of dst, copy, set mode, optionally times */
    FILE *fi = rp_fopen(src, "r");
    if (!fi) { snprintf(a->errbuf, sizeof(a->errbuf), "cp: open '%s': %s", src, strerror(errno)); return -1; }
    int dst_flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (a->error_on_exist) dst_flags |= O_EXCL;
    int fd = open(dst, dst_flags, src_st->st_mode & 0777);
    if (fd < 0)
    {
        if (errno == EEXIST && !a->force && a->error_on_exist)
            snprintf(a->errbuf, sizeof(a->errbuf), "cp: '%s' already exists", dst);
        else
            snprintf(a->errbuf, sizeof(a->errbuf), "cp: open '%s': %s", dst, strerror(errno));
        rp_fclose(fi);
        return -1;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0)
    {
        size_t written = 0;
        while (written < n)
        {
            ssize_t w = write(fd, buf + written, n - written);
            if (w < 0)
            {
                snprintf(a->errbuf, sizeof(a->errbuf), "cp: write '%s': %s", dst, strerror(errno));
                close(fd); rp_fclose(fi); return -1;
            }
            written += w;
        }
    }
    if (ferror(fi))
    {
        snprintf(a->errbuf, sizeof(a->errbuf), "cp: read '%s': %s", src, strerror(errno));
        close(fd); rp_fclose(fi); return -1;
    }
    if (a->preserve_ts)
    {
        struct timeval tv[2];
        tv[0].tv_sec = src_st->st_atime; tv[0].tv_usec = 0;
        tv[1].tv_sec = src_st->st_mtime; tv[1].tv_usec = 0;
        (void)futimes(fd, tv);
    }
    close(fd);
    rp_fclose(fi);
    return 0;
}

static int rp_cp_walk_cb(void *arg, const char *path, int type, int depth)
{
    rp_cp_arg *a = arg;
    /* Map src path -> dst path by stripping src_root prefix and prepending dst_root */
    const char *rel = path + a->src_root_len;
    while (*rel == '/') rel++;
    char dst[PATH_MAX];
    if (*rel)
        snprintf(dst, sizeof(dst), "%s/%s", a->dst_root, rel);
    else
        snprintf(dst, sizeof(dst), "%s", a->dst_root);

    struct stat st;
    int sr = a->dereference ? stat(path, &st) : lstat(path, &st);
    if (sr != 0) {
        snprintf(a->errbuf, sizeof(a->errbuf), "cp: stat '%s': %s", path, strerror(errno));
        return -1;
    }

    if (S_ISDIR(st.st_mode))
    {
        if (mkdir(dst, st.st_mode & 0777) != 0 && errno != EEXIST)
        {
            snprintf(a->errbuf, sizeof(a->errbuf), "cp: mkdir '%s': %s", dst, strerror(errno));
            return -1;
        }
    }
    else if (S_ISLNK(st.st_mode) && !a->dereference)
    {
        char target[PATH_MAX];
        ssize_t tn = readlink(path, target, sizeof(target)-1);
        if (tn < 0) {
            snprintf(a->errbuf, sizeof(a->errbuf), "cp: readlink '%s': %s", path, strerror(errno));
            return -1;
        }
        target[tn] = '\0';
        (void)unlink(dst);  /* in case it exists */
        if (symlink(target, dst) != 0) {
            snprintf(a->errbuf, sizeof(a->errbuf), "cp: symlink failed: %s", strerror(errno));
            return -1;
        }
    }
    else if (S_ISREG(st.st_mode))
    {
        if (rp_cp_copy_regular(a, path, dst, &st) != 0) return -1;
    }
    /* other types (FIFO, socket, device): silently skip */
    return 0;
}

static duk_ret_t duk_rp_cp(duk_context *ctx)
{
    const char *src = REQUIRE_STRING(ctx, 0,
        "cp(): first argument must be a String (src)");
    const char *dst = REQUIRE_STRING(ctx, 1,
        "cp(): second argument must be a String (dest)");

    int recursive = 0, dereference = 0, preserve_ts = 0,
        force = 0, error_on_exist = 0;

    if (duk_is_object(ctx, 2) && !duk_is_array(ctx, 2) && !duk_is_function(ctx, 2))
    {
#define CP_BOOL_OPT(key, var) do { \
        duk_get_prop_string(ctx, 2, key); \
        var = duk_get_boolean_default(ctx, -1, var); \
        duk_pop(ctx); \
} while(0)
        CP_BOOL_OPT("recursive",          recursive);
        CP_BOOL_OPT("dereference",        dereference);
        CP_BOOL_OPT("preserveTimestamps", preserve_ts);
        CP_BOOL_OPT("force",              force);
        CP_BOOL_OPT("errorOnExist",       error_on_exist);
#undef CP_BOOL_OPT
    }

    struct stat src_st;
    if ((dereference ? stat(src, &src_st) : lstat(src, &src_st)) != 0)
        RP_THROW(ctx, "cp(): %s: %s", src, strerror(errno));

    /* Single-file fast path */
    if (!S_ISDIR(src_st.st_mode))
    {
        if (S_ISLNK(src_st.st_mode) && !dereference)
        {
            char target[PATH_MAX];
            ssize_t tn = readlink(src, target, sizeof(target)-1);
            if (tn < 0) RP_THROW(ctx, "cp(): readlink %s: %s", src, strerror(errno));
            target[tn] = '\0';
            (void)unlink(dst);
            if (symlink(target, dst) != 0)
                RP_THROW(ctx, "cp(): symlink %s: %s", dst, strerror(errno));
            return 0;
        }
        rp_cp_arg a = { src, dst, strlen(src), dereference, preserve_ts, force, error_on_exist, 0, {0} };
        if (rp_cp_copy_regular(&a, src, dst, &src_st) != 0)
            RP_THROW(ctx, "%s", a.errbuf);
        return 0;
    }

    /* Directory copy requires recursive */
    if (!recursive)
        RP_THROW(ctx, "cp(): -r not specified; '%s' is a directory", src);

    rp_cp_arg a = { src, dst, strlen(src), dereference, preserve_ts, force, error_on_exist, 0, {0} };
    int r = rp_walk_recursive(src, 0, /*post_order=*/0, /*follow_links=*/dereference,
                               rp_cp_walk_cb, &a);
    if (r != 0)
        RP_THROW(ctx, "%s", a.errbuf[0] ? a.errbuf : "cp(): walk aborted");
    return 0;
}

/* ---- rm -------------------------------------------------------------- */

typedef struct rp_rm_arg_s {
    int  force;
    int  err;
    char errbuf[PATH_MAX + 128];
} rp_rm_arg;

static int rp_rm_walk_cb(void *arg, const char *path, int type, int depth)
{
    rp_rm_arg *a = arg;
    int r;
    if (type == RP_WALK_DIR) r = rmdir(path);
    else                     r = unlink(path);
    if (r != 0 && !(a->force && errno == ENOENT))
    {
        snprintf(a->errbuf, sizeof(a->errbuf), "rm: '%s': %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static duk_ret_t duk_rp_rm(duk_context *ctx)
{
    const char *path = REQUIRE_STRING(ctx, 0,
        "rm(): first argument must be a String (path)");
    int recursive = 0, force = 0;
    if (duk_is_object(ctx, 1) && !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1))
    {
        duk_get_prop_string(ctx, 1, "recursive");
        recursive = duk_get_boolean_default(ctx, -1, recursive); duk_pop(ctx);
        duk_get_prop_string(ctx, 1, "force");
        force = duk_get_boolean_default(ctx, -1, force); duk_pop(ctx);
    }

    struct stat st;
    if (lstat(path, &st) != 0)
    {
        if (errno == ENOENT && force) return 0;
        RP_THROW(ctx, "rm(): %s: %s", path, strerror(errno));
    }

    if (!S_ISDIR(st.st_mode))
    {
        if (unlink(path) != 0 && !(force && errno == ENOENT))
            RP_THROW(ctx, "rm(): %s: %s", path, strerror(errno));
        return 0;
    }

    if (!recursive)
        RP_THROW(ctx, "rm(): %s is a directory (recursive: true required)", path);

    rp_rm_arg a = { force, 0, {0} };
    int r = rp_walk_recursive(path, 0, /*post_order=*/1, /*follow_links=*/0,
                               rp_rm_walk_cb, &a);
    if (r != 0)
        RP_THROW(ctx, "%s", a.errbuf[0] ? a.errbuf : "rm(): walk aborted");
    return 0;
}

/* ---- glob ------------------------------------------------------------ */

/* Minimal glob pattern matcher: supports *, ?, [...], and ** (when used
 * as a path component meaning "any depth").  No brace expansion. */
static int rp_glob_match_segment(const char *pat, const char *str)
{
    while (*pat)
    {
        if (*pat == '*')
        {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            while (*str)
            {
                if (rp_glob_match_segment(pat, str)) return 1;
                str++;
            }
            return 0;
        }
        else if (*pat == '?')
        {
            if (!*str) return 0;
            pat++; str++;
        }
        else if (*pat == '[')
        {
            /* Character class [abc] or [a-z], optional negate [!...] */
            pat++;
            int neg = 0;
            if (*pat == '!') { neg = 1; pat++; }
            int matched = 0;
            while (*pat && *pat != ']')
            {
                if (pat[1] == '-' && pat[2] && pat[2] != ']')
                {
                    if (*str >= pat[0] && *str <= pat[2]) matched = 1;
                    pat += 3;
                }
                else
                {
                    if (*str == *pat) matched = 1;
                    pat++;
                }
            }
            if (*pat == ']') pat++;
            if (matched == neg) return 0;
            str++;
        }
        else
        {
            if (*pat != *str) return 0;
            pat++; str++;
        }
    }
    return *str == '\0';
}

/* Recursive descent for glob.  Splits pattern into path segments and walks
 * directories matching each segment.  ** descends any depth. */
static void rp_glob_descend(duk_context *ctx, const char *base,
                             char **segs, int seg_no, int n_segs,
                             int show_dot, int *count)
{
    if (seg_no >= n_segs)
    {
        /* base is a final match — only emit if it exists */
        struct stat st;
        if (lstat(base, &st) == 0)
        {
            duk_push_string(ctx, base);
            duk_put_prop_index(ctx, -2, (*count)++);
        }
        return;
    }

    const char *seg = segs[seg_no];

    /* ** matches zero-or-more path components */
    if (strcmp(seg, "**") == 0)
    {
        /* Try as zero components: descend with seg_no+1 */
        rp_glob_descend(ctx, base, segs, seg_no+1, n_segs, show_dot, count);
        /* And one-or-more by descending into each subdir */
        DIR *d = opendir(base);
        if (!d) return;
        struct dirent *e;
        size_t blen = strlen(base);
        while ((e = readdir(d)) != NULL)
        {
            if (e->d_name[0] == '.' && (e->d_name[1] == '\0' ||
                (e->d_name[1] == '.' && e->d_name[2] == '\0')))
                continue;
            if (!show_dot && e->d_name[0] == '.') continue;
            char child[PATH_MAX];
            int slash = (blen > 0 && base[blen-1] != '/');
            snprintf(child, sizeof(child), "%s%s%s", base, slash?"/":"", e->d_name);
            struct stat st;
            if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
                rp_glob_descend(ctx, child, segs, seg_no, n_segs, show_dot, count);
        }
        closedir(d);
        return;
    }

    /* Normal segment: open base, match each entry */
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    size_t blen = strlen(base);
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' ||
            (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        if (!show_dot && e->d_name[0] == '.') continue;
        if (rp_glob_match_segment(seg, e->d_name))
        {
            char child[PATH_MAX];
            int slash = (blen > 0 && base[blen-1] != '/');
            snprintf(child, sizeof(child), "%s%s%s", base, slash?"/":"", e->d_name);
            rp_glob_descend(ctx, child, segs, seg_no+1, n_segs, show_dot, count);
        }
    }
    closedir(d);
}

static duk_ret_t duk_rp_glob(duk_context *ctx)
{
    const char *pattern = REQUIRE_STRING(ctx, 0,
        "glob(): first argument must be a String (pattern)");
    const char *cwd = ".";
    int show_dot = 0;

    if (duk_is_object(ctx, 1) && !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1))
    {
        if (duk_get_prop_string(ctx, 1, "cwd"))
            cwd = duk_get_string_default(ctx, -1, ".");
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 1, "dot"))
            show_dot = duk_get_boolean_default(ctx, -1, 0);
        duk_pop(ctx);
    }

    /* Split pattern by '/' */
    char pbuf[PATH_MAX];
    if (strlen(pattern) >= sizeof(pbuf))
        RP_THROW(ctx, "glob(): pattern too long");
    strcpy(pbuf, pattern);

    char *segs[64];
    int n_segs = 0;
    char *base = cwd[0] ? (char*)cwd : ".";

    /* Handle absolute patterns */
    char abs_base[PATH_MAX] = "/";
    if (pbuf[0] == '/')
    {
        base = abs_base;
        char *p = pbuf + 1;
        char *tok = strtok(p, "/");
        while (tok && n_segs < 64) { segs[n_segs++] = tok; tok = strtok(NULL, "/"); }
    }
    else
    {
        char *tok = strtok(pbuf, "/");
        while (tok && n_segs < 64) { segs[n_segs++] = tok; tok = strtok(NULL, "/"); }
    }

    duk_push_array(ctx);
    int count = 0;
    rp_glob_descend(ctx, base, segs, 0, n_segs, show_dot, &count);
    return 1;
}

/* ==========================================================================
 * Section 5 — File watching
 *
 *   watch(path[, opts], callback) -> watcher
 *
 * Backends (compile-time selection, runtime fallback to polling):
 *   - inotify on Linux
 *   - polling everywhere (uses duk_rp_insert_timeout from cmdline.c so the
 *     event loop / thread base / lifecycle plumbing comes for free)
 *
 * The watcher object exposes:
 *   .close()      — stop watching, idempotent
 *   .path         — the watched path (read-only)
 *   .backend      — "inotify" | "polling"
 *
 * The callback receives a single event object:
 *   { type: "change"|"create"|"delete"|"rename", path: <string>, isDir: bool }
 *
 * Returning false from the callback closes the watcher (rampart convention).
 *
 * NOT yet implemented:
 *   - kqueue (macOS/BSD) — polling is used as fallback there
 *   - recursive watching — for now only the named path is watched
 *   - debouncing — events fire as the kernel/poller emits them
 * ========================================================================== */

#ifdef __linux__
#include <sys/inotify.h>
#define RP_HAS_INOTIFY 1
#endif

#include "event.h"

#define RP_WATCH_BACKEND_INOTIFY 1
#define RP_WATCH_BACKEND_POLL    2

typedef struct rp_watcher_s {
    duk_context  *ctx;
    void         *thisptr;          /* duk heap pointer to handle obj */
    char         *path;             /* allocated copy of watched path */
    int           closed;
    int           backend;

    /* Polling state */
    int           poll_ms;
    int           last_existed;
    time_t        last_mtime;
    off_t         last_size;
    ino_t         last_ino;
    /* For clearTimeout-style cancel of the inserted interval — we
       store the EVARGS pointer via duk_rp_insert_timeout's return
       handle on the watcher object itself, retrievable by name. */

    /* Inotify state */
    int           ifd;
    int           wd;
    struct event *e;
    int           is_dir;
} rp_watcher;

/* Forward decl */
static void rp_watcher_do_close(rp_watcher *w);

/* Push an event object onto the stack and fire the watcher's `_cb` JS
 * callback with it.  Returns 1 if the callback returned `false`
 * (meaning "stop watching"), 0 otherwise. */
static int rp_watcher_fire(rp_watcher *w, const char *type, const char *evt_path, int is_dir)
{
    duk_context *ctx = w->ctx;
    duk_idx_t top = duk_get_top(ctx);

    duk_push_heapptr(ctx, w->thisptr);
    if (!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("watch_cb")))
    {
        duk_set_top(ctx, top);
        return 0;
    }
    /* [..., handle, cb] */
    if (!duk_is_function(ctx, -1))
    {
        duk_set_top(ctx, top);
        return 0;
    }
    duk_dup(ctx, -2);   /* this = handle */
    /* event object */
    duk_push_object(ctx);
    duk_push_string(ctx, type);
    duk_put_prop_string(ctx, -2, "type");
    duk_push_string(ctx, evt_path);
    duk_put_prop_string(ctx, -2, "path");
    duk_push_boolean(ctx, is_dir);
    duk_put_prop_string(ctx, -2, "isDir");

    int stop = 0;
    if (duk_pcall_method(ctx, 1) != 0)
    {
        /* JS threw — print but don't propagate (matches forkpty
           and setTimeout error handling). */
        fprintf(stderr, "watch callback error: %s\n",
                duk_safe_to_string(ctx, -1));
    }
    else if (duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1) == 0)
    {
        stop = 1;
    }
    duk_set_top(ctx, top);
    return stop;
}

/* --- Polling backend ------------------------------------------------ */

static int rp_watcher_poll_cb(void *arg, int after)
{
    rp_watcher *w = (rp_watcher *)arg;
    if (after) return w->closed ? 0 : 1;   /* repeat unless closed */
    if (w->closed) return 0;

    struct stat st;
    int exists = (stat(w->path, &st) == 0);
    int stop = 0;

    if (!exists && w->last_existed)
    {
        stop = rp_watcher_fire(w, "delete", w->path, 0);
        w->last_existed = 0;
    }
    else if (exists && !w->last_existed)
    {
        stop = rp_watcher_fire(w, "create", w->path, S_ISDIR(st.st_mode));
        w->last_existed = 1;
        w->last_mtime = st.st_mtime;
        w->last_size  = st.st_size;
        w->last_ino   = st.st_ino;
    }
    else if (exists && w->last_existed)
    {
        if (st.st_ino != w->last_ino)
        {
            stop = rp_watcher_fire(w, "rename", w->path, S_ISDIR(st.st_mode));
            w->last_ino = st.st_ino;
            w->last_mtime = st.st_mtime;
            w->last_size  = st.st_size;
        }
        else if (st.st_mtime != w->last_mtime || st.st_size != w->last_size)
        {
            stop = rp_watcher_fire(w, "change", w->path, S_ISDIR(st.st_mode));
            w->last_mtime = st.st_mtime;
            w->last_size  = st.st_size;
        }
    }

    if (stop) rp_watcher_do_close(w);
    return 0;   /* skip the (nonexistent) pinned JS callback */
}

/* --- Inotify backend ------------------------------------------------- */

#ifdef RP_HAS_INOTIFY
static void rp_watcher_inotify_cb(evutil_socket_t fd, short events, void *arg)
{
    rp_watcher *w = (rp_watcher *)arg;
    if (w->closed) return;

    char buf[4096] __attribute__((aligned(8)));
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        char *p = buf;
        while (p < buf + n)
        {
            struct inotify_event *ie = (struct inotify_event *)p;
            const char *type = "change";
            int is_dir = (ie->mask & IN_ISDIR) ? 1 : 0;

            if (ie->mask & IN_CREATE)         type = "create";
            else if (ie->mask & IN_DELETE)    type = "delete";
            else if (ie->mask & IN_DELETE_SELF) type = "delete";
            else if (ie->mask & IN_MOVE_SELF) type = "rename";
            else if (ie->mask & IN_MOVED_FROM) type = "rename";
            else if (ie->mask & IN_MOVED_TO)  type = "rename";
            else if (ie->mask & IN_MODIFY)    type = "change";
            else if (ie->mask & IN_ATTRIB)    type = "change";

            /* For dir-watch events, ie->name is the child name */
            const char *evt_path = w->path;
            char joined[PATH_MAX];
            if (ie->len > 0 && w->is_dir)
            {
                size_t plen = strlen(w->path);
                int slash = (plen > 0 && w->path[plen-1] != '/');
                snprintf(joined, sizeof(joined), "%s%s%s",
                         w->path, slash?"/":"", ie->name);
                evt_path = joined;
            }

            int stop = rp_watcher_fire(w, type, evt_path, is_dir);
            if (stop) { rp_watcher_do_close(w); return; }
            if (ie->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED))
            {
                /* The watched object went away.  Subsequent events
                   will not arrive; user should reopen if needed. */
            }

            p += sizeof(struct inotify_event) + ie->len;
        }
    }
}
#endif

/* --- Shared lifecycle ----------------------------------------------- */

static void rp_watcher_do_close(rp_watcher *w)
{
    if (w->closed) return;
    w->closed = 1;

    if (w->backend == RP_WATCH_BACKEND_POLL)
    {
        /* Pull the EVARGS handle from the watcher object and run the
           same cleanup that clearTimeout would.  We stashed it under
           the hidden symbol "watch_evhandle" at registration. */
        duk_context *ctx = w->ctx;
        duk_idx_t top = duk_get_top(ctx);
        duk_push_heapptr(ctx, w->thisptr);
        if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("watch_evhandle")))
        {
            /* The handle has { hidden(eventargs): EVARGS*, eventId: N }.
               We invoke the existing clearTimeout helper indirectly:
               look up the global "clearTimeout" and call with the handle. */
            duk_get_global_string(ctx, "clearTimeout");
            if (duk_is_callable(ctx, -1))
            {
                duk_dup(ctx, -2);   /* the eventhandle */
                (void)duk_pcall(ctx, 1);
            }
            duk_pop(ctx);  /* clearTimeout result or err */
        }
        duk_set_top(ctx, top);
    }
#ifdef RP_HAS_INOTIFY
    else if (w->backend == RP_WATCH_BACKEND_INOTIFY)
    {
        if (w->e)
        {
            event_del(w->e);
            event_free(w->e);
            w->e = NULL;
        }
        if (w->wd >= 0 && w->ifd >= 0)
        {
            inotify_rm_watch(w->ifd, w->wd);
            w->wd = -1;
        }
        if (w->ifd >= 0)
        {
            close(w->ifd);
            w->ifd = -1;
        }
    }
#endif

    /* Remove from global stash so GC can collect the handle */
    duk_context *ctx = w->ctx;
    duk_push_global_stash(ctx);
    if (duk_get_prop_string(ctx, -1, "rp_watchers"))
    {
        duk_push_sprintf(ctx, "%p", w->thisptr);
        duk_del_prop(ctx, -2);
        duk_pop(ctx);
    }
    else duk_pop(ctx);
    duk_pop(ctx);  /* stash */
}

static duk_ret_t duk_rp_watcher_finalizer(duk_context *ctx)
{
    rp_watcher *w = NULL;
    if (duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("watcher_ptr")))
        w = duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if (w && !w->closed) rp_watcher_do_close(w);
    if (w)
    {
        free(w->path);
        free(w);
    }
    return 0;
}

static duk_ret_t duk_rp_watcher_close(duk_context *ctx)
{
    duk_push_this(ctx);
    rp_watcher *w = NULL;
    if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("watcher_ptr")))
        w = duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if (w && !w->closed) rp_watcher_do_close(w);
    return 0;
}

/* watch(path [, opts], callback) -> watcher */
static duk_ret_t duk_rp_watch(duk_context *ctx)
{
    int top = duk_get_top(ctx);
    const char *path = NULL;
    duk_idx_t cb_idx = -1;
    int poll_ms = 1000;
    int force_poll = 0;

    /* Parse arg shapes:
         (path, callback)
         (path, opts, callback)
         (opts, callback)   where opts.path required */
    if (top < 2) RP_THROW(ctx, "watch(): expects (path [, opts], callback)");

    if (duk_is_function(ctx, top-1)) cb_idx = top-1;
    else RP_THROW(ctx, "watch(): last argument must be a Function (callback)");

    if (duk_is_string(ctx, 0))
    {
        path = duk_get_string(ctx, 0);
    }
    else if (duk_is_object(ctx, 0) && !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0))
    {
        if (duk_get_prop_string(ctx, 0, "path"))
            path = REQUIRE_STRING(ctx, -1, "watch(): opts.path must be a String");
        duk_pop(ctx);
    }
    if (!path) RP_THROW(ctx, "watch(): path required");

    if (top >= 3 && duk_is_object(ctx, 1) && !duk_is_function(ctx, 1))
    {
        if (duk_get_prop_string(ctx, 1, "interval"))
            poll_ms = duk_get_int_default(ctx, -1, poll_ms);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 1, "poll"))
            force_poll = duk_get_boolean_default(ctx, -1, 0);
        duk_pop(ctx);
    }
    else if (duk_is_object(ctx, 0) && !duk_is_function(ctx, 0))
    {
        if (duk_get_prop_string(ctx, 0, "interval"))
            poll_ms = duk_get_int_default(ctx, -1, poll_ms);
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, 0, "poll"))
            force_poll = duk_get_boolean_default(ctx, -1, 0);
        duk_pop(ctx);
    }

    /* Fail-fast on missing path -- avoids silently falling back to
       polling on a path that doesn't exist (which would never fire
       useful events). */
    struct stat st;
    if (stat(path, &st) != 0)
        RP_THROW(ctx, "watch(): %s: %s", path, strerror(errno));

    /* Allocate watcher struct */
    rp_watcher *w = NULL;
    REMALLOC(w, sizeof(rp_watcher));
    memset(w, 0, sizeof(rp_watcher));
    w->ctx          = ctx;
    w->path         = strdup(path);
    w->ifd          = -1;
    w->wd           = -1;
    w->poll_ms      = poll_ms;
    w->last_existed = 1;
    w->last_mtime   = st.st_mtime;
    w->last_size    = st.st_size;
    w->last_ino     = st.st_ino;
    w->is_dir       = S_ISDIR(st.st_mode);

    /* Build the handle object */
    duk_push_object(ctx);
    duk_idx_t handle_idx = duk_get_top_index(ctx);

    duk_push_pointer(ctx, w);
    duk_put_prop_string(ctx, handle_idx, DUK_HIDDEN_SYMBOL("watcher_ptr"));

    duk_dup(ctx, cb_idx);
    duk_put_prop_string(ctx, handle_idx, DUK_HIDDEN_SYMBOL("watch_cb"));

    duk_push_string(ctx, path);
    duk_put_prop_string(ctx, handle_idx, "path");

    duk_push_c_function(ctx, duk_rp_watcher_close, 0);
    duk_put_prop_string(ctx, handle_idx, "close");

    duk_push_c_function(ctx, duk_rp_watcher_finalizer, 1);
    duk_set_finalizer(ctx, handle_idx);

    w->thisptr = duk_get_heapptr(ctx, handle_idx);

    /* Anchor in global stash so GC can't reap while libevent holds the
       ptr (forkpty pattern). */
    duk_push_global_stash(ctx);
    if (!duk_get_prop_string(ctx, -1, "rp_watchers"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, "rp_watchers");
    }
    duk_push_sprintf(ctx, "%p", w->thisptr);
    duk_dup(ctx, handle_idx);
    duk_put_prop(ctx, -3);
    duk_pop_2(ctx);

    /* Backend selection */
#ifdef RP_HAS_INOTIFY
    if (!force_poll)
    {
        w->ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (w->ifd < 0) goto try_poll;
        uint32_t mask = IN_MODIFY | IN_ATTRIB | IN_CREATE | IN_DELETE |
                        IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;
        w->wd = inotify_add_watch(w->ifd, path, mask);
        if (w->wd < 0) { close(w->ifd); w->ifd = -1; goto try_poll; }
        RPTHR *thr = get_current_thread();
        w->e = event_new(thr->base, w->ifd, EV_READ | EV_PERSIST,
                          rp_watcher_inotify_cb, w);
        if (!w->e) { close(w->ifd); w->ifd = -1; goto try_poll; }
        event_add(w->e, NULL);
        w->backend = RP_WATCH_BACKEND_INOTIFY;
        duk_push_string(ctx, "inotify");
        duk_put_prop_string(ctx, handle_idx, "backend");
        duk_set_top(ctx, handle_idx + 1);   /* leave handle as return */
        return 1;
    }
try_poll:
#endif

    /* Polling fallback — register an interval via the existing
       duk_rp_insert_timeout primitive.  Pass DUK_INVALID_INDEX for the
       JS callback (we dispatch ourselves from the before-cb). */
    w->backend = RP_WATCH_BACKEND_POLL;
    duk_push_string(ctx, "polling");
    duk_put_prop_string(ctx, handle_idx, "backend");

    /* The insert_timeout call leaves an event-handle object on the
       stack.  Save it on our handle for w.close() to invoke
       clearTimeout against. */
    duk_rp_insert_timeout(ctx, /*repeat=setInterval*/1, "watch",
                          rp_watcher_poll_cb, w,
                          DUK_INVALID_INDEX, DUK_INVALID_INDEX,
                          ((double)poll_ms) / 1000.0);
    /* Stack: [..., handle, ..., evhandle]   ->   stash evhandle */
    duk_put_prop_string(ctx, handle_idx, DUK_HIDDEN_SYMBOL("watch_evhandle"));

    duk_set_top(ctx, handle_idx + 1);   /* leave handle */
    return 1;
}

/* ==========================================================================
 * Section 7 — Compression / checksums (libdeflate)
 *
 * One-shot compress/decompress for the three deflate-family containers
 * (raw, zlib, gzip) plus crc32 / adler32 checksums.  All synchronous,
 * all throw on error.  Input may be String or Buffer; returns a Buffer
 * (or Number for the checksums).
 *
 * libdeflate is one-shot only -- no streaming.  For streaming use cases
 * a real zlib backend would be needed; libdeflate's tradeoff is being
 * substantially faster on bulk operations.
 * ========================================================================== */

#include "libdeflate.h"

/* Decompression-failure messages */
static const char *_ldef_decompress_errstr(enum libdeflate_result r)
{
    switch (r) {
        case LIBDEFLATE_SUCCESS:           return "ok";
        case LIBDEFLATE_BAD_DATA:          return "bad compressed data";
        case LIBDEFLATE_SHORT_OUTPUT:      return "decompressed data shorter than expected";
        case LIBDEFLATE_INSUFFICIENT_SPACE:return "output buffer too small";
        default:                           return "unknown libdeflate error";
    }
}

/* Get input bytes from arg 0.  Accepts string or buffer.  Sets *out and
 * *len.  Throws on bad type. */
static void _ldef_get_input(duk_context *ctx, const char *fname,
                             const void **out, duk_size_t *len)
{
    if (duk_is_string(ctx, 0)) {
        *out = duk_get_lstring(ctx, 0, len);
    } else if (duk_is_buffer_data(ctx, 0)) {
        *out = duk_get_buffer_data(ctx, 0, len);
    } else {
        RP_THROW(ctx, "%s(): data must be a String or Buffer", fname);
    }
}

/* Compress dispatch: variant 0=raw, 1=zlib, 2=gzip. */
static duk_ret_t _ldef_compress(duk_context *ctx, int variant, const char *fname)
{
    const void *in; duk_size_t in_len;
    _ldef_get_input(ctx, fname, &in, &in_len);
    int level = duk_is_number(ctx, 1) ? duk_get_int(ctx, 1) : 6;
    if (level < 1) level = 1;
    if (level > 12) level = 12;

    struct libdeflate_compressor *c = libdeflate_alloc_compressor(level);
    if (!c) RP_THROW(ctx, "%s(): out of memory allocating compressor", fname);

    size_t bound;
    switch (variant) {
        case 0: bound = libdeflate_deflate_compress_bound(c, in_len); break;
        case 1: bound = libdeflate_zlib_compress_bound(c,    in_len); break;
        case 2: bound = libdeflate_gzip_compress_bound(c,    in_len); break;
        default: libdeflate_free_compressor(c);
                 RP_THROW(ctx, "%s(): internal: bad variant", fname);
    }

    void *out = duk_push_dynamic_buffer(ctx, (duk_size_t)bound);
    size_t actual = 0;
    switch (variant) {
        case 0: actual = libdeflate_deflate_compress(c, in, in_len, out, bound); break;
        case 1: actual = libdeflate_zlib_compress(c,    in, in_len, out, bound); break;
        case 2: actual = libdeflate_gzip_compress(c,    in, in_len, out, bound); break;
    }
    libdeflate_free_compressor(c);
    if (actual == 0) {
        /* per libdeflate docs, 0 == "didn't fit" -- shouldn't happen given
         * we used the matching bound function */
        RP_THROW(ctx, "%s(): compression failed (output bound miscalc?)", fname);
    }
    duk_resize_buffer(ctx, -1, (duk_size_t)actual);
    return 1;
}

/* Decompress dispatch: variant 0=raw, 1=zlib, 2=gzip. */
static duk_ret_t _ldef_decompress(duk_context *ctx, int variant, const char *fname)
{
    const void *in; duk_size_t in_len;
    _ldef_get_input(ctx, fname, &in, &in_len);

    struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
    if (!d) RP_THROW(ctx, "%s(): out of memory allocating decompressor", fname);

    /* We don't know the output size up front for raw or zlib.  Start with
     * 4x the input and grow on INSUFFICIENT_SPACE.  Cap at 256 MB to
     * avoid runaway zip-bomb input. */
    size_t outcap = (in_len > 1024) ? in_len * 4 : 4096;
    if (outcap < 256) outcap = 256;
    size_t maxcap = 256 * 1024 * 1024;

    void *out = duk_push_dynamic_buffer(ctx, (duk_size_t)outcap);
    size_t actual = 0;
    enum libdeflate_result r;

    for (;;) {
        switch (variant) {
            case 0: r = libdeflate_deflate_decompress(d, in, in_len, out, outcap, &actual); break;
            case 1: r = libdeflate_zlib_decompress   (d, in, in_len, out, outcap, &actual); break;
            case 2: r = libdeflate_gzip_decompress   (d, in, in_len, out, outcap, &actual); break;
            default: libdeflate_free_decompressor(d);
                     RP_THROW(ctx, "%s(): internal: bad variant", fname);
        }
        if (r == LIBDEFLATE_SUCCESS) break;
        if (r != LIBDEFLATE_INSUFFICIENT_SPACE) {
            libdeflate_free_decompressor(d);
            RP_THROW(ctx, "%s(): %s", fname, _ldef_decompress_errstr(r));
        }
        if (outcap >= maxcap) {
            libdeflate_free_decompressor(d);
            RP_THROW(ctx, "%s(): decompressed output exceeds %d MB cap",
                     fname, (int)(maxcap >> 20));
        }
        outcap *= 2;
        if (outcap > maxcap) outcap = maxcap;
        out = duk_resize_buffer(ctx, -1, (duk_size_t)outcap);
    }
    libdeflate_free_decompressor(d);
    duk_resize_buffer(ctx, -1, (duk_size_t)actual);
    return 1;
}

static duk_ret_t duk_rp_gzip(duk_context *ctx)       { return _ldef_compress(ctx,   2, "gzip"); }
static duk_ret_t duk_rp_gunzip(duk_context *ctx)     { return _ldef_decompress(ctx, 2, "gunzip"); }
static duk_ret_t duk_rp_deflate(duk_context *ctx)    { return _ldef_compress(ctx,   1, "deflate"); }
static duk_ret_t duk_rp_inflate(duk_context *ctx)    { return _ldef_decompress(ctx, 1, "inflate"); }
static duk_ret_t duk_rp_deflateRaw(duk_context *ctx) { return _ldef_compress(ctx,   0, "deflateRaw"); }
static duk_ret_t duk_rp_inflateRaw(duk_context *ctx) { return _ldef_decompress(ctx, 0, "inflateRaw"); }

static duk_ret_t duk_rp_crc32(duk_context *ctx)
{
    const void *in; duk_size_t in_len;
    _ldef_get_input(ctx, "crc32", &in, &in_len);
    uint32_t seed = duk_is_number(ctx, 1) ? (uint32_t)duk_get_uint(ctx, 1) : 0;
    uint32_t result = libdeflate_crc32(seed, in, in_len);
    duk_push_uint(ctx, result);
    return 1;
}

static duk_ret_t duk_rp_adler32(duk_context *ctx)
{
    const void *in; duk_size_t in_len;
    _ldef_get_input(ctx, "adler32", &in, &in_len);
    uint32_t seed = duk_is_number(ctx, 1) ? (uint32_t)duk_get_uint(ctx, 1) : 1;
    uint32_t result = libdeflate_adler32(seed, in, in_len);
    duk_push_uint(ctx, result);
    return 1;
}

/* ==========================================================================
 * Section 6 — Registration
 *
 * Called from duk_rampart_init while the rampart.utils object is on
 * top of the stack.  Appends new functions to that object.  Adds both
 * camelCase and (where appropriate) underscored / lowercase aliases.
 * ========================================================================== */
static void rp_fs_extras_register(duk_context *ctx)
{
    /* Section 1 — path-based primitives */
    duk_push_c_function(ctx, duk_rp_readlink, 1);
    duk_put_prop_string(ctx, -2, "readLink");
    duk_push_c_function(ctx, duk_rp_readlink, 1);
    duk_put_prop_string(ctx, -2, "readlink");

    duk_push_c_function(ctx, duk_rp_truncate, 2);
    duk_put_prop_string(ctx, -2, "truncate");

    duk_push_c_function(ctx, duk_rp_statvfs, 1);
    duk_put_prop_string(ctx, -2, "statVfs");
    duk_push_c_function(ctx, duk_rp_statvfs, 1);
    duk_put_prop_string(ctx, -2, "statvfs");

    duk_push_c_function(ctx, duk_rp_writefile, 3);
    duk_put_prop_string(ctx, -2, "writeFile");

    duk_push_c_function(ctx, duk_rp_appendfile, 3);
    duk_put_prop_string(ctx, -2, "appendFile");

    duk_push_c_function(ctx, duk_rp_exists, 1);
    duk_put_prop_string(ctx, -2, "exists");

    duk_push_c_function(ctx, duk_rp_tmpdir, 0);
    duk_put_prop_string(ctx, -2, "tmpDir");
    duk_push_c_function(ctx, duk_rp_tmpdir, 0);
    duk_put_prop_string(ctx, -2, "tmpdir");

    duk_push_c_function(ctx, duk_rp_homedir, 0);
    duk_put_prop_string(ctx, -2, "homeDir");
    duk_push_c_function(ctx, duk_rp_homedir, 0);
    duk_put_prop_string(ctx, -2, "homedir");

    duk_push_c_function(ctx, duk_rp_mkdtemp, 1);
    duk_put_prop_string(ctx, -2, "mkdTemp");
    duk_push_c_function(ctx, duk_rp_mkdtemp, 1);
    duk_put_prop_string(ctx, -2, "mkdtemp");

    /* Symlink-aware variants of chown/chmod/utimes */
    duk_push_c_function(ctx, duk_rp_lchown,  3);
    duk_put_prop_string(ctx, -2, "lchown");
    duk_push_c_function(ctx, duk_rp_lchmod,  2);
    duk_put_prop_string(ctx, -2, "lchmod");
    duk_push_c_function(ctx, duk_rp_lutimes, 3);
    duk_put_prop_string(ctx, -2, "lUtimes");
    duk_push_c_function(ctx, duk_rp_lutimes, 3);
    duk_put_prop_string(ctx, -2, "lutimes");

    /* Section 3 — POSIX integer-fd API */
    duk_push_c_function(ctx, duk_rp_open,         3);
    duk_put_prop_string(ctx, -2, "open");
    duk_push_c_function(ctx, duk_rp_close,        1);
    duk_put_prop_string(ctx, -2, "close");
    duk_push_c_function(ctx, duk_rp_fd_read,      3);
    duk_put_prop_string(ctx, -2, "read");
    duk_push_c_function(ctx, duk_rp_fd_write,     3);
    duk_put_prop_string(ctx, -2, "write");
    duk_push_c_function(ctx, duk_rp_lseek,        3);
    duk_put_prop_string(ctx, -2, "lseek");

    duk_push_c_function(ctx, duk_rp_fstatFd,      1);
    duk_put_prop_string(ctx, -2, "fstatFd");
    duk_push_c_function(ctx, duk_rp_fsyncFd,      1);
    duk_put_prop_string(ctx, -2, "fsyncFd");
    duk_push_c_function(ctx, duk_rp_fdatasyncFd,  1);
    duk_put_prop_string(ctx, -2, "fdatasyncFd");
    duk_push_c_function(ctx, duk_rp_ftruncateFd,  2);
    duk_put_prop_string(ctx, -2, "ftruncateFd");
    duk_push_c_function(ctx, duk_rp_fchmodFd,     2);
    duk_put_prop_string(ctx, -2, "fchmodFd");
    duk_push_c_function(ctx, duk_rp_fchownFd,     3);
    duk_put_prop_string(ctx, -2, "fchownFd");
    duk_push_c_function(ctx, duk_rp_futimesFd,    3);
    duk_put_prop_string(ctx, -2, "futimesFd");

    /* rampart.utils.O.* open(2) flag constants + SEEK_* for lseek */
    rp_push_open_flag_constants(ctx);
    duk_put_prop_string(ctx, -2, "O");

    /* Section 4 — recursive helpers */
    duk_push_c_function(ctx, duk_rp_walkdir,  3);
    duk_put_prop_string(ctx, -2, "walkDir");
    duk_push_c_function(ctx, duk_rp_walkdir,  3);
    duk_put_prop_string(ctx, -2, "walkdir");
    duk_push_c_function(ctx, duk_rp_cp,       3);
    duk_put_prop_string(ctx, -2, "cp");
    duk_push_c_function(ctx, duk_rp_rm,       2);
    duk_put_prop_string(ctx, -2, "rm");
    duk_push_c_function(ctx, duk_rp_glob,     2);
    duk_put_prop_string(ctx, -2, "glob");

    /* Section 5 — file watching */
    duk_push_c_function(ctx, duk_rp_watch,    DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "watch");

    /* Section 7 — compression (libdeflate) */
    duk_push_c_function(ctx, duk_rp_gzip,        2);
    duk_put_prop_string(ctx, -2, "gzip");
    duk_push_c_function(ctx, duk_rp_gunzip,      1);
    duk_put_prop_string(ctx, -2, "gunzip");
    duk_push_c_function(ctx, duk_rp_deflate,     2);
    duk_put_prop_string(ctx, -2, "deflate");
    duk_push_c_function(ctx, duk_rp_inflate,     1);
    duk_put_prop_string(ctx, -2, "inflate");
    duk_push_c_function(ctx, duk_rp_deflateRaw,  2);
    duk_put_prop_string(ctx, -2, "deflateRaw");
    duk_push_c_function(ctx, duk_rp_inflateRaw,  1);
    duk_put_prop_string(ctx, -2, "inflateRaw");
    duk_push_c_function(ctx, duk_rp_crc32,       2);
    duk_put_prop_string(ctx, -2, "crc32");
    duk_push_c_function(ctx, duk_rp_adler32,     2);
    duk_put_prop_string(ctx, -2, "adler32");
}
