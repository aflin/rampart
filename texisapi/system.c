#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "dbquery.h"
#include "texint.h"


int
TXsystem(CONST char *cmd)
/* Returns -1 on error, else exit code of process.
 */
{
  char          **env = CHARPPN;
  int           ret, codeOrSig, isSig;
  TXPOPENARGS   po;
  TXPIPEARGS    pa;
  char          *tmpArgv[10];

  TXPOPENARGS_INIT(&po);
  TXPIPEARGS_INIT(&pa);

#ifdef _WIN32
  /* TXpopenduplex() will merely concatenate its argv array, so no
   * need to split it up here.  But make sure to leave off
   * TXPDF_QUOTEARGS so no quotes are added:
   */
  tmpArgv[0] = (char *)cmd;
  tmpArgv[1] = CHARPN;
#else /* !_WIN32 */
  tmpArgv[0] = "/bin/sh";
  tmpArgv[1] = "-c";
  tmpArgv[2] = (char *)cmd;
  tmpArgv[3] = CHARPN;
#endif /* !_WIN32 */
  po.argv = tmpArgv;
  po.cmd = po.argv[0];
  po.fh[STDIN_FILENO] = TXFHANDLE_STDIN;
  po.fh[STDOUT_FILENO] = TXFHANDLE_STDOUT;
  po.fh[STDERR_FILENO] = TXFHANDLE_STDERR;
  if ((env = tx_mksafeenv(0)) == CHARPPN) goto err;
  po.envp = env;
  /* do not use TXPDF_QUOTEARGS: `cmdline' is probably already quoted
   * ok for Windows single-argv use, and for Unix /bin/sh
   */

  if (!TXpopenduplex(&po, &pa)) goto err;
  TXpendio(&pa, 1);
  if (!TXpgetexitcode(&pa, 1, &codeOrSig, &isSig)) goto err;
  ret = codeOrSig;
  goto done;

err:
  ret = -1;
done:
  TXpcloseduplex(&pa, 1);
  env = TXfree(env);
  return(ret);
}
