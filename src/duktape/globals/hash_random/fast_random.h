//
//  fast_random.h
//  fast_random
//
//  Created by P B Richards on 6/19/21.
//

#ifndef fast_random_h
#define fast_random_h

// very fast, fairly good uniform random number generator
uint64_t xorRand64(void);

// seed or re-seed xorRand64.
// If seed is 0 it will reset it to default val to replicate number order
void xorRand64Seed(uint64_t seed);

// returns a random value from  min to max inclusive of min and max
int64_t randomRange(int64_t min,int64_t max);

#endif /* fast_random_h */
