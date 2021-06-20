HyperLogLog is a very fast and efficient way to count large numbers of
distinct things.  For example if you wanted to know the number of distinct
IP addresses in a web log you might do the following:

cut -f1 -d' ' <web.log | sort | uniq | wc

But this method consumes both a large amount of memory as well as
significant CPU time.  HLL consumes only 16K of ram to do the same thing and
operates in linear time.  The other advantage of HLL is that it can be used
in a stream.  I.E.  adding new items like an accumulator while always being
able to quickly give you a count of the number of distinct things that have
been added.

HLL does not provide a precise value, it provides a fairly accurate
estimate.  Rampart's HLL function has a median estimate error of 0.35%
Ramparts HLL tested to be significantly and consistently more accurate than
the implementations of Redis, Google, and Facebook.  Rampart HLL is tuned to
use Google's CityHash function.  We empirically found that it tended to
produce better hashes for use with the HLL algorithm than the more popular
Mumurhash algorithm.

 
Usage:

int main(int argc, const char * argv[])
{	
   byte HLLbuckets[16384]={0}; 
   char buf[256];
   
   while(fgets(buf,256,stdin))
     addHLL16K(HLLbuckets,CityHash64(buf,(size_t)strlen(buf)));
   

   printf("N distinct ~=%lf\n",countHLL16k(HLLbuckets)); 
  

   exit(0);
}

The mergeHLL16K() function takes two HLL bucket buffers and merges them so
that you can produce a common count.  For example if you had two web servers
and wanted to know how many distinct IP addresses had been seen by them
collectively.  To save memory it can merge both buffers into the first one.

