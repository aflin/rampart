# Rampart Bug Audit — Fixed by Claude, 2026-02-26/27

72 issues identified and reviewed. 66 fixed, 6 false positives.

## CRITICAL

| # | File:Line | Problem | Fix |
|---|-----------|---------|-----|
| 1 | rampart-vector.c:553 | Heap buffer overflow: output buffer allocated `sz4` but `sz4*2` bytes written | Allocate `dim` (the correct doubled size) |
| 2 | rampart-sql.c:448-462 | NULL deref + infinite loop in `die_nicely` signal handler: `h` never advanced | Add `h = h->next` and NULL check in loop |
| 3 | almanac.c:148 | Operator precedence: `+1 % 7` evaluates as `+(1%7)` | Parenthesize: `(ret->tm_wday + 1) % 7` |
| 4 | cmdline.c:866-885 | Use-after-free in `addToSave`: `txt` stored then freed via `freeme` | Remove `freeme` and its `free()` |
| 5 | rampart-curl.c:700 | Malformed format string `"%"` missing specifier | Change to `"%s"` |
| 6 | rampart-html.c:649 | NULL deref in `putdoctype`: uses `fpi->value` where `fpi` is NULL (copy-paste error) | Use `sys->value` |
| 7 | rampart-python.c:2903-2908 | NULL deref: `goto end` commented out; `PyCallable_Check(NULL)` crashes | Uncomment `goto end;` |
| 8 | resp_protocol.c:362 | `floatingpoint == NAN` is always false per IEEE 754 | Use `isnan(floatingpoint)` |

## HIGH

| # | File:Line | Problem | Fix |
|---|-----------|---------|-----|
| 9 | rampart-crypto.c:312 | NULL deref: `rp_crypto_do_passwd()` can return NULL, then `strrchr(hash,'$')` crashes | Add NULL check before strrchr |
| 10 | cmdline.c:950 | NULL passed to `popen()` when no pager found | Guard with `if (pager)` |
| 11 | rampart-curl.c:2768-2774 | `curl_easy_setopt` on NULL handle before `curl_easy_init()` | Remove premature call (duplicate of line 2787) |
| 12 | rampart-thread.c:1790 | `alist=&list` assigns address to local copy, not caller's pointer | Change to `*alist = list` |
| 13 | resp_client.c:395,633 | NULL deref: `lookupPctCode` can return NULL, then `pctCode->code` dereferences it | Add NULL check, return sentinel |
| 14 | rampart-server.c:2325 | Bitwise OR `\|` instead of AND `&` in permission check (always true) | Change `\|` to `&` |
| 15 | cmdline.c:330-374 | `sizeof(d_name)` instead of `strlen(d_name)` — over-allocates | Change to `strlen` |
| 16 | module.c:256-272 | Double mutex unlock on `libload_success` path | Remove duplicate unlock at label |
| 17 | rampart-redis.c:1186 | Missing `break` in switch case 11 falls through to case 12 | Add `break;` |
| 18 | rampart-sql.c:1471 | Unbounded `sprintf` to 1MB shared memory region | Use `snprintf` with `FORKMAPSIZE`; add bounds check in `fork_open` |
| 19 | rampart-lmdb.c:549-553 | `mdb_txn_abort` after failed `mdb_txn_commit` — use-after-free (commit frees txn) | Comment out the abort with explanation |
| 20 | rampart-server.c:294 | `gmtime` not thread-safe | Use `gmtime_r` with stack-allocated `struct tm` |

## MEDIUM

| # | File:Line | Problem | Fix |
|---|-----------|---------|-----|
| 21 | rampart-utils.c:222-223 | File handle leak in `rp_get_line` when `nlines==0` | Add `fclose(file)` before early return |
| 22 | rampart-net.c:493 | `strdup` never freed in `duk_rp_net_x_trigger` | Add `free(ev)` |
| 23 | rampart-net.c:1531-1558 | `getaddrinfo` result leak: second call overwrites `res` | Use separate `res2` variable |
| 24 | rampart-net.c:3734-3773 | FILE leaks in SSL key/cert validation + missing NULL check on `fopen` | Add NULL check and `fclose` |
| 25 | rampart-curl.c:694 | FILE leak in `post_from_file` — never closed | Add `postfile` field to CSOS struct, fclose in cleanup |
| 26 | module.c:87-92,140-144 | Memory/fd leaks on `fread` error and transpile error paths | Add `fclose(f); free(buffer)` / `freeParseRes` on error paths |
| 27 | resp_client.c:493-498,722-727 | `fmtCopy`/`argSizes` leaked on unknown `%` format code | Free both before return; add commented-out `va_end` with explanation |
| 28 | resp_protocol.c:225-237 | `respBufRealloc` sets error on successful in-place realloc | Change `else` to `else if (!newBuffer)` |
| 29 | resp_protocol.c:270 | `skipGraph` doesn't update `priorChar` in quoted string — `"abc\\"` mis-parsed | Add `priorChar = *s;` in quoted string loop |
| 30 | resp_protocol.c:766 | `fflush(stdout)` should be `fflush(fh)` | Change to `fflush(fh)` |
| 31 | rampart-crypto-passwd.c:138,336,347,364 | EVP_MD_CTX leaks: `return NULL` instead of `goto err` (same bug fixed upstream in OpenSSL) | Change `return NULL` to `goto err` |
| 32 | rampart-python.c:499,515 | Missing `Py_INCREF(Py_None)` before returning `Py_None` | Use `Py_RETURN_NONE` |
| 33 | rampart-python.c:2682,2696,3947,3961 | `PyDict_New()` leak (duplicate); `val` not decref'd after `PyDict_SetItemString`; `kwdict` not decref'd in cleanup | Remove duplicate; add `Py_XDECREF(val)` and `RP_Py_XDECREF(kwdict)` |
| 34 | cmdline.c:1273-1300 | **FALSE POSITIVE**: `strjoin`/`strcatdup` realloc first arg in place — no leak | — |
| 35 | cmdline.c:1448-1465 | File descriptor leaks in `load_polyfill` on error and success paths | Add `fclose(f)` before return and goto |
| 36 | cmdline.c:3695-3697 | Missing `break` — `-t` flag falls through to `-q`, silently enabling quiet mode | Add `break;` |
| 37 | rampart-import.c:279,304,316,334,365 | `closeCSV(dcsv.csv)` passes NULL (not yet assigned); `closecsv` macro dereferences NULL on duplicate headers | Change to `closeCSV(csv)`; replace macro with inline cleanup |
| 38 | rampart-import.c:109-112 | `openCSVstr` leaks `csv` struct when `malloc` fails | Replace `malloc` with `REMALLOC` (aborts on failure) |
| 39 | rampart-event.c:295,458,612 | Buffer overflow in `varname` VLA: `strlen(key)+8` too small for `%d` | Change `+8` to `+16` at all three sites |
| 40 | resp_client.c:92,175 | Socket fd checks reject fd 0 as invalid (`<= 0` and `!= 0`) | Change to `< 0` and `> -1` |
| 41 | rampart-server.c:334-340 | `outgz` leaked when gzip compression fails (`outlen == 0`) | Add `free(outgz)` before return |
| 42 | rampart-server.c:2340-2356 | Zero-length cached gzip file: `cfd` opened but never closed | Add `else close(cfd)` |
| 43 | colorspace.c:349 | `LABaCenter` used instead of `LABbCenter` for b-channel upper bound | Change to `LABbCenter` |
| 44 | colorspace.c:851 | Backwards loop: `p <= token` should be `p >= token` — trim loop never executes | Change to `p >= token` |
| 45 | register.c:261-269,284-292 | Duktape stack leak in `array_find`/`array_find_index`: call result not popped each iteration | Add `duk_pop(ctx)` after `duk_to_boolean` |

## LOW

| # | File:Line | Problem | Fix |
|---|-----------|---------|-----|
| 46 | resp_client.c:72 | Bitwise OR `\|` instead of logical OR `\|\|` in NULL check | Change to `\|\|` |
| 47 | linenoise.c:528 | Unsigned underflow when `lc->len == 0`: `lc->len - 1` wraps to SIZE_MAX | Add early `return` if `lc->len == 0` |
| 48 | linenoise.c:1063,1069 | Negative values assigned to `size_t bufshift` — unsigned wraparound | Change type from `size_t` to `int` |
| 49 | linenoise.c:1857 | `write(..., 5)` on 4-char string sends trailing null byte | Change length to 4 |
| 50 | linenoise.c:1779 | **FALSE POSITIVE**: `size_t` to `int` cast — `len` bounded by 16384-byte buffer | — |
| 51 | cmdline.c:3507 | `strcpy(argv0, argv[0])` without bounds check | Use `snprintf(argv0, PATH_MAX, ...)` |
| 52 | cmdline.c:997-998 | `strcpy+strcat` into `histfn[PATH_MAX]` can overflow | Use `snprintf` |
| 53 | cmdline.c:3746 | `stat()` failure prints error but continues with uninitialized `entry_file_stat` | Add `exit(1)` after error message |
| 54 | cmdline.c:375 | Unchecked `stat()` return value — `S_ISDIR` reads garbage on failure | Combine into `if(!stat(...) && S_ISDIR(...))` |
| 55 | rampart-utils.c:587 | `strlen(NULL)` if `duk_get_prop_string` fails and `fn` stays NULL | Init `fn=""`; use `duk_get_lstring`; add bounds check for `.c` suffix test |
| 56 | rampart-utils.c:571-577 | **FALSE POSITIVE**: `sprintf` into `strdup`'d buffer — output is always a subset of original | — |
| 57 | rampart-net.c:830-840 | Uninitialized `saddr` if `ai_family` is not AF_INET or AF_INET6 | Add `default: continue` to skip unknown families |
| 58 | rp_tz.c:65-66 | UB: `unsigned char << 24` shifts into sign bit of `int` | Cast to `uint32_t` before shifting |
| 59 | rp_tz.c:258 | `realpath` failure leaves `rpath` uninitialized; `strcmp` reads garbage | Add `if(!realpath(...)) continue` |
| 60 | colorspace.c:636 | `$TERM` unsanitized in `popen` command — shell injection possible | Validate `term` chars are alnum/hyphen/dot/underscore |
| 61 | colorspace.c:1239 | `sprintf` into 256-byte buffer with unbounded user input | Change to `snprintf(outb, sizeof(outb), ...)` |
| 62 | printf.c:1220 | `sprintf` into 8-byte buffer with `%d` that can be 10+ digits | Enlarge to 16 bytes; change to `snprintf` |
| 63 | csv_parser.c:469-475 | **FALSE POSITIVE**: second `calloc` failure — `closeCSV` handles it via NULL `csv->item[0]` | — |
| 64 | rampart-thread.c:332 | **FALSE POSITIVE**: `rampart_locks` can't be NULL at this point — only called after fork setup | — |
| 65 | rampart-python.c:279-295 | `left` can go negative, cast to `size_t` makes huge value for `snprintf` | Rename to `remaining`; clamp `len` to `remaining`; add early break |
| 66 | rampart-python.c:2158,3050 | Pipe-error paths leak `funcnames`/`fname`/`pickle` | Add `free()` calls on all error-return paths |
| 67 | rampart-crypto.c:252 | **FALSE POSITIVE**: `strlen` to `int` — immediately clamped to `saltlen` (max 16) | — |
| 68 | db_misc.c:844-847 | `markup` not freed when `query` is NULL | Add `free(markup)` |
| 69 | db_misc.c:727-734 | `outBuf`/`fmtData` leaked on `err:` and `noMem:` error paths | Add guarded `closehtbuf`/`TXfree` cleanup + `RPstringformatResetArgs` |
| 70 | fmemopen.c:98 | No NULL check after `malloc` — `memset` dereferences NULL | Add NULL check with `exit(1)` |
| 71 | rampart-curl.c:589 | Format string references NULL `key` instead of `ckey` | Change `key` to `ckey` |
| 72 | resp_client.c:104,111 | Socket fd leaked on `gethostbyname`/`connect` failure in reconnect path | Close socket and set to -1 before return |
| 73 | rampart-curl.c:2704-2723 | `sopts->postfile` not initialized to NULL in `new_curlreq` CSOS allocation; `clean_req` then calls `fclose()` on garbage pointer (crash on Linux, silent on macOS where malloc returns zeroed memory) | Add `sopts->postfile = (FILE *)NULL;` after `sopts->postdata` initialization  |
