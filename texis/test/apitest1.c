#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include "texisapi.h"

void
usage(char *programName)
{
  printf("Usage: %s DATABASE SQL [PARAM]...\n", programName);
  printf("Run SQL against DATABASE\n");
  printf("\n");
  printf("Examples\n");
  printf("%s db \"SELECT * FROM SYSCOLUMNS;\'\n", programName);
  printf("%s db \"SELECT * FROM SYSCOLUMNS WHERE TBNAME = ?;\" SYSTABLES\n", programName);
}

int
main(int argc, char *argv[])
{
  TEXIS *tx;
  FLDLST *fl;
  int argnum;
  int didheader=0;
  TXCOUNTINFO txci;

  if(argc < 3)
  {
    usage(basename(argv[0]));
    return(1);
  }
  tx = texis_open(argv[1], "PUBLIC", "");
  if(!tx)
    return 1;
  texis_set(tx, "hexifybytes", "on"); /* Print varbyte as hex string */
  texis_prepare(tx, argv[2]);
  for(argnum = 1; argnum < (argc-2); argnum++)
  {
    char *Param = argv[argnum+2];
    long  ParamLen = strlen(Param);

    texis_param(tx, argnum, Param, &ParamLen, SQL_C_CHAR, SQL_VARCHAR);
  }
  texis_execute(tx);
  #ifdef SHOW_INDEXCOUNT
  texis_getCountInfo(tx, &txci);
  printf("Matches: %ld - %ld\n", txci.rowsMatchedMin, txci.rowsMatchedMax);
  printf("Return: %ld - %ld\n", txci.rowsReturnedMin, txci.rowsReturnedMax);
  printf("Indexcount: %ld\n", txci.indexCount);
  #endif
  while ((fl = texis_fetch(tx, 0)))
  {
    int i;
    if(!didheader)
    {
      for (i = 0; i < fl->n; i++)
      {
  /*      printf("[%d] Type: %d Size: %d\n", i, fl->type[i], fl->ndata[i]); */
        if(fl->type[i] == 66 || fl->type[i] == 194)
          printf("%*s\t", fl->ndata[i], fl->name[i]);
      }
      printf("\n");
      didheader++;
    }
    for (i = 0; i < fl->n; i++)
    {
/*      printf("[%d] Type: %d Size: %d\n", i, fl->type[i], fl->ndata[i]); */
      if(fl->type[i] == 66 || fl->type[i] == 194)
        printf("%*s\t", fl->ndata[i], (char *)fl->data[i]);
    }
    printf("\n");
  }
  #ifdef SHOW_INDEXCOUNT
  texis_getCountInfo(tx, &txci);
  printf("Matches: %ld - %ld\n", txci.rowsMatchedMin, txci.rowsMatchedMax);
  printf("Return: %ld - %ld\n", txci.rowsReturnedMin, txci.rowsReturnedMax);
  printf("Indexcount: %ld\n", txci.indexCount);
  #endif
  tx = texis_close(tx);
  return 0;
}
