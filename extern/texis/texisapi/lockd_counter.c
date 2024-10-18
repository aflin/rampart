#include <stdio.h>
#include <string.h>
#include "lockd_counter.h"

lockd_counter GlobalCounter = {0,0};

int
counter_cmp(lockd_counter *a, lockd_counter *b)
{
  int rc;

  rc = a->date - b->date;

  if (rc == 0)
  {
    rc = a->seq - b->seq;
  }
  if(rc > 0) {
    return 1;
  } else if(rc < 0) {
    return -1;
  }
  return rc;
}

int
counter_get(lockd_counter *res)
{
  time_t now = time(NULL);

  if(GlobalCounter.date == now) {
    GlobalCounter.seq++;
  } else {
    GlobalCounter.date = now;
    GlobalCounter.seq = 0;
  }
  if(res) {
    *res = GlobalCounter;
  }
  return 0;
}

size_t
counter_tostring(lockd_counter *counter, lockd_counter_format format, char *buffer, size_t len)
{
  size_t desired_len;
  char tmpbuf[50];

  switch(format) {
    case COUNTER_HEX64:
      if(counter->date >= 0x100000000) {
        break;
      }
      /* FALLTHRU */
    case COUNTER_HEX32:
#if TX_FT_COUNTER_DATE_BITS > 32
      if(counter->date >= 0x100000000) {
        if(buffer && len > 0) *buffer = '\0';
        return 0;
      }
#endif
      sprintf(tmpbuf, "%08lx%lx", counter->date, counter->seq);
      desired_len = strlen(tmpbuf) + 1;
      if(len >= desired_len) {
        strcpy(buffer, tmpbuf);
      } else {
        *buffer = '\0';
      }
      return strlen(buffer);
  }
  return -1;
}
