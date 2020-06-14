//
//  respClient.c
//  ramis_client
//
//  Created by Dr Cube on 5/20/20.
//  Copyright © 2020 P. B. Richards. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <strings.h>
#include <unistd.h>
#include <stdarg.h>
#include <poll.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#ifdef RP_USING_DUKTAPE
#include "duktape.h"
#endif
#include "ramis.h"
#include "resp_protocol.h"
#include "respClient.h"



RESPCLIENT *
closeRespClient(RESPCLIENT *rcp)
{
  if(rcp)
  {    
      if(rcp->rppFrom)
         freeRespProto(rcp->rppFrom);
     
      if(rcp->socket>-1)
         close(rcp->socket);
     
      if(rcp->fromBuf)
         ramisFree(rcp->fromBuf);
     
      if(rcp->toBuf)
         ramisFree(rcp->toBuf);

      ramisFree(rcp);
  }
 return(NULL);
}

static
RESPCLIENT *
newRespClient()
{
  RESPCLIENT *rcp=ramisCalloc(1,sizeof(RESPCLIENT));
   if(!rcp)
   {
      fprintf(stderr,"Malloc error in client\n");
      exit(EXIT_FAILURE);
   }
   else
   {
     rcp->rppFrom=newResProto(0); // 0 indicating the parser is not server parsing
     rcp->fromBuf=ramisMalloc(RESPCLIENTBUFSZ);
     rcp->toBuf=ramisMalloc(RESPCLIENTBUFSZ);
     
     if(!rcp->rppFrom || !rcp->fromBuf | !rcp->toBuf)
         return(closeRespClient(rcp));
     
     rcp->fromBufSize=RESPCLIENTBUFSZ;
     rcp->fromReadp=rcp->fromBuf;
    
     rcp->toBufSz=RESPCLIENTBUFSZ;
     
     
     rcp->socket=-1;
   }
  return(rcp);
}


static int
openRespClientSocket(RESPCLIENT *rcp)
{
 	struct sockaddr_in   address;
	struct hostent       *host;
   	// create socket
	rcp->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (rcp->socket<= 0)
	{
		rcp->rppFrom->errorMsg="respClient error: cannot create socket";
		return(RAMISFAIL);
	}

	// connect to server
	address.sin_family = AF_INET;
	address.sin_port = htons(rcp->port);
	host = gethostbyname(rcp->hostname);
	if (!host)
	{
		rcp->rppFrom->errorMsg="respClient error: unknown host";
		return(RAMISFAIL);
	}
	
   memcpy(&address.sin_addr, host->h_addr_list[0], host->h_length);
	if (connect(rcp->socket, (struct sockaddr *)&address, sizeof(address)))
	{
		rcp->rppFrom->errorMsg="respClient error: cannont connect to host";
		return(RAMISFAIL);
	}

   
 return(RAMISOK);
}

// closes and reopens the connection to the server and resets the buffers
int
reconnectRespServer(RESPCLIENT *rcp)
{
  if(rcp->socket>-1)
    close(rcp->socket);
  rcp->fromReadp=rcp->fromBuf;
  return(openRespClientSocket(rcp));
}

// Creates a new RESPCLIENT handle and connects to the server
RESPCLIENT *
connectRespServer(char *hostname,int port)
{
   
	RESPCLIENT *rcp=newRespClient();
   
   if(!rcp)
      return(rcp);
   
   rcp->hostname=hostname;
   rcp->port=port;
   
   if(!openRespClientSocket(rcp))
      return(closeRespClient(rcp));

	return(rcp);
}



//  Polls the socket waiting for data if Timout
//  https://man.openbsd.org/poll.2
static int
waitForRespData(RESPCLIENT *rcp)
{
  struct pollfd pfd;
  int ret;
  memset(&pfd,0,sizeof(struct pollfd));
  pfd.fd=rcp->socket;
  pfd.events=POLLIN|POLLHUP;
  ret=poll(&pfd,1,1000*RESPCLIENTTIMEOUT);
  
  if(ret==-1)
  {
    rcp->rppFrom->errorMsg="poll() Error on read from server";
    if(rcp->socket>-1)
      close(rcp->socket);

    openRespClientSocket(rcp); // attempt reconnect
    return(0);
  }
  else
  if(!ret)
  {// In this case we probably did something stupid and need to reopen it to prevent corruption
    rcp->rppFrom->errorMsg="Timeout reading from server";
    if(rcp->socket)
       close(rcp->socket);

    openRespClientSocket(rcp); // attempt reconnect
    return(0);
  }
  return(1);
}

// see if there's more data waiting to be read
static inline int
isThereMoreComing(RESPCLIENT *rcp)
{
  struct pollfd pfd;
  int ret;
  memset(&pfd,0,sizeof(struct pollfd));
  pfd.fd=rcp->socket;
  pfd.events=POLLIN|POLLHUP;
  ret=poll(&pfd,1,0);
  if(ret==1)
      return(ret);
  return(0);
}


RESPROTO *
getRespReply(RESPCLIENT *rcp)
{
  ssize_t nread;
  int    parseRet;
  int    newBuffer=1;
  ssize_t totalRead=0;
  size_t bufAvailable=rcp->fromBufSize;

  
  rcp->fromReadp=rcp->fromBuf; // re-init read pointer
  
  do
  {
       parseRet=RESP_PARSE_INCOMPLETE;
       //if waitForever is set we'll just block on the read instead of polling with a timeout
       if(!rcp->waitForever)
       if(!waitForRespData(rcp))
            return(NULL);
    
       do
       {
         nread=recv(rcp->socket,rcp->fromReadp,bufAvailable,0);
         if(nread<=0)     // server closed or error
         {
            rcp->rppFrom->errorMsg=strerror( errno );
            reconnectRespServer(rcp);   // try reconnecting
            return(NULL);
         }
         
         totalRead+=nread;
         
         if(nread==(ssize_t)bufAvailable) // we need a bigger buffer becuase it got filled
         {
            rcp->fromBuf=respBufRealloc(rcp->rppFrom,rcp->fromBuf,rcp->fromBufSize+RESPCLIENTBUFSZ);
            if(!rcp->fromBuf)
            {
               rcp->rppFrom->errorMsg="Could not expand recieve buffer in getRespReply()";
               return(NULL);
            }
            rcp->fromReadp=rcp->fromBuf+totalRead;
            rcp->fromBufSize=rcp->fromBufSize+RESPCLIENTBUFSZ;
            bufAvailable=rcp->fromBufSize-totalRead;
         }
         else rcp->fromReadp=rcp->fromBuf+totalRead;
         
       } while(isThereMoreComing(rcp));
     
   
       parseRet=parseResProto(rcp->rppFrom,rcp->fromBuf,totalRead,newBuffer);
     
       if(parseRet==RESP_PARSE_ERROR)
         return(NULL);
     
       newBuffer=0;
       
  } while(parseRet==RESP_PARSE_INCOMPLETE);
  return(rcp->rppFrom);
}



// how many individual items are in the format string
static int
countRespCommandItems(char *s)
{
  int count=0;
  while(*s)
  {
    while(isspace(*s)) ++s;
    if(*s)
    {
      ++count;
      while(*s && !isspace(*s)) ++s;
    }

  }
   return(count);
}


enum percentCode // the list of known escape codes for sendRespCommand
{
unknown=0,
s,
b,
d,
f,
lf,
ld,
lld,
u,
lu,
llu,
pct
};

// we're using this struct for consistency between the functions that deal with % codes
#define PCTCODEINFO struct PercentCodeInfoStruct
PCTCODEINFO
{
  enum percentCode code;
  int  length;
  const char *str;
  const char *fmt;
};

PCTCODEINFO percentCodes[]=
{
 {s,  1, "s",  "%s"     },
 {b,  1, "b",  ""       },
 {d,  1, "d",  "%d"     },
 {f,  1, "f",  "%#.*e"  },
 {lf, 2, "lf", "%#.*e"  },
 {ld, 2, "ld", "%ld"    },
 {lld,3, "lld","%lld"   },
 {u,  1, "u",  "%u"     },
 {lu, 2, "lu", "%lu"    },
 {llu,3, "llu","%llu"   },
 {pct,1,  "%",  ""      },
 {unknown,0,"",""}
};

static PCTCODEINFO *
lookupPctCode(char *s)
{
   int i;
   for(i=0;*percentCodes[i].str;i++)
      if(!strncmp(s,percentCodes[i].str,percentCodes[i].length))
         return(&percentCodes[i]);
   return(NULL);
}


// RE va_list usage: https://wiki.sei.cmu.edu/confluence/display/c/MSC39-C.+Do+not+call+va_arg%28%29+on+a+va_list+that+has+an+indeterminate+value

// Calculates the amount of buffer needed for sendRespCommand and ensures that allocation exists.
// returns an array of argument payload lengths or NULL on error
size_t *
sendRespBufNeeded(RESPCLIENT *rcp,char *fmt,va_list *argp)
{
   va_list arg;
   char *p,*q,t;
   char numberBuf[80];
   PCTCODEINFO *pctCode;
   size_t bufNeeded=0;
   size_t thisArgLength;
   size_t len;
   char   *fmtCopy=strdup(fmt); // everyone gets pissy if we alter the passed fmt
   size_t *argSizes;            // the payload size of each fmt argument
   int     argCount;            // how many individual args are in the fmt;

   argCount=countRespCommandItems(fmt);
   argSizes=ramisCalloc(argCount,sizeof(size_t));
   if(!fmtCopy || !argCount)
   {
      rcp->rppFrom->errorMsg="Memory allocation error in sendRespCommand";
      if(fmtCopy)
         ramisFree(fmtCopy);
      if(argSizes)
         free(argSizes);
      return(NULL);
   }
   
  argCount=0; // the code below uses this as an index into argSizes
  
  va_copy(arg,*argp);
  RP_VA_ARG
  
  // account for the RESP array header
  bufNeeded+=sprintf(numberBuf,"*%d\r\n",countRespCommandItems(fmt));
  
  for(p=fmtCopy;*p;)
  {
    while(isspace(*p)) ++p;

    for(q=p;*q && !isspace(*q);q++); // find the end of this sequence and terminate it
    
    if(!*p)
      break;
     
    t=*q;  // save whatever's pointed to by q
    *q='\0';
    
    thisArgLength=0;
    while(*p)
    {
      if(*p=='%')
      {
        ++p;
        pctCode=lookupPctCode(p);
        switch(pctCode->code)
        {
            case pct: ++bufNeeded;++p;++thisArgLength;break;
            case   s:
            {
              char *thisArg=VA_ARG(arg,char *);
              bufNeeded+=len=strlen(thisArg);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case   b:
            {
              char * thisArg=(char *)VA_ARG(arg,byte *);
              thisArg=NULL; // this is just to shut up a warning for unused variable
              size_t thisLen=len=VA_ARG(arg,size_t);
              bufNeeded+=thisLen;
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case   d:
            {
              int thisArg=VA_ARG(arg,int);
              sprintf(numberBuf,pctCode->fmt,thisArg);
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case  ld:
            {
              long thisArg=VA_ARG(arg,long);
              sprintf(numberBuf,pctCode->fmt,thisArg);
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case lld:
            {
              long long thisArg=VA_ARG(arg,long long);
              sprintf(numberBuf,pctCode->fmt,thisArg);
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case   u:
            {
              unsigned thisArg=VA_ARG(arg,unsigned);
              sprintf(numberBuf,pctCode->fmt,thisArg);
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case  lu:
            {
              unsigned long thisArg=VA_ARG(arg,unsigned long);
              sprintf(numberBuf,pctCode->fmt,thisArg);
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case llu:
            {
              unsigned long long thisArg=VA_ARG(arg,unsigned long long);
              sprintf(numberBuf,pctCode->fmt,thisArg);
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case   f:
            {
              double thisArg=VA_ARG(arg,double);
              sprintf(numberBuf,pctCode->fmt,FLT_DECIMAL_DIG-1,thisArg); // print to the precision of float
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case  lf:
            {
              double thisArg=VA_ARG(arg,double);
              sprintf(numberBuf,pctCode->fmt,DBL_DECIMAL_DIG-1,thisArg); // print to the precision of double
              bufNeeded+=len=strlen(numberBuf);
              thisArgLength+=len;
              p+=pctCode->length;
            } break;
            case unknown:
            default:
            {
              rcp->rppFrom->errorMsg="Invalid % code in sendRespCommand()";
              return(0);
            }
        }
      }
      else // it's just a normal character
      {
        ++bufNeeded;
        ++thisArgLength;
        ++p;
      }
    }
    bufNeeded+=sprintf(numberBuf,"$%zu\r\n",thisArgLength);
    argSizes[argCount++]=thisArgLength;
    bufNeeded+=2; // this is for the \r\n
    *q=t; // put the saved character back into the format string and keep going
  }
  
 VA_END(arg);
  
 if(rcp->toBufSz<bufNeeded)
 { // allocate bigger than needed by RESPCLIENTBUFSZ for future commands that are approx this size
   rcp->toBuf=ramisRealloc(rcp->toBuf,bufNeeded+RESPCLIENTBUFSZ);
   if(!rcp->toBuf)
   {
     rcp->rppFrom->errorMsg="Memory allocation error in sendRespCommand";
     ramisFree(argSizes);
     ramisFree(fmtCopy);
     return(NULL);
   }
   rcp->toBufSz=bufNeeded+RESPCLIENTBUFSZ;
 }
 
 ramisFree(fmtCopy);
 return(argSizes);
}

static int
transmitRespCommand(RESPCLIENT *rcp,byte *buf,size_t n)
{
  //struct pollfd ready; // PBR WTF: Not ready to delete this code yet.
  ssize_t nSent;

  //memset(&ready,0,sizeof(ready));
  //ready.events=POLLOUT; // |POLL_HUP|POLLERR;

  do
  {
    /*
    if(poll(&ready,1,RESPCLIENTTIMEOUT*1000)<0)
    {
      rcp->rppFrom->errorMsg="Poll on server socket failed";
      return(RAMISFAIL);
    }
    */
    nSent=write(rcp->socket,buf,n);
    if(nSent<=0)
    {
      rcp->rppFrom->errorMsg="Send to server socket failed";
      return(RAMISFAIL);
    }
    buf+=nSent;
    n-=nSent;
  } while(n);
  
  return(RAMISOK);
}


// RESP encodes parameters in a printf kind of way and sends them to the server
// returns the server's reply in the form of a list of items in RESPROTO
RESPROTO *
sendRespCommand(RESPCLIENT *rcp,char *fmt,...)
{
  char   *p,*q,t;
  char   *outBuffer;
  char   *bufp;
  char   *fmtCopy=strdup(fmt); // everyone gets pissy if we touch the passed fmt
  va_list arg;
  size_t *argSizes;
  int     argIndex=0;
  PCTCODEINFO *pctCode;
// char   *nullBulkString="$-1\r\n"; PBR WTF I have not yet implemented NULL transmission
  
  
  va_start(arg,fmt);

  argSizes=sendRespBufNeeded(rcp,fmt,&arg);
  RP_VA_ARG

  if(!argSizes)
   return(NULL); // parser or malloc error
   
  if(!fmtCopy)
  {
     bufferFail:
     if(fmtCopy)
         ramisFree(fmtCopy);
     rcp->rppFrom->errorMsg="Malloc error in sendRespCommand";
     return(NULL);
  }
  
  outBuffer=(char *)rcp->toBuf;
  bufp=outBuffer;
  
  // print the RESP array header ( this accounting was not
  rcp->rppFrom->errorMsg=NULL;
  sprintf(bufp,"*%d\r\n",countRespCommandItems(fmt));
  bufp+=strlen(bufp);
  
  bufp+=strlen(bufp);  // not checking for fit here because it has to be long enough
  outBuffer=bufp;
  
  va_start(arg,fmt);
  for(p=fmtCopy;*p;)
  {
    while(isspace(*p)) ++p;
 
    if(!*p)
      break;
    
    sprintf(bufp,"$%zu\r\n",argSizes[argIndex++]); // the bulk string payload header
    bufp+=strlen(bufp);
    
    for(q=p;*q && !isspace(*q);q++); // find the end of this sequence and terminate it
    t=*q;  // save whatever's pointed to by q
    *q='\0';
     
    while(*p)
    {
      if(*p=='%')
      {
        ++p;
        pctCode=lookupPctCode(p);
        switch(pctCode->code)
        {
            case pct:
            {
             *bufp++=*p;
             ++p;
            } break;
            case   s:
            {
              char *thisArg=VA_ARG(arg,char *);
              sprintf(bufp,pctCode->fmt,thisArg);
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case   b:
            {
              char * thisArg=(char *)VA_ARG(arg,byte *);
              size_t thisLen=VA_ARG(arg,size_t);
              memcpy(bufp,thisArg,thisLen);
              bufp+=thisLen;
              p+=pctCode->length;
            } break;
            case   d:
            {
              int thisArg=VA_ARG(arg,int);
              sprintf(bufp,pctCode->fmt,thisArg);
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case  ld:
            {
              long thisArg=VA_ARG(arg,long);
              sprintf(bufp,pctCode->fmt,thisArg);
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case lld:
            {
              long long thisArg=VA_ARG(arg,long long);
              sprintf(bufp,pctCode->fmt,thisArg);
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case   u:
            {
              unsigned thisArg=VA_ARG(arg,unsigned);
              sprintf(bufp,pctCode->fmt,thisArg);
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case  lu:
            {
              unsigned long thisArg=VA_ARG(arg,unsigned long);
              sprintf(bufp,pctCode->fmt,thisArg);
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case llu:
            {
              unsigned long long thisArg=VA_ARG(arg,unsigned long long);
              sprintf(bufp,pctCode->fmt,thisArg);
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case   f:
            {
              double thisArg=VA_ARG(arg,double);
              sprintf(bufp,pctCode->fmt,FLT_DECIMAL_DIG-1,thisArg); // print to the precision of float
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case  lf:
            {
              double thisArg=VA_ARG(arg,double);
              sprintf(bufp,pctCode->fmt,DBL_DECIMAL_DIG-1,thisArg); // print to the precision of double
              bufp+=strlen(bufp);
              p+=pctCode->length;
            } break;
            case unknown:
            default:
            {
              rcp->rppFrom->errorMsg="Invalid % code in sendRespCommand()";
              return(NULL);
            }
        }
      }
      else // it's just a normal character
      {
        *bufp=*p;
        ++bufp;
        ++p;
      }
    }
    *bufp++='\r';
    *bufp++='\n';
    *q=t; // put the saved character back into the format string and keep going
  }
  VA_END(arg);
  
   //fwrite(rcp->toBuf,(byte *)bufp-rcp->toBuf,1,stdout);
   // printf("\n\n");
   
  if(!transmitRespCommand(rcp,rcp->toBuf,(byte *)bufp-rcp->toBuf))
      return(NULL);
  
  return(getRespReply(rcp)); // everything was fine so far, so return the reply from the server 
}


// Sees if anything went wrong. If everything's ok returns NULL , otherwise an error message.
char *
respClienError(RESPCLIENT *rcp)
{
  RESPROTO *rpp=rcp->rppFrom;
  
  if(rpp->errorMsg!=NULL)
    return(rpp->errorMsg);
 
  if(rpp->nItems && rpp->items[0].respType==RESPISERRORMSG)
    return((char *)rpp->items[0].loc);
  
  return(NULL);
}
