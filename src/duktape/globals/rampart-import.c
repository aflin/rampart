/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include  <stdio.h>
#include  <time.h>
#include "csv_parser.h"
#include "../core/duktape.h"
#include "../../rp.h"

extern int csvErrNo;

typedef void (*putcol_t)(duk_context *ctx, CSVITEM item);

static void putcol_prim(duk_context *ctx, CSVITEM item)
{
    switch(item.type)
    {
        case integer:        
            duk_push_number(ctx, (double)item.integer);
            break;
        case floatingPoint:  
            duk_push_number(ctx, (double)item.floatingPoint); 
            break;
        case string:
            duk_push_string(ctx, (const char *)item.string);
            break;
        case dateTime:
        {
            struct tm *t=&item.dateTime;
            duk_get_global_string(ctx, "Date");
            duk_push_number(ctx, 1000.0 * (duk_double_t) mktime(t));
            duk_new(ctx, 1);
            break;
        }
        case nil:
            duk_push_null(ctx);
            break;
    }
}

static void putcol_raw(duk_context *ctx, CSVITEM item)
{
    duk_push_object(ctx);
    switch(item.type)
    {
        case integer:        
            duk_push_number(ctx, (double)item.integer);
            break;
        case floatingPoint:  
            duk_push_number(ctx, (double)item.floatingPoint); 
            break;
        case string:
            duk_push_string(ctx, (const char *)item.string);
            break;
        case dateTime:
        {
            struct tm *t=&item.dateTime;
            duk_get_global_string(ctx, "Date");
            duk_push_number(ctx, 1000.0 * (duk_double_t) mktime(t));
            duk_new(ctx, 1);
            break;
        }
        case nil:
            duk_push_null(ctx);
            break;
    }
    duk_put_prop_string(ctx, -2, "value");
    duk_push_string(ctx, (const char *)item.string);
    duk_put_prop_string(ctx, -2, "raw");
}

CSV *openCSVstr(duk_context *ctx, duk_idx_t str_idx, int inplace)
{
    CSV  *      csv=calloc(1,sizeof(CSV));
    duk_size_t sz;
    byte *data=NULL;

    csvErrNo=0;
    
    if(!csv)
    {
       csvErrNo=errno;
       return(csv);
    }
    
    if(duk_is_buffer_data(ctx, str_idx))
    {
        data = duk_get_buffer_data(ctx, str_idx, &sz);       
    }
    else
    {
        data = (byte *)duk_get_lstring(ctx, str_idx, &sz);
    }
       
    if( duk_is_buffer_data(ctx, str_idx) && inplace)
    {

        csv->buf=data;
    }
    else
    {
        if( !(csv->buf=malloc((size_t)sz+1)) )
        {
            csvErrNo=errno;
            return(NULL);
        }
        
        memcpy(csv->buf, data, sz);
    }
    csvDefaultSettings(csv);
    csv->fileSize=(size_t)sz;
    csv->buf[csv->fileSize]='\0';    // null teminate the buffer
    removeEmptyTailSpace(csv->buf,(size_t)sz); // remove empty lines at end of file
    csv->end=csv->buf+csv->fileSize; // make it easy to check if we're at eof
    return(csv);
}



duk_ret_t duk_rp_import_csv(duk_context *ctx, int isfile)
{
    const char *filename = "";
    const char *func_name  = isfile?"csfFile":"csv"; 
    char **hnames=NULL;
    duk_idx_t  obj_idx=-1, str_idx=-1, 
               col_idx=-1, func_idx=-1, idx=0;
    CSV        *csv=NULL;
    int        row, col, start=0, 
               retType=0, hasHeader=0, normalize=0,
               inplace=0;
    putcol_t putcol = &putcol_prim;

    for (idx=0;idx<3;idx++)
    {
        if( 
            duk_is_string(ctx, idx) 
            || (!isfile && duk_is_buffer_data(ctx, idx))
        )
            str_idx=idx;
        
        else if(duk_is_function(ctx, idx))
            func_idx=idx;
        
        else if( duk_is_object(ctx, idx) )
            obj_idx=idx;
    }


#define setbool(val) do {\
if (duk_get_prop_string(ctx, obj_idx, (#val) ) ) \
    csv->val = REQUIRE_BOOL(ctx, -1, "%s(): option %s requires a Boolean", func_name, (#val) );\
duk_pop(ctx);\
} while(0)


    /* get options that do not require csv to be opened*/
    if(obj_idx>-1)
    {
        if (duk_get_prop_string(ctx, obj_idx, "returnType") )  // not consistent with rampart-sql exec(), but default is array
        {
            const char *s = REQUIRE_STRING(ctx, -1, "%s(): option retType requires a String (\"object\" or \"array\")", func_name);
            if( strcasecmp("object",s) == 0 )
                retType=1;
            else if ( strcasecmp("array", s) != 0 )
                RP_THROW(ctx, "%s(): option retType requires a String (\"object\" or \"array\")", func_name);
        }
        duk_pop(ctx);

        if (duk_get_prop_string(ctx, obj_idx, "hasHeaderRow") )
            hasHeader=REQUIRE_BOOL(ctx, -1, "%s(): option hasHeaderRow requires a Boolean", func_name);
        duk_pop(ctx);

        if (duk_get_prop_string(ctx, obj_idx, "normalize") )
            normalize=REQUIRE_BOOL(ctx, -1, "%s(): option normalize requires a Boolean", func_name);
        duk_pop(ctx);

        if (duk_get_prop_string(ctx, obj_idx, "includeRawString"))
            if (duk_get_boolean_default(ctx, -1, 0))
                putcol = &putcol_raw;
        duk_pop(ctx);

        /* not working.  csv_parse requires a null terminated string
        if (duk_get_prop_string(ctx, obj_idx, "bufferInPlace") )
        {
            if(duk_is_buffer_data(ctx, str_idx))
                inplace=1;
        }
        duk_pop(ctx);
        */
    }

    if (str_idx>-1)
    {
        if (isfile)
        {
            filename = duk_get_string(ctx, str_idx);
            csv=openCSV((char*)filename);
        }
        else
            csv=openCSVstr(ctx, str_idx, inplace);
    }
    else
    {
        if(isfile)
            RP_THROW(ctx, "%s(): string/buffer argument (csvdata) missing", func_name);

        RP_THROW(ctx, "%s(): string argument (filename) missing", func_name);
    }

    if(csv==NULL)
    {
        RP_THROW(ctx, "%s: error - %s - %s", func_name, filename, strerror(csvErrNo));
    }


    /* get options that require csv opened*/
    if(obj_idx>-1)
    {
        setbool(stripLeadingWhite);  //    boolean true    remove leading whitespace characters from cells
        setbool(stripTrailingWhite); //    boolean true    remove trailing whitespace characters from cells
        setbool(doubleQuoteEscape);  //    boolean false   '""' within strings is used to embed '"' characters
        setbool(singleQuoteNest);    //    boolean true    strings may be bounded by ' pairs and '"' characters within are ignored
        setbool(backslashEscape);    //    boolean true    characters preceded by '\' are translated and escaped
        setbool(allEscapes);         //    boolean true    all '\' escape sequences known by the 'C' compiler are translated, if false only backslash, single quote, and double quote
        setbool(europeanDecimal);    //    boolean false   numbers like '123 456,78' will be parsed as 123456.78
        setbool(tryParsingStrings);  //    boolean false   look inside quoted strings for dates and numbers to parse, if false anything quoted is a string

        if (duk_get_prop_string(ctx, obj_idx, "delimiter") ) //use the character 'c' as a column delimiter e.g \t
        {
            const char *s = REQUIRE_STRING(ctx, -1, "%s(): option delimiter requires a String (single character delimiter)", func_name);
            csv->delimiter = *s;
        }
        duk_pop(ctx);
        if (duk_get_prop_string(ctx, obj_idx, "timeFormat") ) // set the format for parsing a date/time see manpage for strptime()
            csv->timeFormat = (char *)REQUIRE_STRING(ctx, -1, "%s(): option timeFormat requires a String (strptime() style string format)", func_name);
        duk_pop(ctx);
    }

    if(parseCSV(csv)<0)      // now actually parse the file
        RP_THROW(ctx, "%s(): parse error: %s\n", func_name, csv->errorMsg);
    
    if(normalize)
        normalizeCSV(csv);        // examine each column and force data type to the majority 

    /* COLUMN NAMES */
    DUKREMALLOC(ctx, hnames, csv->cols * sizeof(char *)); 
    if (hasHeader)
    {
        for(col=0;col<csv->cols;col++)
            hnames[col] = strdup((char*) csv->item[0][col].string);

        /* check for dups */
        {
            int x, y;
            for(x=0; x<csv->cols-1; x++)
            {
                for(y=x+1; y<csv->cols; y++)
                    if (!strcmp( hnames[x], hnames[y]))
                        RP_THROW(ctx, "%s(): duplicate header column names (columns %d and %d )", func_name, x+1, y+1);  
            }
        }
    }
    else
    /* no headers, make our own */
    {
        char str[132];
        for(col=0;col<csv->cols;col++)
        {
            sprintf(str,"col_%d",col+1);
            hnames[col] = strdup (str);
        }
    }
    
    /* push column names into an array*/
    duk_push_array(ctx);
    col_idx=duk_get_top_index(ctx);
    for(col=0;col<csv->cols;col++)
    {
        duk_push_string(ctx, hnames[col]);
        duk_put_prop_index(ctx, -2, col);
    }
    /* END COLUMN NAMES */

    /* outer return object and array when no callback*/
    if(func_idx == -1)
    {
        duk_push_object(ctx);
        duk_push_array(ctx);
    }
    if (retType)
    /* return an array of object or object as first param to callback*/
    {
        /* populate rows */
        if (hasHeader) start=1;
        for(row=start;row<csv->rows;row++)      // iterate through the CSVITEMS contained in each row and column
        {
            if(func_idx > -1)
                duk_dup(ctx, func_idx);
            /* inner row object or object as first parameter to callback */
            duk_push_object(ctx);

            for(col=0;col<csv->cols;col++)
            {
                putcol(ctx, csv->item[row][col]);
                duk_put_prop_string(ctx, -2, hnames[col]);
            }

            if (func_idx > -1)
            {
                duk_push_int(ctx, row-start);
                duk_dup(ctx, col_idx);
                duk_call(ctx, 3);
                if(duk_is_boolean(ctx, -1) && ! duk_get_boolean(ctx, -1) )
                    goto funcend;
            }
            else
                duk_put_prop_index(ctx, -2, row-start);
        }

    }
    else
    /* return an array of arrays or return array as first param to callback*/
    {
        if(hasHeader) start=1;
        for(row=start;row<csv->rows;row++)      // iterate through the CSVITEMS contained in each row and column
        {
            if(func_idx > -1)
                duk_dup(ctx, func_idx);
            /* inner row array or array as first parameter to callback*/
            duk_push_array(ctx);

            for(col=0;col<csv->cols;col++)
            {
                putcol(ctx, csv->item[row][col]);
                duk_put_prop_index(ctx, -2, col);
            }
            if (func_idx > -1)
            {
                duk_push_int(ctx, row-start);
                duk_dup(ctx, col_idx);
                duk_call(ctx, 3);
                if(duk_is_boolean(ctx, -1) && ! duk_get_boolean(ctx, -1) )
                    goto funcend;
            }
            else
                duk_put_prop_index(ctx, -2, row-start);
        }
    }

    funcend:

    if(func_idx > -1)
        duk_push_int(ctx, csv->rows - start);
    else
    {
        duk_put_prop_string(ctx, -2, "results");
        duk_dup(ctx, col_idx);
        duk_put_prop_string(ctx, -2, "columns");
    }

    /* free header names */
    for(col=0;col<csv->cols;col++)
        free(hnames[col]);
    free(hnames);            
    
    /* do not free csv->buf if inplace */
    if (inplace)
        csv->buf=NULL;

    closeCSV(csv);

    return 1;
}


duk_ret_t duk_rp_import_csv_file(duk_context *ctx)
{
    return duk_rp_import_csv(ctx, 1);
}

duk_ret_t duk_rp_import_csv_str(duk_context *ctx)
{
    return duk_rp_import_csv(ctx, 0);
}


void duk_import_init(duk_context *ctx)
{
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }
    if(!duk_get_prop_string(ctx,-1,"import"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    duk_push_c_function(ctx, duk_rp_import_csv_file, 3);
    duk_put_prop_string(ctx, -2, "csvFile");

    duk_push_c_function(ctx, duk_rp_import_csv_str, 3);
    duk_put_prop_string(ctx, -2, "csv");

    duk_put_prop_string(ctx, -2,"import");
    duk_put_global_string(ctx,"rampart");

}
