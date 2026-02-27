

//
//  resp_protocol.c
//  resp
//
//  Created by P. B. Richards 4/10/20.
//  Copyright © 2026 P. B. Richards. All rights reserved.
//
//
// This stuff should handle the Redis RESP 2.0 Protocol
//
// One major difference is that this handler includes floating point. The way
// it differentiates between an integer and a floating point value is if it
// sees a decimal point '.' within a numeric field
//

//#define NEWCOMMAND 1 // uncomment to regenerate skeleton code and headers

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include "rampart-redis.h"
#include "resp_protocol.h"
#ifndef NEWCOMMAND
#endif

// destructor
RESPROTO *
freeRespProto(RESPROTO *rpp)
{
  if (rpp)
  {
    if (rpp->items)
      rp_redisFree(rpp->items);
    rp_redisFree(rpp);
  }
  return (NULL);
}

// create a new RESP parser object and init it
RESPROTO *
newResProto(int isServer)
{
  RESPROTO *rpp = rp_redisCalloc(1, sizeof(RESPROTO));
  if (rpp)
  {
    rpp->items = rp_redisCalloc(INITIALRESPITEMS, sizeof(RESPITEM));

    if (!rpp->items)
      return (freeRespProto(rpp));
    rpp->maxItems = INITIALRESPITEMS;
    rpp->isServer = isServer;
    return (rpp);
  }
  return (NULL);
}

// resets the parser to its "new" state except it does not free allocated items list
// (The implication is that what ever size it became if it grew it will stay that size)
void resetResProto(RESPROTO *rpp)
{
  if (rpp)
  {
    memset(rpp->items, 0, rpp->maxItems * sizeof(RESPITEM)); // zero ALL for security reasons
    rpp->nItems = 0;
    rpp->buf = NULL;
    rpp->bufEnd = NULL;
    rpp->errorMsg = NULL;
  }
}

// looks at what's supposed to be a number and converts it into *pfloat or *pint
// returns pointer to end of number on success, returns NULL if failure
// *pfloat will be set to Nan if it's an integer
// The integer portion of a floating point value will be set correctly
static byte *
parseRespNumber(RESPROTO *rp, byte *s, double *pfloat, int64_t *pint)
{

  byte *p = s;
  byte *endOfInt = NULL;
  byte *endOfFloat = NULL;
  byte *end;

  *pint = (int64_t)strtoll((char *)p, (char **)&endOfInt, 10); // cast because of long long def

  if (endOfInt == NULL || endOfInt == p) // the int part should have parsed even if it was a floating pt val
  {
    *pfloat = NAN;
    rp->errorMsg = ("RESP unreconizable integer in numeric field");
    return (NULL);
  }

  *pfloat = strtod((char *)p, (char **)&endOfFloat);

  if (endOfFloat == endOfInt) // a floating point string encoding the same value must be longer
  {
    *pfloat = NAN;
    end = endOfInt;
  }
  else
    end = endOfFloat;

  if (end == NULL)
  {
    rp->errorMsg = "RESP unreconizable numeric value in field";
    return (NULL);
  }

  return (end);
}

// is it entirely a number
int isItNumeric(byte *s)
{
  byte *numberEnd;
  double f = strtod((char *)s, (char **)&numberEnd);
  if (numberEnd == NULL || (f == 0.0 && numberEnd == s))
    return (0);
  if (*numberEnd != '\0')
    return (0);
  return (1);
}

// Finds end of a RESP line, '\0' terminates it if present
// returns NULL if not present (indicating partial buffer)
// returns a pointer to the next byte after the line if success
static byte *
isThereEOL(byte *s, byte *end)
{
  int sawCR = 0;
  while (s < end)
  {
    if (*s == '\0') // null terminated from a prior parse
    {
      while (s < end && (*s == '\0' || *s == '\x0a'))
        ++s;
      return (s);
    }
    if (*s == '\r')
    {
      if (s + 1 == end) // partial buffer if we have \r but no \n
        return (NULL);
      sawCR = 1;
    }
    else if (*s == '\n')
    {
      if (sawCR)
      {
        *(s - 1) = '\0'; // terminate at the CR
        return (++s);
      }
      else
      {
        *s = '\0';
        return (++s);
      }
    }
    ++s;
  }
  return (NULL); // didn't find a complete line
}

// ensures that there's nothing funky contained in a RESP string
static int
isRESPString(RESPROTO *rpp, byte *p)
{
  for (; *p; p++)
    if (!(isgraph(*p) || *p == ' '))
    {
      rpp->errorMsg = "invalid character in RESP string";
      return (0);
    }
  return (1);
}

// Checks to see if there's room for a new item and if not increases
// the number of items available in a RESPROTO. Returns 1 if ok 0 if not
static int
growRESPItems(RESPROTO *rpp)
{
  int newMaxItems;
  size_t newSize;

  if (rpp->nItems < (rpp->maxItems - 1))
    return (1);

  newMaxItems = rpp->maxItems * RESPITEMSGROWTH;
  newSize = sizeof(RESPITEM) * newMaxItems;

  RESPITEM *newItems = rp_redisRealloc(rpp->items, newSize);
  if (newItems == NULL)
  {
    rpp->errorMsg = "Unable to realloc more memory for RESP parser";
    return (0);
  }
  rpp->items = newItems;
  rpp->maxItems = newMaxItems;
  return (1);
}

// resets the RESPROTO to a starting state, does not shrink the items array
static void
reinitRESP(RESPROTO *rp, byte *buf, size_t bufLen)
{
  rp->nItems = 0;
  rp->buf = rp->currPointer = buf;
  rp->bufEnd = buf + bufLen;
  rp->errorMsg = NULL;
}

// reallocates a client's input buffer and fixes pointers in the parser to adjust for the move
byte *
respBufRealloc(RESPROTO *rp, byte *oldBuffer, size_t newSize)
{
  int i;
  byte *newBuffer = rp_redisRealloc(oldBuffer, newSize);
  if (newBuffer && newBuffer != oldBuffer) // the latter clause is because realloc may return same region
  {
    rp->currPointer = newBuffer + (rp->currPointer - oldBuffer);
    rp->bufEnd = (newBuffer) + (rp->bufEnd - oldBuffer);
    rp->buf = newBuffer;

    // now we have to make all the already parsed pointers valid again
    for (i = 0; i < rp->nItems; i++)
      if (rp->items[i].respType == RESPISSTR || rp->items[i].respType == RESPISBULKSTR || rp->items[i].respType == RESPISPLAINTXT)
        rp->items[i].loc = newBuffer + (rp->items[i].loc - oldBuffer);
  }
  // bug fix: guard error message to only trigger on alloc failure, not same-pointer realloc - 2026-02-27
  else if (!newBuffer)
    rp->errorMsg = "Failed attempt to grow recieve buffer size in respBufRealloc()";

  return (newBuffer);
}

// eats sequential spaces and tabs
static byte *
skipSpace(byte *s)
{
  while (*s && isspace(*s))
    ++s;

  return (s);
}

// eats sequential non-space and tabs. treats stuff inside " " as graphic
static byte *
skipGraph(byte *s)
{
  byte priorChar = '\0';
  while (*s)
  {
    if (*s == '"' && priorChar != '\\')
    {
      ++s;
      while (*s)
      {
        if (*s == '"' && priorChar != '\\')
        {
          priorChar = *s;
          ++s;
          break;
        }
        // bug fix: track priorChar inside quotes so \" is recognized as escaped - 2026-02-27
        priorChar = *s;
        ++s;
      }
    }
    else if (!isgraph(*s))
      break;
    priorChar = *s;
    ++s;
  }
  return (s);
}

// counts the number of items enumerated in a plaintext string
static int
respTextItems(byte *s, byte *end)
{
  int n = 0;

  while (*s && s < end)
  {
    if (isgraph(*s))
    {
      ++n;
      s = skipGraph(s);
    }

    while (*s == ' ' || *s == '\t')
      ++s;
  }
  return (n);
}

// converts a plaintext one line command into an array of items

static int
convertRespPlaintext(RESPROTO *rpp, byte *p, byte *end)
{
  RESPITEM *thisItem;

  if (!growRESPItems(rpp))
    return (RESP_PARSE_ERROR);

  thisItem = &rpp->items[rpp->nItems];

  thisItem->respType = RESPISARRAY;
  thisItem->nItems = respTextItems(p, end);
  rpp->nItems++;

  byte priorChar = '\0';
  byte *q;

  while (p < end && *p)
  {
    p = skipSpace(p); // eat leading space which shouldn't be legal anyway

    if (!growRESPItems(rpp))
      return (RESP_PARSE_ERROR);

    thisItem = &rpp->items[rpp->nItems];

    q = p;
    while (*q && q < end) // while weve got graphic chars or in a quoted string
    {
      if (*q == '\"' && priorChar != '\\')
      {
        p = ++q; // don't include the quotes
        while (*q && q < end)
        {
          if (*q == '"' && priorChar != '\\')
          {
            *q = '\0';
            break;
          }
          priorChar = *q;
          ++q;
        }
      }
      else if (!isgraph(*q))
        break;

      priorChar = *q;
      ++q;
    }

    *q = '\0';

    if (isItNumeric(p))
    {
      double floatingpoint;
      int64_t integer;

      parseRespNumber(rpp, p, &floatingpoint, &integer);

      // bug fix: use isnan() instead of == NAN which never matches - 2026-02-27
      if (isnan(floatingpoint))
      {
        thisItem->respType = RESPISINT;
        thisItem->rinteger = integer;
        thisItem->loc = p;
        rpp->nItems++;
      }
      else
      {
        thisItem->respType = RESPISFLOAT;
        thisItem->rfloat = floatingpoint;
        thisItem->loc = p;
        rpp->nItems++;
      }
    }
    else
    {
      thisItem->respType = RESPISBULKSTR;
      thisItem->length = strlen((char *)p);
      thisItem->loc = p;
      rpp->nItems++;
    }

    p = q + 1;
  }
  return (1);
}

// checks to see if we're currently in an array, and if so decrements the remaining needed values.
// if underflow it decrements the nest level
static void
decrementArray(RESPROTO *rpp)
{
  if (!rpp->arrayDepth) // we're not in an array
    return;

  rpp->arrayNest[(rpp->arrayDepth) - 1]--;

  if (!rpp->arrayNest[(rpp->arrayDepth) - 1])
    rpp->arrayDepth -= 1;
}

// sets the error message and returns the error code
static int
respParseError(RESPROTO *rpp, char *str)
{
  rpp->errorMsg = str;
  return (RESP_PARSE_ERROR);
}

/*
   *4             array
      *2          array
         $5
         hello
         $5
         world
      :15
      +kilroy was here
      :17.8
 
 
   *3
   $-1            null
 
*/

// parses the buffer returns 1 if complete , 0 if incomplete, -1 on error
int parseResProto(RESPROTO *rpp, byte *buf, size_t bufLen, int newBuffer)
{
  byte *p, *end;        // p is where were at in the parse, end is 1 past the end of valid data
  byte *restoreTo;      // this is used to roll back on a partial parse
  double floatingPoint; // used to parse numbers out of protocol
  int64_t integer;      // used to parse numbers out of protocol
  RESPITEM *thisItem;

  rpp->errorMsg = NULL;

  if (newBuffer)
  {
    reinitRESP(rpp, buf, bufLen);
    p = buf;
  }
  else
    p = rpp->currPointer;

  end = rpp->bufEnd = buf + bufLen;

  restoreTo = p; // in case we can't complete

  while (p < end)
  {
    byte *nextItem = isThereEOL(p, end);

    if (nextItem == NULL) // we didn't get a complete line
    {
      rpp->currPointer = restoreTo;
      return (RESP_PARSE_INCOMPLETE);
    }

    if (!growRESPItems(rpp))
      return (RESP_PARSE_ERROR);

    thisItem = &rpp->items[rpp->nItems];

    if (!isRESPString(rpp, p)) // anything chewed up by switch should not contain odd chars
      return (respParseError(rpp, "RESP invalid character in RESP string"));

    switch (*p)
    {
    case '*':
    {
      thisItem->respType=RESPISARRAY;

      if(!parseRespNumber(rpp,p+1,&floatingPoint,&integer))
        return(respParseError(rpp,"RESP invalid integer array length after '*'"));

      thisItem->length=integer;

      decrementArray(rpp); // PBR added March 30th 2021 to solve a conflicting understanding with RESP docs

      if(integer<0) // ajf caught this PBR 2021-04-14 : "-1 is the same as null -ajf"
      {
        integer=0;
        thisItem->length=0;
        thisItem->respType=RESPISNULL;
      }

      if(thisItem->length!=0) // if it's not the 0 length array
      {
        if(rpp->arrayDepth>=RESPNESTEDARRAYMAX)
          return(respParseError(rpp,"RESP array nesting exceeded limit"));
        rpp->arrayNest[rpp->arrayDepth++]=(uint32_t)integer;
      }
      ++rpp->nItems;
      break;
    }
    case '+': // simple string
    {
      thisItem->respType = RESPISSTR;
      thisItem->length = strlen((char *)(p + 1));
      thisItem->loc = (p + 1);
      decrementArray(rpp);
      ++rpp->nItems;
      break;
    }
    case ':': // could be a floating point or an integer in RP_REDIS
    {
      if (!parseRespNumber(rpp, p + 1, &floatingPoint, &integer))
        return (respParseError(rpp, "RESP non-integer or non-floating point value in numeric ':' field"));

      if (strchr((char *)p, '.'))
      {
        thisItem->rfloat = floatingPoint;
        thisItem->loc = p;
        thisItem->respType = RESPISFLOAT;
      }
      else
      {
        thisItem->rinteger = integer;
        thisItem->loc = p;
        thisItem->respType = RESPISINT;
      }
      decrementArray(rpp);
      ++rpp->nItems;
      break;
    }
    case '-':
    {
      thisItem->respType = RESPISERRORMSG;
      thisItem->length = strlen((char *)(p + 1));
      thisItem->loc = (p + 1);
      decrementArray(rpp);
      ++rpp->nItems;
      break;
    }
    case '$': // bulk string
    {
      if (!parseRespNumber(rpp, p + 1, &floatingPoint, &integer))
        return (respParseError(rpp, "RESP invalid integer length in bulk string ($N\\r\\n)"));

      if (integer == -1) // NULL Reply
      {
        thisItem->respType = RESPISNULL;
        thisItem->loc = NULL;
        decrementArray(rpp);
        ++rpp->nItems;
        break;
      }

      thisItem->length = integer;
      thisItem->respType = RESPISBULKSTR;

      if (nextItem + integer < end)
      {
        byte *testEol = isThereEOL(nextItem + integer, end);
        if (!testEol)
          return (RESP_PARSE_INCOMPLETE);
        else
        {
          thisItem->loc = nextItem;
          nextItem = testEol;
          decrementArray(rpp);
          ++rpp->nItems;
          break;
        }
      }
      else
        return (RESP_PARSE_INCOMPLETE);
    }
    default: // could be an ascii command string for the server
    {
      if (rpp->arrayDepth)
        return (respParseError(rpp, "RESP plaintext field within an array is invalid"));

      if (!(isalpha(*p) || *p == '\n')) // all commands must start with one of these
        return (respParseError(rpp, "RESP invalid character in plaintext field"));

      convertRespPlaintext(rpp, p, nextItem);
    }
    }
    //restoreTo = p = nextItem;
    rpp->currPointer=restoreTo=p=nextItem; // PBR 4/28/21 attempt to fix restart
  }
  if (rpp->arrayDepth) // we're still expecting more array members
  {
    rpp->currPointer = restoreTo; //--ajf
    return (RESP_PARSE_INCOMPLETE);
  }
  return (RESP_PARSE_COMPLETE);
}

#ifdef NEEDEDLATERBUTNOTNOW
// returns the ascii rendered length of a double when printed
static size_t
strDoubleLen(double n)
{
  char buf[50]; // larger than needed
  sprintf(buf, "%.*g", DBL_DECIMAL_DIG, n);
  return (strlen(buf));
}

// returns the ascii rendered length of a double when printed
static size_t
strIntegerLen(int64_t n)
{
  char buf[50]; // larger than needed
  sprintf(buf, "%lld", n);
  return (strlen(buf));
}

// calculates the length of the buffer required for a respPrintf
static size_t
respPrintfLen(char *fmt, va_list *args)
{
  size_t len = 0;
  int arraySize = 0;
  char *p;
  va_list arg;         // make a copy of the args. Don't call va_start according to spec
  va_copy(arg, *args); // https://wiki.sei.cmu.edu/confluence/display/c/MSC39-C.+Do+not+call+va_arg%28%29+on+a+va_list+that+has+an+indeterminate+value

  for (p = fmt; *p; p++)
  {
    if (*p == '%')
    {
      ++p;
      ++arraySize;
      if (!strncmp(p, "lld", 3))
      {
        len += strIntegerLen(va_arg(arg, int64_t));
        p += 3;
      }
      else if (!strncmp(p, "ld", 2))
      {
        len += strIntegerLen((int64_t)va_arg(arg, long));
        p += 2;
      }
      else if (*(p + 1) == 'd')
      {
        len += strIntegerLen((int64_t)va_arg(arg, int));
        p++;
      }
      else if (!strncmp(p, "lf", 2))
      {
        len += strDoubleLen(va_arg(arg, double));
        p += 2;
      }
      else if (*p == 'f')
      {
        len += strDoubleLen(va_arg(arg, double));
        ++p;
      }
      else if (*p == 's')
      {
        char *s = va_arg(arg, char *);
        if (s == NULL)
          len += 5; // 5 is the length of redis null "$-1\r\n"
        else
          len = strlen(s) + 2; // plus 2 is for \r\n
        ++p;
      }
      else if (*p == 'b') // b is bulk string and has two arguments, first is length, second is
      {
        byte *b = va_arg(arg, byte *);
        if (b == NULL)
        {
          len += 5;
          va_arg(arg, size_t); // eat the second arg
        }
        else
          len += va_arg(arg, size_t) + 2; // plus 2 for \r\n

        ++p;
      }
      else if (*p == '%')
      {
        len++;
        p++;
      } // %% for a % sign
      else
        return (0); // this is an error, unknown escape
    }
    else // it was a normal character
    {
      ++len;
      ++p;
    }
  }
  va_end(arg);
  return (len);
}
#endif

// counts the number of items enumerated in a string
int respPrintfItems(char *s)
{
  int n = 0;
  while (*s)
  {
    if (isgraph(*s))
    {
      ++n;
      ++s;
      while (isgraph(*s) && *s != '%')
        ++s;
    }

    while (isspace(*s))
      ++s;
  }
  return (n);
}

int respSendReply(RESPROTO *rpp, FILE *fh)
{
  int i;

  for (i = 0; i < rpp->nOutItems; i++)
  {
    RESPITEM *item = &rpp->outItems[i];
    switch (item->respType)
    {
    case (RESPISNULL):
    {
      fprintf(fh, "%s", "$-1\r\n");
      break;
    }
    case (RESPISFLOAT):
    {
      fprintf(fh, ":%#.*e\r\n", DBL_DECIMAL_DIG - 1, item->rfloat);
      break;
    }
    case (RESPISINT):
    {
      fprintf(fh, ":%lld\r\n", (long long int)item->rinteger);
      break;
    }
    case (RESPISARRAY):
    {
      fprintf(fh, "*%zd\r\n", item->length);
      break;
    }
    case (RESPISSTR):
    {
      fputc('+', fh);
      fwrite(item->loc, 1, item->length, fh);
      fprintf(fh, "\r\n");
      break;
    }
    case (RESPISBULKSTR):
    {
      fprintf(fh, "$%zu\r\n", item->length);
      fwrite(item->loc, 1, item->length, fh);
      fprintf(fh, "\r\n");
      break;
    }
    case (RESPISERRORMSG):
    {
      fprintf(fh, "-%s\r\n", item->loc);
      break;
    }
    }
  }

  // bug fix: flush the correct file handle instead of stdout - 2026-02-27
  fflush(fh);
  return (1);
}
