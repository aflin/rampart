//
//  csv_parser.c
//  csv_parser
//
//  Created by P. B. Richards on 10/16/20.
//
//
//  This code is in the public domain. The author assumes no liability for any usage
//  of this code, and makes no claims about its efficacy. If you replicate it please
//  retain the "Created by" line .

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include "csv_parser.h"


/*
*  Lists of backslash escape translations
*/

static byte fullEscList[] =   {0x5C,0x27,0x22,'a' ,'b' ,'e' ,'f' ,'n' ,'r' ,'t' ,'v' ,0x00};
static byte fullEscXlate[] =  {0x5C,0x27,0x22,0x07,0x08,0x1B,0x0C,0x0A,0x0D,0x09,0x0b,0x00};

static byte smallEscList[]=   {0x5C,0x27,0x22,0x00}; // self translating: backslash, single quote, and double quote

int csvErrNo=0;

/* 
* opens and reads the CSV but does not do anything with it so that flags may be altered prior to parsing.
* returns NULL on error and errno should be valid for diagnostics
*/

CSV *
openCSV(char *fileName)
{
   struct stat st;
   FILE *      fh=NULL;
   CSV  *      csv=calloc(1,sizeof(CSV));
   
   csvErrNo=0;
   
   if(!csv)
   {
      csvErrNo=errno;
      return(csv);
   }
   
   // read in the raw file
   if( stat(fileName, &st)<0
   || !(csv->buf=malloc((size_t)st.st_size+1))
   || !(fh=fopen(fileName,"r"))
   || fread(csv->buf,1,(size_t)st.st_size,fh)!=st.st_size
   )
   {
      csvErrNo=errno;
      
      if(fh)
         fclose(fh);
      if(csv)
         free(csv);
      return(NULL);
   }
   
   fclose(fh);                     // we no longer need the file
   
   csvDefaultSettings(csv);
   csv->fileSize=st.st_size;
   csv->buf[csv->fileSize]='\0';    // null teminate the buffer
   removeEmptyTailSpace(csv->buf,(size_t)st.st_size); // remove empty lines at end of file
   csv->end=csv->buf+csv->fileSize; // make it easy to check if we're at eof
   return(csv);
}


/*
* Free up the memory used by CSV and return NULL
*/

CSV *
closeCSV(CSV *csv)
{
   csvErrNo=0;
   
   if(csv)
   {
      if(csv->buf)
         free(csv->buf);
         
      if(csv->item)
      {
         if(csv->item[0])
            free(csv->item[0]);
            
         free(csv->item);
      }
      free(csv);
   }
   
  csvErrNo=errno;
  return(NULL);
}

/*
* Casts an item from one type to another if possible
*/

static void
convertCSVItem(CSVITEM *item,enum CSVitemType toType)
{
   time_t T;
   
   if(item->type==toType)     // nothing needs to be done
      return;
      
   switch(item->type)
   {
      case integer:
         switch(toType)
         {
            case floatingPoint: item->floatingPoint=(double)item->integer;item->type=floatingPoint;return;
            case string       : item->type=string;return; // the string is already stored
            case dateTime     : T=(time_t)item->integer;item->dateTime= *gmtime(&T); item->type=dateTime;return;
            default           : return; // converion to NIL makes no sense
         } break;
      case floatingPoint:
         switch(toType)
         {
            case integer      : item->integer=(int64_t)item->floatingPoint;item->type=integer;return;
            case string       : item->type=string; return;
            case dateTime     : T=(time_t)item->floatingPoint;item->dateTime= *gmtime(&T); item->type=dateTime;return; // if you insist...
            default           : return;
         } break;
      case string: // conversion from string type to anything else isn't possible, because it would have already done so if possible.
         {
           item->type=nil;
           return;
         }
      case dateTime:
         switch(toType)
         {
            case floatingPoint : T=mktime(&item->dateTime);item->floatingPoint=(double)T;item->type=floatingPoint;return;  // this conversion is questionable
            case integer       : T=mktime(&item->dateTime);item->integer=(int64_t)T;item->type=integer;return;             // and so is this one
            case string        : item->type=string;return;
            default            : return;
         }
      case nil                 : return; // nil can not be converted
   }
}

/*
   Evaluates a column[0-Ncols], rows[1-Nrows] and finds the majority type
   then forces all members except row 0 to that type.
   
   Invalid floating point, integers, and dates are turned into NIL types.
   
   Columns with a majority of NIL cells will remain untouched
   
   Columns containing any floating point values but a majority of integers
   will be converted to floating point
*/

void
normalizeCSVColumn(CSV *csv,int columnN,int startRow)
{
  int typeCount[NCSVITEMTYPES];
  int i;
  int max=0;
  enum CSVitemType maxtype=nil;
  
   for(i=0;i<NCSVITEMTYPES;i++)        // zero out counters
      typeCount[i]=0;
   
   for(i=startRow;i<csv->rows;i++)
      typeCount[csv->item[i][columnN].type]+=1;
   
   for(i=0;i<NCSVITEMTYPES;i++)
      if(typeCount[i]>max)
      {
         max=typeCount[i];
         maxtype=i;
      }
   
   if(maxtype==nil)                    // if most of the column is empty/nil leave the remaining cells alone
      return;
   
   if(maxtype==integer                 // if there's some floating point amid more integer values convert all to floating point
   && typeCount[floatingPoint]>0
   ) maxtype=floatingPoint;
   
   for(i=startRow;i<csv->rows;i++)
      convertCSVItem(&csv->item[i][columnN],maxtype);
      
}


/*
*  Checks to see if all of row 0's columns are strings
*  If majority are strings it will force all strings
*  returns 1 if majority string, 0 if not
*/

static int
checkCSVHeaders(CSV *csv)
{
   int i;
   int stringCount=0;
   int nonStringCount=0;
   
   for(i=0;i<csv->cols;i++)
      if(csv->item[0][i].type==string)
         ++stringCount;
      else
         ++nonStringCount;
   
   if(stringCount>=nonStringCount)
   {
      for(i=0;i<csv->cols;i++)
         convertCSVItem(&csv->item[0][i],string);
         
     return(1);
   }
   
   return(0);
}


/*
*  Normalizes all columns of a CSV to each column's majority type
*  Checks to see if there's headers or not and excludes headers from normalization
*/

void
normalizeCSV(CSV *csv)
{
  int startRow=checkCSVHeaders(csv); // see if there's headers , if not start at row 0
  int i;
  
   for(i=0;i<csv->cols;i++)
      normalizeCSVColumn(csv,i,startRow);
}

/*
*  Checks to see if a backslash character escapement is legitimate,
*  and if so returns its translation, otherwise returns 0
*/

static int
isBackslashEscape(CSV *csv,int c)
{
   byte *escape;
   byte *translation;
   
   if(csv->backslashEscape && c) // is \ enabled and is c a character
   {
      if(csv->allEscapes)        // regrets: I should have probably done this assignment within the CSV object
      {
         escape=fullEscList;
         translation=fullEscXlate;
      }
      else escape=translation=smallEscList;
      
      
      while(*escape)
      {
         if(*escape==c)
            return(*translation);
      
         ++translation;
         ++escape;
      }
   }
  return(0);
}



/*
*  find the closing delimiter at position pointed to by s or greater
*/

static byte *
seekNextDelimiter(CSV *csv,byte *s)
{
   int   inSingleQuote=0;
   int   inDoubleQuote=0;
   byte  lastChar='\0';
   byte *escape;
   byte *translation;
   
   if(csv->allEscapes)
   {
      escape=fullEscList;
      translation=fullEscXlate;
   }
   else escape=translation=smallEscList;
   
   
   if(s>=csv->end)         // last call, you don't have to go home but you cant stay here
      return(NULL);
   
   while(s<csv->end)
   {
      if(*s==csv->delimiter)
      {
         if(lastChar=='\\' && csv->backslashEscape)
            goto ignore;
         
         if(inSingleQuote || inDoubleQuote)
            goto ignore;
         
         return(s);        // we found a legit delimiter
      }
      else if(*s =='"')
      {
         if(inSingleQuote)
            goto ignore;
         
         
         if((lastChar=='"' || *(s+1)=='"') && csv->doubleQuoteEscape)
            goto ignore;
      
         if(lastChar=='\\' && csv->backslashEscape)
            goto ignore;
         
         inDoubleQuote=inDoubleQuote ? 0:1;
      }
      else if(*s == '\'' && csv->singleQuoteNest)
      {
         if(lastChar=='\\' && csv->backslashEscape)
            goto ignore;
         
         inSingleQuote=inSingleQuote ? 0:1;

      }
      else if(*s=='\n')
      {
         if(inSingleQuote || inDoubleQuote)
            goto ignore;
         
         return(s);
      }
      ignore:
      lastChar=*s;
      ++s;
   }
  
  return(s);
}

/*
*  shifts the characters of a string by one to the left
*/

static void
leftShiftChars(byte *s)
{
   while(*s)
   {
      *s=*(s+1);
      ++s;
   }
}

/*
*  Count the rows and columns and allocate the items
*  Return the True if ok, False if not.
*/

static int
dimensionCSV(CSV *csv)
{
  byte  *s;
  byte  *lastS;
  int    nCells=0;
  int    row=0;
  int    col=0;

  CSVITEM *itemP;
  
  csv->errorMsg[0]='\0';
  csv->rows=csv->cols=0;
  lastS=s=csv->buf;
  
   while((s=seekNextDelimiter(csv,s))!=NULL)
   {
      ++nCells;
      ++col;
      if(*s=='\n')
      {
         if(!csv->rows)       // first row determines how many columns there are
            csv->cols=nCells;
         else
         {
            if(col!=csv->cols)
            {
              snprintf(csv->errorMsg,MAXCSVERRORMSG,"line: %d has %d columns, expected %d",csv->rows+1,col,csv->cols);
              return(0);
            }
         }
         ++csv->rows;
         col=0;
      }
      lastS=s;
      ++s;
   }
  
   if(*lastS!='\n')           // in the event that the last line (row) did not have a \n
      ++csv->rows;
   
   if(!csv->cols)             // if there was only one row the number of cols needs to be set
      csv->cols=nCells;
   
   
   if( (csv->item=calloc(csv->rows,sizeof(CSVITEM *)))==NULL
   ||  (itemP=calloc(nCells,sizeof(CSVITEM)))==NULL
   )
      return(0);
   
   for(row=0;row<csv->rows;row++,itemP+=csv->cols)  // init the matrix pointers
      csv->item[row]=itemP;
   
   return(1);
}

/*
* akin to isspace() but does not include \n
*/

static inline int
isWhiteCSV(int c)
{
  char *whiteSpace=" \t\r\v\f";
   while(*whiteSpace)
   {
      if(*whiteSpace==c)
         return(1);
      ++whiteSpace;
   }
  return(0);
}

/*
* finds the first non ' ' and returns a pointer to it
*/

static byte *
eatLeadingWhite(byte *s)
{
   while(isWhiteCSV(*s))
      ++s;
   return(s);
}


/*
*  replaces all  ' ' with '\0' at end of string
*/

static byte *
eatTrailingWhite(byte *s)
{
   byte *p=s;
   
   while(*p)
      ++p;
   
   --p;
   
   while(p>=s && isWhiteCSV(*p))
   {
      *p='\0';
      --p;
   }
   return(s);
}

/*
*  Examines leading part of string to see if it contains "inf" or Nan
*  returns true if it's either of them
*/
static int
isInfOrNan(byte *s)
{
   for(;*s==' ';s++);      // skip leading white
   
   // note , this is imperfect, but it'll be caught later if it's wrong
   if(strncasecmp((char *)s,"inf",3)==0
   || strncasecmp((char *)s,"nan",3)==0
   ) return(1);
   
 return(0);
}

/*
*  Test if the entirety of a column could be a numeric value.
*  Returns itemtype if it thinks it is
*/
static enum CSVitemType
itCouldBeNumeric(CSV *csv,byte *s)
{
  int e        =0;
  int period   =0;
  int sign     =0;
  int len      =0;
  
   if(isInfOrNan(s))
      return(floatingPoint);

   for(;*s;s++,++len)
   {
      if(len>=MAXCSVNUMBERLEN)   // it's simply way too long
         return(string);
         
      if(isdigit(*s))
         continue;
      
      switch(*s)
      {
         case '.' :
            if(period)
               return(string);   // two periods
            else
               ++period;
            break;
         
         case '+':
         case '-':
            if(sign>=2)
               return(string);   // more than 2 +/-
            else
               ++sign;
            break;
         
         case 'e':
         case 'E':
            if(e)
               return(string);   // no more than one  N.N E N
            else
               ++e;
            break;
   
         case ' ':
         case ',':
            break;
        
         default : return(string); // if it's not any of the above it's a string
      }

   }
   
   if(e && !period)                // 3 E 9 is invalid (sorry)
      return string;
   
   if(period || csv->europeanDecimal)
      return(floatingPoint);

   return(integer);
}


/*
* determines if there's anything but spaces in s
*/
static int
itsAllSpaces(char *s)
{
   while(*s)
   {
      if(*s!=' ')
         return(0);
      ++s;
   }
  return(1);
}


/*
*  For English numbers this removes commas
*  For Euro numbers it removes ' ' and replaces ',' with '.'
*/
static void
normalizeNumerics(CSV *csv,byte *s,byte *p,int maxLen)
{
   int len=0;
   if(csv->europeanDecimal)
   {
      while(len<maxLen && *s)
      {
         if(*s!=' ')
         {
            if(*s==',')
               *p='.';
            else
               *p=*s;
           
            ++p;
            ++len;
         }
         s++;
      }
   }
   else // US, UK, AU, NZ ....
   {
      while(len<maxLen && *s)
      {
         if(*s!=',')
         {
            *p=*s;
            ++len;
            ++p;
         }
         ++s;
      }
   }
   *p='\0';
}

static byte *
collapseEscapements(CSV *csv,byte *s)
{
   int   inSingleQuote  =0;   // where we're inside ' 's
   int   inDoubleQuote  =0;   // where we're insde " "s
   int   doubleQuoteEsc =0;   // where two "s mean ignore the "s eg : "A""B" becomes A"B
   byte  *p=s;
   
   for(;*s;)
   {
      if(*s=='\\')
      {
         byte translated=isBackslashEscape(csv,*(s+1));   // is it a legitimate \ escapement?
        
         if(translated)
         {
            leftShiftChars(s);
            *s=translated;
         }
      }
      else
      if(*s=='"')
      {
         if(!inSingleQuote)                     // if we're in single quotes we ignore all the double quotes
         {
            if(doubleQuoteEsc)
                  doubleQuoteEsc=0;
            else
            {
               leftShiftChars(s);
            
               if(*s=='"' && csv->doubleQuoteEscape) // this is the case where "" means a single "
               {
                  doubleQuoteEsc=1;
                  continue;
               }
               inDoubleQuote=inDoubleQuote ? 0:1;  // toggle the quote state
               continue;
            }
         }
      }
      else
      if(*s=='\'' && csv->singleQuoteNest && !inDoubleQuote)
      {
         leftShiftChars(s);
         inSingleQuote=inSingleQuote ? 0:1;
      }
      
      ++s;
   }
  return(p);
}



/*
* Tries to determine the type of a CSV cell and then inits the CSVITEM with that
* value. It will default to (string or NULL) if it can't determine a more refined type
*/

static void
parseCSVItem(CSV *csv,CSVITEM *item,byte *s)
{
   char *    endPointer;
   double    floatingPointVal;
   int64_t   integerVal;
   byte      numberString[MAXCSVNUMBERLEN+1];
   byte      checkForNumber;
  
   memset(&item->dateTime,0,sizeof(struct tm));
   
   item->string=s;      // we're defaulting to string
   item->type=string;
      
   if(!*s)
   {
      item->type=nil;
      return;
   }
   
   if(csv->tryParsingStrings)    // some CSV files quote everything, this removes the quotes in advance and then tries the data types
      item->string=collapseEscapements(csv,item->string);
   
   // time must be tried first because it's an arbitrary string
   if((endPointer=strptime((char *)s,csv->timeFormat,&item->dateTime))
      && itsAllSpaces(endPointer)   // more stuff in field means it's not a number
   )
   {
      item->type=dateTime;
      return;
   }
   
   checkForNumber=itCouldBeNumeric(csv,s);
   
   if(checkForNumber!=string)
   {
      // copy the potential number while removing extranious characters
      normalizeNumerics(csv,s,numberString,MAXCSVNUMBERLEN);

      if(checkForNumber==integer)
      {
         integerVal=strtoll((char *)numberString,&endPointer,10);
         if(itsAllSpaces(endPointer))
         {
            item->integer=integerVal;
            item->type=integer;
            return;
         }
      }
      
      if(checkForNumber==floatingPoint)
      {
         floatingPointVal=strtod((char *)numberString,&endPointer);
         if(itsAllSpaces(endPointer))
         {
            item->floatingPoint=floatingPointVal;
            item->type=floatingPoint;
            return;
         }
      }
   }
   
   if(item->type==string && !csv->tryParsingStrings)
      item->string=collapseEscapements(csv,item->string);
}

/*
*  Parses the CSV file and returns the number of rows it found
*  On success it returns the number of rows, on error it will return -1 
*/
int
parseCSV(CSV *csv)
{
   byte *s;
   byte *endOfS;
   CSVITEM *itemP=NULL;
   
   // the two options are mutually exclusive, so silently change the delimeter
   if(csv->europeanDecimal && csv->delimiter==',')
      csv->delimiter=';';
   
   if(!dimensionCSV(csv))
      return(-1);
   
   itemP=csv->item[0];
   s=csv->buf;

   while((endOfS=seekNextDelimiter(csv,s))!=NULL)
   {
      *endOfS='\0';
      
      if(csv->stripLeadingWhite)
         s=eatLeadingWhite(s);
      
      if(csv->stripTrailingWhite)
         eatTrailingWhite(s);
      
      parseCSVItem(csv,itemP,s);
   
      s=endOfS+1;
      ++itemP;
   }
   
  return(csv->rows);
}

// ******************************************************************************************
#ifdef TESTCSV
// All code below here is for testing and not needed for library use
// ******************************************************************************************

/*
*  prints the contents of a string column in a way sql can handle
*/

static void
printSqlString(byte *s)
{
   putchar('\'');
   for(;*s;s++)
   {
      if(*s=='\'')         // escape a single quote
         putchar('\\');
      else
      if(!isprint(*s))     // handle things like \n and \t by concatenation
      {
         putchar('\'');
         do                // there may be one or more sequential \r\n and they might be at end of string
         {
            printf("+CHAR(%d)",(int)*s);
            ++s;
         }
         while(*s && !isprint(*s));
         if(*s)
            printf("+'");
         else
            return;
      }
      
      putchar(*s);
   }
   putchar('\'');
}

/*
*  No real effort was put into this function, it's mostly just a usage demo
*  It makes a lame and probably incorrect assumtion that if the first row is
*  entirely strings that it's a header row, and tosses it.
*/

void
generateSql(CSV *csv,char *tableName)
{
   int row,col;
   
   for(col=0;col<csv->cols && csv->item[0][col].type==string;col++);
   
   if(col==csv->cols)      // if all the columns in the first row are string types assume a header row (probably dumb)
      row=1;
   else
      row=0;
      
   for(;row<csv->rows;row++)
   {
      printf("INSERT INTO %s VALUES(",tableName);
      for(col=0;col<csv->cols;col++)
      {
         switch(csv->item[row][col].type)
         {
            case floatingPoint:  printf("%.15lf",csv->item[row][col].floatingPoint);break;
            case integer      :  printf("%lld",csv->item[row][col].integer);break;
            case string       :  printSqlString(csv->item[row][col].string);break;
            case dateTime     :  {
                                       struct tm *t=&csv->item[row][col].dateTime;
                                       printf("'%4d-%02d-%02d %02d:%02d:%02d'",
                                             t->tm_year+1900,
                                             t->tm_mon+1,
                                             t->tm_mday,
                                             t->tm_hour,
                                             t->tm_min,
                                             t->tm_sec
                                          );
                                 } break;
            case nil          :  printf("NULL");break;
         }
         
         printf("%s",col+1==csv->cols ? ");\n" : ","); // is it end of statement or another column?
      }
   }

}

// show the results of a csv parse
void
dumpCSV(CSV *csv)
{
   int row,col;
   for(row=0;row<csv->rows;row++)
   {
      for(col=0;col<csv->cols;col++)
      {
         switch(csv->item[row][col].type)
         {
            case integer:        printf("%lld",csv->item[row][col].integer);break;
            case floatingPoint:  printf("%lf",csv->item[row][col].floatingPoint);break;
            case string:         printf("\"%s\"",csv->item[row][col].string);break;
            case dateTime:       {
                                    struct tm *t=&csv->item[row][col].dateTime;
                                    printf("%4d-%02d-%02d %02d:%02d:%02d (%ld)",
                                             t->tm_year+1900,
                                             t->tm_mon+1,
                                             t->tm_mday,
                                             t->tm_hour,
                                             t->tm_min,
                                             t->tm_sec,
                                             mktime(t) // use this if you want unix time
                                          );
                                          
                                 }
                                 break;
            case nil:            printf("%16s","NIL");break;
         }
       printf("%c",col+1==csv->cols?'\n':',');
      }
   }
  printf("\n");
}


static void
usage(const char *s)
{
   fprintf(stdout,
   "\n"
   "Usage %s [-paramter_setting value] ... filename\n\n"
   "Where parameters are (default listed first):\n\n"
   "  stripLeadingWhite    1|0      remove leading whitespace characters from cells\n"
   "  stripTrailingWhite   1|0      remove trailing whitespace characters from cells\n"
   "  doubleQuoteEscape    0|1      '\"\"' within strings is used to embed '\"' characters\n"
   "  singleQuoteNest      1|0      strings may be bounded by \' pairs and '\"' characters are ignored\n"
   "  backslashEscape      1|0      characters preceded by '\\' are translated and escaped\n"
   "  allEscapes           1|0      all '\\' escape sequences known by the 'C' compiler are translated\n"
   "  europeanDecimal      0|1      numbers like '123 456,78' will be parsed as 123456.78\n"
   "  tryParsingStrings    1|0      look inside quoted strings for dates and numbers to parse\n"
   "  delimiter            c        use the character 'c' as a column delimiter ie \\t|,|;\n"
   "  timeFormat           string   see manpage for strptime(), default is %%Y-%%m-%%d %%H:%%M:%%S\n"
   "  sql                  string   generate sql insert statements using \'string\' as table name\n"
   "\n"
  ,s);
}


#define argCmpSet(x) \
if(!strcasecmp("-"#x,argv[i]) && i+1<argc && isdigit(*argv[i+1]))\
{\
  csv->x=atoi(argv[++i]);\
  continue; \
}

#define argCmpStr(x) \
if(!strcasecmp("-"#x,argv[i]) && i+1<argc)\
{\
  csv->x=(char *)argv[++i];\
  continue; \
}

#define argCmpChar(x) \
if(!strcasecmp("-"#x,argv[i]) && i+1<argc)\
{\
  csv->x=*argv[++i];\
  continue; \
}


int
main(int argc, const char * argv[])
{
  CSV  * csv=NULL;
  int    i;
  char * tableName=NULL;                  // used for sql option
  
  if(argc<2)
  {
      usage(argv[0]);
      exit(EXIT_SUCCESS);
  }
  for(i=1;i<argc && *argv[i]=='-';i+=2);   // skip - args to find file name
  
  if(i>=argc)
  {
      fprintf(stderr,"Missing filename or argument format invalid\n");
      usage(argv[0]);
      exit(EXIT_FAILURE);
  }
  

  csv=openCSV((char *)argv[i]);
  if(!csv)
  {
      fprintf(stderr,"Failure to Open %s: %s\n",argv[i],strerror(csvErrNo));
      exit(EXIT_FAILURE);
  }
  
  for(i=1;i<argc;i++)
  {
      if(*argv[i]=='-')
      {
         argCmpSet(stripLeadingWhite);
         argCmpSet(stripTrailingWhite);
         argCmpSet(doubleQuoteEscape);
         argCmpSet(singleQuoteNest);
         argCmpSet(backslashEscape);
         argCmpSet(allEscapes);
         argCmpSet(europeanDecimal);
         argCmpSet(tryParsingStrings);
         argCmpChar(delimiter);
         argCmpStr(timeFormat);
         if(!strcasecmp("-sql",argv[i]) && i+1<argc)
         {
            tableName=(char *)argv[++i];
            continue;;
         }
         
         fprintf(stderr,"Invalid argument %s",argv[i]);
         exit(EXIT_FAILURE);
     }
  
  }
  
   if(parseCSV(csv)==-1)
   {
      fprintf(stderr,"Error: %s\n",csv->errorMsg);
      closeCSV(csv);
      exit(EXIT_FAILURE);
   }
   if(!tableName)
   {
      printf("Parsed CSV\n");
      dumpCSV(csv);
  
      normalizeCSV(csv);
  
      printf("Parsed & Normalized CSV\n");
      dumpCSV(csv);
   }
   else
   {
      normalizeCSV(csv);            // columns must not be of mixed types
      generateSql(csv,tableName);
   }
   
 closeCSV(csv);

 exit(EXIT_SUCCESS);
}

#endif // TESTCSV 






