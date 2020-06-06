//
//  ramis.h
//  rampart
//
//  Created by P. B. Richards on 4/24/20.
//  Copyright © 2020 P. B. Richards. All rights reserved.
//

#ifndef ramis_h
#define ramis_h
#include <stdint.h>
#include <float.h> 

#define RAMISOK   1 // functions that return succsess or failure use these
#define RAMISFAIL 0

#ifndef  FLT_DECIMAL_DIG
# define FLT_DECIMAL_DIG 9
#endif
#ifndef  DBL_DECIMAL_DIG
# define DBL_DECIMAL_DIG 17
#endif



typedef double RFLOAT; 


#define RAMIS struct ramisStruct
RAMIS
{
  int nothing;
};

// DO NOT ALTER THIS STRUCT UNDER PENALTY OF FUBAR!!!!!
#define RAMISCMD struct ramisCommandStruct
struct ramisCommandStruct
{
   char *command;         // the command string
   char *syntax;          // help syntax
   char *desc;            // help description
   int (* handler)(RAMIS *r); // that which does the work
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

#define ramisMalloc(x)     malloc((x))
#define ramisRealloc(x,sz) realloc((x),(sz))
#define ramisCalloc(x,sz)  calloc((x),(sz))
#define ramisFree(x)       free((x))


#endif /* ramis_h */
