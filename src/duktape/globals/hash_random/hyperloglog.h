//
//  hyperloglog.h
//  hyperloglog
//
//  Created by P B Richards on 6/11/20.
//  Copyright © 2020 P. B. Richards. All rights reserved.
//

#ifndef hyperloglog_h
#define hyperloglog_h

#include <stdio.h>
#include <stdint.h>

#ifndef byte
#  define byte uint8_t
#endif




// These functions require a buffer of size 16384 initialized to 0 at start
#define newHLL16K() calloc(16384,1)

#define freeHLL16K(x) (x)?free((x)):NULL

// Provides an estimate of the number of unique items added to the HLL
double countHLL16k(byte *buckets);

// Add a new has to the HLL buckets.
// HLL16K is tuned for CityHash64, but other 64 bit hashes will work.
void addHLL16K(byte *buckets,uint64_t val);

// Merge two HLLs. destination may be the same as "a"
void mergeHLL16K(byte *destination,byte *a,byte *b);

#endif /* hyperloglog_h */
