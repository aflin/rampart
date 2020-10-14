#define TALKREDIS 1 // remove this to talk to Ramis

//
//  resp_protocol.h
//  resp
//
//  Created by P. B. Richards on 4/10/20.
//  Copyright © 2020 P. B. Richards. All rights reserved.
//

#ifndef resp_protocol_h
#define resp_protocol_h
#include <stdio.h>

// You'll have to change this in order to run the code generators
#define RESPCODEDIR "/Users/Cube/Src/rampart/rampart"

#ifndef byte
#define byte uint8_t
#endif

#define INITIALRESPITEMS 10   // the preallocated number of RESPITEMS in a RESPPROTO
#define RESPITEMSGROWTH 2     // if we run out what factor to grow by
#define RESPNESTEDARRAYMAX 10 // how deeply nested can arrays be in RESP

#define RESPISNULL 0
#define RESPISFLOAT 1
#define RESPISINT 2
#define RESPISARRAY 3
#define RESPISBULKSTR 4
#define RESPISSTR 5
#define RESPISPLAINTXT 6 // plaintext is turned into an array
#define RESPISERRORMSG 7

#define RESPNULL "$-1\r\n"

#define RESPITEM struct respItemStruct
RESPITEM
{
  union // holds the possible values being parsed
  {
    double rfloat; // floats are not in the Redis RESP Protocol but RamPart will have them
    int64_t rinteger;
    size_t length;   // for bulk strings
    uint64_t nItems; // for arrays
  };
  byte *loc;        // where is the data located
  uint8_t respType; // which of the RESPIS* types is this
};

#define RESPROTO struct respProtocolStruct
RESPROTO
{
  RESPITEM *items;                        // array of RESPITEMS from the input buffer
  int nItems;                             // how many are there in the above
  RESPITEM *outItems;                     // reply from server
  int nOutItems;                          // how many items in reply
  int maxItems;                           // how many are allocated
  byte *currPointer;                      // where are we in the buffer so far
  byte *buf;                              // the caller provided buffer
  byte *bufEnd;                           // end of the buffer so far
  char *errorMsg;                         // NULL if all's ok
  uint32_t arrayNest[RESPNESTEDARRAYMAX]; // keep track of how remaining items are needed for array
  uint8_t arrayDepth;                     // how deeply are we in a nested array
  byte isServer;                          // flag to indicate if this is parsing for server or client
};

#define RESP_PARSE_INCOMPLETE 0    // more data needed to complete object
#define RESP_PARSE_COMPLETE 1      // it has a complete object and no extra data
#define RESP_PARSE_COMPLETE_TAIL 2 // it has a complete object and extra data
#define RESP_PARSE_ERROR -1        // parser failure

// create a new parser, tell it if you're in client or server mode
RESPROTO *newResProto(int isServer);

// destructor for above
RESPROTO *freeRespProto(RESPROTO *rpp); // destructor

// parses the buffer returns 1 if complete , 0 if incomplete, -1 on error
int parseResProto(RESPROTO *rpp, byte *buf, size_t bufSize, int newBuffer);

// resets the parser to new state except it does not free allocated items list
void resetResProto(RESPROTO *rpp);

// sends the output of a command in RESP to FILE *
int respSendReply(RESPROTO *rpp, FILE *);

// sees if it's a legitimate command and if so provides info about it
const struct rp_redisCommandsStruct *
rp_redisFindCommand(register const char *str, register unsigned int len);

// RESP encodes parameters in a printf kind of way and outputs them to fh
int respPrintf(RESPROTO *rpp, FILE *fh, char *fmt, ...);

// counts the number of items in the resp encoding printf
int respPrintfItems(char *s);

// call this to expand a buffer while in the middle of parsing RESP (usually coming from server)
byte *respBufRealloc(RESPROTO *rp, byte *oldBuffer, size_t newSize);

#endif /* resp_protocol_h */
