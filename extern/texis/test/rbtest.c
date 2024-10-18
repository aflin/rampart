#include "texint.h"
#include "ringbuffer.h"

int
main()
{
  int items[] = {1,2,3,4,5,6,7,8,9,10};
  int i, *it, rc;
  TXRingBuffer *rb = TXRingBuffer_Create(5);
  int exitcode = 0;

  TXRingBuffer_Put(rb, &items[0]);
  it = TXRingBuffer_Get(rb);
  if(it && (*it == 1)) {
    printf ("Put And Get 1 Success\n");
  } else {
    printf ("Put And Get 1 Failure\n");
    exitcode = 1;
  }

  for(i = 1; i < 7; i++)
  {
    rc = TXRingBuffer_Put(rb, &items[i]);
    printf("Put[%d] %d = %d\n", i, items[i], rc);
    if(i < 6 && rc != i) exitcode = 1;
    if(i == 6 && rc != -1) exitcode = 1;
  }
  for(i = 1; i < 7; i++)
  {
    printf("Get[%d] = ", i);
    it = TXRingBuffer_Get(rb);
    if(it) {
      printf("%d\n", *it);
      if(*it != (i+1)) exitcode = 1;
    } else {
      printf("null\n");
      if(i != 6) exitcode = 1;
    }
  }
  return exitcode;
}
