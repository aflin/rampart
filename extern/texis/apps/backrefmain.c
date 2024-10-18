//#include "texint.h"

/* This prototype is in texint.h, but don't need all the extra stuff */
int BRmain (int argc, char *argv[], int argcstrip, char **argvstrip);

int
main(int argc, char **argv)
{
  int largc;
  char **largv;

//  TXinitapp(NULL, NULL, argc, argv, &largc, &largv);
  return BRmain(argc, argv, argc, argv);
}
