#ifndef LOCKD_COUNTER_H
#define LOCKD_COUNTER_H

#include <time.h>

typedef struct lockd_counter {
    time_t date;
    unsigned long seq;
} lockd_counter;

typedef enum lockd_counter_format {
  COUNTER_HEX32,
  COUNTER_HEX64
} lockd_counter_format;

int counter_get(lockd_counter *res);
size_t counter_tostring(lockd_counter *counter, lockd_counter_format format, char *buffer, size_t len);

#endif /* LOCKD_COUNTER_H */
