/*
 * fmem.c : fmemopen() on top of BSD's funopen()
 * 20081017 AF
 *

 * from https://github.com/materialsvirtuallab/pyhull/tree/master/src/fmemopen
   added case for fmemopen(NULL,...);
   Aaron Flin - 7/18/2020
//
// Copyright 2012 Jeff Verkoeyen
// Originally ported from https://github.com/ingenuitas/python-tesseract/blob/master/fmemopen.c
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

*/

//#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef linux
struct fmem {
    size_t pos;
    size_t size;
    int ismallocd;
    char *buffer;
};
typedef struct fmem fmem_t;

static int readfn(void *handler, char *buf, int size)
{
    int count = 0;
    fmem_t *mem = handler;
    size_t available = mem->size - mem->pos;

    if(size > available) size = available;
    for(count=0; count < size; mem->pos++, count++)
        buf[count] = mem->buffer[mem->pos];

    return count;
}

static int writefn(void *handler, const char *buf, int size)
{
    int count = 0;
    fmem_t *mem = handler;
    size_t available = mem->size - mem->pos;

    if(size > available) size = available;
    for(count=0; count < size; mem->pos++, count++)
        mem->buffer[mem->pos] = buf[count];

    return count; // ? count : size;
}

static fpos_t seekfn(void *handler, fpos_t offset, int whence)
{
    size_t pos;
    fmem_t *mem = handler;

    switch(whence) {
        case SEEK_SET: pos = offset; break;
        case SEEK_CUR: pos = mem->pos + offset; break;
        case SEEK_END: pos = mem->size + offset; break;
        default: return -1;
    }

    //if(pos < 0 || pos > mem->size) return -1;
    if(pos > mem->size) return -1;
    mem->pos = pos;
    return (fpos_t) pos;
}

static int closefn(void *handler)
{
    fmem_t *mem=(fmem_t *)handler;
    if(mem->ismallocd)
        free(mem->buffer);
    free(handler);
    return 0;
}

/* simple, but portable version of fmemopen for OS X / BSD */
FILE *fmemopen(void *buf, size_t size, const char *mode)
{
    fmem_t *mem = (fmem_t *) malloc(sizeof(fmem_t));
    // bug fix: added NULL check after malloc - 2026-02-27
    if(mem==NULL)
    {
        fprintf(stderr,"fmemopen: could not alloc memory\n");
        exit(1);
    }

    memset(mem, 0, sizeof(fmem_t));
    if(buf==NULL)
    {
        buf=malloc(size);
        if(buf==NULL)
        {
            fprintf(stderr,"fmemopen: could not alloc memory\n");
            exit(1);
        }
        *((char *)buf)='\0';
        mem->ismallocd=1;
    }
    mem->size = size;
    mem->buffer = buf;
    return funopen(mem, readfn, writefn, seekfn, closefn);
}
#endif