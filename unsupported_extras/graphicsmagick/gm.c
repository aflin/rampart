/* ****************************************************************************
    rampart-gm.so module
    copyright (c) 2024 Aaron Flin
    Released under MIT license

    Compiling:
    
    > sudo apt install libgraphicsmagick1-dev
    # or macos
    > brew install graphicsmagick
    # or freebsd
    > pkg install GraphicsMagick

    > make
    # or freebsd
    > gmake

    > cp rampart-gm.so /usr/local/rampart/modules/ 
    # or wherever your modules live, like ~/.rampart/modules
    Basic usage:

    var gm = require("rampart-gm");

    // ---- OPEN ----
    // get an image object by opening image
    var images = gm.open("/path/to/my/image.jpg");
    var image2 = gm.open("/path/to/my/image2.jpg");
    // or multiple images in a single image object
    var images = gm.open(["/path/to/my/image.jpg", "/path/to/my/image2.jpg"]);

    // ---- ADD ----
    // image.add([ imageObject | String(path) | buffer(i.e. img.toBuffer() ])
    // add some image
    images.add("/path/to/my/image2.jpg");   //or
    images.add(image2);                     //or
    images.add(image2.toBuffer('JPG'));     //or
    images.add([image2, "/path/to/my/image3.jpg"]);

    // ---- MOGRIFY ---
    //mogrify (see GraphicsMagick for all options)
    //if object used, single options must be paired with true
    images.mogrify("-blur 20x30");      //or
    images.mogrify("-blur", "20x30");   //or
    images.mogrify("blur", "20x30");    //or
    images.mogrify({"-blur": "20x30"});   //or
    images.mogrify({"blur": "20x30"});

    //example 2:
    images.mogrify("-blur 20x30 -auto-orient +contrast");      //or
    images.mogrify({
        blur: "20x30",
        "auto-orient": true,
        "+contrast": true
    });
    // note setting single options to false skips the option and does nothing

    // ---- SAVE ----
    images.save("/path/to/my/new_image.jpg");

    // ---- TOBUFFER ----
    // save file to a js buffer in the specified format
    var buf = images.toBuffer(["jpg" | "PNG" | "GIF" | ... ]);

    // So this now accomplishes the same as save above
    rampart.utils.fprintf("/path/to/my/new_image.jpg", "%s", buf);

    // Or using rampart-server, return it to client:
    return {jpg: buf}

    // ---- SELECT ----
    //select an image to save in a multi-image document when 
    //not saving to a format that supports it
    images.select(1); //or
    images.select(-1); //last image, negative indexes work      
    image.save("/path/to/my/new_image.jpg");

    // ---- LIST ----
    //get a list of images in the image object
     rampart.utils.printf("%3J\n", images.list());

    // ---- GETCOUNT ----
    //get a count of images in the image object
     rampart.utils.printf("we have %d images\n", images.getCount());
    
    // ---- IDENTIFY ----
    // get simple description Object of current img like "gm identify img.jpg" 
     rampart.utils.printf( "%s\n", images.select(0).identify() );
    // get more detail
     rampart.utils.printf( "%3J\n", images.select(0).identify(true) )

    // ---- CLOSE ----
    // optionally close and free resources
    images.close();
    // otherwise it is automatically closed
    // when var images goes out of scope

    // ----- Make an animaged gif -----
    var images = gm.open('image1.jpg')
        .mogrify({'auto-orient': true}) //chainable
        .mogrify({ 'delay' : 20}); //this frame gets 200ms
    var image2 = gm.open('image2.jpg')
        .mogrify({'auto-orient': true, //or all at once
                  'delay' : 60});//this frame gets 600ms
    images.add(image2);
    images.mogrify({"loop":5});//stop after 5 loops
    image.save("animated.gif");


**************************************************************************** */



#define _GNU_SOURCE
//#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include "/usr/local/rampart/include/rampart.h"
#include <wand/magick_wand.h>



//static void throw_exception(duk_context *ctx, MagickWand *wand, int freewand)
#define throw_exception(ctx, wand, freewand) do {\
    char *description=NULL;\
    ExceptionType severity=0;\
\
    description=MagickGetException(wand,&severity);\
    if( (description == NULL) || (strlen(description) == 0) ) \
        duk_push_error_object(ctx, DUK_ERR_ERROR, "unknown error");\
    else\
        duk_push_error_object(ctx, DUK_ERR_ERROR, description);\
\
    if(description)\
        free(description);\
\
    if(freewand)\
        DestroyMagickWand(wand);\
\
    (void) duk_throw(ctx);\
} while(0)

static int push_mog_exception(duk_context *ctx, const ExceptionInfo *exception)
{
    if (!exception->reason)
        return 0;

    // skip this one
    if (/*exception->severity==430 || check this */ strstr(exception->reason,"Unable to open file") )
    { 
        return 0;
    }

    if (strstr(exception->reason,"%s") && exception->description)
    {
        duk_push_sprintf(ctx, exception->reason, exception->description);
    }
    else
    {
        if(exception->description)
            duk_push_sprintf(ctx, "%s (%s)", exception->reason, exception->description);    
        else
            duk_push_string(ctx, exception->reason);
    }
    duk_push_error_object(ctx, DUK_ERR_ERROR, duk_get_string(ctx, -1));

    return 1;
}
/* from magic_wand.c - if they change it, this will explode */
typedef struct _rpMagickWand
{
  char
    id[MaxTextExtent];

  ExceptionInfo
    exception;

  ImageInfo  
    *image_info;

  QuantizeInfo
    *quantize_info;

  Image
    *image,             /* Current working image */
    *images;            /* Whole image list */

  unsigned int
    iterator;

  unsigned long
    signature;
} rpMW;

static unsigned int getcount(MagickWand *wand)
{
    unsigned int n=0;
    rpMW *rwand = (rpMW *) wand;
    Image *image;

    if(!rwand->images)
        return 0;

    for (image=rwand->images; image; image=image->next)
        n++;

    return n;
}

static void free_arguments(char **argv, int argc, char *argstr, int freeargs)
{
    if(freeargs)
    {
        int i=0;
        for (;i<argc;i++){
            if(argv[i])
                free(argv[i]);
        }
    }
    if(argstr)
        free(argstr);
    if(argv)
        free(argv);
}

static duk_ret_t mogrify(duk_context *ctx)
{
    MagickWand *wand;
    int ret, argc=0, freeargs=0;
    char **argv=NULL, *argstr=NULL;

    duk_push_this(ctx);// idx = 2;

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "Internal error getting gm wand");
    wand = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!wand)
        RP_THROW(ctx, "gm - error using a closed image handle");

    if(duk_is_string(ctx,0) && duk_is_undefined(ctx,1))
    {
        char *p;
        int nargs=0, i=1;

        argstr=strdup(duk_get_string(ctx,0));
        p=argstr;

        while(*p==' ') p++;
        while(*p)
        {
            if(!p)
                break;
            p++;
            if(*p==' ' || !*p)
            {
                while(*p==' ') p++;
                nargs++;
            }
        }
        argc=nargs;
        if(!argc)
        {
            free(argstr);
            RP_THROW(ctx, "gm.mogrify - no parsable options");
            return 1;
        }
        REMALLOC(argv, (nargs + 1) * sizeof(char*) );
        p=argstr;

        while(*p==' ') p++;
        argv[0]=p;
        while(*p)
        {
            if(*p == ' ')
            {
                *p='\0';
                p++;
                while(*p==' ') p++;
                if(*p=='\0')
                    break;
                argv[i]=p;
                i++;
            }
            p++;
        }
    }
    else if(duk_is_string(ctx,0) && (duk_is_string(ctx,1)|| duk_is_boolean(ctx,1) || duk_is_number(ctx, 1)) )
    {
        const char *arg = duk_get_string(ctx,0);
        char *argv0=NULL;

        if(*arg != '-' && *arg != '+')
        {
            REMALLOC(argv0, strlen(arg)+2);
            sprintf(argv0, "-%s", arg);
        }
        else
            argv0=strdup(arg);

        if(duk_is_boolean(ctx,1))
        {
            if(!duk_get_boolean(ctx,1))
            {
                free(argv0);
                return 1; //do nothing
            }
            argc=1;
            REMALLOC(argv, (argc+1) * sizeof(char*) );
        }
        else
        {
            argc=2;
            REMALLOC(argv, (argc+1) * sizeof(char*) );
            if(duk_is_number(ctx, 1))
            {
                duk_dup(ctx, 1);
                duk_to_string(ctx, -1);
                argv[1]=strdup(duk_get_string(ctx,-1));
                duk_pop(ctx);
            }
            else
                argv[1]=strdup(duk_get_string(ctx,1));        
        }
        freeargs=1;
        argv[0]=argv0;
    }
    else if (duk_is_object(ctx, 0))
    {
        int len=0, i=0;
        const char *arg;

        duk_enum(ctx,0,0);
        while(duk_next(ctx, -1, 0))
        {
            len++;
            duk_pop(ctx);
        }
        duk_pop(ctx);

        len*=2;
        REMALLOC(argv, (len+1) * sizeof(char*) );
        for(i=0;i<len;i++)
            argv[i]=NULL;

        duk_enum(ctx,0,0);
        while(duk_next(ctx, -1, 1))
        {
            if(duk_is_string(ctx, -2) && (duk_is_string(ctx,-1)|| duk_is_boolean(ctx,-1)||duk_is_number(ctx,-1)))
            {
                char *argv0=NULL;

                arg = duk_get_string(ctx,-2);
                if(*arg != '-' && *arg != '+')
                {
                    REMALLOC(argv0, strlen(arg)+2);
                    sprintf(argv0, "-%s", arg);
                }
                else
                    argv0=strdup(arg);

                if(duk_is_boolean(ctx,-1))
                {
                    if(!duk_get_boolean(ctx,-1))
                    {
                        free(argv0);
                        duk_pop_2(ctx);
                        continue;
                    }
                    argv[argc++]=argv0;
                }
                else
                {
                    argv[argc++]=argv0;
                    if(duk_is_number(ctx, -1))
                    {
                        duk_dup(ctx, -1);
                        duk_to_string(ctx, -1);
                        argv[argc++]=strdup(duk_get_string(ctx,-1));
                        duk_pop(ctx);
                    }
                    else
                        argv[argc++]=strdup(duk_get_string(ctx,-1));        
                }
            }
            duk_pop_2(ctx);
        }
        freeargs=1;
        duk_pop(ctx);
    }

    if(argc)
    {
        rpMW *rwand=(rpMW *)wand;
        char *text=(char *) NULL;
        Image *image;
        unsigned int save, count, lastcount;

        save  = MagickGetImageIndex(wand);
        count = getcount(wand);

        //mogrifyimagecommand expects a filename at the end.
        //We will still get an error since file doesnt exits, 
        //but we would get a different error if we have too
        //few args.
        argv[argc]="fake.jpg";

        //this checks arguments for errors (but skip the "unable to open file" error)
        ret = MogrifyImageCommand(rwand->image_info, argc+1, argv, &text, &(rwand->exception));
        if(!ret)
        {
            if(push_mog_exception(ctx, &(rwand->exception)))
            {
                free_arguments(argv, argc, argstr, freeargs);
                MagickSetImageIndex(wand,save);
                (void) duk_throw(ctx);
            }
        }
        argv[argc]=NULL;

        ret = MogrifyImages(rwand->image_info, argc, argv, &(rwand->images));
        lastcount=getcount(wand);

        //check if number of images has changed.
        if(count!=lastcount)
        {
            unsigned int j;
            save=0;
            
            duk_push_this(ctx);
            duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("files"));
            duk_remove(ctx, -2);//this
            if(lastcount == 1) //the most likely
            {
                duk_push_string(ctx, "[rendered image]");
                duk_put_prop_index(ctx,-2, 0);
            }
            //we need to remove image names from the list
            if(lastcount < count)
                duk_set_length(ctx, -1, lastcount);
            // or add something
            for (j=count; j<lastcount; j++)
            {
                duk_push_string(ctx, "[new image]");
                duk_put_prop_index(ctx, -2, j);
            }
            duk_set_length(ctx, -1, lastcount);
            duk_pop(ctx);//DUK_HIDDEN_SYMBOL("files")
        }

        // check for errors from mogrifyimages
        if(!ret)
        {
            for (image=rwand->images; image; image=image->next)
            {
                if(push_mog_exception(ctx, &(image->exception)))
                {
                    free_arguments(argv, argc, argstr, freeargs);
                    MagickSetImageIndex(wand,save);
                    (void) duk_throw(ctx);
                }
            }
        }

        // reset wand->image in struct
        MagickSetImageIndex(wand,save);
    }
    else
    {
        free_arguments(argv, argc, argstr, freeargs);
        RP_THROW(ctx, "gm.mogrify - no parsable options");
        return 1;
    }

    free_arguments(argv, argc, argstr, freeargs);

    return 1;//this
}

static duk_ret_t save(duk_context *ctx)
{
    MagickWand *wand, *cwand;
    const char *fn = REQUIRE_STRING(ctx, 0, "gm.save requires a string as it's sole argument");
    char *ext=NULL;
    MagickPassFail status = MagickPass;

    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "Internal error getting gm wand");
    wand = duk_get_pointer(ctx, -1);
    duk_pop(ctx);


    if(!wand)
        RP_THROW(ctx, "gm - error using a closed image handle");

    //MagickSetImageIndex(wand,0);
    ext = strrchr(fn,'.');
    
    /*if(MagickNextImage(wand))*/
    if(ext && !strcasecmp(".gif", ext) && getcount(wand)>1)
    {
        cwand = MagickCoalesceImages(wand);
        if(!cwand)
            throw_exception(ctx, wand, 0);

        status = MagickWriteImages(cwand, fn, MagickTrue);

        DestroyMagickWand(cwand);
    }
    else
        status = MagickWriteImage(wand, fn);

    if(status != MagickPass)
        throw_exception(ctx, wand, 0);

    return 1;
}

static duk_ret_t tobuffer(duk_context *ctx)
{
    MagickWand *wand, *cwand;
    unsigned char *buf, *dukbuf;
    size_t len;
    unsigned int ret;
    const char *fmt = REQUIRE_STRING(ctx, 0, "gm.toBuffer requires a string as it's sole argument (image type)");

    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "Internal error getting gm wand");
    wand = duk_get_pointer(ctx, -1);

    if(!wand)
        RP_THROW(ctx, "gm - error using a closed image handle");

    ret=MagickSetImageFormat(wand, fmt);
    if(!ret)
        throw_exception(ctx, wand, 0);

    //MagickSetImageIndex(wand,0);

    //if(MagickNextImage(wand))
    if(!strcasecmp("gif", fmt) && getcount(wand)>1)
    {
        cwand = MagickCoalesceImages(wand);
        if(!cwand)
            throw_exception(ctx, wand, 0);

        ret=MagickSetImageFormat(cwand, fmt);
        if(!ret)
            throw_exception(ctx, cwand, 1);

        buf = MagickWriteImageBlob(cwand, &len);
        DestroyMagickWand(cwand);
    }
    else
        buf = MagickWriteImageBlob(wand, &len);        

    dukbuf = duk_push_fixed_buffer(ctx, (duk_size_t)len);
    memcpy(dukbuf,buf,len);
    free(buf);

    return 1;
}

static duk_ret_t gmfinal(duk_context *ctx)
{
    MagickWand *wand;
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand") );
    wand=duk_get_pointer(ctx, -1);

    if(!wand)
        return 0;

    DestroyMagickWand(wand);
    duk_push_pointer(ctx, NULL);
    duk_put_prop_string(ctx, -3, DUK_HIDDEN_SYMBOL("wand"));
    return 0;
}

static duk_ret_t gmclose(duk_context *ctx)
{
    duk_push_this(ctx);
    return gmfinal(ctx);
}


static void push_file_names(duk_context *ctx, duk_idx_t files_idx, const char *filename, unsigned int count)
{
    duk_uarridx_t len = duk_get_length(ctx, files_idx);
    int inum=0;
    if(!count)
        return;

    duk_push_string(ctx, filename);

    if(count==1)
    {
        duk_push_string(ctx, filename);
        duk_put_prop_index(ctx, files_idx, len);
        return;
    }
    while(count--)
    {
        duk_push_sprintf(ctx,"%s[%d]", filename, inum);
        duk_put_prop_index(ctx, files_idx, len);
        len++;
        inum++;
    }
    duk_pop(ctx);
}

static unsigned int addbuffer(duk_context *ctx, MagickWand *wand, 
    duk_idx_t buff_idx, duk_idx_t files_idx)
{
    duk_size_t sz;
    unsigned char *blob = duk_get_buffer_data(ctx, 0, &sz);
    unsigned int cur,
        prev=getcount(wand),
        res = MagickReadImageBlob(wand, blob, (size_t)sz);

    if(!res)
        return res;

    cur = getcount(wand);
    push_file_names(ctx, files_idx, "[buffer]", cur-prev);

    return res;
}

static unsigned int addfilename(duk_context *ctx, MagickWand *wand, 
    const char *file, duk_idx_t files_idx)
{
    unsigned int cur,
        prev=getcount(wand),
        res = MagickReadImage(wand, file);
    char *rpath;

    if(!res)
        return res;

    cur = getcount(wand);

    rpath=realpath(file, NULL);
    if(rpath)
    {
        push_file_names(ctx, files_idx, rpath, cur-prev);
        free(rpath);
    }
    else
        push_file_names(ctx, files_idx, file, cur-prev);

    return res;
}

static unsigned int addhandle(duk_context *ctx, MagickWand *wand, duk_idx_t idx, duk_idx_t files_idx)
{
    MagickWand *add;
    unsigned int save, wsave, res;
    duk_uarridx_t len, flen, i=0;

    if(!duk_get_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "gm.add/gm.open - argument must be an image opened with gm.open");

    add = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!add)
        RP_THROW(ctx, "gm.add - cannot add, image has been closed");

    save = MagickGetImageIndex(add);
    MagickSetImageIndex(add,0);
    wsave = MagickGetImageIndex(wand);

    while(MagickNextImage(wand)); //this advances one past the last, presumably to the end of a list where there's a NULL

    // copy all images in add, append to wand
    do {
        res = MagickAddImage(wand, add); //this doesn't add an image, it replaces the one at the current index.
        if(!res)
            return res;
        MagickNextImage(wand);
    } while(MagickNextImage(add));

    // reset image positions
    MagickSetImageIndex(add, save);
    MagickSetImageIndex(wand, wsave);

    // copy filenames
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("files"));
    len=duk_get_length(ctx, -1);
    flen=duk_get_length(ctx, files_idx);
    while (i<len)
    {
        duk_get_prop_index(ctx, -1, i);
        duk_put_prop_index(ctx, files_idx, flen);
        flen++;
        i++;
    }
    return res;
}

static duk_ret_t list(duk_context *ctx)
{
    duk_uarridx_t len, i=0;

    duk_push_this(ctx);//0
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("files"));//1
    duk_push_array(ctx);//2

    len=duk_get_length(ctx, 1);
    while(len > i)
    {
        duk_get_prop_index(ctx, 1, i);
        duk_put_prop_index(ctx, 2, i);
        i++;
    }
    return 1;
}

static duk_ret_t gmselect(duk_context *ctx)
{
    MagickWand *wand;
    unsigned int res=0;
    int index = REQUIRE_INT(ctx, 0, "gm.select requires an int (image index)");

    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "Internal error getting gm wand");
    wand = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!wand)
        RP_THROW(ctx, "gm - error using a closed image handle");

    if(index < 0)
    {
        index = getcount(wand) + index;
    }

    if(index < 0)
        RP_THROW(ctx, "gm.select - could not select index %d of %d (out of range?)", index, (int)getcount(wand));

    res = MagickSetImageIndex(wand, index);
    if(!res)
        RP_THROW(ctx, "gm.select - could not select index %d of %d (out of range?)", index, (int)getcount(wand));

    return 1; //this
}

/*
#define DESC_INDENT_CHARS 2

static void push_parsed_desc(duk_context *ctx, char *buf, size_t blen)
{
    char *p2, *p3, *p=buf, *end=buf+blen, *key, *val;
    size_t indent=0, lastindent=0, nextindent=0;
    duk_idx_t top;
    duk_push_object(ctx);

    top = duk_get_top(ctx);
    while(p<end)
    {
        if(*p=='\0')
        {
            duk_set_top(ctx, top);
            return;
        }
        indent=0;
        while(*p==' ')
        {
            p++;
            indent++;
        }
        //the key
        key=p;
        //the end of the key
        while(*p && *p!='\n' && *p!=':') p++;

        if(!*p)
            RP_THROW(ctx, "gm.identify() parse error");

        if(*p=='\n')  //this should never happen
            continue;

        //  we are at ':'

        //sub category close
        while(lastindent > indent)
        { 
            duk_pop(ctx);
            lastindent -= DESC_INDENT_CHARS;
        }

        //terminate key
        *p='\0';

        //fix up key
        if(islower(*(key+1))) // leave JPEG alone
            *key=tolower(*key);
        p3=p2=key;
        do
        {
            if(*p2==' ' || *p2=='-')
            {
                p2++;
                *p3=toupper(*p2);
                continue;
            }
            *p3=*p2;
            p3++;
            p2++;
        } while(*(p2-1));

        p++;
        while(*p==' ')p++;

        if(*p=='\n' )
        //no associated value on this line, next indent will be greater than cur
        //we are skipping the check for nextindent > indent; so far not necessary
        {
            //sub category, store object under key now.
            //leave ref to object on stack
            duk_push_object(ctx);
            duk_dup(ctx, -1);
            duk_put_prop_string(ctx, -3, key);
            p++;
            lastindent=indent;
            continue;
        }
        
        val=p;
        while(*p && *p!='\n') p++;
        if(!*p)
            RP_THROW(ctx, "gm.identify() parse error");
        *p='\0';
        //check if its a subcat and has a value (like case EXIF does)
        //if so, next indent will be greater than cur;
        nextindent=0;
        p2=p+1;
        while(*p2==' '){
            p2++;
            nextindent++;
        }
        if(nextindent > indent)
        {
            duk_push_object(ctx);
            duk_dup(ctx, -1);
            duk_put_prop_string(ctx, -3, key);
            double d = strtod(val, &p2);
            if(*p2=='\0')
                duk_push_number(ctx, d);
            else
                duk_push_string(ctx, val);
            duk_put_prop_string(ctx, -2, "Value");
        }
        else
        {
            double d = strtod(val, &p2);
            if(*p2=='\0')
                duk_push_number(ctx, d);
            else
                duk_push_string(ctx, val);
            duk_put_prop_string(ctx, -2, key);
        }
        p++;
        lastindent=indent;
    }
    duk_set_top(ctx, top);

}
*/
// REPURPOSED magick/describe.c to directly make a js object
#define True 1
#define False 0
#define Min(x,y)  (((x) < (y)) ? (x) : (y))

extern MagickExport const char *OrientationTypeToString(const OrientationType orientation_type) MAGICK_FUNC_CONST;
MagickExport const char *CompositeOperatorToString(const CompositeOperator composite_op);
MagickExport const char* CompressionTypeToString(const CompressionType compression_type);

static void rp_describe_image(duk_context *ctx, Image *image, const MagickBool verbose)
{
    char color[MaxTextExtent];
    const ImageAttribute *attribute;
    const unsigned char *profile;
    double elapsed_time, user_time;
    size_t profile_length;
    unsigned long columns, rows;
    magick_int64_t pixels_per_second;
    Image *p;
    unsigned long y;
    register size_t i;
    register unsigned long x;
    unsigned long count;

    duk_idx_t idx=duk_get_top(ctx);

    elapsed_time=GetElapsedTime(&image->timer);
    user_time=GetUserTime(&image->timer);
    GetTimerInfo(&image->timer);

    duk_push_object(ctx);

    /*
      Display summary info about the image.
    */
    if (*image->magick_filename != '\0')
      if (LocaleCompare(image->magick_filename,image->filename) != 0)
      {
          duk_push_string(ctx, image->magick_filename);
          duk_put_prop_string(ctx, idx, "magickFilename");
      }

    duk_push_string(ctx, image->filename);
    duk_put_prop_string(ctx, idx, "filename");

    if (image->scene != 0)
    {
        duk_push_int(ctx, (int)image->scene);
        duk_put_prop_string(ctx, idx, "scene");
    }

    p=image;
    while (p->previous != (Image *) NULL)
      p=p->previous;
    for (count=1; p->next != (Image *) NULL; count++)
      p=p->next;
    if (count > 1)
    {
        duk_push_int(ctx, count);
        duk_put_prop_string(ctx, -2, "sceneCount");
    }

    duk_push_string(ctx, image->magick);
    duk_put_prop_string(ctx, idx, "magick");

    columns=image->columns;
    rows=image->rows;
    if ((image->magick_columns != 0) || (image->magick_rows != 0))
        if ((image->magick_columns != image->columns) ||
            (image->magick_rows != image->rows))
        {
            columns=image->magick_columns;
            rows=image->magick_rows;
            duk_push_int(ctx, (int) image->magick_columns);
            duk_put_prop_string(ctx, idx, "columns");
            duk_push_int(ctx, (int) image->magick_rows);
            duk_put_prop_string(ctx, idx, "rows");
        }
    duk_push_int(ctx, (int) image->columns);
    duk_put_prop_string(ctx, idx, "width");
    duk_push_int(ctx, (int) image->rows);
    duk_put_prop_string(ctx, idx, "height");

    if (image->storage_class == DirectClass)
    {
        duk_push_true(ctx);
        duk_put_prop_string(ctx, idx, "directClass");
        if (image->total_colors != 0)
        {
            duk_push_int(ctx, image->total_colors);
            duk_put_prop_string(ctx, idx, "totalColors");
        }
    }
    else
    {
        if (image->total_colors <= image->colors)
        {
            duk_push_true(ctx);
            duk_put_prop_string(ctx, idx, "pseudoClass");
        }
        else
        {
            duk_push_true(ctx);
            duk_put_prop_string(ctx, idx, "pseudoClass");
            duk_push_int(ctx, (int) image->total_colors);
            duk_put_prop_string(ctx, idx, "totalColors");
            duk_push_int(ctx, (int) image->error.mean_error_per_pixel);
            duk_put_prop_string(ctx, idx, "meanErrorPerPixel");
            duk_push_int(ctx, (int) image->error.normalized_mean_error);
            duk_put_prop_string(ctx, idx, "normalizedMeanError");
            duk_push_int(ctx, (int) image->error.normalized_maximum_error);
            duk_put_prop_string(ctx, idx, "normalizedMaximumError");
        }
    }

    duk_push_int(ctx, (int) image->depth);
    duk_put_prop_string(ctx, idx, "depth");

    if (GetBlobSize(image) != 0)
    {
        duk_push_int(ctx, (int) GetBlobSize(image));
        duk_put_prop_string(ctx, idx, "size");
    }

    duk_push_number(ctx, (double) user_time);
    duk_put_prop_string(ctx, idx, "userTime");
    duk_push_number(ctx, (double) elapsed_time);
    duk_put_prop_string(ctx, idx, "elapsedTime");

    /*
      Only display pixel read rate if the time accumulated is at
      least six times the timer's resolution (typically 0.01 on
      Unix).
    */
    if (!(image->ping) && (elapsed_time >= GetTimerResolution()*6))
    {
        pixels_per_second=(magick_int64_t) ((double) rows*columns/
                                            elapsed_time);
        duk_push_number(ctx, (double)pixels_per_second);
        duk_put_prop_string(ctx, idx, "pixelsPerSecond");
    }

    if (!verbose)
       return;

    switch (GetImageType(image,&image->exception))
    {
        case BilevelType: duk_push_string(ctx, "bilevel"); break;
        case GrayscaleType: duk_push_string(ctx, "grayscale"); break;
        case GrayscaleMatteType:
            duk_push_string(ctx, "grayscale with transparency"); break;
        case PaletteType: duk_push_string(ctx, "palette"); break;
        case PaletteMatteType:
            duk_push_string(ctx, "palette with transparency"); break;
        case TrueColorType: duk_push_string(ctx, "true color"); break;
        case TrueColorMatteType:
            duk_push_string(ctx, "true color with transparency"); break;
        case ColorSeparationType: duk_push_string(ctx, "color separated"); break;
        case ColorSeparationMatteType:
            duk_push_string(ctx, "color separated with transparency"); break;
        default: duk_push_string(ctx, "undefined"); break;
    }
    duk_put_prop_string(ctx, idx, "type");

    duk_push_object(ctx);
    if (image->colorspace == CMYKColorspace)
    {
        duk_push_int(ctx, (int)GetImageChannelDepth(image, CyanChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "cyan");

        duk_push_int(ctx, (int)GetImageChannelDepth(image, MagentaChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "magenta");

        duk_push_int(ctx, (int)GetImageChannelDepth(image, YellowChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "yellow");

        duk_push_int(ctx, (int)GetImageChannelDepth(image, BlackChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "black");
    }
    else if ((IsGrayColorspace(image->colorspace)) ||
             (image->is_grayscale))
    {
        duk_push_int(ctx, (int)GetImageChannelDepth(image, RedChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "gray");
    }
    else
    {
        duk_push_int(ctx, (int)GetImageChannelDepth(image, RedChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "red");

        duk_push_int(ctx, (int)GetImageChannelDepth(image, GreenChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "green");

        duk_push_int(ctx, (int)GetImageChannelDepth(image, BlueChannel, &image->exception));
        duk_put_prop_string(ctx, -2, "blue");
    }
    duk_put_prop_string(ctx, idx, "channelDepths");

    if (image->matte)
    {
        duk_push_int(ctx, (int) GetImageChannelDepth(image, OpacityChannel, &image->exception));
        duk_put_prop_string(ctx, idx, "opacityBits"); 
    }

    duk_push_object(ctx); //Channel Statistics
    {
        ImageStatistics statistics;

        (void) GetImageStatistics(image,&statistics,&image->exception);

        duk_push_number(ctx, MaxRGB);
        duk_put_prop_string(ctx, -2, "channelMax");

      if (image->colorspace == CMYKColorspace)
        {
            duk_push_object(ctx);//cyan

            duk_push_number(ctx, (double)statistics.red.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.red.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.red.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.red.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "cyan");

            duk_push_object(ctx);//magenta

            duk_push_number(ctx, (double)statistics.green.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.green.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.green.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.green.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "magenta");

            duk_push_object(ctx);//yellow
            duk_push_number(ctx, (double)statistics.blue.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.blue.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.blue.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.blue.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "yellow");

            duk_push_object(ctx);//black

            duk_push_number(ctx, (double)statistics.opacity.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.opacity.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.opacity.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.opacity.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "black");

        }
        else if ((IsGrayColorspace(image->colorspace)) ||
             (image->is_grayscale == MagickTrue))
        {
            duk_push_object(ctx);//gray

            duk_push_number(ctx, (double)statistics.red.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.red.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.red.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.red.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "gray");

            if (image->matte)
            {
                duk_push_object(ctx);//opacity

                duk_push_number(ctx, (double)statistics.opacity.minimum);
                duk_put_prop_string(ctx, -2, "minimum");

                duk_push_number(ctx, (double)statistics.opacity.maximum);
                duk_put_prop_string(ctx, -2, "maximum");

                duk_push_number(ctx, (double)statistics.opacity.mean);
                duk_put_prop_string(ctx, -2, "mean");

                duk_push_number(ctx, (double)statistics.opacity.standard_deviation);
                duk_put_prop_string(ctx, -2, "standardDeviation");
                duk_put_prop_string(ctx, -2, "opacity");
            }
        }
        else
        {

            duk_push_object(ctx);//red

            duk_push_number(ctx, (double)statistics.red.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.red.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.red.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.red.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "red");

            duk_push_object(ctx);//green

            duk_push_number(ctx, (double)statistics.green.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.green.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.green.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.green.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "green");

            duk_push_object(ctx);//blue

            duk_push_number(ctx, (double)statistics.blue.minimum);
            duk_put_prop_string(ctx, -2, "minimum");

            duk_push_number(ctx, (double)statistics.blue.maximum);
            duk_put_prop_string(ctx, -2, "maximum");

            duk_push_number(ctx, (double)statistics.blue.mean);
            duk_put_prop_string(ctx, -2, "mean");

            duk_push_number(ctx, (double)statistics.blue.standard_deviation);
            duk_put_prop_string(ctx, -2, "standardDeviation");
            duk_put_prop_string(ctx, -2, "blue");

            if (image->matte)
            {
                duk_push_object(ctx);//opacity

                duk_push_number(ctx, (double)statistics.opacity.minimum);
                duk_put_prop_string(ctx, -2, "minimum");

                duk_push_number(ctx, (double)statistics.opacity.maximum);
                duk_put_prop_string(ctx, -2, "maximum");

                duk_push_number(ctx, (double)statistics.opacity.mean);
                duk_put_prop_string(ctx, -2, "mean");

                duk_push_number(ctx, (double)statistics.opacity.standard_deviation);
                duk_put_prop_string(ctx, -2, "standardDeviation");
                duk_put_prop_string(ctx, -2, "opacity");
            }
        }
        duk_put_prop_string(ctx, -2, "channelStatistics");
    }
    x=0;
    p=(Image *) NULL;
    if ((image->matte && (strcmp(image->magick,"GIF") != 0)) || image->taint)
    {
        char
          tuple[MaxTextExtent];

        MagickBool
          found_transparency;

        register const PixelPacket *p;

        p=(PixelPacket *) NULL;
        found_transparency = MagickFalse;
        for (y=0; y < image->rows; y++)
        {
            p=AcquireImagePixels(image,0,y,image->columns,1,&image->exception);
            if (p == (const PixelPacket *) NULL)
                break;
            for (x=0; x < image->columns; x++)
            {
                if (p->opacity == TransparentOpacity)
                {
                    found_transparency=MagickTrue;
                    break;
                }
                p++;
            }
            if (x < image->columns)
                break;
        }
        if (found_transparency)
        {
            GetColorTuple(p,image->depth,image->matte,False,tuple);
            duk_push_sprintf(ctx, "%.1024s ", tuple);
            GetColorTuple(p,image->depth,image->matte,True,tuple);
            duk_push_sprintf(ctx, "%.1024s", tuple);
            duk_concat(ctx, 2);
            duk_put_prop_string(ctx, -2, "opacity");
        }
    }

    if (image->storage_class != DirectClass)
    {
        char name[MaxTextExtent];

        register PixelPacket
          *p;

        /*
          Display image colormap.
        */
        p=image->colormap;
        duk_push_array(ctx);
        duk_push_array(ctx);
        for (i=0; i < image->colors; i++)
        {
            char
              tuple[MaxTextExtent];

            GetColorTuple(p,image->depth,image->matte,False,tuple);
            tuple[0]='[';
            tuple[strlen(tuple)-1]=']';
            duk_push_string(ctx, tuple);
            duk_json_decode(ctx, -1);
            duk_put_prop_index(ctx, -2, i);
            
            
            QueryColorname(image,p,SVGCompliance,name,&image->exception);
            duk_push_string(ctx, name);
            duk_put_prop_index(ctx, -3, i);
            p++;
        }
        duk_put_prop_string(ctx, -3, "colormap");
        duk_put_prop_string(ctx, -2, "hashColormap");
    }

    if (image->error.mean_error_per_pixel != 0.0)
    {
        duk_push_number(ctx, (double) image->error.mean_error_per_pixel);
        duk_put_prop_string(ctx, -2, "meanErrorPerPixel");
    }

    if (image->error.normalized_mean_error != 0.0)
    {
        duk_push_number(ctx, (double) image->error.normalized_mean_error);
        duk_put_prop_string(ctx, -2, "normalizedMeanError");
    }

    if (image->error.normalized_maximum_error != 0.0)
    {
        duk_push_number(ctx, (double) image->error.normalized_maximum_error);
        duk_put_prop_string(ctx, -2, "normalizedMaximumError");
    }

    if (image->rendering_intent == SaturationIntent)
        duk_push_string(ctx, "saturation");
    else
        if (image->rendering_intent == PerceptualIntent)
            duk_push_string(ctx, "perceptual");
        else
            if (image->rendering_intent == AbsoluteIntent)
                duk_push_string(ctx, "absolute");
        else
            if (image->rendering_intent == RelativeIntent)
                duk_push_string(ctx,"relative");

    if(duk_is_string(ctx, -1))
        duk_put_prop_string(ctx, -2, "renderingIntent");

    if (image->gamma != 0.0)
    {
        duk_push_number(ctx, image->gamma);
        duk_put_prop_string(ctx, -2, "gamma");
    }
    if ((image->chromaticity.red_primary.x != 0.0) ||
        (image->chromaticity.green_primary.x != 0.0) ||
        (image->chromaticity.blue_primary.x != 0.0) ||
        (image->chromaticity.white_point.x != 0.0))
    {

        duk_push_object(ctx);//chromacity

        duk_push_object(ctx);//redPrimary
        duk_push_number(ctx, image->chromaticity.red_primary.x);
        duk_put_prop_string(ctx, -2, "x");
        duk_push_number(ctx, image->chromaticity.red_primary.y);
        duk_put_prop_string(ctx, -2, "y");
        duk_put_prop_string(ctx, -2, "redPrimary");

        duk_push_object(ctx);//greenPrimary
        duk_push_number(ctx, image->chromaticity.green_primary.x);
        duk_put_prop_string(ctx, -2, "x");
        duk_push_number(ctx, image->chromaticity.green_primary.y);
        duk_put_prop_string(ctx, -2, "y");
        duk_put_prop_string(ctx, -2, "greenPrimary");

        duk_push_object(ctx);//bluePrimary
        duk_push_number(ctx, image->chromaticity.blue_primary.x);
        duk_put_prop_string(ctx, -2, "x");
        duk_push_number(ctx, image->chromaticity.blue_primary.y);
        duk_put_prop_string(ctx, -2, "y");
        duk_put_prop_string(ctx, -2, "bluePrimary");

        duk_push_object(ctx);//whitePoint
        duk_push_number(ctx, image->chromaticity.white_point.x);
        duk_put_prop_string(ctx, -2, "x");
        duk_push_number(ctx, image->chromaticity.white_point.y);
        duk_put_prop_string(ctx, -2, "y");
        duk_put_prop_string(ctx, -2, "white_point");

        duk_put_prop_string(ctx, -2, "chromacity");
    }

    if ((image->tile_info.width*image->tile_info.height) != 0)
    {
        duk_push_object(ctx);//tileGeometry

        duk_push_int(ctx, (int)image->tile_info.width);
        duk_put_prop_string(ctx, -2, "width");

        duk_push_int(ctx, (int)image->tile_info.height);
        duk_put_prop_string(ctx, -2, "height");

        duk_push_int(ctx, (int)image->tile_info.x);
        duk_put_prop_string(ctx, -2, "x");

        duk_push_int(ctx, (int)image->tile_info.y);
        duk_put_prop_string(ctx, -2, "y");

        duk_put_prop_string(ctx, -2, "tileGeometry");
    }

    if ((image->x_resolution != 0.0) && (image->y_resolution != 0.0))
    {

        duk_push_object(ctx); //resolution

        duk_push_number(ctx, image->x_resolution);
        duk_put_prop_string(ctx, -2, "x");
        duk_push_number(ctx, image->y_resolution);
        duk_put_prop_string(ctx, -2, "y");

        if (image->units == PixelsPerInchResolution)
        {
            duk_push_string(ctx, "inch");
            duk_put_prop_string(ctx, -2, "per");
        }
        else if (image->units == PixelsPerCentimeterResolution)
        {
            duk_push_string(ctx, "centimeter");
            duk_put_prop_string(ctx, -2, "per");
        }
        duk_put_prop_string(ctx, -2, "resolution");
    }

    duk_push_boolean(ctx, image->interlace == UndefinedInterlace? 0: 1);
    duk_put_prop_string(ctx, -2, "interlace");

    duk_push_string(ctx, OrientationTypeToString(image->orientation));
    duk_put_prop_string(ctx, -2, "orientation");

    (void) QueryColorname(image,&image->background_color,SVGCompliance,color,
                          &image->exception);
    duk_push_string(ctx, color);
    duk_put_prop_string(ctx, -2, "backgroundColor");

    (void) QueryColorname(image,&image->border_color,SVGCompliance,color,
                          &image->exception);
    duk_push_string(ctx, color);
    duk_put_prop_string(ctx, -2, "borderColor");

    (void) QueryColorname(image,&image->matte_color,SVGCompliance,color,
                          &image->exception);
    duk_push_string(ctx, color);
    duk_put_prop_string(ctx, -2, "matteColor");

    if ((image->page.width != 0) && (image->page.height != 0))
    {
        duk_push_object(ctx);// pageGeometry
        
        duk_push_int(ctx, (int)image->page.width);
        duk_put_prop_string(ctx, -2, "width");

        duk_push_int(ctx, (int)image->page.height);
        duk_put_prop_string(ctx, -2, "height");

        duk_push_int(ctx, (int)image->page.x);
        duk_put_prop_string(ctx, -2, "x");

        duk_push_int(ctx, (int)image->page.y);
        duk_put_prop_string(ctx, -2, "y");

        duk_put_prop_string(ctx, -2, "pageGeometry");
    }

    duk_push_string(ctx, CompositeOperatorToString(image->compose));
    duk_put_prop_string(ctx, -2, "compose");


    switch (image->dispose)
    {
        case UndefinedDispose: duk_push_string(ctx,"Undefined"); break;
        case NoneDispose: duk_push_string(ctx,"None"); break;
        case BackgroundDispose: duk_push_string(ctx,"Background"); break;
        case PreviousDispose: duk_push_string(ctx,"Previous"); break;
    }
    if(duk_is_string(ctx, -1))
      duk_put_prop_string(ctx, -2, "dispose");


    if (image->delay != 0)
    {
        duk_push_int(ctx, image->delay);
        duk_put_prop_string(ctx, -2, "delay");
    }

    if (image->iterations != 1)
    {
        duk_push_int(ctx, image->iterations);
        duk_put_prop_string(ctx, -2, "iterations");
    }

    duk_push_string(ctx, CompressionTypeToString(image->compression));
    duk_put_prop_string(ctx, -2, "compression");

    /*
      Get formatted image attributes. This must happen before we access
      any pseudo attributes like EXIF since doing so causes real attributes
      to be created and we would get duplicates in the output.
    */
    attribute=GetImageAttribute(image,(char *) NULL);
    {
        for ( ; attribute != (const ImageAttribute *) NULL;
            attribute=attribute->next)
          {
              if (LocaleNCompare("EXIF",attribute->key,4) != 0)
              {
                  duk_push_string(ctx, attribute->value);
                  duk_put_prop_string(ctx, -2, attribute->key);
              }
          }
    }

    if((profile=GetImageProfile(image,"ICM",&profile_length)) != 0)
    {
        duk_push_number(ctx, (double) profile_length);
        duk_put_prop_string(ctx, -2, "profileColor");
    }

    if((profile=GetImageProfile(image,"IPTC",&profile_length)) != 0)
    {
        char *tag;
        size_t length;

        /*
          Describe IPTC data.
        */

        duk_push_object(ctx); // profileIptc
        
        duk_push_number(ctx, (double) profile_length);
        duk_put_prop_string(ctx, -2, "bytes");

        for (i=0; i+5U < profile_length; )
        {
            if (profile[i] != 0x1c)
              {
                i++;
                continue;
              }
            i++;  /* skip file separator */
            i++;  /* skip record number */
            switch (profile[i])
            {
                case 5: tag=(char *) "imageName"; break;
                case 7: tag=(char *) "editStatus"; break;
                case 10: tag=(char *) "priority"; break;
                case 15: tag=(char *) "category"; break;
                case 20: tag=(char *) "supplementalCategory"; break;
                case 22: tag=(char *) "fixtureIdentifier"; break;
                case 25: tag=(char *) "keyword"; break;
                case 30: tag=(char *) "releaseDate"; break;
                case 35: tag=(char *) "releaseTime"; break;
                case 40: tag=(char *) "specialInstructions"; break;
                case 45: tag=(char *) "referenceService"; break;
                case 47: tag=(char *) "referenceDate"; break;
                case 50: tag=(char *) "referenceNumber"; break;
                case 55: tag=(char *) "createdDate"; break;
                case 60: tag=(char *) "createdTime"; break;
                case 65: tag=(char *) "originatingProgram"; break;
                case 70: tag=(char *) "programVersion"; break;
                case 75: tag=(char *) "objectCyc"; break;
                case 80: tag=(char *) "byline"; break;
                case 85: tag=(char *) "bylineTitle"; break;
                case 90: tag=(char *) "city"; break;
                case 95: tag=(char *) "provinceState"; break;
                case 100: tag=(char *) "countryCode"; break;
                case 101: tag=(char *) "country"; break;
                case 103: tag=(char *) "originalTransmissionReference"; break;
                case 105: tag=(char *) "headline"; break;
                case 110: tag=(char *) "credit"; break;
                case 115: tag=(char *) "source"; break;
                case 116: tag=(char *) "copyrightString"; break;
                case 120: tag=(char *) "caption"; break;
                case 121: tag=(char *) "localCaption"; break;
                case 122: tag=(char *) "caption Writer"; break;
                case 200: tag=(char *) "customField_1"; break;
                case 201: tag=(char *) "customField_2"; break;
                case 202: tag=(char *) "customField_3"; break;
                case 203: tag=(char *) "customField_4"; break;
                case 204: tag=(char *) "customField_5"; break;
                case 205: tag=(char *) "customField_6"; break;
                case 206: tag=(char *) "customField_7"; break;
                case 207: tag=(char *) "customField_8"; break;
                case 208: tag=(char *) "customField_9"; break;
                case 209: tag=(char *) "customField_10"; break;
                case 210: tag=(char *) "customField_11"; break;
                case 211: tag=(char *) "customField_12"; break;
                case 212: tag=(char *) "customField_13"; break;
                case 213: tag=(char *) "customField_14"; break;
                case 214: tag=(char *) "customField_15"; break;
                case 215: tag=(char *) "customField_16"; break;
                case 216: tag=(char *) "customField_17"; break;
                case 217: tag=(char *) "customField_18"; break;
                case 218: tag=(char *) "customField_19"; break;
                case 219: tag=(char *) "customField_20"; break;
                default: tag=(char *) "unknown"; break;
            }
            i++;
            length=(size_t) profile[i++] << 8;
            length|=(size_t) profile[i++];
            length=Min(length,profile_length-i);
            duk_push_lstring(ctx, (const char *)profile+i,length);
            duk_put_prop_string(ctx, -2, tag);
            i+=length;
        }
        duk_put_prop_string(ctx, -2, "profileIptc");
    }
    //exif
    {
      const char *profile_name;
      size_t profile_length;
      const unsigned char *profile_info;
      ImageProfileIterator profile_iterator;

      duk_push_object(ctx); // exif

      profile_iterator=AllocateImageProfileIterator(image);
      while(NextImageProfile(profile_iterator,&profile_name,&profile_info,
                             &profile_length) != MagickFail)
      {
          if ((LocaleCompare(profile_name,"ICC") == 0) ||
              (LocaleCompare(profile_name,"ICM") == 0) ||
              (LocaleCompare(profile_name,"IPTC") == 0) ||
              (LocaleCompare(profile_name,"8BIM") == 0))
            continue;

          if (profile_length == 0)
            continue;

          if (LocaleCompare(profile_name,"EXIF") == 0)
          {
              duk_push_number(ctx, (double)profile_length);
              duk_put_prop_string(ctx, -2, "bytes");

              attribute=GetImageAttribute(image,"EXIF:*");
              if (attribute != (const ImageAttribute *) NULL)
              {
                  double d;
                  char *values=strdup(attribute->value), 
                       *p=values, *v=NULL, *k=p, *p2;

                  while(1)
                  {
                      if(*p=='=')
                      {
                          *p='\0';
                          p++;
                          v=p;
                          continue;
                      }
                      if(*p == '\0')
                      {
                          if(v)
                          {
                              if(strncmp("GPS", k, 3))
                                  *k = tolower(*k);
                              d = strtod(v, &p2);
                              if(*p2=='\0')
                                  duk_push_number(ctx, d);
                              else
                                  duk_push_string(ctx, v);
                              duk_put_prop_string(ctx, -2, k);
                          }
                          break;
                      }
                      if(*p == '\n')
                      {
                          *p='\0';
                          if(v)
                          {
                              if(strncmp("GPS", k, 3))
                                  *k = tolower(*k);
                              d = strtod(v, &p2);
                              if(*p2=='\0')
                                  duk_push_number(ctx, d);
                              else
                                  duk_push_string(ctx, v);
                              duk_put_prop_string(ctx, -2, k);
                          }
                          v=NULL;
                          p++;
                          k=p;
                          continue;
                      }
                      p++;
                  }
                  free(values);
              }
          }
      }
      DeallocateImageProfileIterator(profile_iterator);
      duk_put_prop_string(ctx, -2, "exif");
    }



}

static duk_ret_t identify(duk_context *ctx)
{
    rpMW *rwand; 
    //FILE *desc=NULL;
    //char *buf=NULL;
    //size_t blen=0, i=0;
    int verbose=0;

    if(!duk_is_undefined(ctx,0))
        verbose=REQUIRE_BOOL(ctx, 0, "gm.identify - argument must be a boolean (verbose)" );

    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "Internal error getting gm wand");
    rwand = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!rwand)
        RP_THROW(ctx, "gm - error using a closed image handle");

    rp_describe_image(ctx, rwand->images, verbose?1:0 );
    return 1;

    /*
    desc = open_memstream(&buf, &blen);
    for (;i<DESC_INDENT_CHARS;i++)
        fputc(' ', desc);// put image name on same indent level as next
    DescribeImage( rwand->image, desc, verbose?100:0 );
    fputc(0,desc);
    fclose(desc);
    blen++;
    if(!verbose)
    {
        char *s=buf, *p=&buf[blen-2];
        blen-=3;
        while(isspace(*p))
        {
            p--;
            blen--;
        }
        while(*s== ' ')
        {
            s++;
            blen--;
        }
        duk_push_lstring(ctx, s, blen);
        free(buf);
        return 1;
    }
    duk_push_string(ctx, buf);
    push_parsed_desc(ctx, buf, blen);
    free(buf);
    duk_pull(ctx, -2);
    duk_put_prop_string(ctx, -2, "raw");
    return 1;
    */
}

static duk_ret_t gmgetcount(duk_context *ctx)
{
    MagickWand *wand;

    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "Internal error getting gm wand");
    wand = duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if(!wand)
        RP_THROW(ctx, "gm - error using a closed image handle");

    duk_push_int(ctx, getcount(wand) );

    return 1;
}

static void add_open_single(duk_context *ctx, MagickWand *wand, 
    duk_idx_t idx, duk_idx_t files_idx)
{
    MagickPassFail status = MagickPass;

    if(duk_is_buffer_data(ctx, idx))
    {
        status = addbuffer(ctx, wand, idx, files_idx);
    }
    else if (duk_is_object(ctx, idx) && !duk_is_array(ctx, idx) && !duk_is_function(ctx, idx))
    {
        status = addhandle(ctx, wand, idx, files_idx);
    }
    else
    {
        const char *fn = REQUIRE_STRING(ctx, idx, "gm.open/gm.add requires a string, image object or buffer");
        status = addfilename(ctx, wand, fn, files_idx);  
    }

    if(status != MagickPass)
        throw_exception(ctx, wand, 1);

}
    
static duk_ret_t open(duk_context *ctx)
{
    MagickWand *wand;
    int isadd=0;
    duk_idx_t files_idx, this_idx;


    duk_push_this(ctx);
    if(duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
    {
        this_idx=duk_get_top_index(ctx);
        isadd=1;
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand"));
        wand = duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("files"));
        files_idx=duk_get_top_index(ctx);
    }
    else
    {
        duk_pop(ctx);
        duk_push_object(ctx);
        this_idx=duk_get_top_index(ctx);
        duk_push_array(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("files"));
        files_idx=duk_get_top_index(ctx);
        wand=NewMagickWand();
    }
        
    if(duk_is_array(ctx, 0))
    {
        duk_uarridx_t i, len = duk_get_length(ctx, 0);

        for (i=0; i<len; i++)
        {
            duk_get_prop_index(ctx, 0, i);
            add_open_single(ctx, wand, -1, files_idx);
            duk_pop(ctx);
        }
    }
    else
        add_open_single(ctx, wand, 0, files_idx); 

    duk_pull(ctx, this_idx);
    if(!isadd)
    {
        duk_push_pointer(ctx, wand);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("wand"));

        duk_push_c_function(ctx, mogrify, 2);
        duk_put_prop_string(ctx, -2, "mogrify");

        duk_push_c_function(ctx, save, 1);
        duk_put_prop_string(ctx, -2, "save");

        duk_push_c_function(ctx, open, 1);
        duk_put_prop_string(ctx, -2, "add");

        duk_push_c_function(ctx, list, 0);
        duk_put_prop_string(ctx, -2, "list");

        duk_push_c_function(ctx, gmgetcount, 0);
        duk_put_prop_string(ctx, -2, "getCount");

        duk_push_c_function(ctx, gmselect, 1);
        duk_put_prop_string(ctx, -2, "select");

        duk_push_c_function(ctx, tobuffer, 1);
        duk_put_prop_string(ctx, -2, "toBuffer");

        duk_push_c_function(ctx, identify, 1);
        duk_put_prop_string(ctx, -2, "identify");

        duk_push_c_function(ctx, gmfinal, 1);
        duk_set_finalizer(ctx, -2); 

        duk_push_c_function(ctx, gmclose, 0);
        duk_put_prop_string(ctx, -2, "close");
    }
    return 1;
}



/* **************************************************
   Initialize module
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    static int is_init=0;
    /* the return object when var mod=require("gm") is called. */
    duk_push_object(ctx);

    /* initialize once */
    if(!is_init) {
        InitializeMagick(NULL);
        is_init=1;
    }

    /* js function is mod.mogrify and it calls mogrify */
    duk_push_c_function(ctx, open, 1);
    duk_put_prop_string(ctx, -2, "open");

    return 1;
}
