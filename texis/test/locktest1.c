#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/wait.h>
#include "texisapi.h"

void
usage(char *programName)
{
  printf("Usage: %s DATABASE\n", programName);
}

int child(char *db, long pid)
{
  TEXIS *tx;
  long i, ll;

  ll = sizeof(long);
  tx = texis_open(db, "PUBLIC", "");
  if(!tx)
    return 1;
  texis_prepare(tx, "insert into locktest values(?, ?, 0, '');");
  texis_param(tx, 1, &pid, &ll, SQL_C_LONG, SQL_INTEGER);
  for (i = 0; i < 100; i++) {
    texis_param(tx, 2, &i, &ll, SQL_C_LONG, SQL_INTEGER);
    texis_execute(tx);
    texis_flush(tx);
  }
  return 0;
}

#define MAXKIDS 8
int children[MAXKIDS];

int startchildren(char *db, int nkids)
{
    int i, newpid, ret;

    if(nkids > MAXKIDS) return -1;
    for(i = 0; i < MAXKIDS; i++) {
      children[i] = 0;
    }

    for (i = 0; i < nkids; i++) {
      newpid = fork();
      if(newpid == 0) {
        ret = child(db, getpid());
        exit(ret);
      } else {
        children[i] = newpid;
      }
    }
}

int countchildren(void)
{
  int i, n = 0;

  for(i = 0; i < MAXKIDS; i++) {
    if(children[i]) n++;
  }
  return n;
}
int waitchildren(void)
{
  int nchildren, child;
  int i, status;

  while(countchildren()) {
    child = wait(&status);
    for(i = 0; i < MAXKIDS; i++) {
      if(children[i] == child) {
        children[i] = 0;
      }
    }
  }
}
int
main(int argc, char *argv[])
{
  TEXIS *tx;
  FLDLST *fl;
  int argnum;
  int didheader=0;
  int nkids = 4;
  TXCOUNTINFO txci;

  if(argc < 2)
  {
    usage(basename(argv[0]));
    return(1);
  }
  if(argc > 2) {
    nkids = atoi(argv[2]);
  }
  tx = texis_open(argv[1], "PUBLIC", "");
  if(!tx)
    return 1;
  texis_prepare(tx, "create table locktest(pid int, rownum int, value int, text varchar(20));");
  texis_execute(tx);
  texis_flush(tx);

  startchildren(argv[1], nkids);
  waitchildren();

  texis_prepare(tx, "select * from locktest;");
  texis_execute(tx);
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
  texis_prepare(tx, "drop table locktest;");
  texis_execute(tx);
  texis_flush(tx);
  tx = texis_close(tx);
  return 0;
}
