//
//  csv_parser.h
//  csv_parser
//
//  Created by P. B. Richards on 10/16/20.
//
//
//  This code is in the public domain. The author assumes no liability for any usage
//  of this code, and makes no claims about its efficacy. If you replicate it please
//  retain the "Created by" line .

#ifndef csv_parser_h
#define csv_parser_h
#ifndef byte
#define byte unsigned char
#endif

// comment the line below to use as a library
//#define TESTCSV 1

#define MAXCSVERRORMSG  80    // Length of longest error message
#define MAXCSVNUMBERLEN 45    // Size of buffer to convert euro numbers to normalized form

#define NCSVITEMTYPES 5       // how many types are enumerated
enum CSVitemType {nil=0,integer,floatingPoint,dateTime,string};


#define CSVITEM struct csvItemStruct
CSVITEM
{
   union
   {
      int64_t     integer;
      double      floatingPoint;
      struct tm   dateTime;   // times are stored this way so they can exceed time_t limitations (call mktime())
   };
   byte *         string;
   enum CSVitemType  type;
};


#define CSV struct csvTableStruct
CSV
{
   // flags and settings
   char     stripLeadingWhite;   // eat whitespace at begining of a string field
   char     stripTrailingWhite;  // eat whitespace at the end of a string field
   char     doubleQuoteEscape;   // use "" to mean single quote inside a quoted string
   char     backslashEscape;     // use backslash to escape characters
   char     allEscapes;          // translate all known backslash escapes. If false then only single quote, double, and quote backslash
   char     europeanDecimal;     // use ',' as a decmial point
   char     singleQuoteNest;     // single quote may start quoted string with nested double quotes eg. ' "hello" "world" '
   char     tryParsingStrings;   // if this is true it will examine quoted strings to see if they're dates or numbers
   int      delimiter;           // the field delimeter e.g. ','
   char *   timeFormat;          // passed to strptime(), default is 2020-10-20 16:30:00 %Y-%m-%d %H:%M:%S
   
   size_t   fileSize;            // bytes contained in the original CSV file before processing
   byte *   buf;                 // content of CSV which is edited in place durning the parse
   byte *   end;                 // pointer to the end of the buffer
   
   int      rows;                // number of rows located
   int      cols;                // number of columns located
   int      lines;               // number of newlines regardless of quotes -ajf 12/17/2021
   
   void     (*cb)(int, int, CSV*);
                                 // callback during load/normalize (int stage, CSV *csv) --ajf 12/29/2021
                                 //                                   - Stage 0: Counting rows (dimensionCSV)
                                 //                                   - Stage 1: Parsing cells (parseCSVitem)
                                 //                                   - Stage n: Optional normalizing (normalizeCSVColumn - scan)
                                 //                                              n = 2 * colno +2;
                                 //                                   - Stage p: Optional normalizing (normalizeCSVColumn - convert)
                                 //                                              p = 2 * colno + 3;
   unsigned int cb_step;         // mod callback --ajf 12/29/2021
   unsigned char step_mod;       // mod match --ajf 12/30/2021
   void     *cbarg;              // void pointer for callback --ajf 12/29/202

   CSVITEM **item;               // the row and column data itself e.g. item[5][3].string will access the string in row 5 column
   
   char     errorMsg[MAXCSVERRORMSG]; // where to look for what went wrong
};


// NOTE: having both doubleQuoteEscape and backslashEscape enabled will probably produce unexpected behavior

#define csvDefaultSettings(csv)  \
{\
(csv)->stripLeadingWhite  =1;    \
(csv)->stripTrailingWhite =1;    \
(csv)->doubleQuoteEscape  =0;    \
(csv)->singleQuoteNest    =1;    \
(csv)->backslashEscape    =1;    \
(csv)->allEscapes         =1;    \
(csv)->europeanDecimal    =0;    \
(csv)->tryParsingStrings  =1;    \
(csv)->delimiter          =',';  \
(csv)->timeFormat         = "%Y-%m-%d %H:%M:%S";\
(csv)->cb                 = NULL; /* --ajf 12/29/2021 */\
(csv)->cb_step            = 1; /* --ajf 12/29/2021 */\
(csv)->step_mod           = 0; /* --ajf 12/30/2021 */\
}


/*
* opens and reads the CSV but does not do anything with it so that flags may be altered prior to parsing.
* returns NULL on error and errno should be valid for diagnostics
*/

CSV *openCSV(char *fileName);


/*
*  Parses the CSV file and returns the number of rows it found
*  On success it returns the number of rows, on error it will return -1
*/
int parseCSV(CSV *csv);


/*
* Free up the memory used by CSV and return NULL
*/

CSV *closeCSV(CSV *csv);

/*
*  Normalizes the data type of all columns of a CSV to each column's majority type
*  Checks to see if there are headers or not and excludes headers from normalization
*  Then calls normalizeCSVColumn() once for each column
*/


void
normalizeCSV(CSV *csv);

/*
   Evaluates a column[0-Ncols], rows[1-Nrows] and finds the majority type
   then forces all members except row 0 to that type.
   
   Invalid floating point, integers, and dates are turned into NIL types.
   
   Columns with a majority of NIL cells will remain untouched
   
   Columns containing any floating point values but a majority of integers
   will be converted to floating point
*/

void normalizeCSVColumn(CSV *csv,int columnN,int startRow);

/*
*  just in case someone added whitespace to the end of the file, remove it
*
*  removed from csv_parser.c and made it into a macro and usable from
*  rampart-import.c -ajf
*/
#define removeEmptyTailSpace(buf, size) do {\
  byte *p=buf+size-1;\
  while(p>=buf && isspace(*p)) {\
    *p='\0';\
    --p;\
  }\
} while(0)

/* csv parser */

/* rampart stuff */
#ifdef RP_H_INCLUDED
#define DCSV struct duk_csv_s

DCSV {
    CSV *csv;
    duk_context *ctx;
    char **hnames;
    const char *tbname;
    duk_idx_t obj_idx;
    duk_idx_t str_idx;
    duk_idx_t col_idx;
    duk_idx_t func_idx;
    duk_idx_t arr_idx;
    duk_idx_t prog_idx;
    int cbstep;
    char retType;
    char hasHeader;
    char inplace;
    char isfile;
    char include_rawstring;
};

DCSV duk_rp_parse_csv(duk_context *ctx, int isfile, int normalize, const char *func_name);
#endif /* rampart stuff */

#endif /* csv_parser_h */
