#ifndef murmurhash_h
#define murmurhash_h

#include <stdio.h>

/*
 *  MurmurHash.h
 *  MYUtilities
 *
 *  This file created by Jens Alfke on 3/17/08.
 *  Algorithm & source code by Austin Appleby, released to public domain.
 *  <http://murmurhash.googlepages.com/>
 *
 */

#include <stdint.h>
#include <sys/types.h>

/** An extremely efficient general-purpose hash function.
    Murmurhash is claimed to be more than twice as fast as the nearest competitor,
    and to offer better-distributed output with fewer collisions.
    It is, however not suitable for cryptographic use.
    Hash values will differ between bit- and little-endian CPUs, so they shouldn't
    be stored persistently or transmitted over the network.
 
    Written by Austin Appleby: <http://murmurhash.googlepages.com/> */

#define  MURMURSEED 0xFEEDCEED

uint64_t MurmurHash64( const void * key, int len);


#endif /* murmurhash_h */
