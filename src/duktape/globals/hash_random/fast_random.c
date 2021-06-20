//
//  fast_random.c
//
//  Quickly generate uniform distribution random numbers
//  Do not use for lottery tickets or encryption!
//
//  Created by P B Richards on 6/18/21.
//  This code is in the public domain

#include <stdio.h>
#include <inttypes.h>
#include "fast_random.h"

// very fast, fairly good uniform random number generator
// see wikipedia for XorShift RNG

#define default_seed 0xFEE1BADCEEDBA11L
static uint64_t xorRand64Val=default_seed;

uint64_t
xorRand64()
{
    xorRand64Val ^= xorRand64Val << 13;
    xorRand64Val ^= xorRand64Val >> 7;
    xorRand64Val ^= xorRand64Val << 17;
    return(xorRand64Val);
}

// seed or re-seed xorRand64.
// If seed is 0 it will reset it to default val to replicate number order
void
xorRand64Seed(uint64_t seed)
{
   if(!seed)
      xorRand64Val=default_seed;
   else
      xorRand64Val=seed;
}

// returns a random value from  min to max inclusive of min and max
int64_t
randomRange(int64_t min,int64_t max)
{
  return((xorRand64() % (max - min + 1 ))+min);
}


#ifdef TESTRNG

#define NTRIALS  1000000000L
#define NBUCKETS 100

long buckets[NBUCKETS]={0,};

int main(int argc, const char * argv[])
{
   long i;
   // simple distribution check
   for(i=0;i<NTRIALS;i++)
      buckets[randomRange(0,NBUCKETS-1)]++;
   
   // print deviation from equal distribution
   for(i=0;i<NBUCKETS;i++)
      printf("% 8.4f %3ld\n",1.0-((double)buckets[i]/(double)NTRIALS*(double)NBUCKETS),i);
      
   return 0;
}

// how to print an int64_t: printf("%" PRId64 "\n", randomRange(0,9));

#endif // TESTRNG
