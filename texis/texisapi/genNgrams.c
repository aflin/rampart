#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "texint.h"
/* Prints a TXNGRAMSET generated from a given file, as a static const C
 * structure.  Used to generate cooked training data for identifylanguage().
 */

/* ------------------------------------------------------------------------ */

static void
usage(const char *progName)
{
  printf("Usage: %s action [args ...]\n", progName);
  printf("Actions:\n");
  printf("  genNgramSet N file.txt prefix\n");
  exit(TXEXIT_INCORRECTUSAGE);
}

int
main(int argc, char *argv[])
{
#ifdef _WIN32
__try
{
#endif /* _WIN32 */
  TXNGRAMSET    *ngramset = NULL;
  TXEXIT        res;

  tx_setgenericsigs();
  if ((res = TXinitapp(TXPMBUFPN, "genNgrams", argc, argv, NULL, NULL)) > 0)
    exit(res);

  if (argc < 2) usage(argv[0]);
  if (strcmpi(argv[1], "genNgramSet") == 0)
    {
      int               ngramSz;
      TXNGRAM           *curNgram, *ngramsEnd;

      if (argc != 5) usage(argv[0]);
      ngramSz = atoi(argv[2]);
      ngramset = TXngramsetOpenFromFile(TXPMBUFPN, ngramSz, argv[3]);
      if (!ngramset) exit(TXEXIT_UNKNOWNERROR);

      /* Dump the N-gram set: */
      ngramsEnd = ngramset->ngrams + ngramset->numNgrams;
      printf("/* Do not edit: this file generated by %s */\n\n", argv[0]);
      printf("static const TXNGRAM    %sNgrams[%d] =\n",
             argv[4], (int)ngramset->numNgrams);
      printf("{\n");
      for (curNgram = ngramset->ngrams; curNgram < ngramsEnd; curNgram++)
        {
          char          litBuf[256];
          const char    *s = curNgram->text;
          size_t        litLen;

          litLen = TXstrToCLiteral(litBuf, sizeof(litBuf), &s,
                                   ngramset->ngramSz);
          litBuf[litLen] = '\0';
          printf("  { %6d, \"%s\" },\n", (int)curNgram->count, litBuf);
        }
      printf("};\n");
      printf("\n");
      printf("static const TXNGRAMSET %sNgramset =\n", argv[4]);
      printf("{\n");
      printf("  TXPMBUFPN,\n");                         /* pmbuf */
      printf("  %d,\n", (int)ngramset->ngramSz);        /* ngramSz */
      printf("  (TXNGRAM *)%sNgrams,\n", argv[4]);                 /* ngrams */
      printf("  %d,\n", (int)ngramset->numNgrams);      /* numNgrams */
      printf("  NULL,\n");                              /* btree */
      printf("  %1.*lf,\n", (int)(EPI_OS_DOUBLE_MANTISSA_BITS/3),
             ngramset->sqrtSumCountsSquared);
      printf("};\n");

      ngramset = TXngramsetClose(ngramset);
    }
  else
    usage(argv[0]);
  return(TXEXIT_OK);
#ifdef _WIN32
    }
  __except(TXgenericExceptionHandler(_exception_code(), _exception_info()))
    {
      /* TXgenericExceptionHandler() exits */
    }
#endif /* _WIN32 */
}
