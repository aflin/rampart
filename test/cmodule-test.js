
rampart.globalize(rampart.utils);

/* Check that a C compiler is available before running tests */
var _cc = rampart.buildCC || 'cc';
if (!stat(_cc)) {
    fprintf(stderr, "Could not find C compiler (%s)!! SKIPPING CMODULE TESTS\n", _cc);
    process.exit(0);
}

function testFeature(name,test,error)
{
    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }
    printf("testing %-60s - ", name);
    if(test)
        printf("passed\n")
    else
    {
        printf(">>>>> FAILED <<<<<\n");
        if(error) printf('%J\n',error);
        process.exit(1);
    }
    if(error) console.log(error);
}


var cmodule = require('rampart-cmodule.js');

var name = "countPrimes";

var func =  ```
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

{
    int64_t N = REQUIRE_UINT64(ctx, 0, "First arg must be a positive integer");
    int64_t root = (int64_t)floor(sqrt((double)N));
    int64_t *pr = NULL; int pc = 0;
    small_primes(root, &pr, &pc);

    // Number of ODDS per segment (tunable). The span in integers is 2*SEG_ODDS.
    const int64_t SEG_ODDS = 1<<20; // 1,048,576 odds per segment
    const size_t  WORDS    = (size_t)((SEG_ODDS + 63) / 64);
    uint64_t *bits = malloc(WORDS * sizeof(uint64_t));
    if (!bits) { fprintf(stderr, "oom\n"); return 1; }

    int64_t count = (N >= 2); // account for prime 2

    // low/high walk over odd numbers only; segment span in integers is 2*SEG_ODDS
    for (int64_t low = 3; low <= N; low += 2*SEG_ODDS) {
        int64_t high = low + 2*SEG_ODDS - 2; // inclusive, odd
        if (high > N) high = (N | 1);
        int seg_len_odds = (int)((high - low)/2 + 1); // <= SEG_ODDS

        // clear only the portion we’ll use this iteration
        size_t words_used = (size_t)((seg_len_odds + 63) / 64);
        memset(bits, 0, words_used * sizeof(uint64_t));

        for (int i = 0; i < pc; ++i) {
            int64_t p = pr[i];
            if (p == 2) continue;
            int64_t p2 = p * p;

            int64_t start;
            if (p2 >= low) {
                start = p2;
            } else {
                int64_t r = (low % p + p) % p;
                start = (r == 0) ? low : low + (p - r);
            }
            if ((start & 1) == 0) start += p; // make odd
            if (start < low) {
                int64_t step = 2*p;
                int64_t delta = ((low - start + step - 1) / step) * step;
                start += delta;
            }
            for (int64_t x = start; x <= high; x += 2*p) {
                int idx = (int)((x - low) >> 1);
                BITSET(bits, idx);
            }
        }

        // count unset bits (primes)
        int i = 0;
        while (i + 64 <= seg_len_odds) {
            uint64_t w = bits[i >> 6];
            count += 64 - __builtin_popcountll(w);
            i += 64;
        }
        for (; i < seg_len_odds; ++i) if (!BITGET(bits, i)) ++count;
    }

    duk_push_number(ctx, (double)count);
    
    return 1;
}

```;


var supportFuncs = ```
#define BITGET(A,i) ((A[(i)>>6] >> ((i)&63)) & 1ULL)
#define BITSET(A,i) (A[(i)>>6] |= (1ULL << ((i)&63)))

static void small_primes(int64_t limit, int64_t **pr, int *cnt) {
    int64_t n = limit + 1;
    char *is = calloc(n, 1);
    int c = 0;
    for (int64_t i = 2; i * i <= limit; ++i)
        if (!is[i]) for (int64_t j = i*i; j <= limit; j += i) is[j] = 1;
    for (int64_t i = 2; i <= limit; ++i) if (!is[i]) ++c;
    *pr = malloc(c * sizeof(int64_t));
    int k = 0; for (int64_t i = 2; i <= limit; ++i) if (!is[i]) (*pr)[k++] = i;
    *cnt = c; free(is);
}
```;

var extraFlags="-O3 -std=c99";

var libs = "-lm"


// Segmented prime counter in ES5 (bit-packed: 1 bit per odd)
// Usage (Node): node -e "console.log(countPrimes(100000000))"

  var POP = (function () {
    var t = new Array(256);
    for (var i = 0; i < 256; ++i) {
      var x = i, c = 0;
      while (x) { x &= (x - 1); ++c; }
      t[i] = c;
    }
    return t;
  })();

  function popcount32(u) {
    // u is assumed 32-bit unsigned
    return POP[u & 255] +
           POP[(u >>> 8) & 255] +
           POP[(u >>> 16) & 255] +
           POP[(u >>> 24) & 255];
  }

  // Simple sieve to get primes up to 'limit'
  function smallPrimes(limit) {
    var n = limit + 1;
    var is = new Array(n);
    for (var i = 0; i < n; ++i) is[i] = false;

    for (var p = 2; p * p <= limit; ++p) {
      if (!is[p]) {
        for (var j = p * p; j <= limit; j += p) is[j] = true;
      }
    }
    var arr = [];
    for (var k = 2; k <= limit; ++k) if (!is[k]) arr.push(k);
    return arr;
  }

  // Set bit at index i (0-based) in bitset
  function bitsetSet(bits, i) {
    bits[i >>> 5] |= (1 << (i & 31));
  }
  // Get bit at index i (0-based) in bitset (returns 0/1)
  function bitsetGet(bits, i) {
    return (bits[i >>> 5] >>> (i & 31)) & 1;
  }

  // Clear first 'wordsUsed' 32-bit words to zero
  function clearWords(bits, wordsUsed) {
    for (var i = 0; i < wordsUsed; ++i) bits[i] = 0;
  }

  // Count zeros among first 'lenBits' bits in bitset
  function countZeroBits(bits, lenBits) {
    var fullWords = (lenBits >>> 5);
    var remBits = (lenBits & 31);
    var cnt = 0;

    for (var i = 0; i < fullWords; ++i) {
      var w = bits[i] >>> 0;
      cnt += 32 - popcount32(w);
    }
    if (remBits) {
      var last = bits[fullWords] >>> 0;
      // mask off bits beyond lenBits
      var mask = (remBits === 32) ? 0xFFFFFFFF : ((1 << remBits) - 1);
      last &= mask;
      cnt += remBits - popcount32(last);
    }
    return cnt;
  }

  // Main: count primes ≤ N
  function JcountPrimes(N, SEG_ODDS) {
    if (typeof SEG_ODDS === 'undefined') SEG_ODDS = 1 << 20; // odds per segment
    if (N < 2) return 0;

    var root = Math.floor(Math.sqrt(N));
    var primes = smallPrimes(root);

    // Allocate bitset for one segment of SEG_ODDS odds
    var words = ((SEG_ODDS + 31) >>> 5);
    var bits;
    if (typeof Uint32Array !== 'undefined') bits = new Uint32Array(words);
    else {
      bits = new Array(words);
      for (var i = 0; i < words; ++i) bits[i] = 0;
    }

    var count = (N >= 2) ? 1 : 0; // prime 2

    // Sweep segments over odd numbers only
    // Segment covers integers [low..high], both odd, span = 2*SEG_ODDS - 2
    for (var low = 3; low <= N; low += 2 * SEG_ODDS) {
      var high = low + 2 * SEG_ODDS - 2;
      if (high > N) high = (N | 1);
      var seg_len_odds = (((high - low) >> 1) + 1); // number of odds in segment
      var wordsUsed = ((seg_len_odds + 31) >>> 5);
      clearWords(bits, wordsUsed);

      // mark composites in this segment
      for (var pi = 0; pi < primes.length; ++pi) {
        var p = primes[pi];
        if (p === 2) continue;
        var p2 = p * p;
        var start;

        if (p2 >= low) {
          start = p2;
        } else {
          var r = low % p;
          if (r < 0) r += p;
          start = (r === 0) ? low : (low + (p - r));
        }
        if ((start & 1) === 0) start += p; // make odd

        if (start < low) {
          var step = 2 * p;
          var delta = Math.floor((low - start + step - 1) / step) * step;
          start += delta;
        }
        // mark each odd multiple: x = start, start+2p, ...
        for (var x = start; x <= high; x += 2 * p) {
          var idx = (x - low) >> 1;      // 0..seg_len_odds-1
          bitsetSet(bits, idx);
        }
      }

      // count odds that remain unmarked (primes)
      count += countZeroBits(bits, seg_len_odds);
    }

    return count;
  }

var countPrimes;
try {
    countPrimes = cmodule(name, func, supportFuncs, extraFlags, libs);
    printf("testing %-60s - passed\n", "cmodule compile");
} catch(e) {
    fprintf(stderr, "C module compilation failed. SKIPPING CMODULE TESTS\n");
    process.exit(0);
}

var s=performance.now();
var ccount = countPrimes(1000000);
var e=performance.now();
var ctime = e-s;
var ctimestr = rampart.utils.sprintf("c (%.1fms)", ctime);

var s=performance.now();
var jscount = JcountPrimes(1000000);
var e=performance.now();
var jstime = e-s;
var jstimestr = rampart.utils.sprintf("js (%.1fms)", jstime);

testFeature("cmodule - results of countPrimes, jsRes ==  cRes", function(){
    return jscount == ccount;
});

testFeature(rampart.utils.sprintf("cmodule timing %s < %s", ctimestr, jstimestr), function(){
    return ctime < jstime;
});
