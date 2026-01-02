//
//  rampart-redis.h
//  rampart
//
//  Created by P. B. Richards on 4/24/20.
//  Copyright © 2026 P. B. Richards. All rights reserved.
//

#ifndef rampart_redis_h
#define rampart_redis_h
#include <stdint.h>
#include <float.h> 

#define RP_REDISOK   1 // functions that return succsess or failure use these
#define RP_REDISFAIL 0

#ifndef  FLT_DECIMAL_DIG
# define FLT_DECIMAL_DIG 9
#endif
#ifndef  DBL_DECIMAL_DIG
# define DBL_DECIMAL_DIG 17
#endif



typedef double RFLOAT; 


#define RP_REDIS struct rp_redisStruct
RP_REDIS
{
  int nothing;
};

// DO NOT ALTER THIS STRUCT UNDER PENALTY OF FUBAR!!!!!
#define RP_REDISCMD struct rp_redisCommandStruct
struct rp_redisCommandStruct
{
   char *command;         // the command string
   char *syntax;          // help syntax
   char *desc;            // help description
   int (* handler)(RP_REDIS *r); // that which does the work
   uint64_t flags;         // bitwise flags and ACLs
   uint64_t invocations;   // how many times has it been called
   int8_t  commandClass;   // what class of operation is it
   int8_t  spaceCommand;   // indicates where the idiotic second word of the command is 0 if none
   int8_t  minArgs;        // minimum number of arguments
   int8_t  optionalArgs;   // does this command contain optional arguments
   int8_t  isVarArg;       // does it have a variable number of args
   int8_t  arity;          // Redis' count of elements in a command, -3 means >=3
   int8_t  firstKey;       // index of the first element that is a key
   int8_t  lastKey;        // index of the last element that is a key
   int8_t  keyStep;        // step size to get all the keys
   uint8_t isImplemented;  // is this command functional yet
};

#define TCMD struct tempCmdStruct
TCMD
{
  char *group;
  char *command;
  char *args;
  char *desc;
  int  varArgs;
  int  minArgs;
  int  optionalArgs;
  int  firstKeyIndex;
  int  lastKeyIndex;
  int  keyStep;
  int  arity;
  uint64_t flags;
  TCMD *next;
};

#define rp_redisMalloc(x)     malloc((x))
#define rp_redisRealloc(x,sz) realloc((x),(sz))
#define rp_redisCalloc(x,sz)  calloc((x),(sz))
#define rp_redisFree(x)       free((x))


#endif /* rampart_redis_h */
