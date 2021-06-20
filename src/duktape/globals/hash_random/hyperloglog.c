//
//  hyperloglog.c
//  hyperloglog
//
//  Created by P B Richards on 6/11/20.
//  Copyright © 2020 P. B. Richards. All rights reserved.
//

#include "hyperloglog.h"
#include "cityhash.h"
#include <math.h>


/* 
HyperLogLog Comparison Report  

I use two sets of 250,000,000 unique strings.
The second set is the first set with each character xored with 0x4F.

Redis HyperLogLog: 500 million PFADDs and 174975 PFCOUNTs

2017 macbook pro
real	285m9.557s
user	47m8.424s
sys	77m27.823s

(500e6+174975)/((285*60)+9.5) == 29,234 HLLs/second

mean error set 1:  0.51 % 
mean error set 2:  0.91 %
max error  set 1:  2.59 %
max error  set 2:  2.25 %
errors >=1% set1:  8.69 %
errors >=1% set2: 44.98 %
errors >=1% both: 26.84 %

median error is   0.69%

Rampart HyperLogLog:  500 million PFADDs and 174975 PFCOUNTs:

2017 macbook pro
real	3m37.261s
user	3m34.125s
sys	0m1.960s

(500e6+174975)/((3*60)+37.261) == 2,302,200 HLLs/sec

mean error set1:  0.40 %
mean error set2:  0.37 %
max error set 1:  1.50 %
max error set 2:  1.81 %
error >=1% set1:  1.62 %
error >=1% set2:  4.10%
error >=1% both:  2.9 %

median error is   0.35%    

So this stuff 79 times faster with notably less error.
However, the speed is an unfair comparison because I was
testing Redis with RESP protocol overhead. I'm certain
Redis' HLL is much faster internally.

*/

// https://pdfs.semanticscholar.org/75ba/51ffd9d2bed8a65029c9340d058f587059da.pdf
// https://blog.usejournal.com/hyperloglog-count-once-count-fast-48727765ffc9
// http://antirez.com/news/75
// https://storage.googleapis.com/pub-tools-public-publication-data/pdf/40671.pdf
// https://engineering.fb.com/data-infrastructure/hyperloglog/


// provides an estimate of the number of unique items added to the HLL
double
countHLL16k(byte *buckets)
{
  
  double E=0.0;      // the sum - estimate
  int    nEmpty=0;   // number of empty buckets
  int    i;
   
   for(i=0;i<16384;i++)
   {
      if(!buckets[i])
          ++nEmpty;
      E+=1.0/(double)(1ULL<<buckets[i]);
   }

   if(nEmpty>1246)  // if linear counting is less insane than HLL
   {
     E=-16384.0*log2((double)nEmpty/16384.0);
     E*= 0.6931471806;
     if(E<37000)
        return(E);
   }
   
   E=268107776.0/E; // 16,384^2 / E
   
   // Note: I suspect nEmpty could be used to derive a better
   //       alpha across its range for HLL to use. I ran out of time.
   
   if(nEmpty>715)              //  alpha compensation for low values
         E*=0.703988504668440;
   else
         E*=0.719343555299421; // CityHash specific

  return(E);
}


/* 
   The method used here is different from Google, FB, and Antirez's.
   Where buckets == 16384, the least significant 14 bits are used
   to derive the index into the buckets. Then, successive 1's are
   counted from bit 15 and up. This should be a little faster on
   all the processors we wish to support.
*/

void
addHLL16K(byte *buckets,uint64_t val)
{
  uint64_t bit=1;
  int    n=1;
  int    bucket=0;
   
   for(bit=1;bit<16384;bit<<=1) // get bucket index
   {
      bucket<<=1;
      bucket|=val&1;
      val>>=1;
   }

   while(val&1) // count sequential 1s from lsb
   {
      ++n;
      val>>=1;
   }
   
   if(n>buckets[bucket])
      buckets[bucket]=n;
}

// merge two HLLs. destination may be the same as "a"
void
mergeHLL16K(byte *destination,byte *a,byte *b)
{
   int i;
   
   for(i=0;i<16384;i++)
      destination[i]=a[i]>b[i]?a[i]:b[i];
}
