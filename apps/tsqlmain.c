#include "texint.h"

int
main(int argc, char **argv)
{
  int largc;
  char **largv;

  TXinitapp(NULL, NULL, argc, argv, &largc, &largv);
  return TXtsqlmain(argc, argv, largc, largv);
}
