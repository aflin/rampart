/* ****************************************************************************
    rampart-gm.so module
    copyright (c) 2024 Aaron Flin
    Released under MIT license

    Compiling:
    
    > sudo apt install libgraphicsmagick1-dev
    # or
    > brew install graphicsmagick

    > make
    > cp rampart-gm.so /usr/local/rampart/modules/ 
    # or wherever your modules live, like ~/.rampart/modules
    Basic usage:

    var gm = require("rampart-gm");

    // ---- OPEN ----
    // get an image object by opening image
    var images = gm.open("/path/to/my/image.jpg");
    var image2 = gm.open("/path/to/my/image2.jpg");

    // ---- ADD ----
    // image.add([ imageObject | String(path) | buffer(i.e. img.toBuffer() ])
    // add some image
    images.add("/path/to/my/image2.jpg");   //or
    images.add(image2);                     //or
    images.add(image2.toBuffer('JPG'));

    // ---- MOGRIFY ---
    //mogrify (see GraphicsMagick for all options)
    //if object used, single options must be paired with true
    images.mogrify("-blur 20x30");      //or
    images.mogrify("-blur", "20x30");   //or
    images.mogrify("blur", "20x30");    //or
    images.mogrify({"-blur", "20x30);   //or
    images.mogrify({"blur", "20x30);

    //example 2:
    images.mogrify("-blur 20x30 -auto-orient +contrast");      //or
    images.mogrify({
        blur: "20x30, 
        "auto-orient": true,
        "+contrast": true
    ]);
    // note setting single options to false skips the option and does nothing

    // ---- SAVE ----
    images.save("/path/to/my/new_image.jpg");

    // ---- TOBUFFER ----
    // save file to a js buffer in the specified format
    var buf = images.toBuffer(["jpg" | "PNG" | "GIF" | ... ]);

    // So this now accomplishes the same as save
    fprintf("/path/to/my/new_image.jpg", "%s", buf);

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
    printf("%3J\n", images.list());

    // ---- GETCOUNT ----
    //get a count of images in the image object
    printf("we have %d images\n", images.getCount());
    
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
        //printf("NO FILE %d %s %s\n",exception->severity,exception->reason,exception->description);
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
    //(void) duk_throw(ctx);
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
    unsigned int n=0;//, save;
    rpMW *rwand = (rpMW *) wand;
    Image *image;

    // getting current index in a new wand with no current images does something that prevents
    // adding images: "Error: Wand contains no image indices `(null) (MagickWand-1)"
    if(!rwand->images) {
        //printf("getcount no images, ret 0\n");
        return 0;
    }
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

    /*
    int i=0;
    for (;i<argc;i++){
        printf("%d: '%s'\n",i,argv[i]);
    }
    */

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

        /*{
            int i=0;
            for (;i<argc+1;i++){
                printf("%d: '%s'\n",i,argv[i]);
            }
        }*/

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
                if(push_mog_exception(ctx, &(rwand->exception)))
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
    MagickPassFail status = MagickPass;

    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("wand")))
        RP_THROW(ctx, "Internal error getting gm wand");
    wand = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!wand)
        RP_THROW(ctx, "gm - error using a closed image handle");
    /*
    int n=1;
    MagickSetImageIndex(wand,0);
    while(MagickNextImage(wand)) n++;
    printf("num images = %d\n", n);
    */

    MagickSetImageIndex(wand,0);
    
    if(MagickNextImage(wand))
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

    MagickSetImageIndex(wand,0);

    if(MagickNextImage(wand))
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
    
static duk_ret_t open(duk_context *ctx)
{
    MagickWand *wand;
    MagickPassFail status = MagickPass;
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
        

    duk_idx_t idx=0;

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
