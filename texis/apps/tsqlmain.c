#include "texint.h"

int
main(int argc, char **argv)
{
  int largc;
  char **largv;
  int i, rc;

  TXinitapp(NULL, NULL, argc, argv, &largc, &largv);
  rc = TXtsqlmain(argc, argv, largc, largv);
  TXcloseapp();
  if(largv) {
    for(i = 0; i < largc; i++) {
      if(largv[i]) {
        largv[i] = TXfree(largv[i]);
      }
    }
    largv = TXfree(largv);
  }
  return rc;
}
