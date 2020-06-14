/*
 * This program is copyright Alec Muffett 1991 except for some portions of
 * code in "crack-fcrypt.c" which are copyright Robert Baldwin, Icarus Sparry
 * and Alec Muffett.  The author(s) disclaims all responsibility or liability
 * with respect to it's usage or its effect upon hardware or computer
 * systems, and maintain copyright as set out in the "LICENCE" document which
 * accompanies distributions of Crack v4.0 and upwards.
 */

#undef DEVELOPMENT_VERSION

/*
 * define this symbol if you are on a system where you don't have the
 * strchr() function in your standard library (usually this means you are on
 * a BSD based system with no System 5isms) but you DO have the equivalent
 * index() function.
 */

#undef INDEX_NOT_STRCHR

/*
 * define this if you have a smart toupper() and tolower() (a-la ANSI), which
 * do not generate barf when something which is not a lowercase letter is
 * uppercased, or vice-versa (a-la K&R). Check your manpage or leave it
 * undefined
 */

#undef FAST_TOCASE

/*
 * define this if you are on a Sys V type system with a uname() system call
 * AND YOU HAVE NO gethostname() - it fakes up a BSD gethostname() so you can
 * use CRACK_NETWORK; see crack-port.c
 */

#undef CRACK_UNAME

/*
 * define CRACK_DOTFILES if you want to search the first 1Kb segment of users
 * .plan/.project/.signature files for potential passwords.
 *
 * define CRACK_DOTSANE to likewise do (possibly non-portable) sanity testing
 * on the dotfiles before opening them (check that they are not named pipes,
 * etc...)
 */

#undef CRACK_DOTFILES
#undef CRACK_DOTSANE

/*
 * define "COMPRESSION" if you have enabled compression in the Crack
 * Shellscript
 *
 * this is enabled by default if you have /usr/ucb/compress; change the pathname
 * of the $compress variable in the Crack script if you use another pathname
 * to get to "compress" and then put the path name of the "pipe to stdout"
 * version of the compression prog here.
 */

#define COMPRESSION
#define ZCAT    "/usr/ucb/zcat" /* as in "zcat Dicts/bigdict.Z" */
#define PCAT    "/usr/bin/pcat"

/*
 * define this if you are using fcrypt() - you might not want to if fcrypt()
 * doesn't work properly
 */

#undef  FCRYPT
#define LITTLE_ENDIAN
#define FDES_4BYTE
